#include "lumenis/media/link.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/fields.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/motion_adapter.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QMutexLocker>
#include <QObject>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <exception>
#include <cmath>

// Linked media (ROADMAP Phase 2.2): master clips that point at files where they are, through a Link
// kept as JSON in model::BinItem::link. This file makes such clips from probed files (linkClip(),
// linkFiles(), groupStills() for image sequences), with the bin columns their metadata fills and a
// source chain of MobIDs (docs/spec/native-model.md); relinks them (Relink > Linked Media:
// indexFolder(), locateMedia(), relinkClip()); and decodes their frames (FrameCache). Managed media
// (managed.cpp) uses the same Link with a mediaId and a file per track.
// Callers: the Source Browser (src/ui/source_browser.cpp), its linking and importing in
// src/ui/main_window.cpp, the relink dialogs (src/ui/relink_dialog.cpp) and the frames the monitors,
// bins and Timeline show (src/ui/media_frames.cpp).

namespace lumenis::media {

namespace {

// "num/den", as Link::toJson() keeps a rate.
QString rateText(const Rational& r) {
    return QStringLiteral("%1/%2").arg(r.num()).arg(r.den());
}

// Parses rateText(); the default Rational if it is not a positive rate.
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

// a * b / c for frame and sample counts, in integers so it is exact on
// every platform (long double is a plain double on Apple arm64). The
// products stay far below 2^63: hours of frames times rate numerators.
std::int64_t rescale(std::int64_t a, std::int64_t b, std::int64_t c) {
    if (c <= 0) return 0;
    return (a * b + c / 2) / c; // rounded to the nearest
}

// a * b / c rounded down, and up; 0 when c is not positive.
std::int64_t rescaleDown(std::int64_t a, std::int64_t b, std::int64_t c) {
    return c <= 0 ? 0 : a * b / c;
}

std::int64_t rescaleUp(std::int64_t a, std::int64_t b, std::int64_t c) {
    return c <= 0 ? 0 : (a * b + c - 1) / c;
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

// The audio codec as editors know it: "PCM", "AAC", ...
QString audioName(const StreamInfo& s) {
    if (s.codec.startsWith(QLatin1String("pcm_"))) return QStringLiteral("PCM");
    static const QHash<QString, QString> names{
        {QStringLiteral("aac"), QStringLiteral("AAC")}, {QStringLiteral("ac3"), QStringLiteral("AC-3")},
        {QStringLiteral("eac3"), QStringLiteral("E-AC-3")}, {QStringLiteral("mp3"), QStringLiteral("MP3")},
        {QStringLiteral("flac"), QStringLiteral("FLAC")}, {QStringLiteral("opus"), QStringLiteral("Opus")},
    };
    return names.value(s.codec, s.codec.toUpper());
}

// Whether FFmpeg's pixel format name is an RGB one, for the Color Space column.
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

// Frames from midnight as the project's (non-drop) timecode, wrapped at 24 hours.
QString timecodeText(std::int64_t frames, const Rational& editRate) {
    const TimecodeFormat format = TimecodeFormat::forRate(editRate, false);
    return QString::fromStdString(Timecode(frames % format.framesPerDay(), format).toString());
}

} // namespace

QString linkPluginName() {
    return QStringLiteral("FFmpeg");
}

// model::subclipItem() makes the item; the subclip's link is the master's with `first` moved to the
// file frame of `in`, converted when the file runs at another rate.
std::optional<model::BinItem> makeSubclip(const model::Bin& bin, const model::BinItem& master, std::int64_t in,
                                          std::int64_t out, const QDateTime& when) {
    auto item = model::subclipItem(bin, master, in, out, when);
    if (!item) return std::nullopt;
    if (auto link = linkOf(master)) {
        // The master's file frame at `in`, before its own offset.
        const std::int64_t at = item->sources.first().offset;
        link->first += link->rate == master.editRate || master.editRate.num() <= 0
                           ? at
                           : rescaleDown(at, master.editRate.den() * link->rate.num(), master.editRate.num() * link->rate.den());
        item->link = link->toJson();
    }
    return item;
}

// Converts at the file's rate when it differs (rounding down), adds the start offset `first`, and
// clamps to the file's frames.
std::int64_t Link::fileFrame(std::int64_t clipFrame, const Rational& clipRate) const {
    std::int64_t f = clipFrame;
    if (clipRate.num() > 0 && !(rate == clipRate)) {
        // The file frame showing at the clip frame's time.
        f = rescaleDown(clipFrame, clipRate.den() * rate.num(), clipRate.num() * rate.den());
    }
    f += first;
    if (frames > 0) f = std::min(f, frames - 1);
    return std::max<std::int64_t>(f, 0);
}

// The link as kept in the bin; `first`, `unlinked`, `mediaId` and `audioPaths` only when set.
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
    QJsonArray perStream;
    for (int n : streamChannels) perStream.append(n);
    o.insert(QStringLiteral("streamChannels"), perStream);
    if (unlinked) o.insert(QStringLiteral("unlinked"), true);
    if (!mediaId.isEmpty()) {
        o.insert(QStringLiteral("mediaId"), mediaId);
        o.insert(QStringLiteral("audioPaths"), QJsonArray::fromStringList(audioPaths));
    } else if (!audioPaths.isEmpty()) {
        // A linked clip of several files (a monophonic group, groupMonoWavs()).
        o.insert(QStringLiteral("audioPaths"), QJsonArray::fromStringList(audioPaths));
    }
    if (first != 0) o.insert(QStringLiteral("first"), static_cast<qint64>(first));
    if (!topFieldFirst) o.insert(QStringLiteral("fieldOrder"), QStringLiteral("bottom"));
    return o;
}

// Nothing if there is no path; absent fields keep their defaults.
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
    for (const QJsonValue& v : json.value(QStringLiteral("streamChannels")).toArray()) l.streamChannels << v.toInt();
    // Links made before this was kept: one stream holds every channel.
    if (l.streamChannels.isEmpty() && l.audioStreams.size() == 1) l.streamChannels << l.channels;
    l.unlinked = json.value(QStringLiteral("unlinked")).toBool();
    l.mediaId = json.value(QStringLiteral("mediaId")).toString();
    for (const QJsonValue& v : json.value(QStringLiteral("audioPaths")).toArray()) l.audioPaths << v.toString();
    l.first = json.value(QStringLiteral("first")).toInteger();
    l.topFieldFirst = json.value(QStringLiteral("fieldOrder")).toString() != QLatin1String("bottom");
    return l;
}

