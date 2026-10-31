#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QList>
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

// The Type menu's rendering options (p.185-186). FluidMotion and FluidMotion Draft make their in-between
// frames along the motion between the source frames (media::interpolateFrames, 6.5d1).
enum class TimewarpType { DuplicatedField, BothFields, InterpolatedField, VtrStyle, BlendedInterpolated, BlendedVtr, FluidMotionDraft, FluidMotion };
QString timewarpTypeName(TimewarpType t); // "duplicated", "both", ..., "fluidMotion"
TimewarpType timewarpTypeFromName(const QString& name);

// The Source and Output menus (p.183, p.205): "Your selection should match the format of the original source
// material"; the Output "lets you create a film look from interlaced video sources". Film is "Film with
// pulldown" (2:3), Repeated "Progressive Repeated" (repeated progressive frames, a source only). The pictures
// are made by media::convertedFrame (lumenis/media/fields.h).
enum class TimewarpFormat { Interlaced, Progressive, Film, Repeated };
QString timewarpFormatName(TimewarpFormat f); // "interlaced", "progressive", "film", "repeated"
std::optional<TimewarpFormat> timewarpFormatFromName(const QString& name);
// The menus' text: "Interlaced", "Progressive", "Film with pulldown", "Progressive Repeated".
QString timewarpFormatText(TimewarpFormat f);

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
    // Ignore Render Setting (p.183): keep `type` when the Render setting's Timewarps Render Using names
    // another; without it a render changes the type to the setting's (p.173; model::renderTypeFor).
    bool ignoreRenderSetting = false;
    // The Source and Output formats; nothing for the project's own (Interlaced in an interlaced project,
    // else Progressive).
    std::optional<TimewarpFormat> source;
    std::optional<TimewarpFormat> output;
    // A Film source's 2:3 phase (0-4: the cycle frame AA, BB, BC, CD or DD of its first frame) and a Repeated
    // source's frames per picture, "manually or automatically" set (p.205): `autoPattern` has them found in
    // the clip (media::detectPulldownPhase, detectRepeatCount) when the Source is chosen.
    int pulldownPhase = 0;
    int repeatCount = 2;
    bool autoPattern = true;
    // Adaptive Deinterlace Source (p.181), used only where adaptiveAvailable() allows it.
    bool adaptiveDeinterlace = false;
    bool operator==(const Timewarp&) const = default;
};

// The formats in effect: the Timewarp's, or the project's.
TimewarpFormat sourceFormat(const Timewarp& t, bool interlacedProject);
TimewarpFormat outputFormat(const Timewarp& t, bool interlacedProject);
// The Source menu's choices (p.205 table) for a project of `nominalFps` (TimecodeFormat's) and scan. "Progressive
// projects do not allow format conversions": there only the Output is fixed, at Progressive, so that "an
// interlaced clip within a progressive project" can have "the Source menu set to Interlaced and the Output menu
// to Progressive" (p.183). Film with pulldown only in 30 fps projects. Empty in Film projects: "The Source and
// Output menus are not available in Film projects".
QList<TimewarpFormat> sourceFormats(int nominalFps, bool interlacedProject, bool filmProject);
// The Output menu's choices for a Source: the source's own format, and the conversions of the p.205 table.
QList<TimewarpFormat> outputFormats(TimewarpFormat source, int nominalFps, bool interlacedProject);
// "The Adaptive Deinterlace Source check box is available ... when ... The Source parameter value is
// Interlaced. The Output parameter value is Progressive. The Type parameter value is one of the following:
// Both Fields, Blended Interpolated, Blended VTR" (p.181).
bool adaptiveAvailable(TimewarpFormat source, TimewarpFormat output, TimewarpType type);
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
// Whether the Type makes in-between frames from the two source frames around the position: Blended
// Interpolated and Blended VTR mix them ("Blends, or averages, pixels from the original frames", p.186),
// FluidMotion follows the motion between them (the compositor tells them apart). The others show whole
// source frames.
bool blendsFrames(const Effect& effect);
// "If the option is set to a specific motion effect type, Media Composer changes all motion effects to
// that type when it renders them" (p.173): the Type a Timewarp renders with under the Render setting's
// Timewarps Render Using (`renderUsing`, "original" or a timewarpTypeName()), or nothing when it keeps
// its own (Original Preference, or Ignore Render Setting).
std::optional<TimewarpType> renderTypeFor(const Effect& effect, const QString& renderUsing);

// Promote to Timewarp (p.177): a Timewarp effect from a traditional motion effect's speed ("The Motion
// Effect Editor uses the duration and speed information from the traditional motion effect to create a
// Timewarp effect"; "Strobe motion information does not carry over").
Effect promotedTimewarp(double speed);
// Promotes the segment's motion effect to a Timewarp: its own Motion Effect (motionSpeed), or, for a
// motion effect clip (resolved through `resolve`), the clip's source with the source frames the
// segment showed. False when the segment has no traditional motion effect.
bool promoteToTimewarp(Segment& segment, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve);

} // namespace lumenis::model
