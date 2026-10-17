#include "lumenis/media/managed.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPainter>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUuid>

#include <algorithm>

namespace lumenis::media {

namespace {

// The metadata that says which clip and track a file is (MXF user comments).
const QString kIdKey = QStringLiteral("comment_lumenis_media");
const QString kTrackKey = QStringLiteral("comment_lumenis_track");
const QString kProjectKey = QStringLiteral("comment_lumenis_project");

// Frames to samples at `sampleRate`, exact in integers (a * b / c).
std::int64_t samplesAt(std::int64_t frames, const Rational& rate, int sampleRate) {
    if (rate.num() <= 0) return 0;
    return frames * sampleRate * rate.den() / rate.num();
}

QString rateText(const Rational& r) {
    return QStringLiteral("%1/%2").arg(r.num()).arg(r.den());
}

QString fpsText(double fps) {
    QString s = QString::number(fps, 'f', 3);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

// A clip name made safe for a file name on every platform.
QString fileStem(const QString& name) {
    QString s = name;
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith(QLatin1Char('.'))) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("Clip");
    return s.left(80);
}

QString driveName(const QString& path) {
    const QStorageInfo volume(path);
    if (!volume.isValid()) return {};
    return volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName();
}

// The picture at the raster, keeping its shape, on black.
QImage fitted(const QImage& image, QSize raster) {
    if (image.size() == raster) return image;
    QImage out(raster, QImage::Format_RGBA8888);
    out.fill(Qt::black);
    if (image.isNull()) return out;
    const QSize s = image.size().scaled(raster, Qt::KeepAspectRatio);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(QPoint((raster.width() - s.width()) / 2, (raster.height() - s.height()) / 2), s), image);
    return out;
}

std::optional<std::int64_t> startFrame(const model::BinItem& clip) {
    const QString text = clip.fields.value(QStringLiteral("Start"));
    if (text.isEmpty()) return std::nullopt;
    const auto tc = Timecode::parse(text.toStdString(), TimecodeFormat::forRate(clip.editRate, false));
    if (!tc) return std::nullopt;
    return tc->frameCount();
}

QString timecodeText(std::int64_t frames, const Rational& rate) {
    const TimecodeFormat format = TimecodeFormat::forRate(rate, false);
    return QString::fromStdString(Timecode(frames % format.framesPerDay(), format).toString());
}

QString trackList(bool video, int channels) {
    QStringList parts;
    if (video) parts << QStringLiteral("V1");
    if (channels == 1) parts << QStringLiteral("A1");
    if (channels > 1) parts << QStringLiteral("A1-%1").arg(channels);
    return parts.join(QLatin1Char(' '));
}

// Writes one track's media; the files made so far are removed on failure.
class Maker {
public:
    Maker(const model::BinItem& clip, const Link& link, std::int64_t in, std::int64_t out,
          const MediaCreation& creation)
        : clip_(clip), link_(link), in_(in), out_(out), creation_(creation) {
        id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const std::optional<std::int64_t> start = startFrame(clip);
        startTc_ = timecodeText(start.value_or(0) + in, clip.editRate);
    }
    ~Maker() {
        if (!kept_) {
            for (const QString& f : made_) QFile::remove(f);
        }
    }

    const QString& id() const { return id_; }
    const QString& startTimecode() const { return startTc_; }
    void keep() { kept_ = true; }
    void setWork(std::int64_t total) { total_ = std::max<std::int64_t>(total, 1); }

    // A new file's path for a track in a folder.
    QString pathFor(const QString& folder, const QString& track) {
        // "<clip>.<track>.<id>.mxf": unique, and readable in a file browser.
        QString p = QStringLiteral("%1/%2.%3.%4.mxf").arg(folder, fileStem(clip_.name), track, id_.left(8));
        made_ << p;
        return p;
    }

    WriterSettings settingsFor(const QString& track) const {
        WriterSettings s;
        s.format = QStringLiteral("mxf_opatom");
        s.frameRate = clip_.editRate;
        s.startTimecode = startTc_;
        s.reelName = clip_.fields.value(QStringLiteral("Tape"));
        s.metadata = {{QStringLiteral("material_package_name"), clip_.name},
                      {kIdKey, id_},
                      {kTrackKey, track}};
        if (!creation_.project.isEmpty()) s.metadata.append({kProjectKey, creation_.project});
        return s;
    }

