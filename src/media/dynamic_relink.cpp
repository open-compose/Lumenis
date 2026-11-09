// MultiRez and Dynamic Relink (Editing Guide ch.31, p.1095-1121; docs/spec/dynamic-relink.md): the media a master clip
// can play, the quality matching that chooses among them, and relinking the clip's link to the choice. MainWindow runs
// it when clips load into the monitors and the Timeline, and when the Dynamic Relink setting changes.
#include "lumenis/media/dynamic_relink.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <QFileInfo>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

const QString kAffinityKey = QStringLiteral("multiRez");
const QString kOfflineKey = QStringLiteral("multiRezOffline");
// The clip's Video column while it plays other media: the column shows the resolution linked to (p.1116).
const QString kVideoKey = QStringLiteral("multiRezVideo");

RelinkRules::NoMatch noMatchOf(const QString& v, RelinkRules::NoMatch fallback) {
    if (v == QLatin1String("highestQuality")) return RelinkRules::NoMatch::HighestQuality;
    if (v == QLatin1String("lowerBandwidth")) return RelinkRules::NoMatch::LowerBandwidth;
    if (v == QLatin1String("keepExisting")) return RelinkRules::NoMatch::KeepExisting;
    if (v == QLatin1String("offline")) return RelinkRules::NoMatch::Offline;
    return fallback;
}

RelinkRules::Method methodOf(const QString& v, RelinkRules::Method fallback) {
    if (v == QLatin1String("mostRecent")) return RelinkRules::Method::MostRecent;
    if (v == QLatin1String("highestQuality")) return RelinkRules::Method::HighestQuality;
    if (v == QLatin1String("minimalBandwidth")) return RelinkRules::Method::MinimalBandwidth;
    if (v == QLatin1String("specificResolution")) return RelinkRules::Method::SpecificResolution;
    return fallback;
}

RelinkRules::AudioMethod audioMethodOf(const QString& v, RelinkRules::AudioMethod fallback) {
    if (v == QLatin1String("mostRecent")) return RelinkRules::AudioMethod::MostRecent;
    if (v == QLatin1String("highestQuality")) return RelinkRules::AudioMethod::HighestQuality;
    if (v == QLatin1String("mostCompressed")) return RelinkRules::AudioMethod::MostCompressed;
    if (v == QLatin1String("specificQuality")) return RelinkRules::AudioMethod::SpecificQuality;
    return fallback;
}

// "any" (the menus' first choice) and empty both mean no filter.
QString filterOf(const QJsonObject& values, const QString& key) {
    const QString v = values.value(key).toString();
    return v == QLatin1String("any") ? QString() : v;
}

RelinkRules rulesOf(const QJsonObject& values, const QString& tab) {
    RelinkRules r;
    const QString p = tab + QLatin1Char('.');
    r.noMatch = noMatchOf(values.value(p + QStringLiteral("noMatch")).toString(), r.noMatch);
    r.video = methodOf(values.value(p + QStringLiteral("videoMethod")).toString(), r.video);
    r.raster = filterOf(values, p + QStringLiteral("raster"));
    r.frameRate = filterOf(values, p + QStringLiteral("frameRate"));
    r.codecFamily = filterOf(values, p + QStringLiteral("codecFamily"));
    r.compression = filterOf(values, p + QStringLiteral("compression"));
    r.container = filterOf(values, p + QStringLiteral("container"));
    r.preferTargetRaster = values.value(p + QStringLiteral("videoMultiple")).toString() != QLatin1String("lowestBandwidth");
    r.audio = audioMethodOf(values.value(p + QStringLiteral("audioMethod")).toString(), r.audio);
    r.audioCodec = filterOf(values, p + QStringLiteral("audioCodec"));
    r.bitDepth = filterOf(values, p + QStringLiteral("bitDepth"));
    r.sampleRate = filterOf(values, p + QStringLiteral("sampleRate"));
    r.audioContainer = filterOf(values, p + QStringLiteral("audioContainer"));
    r.preferHighestBitrate = values.value(p + QStringLiteral("audioMultiple")).toString() != QLatin1String("lowestBitrate");
    return r;
}

