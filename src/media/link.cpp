#include "lumenis/media/link.h"

#include "lumenis/core/timecode.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QMutexLocker>
#include <QObject>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

QString rateText(const Rational& r) {
    return QStringLiteral("%1/%2").arg(r.num()).arg(r.den());
}

Rational parseRate(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    const std::int64_t num = parts.value(0).toLongLong();
    const std::int64_t den = parts.size() > 1 ? parts.at(1).toLongLong() : 1;
    if (num <= 0 || den <= 0) return {};
    return {num, den};
}

// 23.976, 29.97, 25, 59.94: as frame rates are usually written.
QString fpsText(double fps) {
    QString s = QString::number(fps, 'f', 3);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

// a * b / c, rounded to the nearest, for frame counts.
std::int64_t rescale(std::int64_t a, std::int64_t b, std::int64_t c) {
    if (c == 0) return 0;
    const long double v = static_cast<long double>(a) * static_cast<long double>(b) / static_cast<long double>(c);
    return static_cast<std::int64_t>(std::llround(v));
}

// The codec as editors know it.
QString videoName(const StreamInfo& s) {
    const QString profile = s.profile;
    if (s.codec == QLatin1String("dnxhd")) {
        // FFmpeg's profiles: "DNXHD", "DNXHR LB", "DNXHR HQX", ...
        if (profile.startsWith(QLatin1String("DNXHR"))) return QStringLiteral("DNxHR") + profile.mid(5);
        return QStringLiteral("DNxHD");
    }
    if (s.codec.startsWith(QLatin1String("prores"))) {
        return profile.isEmpty() ? QStringLiteral("ProRes") : QStringLiteral("ProRes ") + profile;
    }
    static const QHash<QString, QString> names{
        {QStringLiteral("h264"), QStringLiteral("H.264")},  {QStringLiteral("hevc"), QStringLiteral("H.265")},
        {QStringLiteral("mpeg2video"), QStringLiteral("MPEG-2")}, {QStringLiteral("mpeg4"), QStringLiteral("MPEG-4")},
        {QStringLiteral("mjpeg"), QStringLiteral("Motion JPEG")}, {QStringLiteral("rawvideo"), QStringLiteral("Uncompressed")},
        {QStringLiteral("v210"), QStringLiteral("Uncompressed 10-bit")}, {QStringLiteral("ffv1"), QStringLiteral("FFV1")},
        {QStringLiteral("dvvideo"), QStringLiteral("DV")},     {QStringLiteral("vp9"), QStringLiteral("VP9")},
        {QStringLiteral("av1"), QStringLiteral("AV1")},        {QStringLiteral("png"), QStringLiteral("PNG")},
        {QStringLiteral("tiff"), QStringLiteral("TIFF")},      {QStringLiteral("dpx"), QStringLiteral("DPX")},
        {QStringLiteral("exr"), QStringLiteral("OpenEXR")},
    };
    return names.value(s.codec, s.codec.toUpper());
}

QString audioName(const StreamInfo& s) {
    if (s.codec.startsWith(QLatin1String("pcm_"))) return QStringLiteral("PCM");
    static const QHash<QString, QString> names{
        {QStringLiteral("aac"), QStringLiteral("AAC")}, {QStringLiteral("ac3"), QStringLiteral("AC-3")},
        {QStringLiteral("eac3"), QStringLiteral("E-AC-3")}, {QStringLiteral("mp3"), QStringLiteral("MP3")},
        {QStringLiteral("flac"), QStringLiteral("FLAC")}, {QStringLiteral("opus"), QStringLiteral("Opus")},
    };
    return names.value(s.codec, s.codec.toUpper());
}

bool isRgb(const QString& pixelFormat) {
    return pixelFormat.startsWith(QLatin1String("rgb")) || pixelFormat.startsWith(QLatin1String("bgr")) ||
           pixelFormat.startsWith(QLatin1String("gbr")) || pixelFormat.startsWith(QLatin1String("argb")) ||
           pixelFormat.startsWith(QLatin1String("abgr")) || pixelFormat.startsWith(QLatin1String("0rgb")) ||
           pixelFormat.startsWith(QLatin1String("rgba")) || pixelFormat.startsWith(QLatin1String("bgra"));
}

// "V1 A1-2", as the Tracks column shows tracks (p.278).
QString trackList(bool video, int channels) {
    QStringList parts;
    if (video) parts << QStringLiteral("V1");
    if (channels == 1) parts << QStringLiteral("A1");
    if (channels > 1) parts << QStringLiteral("A1-%1").arg(channels);
    return parts.join(QLatin1Char(' '));
}

// The file's starting timecode in the project's format. A timecode is read
// as a time of day in the project's rate (as Sound TC is, p.330); one the
// project's rate cannot hold (frame 29 at 24 fps) is converted by time.
std::optional<std::int64_t> startFrames(const QString& text, const Rational& fileRate, const Rational& editRate) {
    if (text.isEmpty()) return std::nullopt;
    const bool drop = text.contains(QLatin1Char(';'));
    const TimecodeFormat project = TimecodeFormat::forRate(editRate, false);
    QString plain = text;
    plain.replace(QLatin1Char(';'), QLatin1Char(':'));
    if (const auto tc = Timecode::parse(plain.toStdString(), project)) return tc->frameCount();
    const TimecodeFormat file = TimecodeFormat::forRate(fileRate, drop && TimecodeFormat::forRate(fileRate, true).isValid());
    if (const auto tc = Timecode::parse(text.toStdString(), file)) {
        return rescale(tc->frameCount(), fileRate.den() * editRate.num(), fileRate.num() * editRate.den());
    }
    return std::nullopt;
}

QString timecodeText(std::int64_t frames, const Rational& editRate) {
    const TimecodeFormat format = TimecodeFormat::forRate(editRate, false);
    return QString::fromStdString(Timecode(frames % format.framesPerDay(), format).toString());
}

} // namespace