std::optional<std::pair<int, int>> Link::channelOf(int track) const {
    // Track A1 is the first channel of the first audio stream, and so on.
    int k = track;
    for (int i = 0; i < audioStreams.size() && i < streamChannels.size(); ++i) {
        if (k < streamChannels.at(i)) return std::make_pair(audioStreams.at(i), k);
        k -= streamChannels.at(i);
    }
    return std::nullopt;
}

QString Link::audioFile(int track) const {
    // The stream that track is in, and that stream's file.
    int k = track;
    for (int i = 0; i < audioStreams.size() && i < streamChannels.size(); ++i) {
        if (k < streamChannels.at(i)) {
            const QString p = audioPaths.value(i);
            return p.isEmpty() ? path : p;
        }
        k -= streamChannels.at(i);
    }
    return {};
}

// The picture's file (or, without one, the path when there are no per-track audio files), then each
// distinct audio file.
QStringList Link::files() const {
    QStringList out;
    if (videoStream >= 0 || audioPaths.isEmpty()) out << file();
    for (const QString& p : audioPaths) {
        if (!p.isEmpty() && !out.contains(p)) out << p;
    }
    return out;
}

QString Link::file() const {
    if (const auto seq = ImageSequence::fromSpec(path)) return seq->fileAt(0);
    return path;
}

// The item's link, if it has one.
std::optional<Link> linkOf(const model::BinItem& item) {
    return item.link.isEmpty() ? std::nullopt : Link::fromJson(item.link);
}

// Whether the item has a link with a path (unlinked ones too).
bool isLinked(const model::BinItem& item) {
    return !item.link.value(QStringLiteral("path")).toString().isEmpty();
}

// Whether the clip's media was unlinked (Clip > Modify > Unlink Media).
bool isUnlinked(const model::BinItem& item) {
    return item.link.value(QStringLiteral("unlinked")).toBool();
}

