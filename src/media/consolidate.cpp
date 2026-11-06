#include "lumenis/media/consolidate.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>

#include <algorithm>
#include <map>

// Clip > Consolidate/Transcode over a bin selection (Editing Guide p.349-356). This file decides
// what to make and how the bin changes; the media itself is written by consolidateClip() and
// transcodeClip() in managed.cpp. The dialog is src/ui/consolidate_dialog.cpp; the command runs from
// src/ui/main_window.cpp, through the Background Queue (src/ui/background_queue.cpp).

namespace lumenis::media {

namespace {

// Master clips and audio clips: the items that point at media themselves.
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

// Source rasters are read from the clip's file (probe()); without a picture or a readable file the
// project's raster is used. Sizes are kept even and never below kMinimumRaster.
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

// Tries ".01", ".02", ... until the name is free.
QString numberedName(const QString& name, const QString& suffix, const QStringList& taken) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1%2.%3").arg(name, suffix).arg(n, 2, 10, QLatin1Char('0'));
        if (!taken.contains(candidate)) return candidate;
    }
}

// Goes through the selection in order and stops at the first error (which also leaves what was made
// so far in `added`). `names` grows with each new name, so numbering stays unique within one run.
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
    // Makes the media of frames [in, out) (out -1: to the end): copied for Consolidate, otherwise
    // transcoded at the raster the options choose for this clip.
    auto make = [&](const model::BinItem& clip, std::int64_t in, std::int64_t out, QString* error) {
        if (copy) {
            // The clip itself takes the copy unless masters stay linked: the files then name it.
            MediaCreation creation = options.creation;
            if (!options.keepMastersLinked && in == 0 && out < 0) creation.master = clip.mobId;
            return consolidateClip(clip, in, out, creation, error);
        }
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
                // The clip takes the copy; ".old", a new master clip, keeps the media it had (p.349). The
                // clip keeps its MobID, so the sequences that name it play the copy: the copy's files
                // were made naming it (make()), and it takes their file sources. The old files still
                // name the clip's MobID too; they hold the same frames.
                model::BinItem old = item;
                old.id = model::newItemId();
                old.mobId = MobId::generate();
                old.name = numberedName(item.name, QStringLiteral(".old"), names);
                names << old.name;
                model::BinItem relinked = item;
                relinked.link = made->link;
                relinked.fields = made->fields;
                relinked.sources = made->sources;
                if (relinked.mobId.isNull()) relinked.mobId = made->mobId; // a clip from before MobIDs
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
        // Each clip used gets new media for its used range plus handles, clamped to the clip.
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
        // Each segment that used a remade clip points at the new one, which starts at `from`.
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

CopiedMedia copyClipMedia(const QList<model::BinItem>& clips, const QString& folder, bool video, bool audio,
                          const QSet<QString>& exclude) {
    CopiedMedia out;
    QDir().mkpath(folder);
    QHash<QString, QString> copied; // source file -> its copy
    for (model::BinItem clip : clips) {
        for (model::ClipSource& s : clip.sources) {
            if (s.kind != model::ClipSource::Kind::File || s.path.isEmpty()) continue;
            const bool isVideo = s.track.startsWith(QLatin1Char('V'));
            if ((isVideo && !video) || (!isVideo && !audio)) continue;
            const QString from = QFileInfo(s.path).absoluteFilePath();
            if (exclude.contains(from)) continue;
            if (const auto it = copied.constFind(from); it != copied.constEnd()) {
                s.path = *it;
                continue;
            }
            if (!QFileInfo::exists(from)) {
                out.warnings << QObject::tr("%1: its media file %2 is offline").arg(clip.name, QDir::toNativeSeparators(from));
                continue;
            }
            const QFileInfo f(from);
            QString to = QDir(folder).filePath(f.fileName());
            for (int n = 2; QFileInfo::exists(to); ++n) {
                to = QDir(folder).filePath(QStringLiteral("%1 %2.%3").arg(f.completeBaseName()).arg(n).arg(f.suffix()));
            }
            if (!QFile::copy(from, to)) {
                out.warnings << QObject::tr("%1: %2 could not be copied").arg(clip.name, QDir::toNativeSeparators(from));
                continue;
            }
            copied.insert(from, to);
            s.path = to;
            ++out.files;
        }
        out.items << clip;
    }
    return out;
}

} // namespace lumenis::media