QString linkPluginName() {
    return QStringLiteral("FFmpeg");
}

std::int64_t Link::fileFrame(std::int64_t clipFrame, const Rational& clipRate) const {
    std::int64_t f = clipFrame;
    if (clipRate.num() > 0 && !(rate == clipRate)) {
        // The file frame showing at the clip frame's time.
        const long double t = static_cast<long double>(clipFrame) * clipRate.den() / clipRate.num();
        f = static_cast<std::int64_t>(std::floor(t * rate.num() / rate.den() + 1e-6L));
    }
    if (frames > 0) f = std::min(f, frames - 1);
    return std::max<std::int64_t>(f, 0);
}

QJsonObject Link::toJson() const {
    QJsonObject o{
        {QStringLiteral("path"), path},
        {QStringLiteral("rate"), rateText(rate)},
        {QStringLiteral("frames"), static_cast<qint64>(frames)},
        {QStringLiteral("videoStream"), videoStream},
        {QStringLiteral("channels"), channels},
    };
    QJsonArray audio;
    for (int s : audioStreams) audio.append(s);
    o.insert(QStringLiteral("audioStreams"), audio);
    return o;
}

std::optional<Link> Link::fromJson(const QJsonObject& json) {
    Link l;
    l.path = json.value(QStringLiteral("path")).toString();
    if (l.path.isEmpty()) return std::nullopt;
    const Rational r = parseRate(json.value(QStringLiteral("rate")).toString());
    if (r.num() > 0) l.rate = r;
    l.frames = json.value(QStringLiteral("frames")).toInteger();
    l.videoStream = json.value(QStringLiteral("videoStream")).toInt(-1);
    l.channels = json.value(QStringLiteral("channels")).toInt();
    for (const QJsonValue& v : json.value(QStringLiteral("audioStreams")).toArray()) l.audioStreams << v.toInt();
    return l;
}

std::optional<Link> linkOf(const model::BinItem& item) {
    return item.link.isEmpty() ? std::nullopt : Link::fromJson(item.link);
}

