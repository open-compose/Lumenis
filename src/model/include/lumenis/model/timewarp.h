#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

// The Timewarp effect (FX Guide p.174-196, p.484-488; ROADMAP 6.5b; docs/spec/motion-effects.md): a
// segment effect that maps the segment's output frames to frames of its source through either of two
// control graphs. "The Speed control ... The Motion Effect Editor displays the speed of the source
// (input speed) at output time in the Speed graph ... Position maps input (source) time to output
// (effect) time" (p.175). Both are keyframed effect parameters (effect_keyframes.h): "Speed/Percent
// Speed" in percent and "Position/Input Position" in source frames from the segment's source IN. The
// rest is kept under the "Timewarp" parameter category: the active graph, the Source Anchor Frame and
// where it is anchored, Strobe and the render Type. The segment's frames come from
// model::segmentSourcePosition, which the compositor plays.
namespace lumenis::model {

// The Type menu's rendering options (p.185-186). FluidMotion is 6.5d's; until then it blends as
// Blended Interpolated does.
enum class TimewarpType { DuplicatedField, BothFields, InterpolatedField, VtrStyle, BlendedInterpolated, BlendedVtr, FluidMotionDraft, FluidMotion };
QString timewarpTypeName(TimewarpType t); // "duplicated", "both", ..., "fluidMotion"
TimewarpType timewarpTypeFromName(const QString& name);

inline constexpr char kTimewarpCategory[] = "Timewarp";
inline constexpr char kSpeedCategory[] = "Speed";
inline constexpr char kSpeedParameter[] = "Percent Speed";
inline constexpr char kPositionCategory[] = "Position";
inline constexpr char kPositionParameter[] = "Input Position";

struct Timewarp {
    // "You can work with either the Speed graph or the Position graph but not both at once ... When you
    // render a Timewarp effect, Media Composer applies only the information from the active graph"
    // (p.175).
    bool positionGraph = false;
    // The Source Anchor Frame (p.181): the source frame, from the segment's source IN, that the Speed
    // graph keeps at output time `anchorAt` (the effect's time, 0 its first frame and 1 its last). "The
    // default Source Anchor Frame is the first frame or field of the source, and the default point for
    // anchoring ... timecode 0".
    double anchor = 0.0;
    double anchorAt = 0.0;
    // "Strobe n frames": the frames the image is held before it updates (p.184); 1 for none.
    int strobe = 1;
    TimewarpType type = TimewarpType::DuplicatedField;
    bool operator==(const Timewarp&) const = default;
};

// Whether an effect is a Timewarp (any of the Timewarp category's effects, which are its templates).
bool isTimewarp(const Effect& effect);
// The settings kept with a Timewarp effect; the defaults for one that has none.
Timewarp timewarpOf(const Effect& effect);
void setTimewarp(Effect& effect, const Timewarp& timewarp);

// A Timewarp effect as applied to a segment `length` frames long: the template's graphs (p.203-204,
// p.484-488; ours where the guide shows them only in pictures): 0% To 100% and 100% to 0% ramp the
// speed ("The last frame of the clip is the last frame of the motion effect"); Reverse Motion maps the
// segment's last source frame to its first output frame and back; Speed Boost (50%, about 200% in the
// middle, 50%) and Speed Bump (100%, about 50%, 100%) on a Spline; Trim to Fill a Position graph from
// the first to the last source frame, which trimming stretches ("You can adjust the speed by entering
// Trim mode and trimming the effect"). The FluidFilm templates render with FluidMotion. Other effects
// come back as they are.
Effect timewarpForSegment(const Effect& effect, std::int64_t length);

// The source position (frames from the segment's source IN, with a fraction) that output frame
// `offset` of a `length`-frame Timewarp shows: the Position graph's value there, or the Speed graph's
// speeds added up from the anchor ("If Percent Speed is 200 at an Output Position of 00:00:02:15,
// then at 2:15 after the start of the effect, the speed of the effect is twice the speed (200%) of the
// source", p.187-188). Strobe holds the output frame at each multiple of the strobe.
double timewarpPosition(const Effect& effect, std::int64_t offset, std::int64_t length);
// Whether the Type makes in-between frames by blending the two source frames around the position
// (Blended Interpolated, Blended VTR and, until 6.5d, FluidMotion: "Blends, or averages, pixels from the
// original frames", p.186). The others show whole source frames.
bool blendsFrames(const Effect& effect);

// Promote to Timewarp (p.177): a Timewarp effect from a traditional motion effect's speed ("The Motion
// Effect Editor uses the duration and speed information from the traditional motion effect to create a
// Timewarp effect"; "Strobe motion information does not carry over").
Effect promotedTimewarp(double speed);
// Promotes the segment's motion effect to a Timewarp: its own Motion Effect (motionSpeed), or, for a
// motion effect clip (resolved through `resolve`), the clip's source with the source frames the
// segment showed. False when the segment has no traditional motion effect.
bool promoteToTimewarp(Segment& segment, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve);

} // namespace lumenis::model
