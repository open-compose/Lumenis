#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>
#include <utility>

// Keyframes on effect parameters (FX Guide ch.4, p.94-134; docs/spec/effect-keyframes.md). Each
// keyframeable parameter of an effect can have keyframes of its own ("advanced keyframes", p.94):
// values at points in the effect, with an interpolation between them, Elastic or Fixed timing, and
// Extrapolate or Hold outside the first and last. They are kept with the effect's parameters, under
// kKeyframesKey in model::Effect::parameters, by "Category/Name". A parameter without keyframes has
// the one value stored for it (or its default) for the whole effect.
namespace lumenis::model {

// The key in Effect::parameters that holds the keyframes. No parameter category has this name.
inline constexpr char kKeyframesKey[] = "_keyframes";

// "Shelf", "Linear", "Spline", "Bézier" (p.120-122). Linear is the default for new keyframes.
enum class Interpolation { Shelf, Linear, Spline, Bezier };

// How an effect's frames map to its time, the fraction (0 to 1) its keyframes are placed in. A
// segment effect's frame f of `length` is at f / (length - 1), so its first frame is 0 and its last 1;
// a transition's is at its progress, (f + 1) / (length + 1), as the compositor blends it
// (docs/spec/compositor.md), so neither end shows only one side. Implicitly made from a segment
// effect's length.
struct EffectSpan {
    std::int64_t length = 1;
    bool transition = false;
    EffectSpan(std::int64_t length_ = 1, bool transition_ = false) : length(length_), transition(transition_) {}
    // The time of a frame into the effect (0: its first frame; fractions and frames outside it allowed).
    double timeOf(double frame) const;
    // The frame into the effect at a time (not rounded).
    double frameOf(double t) const;
    // Half a frame, as time.
    double halfFrame() const { return timeOf(0.5) - timeOf(0.0); }
};

struct EffectKeyframe {
    // Where, as the effect's time (EffectSpan) for Elastic keyframes, or in frames from its first
    // frame for Fixed ones (p.124). Keyframes can lie outside the effect (p.113).
    double at = 0.0;
    // A number, a colour ("#rrggbb") or a flag; only numbers and colours change between keyframes.
    QJsonValue value;
    // Bézier direction handles (p.122): the curve leaves along (outTime, outValue) and arrives along
    // (inTime, inValue), in the units of `at` and of the value. Zero handles: the Spline's tangents.
    double inTime = 0.0, inValue = 0.0, outTime = 0.0, outValue = 0.0;

    QJsonObject toJson() const;
    static EffectKeyframe fromJson(const QJsonObject& o);
    bool operator==(const EffectKeyframe&) const = default;
};

struct ParameterKeyframes {
    QList<EffectKeyframe> keys; // sorted by `at`
    Interpolation interpolation = Interpolation::Linear;
    // "Fixed": keyframe timing stays when the effect is trimmed; "Elastic", the default, scales with it
    // (p.124). All keyframes of one parameter are one or the other (p.125).
    bool fixed = false;
    // "Extrapolate" carries the change on beyond the first and last keyframes; "Hold", the default,
    // keeps their values (p.125-127).
    bool extrapolate = false;

