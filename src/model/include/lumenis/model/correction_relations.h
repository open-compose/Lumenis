#pragma once

// Relationship colour corrections (FX Guide p.596-598; docs/spec/color-correction.md, 6.10d1): "Relationship color
// corrections define the scope of any correction you make. For example, if all the material from one source tape in
// your project needs the same color correction ... you can make that correction to every segment in a sequence that
// you captured from that source tape in one operation" (p.596). The Correction Type menu's relationships (p.597):
//   Src Tape       every segment in the sequence from the same source tape;
//   Src Clip Name  every segment with the same clip name, ignoring ".new" and what follows it;
//   Master Clip    every segment from the same master clip;
//   Sub Clip       every segment from the same subclip;
//   Src Segment    only the current segment;
//   Prog Track     the current segment "and all other material on that segment's track";
//   Prog Segment   only the current segment.
//
// Ours: each segment keeps its own copy of the correction with the relationship it was made with
// (Segment::sourceRelationship/programRelationship). A correction made with a relationship goes to every related video
// segment there is when it is made; segments edited in later take it with Update Correction (6.10d4), as in Media
// Composer, where "Media Composer can apply existing color corrections to the new segments, based on their relationship
// to the existing material" (p.700). The related segments are on any video track for the Source relationships and on
// the segment's own track for Prog Track; filler is never one.

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <functional>
#include <optional>

namespace lumenis::model {

// What a segment is related by. Empty: it has none (a segment from a clip without a tape is related to no other by Src
// Tape; one from a master clip has no subclip).
struct CorrectionKeys {
    QString tape;       // its source tape's name
    QString clipName;   // its clip name without ".new..." (p.597)
    QString masterClip; // its master clip's MobID (a subclip's master's)
    QString subClip;    // its subclip's MobID
    bool operator==(const CorrectionKeys&) const = default;
};
// The clip a segment comes from, for its keys (the Timeline passes the bins' items); nothing when it is not found.
using CorrectionClipLookup = std::function<std::optional<BinItem>(const Segment&)>;

// "This relationship ignores clip name extensions — .new and any subsequent characters — created during decomposing and
// batch capturing" (p.597).
QString baseClipName(const QString& name);
// A segment's keys from its clip (or from the segment alone without one: its name and MobID).
CorrectionKeys correctionKeysOf(const Segment& segment, const std::optional<BinItem>& clip);

// The video segments a correction of `relationship` on the segment at `index` of `track` goes to: that segment first,
// then the others in track and time order. Only that one for the segment relationships, and for a key it has none of.
QList<SegmentRef> relatedSegments(const Sequence& sequence, const QString& track, int index,
                                  CorrectionRelationship relationship, const CorrectionClipLookup& lookup);

// Makes `correction` the correction of every segment related to the one at `index` by `relationship`, on that
// relationship's side, with that relationship. False when nothing changes.
bool applyRelationshipCorrection(Sequence& sequence, const QString& track, int index, CorrectionRelationship relationship,
                                 const ColorCorrection& correction, const CorrectionClipLookup& lookup);

// "To convert a Source relationship correction to another Source relationship correction ... Click the Correction Type
// button, and choose a new type from the menu" (p.598): the segment's correction on `to`'s side, made again with `to`.
// Ours: it goes to every segment related by `to`; the segments it reached by the old relationship that `to` leaves out
// keep their values as Src or Prog Segment corrections. False when the segment has no correction on that side, or it
// already has `to`.
bool convertRelationship(Sequence& sequence, const QString& track, int index, CorrectionRelationship to,
                         const CorrectionClipLookup& lookup);

} // namespace lumenis::model
