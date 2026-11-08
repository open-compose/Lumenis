#include "lumenis/media/managed.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/color_transform.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPainter>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QUuid>

#include <algorithm>

// Managed media (ROADMAP Phase 2.4a/b): media the application writes itself, as MXF OP-Atom files,
// one per track, in "<drive>/Avid MediaFiles/MXF/<n>/". This file holds:
// - the resolutions (Avid DNx levels through FFmpeg's DNxHR encoder) and the media drives;
// - makeMedia(), behind transcodeClip() (Transcode, Import) and consolidateClip() (Consolidate):
//   the Maker class writes each track through MediaWriter or copyVideoFrames() (media.cpp), with
//   the metadata the media database and relinking read back, and the clip's MobIDs as the files'
//   package IDs (docs/spec/native-model.md);
// - readMediaFile() and MediaDatabase, the index of the managed files on the drives (one
//   MediaDatabase.json per MXF folder), and deleteMediaFiles().
// Callers: consolidate.cpp and relink.cpp here; in src/ui, main_window.cpp (Import, Consolidate/
// Transcode, Delete Media), consolidate_dialog.cpp, settings_window.cpp (drive settings),
// and media_tool.cpp. Tests pick drives with setExtraMediaDrives() or
// LUMENIS_MEDIA_DRIVES (BUILDING.md); scripts/check-mxf.py checks the files with bmx.

namespace lumenis::media {

namespace {

// The metadata that says which clip and track a file is (MXF user comments).
const QString kIdKey = QStringLiteral("comment_lumenis_media");
const QString kTrackKey = QStringLiteral("comment_lumenis_track");
const QString kProjectKey = QStringLiteral("comment_lumenis_project");
const QString kSourceKey = QStringLiteral("comment_lumenis_source");
const QString kFramesKey = QStringLiteral("comment_lumenis_frames");
const QString kRateKey = QStringLiteral("comment_lumenis_rate");
const QString kResolutionKey = QStringLiteral("comment_lumenis_resolution");
// The index's layout; an index of another version is read again.
constexpr int kIndexVersion = 3; // 3: each file's material package MobID

// Frames to samples at `sampleRate`, exact in integers (a * b / c).
std::int64_t samplesAt(std::int64_t frames, const Rational& rate, int sampleRate) {
    if (rate.num() <= 0) return 0;
    return frames * sampleRate * rate.den() / rate.num();
}

// "num/den", as the files and the index keep a rate.
QString rateText(const Rational& r) {
    return QStringLiteral("%1/%2").arg(r.num()).arg(r.den());
}

// 23.976, 25, 29.97: the rate as the FPS column shows it.
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

// The volume's name for the Drive column; its root when it has none.
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

// The clip's Start column as a frame count at its edit rate; nothing without one.
std::optional<std::int64_t> startFrame(const model::BinItem& clip) {
    const QString text = clip.fields.value(QStringLiteral("Start"));
    if (text.isEmpty()) return std::nullopt;
    const auto tc = Timecode::parse(text.toStdString(), TimecodeFormat::forRate(clip.editRate, false));
    if (!tc) return std::nullopt;
    return tc->frameCount();
}

// Frames from midnight as non-drop timecode at the rate, wrapped at 24 hours.
QString timecodeText(std::int64_t frames, const Rational& rate) {
    const TimecodeFormat format = TimecodeFormat::forRate(rate, false);
    return QString::fromStdString(Timecode(frames % format.framesPerDay(), format).toString());
}

// "V1 A1-2", as the Tracks column shows tracks (p.278).
QString trackList(bool video, int channels) {
    QStringList parts;
    if (video) parts << QStringLiteral("V1");
    if (channels == 1) parts << QStringLiteral("A1");
    if (channels > 1) parts << QStringLiteral("A1-%1").arg(channels);
    return parts.join(QLatin1Char(' '));
}

// Writes one track's media; the files made so far are removed on failure.
// One Maker makes one clip's media: it holds the media id (a UUID shared by every track file), the
// master clip's MobID, a file MobID per track and the source the files came from. Frames [in, out)
// are clip frames at the clip's edit rate; Link::fileFrame() maps them to the file's frames.
// Progress counts frames per track; MediaCreation::progress returning false cancels.
class Maker {
public:
    Maker(const model::BinItem& clip, const Link& link, std::int64_t in, std::int64_t out,
          const MediaCreation& creation)
        : clip_(clip), link_(link), in_(in), out_(out), creation_(creation), created_(QDateTime::currentDateTimeUtc()) {
        id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!creation.master.isNull()) master_ = creation.master;
        const std::optional<std::int64_t> start = startFrame(clip);
        startTc_ = timecodeText(start.value_or(0) + in, clip.editRate);
        // The source before the files: the clip's own tape or import source
        // when it has one, else a new one (docs/spec/native-model.md).
        origin_.kind = clip.fields.value(QStringLiteral("Tape")).isEmpty() ? model::ClipSource::Kind::Import
                                                                         : model::ClipSource::Kind::Tape;
        for (const model::ClipSource& s : clip.sources) {
            if (s.kind == model::ClipSource::Kind::Tape || s.kind == model::ClipSource::Kind::Import) {
                origin_ = s;
                break;
            }
        }
        if (origin_.mob.isNull()) {
            origin_.mob = MobId::generate();
            origin_.name = origin_.kind == model::ClipSource::Kind::Tape ? clip.fields.value(QStringLiteral("Tape"))
                                                                         : QFileInfo(link.path).fileName();
            if (origin_.kind == model::ClipSource::Kind::Import) origin_.path = link.path;
            origin_.startTimecode = timecodeText(start.value_or(0), clip.editRate);
        }
    }
    ~Maker() {
        if (!kept_) {
            for (const QString& f : made_) QFile::remove(f);
        }
    }

