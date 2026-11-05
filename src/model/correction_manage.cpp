// Update, Merge, Flatten and Remove Correction (FX Guide ch.21, p.700-706). See lumenis/model/correction_manage.h.

#include "lumenis/model/correction_manage.h"

#include <algorithm>

namespace lumenis::model {

namespace {

// The key a non-segment relationship relates segments by: the clip's tape, name, master clip or subclip, or the
// segment's track for Prog Track. Empty: none (the segment is related to no other by it).
QString keyOf(const Segment& s, const QString& track, CorrectionRelationship r, const CorrectionClipLookup& lookup) {
    if (r == CorrectionRelationship::ProgramTrack) return track;
    const CorrectionKeys k = correctionKeysOf(s, lookup ? lookup(s) : std::nullopt);
    switch (r) {
    case CorrectionRelationship::SourceTape: return k.tape;
    case CorrectionRelationship::SourceClipName: return k.clipName;
    case CorrectionRelationship::MasterClip: return k.masterClip;
    case CorrectionRelationship::SubClip: return k.subClip;
    default: return {};
    }
}

// Calls `f(segment, track name)` for every video segment that is not filler.
template <typename F>
void forEachSegment(const Sequence& sequence, F&& f) {
    for (const Track& t : sequence.tracks()) {
        if (t.kind != TrackKind::Video) continue;
        for (const Segment& s : t.segments) {
            if (!s.isFiller()) f(s, t.name());
        }
    }
}
template <typename F>
void forEachSegment(Sequence& sequence, F&& f) {
    QStringList names;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Video) names << t.name();
    }
    for (const QString& name : names) {
        Track* t = sequence.track(name);
        for (Segment& s : t->segments) {
            if (!s.isFiller()) f(s, name);
        }
    }
}

// Each key's correction of relationship `r` in the sequence (the first segment's with it).
QMap<QString, ColorCorrection> correctionsByKey(const Sequence& sequence, CorrectionRelationship r, const CorrectionClipLookup& lookup) {
    QMap<QString, ColorCorrection> out;
    const CorrectionSide side = sideOf(r);
    forEachSegment(sequence, [&](const Segment& s, const QString& track) {
        Segment copy = s;
        const ColorCorrection& c = correctionOf(copy, side);
        if (c.isEmpty() || relationshipOf(s, side) != r) return;
        const QString key = keyOf(s, track, r, lookup);
        if (!key.isEmpty() && !out.contains(key)) out.insert(key, c);
    });
    return out;
}

CorrectionRelationship& relationshipField(Segment& s, CorrectionSide side) {
    return side == CorrectionSide::Source ? s.sourceRelationship : s.programRelationship;
}

// Puts each key's correction in `corrections` on the segments of `sequence`: on those already corrected with `r` and,
// for those with no correction on the side, with `r`. `taken` keeps the segments given one in this pass (by address),
// so that a later relationship does not replace it. The number changed.
int spread(Sequence& sequence, CorrectionRelationship r, const QMap<QString, ColorCorrection>& corrections, const CorrectionClipLookup& lookup,
           QList<const Segment*>& taken) {
    int changed = 0;
    const CorrectionSide side = sideOf(r);
    forEachSegment(sequence, [&](Segment& s, const QString& track) {
        if (taken.contains(&s)) return;
        ColorCorrection& c = correctionOf(s, side);
        const bool bare = c.isEmpty();
        if (!bare && relationshipOf(s, side) != r) return;
        const QString key = keyOf(s, track, r, lookup);
        const auto it = corrections.constFind(key);
        if (key.isEmpty() || it == corrections.constEnd()) return;
        if (bare || c != *it) {
            c = *it;
            relationshipField(s, side) = r;
            ++changed;
            taken << &s;
        }
    });
    return changed;
}

bool sameSource(const Segment& a, const Segment& b) {
    return a.sourceMobId() == b.sourceMobId() && a.sourceTrack == b.sourceTrack;
}

std::int64_t overlap(const Segment& a, const Segment& b) {
    if (!sameSource(a, b)) return 0;
    return std::max<std::int64_t>(0, std::min(a.sourceIn + a.length, b.sourceIn + b.length) - std::max(a.sourceIn, b.sourceIn));
}

} // namespace

QList<CorrectionRelationship> sourceRelationships() {
    return {CorrectionRelationship::SourceTape, CorrectionRelationship::SourceClipName, CorrectionRelationship::MasterClip,
            CorrectionRelationship::SubClip, CorrectionRelationship::SourceSegment};
}

QList<CorrectionRelationship> programRelationships() {
    return {CorrectionRelationship::ProgramTrack, CorrectionRelationship::ProgramSegment};
}

