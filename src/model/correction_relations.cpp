// Relationship colour corrections (FX Guide p.596-598): which segments a correction made in Color Correction mode goes
// to. See lumenis/model/correction_relations.h.

#include "lumenis/model/correction_relations.h"

namespace lumenis::model {

QString baseClipName(const QString& name) {
    const qsizetype at = name.indexOf(QLatin1String(".new"));
    return at < 0 ? name : name.left(at);
}

CorrectionKeys correctionKeysOf(const Segment& segment, const std::optional<BinItem>& clip) {
    CorrectionKeys keys;
    keys.clipName = baseClipName(clip ? clip->name : segment.name);
    if (!clip) {
        keys.masterClip = QString::fromStdString(segment.sourceMobId().toString());
        return keys;
    }
    const QString own = QString::fromStdString(clip->mobId.isNull() ? segment.sourceMobId().toString() : clip->mobId.toString());
    for (const ClipSource& s : clip->sources) {
        if (s.kind == ClipSource::Kind::Tape && keys.tape.isEmpty()) keys.tape = s.name;
    }
    // A subclip's tape comes with its master's headings (subclipItem copies them).
    if (keys.tape.isEmpty()) keys.tape = clip->fields.value(QStringLiteral("Tape"));
    if (clip->kind == BinItemKind::Subclip) {
        keys.subClip = own;
        for (const ClipSource& s : clip->sources) {
            if (s.kind == ClipSource::Kind::Clip && keys.masterClip.isEmpty()) keys.masterClip = QString::fromStdString(s.mob.toString());
        }
    } else {
        keys.masterClip = own;
    }
    return keys;
}

namespace {

// The key `relationship` relates segments by; empty for the segment and track relationships.
QString keyFor(const CorrectionKeys& k, CorrectionRelationship relationship) {
    switch (relationship) {
    case CorrectionRelationship::SourceTape: return k.tape;
    case CorrectionRelationship::SourceClipName: return k.clipName;
    case CorrectionRelationship::MasterClip: return k.masterClip;
    case CorrectionRelationship::SubClip: return k.subClip;
    default: return {};
    }
}

std::int64_t startOf(const Track& t, int index) {
    std::int64_t at = 0;
    for (int i = 0; i < index; ++i) at += t.segments.at(i).length;
    return at;
}

Segment* segmentAt(Sequence& sequence, const SegmentRef& ref) {
    Track* t = sequence.track(ref.track);
    const int i = t ? t->segmentAt(ref.start) : -1;
    return i < 0 ? nullptr : &t->segments[i];
}

} // namespace

QList<SegmentRef> relatedSegments(const Sequence& sequence, const QString& track, int index,
                                  CorrectionRelationship relationship, const CorrectionClipLookup& lookup) {
    const Track* t = sequence.track(track);
    if (!t || index < 0 || index >= t->segments.size() || t->segments.at(index).isFiller()) return {};
    QList<SegmentRef> out{{track, startOf(*t, index)}};
    if (isSegmentOnly(relationship)) return out;
    const auto keysOf = [&](const Segment& s) { return correctionKeysOf(s, lookup ? lookup(s) : std::nullopt); };
    const QString key = relationship == CorrectionRelationship::ProgramTrack ? QString() : keyFor(keysOf(t->segments.at(index)), relationship);
    if (relationship != CorrectionRelationship::ProgramTrack && key.isEmpty()) return out;
    for (const Track& other : sequence.tracks()) {
        if (other.kind != TrackKind::Video) continue;
        if (relationship == CorrectionRelationship::ProgramTrack && other.name() != track) continue;
        std::int64_t at = 0;
        for (int i = 0; i < other.segments.size(); ++i) {
            const Segment& s = other.segments.at(i);
            const bool self = other.name() == track && i == index;
            if (!self && !s.isFiller() && (relationship == CorrectionRelationship::ProgramTrack || keyFor(keysOf(s), relationship) == key))
                out << SegmentRef{other.name(), at};
            at += s.length;
        }
    }
    return out;
}

bool applyRelationshipCorrection(Sequence& sequence, const QString& track, int index, CorrectionRelationship relationship,
                                 const ColorCorrection& correction, const CorrectionClipLookup& lookup) {
    const CorrectionSide side = sideOf(relationship);
    bool changed = false;
    for (const SegmentRef& ref : relatedSegments(sequence, track, index, relationship, lookup)) {
        Segment* s = segmentAt(sequence, ref);
        if (!s) continue;
        ColorCorrection& c = correctionOf(*s, side);
        CorrectionRelationship& r = side == CorrectionSide::Source ? s->sourceRelationship : s->programRelationship;
        // A removed correction leaves the side's segment relationship (what an uncorrected segment has).
        const CorrectionRelationship kept = correction.isEmpty() ? segmentRelationship(side) : relationship;
        if (c == correction && r == kept) continue;
        c = correction;
        r = kept;
        changed = true;
    }
    return changed;
}

bool convertRelationship(Sequence& sequence, const QString& track, int index, CorrectionRelationship to,
                         const CorrectionClipLookup& lookup) {
    Track* t = sequence.track(track);
    if (!t || index < 0 || index >= t->segments.size()) return false;
    const CorrectionSide side = sideOf(to);
    const Segment current = t->segments.at(index);
    const ColorCorrection correction = side == CorrectionSide::Source ? current.sourceCorrection : current.programCorrection;
    const CorrectionRelationship from = relationshipOf(current, side);
    if (correction.isEmpty() || from == to) return false;
    const QList<SegmentRef> before = relatedSegments(sequence, track, index, from, lookup);
    const QList<SegmentRef> after = relatedSegments(sequence, track, index, to, lookup);
    for (const SegmentRef& ref : before) {
        Segment* s = after.contains(ref) ? nullptr : segmentAt(sequence, ref);
        // Only those that still have the same correction by the old relationship were reached by it.
        if (s && relationshipOf(*s, side) == from && correctionOf(*s, side) == correction)
            (side == CorrectionSide::Source ? s->sourceRelationship : s->programRelationship) = segmentRelationship(side);
    }
    applyRelationshipCorrection(sequence, track, index, to, correction, lookup);
    return true;
}

} // namespace lumenis::model