bool unlinkClip(model::BinItem& item) {
    // Only master clips point at media (p.368).
    if (!isLinked(item) || item.kind == model::BinItemKind::Sequence || item.kind == model::BinItemKind::Subclip) {
        return false;
    }
    item.link.insert(QStringLiteral("unlinked"), true);
    return true;
}

// True if a video or audio stream can be decoded in this build.
bool isLinkable(const MediaInfo& info) {
    return std::any_of(info.streams.begin(), info.streams.end(), [](const StreamInfo& s) {
        return (s.kind == StreamKind::Video || s.kind == StreamKind::Audio) && s.decodable;
    });
}

// The clip runs at the project's rate (options.editRate); its duration converts the file's frames
// (or, for sound only, its samples) to that rate. Undecodable pictures are ignored, so a file with
// sound the build can read and a picture it cannot becomes an audio clip.
namespace {

std::optional<model::BinItem> linkClipUnchecked(const MediaInfo& info, const LinkOptions& options, QString* error) {
    if (!isLinkable(info)) {
        if (error) *error = QObject::tr("%1 has no picture or sound that can be read.").arg(info.path);
        return std::nullopt;
    }
    const Rational editRate = options.editRate.num() > 0 ? options.editRate : Rational(24000, 1001);
    const StreamInfo* video = info.firstVideo();
    if (video && !video->decodable) video = nullptr;
    Link link;
    const auto sequence = ImageSequence::fromSpec(info.path);
    // A sequence's files, else the file itself.
    const QFileInfo file(sequence ? sequence->fileAt(0) : info.path);
    link.path = sequence ? info.path : file.absoluteFilePath();
    const StreamInfo* firstAudio = nullptr;
    for (const StreamInfo* a : info.audio()) {
        if (!a->decodable || a->channels <= 0) continue;
        if (!firstAudio) firstAudio = a;
        link.audioStreams << a->index;
        link.streamChannels << a->channels;
        link.channels += a->channels;
    }
    // isLinkable() also passes sound streams with no channels, and those are skipped above.
    if (!video && !firstAudio) {
        if (error) *error = QObject::tr("%1 has no picture or sound that can be read.").arg(info.path);
        return std::nullopt;
    }

    // The item: a master clip with a picture, an audio clip without.
    model::BinItem item;
    item.id = model::newItemId();
    item.kind = video ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;
    // The file name without its extension (p.318).
    item.name = file.completeBaseName();
    // A sequence is named for its files without their numbers (p.326),
    // or for its folder when they are only numbers.
    if (sequence) item.name = sequence->name().isEmpty() ? file.absoluteDir().dirName() : sequence->name();
    item.created = options.when.isValid() ? options.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = editRate;

    // Its duration, from the picture's frames at the file's rate, or from the first audio stream's
    // samples.
    Rational fileRate = editRate;
    if (video) {
        link.videoStream = video->index;
        link.topFieldFirst = !video->interlaced || video->topFieldFirst;
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
        item.durationFrames =
            std::max<std::int64_t>(rescaleUp(samples, editRate.num(), firstAudio->sampleRate * editRate.den()), 1);
    }
    item.link = link.toJson();

    // The columns (p.274-278), set only when there is a value.
    auto set = [&](const char* heading, const QString& value) {
        if (!value.isEmpty()) item.fields.insert(QString::fromLatin1(heading), value);
    };
    set("Tracks", trackList(video != nullptr, link.channels));
    std::int64_t start = startFrames(info.startTimecode, fileRate, editRate).value_or(0);
    // A Broadcast WAV's time stamp, frame accurate: the frame its first sample falls in.
    if (info.startTimecode.isEmpty() && firstAudio && firstAudio->sampleRate > 0) {
        if (const auto tr = bwfTimeReference(info)) {
            start = rescaleDown(*tr, editRate.num(), std::int64_t{firstAudio->sampleRate} * editRate.den());
        }
    }
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
        // "Set the default for all new clips that are created" (p.1204).
        set("Reformatting Options", options.reformat);
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
    set("Source File", file.fileName());
    set("Source Path", file.absolutePath());
    const QStorageInfo volume(file.absoluteFilePath());
    set("Drive", volume.isValid() ? (volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName()) : QString());
    set("Plug-In", linkPluginName());
    // Its source chain (docs/spec/native-model.md): this file, one file
    // source (one MobID) listed for each of its tracks, from an import
    // source, the file as it was linked. MXF
    // names its own: the master clip is its material package, the file
    // source its file package (as FFmpeg reads them, "0x" and 64 digits).
    auto umid = [&](const char* key) {
        const QString text = info.metadata.value(QString::fromLatin1(key));
        const auto mob = MobId::fromString(text.startsWith(QLatin1String("0x")) ? text.mid(2).toStdString() : text.toStdString());
        return mob && !mob->isNull() ? *mob : MobId();
    };
    item.mobId = umid("material_package_umid");
    model::ClipSource import;
    import.kind = model::ClipSource::Kind::Import;
    import.mob = MobId::generate();
    import.name = file.fileName();
    import.path = link.path;
    import.startTimecode = timecodeText(start, editRate);
    const MobId filePackage = umid("file_package_umid");
    const MobId fileMob = filePackage.isNull() ? MobId::generate() : filePackage;
    QStringList tracks;
    if (video) tracks << QStringLiteral("V1");
    for (int c = 0; c < link.channels; ++c) tracks << QStringLiteral("A%1").arg(c + 1);
    for (const QString& track : tracks) {
        model::ClipSource s;
        s.kind = model::ClipSource::Kind::File;
        s.mob = fileMob;
        s.track = track;
        s.name = file.fileName();
        s.path = link.path;
        s.from = import.mob;
        item.sources << s;
    }
    item.sources << import;
    return item;
}

} // namespace

