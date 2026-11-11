// Sync Clips (sync_clips.h; Editing Guide p.496-498).
#include "lumenis/model/sync_clips.h"

#include "lumenis/core/timecode.h"

#include <QDateTime>
#include <QJsonObject>

#include <algorithm>
#include <limits>

namespace lumenis::model {

namespace {

constexpr char kSyncedKey[] = "syncedSubclip";

bool hasTrack(const BinItem& clip, TrackKind kind) {
    for (const QString& t : sourceTracksOf(clip)) {
        if (const auto ref = parseTrackName(t); ref && ref->kind == kind) return true;
    }
    return false;
}

// The audio track numbers of a clip, in order.
QList<int> audioTracks(const BinItem& clip) {
    QList<int> out;
    for (const QString& t : sourceTracksOf(clip)) {
        if (const auto ref = parseTrackName(t); ref && ref->kind == TrackKind::Audio) out << ref->number;
    }
    return out;
}

// The sync point (group_clip.h's convention: frame f of the clip lines up at f - point). Film TC/Sound TC: the video
// clip's Film TC and an audio clip's Sound TC (p.497: "matching film and sound timecode recorded in the field").
std::optional<std::int64_t> syncPoint(const BinItem& clip, bool video, const SyncClipsOptions& options, QString* why) {
    if (options.sync.sync == GroupSync::FilmTc) {
        const QString heading = video ? QStringLiteral("Film TC") : QStringLiteral("Sound TC");
        const auto tc = timecodeFrames(clip.fields.value(heading), clip.editRate);
        if (!tc) {
            *why = QStringLiteral("\"%1\" has no %2.").arg(clip.name, heading);
            return std::nullopt;
        }
        return -*tc;
    }
    return groupSyncPoint(clip, options.sync, why);
}

QString syncedName(const Bin& bin, const QString& name, const QStringList& taken) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.sync.%2").arg(name).arg(n);
        const bool used = taken.contains(candidate) ||
                          std::any_of(bin.items.cbegin(), bin.items.cend(), [&](const BinItem& i) { return i.name == candidate; });
        if (!used) return candidate;
    }
}

Segment clipSegment(const BinItem& clip, const QString& binId, const QString& track, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = clip.id;
    s.sourceMob = clip.mobId;
    s.binId = binId;
    s.name = clip.name;
    s.sourceTrack = track;
    s.sourceIn = in;
    s.length = length;
    return s;
}

struct Placed {
    const BinItem* clip;
    std::int64_t start; // where its frame 0 lines up
    std::int64_t end;
};

} // namespace

