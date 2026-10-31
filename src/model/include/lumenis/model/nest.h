#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <utility>

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

// ---- Nested effects in the Effect Editor (p.221-222; ROADMAP 6.6b2)
// One effect of a segment's nested effects: the effect, where its segment starts relative to the outer
// segment's first frame (in the outer segment's frames; negative when it starts before), and its length.
struct NestedEffect {
    Effect effect;
    std::int64_t start = 0;
    std::int64_t length = 0;
    bool operator==(const NestedEffect&) const = default;
};
// "Nested effects on a segment are fully available in the Effect Editor ... if you have a Picture-in-Picture
// effect with a Color Correction effect nested inside it, you can see both effects" (p.221): the segment's
// effect, then the effect of the segment on its nest's V1 at the same frame (`offset` frames into the outer
// segment), and so on in, while a nest holds that one video track; a nest of several tracks (a Submaster's,
// a Matte Key's) ends the list, as does a nested segment without an effect. Empty without an effect.
QList<NestedEffect> nestedEffects(const Segment& segment, std::int64_t offset);
// Sets the effect at `level` of nestedEffects(segment, offset) (0: the segment's own). False when there is
// no such level. Renders on the way in are dropped.
bool setNestedEffect(Segment& segment, std::int64_t offset, int level, const Effect& effect);
// "Drag the effect icon for the effect you want to move to the position you want it to occupy in the nest"
// (p.222): the effect at level `from` moves to level `to`, the others keeping their order. "If you attempt a
// change in order that is not possible, a message box opens that explains why" (p.222): refused, with
// `error` saying why, when a Timewarp would not stay innermost ("it must be the innermost effect", p.177) or
// the innermost effect works on a nest of several tracks ("you cannot change the order of nested effects if
// the result would not provide enough video inputs for the effect", p.222).
bool moveNestedEffect(Segment& segment, std::int64_t offset, int from, int to, QString* error = nullptr);

// ---- Expanding nested effects in the Timeline (p.220-221; ROADMAP 6.6b3)
// "You can expand an existing nested effect for editing within the Timeline ... The nested tracks appear in
// the Timeline above the track you double-clicked. Editing and patching features are available for changing
// the content of the track's effect" (p.220). The Timeline shows and edits an expanded sequence: the
// sequence's tracks, those video tracks above the expanded segment's moved up to make room, and the nest's
// video tracks between them, named "<track number>.<nest track>" (Track::customName; "2.1", "2.2" over V2 as
// in the pictures on p.221), holding the nest's material at the segment's frames. NestExpansion says how to
// take it apart again.
struct NestExpansion {
    QString track;              // the expanded segment's track ("V2")
    std::int64_t start = 0;     // its first frame and length
    std::int64_t length = 0;
    std::int64_t nestIn = 0;    // the nest frame of its first frame
    int nested = 0;             // the nest's video tracks shown
    QJsonObject nest;           // the nest as shown, which finds the segment again after an edit
    QStringList named;          // moved tracks given their old name as customName (they had none)
    bool operator==(const NestExpansion&) const = default;
    // The expanded sequence's name for nest track `k` (1 is the nest's V1), and whether `name` is one.
    QString nestedTrack(int k) const;
    bool isNested(const QString& name) const;
};
// The sequence with the nest of the segment under `frame` on `track` expanded, and how; nothing when that
// segment has no nest or the tracks would pass kMaxTracks.
std::optional<std::pair<Sequence, NestExpansion>> expandNest(const Sequence& sequence, const QString& track, std::int64_t frame);
// The sequence again from an expanded one, perhaps edited: the nested tracks' material over the segment's
// frames goes into its nest, the moved tracks go back. `expansion` follows the segment if an edit moved or
// trimmed it, and becomes empty when it is gone (the nested tracks' changes are then dropped).
Sequence collapseExpansion(const Sequence& expanded, std::optional<NestExpansion>& expansion);

// "N2 indicates that you are two levels in from the outermost track level" (p.220): the label for a nest
// level, empty at the top.
QString nestLevelLabel(int level);

} // namespace lumenis::model