    // Counts work done; false when cancelled.
    bool step(std::int64_t units = 1) {
        done_ += units;
        if (!creation_.progress) return true;
        return creation_.progress(std::min(1.0, static_cast<double>(done_) / static_cast<double>(total_)));
    }

    bool transcodeVideo(const QString& path, QString* error) {
        VideoReader reader;
        if (!reader.open(link_.path, link_.videoStream, error)) return false;
        WriterSettings s = settingsFor(QStringLiteral("V1"));
        s.videoCodec = QStringLiteral("dnxhd");
        s.videoProfile = creation_.resolution.profile;
        s.pixelFormat = creation_.resolution.pixelFormat;
        s.width = creation_.raster.width();
        s.height = creation_.raster.height();
        MediaWriter writer;
        if (!writer.open(path, s, error)) return false;
        for (std::int64_t f = in_; f < out_; ++f) {
            const QImage frame = reader.frame(link_.fileFrame(f, clip_.editRate));
            if (!writer.writeVideo(fitted(frame, creation_.raster))) {
                if (error) *error = writer.lastError();
                return false;
            }
            if (!step()) return cancelled(error);
        }
        return writer.finish(error);
    }

    bool copyVideo(const QString& path, QString* error) {
        WriterSettings s = settingsFor(QStringLiteral("V1"));
        if (!copyVideoFrames(link_.path, link_.videoStream, link_.fileFrame(in_, clip_.editRate), out_ - in_, path, s,
                             error)) {
            return false;
        }
        return step(out_ - in_) || cancelled(error);
    }

