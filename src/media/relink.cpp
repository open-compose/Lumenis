#include "lumenis/media/relink.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

namespace lumenis::media {

namespace {

std::int64_t framesOf(const QString& timecode, const Rational& rate) {
    if (timecode.isEmpty()) return 0;
    QString plain = timecode;
    plain.replace(QLatin1Char(';'), QLatin1Char(':'));
    const auto tc = Timecode::parse(plain.toStdString(), TimecodeFormat::forRate(rate, false));
    return tc ? tc->frameCount() : 0;
}

bool isMaster(const model::BinItem& item) {
    return item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
}

// The tracks a clip has: picture, and how many audio channels.
std::pair<bool, int> tracksOf(const model::BinItem& clip) {
    if (const auto link = linkOf(clip)) return {link->videoStream >= 0, link->channels};
    // "V1 A1-2", "A1", ...
    bool video = false;
    int audio = 0;
    for (const QString& part : clip.fields.value(QStringLiteral("Tracks")).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (part.startsWith(QLatin1Char('V'))) video = true;
        if (part.startsWith(QLatin1Char('A'))) {
            const qsizetype dash = part.indexOf(QLatin1Char('-'));
            audio = dash > 0 ? part.mid(dash + 1).toInt() : std::max(audio, part.mid(1).toInt());
        }
    }
    return {video, audio};
}

bool onDrives(const QString& path, const QStringList& drives) {
    if (drives.isEmpty()) return true;
    const QString file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return std::any_of(drives.begin(), drives.end(), [&](const QString& d) {
        return file.startsWith(QDir::cleanPath(QFileInfo(mediaFilesFolder(d)).absoluteFilePath()) + QLatin1Char('/'));
    });
}

// Whether a candidate's quality passes the relink method, and which of two
// is better by it.
bool passes(int quality, const RelinkOptions& o) {
    if (o.method != RelinkOptions::Method::Specific) return true;
    const int wanted = qualityOf(o.resolution);
    if (wanted < 0) return true;
    switch (o.quality) {
    case RelinkOptions::Quality::AtLeast: return quality >= wanted;
    case RelinkOptions::Quality::Equal: return quality == wanted;
    case RelinkOptions::Quality::AtMost: return quality <= wanted;
    }
    return true;
}

bool better(int a, int b, const RelinkOptions& o) {
    switch (o.method) {
    case RelinkOptions::Method::Highest: return a > b;
    case RelinkOptions::Method::MostCompressed: return a < b;
    case RelinkOptions::Method::Specific: {
        // The nearest to the one asked for.
        const int wanted = qualityOf(o.resolution);
        return std::abs(a - wanted) < std::abs(b - wanted);
    }
    }
    return false;
}

bool covers(const Material& outer, const Material& inner, const RelinkOptions& o) {
    if (!(outer.rate == inner.rate) && !o.acrossRates) return false;
    return outer.start <= inner.start && outer.start + outer.frames >= inner.start + inner.frames;
}

} // namespace

Material materialOf(const model::BinItem& clip, RelinkOptions::By by) {
    Material m;
    if (by == RelinkOptions::By::SourceName) {
        m.name = clip.fields.value(QStringLiteral("Tape"));
        if (m.name.isEmpty()) m.name = clip.fields.value(QStringLiteral("Source File"));
    }
    if (m.name.isEmpty()) m.name = clip.name;
    m.rate = clip.editRate;
    m.start = framesOf(clip.fields.value(QStringLiteral("Start")), clip.editRate);
    m.frames = clip.durationFrames;
    return m;
}

Material materialOf(const MediaFileInfo& file, RelinkOptions::By by) {
    Material m;
    if (by == RelinkOptions::By::SourceName) m.name = file.tape.isEmpty() ? file.source : file.tape;
    if (m.name.isEmpty()) m.name = file.clipName;
    m.rate = file.rate;
    m.start = framesOf(file.start, file.rate);
    m.frames = file.frames;
    return m;
}

bool sameName(const QString& a, const QString& b, const RelinkOptions& options) {
    auto clean = [&](QString s) {
        if (options.ignoreExtension) {
            const qsizetype dot = s.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) s.truncate(dot);
        }
        if (!options.ignoreAfter.isEmpty()) {
            const qsizetype at = s.lastIndexOf(options.ignoreAfter);
            if (at >= 0) s.truncate(at);
        }
        return s;
    };
    return clean(a).compare(clean(b), options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
}

int qualityOf(const QString& resolution) {
    // A copied DNxHR picture is named for its profile: "DNxHR HQ".
    const QString name = resolution.startsWith(QLatin1String("DNxHR ")) ? QStringLiteral("Avid DNx ") + resolution.mid(6)
                                                                         : resolution;
    const QList<Resolution>& all = resolutions();
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (all.at(i).id == name || all.at(i).name == name) return static_cast<int>(i);
    }
    return -1;
}

