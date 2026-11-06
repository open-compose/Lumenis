// The Simplified AAF's sequence (Phase 7.1c3; docs/spec/aaf.md "Simplified AAF"; Editing Guide p.884-885): which
// parts of a sequence's picture are references to master clips or filler and which must be mixed down, and which
// audio tracks contain effects or gain changes. MainWindow::prepareAafMedia renders the mixdowns and builds the
// sequence from these.
#pragma once

#include "lumenis/model/sequence.h"

#include <QList>

#include <cstdint>

namespace lumenis::model {

// One stretch of the simplified V1. "When the video is mixed down, if a segment of the video is an existing master
// clip or filler, a reference to that segment is added to the new sequence. If the segment is a transition or effect,
// a video mixdown occurs which creates a new master clip" (p.884).
struct SimplifiedSpan {
    enum class Kind { Filler, Clip, Mixdown };
    Kind kind = Kind::Filler;
    std::int64_t start = 0; // sequence frames [start, end)
    std::int64_t end = 0;
    Segment segment;        // Clip: the segment the span shows, trimmed to it (sourceIn and length)
};

// The spans of V1, in order, covering the sequence's duration. A span is a Clip when the topmost non-filler video
// segment there has no effect and no transition runs over it (a plain segment on top hides those below it), Filler
// when every video track is filler there, and a Mixdown otherwise. Neighbouring Mixdown spans join; Clip spans of one
// segment do too.
QList<SimplifiedSpan> simplifiedVideo(const Sequence& sequence);

// "If an audio track contains at least one effect or gain change, the entire track is mixed down" (p.884): a segment
// with clip gain, volume automation, an EQ or AudioSuite effect, or an insert on the track.
bool audioTrackHasEffects(const Track& track);

} // namespace lumenis::model