    QJsonObject toJson() const;
    static ParameterKeyframes fromJson(const QJsonObject& o);
    bool operator==(const ParameterKeyframes&) const = default;
};

// "Category/Name", the key of a parameter's keyframes.
QString parameterKey(const QString& category, const QString& name);

// The keyframes of one parameter; empty when it has none.
ParameterKeyframes keyframesOf(const Effect& effect, const QString& category, const QString& name);
void setKeyframes(Effect& effect, const QString& category, const QString& name, const ParameterKeyframes& keys);
// Every parameter with keyframes, by "Category/Name".
QStringList keyframedParameters(const Effect& effect);

// A keyframe's time in the effect, and the position (`at`) a keyframe at time `t` is stored at.
double keyframeTime(const ParameterKeyframes& keys, const EffectKeyframe& key, const EffectSpan& span);
double storedPosition(const ParameterKeyframes& keys, double t, const EffectSpan& span);

// The parameter's value at time `t` of the effect: from its keyframes, else the value stored for it,
// else nothing. The effect's Acceleration (FX Guide p.492) eases the value in and out of each keyframe.
std::optional<double> numberAt(const Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span);
// A colour parameter's red, green and blue, 0 to 1 ("#rrggbb" values, each channel on its own curve).
struct Rgb {
    double red = 0.0, green = 0.0, blue = 0.0;
};
std::optional<Rgb> colorAt(const Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span);

// Sets (adds or replaces) the keyframe at `t` of the effect for one parameter, keeping its handles if
// one was there. A new keyframe is Linear unless the parameter has an interpolation already.
void setKeyframe(Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span, const QJsonValue& value);
// Removes the keyframes of the parameter within half a frame of `t`; true if one went.
bool removeKeyframe(Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span);
// The index of the parameter's keyframe within half a frame of `t`; -1 if none.
int keyframeAt(const ParameterKeyframes& keys, double t, const EffectSpan& span);

// A Bézier direction handle (p.122): where it points from its keyframe, in time and value.
struct BezierHandle {
    double time = 0.0;
    double value = 0.0;
};
// A numeric keyframe's way in and way out as the Bézier curve uses them: its handles, or where they are
// when not set (the Spline's tangent, a third of the way to the neighbour on that side).
std::pair<BezierHandle, BezierHandle> effectiveHandles(const ParameterKeyframes& keys, int index, const EffectSpan& span);
// Sets a keyframe's handles (the way in at or before it in time, the way out at or after it).
void setHandles(ParameterKeyframes& keys, int index, const BezierHandle& in, const BezierHandle& out, const EffectSpan& span);

// Fixed or Elastic (p.124-125), keeping every keyframe at the time it is at now.
void setFixed(ParameterKeyframes& keys, bool fixed, const EffectSpan& span);

// Moves keyframe `index` to time `t` and/or value `value` (dragging, p.112-113). Keyframes cannot pass
// their neighbours: the move stops a frame short of them (p.114). True if anything changed.
bool moveKeyframe(ParameterKeyframes& keys, int index, std::optional<double> t, std::optional<double> value, const EffectSpan& span);

// Align Keyframes (p.114-116): the keyframes `selected` (indices) move by the distance that brings
// `reference` to time `t`, as a group, stopping a frame short of a keyframe not selected. Slip
// Keyframes (p.116, `slip`): every keyframe of the parameter moves by that distance. Either can move
// keyframes outside the effect (p.115). True if anything moved.
bool alignKeyframes(ParameterKeyframes& keys, const QList<int>& selected, int reference, double t, const EffectSpan& span, bool slip);

// Remove Redundant Keyframes (p.106-107): keyframes the curve passes through anyway. With Linear, a
// keyframe on the straight line between its neighbours; with Shelf, one with its predecessor's value;
// with Spline and Bézier, conservatively, only one with the same value as both neighbours. The first
// and last keyframes stay. Returns how many went.
int removeRedundantKeyframes(ParameterKeyframes& keys);

// After `frames` were added at the head of a segment (negative: taken off), its effect's Fixed
// keyframes move with the material so their changes stay at the same timecode ("Fixed keyframes let
// you 'pin' a parameter change to a particular timecode", p.124); Elastic ones scale with the new
// length by themselves.
void shiftFixedKeyframes(Effect& effect, std::int64_t frames);

// Reverse Keyframes (p.123-124): the keyframes of the parameters named (empty: every parameter)
// mirrored in time, their values unchanged. Reversing the whole of a transition also turns its Swap
// Sources on (off when it was on), "to maintain shot continuity".
// The span says whether the effect is a transition.
void reverseKeyframes(Effect& effect, const EffectSpan& span, const QStringList& parameters = {});

// The Fade Effect button (p.46-47): the Foreground Level keyframed from 0 to 100 over the first `up`
// frames and back to 0 over the last `down` frames of an effect `length` frames long. Either can be 0.
void fadeEffect(Effect& effect, std::int64_t length, std::int64_t up, std::int64_t down);

} // namespace lumenis::model
