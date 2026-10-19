#include "lumenis/media/link.h"

#include "lumenis/core/timecode.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QMutexLocker>
#include <QObject>
#include <QStorageInfo>

#include <algorithm>
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
    }
    if (first != 0) o.insert(QStringLiteral("first"), static_cast<qint64>(first));
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
std::optional<model::BinItem> linkClip(const MediaInfo& info, const LinkOptions& options, QString* error) {
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

// One open file, with its own mutex: a VideoReader is used by one thread at a time. `ok` is false if
// the file has no picture or could not be opened; such a reader stays cached until it is dropped.
struct FrameCache::Reader {
    QString path;
    QMutex mutex;
    VideoReader video;
    bool ok = false;
};

FrameCache::FrameCache(int openFiles, int frames) : openFiles_(std::max(openFiles, 1)), frames_(std::max(frames, 1)) {}

FrameCache::~FrameCache() = default;

// The reader for the link's file, most recently used first; the least recently used are closed past
// openFiles_. Readers are kept by path alone.
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

// Looks up the scaled frame; on a miss it decodes outside the cache lock, holding only that file's
// reader lock, so other files decode at the same time on other threads. The frame is decoded at the
// file's raster and scaled with QImage, then kept, dropping the oldest past frames_.
QImage FrameCache::frame(const model::BinItem& item, std::int64_t clipFrame, QSize size) {
    const auto link = linkOf(item);
    if (!link || link->videoStream < 0 || link->unlinked) return {};
    const std::int64_t n = link->fileFrame(clipFrame, item.editRate);
    // One arg() call: a sequence's path holds a %04d of its own.
    const QString key = QStringLiteral("%1|%2|%3x%4")
                            .arg(link->path, QString::number(n), QString::number(size.width()),
                                 QString::number(size.height()));
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