QMap<CorrectionRelationship, int> correctionCounts(const Sequence& sequence) {
    QMap<CorrectionRelationship, int> out;
    for (CorrectionRelationship r : sourceRelationships() + programRelationships()) out.insert(r, 0);
    forEachSegment(sequence, [&](const Segment& s, const QString&) {
        for (CorrectionSide side : {CorrectionSide::Source, CorrectionSide::Program}) {
            Segment copy = s;
            if (!correctionOf(copy, side).isEmpty()) ++out[relationshipOf(s, side)];
        }
    });
    return out;
}

int updateCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships, const CorrectionClipLookup& lookup) {
    int changed = 0;
    QList<const Segment*> taken;
    for (CorrectionRelationship r : sourceRelationships() + programRelationships()) {
        if (isSegmentOnly(r) || !relationships.contains(r)) continue;
        // Only the segments without a correction take one: the corrected ones of a key already share it.
        const QMap<QString, ColorCorrection> byKey = correctionsByKey(sequence, r, lookup);
        const CorrectionSide side = sideOf(r);
        forEachSegment(sequence, [&](Segment& s, const QString& track) {
            if (taken.contains(&s) || !correctionOf(s, side).isEmpty()) return;
            const QString key = keyOf(s, track, r, lookup);
            const auto it = byKey.constFind(key);
            if (key.isEmpty() || it == byKey.constEnd()) return;
            correctionOf(s, side) = *it;
            relationshipField(s, side) = r;
            taken << &s;
            ++changed;
        });
    }
    return changed;
}

int mergeCorrections(Sequence& record, const Sequence& source, const QList<CorrectionRelationship>& relationships, MergeConflict conflict,
                     bool sourceIsLater, const CorrectionClipLookup& lookup) {
    const bool sourceWins = conflict == MergeConflict::PreferSource || (conflict == MergeConflict::PreferLatest && sourceIsLater);
    int changed = 0;
    QList<const Segment*> taken;
    // The segment relationships first: by the source and timecode the segments share (p.702-703).
    for (CorrectionRelationship r : {CorrectionRelationship::SourceSegment, CorrectionRelationship::ProgramSegment}) {
        if (!relationships.contains(r)) continue;
        const CorrectionSide side = sideOf(r);
        forEachSegment(record, [&](Segment& s, const QString& track) {
            ColorCorrection& c = correctionOf(s, side);
            if (!c.isEmpty() && relationshipOf(s, side) != r) return; // a relationship correction keeps its type
            const Segment* best = nullptr;
            std::int64_t most = 0;
            for (const Track& t : source.tracks()) {
                if (t.kind != TrackKind::Video || (r == CorrectionRelationship::ProgramSegment && t.name() != track)) continue;
                for (const Segment& o : t.segments) {
                    Segment copy = o;
                    if (o.isFiller() || correctionOf(copy, side).isEmpty() || relationshipOf(o, side) != r) continue;
                    const std::int64_t n = overlap(s, o);
                    if (n > most) {
                        most = n;
                        best = &o;
                    }
                }
            }
            if (!best) return;
            Segment copy = *best;
            const ColorCorrection& theirs = correctionOf(copy, side);
            if (!c.isEmpty() && (!sourceWins || c == theirs)) return;
            c = theirs;
            relationshipField(s, side) = r;
            taken << &s;
            ++changed;
        });
    }
    for (CorrectionRelationship r : sourceRelationships() + programRelationships()) {
        if (isSegmentOnly(r) || !relationships.contains(r)) continue;
        QMap<QString, ColorCorrection> merged = correctionsByKey(record, r, lookup);
        const QMap<QString, ColorCorrection> theirs = correctionsByKey(source, r, lookup);
        for (auto it = theirs.constBegin(); it != theirs.constEnd(); ++it) {
            if (!merged.contains(it.key()) || sourceWins) merged.insert(it.key(), it.value());
        }
        changed += spread(record, r, merged, lookup, taken);
    }
    return changed;
}

int flattenCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships) {
    int changed = 0;
    forEachSegment(sequence, [&](Segment& s, const QString&) {
        for (CorrectionSide side : {CorrectionSide::Source, CorrectionSide::Program}) {
            const CorrectionRelationship r = relationshipOf(s, side);
            if (correctionOf(s, side).isEmpty() || isSegmentOnly(r) || !relationships.contains(r)) continue;
            relationshipField(s, side) = segmentRelationship(side);
            ++changed;
        }
    });
    return changed;
}

int removeCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships) {
    int changed = 0;
    forEachSegment(sequence, [&](Segment& s, const QString&) {
        for (CorrectionSide side : {CorrectionSide::Source, CorrectionSide::Program}) {
            if (correctionOf(s, side).isEmpty() || !relationships.contains(relationshipOf(s, side))) continue;
            correctionOf(s, side) = {};
            relationshipField(s, side) = segmentRelationship(side);
            ++changed;
        }
    });
    return changed;
}

} // namespace lumenis::model