bool sameRules(const RelinkRules& a, const RelinkRules& b) {
    return a.noMatch == b.noMatch && a.video == b.video && a.raster == b.raster && a.frameRate == b.frameRate &&
           a.codecFamily == b.codecFamily && a.compression == b.compression && a.container == b.container &&
           a.preferTargetRaster == b.preferTargetRaster && a.audio == b.audio && a.audioCodec == b.audioCodec &&
           a.bitDepth == b.bitDepth && a.sampleRate == b.sampleRate && a.audioContainer == b.audioContainer &&
           a.preferHighestBitrate == b.preferHighestBitrate;
}

// The link without the keys dynamic relink adds.
QJsonObject plainLink(QJsonObject link) {
    link.remove(kAffinityKey);
    link.remove(kOfflineKey);
    link.remove(kVideoKey);
    return link;
}

QString rasterText(const QSize& s) {
    return s.isValid() ? QStringLiteral("%1x%2").arg(s.width()).arg(s.height()) : QString();
}

// Frame rates as the Frame Rate menu writes them: 23.976 and 24 differ, 23.976 and 23.98 do not.
bool sameRate(const Rational& rate, const QString& text) {
    bool ok = false;
    const double want = text.toDouble(&ok);
    return ok && rate.num() > 0 && std::abs(rate.toDouble() - want) < 0.01;
}

bool sameRate(const Rational& a, const Rational& b) {
    return a.num() > 0 && b.num() > 0 && std::abs(a.toDouble() - b.toDouble()) < 0.001;
}

// Whether one rate is a whole multiple of the other (25 and 50, 23.976 and 47.952).
bool multipleRates(double a, double b) {
    if (a <= 0 || b <= 0) return false;
    const double hi = std::max(a, b);
    const double lo = std::min(a, b);
    const double ratio = hi / lo;
    return std::abs(ratio - std::round(ratio)) < 0.002;
}

bool newer(const MediaCandidate& a, const MediaCandidate& b) {
    return a.created > b.created;
}

// Highest Quality: "the least compressed and most highly defined media (more pixels, higher bit depth, more color
// information) with a format that most closely matches the project settings" (p.1120): the project's raster first,
// then more pixels, then more bytes a frame (ours, as our files do not record bit depth or chroma).
bool better(const MediaCandidate& a, const MediaCandidate& b, const QSize& projectRaster) {
    const bool aFits = a.raster == projectRaster;
    const bool bFits = b.raster == projectRaster;
    if (aFits != bFits) return aFits;
    const qint64 aPixels = static_cast<qint64>(a.raster.width()) * a.raster.height();
    const qint64 bPixels = static_cast<qint64>(b.raster.width()) * b.raster.height();
    if (aPixels != bPixels) return aPixels > bPixels;
    return a.bandwidth > b.bandwidth;
}

bool betterAudio(const MediaCandidate& a, const MediaCandidate& b) {
    const qint64 aq = static_cast<qint64>(a.sampleRate) * std::max(a.bitDepth, 1);
    const qint64 bq = static_cast<qint64>(b.sampleRate) * std::max(b.bitDepth, 1);
    if (aq != bq) return aq > bq;
    return a.bandwidth > b.bandwidth;
}

template <typename Less>
std::optional<MediaCandidate> best(const QList<MediaCandidate>& from, Less less) {
    if (from.isEmpty()) return std::nullopt;
    return *std::min_element(from.begin(), from.end(), less);
}

// Specific Resolution's filters (p.1170); the frame rate only when it is not a preference (Allow Mixed Frame Rate
// Media turns the menu into "preferred values", p.1110).
bool passesVideo(const MediaCandidate& c, const RelinkRules& r, bool rateFilter) {
    if (!r.raster.isEmpty() && rasterText(c.raster) != r.raster) return false;
    if (rateFilter && !r.frameRate.isEmpty() && !sameRate(c.rate, r.frameRate)) return false;
    if (!r.codecFamily.isEmpty() && c.codecFamily != r.codecFamily) return false;
    if (!r.compression.isEmpty() && c.resolution != r.compression) return false;
    if (!r.container.isEmpty() && c.container != r.container) return false;
    return true;
}

bool passesAudio(const MediaCandidate& c, const RelinkRules& r) {
    if (!r.audioCodec.isEmpty() && c.codecFamily != r.audioCodec) return false;
    if (!r.bitDepth.isEmpty() && QString::number(c.bitDepth) != r.bitDepth) return false;
    if (!r.sampleRate.isEmpty() && QString::number(c.sampleRate) != r.sampleRate) return false;
    if (!r.audioContainer.isEmpty() && c.container != r.audioContainer) return false;
    return true;
}