std::optional<model::BinItem> linkClip(const MediaInfo& info, const LinkOptions& options, QString* error) {
    // A damaged file's numbers can be past what Rational holds (it throws rather than wrap): the
    // file is then unreadable, not a crash.
    try {
        return linkClipUnchecked(info, options, error);
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("%1: %2").arg(info.path, QString::fromUtf8(e.what()));
        return std::nullopt;
    }
}

namespace {

// A numbered still split up: "/a/Kermit_", 12, 4 digits, ".dpx".
struct Numbered {
    QString stem;
    std::int64_t number = 0;
    int width = 0;
    QString suffix;
    QString key() const { return stem + QLatin1Char('|') + QString::number(width) + QLatin1Char('|') + suffix; }
};

// Splits a numbered still's path; nothing if it is not one (isNumberedStill()).
std::optional<Numbered> numbered(const QString& path) {
    if (!isNumberedStill(path)) return std::nullopt;
    const QFileInfo fi(path);
    const QString file = fi.fileName();
    const qsizetype dot = file.lastIndexOf(QLatin1Char('.'));
    qsizetype start = dot;
    while (start > 0 && file.at(start - 1).isDigit()) --start;
    Numbered n;
    n.stem = fi.absolutePath() + QLatin1Char('/') + file.left(start);
    n.number = file.mid(start, dot - start).toLongLong();
    n.width = static_cast<int>(dot - start);
    n.suffix = file.mid(dot);
    return n;
}

// The ImageSequence of a run: the stem, "%0<width>d" and the suffix as FFmpeg's image2 pattern.
ImageSequence sequenceOf(const Numbered& n, std::int64_t first, std::int64_t count, const Rational& rate) {
    ImageSequence s;
    s.pattern = n.stem + QStringLiteral("%0") + QString::number(n.width) + QLatin1Char('d') + n.suffix;
    s.first = first;
    s.count = count;
    s.rate = rate;
    return s;
}

} // namespace

