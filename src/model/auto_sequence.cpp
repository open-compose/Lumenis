#include "lumenis/model/auto_sequence.h"

#include "lumenis/model/multichannel.h"

#include <QMap>

#include <algorithm>
#include <limits>

// AutoSequence (auto_sequence.h). Each clip is placed whole at its Start timecode less the earliest
// one's; Camera groups get their own video and audio tracks after the groups before them (ours: the
// guide says only that matching values go "on the same track", p.499).

namespace lumenis::model {

namespace {

struct Placed {
    const BinItem* item = nullptr;
    std::int64_t start = 0;
    std::int64_t end = 0; // exclusive
};

bool usable(const BinItem& item) {
    return item.kind == BinItemKind::MasterClip || item.kind == BinItemKind::Subclip || item.kind == BinItemKind::AudioClip;
}

// The master clip a subclip refers to, if any.
std::optional<MobId> masterOf(const BinItem& item) {
    for (const ClipSource& s : item.sources) {
        if (s.kind == ClipSource::Kind::Clip) return s.mob;
    }
    return std::nullopt;
}

// One of two related clips, or nothing when they are unrelated.
const Placed* related(const Placed& a, const Placed& b) {
    const auto ma = masterOf(*a.item);
    const auto mb = masterOf(*b.item);
    if (ma && *ma == b.item->mobId) return &b;
    if (mb && *mb == a.item->mobId) return &a;
    if (ma && mb && *ma == *mb) return (a.end - a.start) >= (b.end - b.start) ? &a : &b;
    return nullptr;
}

} // namespace

AutoSequenceResult autoSequence(const QList<BinItem>& clips, const QString& binId, bool noFiller) {
    AutoSequenceResult out;
    QList<Placed> placed;
    for (const BinItem& c : clips) {
        if (!usable(c) || c.durationFrames <= 0) continue;
        const std::int64_t start = timecodeFrames(c.fields.value(QStringLiteral("Start")), c.editRate).value_or(0);
        placed.append({&c, start, start + c.durationFrames});
    }
    if (placed.isEmpty()) {
        out.error = QStringLiteral("Select master clips, subclips or audio clips to AutoSequence.");
        return out;
    }
    // Camera groups, in the order of their values; clips without one form the first.
    QMap<QString, QList<Placed>> groups;
    for (const Placed& p : placed) groups[p.item->fields.value(QStringLiteral("Camera")).trimmed()] << p;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QList<Placed>& g = it.value();
        std::stable_sort(g.begin(), g.end(), [](const Placed& a, const Placed& b) { return a.start < b.start; });
        // Overlaps: related clips keep one; unrelated ones are refused.
        for (int i = 0; i + 1 < g.size();) {
            const Placed& a = g.at(i);
            const Placed& b = g.at(i + 1);
            if (b.start >= a.end) {
                ++i;
                continue;
            }
            const Placed* keep = related(a, b);
            if (!keep) {
                out.error = QStringLiteral("%1 and %2 overlap: AutoSequence cannot place unrelated clips with overlapping timecodes.")
                                .arg(a.item->name, b.item->name);
                return out;
            }
            const int drop = keep == &a ? i + 1 : i;
            out.dropped << g.at(drop).item->name;
            g.removeAt(drop);
        }
    }
    out.start = std::numeric_limits<std::int64_t>::max();
    for (const auto& g : groups) {
        for (const Placed& p : g) out.start = std::min(out.start, p.start);
    }
    const BinItem& first = *groups.first().first().item;
    out.name = first.fields.value(QStringLiteral("Tape"));
    if (out.name.isEmpty()) out.name = first.fields.value(QStringLiteral("Source File"));
    if (out.name.isEmpty()) out.name = first.name;
    Sequence seq;
    int videoOffset = 0;
    int audioOffset = 0;
    for (const auto& g : groups) {
        int videoCount = 0;
        int audioCount = 0;
        std::int64_t next = 0; // without filler: where the next clip goes
        for (const Placed& p : g) {
            const std::int64_t at = noFiller ? next : p.start - out.start;
            next = at + (p.end - p.start);
            for (const QString& track : sourceTracksOf(*p.item)) {
                const auto ref = parseTrackName(track);
                if (!ref || ref->kind == TrackKind::Data) continue;
                const bool video = ref->kind == TrackKind::Video;
                const int number = ref->number + (video ? videoOffset : audioOffset);
                (video ? videoCount : audioCount) = std::max(video ? videoCount : audioCount, ref->number);
                const QString to = trackName(ref->kind, number);
                if (!seq.ensureTrack(to, sourceFormat(*p.item, track))) continue;
                Segment s;
                s.binId = binId;
                s.clipId = p.item->id;
                s.sourceMob = p.item->mobId;
                s.name = p.item->name;
                s.sourceTrack = sourceChannel(*p.item, track);
                s.sourceIn = 0;
                s.length = p.end - p.start;
                seq.overwrite(to, at, {s});
            }
        }
        videoOffset += videoCount;
        audioOffset += audioCount;
    }
    out.sequence = seq;
    return out;
}

} // namespace lumenis::model