SyncClipsResult syncClips(const Bin& bin, const QString& binId, const QList<BinItem>& clips, const SyncClipsOptions& options) {
    SyncClipsResult out;
    QList<Placed> videos, audios;
    QString why;
    for (const BinItem& c : clips) {
        if (c.kind != BinItemKind::MasterClip && c.kind != BinItemKind::Subclip && c.kind != BinItemKind::AudioClip) continue;
        if (isSyncedSubclip(c) || c.durationFrames <= 0) continue;
        const bool video = hasTrack(c, TrackKind::Video);
        if (!video && !hasTrack(c, TrackKind::Audio)) continue;
        const auto point = syncPoint(c, video, options, &why);
        if (!point) {
            out.error = why;
            return out;
        }
        (video ? videos : audios) << Placed{&c, -*point, c.durationFrames - *point};
    }
    if (videos.isEmpty() || audios.isEmpty()) {
        out.error = QStringLiteral("Select a video clip and an audio clip: Sync Clips syncs audio clips with video clips only.");
        return out;
    }
    const bool marks = options.sync.sync == GroupSync::Inpoints || options.sync.sync == GroupSync::Outpoints;
    if (marks && videos.size() > 1) {
        out.error = QStringLiteral("Syncing by In or Out points syncs one video clip at a time.");
        return out;
    }
    for (const Placed& a : audios) {
        if (!(a.clip->editRate == videos.first().clip->editRate)) {
            out.error = QStringLiteral("\"%1\" is at another frame rate; synced clips share one.").arg(a.clip->name);
            return out;
        }
    }
    QStringList made;
    for (const Placed& v : videos) {
        QList<Placed> sound;
        for (const Placed& a : audios) {
            if (a.start < v.end && a.end > v.start) sound << a;
        }
        if (sound.isEmpty()) continue;
        std::int64_t first = v.start, last = v.end;
        if (!marks) {
            for (const Placed& a : sound) {
                first = std::max(first, a.start);
                last = std::min(last, a.end);
            }
        }
        const std::int64_t length = last - first;
        if (length <= 0) continue;
        Sequence seq;
        seq.setTimecodeTrack(false);
        for (const QString& t : sourceTracksOf(*v.clip)) {
            if (const auto ref = parseTrackName(t); ref && ref->kind == TrackKind::Video) {
                seq.ensureTrack(t)->segments = {clipSegment(*v.clip, binId, t, first - v.start, length)};
            }
        }
        // The audio tracks, the video clip's kept ones first, then each audio clip's after the last used number; with
        // Collapse Audio Tracks only those with sound, numbered from A1.
        QList<QList<Segment>> tracks; // index = track number - 1; empty = no sound
        auto place = [&](const Placed& p, const std::pair<int, int>& range) {
            const int offset = static_cast<int>(tracks.size());
            for (const int n : audioTracks(*p.clip)) {
                if (n < range.first || n > range.second) continue;
                const int at = offset + n - range.first;
                while (tracks.size() <= at) tracks << QList<Segment>{};
                // Its material within the subclip, filler where it has none.
                const std::int64_t from = std::max(first, p.start), to = std::min(last, p.end);
                QList<Segment> segments;
                if (from > first) segments << Segment::filler(from - first);
                if (to > from) segments << clipSegment(*p.clip, binId, QStringLiteral("A%1").arg(n), from - p.start, to - from);
                tracks[at] = segments;
            }
        };
        const std::pair<int, int> all{1, 999};
        if (options.keepVideoAudio) place(v, *options.keepVideoAudio);
        for (const Placed& a : sound) place(a, options.audioOnlyTracks.value_or(all));
        // Trailing tracks without sound are not kept in any case.
        while (!tracks.isEmpty() && tracks.last().isEmpty()) tracks.removeLast();
        int number = 0;
        for (const QList<Segment>& t : tracks) {
            const bool silent = std::none_of(t.cbegin(), t.cend(), [](const Segment& s) { return !s.isFiller(); });
            if (options.collapseAudio && silent) continue;
            ++number;
            seq.ensureTrack(QStringLiteral("A%1").arg(number))->segments = t;
        }
        BinItem item;
        item.id = newItemId();
        item.kind = BinItemKind::Subclip;
        item.name = syncedName(bin, v.clip->name, made);
        made << item.name;
        item.created = QDateTime::currentDateTimeUtc();
        item.modified = item.created;
        item.editRate = v.clip->editRate;
        item.fields = v.clip->fields;
        for (const char* gone : {"Mark IN", "Mark OUT", "Tracks"}) item.fields.remove(QLatin1String(gone));
        // Its timecode is the video clip's, from the subclip's first frame.
        const TimecodeFormat format = timecodeFormatOf(*v.clip);
        const std::int64_t start = startFrameOf(*v.clip) + (first - v.start);
        item.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(start, format).toString()));
        item.fields.insert(QStringLiteral("End"), QString::fromStdString(Timecode(start + length, format).toString()));
        item.fields.insert(QStringLiteral("Tracks"), seq.trackNames().join(QLatin1Char(' ')));
        item.composition = QJsonObject{{QLatin1String(kSyncedKey), seq.toJson()}};
        item.durationFrames = length;
        // Its sources are the clips it syncs, the video clip first.
        ClipSource from;
        from.kind = ClipSource::Kind::Clip;
        from.mob = mobIdOf(*v.clip);
        from.name = v.clip->name;
        from.offset = first - v.start;
        item.sources << from;
        for (const Placed& a : sound) {
            ClipSource s;
            s.kind = ClipSource::Kind::Clip;
            s.mob = mobIdOf(*a.clip);
            s.name = a.clip->name;
            s.offset = std::max<std::int64_t>(first - a.start, 0);
            item.sources << s;
        }
        out.subclips << item;
    }
    if (out.subclips.isEmpty()) out.error = QStringLiteral("The video and audio clips do not overlap when synced this way.");
    return out;
}

bool isSyncedSubclip(const BinItem& item) {
    return item.kind == BinItemKind::Subclip && item.composition.value(QLatin1String(kSyncedKey)).isObject();
}

std::optional<BinItem> syncedSequence(const BinItem& item) {
    if (!isSyncedSubclip(item)) return std::nullopt;
    BinItem out = item;
    out.kind = BinItemKind::Sequence;
    out.composition = item.composition.value(QLatin1String(kSyncedKey)).toObject();
    return out;
}

std::optional<Sequence> composedSequence(const BinItem& item) {
    if (item.kind == BinItemKind::Sequence || item.kind == BinItemKind::Title) return Sequence::fromItem(item);
    if (const auto synced = syncedSequence(item)) return Sequence::fromItem(*synced);
    return std::nullopt;
}

} // namespace lumenis::model