    const QString& id() const { return id_; }
    const MobId& master() const { return master_; }
    const model::ClipSource& origin() const { return origin_; }
    // A track's file source MobID, made the first time it is asked for.
    const MobId& fileMob(const QString& track) {
        auto it = files_.find(track);
        if (it == files_.end()) it = files_.insert(track, MobId::generate());
        return *it;
    }
    const QString& startTimecode() const { return startTc_; }
    void keep() { kept_ = true; }
    void setWork(std::int64_t total) { total_ = std::max<std::int64_t>(total, 1); }

    // A new file's path for a track in a folder.
    QString pathFor(const QString& folder, const QString& track) {
        fileMob(track); // the file's MobID, written into it
        // "<clip>.<track>.<id>.mxf": unique, and readable in a file browser. Sound at another rate than
        // MXF takes is WAV.
        const bool wav = track.startsWith(QLatin1Char('A')) && targetRate() != kManagedSampleRate;
        QString p = QStringLiteral("%1/%2.%3.%4.%5")
                        .arg(folder, fileStem(clip_.name), track, id_.left(8), wav ? QStringLiteral("wav") : QStringLiteral("mxf"));
        made_ << p;
        return p;
    }

    // The writer settings every track file shares: OP-Atom, the clip's rate and start timecode, and the
    // "comment_lumenis_*" metadata readMediaFile() reads back.
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
        // What relinking compares (p.362-363): the source, length and rate,
        // and the resolution.
        const QString source = clip_.fields.value(QStringLiteral("Source File"));
        s.metadata.append({kSourceKey, source.isEmpty() ? clip_.name : source});
        s.metadata.append({kFramesKey, QString::number(out_ - in_)});
        s.metadata.append({kRateKey, rateText(clip_.editRate)});
        if (!resolution_.isEmpty()) s.metadata.append({kResolutionKey, resolution_});
        // One master clip in all its track files, a file source each.
        s.materialPackage = master_;
        s.filePackage = files_.value(track);
        // The packages' creation and modified dates; without it FFmpeg writes
        // zeros (0-00-00), which bmx reports and the media database keys on.
        const QDateTime when = creation_.when.isValid() ? creation_.when : created_;
        s.metadata.append({QStringLiteral("creation_time"), when.toUTC().toString(Qt::ISODateWithMs)});
        return s;
    }
    void setResolution(const QString& name) { resolution_ = name; }

    // Counts work done; false when cancelled.
    bool step(std::int64_t units = 1) {
        done_ += units;
        if (!creation_.progress) return true;
        return creation_.progress(std::min(1.0, static_cast<double>(done_) / static_cast<double>(total_)));
    }