    // Track `t` (0 for A1) as mono PCM.
    bool writeAudio(int t, const QString& path, QString* error) {
        const auto channel = link_.channelOf(t);
        if (!channel) {
            if (error) *error = QObject::tr("%1 has no track A%2.").arg(clip_.name).arg(t + 1);
            return false;
        }
        AudioReader reader;
        if (!reader.open(link_.audioFile(t), channel->first, error)) return false;
        const int rate = reader.sampleRate();
        const int channels = std::max(reader.channels(), 1);
        const int c = std::clamp(channel->second, 0, channels - 1);
        const std::int64_t wanted = samplesAt(out_ - in_, clip_.editRate, kManagedSampleRate);
        WriterSettings s = settingsFor(QStringLiteral("A%1").arg(t + 1));
        s.audioCodec = creation_.bitDepth <= 16 ? QStringLiteral("pcm_s16le") : QStringLiteral("pcm_s24le");
        s.sampleRate = kManagedSampleRate;
        s.channels = 1;
        // The edit rate the file's timecode counts in.
        s.formatOptions = {{QStringLiteral("mxf_audio_edit_rate"), rateText(clip_.editRate)}};
        MediaWriter writer;
        if (!writer.open(path, s, error)) return false;
        std::unique_ptr<Resampler> resampler;
        if (rate != kManagedSampleRate) resampler = std::make_unique<Resampler>(1, rate, kManagedSampleRate);
        std::int64_t at = samplesAt(in_, clip_.editRate, rate); // in the file
        const std::int64_t end = samplesAt(out_, clip_.editRate, rate);
        std::int64_t written = 0;
        auto put = [&](std::vector<float> mono) {
            const std::int64_t n = std::min<std::int64_t>(static_cast<std::int64_t>(mono.size()), wanted - written);
            if (n <= 0) return true;
            written += n;
            return writer.writeAudio(mono.data(), static_cast<int>(n));
        };
        const int chunk = std::max(rate / 4, 1);
        const std::int64_t perStep = samplesAt(1, clip_.editRate, rate) > 0 ? samplesAt(1, clip_.editRate, rate) : 1;
        while (at < end) {
            const int n = static_cast<int>(std::min<std::int64_t>(chunk, end - at));
            std::vector<float> got = reader.samples(at, n);
            const std::size_t frames = got.size() / static_cast<std::size_t>(channels);
            std::vector<float> mono(static_cast<std::size_t>(n), 0.0f); // silence past the file's end
            for (std::size_t i = 0; i < frames && i < mono.size(); ++i) {
                mono[i] = got[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
            }
            if (resampler) mono = resampler->push(mono.data(), n);
            if (!put(std::move(mono))) {
                if (error) *error = writer.lastError();
                return false;
            }
            at += n;
            if (!step(std::max<std::int64_t>(n / perStep, 1))) return cancelled(error);
        }
        if (resampler && !put(resampler->flush())) return false;
        // Exactly the frames' length.
        if (written < wanted) {
            const std::vector<float> silence(static_cast<std::size_t>(wanted - written), 0.0f);
            if (!put(silence)) return false;
        }
        return writer.finish(error);
    }

    static bool cancelled(QString* error) {
        if (error) *error = QObject::tr("Cancelled.");
        return false;
    }

private:
    const model::BinItem& clip_;
    const Link& link_;
    std::int64_t in_;
    std::int64_t out_;
    const MediaCreation& creation_;
    QString id_;
    QString startTc_;
    QStringList made_;
    bool kept_ = false;
    std::int64_t total_ = 1;
    std::int64_t done_ = 0;
};

std::optional<model::BinItem> makeMedia(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                        const MediaCreation& creation, bool copy, QString* error) {
    auto failed = [&](const QString& text) -> std::optional<model::BinItem> {
        if (error) *error = text;
        return std::nullopt;
    };
    const auto link = linkOf(clip);
    if (!link || link->unlinked) return failed(QObject::tr("%1 has no media.").arg(clip.name));
    if (out < 0 || out > clip.durationFrames) out = clip.durationFrames;
    in = std::clamp<std::int64_t>(in, 0, out);
    if (out <= in) return failed(QObject::tr("%1 has no frames to make media of.").arg(clip.name));
    if (creation.videoDrive.isEmpty()) return failed(QObject::tr("No target drive is chosen."));
    if (!copy && (creation.raster.width() < kMinimumRaster.width() || creation.raster.height() < kMinimumRaster.height())) {
        return failed(QObject::tr("The raster must be at least %1x%2.")
                          .arg(kMinimumRaster.width())
                          .arg(kMinimumRaster.height()));
    }

    const bool video = link->videoStream >= 0;
    const int channels = link->channels;
    QString videoFolder;
    QString audioFolder;
    if (video) {
        videoFolder = mxfFolder(creation.videoDrive, error);
        if (videoFolder.isEmpty()) return std::nullopt;
    }
    if (channels > 0) {
        audioFolder = mxfFolder(creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive, error);
        if (audioFolder.isEmpty()) return std::nullopt;
    }

    Maker maker(clip, *link, in, out, creation);
    maker.setWork((out - in) * ((video ? 1 : 0) + channels));
    Link made;
    made.rate = clip.editRate;
    made.frames = out - in;
    made.mediaId = maker.id();
    QString err;
    if (video) {
        const QString path = maker.pathFor(videoFolder, QStringLiteral("V1"));
        const bool ok = copy ? maker.copyVideo(path, &err) : maker.transcodeVideo(path, &err);
        if (!ok) return failed(err);
        made.path = path;
        made.videoStream = 0;
    }
    for (int t = 0; t < channels; ++t) {
        const QString path = maker.pathFor(audioFolder, QStringLiteral("A%1").arg(t + 1));
        if (!maker.writeAudio(t, path, &err)) return failed(err);
        if (made.path.isEmpty()) made.path = path;
        made.audioPaths << path;
        made.audioStreams << 0;
        made.streamChannels << 1;
        made.channels += 1;
    }
    maker.keep();

    model::BinItem item;
    item.id = model::newItemId();
    item.kind = video ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;
    item.name = clip.name;
    item.created = creation.when.isValid() ? creation.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = clip.editRate;
    item.durationFrames = out - in;
    item.color = clip.color;
    item.link = made.toJson();
    // The columns that still describe it.
    for (const char* heading : {"Tape", "Source File", "Source Path", "Camera", "Scene", "Take", "Comments"}) {
        const QString key = QString::fromLatin1(heading);
        if (clip.fields.contains(key)) item.fields.insert(key, clip.fields.value(key));
    }
    auto set = [&](const char* heading, const QString& value) {
        if (!value.isEmpty()) item.fields.insert(QString::fromLatin1(heading), value);
    };
    const std::int64_t start = startFrame(clip).value_or(0) + in;
    set("Start", timecodeText(start, clip.editRate));
    set("End", timecodeText(start + item.durationFrames, clip.editRate));
    set("Tracks", trackList(video, made.channels));
    if (video) {
        const double fps = clip.editRate.toDouble();
        set("FPS", fpsText(fps));
        if (copy) {
            for (const char* heading : {"Video", "Format", "Image Aspect Ratio", "Pixel Aspect Ratio", "Color Space"}) {
                set(heading, clip.fields.value(QString::fromLatin1(heading)));
            }
        } else {
            set("Video", creation.resolution.name);
            set("Format", QStringLiteral("%1p/%2").arg(creation.raster.height()).arg(fpsText(fps)));
            set("Color Space", creation.resolution.rgb ? QStringLiteral("RGB 709") : QStringLiteral("YCbCr 709"));
            set("Image Aspect Ratio", clip.fields.value(QStringLiteral("Image Aspect Ratio")));
        }
        set("Drive", driveName(videoFolder));
    } else {
        set("Drive", driveName(audioFolder));
    }
    if (made.channels > 0) {
        set("Audio SR", QString::number(kManagedSampleRate));
        set("Audio Bit Depth", QStringLiteral("%1 bit").arg(creation.bitDepth <= 16 ? 16 : 24));
        set("Audio Format", QStringLiteral("PCM"));
    }
    set("Video File Format", QStringLiteral("MXF"));
    return item;
}

} // namespace

const QList<Resolution>& resolutions() {
    // The quality levels p.1240 names, in order of quality.
    static const QList<Resolution> list{
        {QStringLiteral("dnx_lb"), QStringLiteral("Avid DNx LB"), QStringLiteral("dnxhr_lb"), QStringLiteral("yuv422p"),
         false},
        {QStringLiteral("dnx_sq"), QStringLiteral("Avid DNx SQ"), QStringLiteral("dnxhr_sq"), QStringLiteral("yuv422p"),
         false},
        {QStringLiteral("dnx_hq"), QStringLiteral("Avid DNx HQ"), QStringLiteral("dnxhr_hq"), QStringLiteral("yuv422p"),
         false},
        {QStringLiteral("dnx_hqx"), QStringLiteral("Avid DNx HQX"), QStringLiteral("dnxhr_hqx"),
         QStringLiteral("yuv422p10le"), false},
        {QStringLiteral("dnx_444"), QStringLiteral("Avid DNx 444"), QStringLiteral("dnxhr_444"),
         QStringLiteral("gbrp10le"), true},
    };
    return list;
}

const Resolution* findResolution(const QString& idOrName) {
    for (const Resolution& r : resolutions()) {
        if (r.id == idOrName || r.name == idOrName) return &r;
    }
    return nullptr;
}

const Resolution& defaultResolution() {
    return resolutions().at(2);
}

QList<MediaDrive> mediaDrives(const QStringList& extra) {
    QList<MediaDrive> out;
    auto add = [&](const QString& root, const QString& name, std::int64_t free) {
        const QString clean = QDir::cleanPath(root);
        for (const MediaDrive& d : out) {
            if (d.root == clean) return;
        }
        out.append({name, clean, free});
    };
    static const QStringList skippedTypes{QStringLiteral("tmpfs"), QStringLiteral("devtmpfs"),
                                          QStringLiteral("squashfs"), QStringLiteral("proc"),
                                          QStringLiteral("sysfs"), QStringLiteral("efivarfs"),
                                          QStringLiteral("overlay"), QStringLiteral("autofs")};
    static const QStringList skippedRoots{QStringLiteral("/boot"), QStringLiteral("/snap"), QStringLiteral("/run"),
                                          QStringLiteral("/dev"), QStringLiteral("/sys"), QStringLiteral("/proc"),
                                          QStringLiteral("/System/Volumes")};
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.isReadOnly()) continue;
        if (skippedTypes.contains(QString::fromUtf8(v.fileSystemType()))) continue;
        const QString root = v.rootPath();
        if (std::any_of(skippedRoots.begin(), skippedRoots.end(),
                        [&](const QString& s) { return root == s || root.startsWith(s + QLatin1Char('/')); })) {
            continue;
        }
        if (!QFileInfo(root).isWritable()) continue;
        add(root, v.displayName().isEmpty() ? root : v.displayName(), v.bytesAvailable());
    }
    // Users cannot write at the top of the system volume on macOS and
    // Linux: their home folder stands for it.
    const QString home = QDir::homePath();
    const QStorageInfo homeVolume(home);
    if (homeVolume.isValid() && !QFileInfo(homeVolume.rootPath()).isWritable()) {
        add(home, QDir::toNativeSeparators(home), homeVolume.bytesAvailable());
    }
    for (const QString& folder : extra) {
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) continue;
        add(folder, QDir::toNativeSeparators(folder), QStorageInfo(folder).bytesAvailable());
    }
    return out;
}