std::optional<Link> findMedia(const model::BinItem& clip, const MediaDatabase& db, const RelinkOptions& options) {
    const Material want = materialOf(clip, options.by);
    const auto [video, audio] = tracksOf(clip);
    std::optional<MediaSet> best;
    int bestQuality = 0;
    for (const MediaSet& set : db.sets()) {
        const MediaFileInfo& f = set.any();
        if (!onDrives(f.path, options.drives)) continue;
        if (options.currentProjectOnly && f.project != options.project) continue;
        if ((video && !set.hasVideo()) || set.audioTracks() < audio) continue;
        const Material have = materialOf(f, options.by);
        if (!sameName(have.name, want.name, options) || !covers(have, want, options)) continue;
        const int quality = qualityOf(set.hasVideo() ? set.track(QStringLiteral("V1")).resolution : f.resolution);
        if (!passes(quality, options)) continue;
        if (!best || better(quality, bestQuality, options)) {
            best = set;
            bestQuality = quality;
        }
    }
    if (!best) return std::nullopt;
    const Material have = materialOf(best->any(), options.by);
    Link link = best->link(want.start - have.start);
    // Only the clip's tracks: a set may hold more channels.
    if (!video) {
        link.videoStream = -1;
        if (!link.audioPaths.isEmpty()) link.path = link.audioPaths.constFirst();
    }
    while (link.audioPaths.size() > audio) {
        link.audioPaths.removeLast();
        link.audioStreams.removeLast();
        link.streamChannels.removeLast();
        link.channels -= 1;
    }
    return link;
}

RelinkResult relinkManaged(const QList<model::BinItem>& selection, const MediaDatabase& db,
                           const QList<BinClip>& candidates, const ClipLookup& lookup, const RelinkOptions& options,
                           const QStringList& taken) {
    RelinkResult result;
    QStringList names = taken;
    for (const model::BinItem& item : selection) {
        if (isMaster(item)) {
            if (!options.masterClips) continue;
            const auto link = findMedia(item, db, options);
            if (!link) {
                result.notRelinked << item.name;
                if (options.unlinkWithoutMatch) {
                    model::BinItem offline = item;
                    if (unlinkClip(offline)) result.changed << offline;
                }
                continue;
            }
            model::BinItem relinked = item;
            relinked.link = link->toJson();
            // The columns the new media changes.
            if (const auto set = db.set(link->mediaId)) {
                const QString res = set->hasVideo() ? set->track(QStringLiteral("V1")).resolution : QString();
                if (!res.isEmpty()) relinked.fields.insert(QStringLiteral("Video"), res);
            }
            if (const QStorageInfo volume(link->path); volume.isValid()) {
                relinked.fields.insert(QStringLiteral("Drive"),
                                       volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName());
            }
            relinked.fields.remove(QStringLiteral("Plug-In"));
            result.changed << relinked;
            continue;
        }
        if (item.kind != model::BinItemKind::Sequence || !options.otherItems) continue;
        // Each segment to the best master clip holding its material.
        model::Sequence sequence = model::Sequence::fromItem(item);
        bool any = false;
        bool missed = false;
        for (const QString& name : sequence.trackNames()) {
            model::Track* track = sequence.track(name);
            if (!track || track->kind == model::TrackKind::Data) continue;
            for (model::Segment& s : track->segments) {
                if (s.isFiller()) continue;
                const auto clip = lookup ? lookup(s.binId, s.clipId) : std::nullopt;
                if (!clip) {
                    missed = true;
                    continue;
                }
                Material used = materialOf(*clip, options.by);
                used.start += s.sourceIn;
                used.frames = s.length;
                const auto ref = model::parseTrackName(s.sourceTrack);
                const BinClip* best = nullptr;
                int bestQuality = 0;
                for (const BinClip& c : candidates) {
                    if (!isMaster(c.item)) continue;
                    const auto link = linkOf(c.item);
                    if (!link || link->unlinked) continue;
                    if (ref) {
                        if (ref->kind == model::TrackKind::Video && link->videoStream < 0) continue;
                        if (ref->kind == model::TrackKind::Audio && link->channels < ref->number) continue;
                    }
                    if (options.currentProjectOnly && link->isManaged()) {
                        const auto set = db.set(link->mediaId);
                        if (set && set->any().project != options.project) continue;
                    }
                    if (link->isManaged() && !onDrives(link->path, options.drives)) continue;
                    const Material have = materialOf(c.item, options.by);
                    if (!sameName(have.name, used.name, options) || !covers(have, used, options)) continue;
                    const int quality = qualityOf(c.item.fields.value(QStringLiteral("Video")));
                    if (!passes(quality, options)) continue;
                    if (!best || better(quality, bestQuality, options)) {
                        best = &c;
                        bestQuality = quality;
                    }
                }
                if (!best) {
                    missed = true;
                    continue;
                }
                const std::int64_t sourceIn = used.start - materialOf(best->item, options.by).start;
                if (best->item.id == s.clipId && sourceIn == s.sourceIn) continue;
                s.clipId = best->item.id;
                s.binId = best->binId;
                s.sourceIn = sourceIn;
                any = true;
            }
        }
        if (missed) result.notRelinked << item.name;
        if (!any) continue;
        model::BinItem relinked = item;
        sequence.storeIn(relinked);
        relinked.modified = QDateTime::currentDateTimeUtc();
        if (options.newSequences) {
            relinked.id = model::newItemId();
            relinked.name = item.name + QStringLiteral(".relinked");
            if (names.contains(relinked.name)) relinked.name = numberedName(relinked.name, QString(), names);
            names << relinked.name;
            relinked.created = relinked.modified;
            result.added << relinked;
        } else {
            result.changed << relinked;
        }
    }
    return result;
}

} // namespace lumenis::media