    // V1 re-encoded as DNxHR at the creation's resolution and raster.
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
        // Fitted into the raster keeping its shape, on black, straight from
        // the decoded picture: no 8-bit step between (10-bit DNx keeps its
        // depth; ROADMAP Phase 2 gate 5).
        const QSize picture = QSize(reader.info().width, reader.info().height).scaled(creation_.raster, Qt::KeepAspectRatio);
        const QRect target(QPoint((creation_.raster.width() - picture.width()) / 2,
                                  (creation_.raster.height() - picture.height()) / 2),
                           picture);
        for (std::int64_t f = in_; f < out_; ++f) {
            bool ok = false;
            if (creation_.colorTransformations.isEmpty()) {
                ok = writer.writeVideoFrom(reader, link_.fileFrame(f, clip_.editRate), target);
            } else {
                // The clip's colour adapter applied (p.544): through an 8-bit picture, the transformations' domain.
                const QImage frame = reader.frame(link_.fileFrame(f, clip_.editRate));
                if (!frame.isNull()) {
                    QImage shown(creation_.raster, QImage::Format_RGBA8888);
                    shown.fill(Qt::black);
                    QPainter p(&shown);
                    p.setRenderHint(QPainter::SmoothPixmapTransform);
                    p.drawImage(QRectF(target), applyColorTransformations(frame, creation_.colorTransformations));
                    p.end();
                    ok = writer.writeVideo(shown);
                }
            }
            // A frame that cannot be read is black.
            if (!ok && writer.lastError().isEmpty()) ok = writer.writeVideo(fitted(QImage(), creation_.raster));
            if (!ok) {
                if (error) *error = writer.lastError();
                return false;
            }
            if (!step()) return cancelled(error);
        }
        return writer.finish(error);
    }

    // V1 from rendered pictures (renderedVideoClip()), in the creation's resolution and raster. With
    // `keep`, a cancel ends the file at the frames written so far (`written`) instead of failing.
    bool writeRenderedVideo(const QString& path, const RenderVideo& render, QString* error, bool keep, std::int64_t& written) {
        WriterSettings s = settingsFor(QStringLiteral("V1"));
        s.videoCodec = QStringLiteral("dnxhd");
        s.videoProfile = creation_.resolution.profile;
        s.pixelFormat = creation_.resolution.pixelFormat;
        s.width = creation_.raster.width();
        s.height = creation_.raster.height();
        MediaWriter writer;
        if (!writer.open(path, s, error)) return false;
        written = 0;
        for (std::int64_t f = in_; f < out_; ++f) {
            if (!writer.writeVideo(fitted(render(f), creation_.raster))) {
                if (error) *error = writer.lastError();
                return false;
            }
            ++written;
            if (!step()) {
                if (keep) return writer.finish(error);
                return cancelled(error);
            }
        }
        return writer.finish(error);
    }

    // V1 copied packet for packet (Consolidate; canConsolidate() made sure that is exact).
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
        // One channel of the source stream, read at its own rate, resampled to 48 kHz when needed, and
        // written exactly as long as the frames [in, out) last; past the file's end it is silence.
        AudioReader reader;
        if (!reader.open(link_.audioFile(t), channel->first, error)) return false;
        const int rate = reader.sampleRate();
        const int channels = std::max(reader.channels(), 1);
        const int c = std::clamp(channel->second, 0, channels - 1);
        const int target = targetRate();
        const std::int64_t wanted = samplesAt(out_ - in_, clip_.editRate, target);
        const WriterSettings s = audioSettings(t);
        MediaWriter writer;
        if (!writer.open(path, s, error)) return false;
        // A pull rate kept as it is is read as if it were the target rate.
        const bool relabel = creation_.relabelPullRates && isPullRate(rate, target);
        std::unique_ptr<Resampler> resampler;
        if (rate != target && !relabel) {
            resampler = std::make_unique<Resampler>(1, rate, target, creation_.resampleFilter);
            // An invalid one gives no samples: the track would be silence.
            if (!resampler->isValid()) {
                if (error) *error = QObject::tr("Cannot convert the sound of %1 from %2 Hz.").arg(clip_.name).arg(rate);
                return false;
            }
        }
        const int readRate = relabel ? target : rate;
        // In the file, where the clip starts in it; Subframe Alignment moves it (audioShift).
        const std::int64_t offset = samplesAt(link_.first, link_.rate, readRate) - std::min<std::int64_t>(creation_.audioShift, 0);
        std::int64_t at = samplesAt(in_, clip_.editRate, readRate) + offset;
        const std::int64_t end = samplesAt(out_, clip_.editRate, readRate) + offset;
        std::int64_t written = 0;
        auto put = [&](std::vector<float> mono) {
            const std::int64_t n = std::min<std::int64_t>(static_cast<std::int64_t>(mono.size()), wanted - written);
            if (n <= 0) return true;
            written += n;
            return writer.writeAudio(mono.data(), static_cast<int>(n));
        };
        // Silence before the sound, through the resampler as the sound goes.
        if (creation_.audioShift > 0 && in_ == 0) {
            std::vector<float> silence(static_cast<std::size_t>(creation_.audioShift), 0.0f);
            if (resampler) silence = resampler->push(silence.data(), static_cast<int>(silence.size()));
            if (!put(std::move(silence))) {
                if (error) *error = writer.lastError();
                return false;
            }
        }
        const int chunk = std::max(readRate / 4, 1);
        const std::int64_t perStep = samplesAt(1, clip_.editRate, readRate) > 0 ? samplesAt(1, clip_.editRate, readRate) : 1;
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
        auto failed = [&] {
            if (error) *error = writer.lastError();
            return false;
        };
        if (resampler && !put(resampler->flush())) return failed();
        // Exactly the frames' length.
        if (written < wanted) {
            const std::vector<float> silence(static_cast<std::size_t>(wanted - written), 0.0f);
            if (!put(silence)) return failed();
        }
        return writer.finish(error);
    }

    // Track `t` (0 for A1) from rendered sound (renderedAudioClip()): `channel` of what `render` gives,
    // exactly as long as the frames [in, out) last.
    // The sound is rendered at 48 kHz and, for another project rate (Audio Project settings, p.1148),
    // resampled to it.
    bool writeRendered(int t, int channel, int channels, const RenderAudio& render, const QString& path, QString* error) {
        const int target = targetRate();
        MediaWriter writer;
        if (!writer.open(path, audioSettings(t), error)) return false;
        std::unique_ptr<Resampler> resampler;
        if (target != kManagedSampleRate) {
            resampler = std::make_unique<Resampler>(1, kManagedSampleRate, target, creation_.resampleFilter);
            if (!resampler->isValid()) {
                if (error) *error = QObject::tr("Cannot convert the sound to %1 Hz.").arg(target);
                return false;
            }
        }
        const std::int64_t rendered = samplesAt(out_ - in_, clip_.editRate, kManagedSampleRate);
        const std::int64_t wanted = samplesAt(out_ - in_, clip_.editRate, target);
        const std::int64_t perStep = std::max<std::int64_t>(samplesAt(1, clip_.editRate, kManagedSampleRate), 1);
        constexpr int kChunk = kManagedSampleRate / 4;
        std::vector<std::vector<float>> buffers(static_cast<std::size_t>(channels));
        std::int64_t written = 0;
        auto put = [&](const std::vector<float>& mono) {
            const std::int64_t n = std::min<std::int64_t>(static_cast<std::int64_t>(mono.size()), wanted - written);
            if (n <= 0) return true;
            written += n;
            return writer.writeAudio(mono.data(), static_cast<int>(n));
        };
        for (std::int64_t at = 0; at < rendered;) {
            const int n = static_cast<int>(std::min<std::int64_t>(kChunk, rendered - at));
            for (auto& b : buffers) b.assign(static_cast<std::size_t>(n), 0.0f);
            render(at, n, buffers);
            const std::vector<float>& mono = buffers[static_cast<std::size_t>(channel)];
            if (!put(resampler ? resampler->push(mono.data(), n) : mono)) {
                if (error) *error = writer.lastError();
                return false;
            }
            at += n;
            if (!step(std::max<std::int64_t>(n / perStep, 1))) return cancelled(error);
        }
        // Exactly the frames' length: what the resampler still holds is silence's worth at most.
        if (written < wanted && !put(std::vector<float>(static_cast<std::size_t>(wanted - written), 0.0f))) {
            if (error) *error = writer.lastError();
            return false;
        }
        return writer.finish(error);
    }

    // The rate new audio media is written at: creation.sampleRate, else 48 kHz.
    int targetRate() const { return creation_.sampleRate > 0 ? creation_.sampleRate : kManagedSampleRate; }

    // The writer settings of audio track `t` (0 for A1): mono PCM at targetRate() and the project's
    // bit depth. 48 kHz is OP-Atom MXF; other rates are WAV (ours: FFmpeg's OP-Atom muxer takes only
    // 48 kHz; docs/spec/clip-audio.md), without the MXF packages.
    WriterSettings audioSettings(int t) const {
        WriterSettings s = settingsFor(QStringLiteral("A%1").arg(t + 1));
        s.audioCodec = creation_.bitDepth <= 16 ? QStringLiteral("pcm_s16le") : QStringLiteral("pcm_s24le");
        s.sampleRate = targetRate();
        s.channels = 1;
        if (s.sampleRate == kManagedSampleRate) {
            // The edit rate the file's timecode counts in.
            s.formatOptions = {{QStringLiteral("mxf_audio_edit_rate"), rateText(clip_.editRate)}};
        } else {
            s.format = QStringLiteral("wav");
            s.materialPackage = {};
            s.filePackage = {};
        }
        return s;
    }

    // Sets the error to "Cancelled." and returns false.
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
    QDateTime created_; // when creation_ has no time: one for all the tracks
    MobId master_ = MobId::generate();
    QHash<QString, MobId> files_;
    model::ClipSource origin_;
    QString id_;
    QString startTc_;
    QString resolution_;
    QStringList made_;
    bool kept_ = false;
    std::int64_t total_ = 1;
    std::int64_t done_ = 0;
};

