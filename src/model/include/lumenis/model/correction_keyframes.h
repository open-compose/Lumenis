#pragma once

// Keyframing colour corrections (FX Guide p.683-686; docs/spec/color-correction.md, 6.10d3): "You can use the advanced
// keyframes interface in the Effect Editor to animate most color correction adjustments" (p.683). A correction keeps
// the keyframes of its controls in ColorCorrection::keyframes, by control path, as an effect keeps its parameters'
// (lumenis/model/effect_keyframes.h), over the segment's frames (EffectSpan of the segment's length).
//
// Which controls take keyframes (p.685): the HSL Controls tab's, the Hue Offsets tab's (each ChromaWheel as its Hue
// and Hue Amount) and the Secondary tab's sliders are "fully keyframeable"; the Curves and Levels tabs' "you can
// keyframe ... in their keyframe tracks" without graphs; Isolate, Invert Last Vector, the Luma Ranges and the Channels
// tabs take none.

#include "lumenis/model/color_correction.h"
#include "lumenis/model/effect_keyframes.h"

namespace lumenis::model {

// How a control takes keyframes: not at all, with a graph (numbers between keyframes as other effects' parameters do),
// or on a keyframe track only (ours: the value of the keyframe at or before the time, as Shelf; a curve's points do not
// blend).
enum class CorrectionKeyframing { None, Graph, Track };
CorrectionKeyframing correctionKeyframing(const QString& path);

// A control's keyframes (empty: none) and setting them (empty removes them).
ParameterKeyframes correctionKeyframesOf(const ColorCorrection& c, const QString& path);
void setCorrectionKeyframes(ColorCorrection& c, const QString& path, const ParameterKeyframes& keys);
// The keyframed controls, by path.
QStringList keyframedControls(const ColorCorrection& c);
// Sets (adds or replaces) the keyframe of a control at time `t`; a control that takes no keyframes gets the value
// instead. A new keyframe track starts from nothing: the control's value elsewhere is the keyframe's until another is
// added.
void setCorrectionKeyframe(ColorCorrection& c, const QString& path, double t, const EffectSpan& span, const QJsonValue& value);
// Removes the control's keyframe within half a frame of `t`; the last one going leaves the control at that value.
bool removeCorrectionKeyframe(ColorCorrection& c, const QString& path, double t, const EffectSpan& span);

// The correction at time `t` of the segment: each keyframed control's value from its keyframes, no keyframes left.
ColorCorrection correctionAt(const ColorCorrection& c, double t, const EffectSpan& span);

// An adjustment made in the Color Correction tool at time `t`: `edited` is correctionAt(stored, t) changed by the tool.
// A changed keyframed control gets a keyframe at `t` ("Make an adjustment to the appropriate parameter in the Color
// Correction tool. A new keyframe appears ... at the location of the position indicator", p.686); a control the tool
// reset loses its keyframes; any other change is a plain value, and the Enable buttons are the edited ones.
//
// `selected` are the frames (into the segment) of selected keyframe markers (p.686): a changed control with a keyframe
// at one of them changes those keyframes rather than getting a new one at `t`, which is why the guide's steps deselect
// the first keyframe before moving on ("Ctrl-click ... the keyframe added in step 1 to deselect it").
ColorCorrection adjustedAt(const ColorCorrection& stored, const ColorCorrection& edited, double t, const EffectSpan& span,
                           const QList<double>& selected = {});

// Every keyframe's frame into the segment (sorted, each once), for the keyframe markers below the Current monitor
// (p.686).
QList<double> correctionKeyframeFrames(const ColorCorrection& c, const EffectSpan& span);

// The keyframe markers' Delete (p.686): every control's keyframes at the frames (into the segment); a control losing its
// last keeps that value. True if any went.
bool removeCorrectionKeyframesAt(ColorCorrection& c, const QList<double>& frames, const EffectSpan& span);
// Alt-dragging keyframe markers (p.686): every control's keyframes at `frames` move by `delta` frames, together, as the
// Effect Editor's Align Keyframes moves a group (each stops a frame short of a keyframe of its control that is not
// moving, and stays within the segment). True if any moved.
bool moveCorrectionKeyframes(ColorCorrection& c, const QList<double>& frames, double delta, const EffectSpan& span);

} // namespace lumenis::model