// The candidate of the clip's affinity link, from probing its file; nothing while its files are not there.
std::optional<MediaCandidate> affinityCandidate(const model::BinItem& clip, const QJsonObject& own) {
    const auto link = Link::fromJson(own);
    if (!link || link->unlinked) return std::nullopt;
    for (const QString& f : link->files()) {
        if (!QFileInfo::exists(f)) return std::nullopt;
    }
    MediaCandidate c;
    c.link = own;
    c.mediaId = link->mediaId;
    c.affinity = true;
    c.video = link->videoStream >= 0;
    const auto info = probe(link->file());
    qint64 bytes = 0;
    for (const QString& f : link->files()) bytes += QFileInfo(f).size();
    QDateTime created;
    {
        const QFileInfo fi(link->file());
        created = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
    }
    c.created = created;
    if (info) {
        c.container = containerOf(info->format, link->file());
        for (const StreamInfo& s : info->streams) {
            if (s.kind == StreamKind::Video && (c.video ? s.index == link->videoStream : false)) {
                c.raster = QSize(s.width, s.height);
                c.rate = link->rate;
                c.codecFamily = codecFamilyOf(s.codec);
                c.resolution = s.profile.isEmpty() ? s.codecLongName : s.profile;
            } else if (s.kind == StreamKind::Audio && c.sampleRate == 0) {
                c.sampleRate = s.sampleRate;
                c.bitDepth = s.bitsPerSample;
                if (!c.video) c.codecFamily = codecFamilyOf(s.codec);
            }
        }
    }
    // Managed affinity media: what the database recorded is what the files hold.
    if (!c.video && c.codecFamily.isEmpty()) c.codecFamily = QStringLiteral("PCM");
    const std::int64_t frames = clip.durationFrames > 0 ? clip.durationFrames : 1;
    c.bandwidth = static_cast<double>(bytes) / static_cast<double>(frames);
    return c;
}

// A managed media set as a candidate.
MediaCandidate setCandidate(const MediaSet& set, std::int64_t frames) {
    MediaCandidate c;
    c.link = set.link().toJson();
    c.mediaId = set.mediaId;
    c.video = set.hasVideo();
    const MediaFileInfo first = c.video ? set.track(QStringLiteral("V1")) : set.any();
    c.resolution = c.video ? first.resolution : QString();
    c.codecFamily = c.video ? codecFamilyOf(first.codec) : QStringLiteral("PCM");
    if (c.video && c.codecFamily == QLatin1String("Other") && first.resolution.startsWith(QLatin1String("Avid DNx"))) {
        c.codecFamily = QStringLiteral("Avid DNx");
    }
    c.container = QStringLiteral("MXF");
    c.raster = QSize(first.width, first.height);
    c.rate = c.video ? first.rate : Rational(0, 1);
    qint64 bytes = 0;
    QDateTime created;
    for (const auto& [track, file] : set.tracks) {
        bytes += file.bytes;
        if (!created.isValid() || file.created > created) created = file.created;
        if (track.startsWith(QLatin1Char('A')) && c.sampleRate == 0) {
            c.sampleRate = file.sampleRate;
            c.bitDepth = file.bitDepth;
        }
    }
    c.created = created;
    c.bandwidth = static_cast<double>(bytes) / static_cast<double>(std::max<std::int64_t>(frames, 1));
    return c;
}

// Whether a set holds the clip's tracks.
bool hasTracks(const MediaSet& set, const std::optional<Link>& own) {
    if (!own) return true;
    if (own->videoStream >= 0 && !set.hasVideo()) return false;
    // Managed media keep a file a track; linked media a channel a track.
    const int tracks = own->isManaged() ? static_cast<int>(own->audioPaths.size()) : own->channels;
    return set.audioTracks() >= tracks;
}

// A timecode label as seconds of timecode (frames over the nominal rate, so labels at different rates compare);
// nothing when it does not read.
std::optional<double> timecodeSeconds(const QString& text, const Rational& rate) {
    if (text.isEmpty() || rate.num() <= 0) return std::nullopt;
    const TimecodeFormat format = TimecodeFormat::forRate(rate, text.contains(QLatin1Char(';')));
    const auto tc = Timecode::parse(text.toStdString(), format);
    if (!tc || format.nominalFps <= 0) return std::nullopt;
    return static_cast<double>(tc->frameCount()) / format.nominalFps;
}