// The media for frames [in, out) of a clip: every track written (copy: Consolidate, else Transcode)
// into the MXF folder of the video or audio drive, then a new master clip (an audio clip without
// picture) linked to the files with its source chain and columns. If any track fails, the files
// made so far are deleted (Maker's destructor) and the error says why.
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
    maker.setResolution(copy ? clip.fields.value(QStringLiteral("Video")) : creation.resolution.name);
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

    // The new clip.
    model::BinItem item;
    item.id = model::newItemId();
    item.mobId = maker.master();
    // Its source chain: the files, one a track, then where they came from.
    auto fileSource = [&](const QString& track, const QString& path) {
        model::ClipSource s;
        s.kind = model::ClipSource::Kind::File;
        s.mob = maker.fileMob(track);
        s.track = track;
        s.name = QFileInfo(path).fileName();
        s.path = path;
        s.from = maker.origin().mob;
        item.sources << s;
    };
    if (video) fileSource(QStringLiteral("V1"), made.path);
    for (int t = 0; t < made.audioPaths.size(); ++t) fileSource(QStringLiteral("A%1").arg(t + 1), made.audioPaths.at(t));
    item.sources << maker.origin();
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
        set("Audio SR", QString::number(creation.sampleRate > 0 ? creation.sampleRate : kManagedSampleRate));
        set("Audio Bit Depth", QStringLiteral("%1 bit").arg(creation.bitDepth <= 16 ? 16 : 24));
        set("Audio Format", QStringLiteral("PCM"));
    }
    set("Video File Format", QStringLiteral("MXF"));
    return item;
}

} // namespace

