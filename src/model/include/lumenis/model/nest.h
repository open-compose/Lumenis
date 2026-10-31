#pragma once

#include "lumenis/model/sequence.h"

#include <QString>
#include <QStringList>

#include <cstdint>

// Nested effects (FX Guide p.219-227; ROADMAP 6.6a; docs/spec/nesting.md): "You can place effect tracks
// inside one another ... step into a track, perform a series of editing operations, and then step out to view
// and render the effect as one segment on the track" (p.219). A segment's nest (Segment::nest) is a sequence
// of its own whose composite is the picture the segment's effect works on; the compositor plays it in place of
// the segment's clip.
namespace lumenis::model {

// The segment's nested tracks; an empty sequence when it has none.
Sequence nestOf(const Segment& segment);
void setNest(Segment& segment, const Sequence& nest);

// Step In (p.220): "You can step into any segment effect in this way". A segment effect without a nest gets
// one: its own picture (the clip, or nothing for filler) on the nest's V1, under the effect. False for a
// segment without an effect, which has nothing to step into.
bool ensureNest(Segment& segment);

// Nesting an effect inside another (Alt+drag or Alt+double-click, FX Guide p.216, p.276): `outer` becomes the
// segment's effect and the effect it had goes inside, on the nest's V1 with the segment's picture. A
// segment without an effect simply takes `outer`. Renders of the old effect go with it.
void nestEffect(Segment& segment, const Effect& outer);

// "The Submaster effect is a single-track segment effect, available in the Image category" (p.224).
inline constexpr char kSubmasterCategory[] = "Image";
inline constexpr char kSubmasterName[] = "Submaster";
bool isSubmaster(const Effect& effect);

// Collapse (p.226-227): the material of `tracks` (adjacent video tracks, any order) between `start` and
// `end` (exclusive) goes into a Submaster effect's nest, each track on the nest track of its place (the
// lowest on V1), and the Submaster takes its place on the lowest track; the others become filler there.
// Locked tracks refuse it. `error` says why it could not be done.
bool collapseTracks(Sequence& sequence, const QStringList& tracks, std::int64_t start, std::int64_t end, QString* error = nullptr);

// "N2 indicates that you are two levels in from the outermost track level" (p.220): the label for a nest
// level, empty at the top.
QString nestLevelLabel(int level);

} // namespace lumenis::model
