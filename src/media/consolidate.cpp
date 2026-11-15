#include "lumenis/media/consolidate.h"

#include "lumenis/media/color_transform.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/color_adapter.h"
#include "lumenis/model/frame_flex.h"
#include "lumenis/model/group_clip.h"
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

// The managed files a clip plays (deleteOriginals): only those under an "Avid MediaFiles" folder, never the user's own.
QStringList managedFiles(const model::BinItem& clip) {
    QStringList out;
    const auto link = linkOf(clip);
    if (!link || !link->isManaged()) return out;
    for (const QString& f : link->files()) {
        if (QDir::fromNativeSeparators(QFileInfo(f).absoluteFilePath()).contains(QStringLiteral("/Avid MediaFiles/"))) out << f;
    }
    return out;
}

// The sample rate a clip's sound is in: its Audio SR column, else its first audio file's.
int clipSampleRate(const model::BinItem& clip) {
    if (const int rate = clip.fields.value(QStringLiteral("Audio SR")).toInt(); rate > 0) return rate;
    const auto link = linkOf(clip);
    if (!link || link->channels <= 0) return 0;
    if (const auto info = probe(link->audioFile(0))) {
        for (const StreamInfo& st : info->streams) {
            if (st.sampleRate > 0) return st.sampleRate;
        }
    }
    return 0;
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
    // The clip being transcoded is to keep the new media itself (Create new clips off).
    bool associating = false;
    // The clip takes the copy itself (its files name it) unless masters stay linked; always when the originals go.
    const bool takesCopy = !options.keepMastersLinked || options.deleteOriginals;
    // The sound's rate (p.352, p.355): the creation's, or the clip's own when not converted and it can be made.
    auto withRate = [&](MediaCreation creation, const model::BinItem& clip) {
        if (!options.convertSampleRate) {
            if (const int rate = clipSampleRate(clip); rate > 0 && isImportRate(rate)) creation.sampleRate = rate;
        }
        return creation;
    };
    auto make = [&](const model::BinItem& clip, std::int64_t in, std::int64_t out, QString* error) {
        if (copy) {
            MediaCreation creation = withRate(options.creation, clip);
            if (takesCopy && in == 0 && out < 0) creation.master = clip.mobId;
            return consolidateClip(clip, in, out, creation, error);
        }
        MediaCreation creation = withRate(options.creation, clip);
        creation.raster = rasterFor(clip, options.raster, options.creation.raster);
        // Frame Flex applied (p.355): the clip's box and Reformat, with its pixels' shape, into the media.
        if (options.applyFraming) {
            if (const auto adapter = model::spatialAdapterOf(clip)) creation.spatial = model::withPixelAspect(*adapter, clip);
        }
        if (associating) creation.master = model::mobIdOf(clip);
        const auto encoding = model::colorAdapterOf(clip);
        if (options.applyColorEncoding && encoding) {
            creation.colorTransformations = encoding->applied();
            if (options.acesWorkingSpace) creation.colorTransformations = withAcesWorkingSpace(creation.colorTransformations);
        }
        auto made = transcodeClip(clip, in, out, creation, error);
        // The new clip has the colours in its media, or keeps the clip's encoding to apply them itself.
        if (made) model::setColorEncoding(*made, creation.colorTransformations.isEmpty() ? model::colorEncodingOf(clip) : std::nullopt);
        return made;
    };

    // A group clip remade (p.351, "Consolidate all clips in a group edit"; 11.4a2): each angle in `ranges` (the angle's
    // clip frames [first, end), end -1 for all of it) gets new media and a new master clip; the new group, "<name>.new.nn",
    // plays them, its offsets moved by where each new clip starts (`shifts`, for the segments' own offsets). Angles not
    // in `ranges` keep their clips. Nothing, with `error` set or a reason in result.skipped, when it was not made.
    auto remakeGroup = [&](const model::BinItem& groupItem, const std::map<int, std::pair<std::int64_t, std::int64_t>>& ranges,
                           std::map<int, std::int64_t>& shifts, QString* error) -> std::optional<model::BinItem> {
        auto group = model::groupOf(groupItem);
        if (!group || model::isMultigroup(groupItem)) {
            result.skipped << QObject::tr("%1: the media of a group of groups is not made new; consolidate its groups.").arg(groupItem.name);
            return std::nullopt;
        }
        bool any = false;
        for (const auto& [angle, range] : ranges) {
            if (angle < 0 || angle >= group->angles.size()) continue;
            model::GroupAngle& a = group->angles[angle];
            if (const QString why = refuse(a.clip); !why.isEmpty()) {
                result.skipped << why;
                continue;
            }
            const std::int64_t from = std::clamp<std::int64_t>(range.first, 0, a.clip.durationFrames);
            const std::int64_t to = range.second < 0 ? -1 : std::clamp<std::int64_t>(range.second, from, a.clip.durationFrames);
            if (to >= 0 && to <= from) continue; // the angle has no material there
            auto made = make(a.clip, from, to, error);
            if (!made) return std::nullopt;
            if (copy && options.deleteOriginals) result.deleteFiles << managedFiles(a.clip);
            made->name = numberedName(a.clip.name, QStringLiteral(".new"), names);
            names << made->name;
            result.added << *made;
            a.binId = binId;
            a.clipId = made->id;
            a.clip = *made;
            a.offset -= from;
            shifts[angle] = from;
            any = true;
        }
        if (!any) return std::nullopt;
        model::BinItem out = groupItem;
        out.id = model::newItemId();
        out.mobId = MobId::generate();
        out.name = numberedName(groupItem.name, QStringLiteral(".new"), names);
        names << out.name;
        out.created = out.modified = QDateTime::currentDateTimeUtc();
        model::storeGroup(out, *group, groupItem.durationFrames);
        return out;
    };

    for (const model::BinItem& item : selection) {
        if (!result.error.isEmpty()) break;
        // A group clip in the bin: all its angles whole with Consolidate all clips in a group edit, else the angle it
        // shows (ours).
        if (item.kind == model::BinItemKind::Group) {
            const auto group = model::groupOf(item);
            std::map<int, std::pair<std::int64_t, std::int64_t>> ranges;
            for (int a = 0; group && a < group->angles.size(); ++a) {
                if (options.groupEdits || a == group->selected) ranges[a] = {0, -1};
            }
            std::map<int, std::int64_t> shifts;
            QString error;
            const auto made = remakeGroup(item, ranges, shifts, &error);
            if (!error.isEmpty()) {
                result.error = error;
                break;
            }
            if (made) result.added << *made;
            continue;
        }
        if (isClip(item)) {
            // Relink before skipping (p.352): media of the clip's own on the target drive, which it does not play yet.
            if (options.skipOnTarget && options.relinkBeforeSkipping && options.mediaOnTarget) {
                const auto link = linkOf(item);
                if (link && !(link->isManaged() && onDrive(*link, target))) {
                    if (const auto there = options.mediaOnTarget(item, target); there && onDrive(*there, target)) {
                        model::BinItem relinked = item;
                        relinked.link = there->toJson();
                        relinked.modified = QDateTime::currentDateTimeUtc();
                        result.changed << relinked;
                        result.skipped << QObject::tr("%1 was relinked to its media on the target drive.").arg(item.name);
                        continue;
                    }
                }
            }
            if (const QString why = refuse(item); !why.isEmpty()) {
                result.skipped << why;
                continue;
            }
            QString error;
            associating = !copy && !options.newClips;
            // Convert to Project Frame Rate (p.355): the clip retimed to the project's rate, its frames and sound taken by
            // time from the same media (Link::fileFrame), as long in seconds as it was.
            model::BinItem source = item;
            if (!copy && options.convertToProjectRate && options.projectRate.num() > 0 && !(item.editRate == options.projectRate)) {
                source.editRate = options.projectRate;
                source.durationFrames = std::max<std::int64_t>(
                    1, (Rational(item.durationFrames) * options.projectRate / item.editRate).round());
            }
            auto made = make(source, 0, -1, &error);
            associating = false;
            if (!made) {
                result.error = error;
                break;
            }
            if (!copy && !options.newClips) {
                // The clip keeps its link; its new media wait in the media database for Dynamic Relink.
                result.associated << item;
                continue;
            }
            made->markers = item.markers;
            if (copy && takesCopy) {
                if (options.deleteOriginals) result.deleteFiles << managedFiles(item);
                // The clip takes the copy; ".old", a new master clip, keeps the media it had (p.349). The
                // clip keeps its MobID, so the sequences that name it play the copy: the copy's files
                // were made naming it (make()), and it takes their file sources. The old files still
                // name the clip's MobID too; they hold the same frames.
                model::BinItem relinked = item;
                relinked.link = made->link;
                relinked.fields = made->fields;
                relinked.sources = made->sources;
                if (relinked.mobId.isNull()) relinked.mobId = made->mobId; // a clip from before MobIDs
                relinked.modified = made->modified;
                result.changed << relinked;
                // Deleted originals leave no media for an ".old" clip.
                if (!options.deleteOriginals) {
                    model::BinItem old = item;
                    old.id = model::newItemId();
                    old.mobId = MobId::generate();
                    old.name = numberedName(item.name, QStringLiteral(".old"), names);
                    names << old.name;
                    result.added << old;
                }
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
            // A group's segments: each angle's clip frames used, by angle (the shown ones, or every angle with
            // groupEdits), from the segments' own offsets.
            std::map<int, std::pair<std::int64_t, std::int64_t>> angles;
        };
        std::map<QString, Use> uses; // by clip id; in order, so results are stable
        for (const model::Track& track : sequence.tracks()) {
            for (const model::Segment& s : track.segments) {
                if (s.isFiller()) continue;
                auto [it, fresh] = uses.try_emplace(s.clipId, Use{s.binId, s.sourceIn, s.sourceIn + s.length, {}});
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
            std::map<int, std::int64_t> shifts; // a remade group's angles: where each new clip starts in the old
        };
        QHash<QString, Made> remap;
        // Each clip used gets new media for its used range plus handles, clamped to the clip.
        for (const auto& [clipId, use] : uses) {
            const auto clip = clips ? clips(use.binId, clipId) : std::nullopt;
            if (!clip) {
                result.skipped << QObject::tr("%1: a clip it uses is not in an open bin.").arg(item.name);
                continue;
            }
            // A group edit (p.351): the angles it shows (the segments' own angle and the Group menu's patched audio
            // angles), or with Consolidate all clips in a group edit every angle, over the group frames used.
            if (clip->kind == model::BinItemKind::Group) {
                const auto group = model::groupOf(*clip);
                std::map<int, std::pair<std::int64_t, std::int64_t>> ranges;
                for (const model::Track& track : sequence.tracks()) {
                    for (const model::Segment& seg : track.segments) {
                        if (seg.isFiller() || seg.clipId != clipId || !group) continue;
                        QSet<int> shown;
                        for (int a = 0; a < group->angles.size(); ++a) {
                            if (options.groupEdits) shown << a;
                        }
                        shown << seg.groupAngle.value_or(group->selected);
                        for (const int a : group->trackAngles) shown << a;
                        for (const int a : shown) {
                            if (a < 0 || a >= group->angles.size()) continue;
                            const std::int64_t offset = seg.groupOffsets.value(a, group->angles.at(a).offset);
                            const std::int64_t first = seg.sourceIn + offset - options.handles;
                            const std::int64_t end = seg.sourceIn + seg.length + offset + options.handles;
                            auto [r, fresh] = ranges.try_emplace(a, first, end);
                            if (!fresh) r->second = {std::min(r->second.first, first), std::max(r->second.second, end)};
                        }
                    }
                }
                std::map<int, std::int64_t> shifts;
                QString error;
                const auto made = remakeGroup(*clip, ranges, shifts, &error);
                if (!error.isEmpty()) {
                    result.error = error;
                    break;
                }
                if (!made) continue;
                remap.insert(clipId, {made->id, made->mobId, 0, shifts});
                result.added << *made;
                continue;
            }
            if (const QString why = refuse(*clip); !why.isEmpty()) {
                result.skipped << why;
                continue;
            }
            const std::int64_t from = std::max<std::int64_t>(use.first - options.handles, 0);
            const std::int64_t to = std::min(use.end + options.handles, clip->durationFrames);
            QString error;
            // Without Create new clips the clip keeps the media of the range it is used for: partly online at that
            // resolution (p.1118: "Consolidating and Transcoding a Portion of a Clip"), its segments left as they are.
            associating = !copy && !options.newClips;
            auto made = make(*clip, from, to, &error);
            const bool kept = associating;
            associating = false;
            if (!made) {
                result.error = error;
                break;
            }
            if (kept) {
                const bool listed = std::any_of(result.associated.begin(), result.associated.end(),
                                                [&](const model::BinItem& a) { return a.id == clip->id; });
                if (!listed) result.associated << *clip;
                continue;
            }
            // The clip's original files go too (p.351): the clip itself is then offline, as other sequences using it.
            if (copy && options.deleteOriginals) result.deleteFiles << managedFiles(*clip);
            made->name = numberedName(clip->name, QStringLiteral(".new"), names);
            names << made->name;
            remap.insert(clipId, {made->id, made->mobId, from, {}});
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
                // A remade group's segment keeps its own offsets, moved as the group's are.
                for (const auto& [angle, shift] : it->shifts) {
                    if (angle < s.groupOffsets.size()) s.groupOffsets[angle] -= shift;
                }
                // Colours transcoded into the media are not applied again (p.544).
                if (!copy && options.applyColorEncoding) s.color.reset();
                // So is the framing (p.355).
                if (!copy && options.applyFraming) s.spatial.reset();
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
    result.deleteFiles.removeDuplicates();
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
