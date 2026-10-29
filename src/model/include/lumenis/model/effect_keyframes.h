#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

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

struct EffectKeyframe {
    // Where, as a fraction of the effect (0: its first frame, 1: its last) for Elastic keyframes, or
    // in frames from its first frame for Fixed ones (p.124).
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

// The parameter's value at `t`, a fraction of the effect (0 to 1), in an effect `length` frames long
// (for Fixed keyframes): from its keyframes, else the value stored for it, else nothing.
std::optional<double> numberAt(const Effect& effect, const QString& category, const QString& name, double t, std::int64_t length);
// A colour parameter's red, green and blue, 0 to 1 ("#rrggbb" values, each channel on its own curve).
struct Rgb {
    double red = 0.0, green = 0.0, blue = 0.0;
};
std::optional<Rgb> colorAt(const Effect& effect, const QString& category, const QString& name, double t, std::int64_t length);

// Sets (adds or replaces) the keyframe at `t` of the effect for one parameter, keeping its handles if
// one was there. A new keyframe is Linear unless the parameter has an interpolation already.
void setKeyframe(Effect& effect, const QString& category, const QString& name, double t, std::int64_t length, const QJsonValue& value);
// Removes the keyframes of the parameter within half a frame of `t`; true if one went.
bool removeKeyframe(Effect& effect, const QString& category, const QString& name, double t, std::int64_t length);

// Reverse Keyframes (p.123-124): the keyframes of the parameters named (empty: every parameter)
// mirrored in time, their values unchanged. Reversing the whole of a transition also turns its Swap
// Sources on (off when it was on), "to maintain shot continuity".
void reverseKeyframes(Effect& effect, std::int64_t length, const QStringList& parameters = {}, bool transition = false);

// The Fade Effect button (p.46-47): the Foreground Level keyframed from 0 to 100 over the first `up`
// frames and back to 0 over the last `down` frames of an effect `length` frames long. Either can be 0.
void fadeEffect(Effect& effect, std::int64_t length, std::int64_t up, std::int64_t down);

} // namespace lumenis::model