std::optional<model::BinItem> resampleClipAudio(const model::BinItem& clip, const MediaCreation& creation,
                                                QStringList* oldFiles, QString* error) {
    auto failed = [error](const QString& text) -> std::optional<model::BinItem> {
        if (error) *error = text;
        return std::nullopt;
    };
    const auto link = linkOf(clip);
    if (!link || link->unlinked || link->channels <= 0) return failed(QObject::tr("%1 has no audio media.").arg(clip.name));
    const QString drive = creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive;
    if (drive.isEmpty()) return failed(QObject::tr("No target drive is chosen."));
    // The picture and the sound share one offset into the media (Link::first), which the new sound
    // would not have.
    if (link->videoStream >= 0 && link->first != 0) {
        return failed(QObject::tr("%1 was relinked to media that starts earlier; consolidate it first.").arg(clip.name));
    }
    const QString folder = mxfFolder(drive, error);
    if (folder.isEmpty()) return std::nullopt;
    Maker maker(clip, *link, 0, clip.durationFrames, creation);
    maker.setWork(clip.durationFrames * link->channels);
    Link made = *link;
    made.audioPaths.clear();
    made.audioStreams.clear();
    made.streamChannels.clear();
    made.channels = 0;
    QStringList old;
    QStringList paths;
    for (int t = 0; t < link->channels; ++t) {
        const QString was = link->audioFile(t);
        if (was.contains(QLatin1String("Avid MediaFiles")) && !old.contains(was)) old << was;
        const QString path = maker.pathFor(folder, QStringLiteral("A%1").arg(t + 1));
        QString err;
        if (!maker.writeAudio(t, path, &err)) return failed(err);
        paths << path;
        made.audioPaths << path;
        made.audioStreams << 0;
        made.streamChannels << 1;
        made.channels += 1;
    }
    // A clip without picture is linked by its first audio file, which starts where the clip does; one
    // with picture keeps its picture's file.
    if (link->videoStream < 0) {
        made.path = paths.constFirst();
        made.first = 0;
    }
    maker.keep();
    model::BinItem out = clip;
    out.link = made.toJson();
    out.fields.insert(QStringLiteral("Audio SR"), QString::number(creation.sampleRate));
    out.fields.insert(QStringLiteral("Audio Bit Depth"), QStringLiteral("%1 bit").arg(creation.bitDepth <= 16 ? 16 : 24));
    // The file sources of the audio tracks name the new files.
    for (model::ClipSource& source : out.sources) {
        if (source.kind != model::ClipSource::Kind::File || !source.track.startsWith(QLatin1Char('A'))) continue;
        const int t = source.track.mid(1).toInt() - 1;
        if (t < 0 || t >= paths.size()) continue;
        source.mob = maker.fileMob(source.track);
        source.name = QFileInfo(paths.at(t)).fileName();
        source.path = paths.at(t);
    }
    if (oldFiles) *oldFiles = old;
    return out;
}

// A Maker for a clip that exists only as its name, rate, length and start (no media of its own):
// the files' source is a new import source named as the clip.
std::optional<model::BinItem> renderedAudioClip(const QString& name, const Rational& editRate, std::int64_t frames,
                                                int channels, const RenderAudio& render, const QString& startTimecode,
                                                const MediaCreation& creation, QString* error) {
    auto failed = [&](const QString& text) -> std::optional<model::BinItem> {
        if (error) *error = text;
        return std::nullopt;
    };
    if (frames <= 0 || channels <= 0) return failed(QObject::tr("There is no sound to make media of."));
    const QString drive = creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive;
    if (drive.isEmpty()) return failed(QObject::tr("No target drive is chosen."));
    const QString folder = mxfFolder(drive, error);
    if (folder.isEmpty()) return std::nullopt;
    model::BinItem clip;
    clip.name = name;
    clip.editRate = editRate;
    clip.durationFrames = frames;
    if (!startTimecode.isEmpty()) clip.fields.insert(QStringLiteral("Start"), startTimecode);
    const Link none;
    Maker maker(clip, none, 0, frames, creation);
    maker.setWork(frames * channels);
    Link made;
    made.rate = editRate;
    made.frames = frames;
    made.mediaId = maker.id();
    QString err;
    for (int t = 0; t < channels; ++t) {
        const QString path = maker.pathFor(folder, QStringLiteral("A%1").arg(t + 1));
        if (!maker.writeRendered(t, t, channels, render, path, &err)) return failed(err);
        if (made.path.isEmpty()) made.path = path;
        made.audioPaths << path;
        made.audioStreams << 0;
        made.streamChannels << 1;
        made.channels += 1;
    }
    maker.keep();

    model::BinItem item;
    item.id = model::newItemId();
    item.mobId = maker.master();
    model::ClipSource origin = maker.origin();
    origin.name = name;
    for (int t = 0; t < made.audioPaths.size(); ++t) {
        model::ClipSource s;
        s.kind = model::ClipSource::Kind::File;
        const QString track = QStringLiteral("A%1").arg(t + 1);
        s.mob = maker.fileMob(track);
        s.track = track;
        s.name = QFileInfo(made.audioPaths.at(t)).fileName();
        s.path = made.audioPaths.at(t);
        s.from = origin.mob;
        item.sources << s;
    }
    item.sources << origin;
    item.kind = model::BinItemKind::AudioClip;
    item.name = name;
    item.created = creation.when.isValid() ? creation.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = editRate;
    item.durationFrames = frames;
    item.link = made.toJson();
    const std::int64_t start = startFrame(clip).value_or(0);
    item.fields.insert(QStringLiteral("Start"), timecodeText(start, editRate));
    item.fields.insert(QStringLiteral("End"), timecodeText(start + frames, editRate));
    item.fields.insert(QStringLiteral("Tracks"), trackList(false, made.channels));
    item.fields.insert(QStringLiteral("Drive"), driveName(folder));
    const int rate = creation.sampleRate > 0 ? creation.sampleRate : kManagedSampleRate;
    item.fields.insert(QStringLiteral("Audio SR"), QString::number(rate));
    item.fields.insert(QStringLiteral("Audio Bit Depth"), QStringLiteral("%1 bit").arg(creation.bitDepth <= 16 ? 16 : 24));
    item.fields.insert(QStringLiteral("Audio Format"), QStringLiteral("PCM"));
    item.fields.insert(QStringLiteral("Video File Format"), rate == kManagedSampleRate ? QStringLiteral("MXF") : QStringLiteral("WAV"));
    return item;
}