namespace {
QStringList& extraDrives() {
    static QStringList folders = [] {
        QStringList list;
        const QString env = qEnvironmentVariable("LUMENIS_MEDIA_DRIVES");
        for (const QString& f : env.split(QDir::listSeparator(), Qt::SkipEmptyParts)) list << QDir::cleanPath(f);
        return list;
    }();
    return folders;
}
} // namespace

void setExtraMediaDrives(const QStringList& folders) {
    extraDrives() = folders;
}

QStringList extraMediaDrives() {
    return extraDrives();
}

QList<MediaDrive> mediaDrives() {
    return mediaDrives(extraDrives());
}

QString chooseDrive(const QString& setting) {
    const QList<MediaDrive> drives = mediaDrives();
    const QString clean = setting.isEmpty() ? QString() : QDir::cleanPath(setting);
    for (const MediaDrive& d : drives) {
        if (d.root == clean) return d.root;
    }
    const int best = mostSpace(drives);
    return best >= 0 ? drives.at(best).root : QString();
}

int mostSpace(const QList<MediaDrive>& drives) {
    int best = -1;
    for (int i = 0; i < drives.size(); ++i) {
        if (best < 0 || drives.at(i).freeBytes > drives.at(best).freeBytes) best = i;
    }
    return best;
}