bool isLinked(const model::BinItem& item) {
    return !item.link.value(QStringLiteral("path")).toString().isEmpty();
}

bool isLinkable(const MediaInfo& info) {
    return std::any_of(info.streams.begin(), info.streams.end(), [](const StreamInfo& s) {
        return (s.kind == StreamKind::Video || s.kind == StreamKind::Audio) && s.decodable;
    });
}

std::optional<model::BinItem> linkClip(const MediaInfo& info, const LinkOptions& options, QString* error) {
    if (!isLinkable(info)) {
        if (error) *error = QObject::tr("%1 has no picture or sound that can be read.").arg(info.path);
        return std::nullopt;
    }
    const Rational editRate = options.editRate.num() > 0 ? options.editRate : Rational(24000, 1001);
    const StreamInfo* video = info.firstVideo();
    if (video && !video->decodable) video = nullptr;
    Link link;
    link.path = QFileInfo(info.path).absoluteFilePath();
    const StreamInfo* firstAudio = nullptr;
    for (const StreamInfo* a : info.audio()) {
        if (!a->decodable || a->channels <= 0) continue;
        if (!firstAudio) firstAudio = a;
        link.audioStreams << a->index;
        link.channels += a->channels;
    }

    model::BinItem item;
    item.id = model::newItemId();
    item.kind = video ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;
    // The file name without its extension (p.318).
    item.name = QFileInfo(info.path).completeBaseName();
    item.created = options.when.isValid() ? options.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = editRate;

    Rational fileRate = editRate;
    if (video) {
        link.videoStream = video->index;
        if (video->frameRate.num() > 0) fileRate = video->frameRate;
        link.rate = fileRate;
        link.frames = video->length > 0 ? video->length
                                        : static_cast<std::int64_t>(std::llround(info.durationSeconds * fileRate.toDouble()));
        // A single picture (a still) lasts one frame.
        link.frames = std::max<std::int64_t>(link.frames, 1);
        item.durationFrames = rescale(link.frames, fileRate.den() * editRate.num(), fileRate.num() * editRate.den());
        item.durationFrames = std::max<std::int64_t>(item.durationFrames, 1);
    } else {
        link.rate = editRate;
        std::int64_t samples = firstAudio->length;
        if (samples <= 0) samples = static_cast<std::int64_t>(std::llround(info.durationSeconds * firstAudio->sampleRate));
        // From the frame with the first sample to the frame with the last
        // (p.329).
        const long double frames = static_cast<long double>(samples) * editRate.num() /
                                   (static_cast<long double>(firstAudio->sampleRate) * editRate.den());
        item.durationFrames = std::max<std::int64_t>(static_cast<std::int64_t>(std::ceil(frames - 1e-9L)), 1);
    }
    item.link = link.toJson();

    auto set = [&](const char* heading, const QString& value) {
        if (!value.isEmpty()) item.fields.insert(QString::fromLatin1(heading), value);
    };
    set("Tracks", trackList(video != nullptr, link.channels));
    const std::int64_t start = startFrames(info.startTimecode, fileRate, editRate).value_or(0);
    set("Start", timecodeText(start, editRate));
    set("End", timecodeText(start + item.durationFrames, editRate));
    set("Tape", info.reelName);
    if (video) {
        set("Video", videoName(*video));
        const double fps = fileRate.toDouble();
        set("FPS", fpsText(fps));
        set("Format", QStringLiteral("%1%2/%3")
                          .arg(video->height)
                          .arg(video->interlaced ? QLatin1Char('i') : QLatin1Char('p'))
                          .arg(fpsText(video->interlaced ? fps * 2 : fps)));
        Rational aspect = video->aspect;
        if (options.sdWidescreen && video->height <= 576) aspect = Rational(16, 9);
        if (aspect.num() > 0) set("Image Aspect Ratio", QStringLiteral("%1:%2").arg(aspect.num()).arg(aspect.den()));
        if (video->width > 0 && video->height > 0 && aspect.num() > 0) {
            // The pixel shape that stretches the stored size to the picture's.
            const double par = aspect.toDouble() * video->height / video->width;
            set("Pixel Aspect Ratio", fpsText(par));
        }
        set("Color Space", isRgb(video->pixelFormat) ? QStringLiteral("RGB")
                           : video->height > 576 || video->codec == QLatin1String("dnxhd")
                               ? QStringLiteral("YCbCr 709")
                               : QStringLiteral("YCbCr 601"));
    }
    if (firstAudio) {
        set("Audio SR", QString::number(firstAudio->sampleRate));
        if (firstAudio->bitsPerSample > 0) set("Audio Bit Depth", QStringLiteral("%1 bit").arg(firstAudio->bitsPerSample));
        set("Audio Format", audioName(*firstAudio));
    }
    if (info.format.contains(QLatin1String("mxf"))) set("Video File Format", QStringLiteral("MXF"));
    set("Source File", QFileInfo(info.path).fileName());
    set("Source Path", QFileInfo(info.path).absolutePath());
    const QStorageInfo volume(info.path);
    set("Drive", volume.isValid() ? (volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName()) : QString());
    set("Plug-In", linkPluginName());
    return item;
}