// The clip's source as its media files name it (managed.cpp's Maker): its tape, else its source file.
bool sharesSource(const MediaFileInfo& file, const model::BinItem& clip) {
    const QString tape = clip.fields.value(QStringLiteral("Tape"));
    if (!tape.isEmpty()) return file.tape == tape;
    const QString source = clip.fields.value(QStringLiteral("Source File"));
    return !source.isEmpty() && file.tape.isEmpty() && file.source == source;
}

} // namespace

DynamicRelinkSettings DynamicRelinkSettings::fromValues(const QJsonObject& values) {
    DynamicRelinkSettings s;
    s.enabled = values.value(QStringLiteral("enabled")).toBool(false);
    s.mixedRates = values.value(QStringLiteral("mixedRates")).toBool(false);
    s.overrideWorking = values.value(QStringLiteral("overrideWorking")).toBool(false);
    s.working = rulesOf(values, QStringLiteral("working"));
    s.target = rulesOf(values, QStringLiteral("target"));
    return s;
}

bool DynamicRelinkSettings::operator==(const DynamicRelinkSettings& o) const {
    return enabled == o.enabled && mixedRates == o.mixedRates && overrideWorking == o.overrideWorking &&
           sameRules(working, o.working) && sameRules(target, o.target);
}

QString codecFamilyOf(const QString& codec) {
    if (codec == QLatin1String("dnxhd") || codec == QLatin1String("dnxuc")) return QStringLiteral("Avid DNx");
    if (codec == QLatin1String("prores")) return QStringLiteral("Apple ProRes");
    if (codec == QLatin1String("h264")) return QStringLiteral("H.264");
    if (codec == QLatin1String("hevc")) return QStringLiteral("H.265");
    if (codec == QLatin1String("rawvideo") || codec == QLatin1String("v210") || codec == QLatin1String("v410")) {
        return QStringLiteral("Uncompressed");
    }
    if (codec.startsWith(QLatin1String("pcm_"))) return QStringLiteral("PCM");
    if (codec == QLatin1String("aac")) return QStringLiteral("AAC");
    return QStringLiteral("Other");
}

QString containerOf(const QString& format, const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (format.contains(QLatin1String("mxf")) || suffix == QLatin1String("mxf")) return QStringLiteral("MXF");
    if (suffix == QLatin1String("mp4") || suffix == QLatin1String("m4v")) return QStringLiteral("MP4");
    if (format.startsWith(QLatin1String("mov")) || suffix == QLatin1String("mov")) return QStringLiteral("QuickTime");
    if (format == QLatin1String("wav") || suffix == QLatin1String("wav")) return QStringLiteral("WAV");
    return QStringLiteral("Other");
}

MediaCoverage mediaCoverage(const MediaSet& set, const model::BinItem& clip) {
    const MediaFileInfo& any = set.any();
    if (clip.editRate.num() <= 0 || any.rate.num() <= 0) return MediaCoverage::Full;
    const double need = static_cast<double>(std::max<std::int64_t>(clip.durationFrames, 0)) / clip.editRate.toDouble();
    const double have = static_cast<double>(any.frames) / any.rate.toDouble();
    const double tolerance = 0.5 / clip.editRate.toDouble();
    const auto clipStart = timecodeSeconds(clip.fields.value(QStringLiteral("Start")), clip.editRate);
    const auto setStart = timecodeSeconds(any.start, any.rate);
    if (!clipStart || !setStart) {
        // Without timecode both start together (ours).
        if (have + tolerance >= need) return MediaCoverage::Full;
        return have > 0 ? MediaCoverage::Partial : MediaCoverage::None;
    }
    const double clipEnd = *clipStart + need;
    const double setEnd = *setStart + have;
    if (*setStart <= *clipStart + tolerance && setEnd + tolerance >= clipEnd) return MediaCoverage::Full;
    if (std::min(clipEnd, setEnd) - std::max(*clipStart, *setStart) > tolerance) return MediaCoverage::Partial;
    return MediaCoverage::None;
}