std::optional<model::BinItem> renderedVideoClip(const QString& name, const Rational& editRate, std::int64_t frames,
                                                const RenderVideo& render, const MediaCreation& creation,
                                                model::BinItemKind kind, QString* error, bool keepOnCancel) {
    auto failed = [&](const QString& text) -> std::optional<model::BinItem> {
        if (error) *error = text;
        return std::nullopt;
    };
    if (frames <= 0) return failed(QObject::tr("There is no picture to make media of."));
    if (creation.videoDrive.isEmpty()) return failed(QObject::tr("No target drive is chosen."));
    QString folder;
    if (creation.folder.isEmpty()) {
        folder = mxfFolder(creation.videoDrive, error);
    } else {
        folder = QDir(QDir(mediaFilesFolder(creation.videoDrive)).filePath(QStringLiteral("MXF"))).filePath(creation.folder);
        if (!QDir().mkpath(folder)) return failed(QObject::tr("Cannot make the folder %1.").arg(QDir::toNativeSeparators(folder)));
    }
    if (folder.isEmpty()) return std::nullopt;
    model::BinItem clip;
    clip.name = name;
    clip.editRate = editRate;
    clip.durationFrames = frames;
    const Link none;
    Maker maker(clip, none, 0, frames, creation);
    maker.setResolution(creation.resolution.name);
    maker.setWork(frames);
    Link made;
    made.rate = editRate;
    made.frames = frames;
    made.mediaId = maker.id();
    QString err;
    const QString path = maker.pathFor(folder, QStringLiteral("V1"));
    std::int64_t written = 0;
    if (!maker.writeRenderedVideo(path, render, &err, keepOnCancel, written)) return failed(err);
    frames = written;
    made.frames = frames;
    made.path = path;
    made.videoStream = 0;
    maker.keep();

    model::BinItem item;
    item.id = model::newItemId();
    item.mobId = maker.master();
    model::ClipSource file;
    file.kind = model::ClipSource::Kind::File;
    file.mob = maker.fileMob(QStringLiteral("V1"));
    file.track = QStringLiteral("V1");
    file.name = QFileInfo(path).fileName();
    file.path = path;
    file.from = maker.origin().mob;
    item.sources << file << maker.origin();
    item.kind = kind;
    item.name = name;
    item.created = creation.when.isValid() ? creation.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = editRate;
    item.durationFrames = frames;
    item.link = made.toJson();
    const double fps = editRate.toDouble();
    item.fields.insert(QStringLiteral("Start"), timecodeText(0, editRate));
    item.fields.insert(QStringLiteral("End"), timecodeText(frames, editRate));
    item.fields.insert(QStringLiteral("Tracks"), trackList(true, 0));
    item.fields.insert(QStringLiteral("FPS"), fpsText(fps));
    item.fields.insert(QStringLiteral("Video"), creation.resolution.name);
    item.fields.insert(QStringLiteral("Format"), QStringLiteral("%1p/%2").arg(creation.raster.height()).arg(fpsText(fps)));
    item.fields.insert(QStringLiteral("Color Space"), creation.resolution.rgb ? QStringLiteral("RGB 709") : QStringLiteral("YCbCr 709"));
    item.fields.insert(QStringLiteral("Drive"), driveName(folder));
    return item;
}

bool isPullRate(int rate, int nominal) {
    const auto up = static_cast<int>(std::lround(nominal * 1001.0 / 1000.0));
    const auto down = static_cast<int>(std::lround(nominal * 1000.0 / 1001.0));
    return nominal > 0 && (rate == up || rate == down);
}

bool isImportRate(int rate) {
    return rate == 32000 || rate == 44100 || rate == 48000 || rate == 88200 || rate == 96000;
}

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

// The writable volumes that can hold media (system, temporary and read-only file systems are
// skipped), the home folder when its volume's top is not writable, and the existing `extra`
// folders, each root once.
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
    const QByteArray noSystem = qgetenv("LUMENIS_NO_SYSTEM_DRIVES");
    const bool systemDrives = noSystem.isEmpty() || noSystem == "0";
    for (const QStorageInfo& v : systemDrives ? QStorageInfo::mountedVolumes() : QList<QStorageInfo>{}) {
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
    if (systemDrives && homeVolume.isValid() && !QFileInfo(homeVolume.rootPath()).isWritable()) {
        add(home, QDir::toNativeSeparators(home), homeVolume.bytesAvailable());
    }
    for (const QString& folder : extra) {
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) continue;
        add(folder, QDir::toNativeSeparators(folder), QStorageInfo(folder).bytesAvailable());
    }
    return out;
}