QStringList groupStills(const QStringList& paths, const Rational& rate) {
    // Numbered stills of one name, number width and type are one sequence
    // (p.325-326): one file chosen brings its whole run; several chosen
    // make a clip of each run of consecutive numbers among them.
    QStringList out;
    QHash<QString, QList<std::int64_t>> groups;
    QHash<QString, Numbered> examples;
    QHash<QString, qsizetype> placeOf; // where each group goes in `out`
    for (const QString& path : paths) {
        const auto n = isSequenceSpec(path) ? std::nullopt : numbered(path);
        if (!n) {
            out << path;
            continue;
        }
        const QString key = n->key();
        if (!groups.contains(key)) {
            placeOf.insert(key, out.size());
            out << QString(); // filled below
            examples.insert(key, *n);
        }
        groups[key] << n->number;
    }
    // Each group's runs go where its first file was among `paths`.
    QHash<qsizetype, QStringList> filled;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const Numbered& n = examples.value(it.key());
        QList<std::int64_t> numbers = it.value();
        if (numbers.size() == 1) {
            // The whole run around the one chosen.
            const std::int64_t at = numbers.first();
            auto exists = [&](std::int64_t k) {
                return k >= 0 && QFileInfo::exists(n.stem + QStringLiteral("%1").arg(k, n.width, 10, QLatin1Char('0')) + n.suffix);
            };
            std::int64_t lo = at;
            std::int64_t hi = at;
            while (exists(lo - 1)) --lo;
            while (exists(hi + 1)) ++hi;
            numbers.clear();
            for (std::int64_t k = lo; k <= hi; ++k) numbers << k;
        }
        std::sort(numbers.begin(), numbers.end());
        numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
        QStringList runs;
        for (qsizetype i = 0; i < numbers.size();) {
            qsizetype j = i;
            while (j + 1 < numbers.size() && numbers.at(j + 1) == numbers.at(j) + 1) ++j;
            const std::int64_t count = numbers.at(j) - numbers.at(i) + 1;
            if (count == 1) {
                // A lone still is a clip of its own.
                runs << n.stem + QStringLiteral("%1").arg(numbers.at(i), n.width, 10, QLatin1Char('0')) + n.suffix;
            } else {
                runs << sequenceOf(n, numbers.at(i), count, rate).spec();
            }
            i = j + 1;
        }
        filled.insert(placeOf.value(it.key()), runs);
    }
    QStringList result;
    for (qsizetype i = 0; i < out.size(); ++i) {
        if (filled.contains(i)) {
            result << filled.value(i);
        } else {
            result << out.at(i);
        }
    }
    return result;
}

// Probes each path (or sequence spec) and links it with linkClip(), in order.
QList<model::BinItem> linkFiles(const QStringList& paths, const LinkOptions& options, int* skipped) {
    QList<model::BinItem> items;
    int missed = 0;
    for (const QString& path : groupStills(paths, options.editRate)) {
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

std::optional<std::int64_t> bwfTimeReference(const MediaInfo& info) {
    const QString text = info.metadata.value(QStringLiteral("time_reference"));
    bool ok = false;
    const qlonglong tr = text.toLongLong(&ok);
    if (!ok || tr < 0) return std::nullopt;
    return tr;
}

BwfAlignment alignToFrame(std::int64_t timeReference, int sampleRate, const Rational& editRate) {
    BwfAlignment a;
    if (sampleRate <= 0 || editRate.num() <= 0) return a;
    const double perFrame = static_cast<double>(sampleRate) * static_cast<double>(editRate.den()) / static_cast<double>(editRate.num());
    const auto frame = static_cast<std::int64_t>(std::floor(static_cast<double>(timeReference) / perFrame));
    const auto at = [&](std::int64_t f) { return static_cast<std::int64_t>(std::llround(static_cast<double>(f) * perFrame)); };
    const std::int64_t past = timeReference - at(frame);
    if (2 * past <= at(frame + 1) - at(frame)) {
        a.startFrame = frame;
        a.shift = past; // silence before the sound
    } else {
        a.startFrame = frame + 1;
        a.shift = timeReference - at(frame + 1); // negative: the sound before the next frame left out
    }
    return a;
}

QList<model::BinItem> groupMonoWavs(const QList<model::BinItem>& clips) {
    static const QRegularExpression pattern(QStringLiteral("^(.+)_(\\d+)\\.(wav|bwf)$"), QRegularExpression::CaseInsensitiveOption);
    struct Part {
        int index;
        int number;
    };
    QMap<QString, QList<Part>> groups; // folder|name -> parts
    for (int i = 0; i < clips.size(); ++i) {
        const auto link = linkOf(clips.at(i));
        if (!link || link->videoStream >= 0 || link->channels != 1) continue;
        const QFileInfo file(link->path);
        const auto m = pattern.match(file.fileName());
        if (!m.hasMatch()) continue;
        groups[file.absolutePath() + QLatin1Char('|') + m.captured(1)] << Part{i, m.captured(2).toInt()};
    }
    QHash<int, model::BinItem> joined; // first part's index -> the group's clip
    QSet<int> taken;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QList<Part>& parts = it.value();
        if (parts.size() < 2) continue;
        std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.number < b.number; });
        model::BinItem item = clips.at(parts.first().index);
        Link link = *linkOf(item);
        link.audioPaths = {link.path};
        item.name = it.key().section(QLatin1Char('|'), 1);
        // Each later file's sound as the next track, with its file source.
        for (int k = 1; k < parts.size(); ++k) {
            const model::BinItem& part = clips.at(parts.at(k).index);
            const Link other = *linkOf(part);
            link.audioPaths << other.path;
            link.audioStreams << other.audioStreams.value(0);
            link.streamChannels << 1;
            link.channels += 1;
            item.durationFrames = std::max(item.durationFrames, part.durationFrames);
            // Its file source as track A<k+1>, and the import source that file source comes from.
            for (model::ClipSource s : part.sources) {
                if (s.kind == model::ClipSource::Kind::File) {
                    s.track = QStringLiteral("A%1").arg(k + 1);
                    item.sources.insert(item.sources.size() - 1, s); // before the first import source
                } else {
                    item.sources << s;
                }
            }
        }
        for (const Part& p : parts) taken.insert(p.index);
        item.link = link.toJson();
        item.fields.insert(QStringLiteral("Tracks"), trackList(false, link.channels));
        item.fields.insert(QStringLiteral("Source File"), item.name);
        joined.insert(parts.first().index, item);
    }
    QList<model::BinItem> out;
    for (int i = 0; i < clips.size(); ++i) {
        if (joined.contains(i)) {
            out << joined.value(i);
        } else if (!taken.contains(i)) {
            out << clips.at(i);
        }
    }
    return out;
}

