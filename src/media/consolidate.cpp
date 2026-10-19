#include "lumenis/media/consolidate.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QObject>

#include <algorithm>
#include <map>

namespace lumenis::media {

namespace {

bool isClip(const model::BinItem& item) {
    return item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
}

// Whether every file of a managed clip is on a drive already.
bool onDrive(const Link& link, const QString& drive) {
    if (drive.isEmpty()) return false;
    const QString root = QDir::cleanPath(QFileInfo(mediaFilesFolder(drive)).absoluteFilePath()) + QLatin1Char('/');
    const QStringList files = link.files();
    return std::all_of(files.begin(), files.end(), [&](const QString& f) {
        return QDir::cleanPath(QFileInfo(f).absoluteFilePath()).startsWith(root);
    });
}

} // namespace

std::int64_t defaultHandles(const Rational& editRate) {
    const double fps = editRate.toDouble();
    if (editRate.den() == 1001 && fps > 29.0) return 60; // 29.97, 59.94
    if (editRate.den() == 1 && (editRate.num() == 25 || editRate.num() == 50)) return 50;
    return 24;
}

QSize rasterFor(const model::BinItem& clip, ConsolidateOptions::Raster raster, QSize project) {
    if (raster == ConsolidateOptions::Raster::Project) return project;
    const auto link = linkOf(clip);
    const auto info = link && link->videoStream >= 0 ? probe(link->path) : std::nullopt;
    QSize size = project;
    if (info) {
        for (const StreamInfo& s : info->streams) {
            if (s.index == link->videoStream && s.width > 0 && s.height > 0) size = QSize(s.width, s.height);
        }
    }
    // A quarter of the picture is half as wide and high; a sixteenth, a
    // quarter.
    const int divide = raster == ConsolidateOptions::Raster::SourceQuarter     ? 2
                       : raster == ConsolidateOptions::Raster::SourceSixteenth ? 4
                                                                                : 1;
    size = QSize(size.width() / divide / 2 * 2, size.height() / divide / 2 * 2);
    return size.expandedTo(kMinimumRaster);
}

QString numberedName(const QString& name, const QString& suffix, const QStringList& taken) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1%2.%3").arg(name, suffix).arg(n, 2, 10, QLatin1Char('0'));
        if (!taken.contains(candidate)) return candidate;
    }
}

ConsolidateResult consolidate(const QList<model::BinItem>& selection, const QString& binId, const ClipLookup& clips,
                              const QStringList& taken, const ConsolidateOptions& options) {
    ConsolidateResult result;
    QStringList names = taken;
    const bool copy = options.mode == ConsolidateOptions::Mode::Consolidate;
    const QString& target = options.creation.videoDrive;

    // Which clip, if it should be made new: the reason it is not otherwise.
    auto refuse = [&](const model::BinItem& clip) -> QString {
        const auto link = linkOf(clip);
        if (!link || link->unlinked) return QObject::tr("%1 has no media.").arg(clip.name);
        if (options.linkedOnly && link->isManaged()) return QObject::tr("%1 is not linked media.").arg(clip.name);
        if (options.skipOnTarget && link->isManaged() && onDrive(*link, target)) {
            return QObject::tr("%1's media is on the target drive already.").arg(clip.name);
        }
        if (copy && !canConsolidate(clip)) {
            return QObject::tr("%1's media cannot be copied as it is; transcode it.").arg(clip.name);
        }
        return {};
    };
    auto make = [&](const model::BinItem& clip, std::int64_t in, std::int64_t out, QString* error) {
        if (copy) return consolidateClip(clip, in, out, options.creation, error);
        MediaCreation creation = options.creation;
        creation.raster = rasterFor(clip, options.raster, options.creation.raster);
        return transcodeClip(clip, in, out, creation, error);
    };

    for (const model::BinItem& item : selection) {
        if (!result.error.isEmpty()) break;
        if (isClip(item)) {
            if (const QString why = refuse(item); !why.isEmpty()) {
                result.skipped << why;
                continue;
            }
            QString error;
            auto made = make(item, 0, -1, &error);
            if (!made) {
                result.error = error;
                break;
            }
            made->markers = item.markers;
            if (copy && !options.keepMastersLinked) {
                // The clip takes the copy; ".old" keeps the media it had.
                model::BinItem old = item;
                old.id = model::newItemId();
                old.name = numberedName(item.name, QStringLiteral(".old"), names);
                names << old.name;
                model::BinItem relinked = item;
                relinked.link = made->link;
                relinked.fields = made->fields;
                relinked.modified = made->modified;
                result.changed << relinked;
                result.added << old;
            } else {
                made->name = numberedName(item.name, QStringLiteral(".new"), names);
                names << made->name;
                result.added << *made;
            }
            continue;
        }
        if (item.kind != model::BinItemKind::Sequence) {
            result.skipped << QObject::tr("%1: only master clips and sequences have their media made new.").arg(item.name);
            continue;
        }
        // The parts of each clip the sequence uses, with handles.
        model::Sequence sequence = model::Sequence::fromItem(item);
        struct Use {
            QString binId;
            std::int64_t first = 0;
            std::int64_t end = 0;
        };
        std::map<QString, Use> uses; // by clip id; in order, so results are stable
        for (const model::Track& track : sequence.tracks()) {
            for (const model::Segment& s : track.segments) {
                if (s.isFiller()) continue;
                auto [it, fresh] = uses.try_emplace(s.clipId, Use{s.binId, s.sourceIn, s.sourceIn + s.length});
                if (!fresh) {
                    it->second.first = std::min(it->second.first, s.sourceIn);
                    it->second.end = std::max(it->second.end, s.sourceIn + s.length);
                }
            }
        }
        struct Made {
            QString id;
            MobId mobId;
            std::int64_t from = 0;
        };
        QHash<QString, Made> remap;
        for (const auto& [clipId, use] : uses) {
            const auto clip = clips ? clips(use.binId, clipId) : std::nullopt;
            if (!clip) {
                result.skipped << QObject::tr("%1: a clip it uses is not in an open bin.").arg(item.name);
                continue;
            }
            if (const QString why = refuse(*clip); !why.isEmpty()) {
                result.skipped << why;
                continue;
            }
            const std::int64_t from = std::max<std::int64_t>(use.first - options.handles, 0);
            const std::int64_t to = std::min(use.end + options.handles, clip->durationFrames);
            QString error;
            auto made = make(*clip, from, to, &error);
            if (!made) {
                result.error = error;
                break;
            }
            made->name = numberedName(clip->name, QStringLiteral(".new"), names);
            names << made->name;
            remap.insert(clipId, {made->id, made->mobId, from});
            result.added << *made;
        }
        if (!result.error.isEmpty()) break;
        if (remap.isEmpty()) continue;
        // The sequence's segments now use the new clips (p.349).
        for (const QString& name : sequence.trackNames()) {
            model::Track* track = sequence.track(name);
            if (!track) continue;
            for (model::Segment& s : track->segments) {
                const auto it = remap.constFind(s.clipId);
                if (it == remap.constEnd()) continue;
                s.clipId = it->id;
                s.sourceMob = it->mobId;
                s.binId = binId;
                s.sourceIn -= it->from;
            }
        }
        model::BinItem relinked = item;
        sequence.storeIn(relinked);
        relinked.modified = QDateTime::currentDateTimeUtc();
        if (options.newSequences) {
            relinked.id = model::newItemId();
            relinked.name = numberedName(item.name, QStringLiteral(".new"), names);
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
