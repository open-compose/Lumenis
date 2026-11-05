#pragma once

// Managing colour-corrected sequences (FX Guide ch.21, p.700-706; docs/spec/color-correction.md, 6.10d4): the Color
// Correction tool's shortcut menu commands Update Correction, Merge Correction, Flatten Correction and Remove Correction,
// each working on the relationships its dialog selects:
//   Update   "Media Composer can apply existing color corrections to the new segments, based on their relationship to
//            the existing material" (p.700): segments without a correction on a side take the correction their
//            relationship keys share with corrected segments.
//   Merge    "merges any color corrections based on Source Tape, Clip Name, Master Clip, Segment, or Sub Clip
//            relationships, or based on Program Track or Segment relationships, into the latest version of the edited
//            sequence" (p.701): the Source monitor's sequence's corrections into the Record monitor's, conflicts settled
//            by the "In case of conflict" menu (p.703).
//   Flatten  "all the color corrections become segment-only relationships" (p.704).
//   Remove   "removes color correction for all segments affected by the relationships you have selected" (p.706).
// They look at every video track's segments; filler has no correction.

#include "lumenis/model/correction_relations.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QMap>

namespace lumenis::model {

// How many segments of the sequence have a correction made with each relationship ("The dialog box indicates the
// number of segments in the sequence for each relationship category", p.705).
QMap<CorrectionRelationship, int> correctionCounts(const Sequence& sequence);

// "In case of conflict" (p.703): which sequence's correction wins when both have one for the same relationship key (or
// overlapping segments, for the segment relationships). Prefer Latest, "the most recent information", is the default;
// ours: the sequence modified last (`sourceIsLater` below).
enum class MergeConflict { PreferSource, PreferRecord, PreferLatest };

// Update Correction (p.700-701): for each selected relationship other than the segment ones, segments with no
// correction on its side take the correction (and the relationship) of a segment related to them by it; a segment takes
// the first of Tape, Clip Name, Master Clip and Sub Clip that has one (Program: Track). The number of segments changed.
int updateCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships, const CorrectionClipLookup& lookup);

// Merge Correction (p.701-703) of `source` (the Source monitor's sequence) into `record`. For each selected relationship
// other than the segment ones, the corrections each sequence has per key are put together (a key in both with different
// corrections is a conflict); every record segment with that relationship's correction on that side takes the key's
// correction, and every one without a correction takes it with the relationship, as Update does. With a segment
// relationship selected, "Color corrections using Source or Program segment relationships will be merged using the
// Segment's source and timecode ... If more than one segment in the source monitor overlaps the source and timecode of a
// segment in the record monitor, then the segment with the most overlap ... will be used"; it is tried first for record
// segments without a correction (p.702-703). Ours: overlap is the same source clip and track with source frames in
// common; Program Segment corrections also need the same record track. "Segments that already have a relationship color
// correction on them will not have their type of relationship changed". `lookup` must find the clips of both
// sequences. The number of record segments changed.
int mergeCorrections(Sequence& record, const Sequence& source, const QList<CorrectionRelationship>& relationships, MergeConflict conflict,
                     bool sourceIsLater, const CorrectionClipLookup& lookup);

// Flatten Correction (p.704-705): segments corrected with a selected relationship keep their corrections as segment-only
// ones (Src or Prog Segment). The number changed.
int flattenCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships);

// Remove Correction (p.705-706): segments corrected with a selected relationship lose that side's correction. The number
// changed.
int removeCorrections(Sequence& sequence, const QList<CorrectionRelationship>& relationships);

// The relationships of each side in the dialogs' order (p.701, p.706): Source Tape, Clip Name, Master Clip, Sub Clip,
// Segment; Program Track, Segment.
QList<CorrectionRelationship> sourceRelationships();
QList<CorrectionRelationship> programRelationships();

} // namespace lumenis::model
