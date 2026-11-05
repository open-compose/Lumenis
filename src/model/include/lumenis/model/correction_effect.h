#pragma once

// A colour correction shown in the Effect Editor (FX Guide p.683-686; docs/spec/color-correction.md, 6.10d3b): "Slider
// controls appear in the Effect Editor, so you can adjust each parameter either in the Effect Editor or in the Color
// Correction tool ... ganged together, so changes in one are immediately represented in the other" (p.685).
//
// Ours: the Effect Editor edits effects, so in Color Correction mode it is given the current correction as an effect
// (correctionEffect) of category kCorrectionModeCategory, with one parameter category per tab: "HSL Master",
// "HSL Highlights", "HSL Midtones", "HSL Shadows" (the Controls tab's ranges), "HSL Hue Offsets" (each ChromaWheel as
// its Hue and Hue Amount) and "Secondary <Vector>" for each of the twelve vectors' sliders; its changes go back with
// correctionFromEffect. The Levels and Curves tabs come as "Levels <Tab>" and "Curves" (6.10d3c): "You can enable and
// disable these parameters in the Effect Editor. You can also keyframe these parameters in their keyframe tracks.
// However, because these parameters require specialized controls, you cannot open keyframe graphs or adjust the parameter
// values directly in the Effect Editor" (p.685): they are track-only parameters with their Enable buttons.

#include "lumenis/model/color_correction.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// The category of the effect correctionEffect makes; no effect of the catalogue has it.
inline const QString kCorrectionModeCategory = QStringLiteral("Color Correction Mode");

// One slider of a correction in the Effect Editor.
struct CorrectionParameter {
    QString category; // "HSL Master"
    QString name;     // "Gain"
    QString path;     // "hsl/controls/master/gain"
    double min = 0.0;
    double max = 100.0;
    double defaultValue = 0.0;
    int decimals = 0;
    // A Levels or Curves control: a keyframe track and an Enable button, no slider or graph (p.685).
    bool trackOnly = false;
    // The Enable button the parameter's row has in the Effect Editor (track-only parameters): its path in the
    // correction ("levels/composite", "curves/red"); empty for none.
    QString enablePath;
};
// Every one, in the editor's order.
const QList<CorrectionParameter>& correctionParameters();
// The categories in that order.
QStringList correctionParameterCategories();
std::optional<CorrectionParameter> correctionParameterOf(const QString& category, const QString& name);
std::optional<CorrectionParameter> correctionParameterAtPath(const QString& path);

// "In the Effect Editor, values range from -360 degrees to +360 degrees. In the Color Correction tool, values range from
// -180 degrees to +180 degrees ... a value of +340 in the Effect Editor displays as -20 in the Color Correction tool"
// (p.685): the tool's hue for one of the Effect Editor's.
double toolHue(double editorHue);

// The correction as an effect: each parameter's value (only those the correction sets) and its keyframes.
Effect correctionEffect(const ColorCorrection& correction);
// `base` with the effect's parameters and keyframes in place of its own for every parameter correctionEffect gives; the
// controls it does not show (Channels, Luma Ranges, the Enable buttons...) and the values of track-only parameters stay
// `base`'s (their keyframes are the effect's). A value at
// its default is left out as the tool leaves it; one set anew turns its Enable buttons on, as in the tool.
ColorCorrection correctionFromEffect(const Effect& effect, const ColorCorrection& base);

bool isCorrectionModeEffect(const Effect& effect);

} // namespace lumenis::model