// File name to path for every file under `folder`, recursively; the first one found wins a name.
QHash<QString, QString> indexFolder(const QString& folder) {
    QHash<QString, QString> index;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString name = QFileInfo(path).fileName();
        if (!index.contains(name)) index.insert(name, path);
    }
    return index;
}

// Looks the clip's file name up in the index; for a sequence, its first file, and the result is the
// sequence's spec moved to that folder. Empty when it is not there.
QString locateMedia(const model::BinItem& item, const QHash<QString, QString>& index) {
    const auto link = linkOf(item);
    if (!link) return {};
    const QString found = index.value(QFileInfo(link->file()).fileName());
    if (found.isEmpty()) return {};
    if (auto seq = ImageSequence::fromSpec(link->path)) {
        // The same run in the folder where its first file is now.
        const QString pattern = seq->pattern.mid(seq->pattern.lastIndexOf(QLatin1Char('/')) + 1);
        seq->pattern = QFileInfo(found).absolutePath() + QLatin1Char('/') + pattern;
        return seq->spec();
    }
    return found;
}

// Links the file afresh at the clip's rate and checks it fits: the same duration and Tracks. The
// clip keeps its name, id and other columns; only the link (which drops any Unlink) and the Source
// File, Source Path, Drive and Plug-In columns change.
bool relinkClip(model::BinItem& item, const QString& path, QString* error) {
    const auto info = probe(path, error);
    if (!info) return false;
    LinkOptions options;
    options.editRate = item.editRate;
    const auto fresh = linkClip(*info, options, error);
    if (!fresh) return false;
    // Only media that fits the clip (p.320).
    if (fresh->durationFrames != item.durationFrames ||
        fresh->fields.value(QStringLiteral("Tracks")) != item.fields.value(QStringLiteral("Tracks"))) {
        if (error) *error = QObject::tr("%1 does not match the clip's duration or tracks.").arg(path);
        return false;
    }
    item.link = fresh->link;
    for (const char* heading : {"Source File", "Source Path", "Drive", "Plug-In"}) {
        item.fields.insert(QString::fromLatin1(heading), fresh->fields.value(QString::fromLatin1(heading)));
    }
    return true;
}

// ---------------------------------------------------------------- FrameCache

// One open file's video stream, with its own mutex: a VideoReader is used by one thread at a time.
// `ok` is false if the file has no picture or could not be opened; such a reader is not cached.
struct FrameCache::Reader {
    QString path;
    int stream = -1;
    QMutex mutex;
    VideoReader video;
    bool ok = false;
};

FrameCache::FrameCache(int openFiles, int frames) : openFiles_(std::max(openFiles, 1)), frames_(std::max(frames, 1)) {}

FrameCache::~FrameCache() = default;