QList<MediaSet> associatedMedia(const model::BinItem& clip, const MediaDatabase& db, bool affinityOnly) {
    QList<MediaSet> out;
    if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::AudioClip) return out;
    const MobId master = model::mobIdOf(clip);
    for (const MediaSet& set : db.sets()) {
        const MediaFileInfo& any = set.any();
        const bool own = any.material == master;
        if (!own && (affinityOnly || !sharesSource(any, clip))) continue;
        if (!own && mediaCoverage(set, clip) == MediaCoverage::None) continue;
        out << set;
    }
    return out;
}

QStringList multiRezHeadings() {
    QStringList out;
    for (const Resolution& r : resolutions()) out << r.name;
    out << QStringLiteral("32 kHz") << QStringLiteral("44.1 kHz") << QStringLiteral("48 kHz") << QStringLiteral("96 kHz");
    return out;
}

namespace {

QString sampleRateHeading(int rate) {
    if (rate <= 0) return {};
    const double khz = rate / 1000.0;
    return QStringLiteral("%1 kHz").arg(QString::number(khz, 'g', 4));
}

} // namespace

QHash<QString, MediaCoverage> multiRezCoverage(const model::BinItem& clip, const MediaDatabase& db) {
    QHash<QString, MediaCoverage> out;
    auto keep = [&out](const QString& heading, MediaCoverage c) {
        if (heading.isEmpty()) return;
        if (static_cast<int>(c) > static_cast<int>(out.value(heading, MediaCoverage::None))) out.insert(heading, c);
    };
    for (const MediaSet& set : associatedMedia(clip, db)) {
        const MediaCoverage c = mediaCoverage(set, clip);
        if (set.hasVideo()) keep(set.track(QStringLiteral("V1")).resolution, c);
        if (set.audioTracks() > 0) keep(sampleRateHeading(set.track(QStringLiteral("A1")).sampleRate), c);
    }
    return out;
}

QString multiRezCellText(MediaCoverage coverage) {
    switch (coverage) {
    case MediaCoverage::Full: return QStringLiteral("\u25CF");    // filled circle: online
    case MediaCoverage::Partial: return QStringLiteral("\u25D0"); // half-filled circle: partially online
    case MediaCoverage::None: break;
    }
    return QStringLiteral("\u25CB"); // empty circle: offline or not available
}

QMap<QString, QStringList> affinityMediaFiles(const model::BinItem& clip, const MediaDatabase& db) {
    QMap<QString, QStringList> out;
    for (const MediaSet& set : associatedMedia(clip, db, true)) {
        const QString heading = set.hasVideo() ? set.track(QStringLiteral("V1")).resolution
                                               : sampleRateHeading(set.any().sampleRate);
        QStringList& files = out[heading.isEmpty() ? QStringLiteral("Media") : heading];
        for (const auto& [track, file] : set.tracks) files << file.path;
    }
    // The clip's own managed files even when the database has not read them.
    if (const auto own = Link::fromJson(affinityLink(clip)); own && own->isManaged()) {
        bool listed = false;
        for (const QStringList& files : out) listed = listed || files.contains(own->file());
        if (!listed) out[QStringLiteral("Media")] << own->files();
    }
    for (QStringList& files : out) files.removeDuplicates();
    return out;
}

QJsonObject affinityLink(const model::BinItem& clip) {
    const QJsonValue kept = clip.link.value(kAffinityKey);
    if (kept.isObject()) return kept.toObject();
    return plainLink(clip.link);
}

QList<MediaCandidate> mediaCandidates(const model::BinItem& clip, const MediaDatabase& db) {
    QList<MediaCandidate> out;
    if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::AudioClip) return out;
    const QJsonObject own = affinityLink(clip);
    const auto ownLink = Link::fromJson(own);
    if (auto c = affinityCandidate(clip, own)) out << *c;
    // Its media at other resolutions, and those of clips sharing its source and timecode span (p.1100-1102); only media
    // holding the whole clip are played (ours: partly online media show in the bin's MultiRez headings).
    for (const MediaSet& set : associatedMedia(clip, db)) {
        if (ownLink && set.mediaId == ownLink->mediaId) continue;
        if (!hasTracks(set, ownLink) || mediaCoverage(set, clip) != MediaCoverage::Full) continue;
        out << setCandidate(set, clip.durationFrames);
    }
    std::stable_sort(out.begin(), out.end(), newer);
    return out;
}