QList<model::BinItem> linkFiles(const QStringList& paths, const LinkOptions& options, int* skipped) {
    QList<model::BinItem> items;
    int missed = 0;
    for (const QString& path : paths) {
        const auto info = probe(path);
        const auto item = info ? linkClip(*info, options) : std::nullopt;
        if (item) {
            items << *item;
        } else {
            ++missed;
        }
    }
    if (skipped) *skipped = missed;
    return items;
}

// ---------------------------------------------------------------- FrameCache

struct FrameCache::Reader {
    QString path;
    QMutex mutex;
    VideoReader video;
    bool ok = false;
};

FrameCache::FrameCache(int openFiles, int frames) : openFiles_(std::max(openFiles, 1)), frames_(std::max(frames, 1)) {}

FrameCache::~FrameCache() = default;

std::shared_ptr<FrameCache::Reader> FrameCache::reader(const Link& link) {
    // Called with mutex_ held.
    for (auto it = readers_.begin(); it != readers_.end(); ++it) {
        if ((*it)->path == link.path) {
            readers_.splice(readers_.begin(), readers_, it);
            return readers_.front();
        }
    }
    auto r = std::make_shared<Reader>();
    r->path = link.path;
    r->ok = link.videoStream >= 0 && r->video.open(link.path, link.videoStream);
    readers_.push_front(r);
    while (static_cast<int>(readers_.size()) > openFiles_) readers_.pop_back();
    return r;
}

QImage FrameCache::frame(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    const auto link = linkOf(item);
    if (!link || link->videoStream < 0) return {};
    const std::int64_t n = link->fileFrame(clipFrame, item.editRate);
    const QString key = QStringLiteral("%1|%2|%3x%4").arg(link->path).arg(n).arg(size.width()).arg(size.height());
    std::shared_ptr<Reader> r;
    {
        QMutexLocker lock(&mutex_);
        if (const auto it = images_.constFind(key); it != images_.constEnd()) return *it;
        r = reader(*link);
    }
    QImage image;
    {
        QMutexLocker lock(&r->mutex);
        if (!r->ok) return {};
        image = r->video.frame(n);
    }
    if (image.isNull()) return {};
    if (size.isValid() && image.size() != size) {
        image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    QMutexLocker lock(&mutex_);
    images_.insert(key, image);
    imageOrder_.removeAll(key);
    imageOrder_.append(key);
    while (imageOrder_.size() > frames_) images_.remove(imageOrder_.takeFirst());
    return image;
}

bool FrameCache::isOnline(const model::BinItem& item) {
    const auto link = linkOf(item);
    return link && QFileInfo::exists(link->path);
}

void FrameCache::clear() {
    QMutexLocker lock(&mutex_);
    readers_.clear();
    images_.clear();
    imageOrder_.clear();
}

} // namespace lumenis::media