// The reader for the link's file, most recently used first; the least recently used are closed past
// openFiles_. Readers are kept by path and stream; one that failed to open is returned but not kept,
// so the next call tries again (the file may have come online).
std::shared_ptr<FrameCache::Reader> FrameCache::reader(const Link& link) {
    // Called with mutex_ held.
    for (auto it = readers_.begin(); it != readers_.end(); ++it) {
        if ((*it)->path == link.path && (*it)->stream == link.videoStream) {
            readers_.splice(readers_.begin(), readers_, it);
            return readers_.front();
        }
    }
    auto r = std::make_shared<Reader>();
    r->path = link.path;
    r->stream = link.videoStream;
    r->ok = link.videoStream >= 0 && r->video.open(link.path, link.videoStream);
    if (!r->ok) return r;
    readers_.push_front(r);
    while (static_cast<int>(readers_.size()) > openFiles_) readers_.pop_back();
    return r;
}

// Looks up the scaled frame; on a miss it decodes outside the cache lock, holding only that file's
// reader lock, so other files decode at the same time on other threads. The frame is decoded at the
// file's raster and scaled with QImage, then kept, dropping the oldest past frames_.
QImage FrameCache::frame(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    const auto link = linkOf(item);
    if (!link || link->videoStream < 0 || link->unlinked) return {};
    // A group's angle shows black where it has no material (group_clip.h).
    if (const auto range = model::groupAngleRange(item); range && (clipFrame < range->first || clipFrame >= range->second)) return {};
    const std::int64_t n = link->fileFrame(clipFrame, item.editRate);
    // A mixed rate clip's adapter of a blending Type (model::adapterBlends) mixes the file frame with the
    // next one by how far the clip frame's time is between them ("Blended Interpolated", p.1215): the
    // in-between frames of a clip played at a rate other than its own. FluidMotion is the compositor's
    // (6.5d1) and blends here too.
    // A Motion Adapter that follows the clip's Field Motion, or makes an interlaced project's frames (6.5d3a):
    // the frame made field by field from the file's frames at the clip frame's time.
    if (const auto adapter = model::adapterOf(item);
        adapter && (adapter->interlaced || model::fieldMotionFromText(adapter->fieldMotion) != model::FieldMotionAttribute::Progressive)) {
        return convertedFrame(item, *link, *adapter, clipFrame, size);
    }
    double blend = 0.0;
    if (const auto adapter = model::adapterOf(item); adapter && model::adapterBlends(*adapter) && !(link->rate == item.editRate) &&
                                                     item.editRate.num() > 0 && n + 1 < link->frames) {
        const double at = static_cast<double>(clipFrame) * link->rate.toDouble() / item.editRate.toDouble() + static_cast<double>(link->first);
        blend = std::clamp(at - static_cast<double>(n), 0.0, 1.0);
        if (blend < 1e-3 || blend > 1.0 - 1e-3) blend = 0.0;
    }
    // One arg() call: a sequence's path holds a %04d of its own.
    const QString key = QStringLiteral("%1|%2|%3|%4x%5|%6")
                            .arg(link->path, QString::number(link->videoStream), QString::number(n),
                                 QString::number(size.width()), QString::number(size.height()), QString::number(blend, 'f', 3));
    std::shared_ptr<Reader> r;
    {
        QMutexLocker lock(&mutex_);
        if (const auto it = images_.constFind(key); it != images_.constEnd()) return *it;
        r = reader(*link);
    }
    QImage image;
    QImage next;
    {
        QMutexLocker lock(&r->mutex);
        if (!r->ok) return {};
        image = r->video.frame(n);
        if (blend > 0.0) next = r->video.frame(n + 1);
    }
    if (image.isNull()) return {};
    if (!next.isNull() && next.size() == image.size()) {
        // Drawn over at the blend's opacity: (1 - blend) of this frame and `blend` of the next.
        image = image.convertToFormat(QImage::Format_RGB32);
        QPainter p(&image);
        p.setOpacity(blend);
        p.drawImage(0, 0, next);
    }
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

// The source format from the Field Motion (Editing Guide p.472-473): 2:3 Film with its phase and Progressive
// Strobe with its repeat count found in the file's first frames once (ours: the adapter has no pattern of its
// own), Interlaced and Progressive as they are; the output the project's scan.
QImage FrameCache::convertedFrame(const model::BinItem& item, const Link& link, const model::MotionAdapter& adapter,
                                  std::int64_t clipFrame, QSize size) {
    const QString key = QStringLiteral("%1|%2|f%3|%4x%5|%6|%7|%8|%9")
                            .arg(link.path, QString::number(link.videoStream), QString::number(clipFrame), QString::number(size.width()),
                                 QString::number(size.height()), QString::fromStdString(item.editRate.toString()), adapter.fieldMotion,
                                 adapter.type, adapter.interlaced ? QStringLiteral("i") : QStringLiteral("p"));
    std::shared_ptr<Reader> r;
    {
        QMutexLocker lock(&mutex_);
        if (const auto it = images_.constFind(key); it != images_.constEnd()) return *it;
        r = reader(link);
    }
    const FrameGetter frameAt = [&](std::int64_t n) -> QImage {
        if (n < 0 || (link.frames > 0 && n >= link.frames)) return {};
        QMutexLocker lock(&r->mutex);
        return r->ok ? r->video.frame(n) : QImage();
    };
    FieldConversion conv;
    conv.rendering = fieldRendering(model::adapterType(adapter));
    conv.output = adapter.interlaced ? OutputFormat::Interlaced : OutputFormat::Progressive;
    conv.topFirst = link.topFieldFirst;
    switch (model::fieldMotionFromText(adapter.fieldMotion).value_or(model::FieldMotionAttribute::Progressive)) {
    case model::FieldMotionAttribute::Interlaced: conv.source = SourceFormat::Interlaced; break;
    case model::FieldMotionAttribute::Progressive: conv.source = SourceFormat::Progressive; break;
    case model::FieldMotionAttribute::Film23: {
        conv.source = SourceFormat::FilmPulldown;
        QMutexLocker lock(&mutex_);
        const auto it = patterns_.constFind(link.path);
        const bool known = it != patterns_.constEnd();
        lock.unlock();
        const int phase = known ? *it : detectPulldownPhase([&](std::int64_t n) { return frameAt(link.first + n); }, link.frames, link.topFieldFirst);
        if (!known) {
            lock.relock();
            patterns_.insert(link.path, phase);
        }
        conv.phase = phase;
        break;
    }
    case model::FieldMotionAttribute::ProgressiveStrobe: {
        conv.source = SourceFormat::ProgressiveRepeated;
        QMutexLocker lock(&mutex_);
        const QString repeatKey = link.path + QStringLiteral("|repeat");
        const auto it = patterns_.constFind(repeatKey);
        const bool known = it != patterns_.constEnd();
        lock.unlock();
        const int repeat = known ? *it : detectRepeatCount([&](std::int64_t n) { return frameAt(link.first + n); }, link.frames);
        if (!known) {
            lock.relock();
            patterns_.insert(repeatKey, repeat);
        }
        conv.repeat = repeat;
        break;
    }
    }
    // The file frame (with a fraction) at a clip frame's time; the cadence and repeats count from its first
    // frame the clip uses.
    const double ratio = item.editRate.num() > 0 ? link.rate.toDouble() / item.editRate.toDouble() : 1.0;
    const FrameGetter fromFirst = [&](std::int64_t n) { return frameAt(link.first + n); };
    QImage image = media::convertedFrame(fromFirst, [&](double t) { return t * ratio; }, static_cast<double>(clipFrame), conv);
    if (image.isNull()) return {};
    if (size.isValid() && image.size() != size) image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QMutexLocker lock(&mutex_);
    images_.insert(key, image);
    imageOrder_.removeAll(key);
    imageOrder_.append(key);
    while (imageOrder_.size() > frames_) images_.remove(imageOrder_.takeFirst());
    return image;
}

// Every file the media is in exists; nothing is opened.
bool FrameCache::isOnline(const model::BinItem& item) {
    const auto link = linkOf(item);
    if (!link || link->unlinked) return false;
    const QStringList files = link->files();
    return std::all_of(files.begin(), files.end(), [](const QString& f) { return QFileInfo::exists(f); });
}

void FrameCache::clear() {
    QMutexLocker lock(&mutex_);
    readers_.clear();
    images_.clear();
    imageOrder_.clear();
}

} // namespace lumenis::media