std::optional<MediaCandidate> currentMedia(const model::BinItem& clip, const QList<MediaCandidate>& candidates) {
    if (clip.link.value(kOfflineKey).toBool()) return std::nullopt;
    const auto link = Link::fromJson(plainLink(clip.link));
    if (!link || link->unlinked) return std::nullopt;
    const bool onAffinity = !clip.link.value(kAffinityKey).isObject();
    for (const MediaCandidate& c : candidates) {
        if (onAffinity ? c.affinity : (!c.mediaId.isEmpty() && c.mediaId == link->mediaId)) return c;
    }
    return std::nullopt;
}

RelinkChoice chooseMedia(const QList<MediaCandidate>& candidates, const std::optional<MediaCandidate>& current,
                         const RelinkRules& rules, const Rational& projectRate, const QSize& projectRaster,
                         bool mixedRates, bool crossRate) {
    RelinkChoice choice;
    if (candidates.isEmpty()) return choice;
    const bool video = std::any_of(candidates.begin(), candidates.end(), [](const MediaCandidate& c) { return c.video; });
    auto recent = [](const MediaCandidate& a, const MediaCandidate& b) { return newer(a, b); };
    auto quality = [&](const MediaCandidate& a, const MediaCandidate& b) {
        return video ? better(a, b, projectRaster) : betterAudio(a, b);
    };
    auto lowest = [](const MediaCandidate& a, const MediaCandidate& b) { return a.bandwidth < b.bandwidth; };
    auto highest = [](const MediaCandidate& a, const MediaCandidate& b) { return a.bandwidth > b.bandwidth; };

    // The pool: media at the project's rate. "In case of no media with current project frame rate, Media Composer uses
    // the most recent online compression out of all available for that object" (p.1169).
    // Cross Rate Dynamic Relink (p.1109-1110): with Specific Resolution's Frame Rate, media at a multiple of the project's
    // rate count as the project's ("material available in 25fps and 50fps can be cross-rate relinked ... in projects with
    // frame rates multiple to 25").
    const bool rateChosen = video && rules.video == RelinkRules::Method::SpecificResolution && !rules.frameRate.isEmpty();
    QList<MediaCandidate> pool;
    for (const MediaCandidate& c : candidates) {
        if (!video || !c.video || sameRate(c.rate, projectRate) || projectRate.num() <= 0 ||
            (crossRate && rateChosen && multipleRates(c.rate.toDouble(), projectRate.toDouble()))) {
            pool << c;
        }
    }
    if (pool.isEmpty()) {
        if (!mixedRates) {
            choice.media = best(candidates, recent);
            choice.matched = true;
            return choice;
        }
        pool = candidates;
    } else if (mixedRates) {
        pool = candidates;
    }

    std::optional<MediaCandidate> chosen;
    if (video) {
        switch (rules.video) {
        case RelinkRules::Method::MostRecent: chosen = best(pool, recent); break;
        case RelinkRules::Method::HighestQuality: chosen = best(pool, quality); break;
        case RelinkRules::Method::MinimalBandwidth: chosen = best(pool, lowest); break;
        case RelinkRules::Method::SpecificResolution: {
            QList<MediaCandidate> matches;
            for (const MediaCandidate& c : pool) {
                if (c.video && passesVideo(c, rules, !mixedRates)) matches << c;
            }
            // A preferred frame rate (Allow Mixed Frame Rate Media, p.1110): media at it first; "In the case of no media
            // with exact frame rate match, Dynamic Relink will try to find the closest multiple frame rate value".
            if (mixedRates && !rules.frameRate.isEmpty()) {
                QList<MediaCandidate> atRate;
                for (const MediaCandidate& c : matches) {
                    if (sameRate(c.rate, rules.frameRate)) atRate << c;
                }
                const double want = rules.frameRate.toDouble();
                if (atRate.isEmpty() && want > 0) {
                    double closest = -1;
                    for (const MediaCandidate& c : matches) {
                        if (!multipleRates(c.rate.toDouble(), want)) continue;
                        const double d = std::abs(c.rate.toDouble() - want);
                        if (closest < 0 || d < closest - 0.001) {
                            closest = d;
                            atRate = {c};
                        } else if (std::abs(d - closest) < 0.001) {
                            atRate << c;
                        }
                    }
                }
                if (!atRate.isEmpty()) matches = atRate;
            }
            if (rules.preferTargetRaster) {
                QList<MediaCandidate> atRaster;
                for (const MediaCandidate& c : matches) {
                    if (c.raster == projectRaster) atRaster << c;
                }
                chosen = best(atRaster.isEmpty() ? matches : atRaster, highest);
            } else {
                chosen = best(matches, lowest);
            }
            break;
        }
        }
    } else {
        switch (rules.audio) {
        case RelinkRules::AudioMethod::MostRecent: chosen = best(pool, recent); break;
        case RelinkRules::AudioMethod::HighestQuality: chosen = best(pool, quality); break;
        case RelinkRules::AudioMethod::MostCompressed: chosen = best(pool, lowest); break;
        case RelinkRules::AudioMethod::SpecificQuality: {
            QList<MediaCandidate> matches;
            for (const MediaCandidate& c : pool) {
                if (passesAudio(c, rules)) matches << c;
            }
            chosen = best(matches, rules.preferHighestBitrate ? highest : lowest);
            break;
        }
        }
    }
    if (chosen) {
        choice.media = chosen;
        choice.matched = true;
        return choice;
    }
    // "If no match is found" (p.1170).
    switch (rules.noMatch) {
    case RelinkRules::NoMatch::HighestQuality: choice.media = best(pool, quality); break;
    case RelinkRules::NoMatch::LowerBandwidth: choice.media = best(pool, lowest); break;
    case RelinkRules::NoMatch::KeepExisting: choice.media = current; break;
    case RelinkRules::NoMatch::Offline: break;
    }
    return choice;
}