namespace {
// The extra folders: LUMENIS_MEDIA_DRIVES at first use, then whatever setExtraMediaDrives() sets.
// Not locked: set it before media work starts on other threads.
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

namespace {
QStringList& unmounted() {
    static QStringList roots;
    return roots;
}
} // namespace

QList<MediaDrive> mediaDrives() {
    QList<MediaDrive> drives = mediaDrives(extraDrives());
    drives.removeIf([](const MediaDrive& d) { return unmounted().contains(d.root); });
    return drives;
}

void unmountDrives(const QStringList& roots) {
    for (const QString& r : roots) {
        const QString clean = QDir::cleanPath(r);
        if (!unmounted().contains(clean)) unmounted() << clean;
    }
}

void remountAllDrives() {
    unmounted().clear();
}

QStringList unmountedDrives() {
    return unmounted();
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

// Takes the highest-numbered folder (by its name as a number) and makes it, or "1", if needed.
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

// Audio is always written as PCM; a picture must be DNx at the clip's rate, in a file (not an image
// sequence), so its frames can be copied as they are.
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

namespace {

// Parses "num/den"; 25/1 when it is not a positive rate.
Rational rateFrom(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    const std::int64_t num = parts.value(0).toLongLong();
    const std::int64_t den = parts.size() > 1 ? parts.at(1).toLongLong() : 1;
    return num > 0 && den > 0 ? Rational(num, den) : Rational(25, 1);
}

} // namespace

// A file's entry in a folder's MediaDatabase.json (the index adds its size and time).
QJsonObject MediaFileInfo::toJson() const {
    QJsonObject o{{QStringLiteral("id"), mediaId},     {QStringLiteral("track"), track},
                  {QStringLiteral("clip"), clipName},  {QStringLiteral("project"), project},
                  {QStringLiteral("source"), source},  {QStringLiteral("tape"), tape},
                  {QStringLiteral("start"), start},    {QStringLiteral("frames"), static_cast<qint64>(frames)},
                  {QStringLiteral("rate"), rateText(rate)}, {QStringLiteral("resolution"), resolution},
                  {QStringLiteral("bytes"), static_cast<qint64>(bytes)}};
    if (created.isValid()) o.insert(QStringLiteral("created"), created.toString(Qt::ISODate));
    if (!material.isNull()) o.insert(QStringLiteral("material"), QString::fromStdString(material.toString()));
    return o;
}

MediaFileInfo MediaFileInfo::fromJson(const QJsonObject& o) {
    MediaFileInfo f;
    f.mediaId = o.value(QStringLiteral("id")).toString();
    f.track = o.value(QStringLiteral("track")).toString();
    f.clipName = o.value(QStringLiteral("clip")).toString();
    f.project = o.value(QStringLiteral("project")).toString();
    f.source = o.value(QStringLiteral("source")).toString();
    f.tape = o.value(QStringLiteral("tape")).toString();
    f.start = o.value(QStringLiteral("start")).toString();
    f.frames = o.value(QStringLiteral("frames")).toInteger();
    f.rate = rateFrom(o.value(QStringLiteral("rate")).toString());
    f.resolution = o.value(QStringLiteral("resolution")).toString();
    f.bytes = o.value(QStringLiteral("bytes")).toInteger();
    f.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    f.material = MobId::fromString(o.value(QStringLiteral("material")).toString().toStdString()).value_or(MobId());
    return f;
}

// Probes a file and reads the "comment_lumenis_*" metadata Maker wrote. Nothing if the file cannot
// be read or is not managed media (no media id or track).
std::optional<MediaFileInfo> readMediaFile(const QString& path) {
    const auto info = probe(path);
    if (!info) return std::nullopt;
    MediaFileInfo f;
    f.path = path;
    const auto& m = info->metadata;
    f.mediaId = m.value(kIdKey);
    f.track = m.value(kTrackKey);
    if (f.mediaId.isEmpty() || f.track.isEmpty()) return std::nullopt;
    f.clipName = m.value(QStringLiteral("material_package_name"));
    f.project = m.value(kProjectKey);
    f.source = m.value(kSourceKey, f.clipName);
    f.tape = info->reelName;
    f.start = info->startTimecode;
    f.rate = rateFrom(m.value(kRateKey));
    f.frames = m.value(kFramesKey).toLongLong();
    f.resolution = m.value(kResolutionKey);
    // FFmpeg reads package UMIDs as "0x" and 64 hex digits.
    QString material = m.value(QStringLiteral("material_package_umid"));
    if (material.startsWith(QLatin1String("0x"))) material = material.mid(2);
    f.material = MobId::fromString(material.toStdString()).value_or(MobId());
    if (f.frames <= 0 && !info->streams.empty()) {
        // Files made before these were written: from the stream.
        const StreamInfo& st = info->streams.front();
        if (st.kind == StreamKind::Video) {
            f.frames = st.length;
            if (st.frameRate.num() > 0) f.rate = st.frameRate;
        } else if (st.sampleRate > 0) {
            f.frames = st.length * f.rate.num() / (static_cast<std::int64_t>(st.sampleRate) * f.rate.den());
        }
    }
    const QFileInfo fi(path);
    f.bytes = fi.size();
    f.created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
    return f;
}

MediaFileInfo MediaSet::track(const QString& name) const {
    const auto it = tracks.find(name);
    return it == tracks.end() ? MediaFileInfo{} : it->second;
}

int MediaSet::audioTracks() const {
    int n = 0;
    while (tracks.contains(QStringLiteral("A%1").arg(n + 1))) ++n;
    return n;
}

// Every file a track; each file has one stream, so audio streams are 0 and one channel each.
Link MediaSet::link(std::int64_t first) const {
    Link l;
    l.mediaId = mediaId;
    l.first = first;
    const MediaFileInfo& f = any();
    l.rate = f.rate;
    l.frames = f.frames;
    if (hasVideo()) {
        l.path = track(QStringLiteral("V1")).path;
        l.videoStream = 0;
    }
    for (int t = 0; t < audioTracks(); ++t) {
        const QString a = track(QStringLiteral("A%1").arg(t + 1)).path;
        if (l.path.isEmpty()) l.path = a;
        l.audioPaths << a;
        l.audioStreams << 0;
        l.streamChannels << 1;
        l.channels += 1;
    }
    return l;
}

void MediaDatabase::clear() {
    files_.clear();
    resolutions_.reset();
}

bool MediaDatabase::hasMixedResolution(const MobId& master) const {
    if (master.isNull()) return false;
    if (!resolutions_) {
        resolutions_.emplace();
        for (const MediaFileInfo& f : files_) {
            if (!f.material.isNull() && f.track.startsWith(QLatin1Char('V'))) {
                (*resolutions_)[QString::fromStdString(f.material.toString())].insert(f.resolution);
            }
        }
    }
    return resolutions_->value(QString::fromStdString(master.toString())).size() > 1;
}

// Reads every MXF folder of each drive. A file is read again (readMediaFile()) only when its size or
// modification time differs from what the folder's index recorded; the index is rewritten when
// anything changed. Files that are not managed media stay in the index but not in the database.
// What earlier scans found on the drives scanned now is forgotten first, so deleted files go; files on
// other drives are kept (clear() forgets them too).
void MediaDatabase::scan(const QStringList& drives) {
    resolutions_.reset();
    for (const QString& drive : drives) {
        const QDir mxf(QDir(mediaFilesFolder(drive)).filePath(QStringLiteral("MXF")));
        const QString under = QDir::cleanPath(mxf.absolutePath()) + QLatin1Char('/');
        for (auto it = files_.begin(); it != files_.end();) {
            if (QDir::cleanPath(it.key()).startsWith(under)) it = files_.erase(it);
            else ++it;
        }
        for (const QString& sub : mxf.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir folder(mxf.filePath(sub));
            // The folder's index: the files read before, by name, with the
            // size and time they had then.
            const QString indexPath = folder.filePath(indexName());
            QJsonObject index;
            if (QFile f(indexPath); f.open(QIODevice::ReadOnly)) {
                const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
                // Older indexes lack what relinking needs: read again.
                if (doc.value(QStringLiteral("version")).toInt() == kIndexVersion) {
                    index = doc.value(QStringLiteral("files")).toObject();
                }
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
                    e = m ? m->toJson() : QJsonObject{};
                    e.insert(QStringLiteral("size"), size);
                    e.insert(QStringLiteral("time"), time);
                    changed = true;
                }
                fresh.insert(fi.fileName(), e);
                MediaFileInfo info = MediaFileInfo::fromJson(e);
                if (info.mediaId.isEmpty()) continue;
                info.path = fi.absoluteFilePath();
                files_.insert(info.path, info);
            }
            if (changed || fresh.size() != index.size()) {
                QFile f(indexPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), kIndexVersion},
                                                      {QStringLiteral("files"), fresh}})
                                .toJson(QJsonDocument::Indented));
                }
            }
        }
    }
}