QString mediaFilesFolder(const QString& drive) {
    return QDir(drive).filePath(QStringLiteral("Avid MediaFiles"));
}

QString mxfFolder(const QString& drive, QString* error) {
    const QDir mxf(QDir(mediaFilesFolder(drive)).filePath(QStringLiteral("MXF")));
    int highest = 0;
    for (const QString& name : mxf.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int n = name.toInt(&ok);
        if (ok && n > highest) highest = n;
    }
    const QString folder = mxf.filePath(QString::number(std::max(highest, 1)));
    if (!QDir().mkpath(folder)) {
        if (error) *error = QObject::tr("Cannot make the folder %1.").arg(QDir::toNativeSeparators(folder));
        return {};
    }
    return folder;
}

std::optional<model::BinItem> transcodeClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                            const MediaCreation& creation, QString* error) {
    return makeMedia(clip, in, out, creation, false, error);
}

bool canConsolidate(const model::BinItem& clip) {
    const auto link = linkOf(clip);
    if (!link || link->unlinked) return false;
    if (link->videoStream < 0) return true;
    // Frame for frame: the file at the clip's rate.
    if (!(link->rate == clip.editRate) || ImageSequence::fromSpec(link->path)) return false;
    const auto info = probe(link->path);
    if (!info) return false;
    for (const StreamInfo& s : info->streams) {
        if (s.index == link->videoStream) return s.codec == QLatin1String("dnxhd");
    }
    return false;
}