bool dynamicRelink(model::BinItem& clip, const MediaDatabase& db, const DynamicRelinkSettings& settings,
                   const Rational& projectRate, const QSize& projectRaster, RelinkChoice* out, bool crossRate) {
    if (!settings.enabled) return false;
    if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::AudioClip) return false;
    if (clip.link.isEmpty()) return false;
    // A clip the user unlinked (Clip > Modify > Unlink, p.368) stays offline.
    if (const auto link = Link::fromJson(plainLink(clip.link)); link && link->unlinked && !clip.link.value(kOfflineKey).toBool()) {
        return false;
    }
    const QList<MediaCandidate> candidates = mediaCandidates(clip, db);
    const auto current = currentMedia(clip, candidates);
    const RelinkChoice choice = chooseMedia(candidates, current, settings.active(), projectRate, projectRaster,
                                            settings.mixedRates, crossRate);
    if (out) *out = choice;
    const QJsonObject own = affinityLink(clip);
    // "Video format: This column displays the resolution of the media files to which the clips ... are currently linked"
    // (p.1116): the column follows the media, its own text kept for the way back.
    const QString ownVideo = clip.link.contains(kVideoKey) ? clip.link.value(kVideoKey).toString()
                                                           : clip.fields.value(QStringLiteral("Video"));
    QJsonObject next;
    QString video = ownVideo;
    if (!choice.media) {
        if (candidates.isEmpty()) return false; // nothing online at all: leave it as it is
        next = own;
        next.insert(QStringLiteral("unlinked"), true);
        next.insert(kOfflineKey, true);
        next.insert(kAffinityKey, own);
        next.insert(kVideoKey, ownVideo);
        video = clip.fields.value(QStringLiteral("Video"));
    } else if (choice.media->affinity) {
        next = own;
    } else {
        next = choice.media->link;
        next.insert(kAffinityKey, own);
        next.insert(kVideoKey, ownVideo);
        if (!choice.media->resolution.isEmpty()) video = choice.media->resolution;
    }
    if (next == clip.link) return false;
    clip.link = next;
    if (!video.isEmpty() || clip.fields.contains(QStringLiteral("Video"))) clip.fields.insert(QStringLiteral("Video"), video);
    return true;
}

bool restoreAffinityMedia(model::BinItem& clip) {
    if (!clip.link.contains(kAffinityKey) && !clip.link.contains(kOfflineKey)) return false;
    if (clip.link.contains(kVideoKey)) clip.fields.insert(QStringLiteral("Video"), clip.link.value(kVideoKey).toString());
    clip.link = affinityLink(clip);
    return true;
}

} // namespace lumenis::media
