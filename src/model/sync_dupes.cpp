#include "lumenis/model/sync_dupes.h"

#include <QHash>
#include <QPair>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>

// Sync breaks and Dupe Detection (sync_dupes.h; Editing Guide p.490-492, p.641-642). Both work on a
// flat list of the sequence's material segments with their record and source frames.
namespace lumenis::model {

namespace {

struct Piece {
    const Track* track = nullptr;
    const Segment* segment = nullptr;
    std::int64_t start = 0; // record frame
    std::int64_t length = 0;
    // The source frame at record frame 0, were the segment to extend that far: segments of one clip
    // whose shift is equal run in sync.
    std::int64_t shift() const { return segment->sourceIn - start; }
};

QList<Piece> piecesOf(const Sequence& sequence, const std::function<bool(const Track&)>& take) {
    QList<Piece> out;
    for (const Track& t : sequence.tracks()) {
        if (!take(t)) continue;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            if (!s.isFiller() && !s.clipId.isEmpty()) out.append(Piece{&t, &s, at, s.length});
            at += s.length;
        }
    }
    return out;
}

bool sameClip(const Segment& a, const Segment& b) {
    return a.clipId == b.clipId && a.binId == b.binId;
}

} // namespace

QList<SyncBreak> syncBreaks(const Sequence& sequence) {
    const QList<Piece> pieces = piecesOf(sequence, [](const Track& t) { return t.kind != TrackKind::Data; });
    // The pieces of each clip, in the pieces' order (tracks in order): partners are only ever of the same clip, and
    // looking through every piece for each made painting a long sequence slow (10.1b).
    QHash<QPair<QString, QString>, QList<const Piece*>> byClip;
    for (const Piece& q : pieces) byClip[qMakePair(q.segment->binId, q.segment->clipId)].append(&q);
    QList<SyncBreak> out;
    for (const Piece& p : pieces) {
        // Walk the segment's record frames in runs where the set of overlapping partners is the same.
        QList<std::int64_t> cuts{p.start, p.start + p.length};
        QList<const Piece*> partners;
        for (const Piece* qp : byClip.value(qMakePair(p.segment->binId, p.segment->clipId))) {
            const Piece& q = *qp;
            if (q.track->kind == p.track->kind || !sameClip(*q.segment, *p.segment)) continue;
            const std::int64_t from = std::max(p.start, q.start);
            const std::int64_t to = std::min(p.start + p.length, q.start + q.length);
            if (from >= to) continue;
            partners.append(&q);
            cuts << from << to;
        }
        if (partners.isEmpty()) continue;
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (qsizetype i = 1; i < cuts.size(); ++i) {
            const std::int64_t from = cuts.at(i - 1);
            const std::int64_t to = cuts.at(i);
            bool inSync = false;
            const Piece* first = nullptr;
            for (const Piece* q : partners) {
                if (q->start > from || q->start + q->length < to) continue;
                inSync = inSync || q->shift() == p.shift();
                // Partners come in track order, V1 before V2, A1 before A2 (Sequence keeps tracks so).
                if (!first) first = q;
            }
            if (!first || inSync) continue;
            const std::int64_t offset = p.shift() - first->shift();
            // Neighbouring runs with the same offset are one break.
            if (!out.isEmpty() && out.constLast().track == p.track->name() &&
                out.constLast().start + out.constLast().length == from && out.constLast().offset == offset) {
                out.last().length += to - from;
            } else {
                out.append(SyncBreak{p.track->name(), from, to - from, offset});
            }
        }
    }
    return out;
}

QList<Dupe> dupes(const Sequence& sequence, const QSet<TrackKind>& kinds, double handles) {
    const QList<Piece> pieces = piecesOf(sequence, [&kinds](const Track& t) { return kinds.contains(t.kind); });
    // In half frames, so that half-frame handles are exact.
    const auto h = static_cast<std::int64_t>(std::llround(std::max(handles, 0.0) * 2));
    struct Pair {
        std::int64_t first = 0; // the set's first record frame
        Dupe a;
        Dupe b;
    };
    QList<Pair> pairs;
    // `p`'s record frames whose source frames fall inside `q`'s widened range.
    auto marked = [h](const Piece& p, const Piece& q) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        const std::int64_t qFrom = 2 * q.segment->sourceIn - h;
        const std::int64_t qTo = 2 * (q.segment->sourceIn + q.length) + h;
        // Source frame f of p (half frames [2f, 2f + 2)) is marked when it meets q's range
        // [qFrom, qTo): from floor(qFrom / 2) up to, not including, ceil(qTo / 2).
        auto floorHalf = [](std::int64_t v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); };
        const std::int64_t fFrom = std::max(p.segment->sourceIn, floorHalf(qFrom));
        const std::int64_t fTo = std::min(p.segment->sourceIn + p.length, -floorHalf(-qTo));
        if (fFrom >= fTo) return std::nullopt;
        return std::pair{p.start + fFrom - p.segment->sourceIn, fTo - fFrom};
    };
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        for (qsizetype j = i + 1; j < pieces.size(); ++j) {
            const Piece& p = pieces.at(i);
            const Piece& q = pieces.at(j);
            if (!sameClip(*p.segment, *q.segment) || p.segment->sourceTrack != q.segment->sourceTrack) continue;
            const auto a = marked(p, q);
            const auto b = marked(q, p);
            if (!a || !b) continue;
            pairs.append(Pair{std::min(a->first, b->first), Dupe{p.track->name(), a->first, a->second, 0},
                              Dupe{q.track->name(), b->first, b->second, 0}});
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& x, const Pair& y) { return x.first < y.first; });
    QList<Dupe> out;
    for (qsizetype n = 0; n < pairs.size(); ++n) {
        Pair& pair = pairs[n];
        pair.a.set = static_cast<int>(n);
        pair.b.set = static_cast<int>(n);
        out << pair.a << pair.b;
    }
    return out;
}

QString dupeColor(int set) {
    static const std::array<const char*, 10> colors{"#f08c28", "#3cb44a", "#2f8fd8", "#d84f9a", "#e6d23c",
                                                    "#8c5ad8", "#3cc8c8", "#d85a3c", "#a0c83c", "#c8a078"};
    return QString::fromLatin1(colors.at(static_cast<std::size_t>(std::max(set, 0)) % colors.size()));
}

} // namespace lumenis::model