std::optional<model::BinItem> consolidateClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                              const MediaCreation& creation, QString* error) {
    if (!canConsolidate(clip)) {
        if (error) *error = QObject::tr("%1's media cannot be copied as it is; transcode it.").arg(clip.name);
        return std::nullopt;
    }
    return makeMedia(clip, in, out, creation, true, error);
}

std::optional<MediaFileInfo> readMediaFile(const QString& path) {
    const auto info = probe(path);
    if (!info) return std::nullopt;
    MediaFileInfo f;
    f.path = path;
    f.mediaId = info->metadata.value(kIdKey);
    f.track = info->metadata.value(kTrackKey);
    f.clipName = info->metadata.value(QStringLiteral("material_package_name"));
    f.project = info->metadata.value(kProjectKey);
    if (f.mediaId.isEmpty() || f.track.isEmpty()) return std::nullopt;
    return f;
}

void MediaDatabase::clear() {
    files_.clear();
}

void MediaDatabase::scan(const QStringList& drives) {
    for (const QString& drive : drives) {
        const QDir mxf(QDir(mediaFilesFolder(drive)).filePath(QStringLiteral("MXF")));
        for (const QString& sub : mxf.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir folder(mxf.filePath(sub));
            // The folder's index: the files read before, by name, with the
            // size and time they had then.
            const QString indexPath = folder.filePath(indexName());
            QJsonObject index;
            if (QFile f(indexPath); f.open(QIODevice::ReadOnly)) {
                index = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("files")).toObject();
            }
            QJsonObject fresh;
            bool changed = false;
            const QFileInfoList entries = folder.entryInfoList({QStringLiteral("*.mxf"), QStringLiteral("*.MXF")},
                                                               QDir::Files);
            for (const QFileInfo& fi : entries) {
                QJsonObject e = index.value(fi.fileName()).toObject();
                const qint64 size = fi.size();
                const qint64 time = fi.lastModified().toMSecsSinceEpoch();
                if (e.value(QStringLiteral("size")).toInteger() != size ||
                    e.value(QStringLiteral("time")).toInteger() != time) {
                    const auto m = readMediaFile(fi.absoluteFilePath());
                    e = QJsonObject{{QStringLiteral("size"), size}, {QStringLiteral("time"), time}};
                    if (m) {
                        e.insert(QStringLiteral("id"), m->mediaId);
                        e.insert(QStringLiteral("track"), m->track);
                        e.insert(QStringLiteral("clip"), m->clipName);
                    }
                    changed = true;
                }
                fresh.insert(fi.fileName(), e);
                const QString id = e.value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) files_[id].insert(e.value(QStringLiteral("track")).toString(), fi.absoluteFilePath());
            }
            if (changed || fresh.size() != index.size()) {
                QFile f(indexPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("files"), fresh}})
                                .toJson(QJsonDocument::Indented));
                }
            }
        }
    }
}

QHash<QString, QString> MediaDatabase::filesOf(const QString& mediaId) const {
    return files_.value(mediaId);
}

int MediaDatabase::fileCount() const {
    int n = 0;
    for (const auto& tracks : files_) n += static_cast<int>(tracks.size());
    return n;
}

bool MediaDatabase::resolve(model::BinItem& item) const {
    auto link = linkOf(item);
    if (!link || !link->isManaged()) return false;
    const QHash<QString, QString> files = filesOf(link->mediaId);
    if (files.isEmpty()) return false;
    Link l = *link;
    if (l.videoStream >= 0) {
        const QString v = files.value(QStringLiteral("V1"));
        if (v.isEmpty()) return false;
        l.path = v;
    }
    for (int t = 0; t < l.audioPaths.size(); ++t) {
        const QString a = files.value(QStringLiteral("A%1").arg(t + 1));
        if (a.isEmpty()) return false;
        l.audioPaths[t] = a;
    }
    if (l.videoStream < 0 && !l.audioPaths.isEmpty()) l.path = l.audioPaths.constFirst();
    if (l.path == link->path && l.audioPaths == link->audioPaths) return true;
    item.link = l.toJson();
    return true;
}

} // namespace lumenis::media