QHash<QString, QString> MediaDatabase::filesOf(const QString& mediaId) const {
    QHash<QString, QString> out;
    for (const MediaFileInfo& f : files_) {
        if (f.mediaId == mediaId) out.insert(f.track, f.path);
    }
    return out;
}

int MediaDatabase::fileCount() const {
    return static_cast<int>(files_.size());
}

QList<MediaFileInfo> MediaDatabase::files() const {
    QList<MediaFileInfo> out = files_.values();
    std::sort(out.begin(), out.end(), [](const MediaFileInfo& a, const MediaFileInfo& b) { return a.path < b.path; });
    return out;
}

// One set per media id, in the order of the files' paths.
QList<MediaSet> MediaDatabase::sets() const {
    QHash<QString, MediaSet> byId;
    QStringList order;
    for (const MediaFileInfo& f : files()) {
        MediaSet& set = byId[f.mediaId];
        if (set.mediaId.isEmpty()) {
            set.mediaId = f.mediaId;
            order << f.mediaId;
        }
        set.tracks.insert_or_assign(f.track, f);
    }
    QList<MediaSet> out;
    for (const QString& id : order) out << byId.value(id);
    return out;
}

std::optional<MediaSet> MediaDatabase::set(const QString& mediaId) const {
    MediaSet set;
    set.mediaId = mediaId;
    for (const MediaFileInfo& f : files_) {
        if (f.mediaId == mediaId) set.tracks.insert_or_assign(f.track, f);
    }
    if (set.tracks.empty()) return std::nullopt;
    return set;
}

QStringList MediaDatabase::projects() const {
    QStringList out;
    for (const MediaFileInfo& f : files_) {
        if (!f.project.isEmpty() && !out.contains(f.project)) out << f.project;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

void MediaDatabase::remove(const QString& path) {
    files_.remove(path);
    resolutions_.reset();
}

// The set of the first file whose material package is that MobID.
std::optional<MediaSet> MediaDatabase::setOf(const MobId& master) const {
    if (master.isNull()) return std::nullopt;
    for (const MediaFileInfo& f : files_) {
        if (f.material == master) return set(f.mediaId);
    }
    return std::nullopt;
}

// Replaces the link's paths with where the database has the files of its media id now; the item is
// changed only when a path differs.
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

// Files that readMediaFile() does not take as managed media are left alone, and so are files that
// cannot be removed; neither is in the returned list.
QStringList deleteMediaFiles(const QStringList& paths, MediaDatabase* db) {
    QStringList deleted;
    for (const QString& path : paths) {
        // Managed media only: the file says which clip it is.
        if (!readMediaFile(path)) continue;
        if (QFile::remove(path)) {
            deleted << path;
            if (db) db->remove(path);
        }
    }
    return deleted;
}

} // namespace lumenis::media
