#pragma once

// Shape-based colour correction (FX Guide p.697-699; docs/spec/color-correction.md, 6.10e3): a Color Correction
// effect ("Select CC Effect from the color correction relationship pulldown options") has two layers. "Base - sets the
// base layer that allows you to make color corrections to the material in the sequence"; "Shapes - selects the shape
// layer that allows you to use the shape based tools to color correct portions of the material". The shape tools draw
// rectangles, ovals, polygons, curves and brush strokes; "Each shape can have a different softness"; "Serial Mode -
// when selected, any changes you have made to the base will be included when you are performing a shaped based color
// correction"; "Invert Alpha - when selected displays the inverted alpha" (ours: the shape layer then corrects outside
// the shapes); shapes can follow trackers (p.699).
//
// Ours: the base correction is the effect's "Color Correction" category (model::colorCorrectionOf); the shapes are
// the effect's Intraframe objects (lumenis/model/intraframe.h, category "Intraframe"), each with its Softness under
// its own "Shapes" category; the shape layer's correction is the "Shape Correction" category, kept as the base one; Serial
// Mode and Invert Alpha are the "Layers" category. The picture (media::colorCorrectedShapesPicture): the base
// correction on the layer; the shape correction on that (Serial Mode) or on the layer; the two mixed by the shapes'
// coverage, softened and inverted as set.

#include "lumenis/model/color_correction.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"

namespace lumenis::model {

inline const QString kShapeCorrectionCategory = QStringLiteral("Shape Correction");
// The effect's Serial Mode and Invert Alpha, the Effect Editor's "Layers" category (p.699's picture).
inline const QString kShapeOptionsCategory = QStringLiteral("Layers");
// A shape's own settings (Softness), its "Shapes" category; its tracker is kObjectTrackingCategory (intraframe.h).
inline const QString kShapeCategory = QStringLiteral("Shapes");

// Which layer the Color Correction tool works on: "Shapes" or "Base" (p.698).
enum class CorrectionLayer { Base, Shapes };

// The shape layer's correction, and setting it (empty removes it).
ColorCorrection shapeCorrectionOf(const Effect& effect);
void setShapeCorrection(Effect& effect, const ColorCorrection& correction);
// A layer's correction: the base one (colorCorrectionOf) or the shape one.
ColorCorrection layerCorrectionOf(const Effect& effect, CorrectionLayer layer);
void setLayerCorrection(Effect& effect, CorrectionLayer layer, const ColorCorrection& correction);

// "Serial Mode" (on at first, as the guide's picture shows it, p.698) and "Invert Alpha" (off).
struct CorrectionShapeOptions {
    bool serial = true;
    bool invertAlpha = false;
    bool operator==(const CorrectionShapeOptions&) const = default;
};
CorrectionShapeOptions shapeOptionsOf(const Effect& effect);
void setShapeOptions(Effect& effect, const CorrectionShapeOptions& options);

// What Color Correction mode's monitors show of a Color Correction effect with shapes (p.698; 6.10e3b1), by the shape
// panel's buttons: "Show Result - when selected, shows the composite result of the base+ shape layer (the final result)"
// (Result, also outside Color Correction mode); "Show Alpha Icon - when selected displays the alpha" (Alpha: the shapes'
// coverage in grey, white inside). Without Show Result the layer being worked on (ours): Base, the base correction
// alone; Shapes, the shape layer's picture inside the shapes over the uncorrected picture.
enum class CorrectionShapeView { Result, Base, Shapes, Alpha };
// The view the buttons give: Show Alpha first, then Show Result, then the layer.
CorrectionShapeView shapeViewFor(CorrectionLayer layer, bool showAlpha, bool showResult);

// Whether the effect has shapes; the shape layer counts only then.
bool hasCorrectionShapes(const Effect& effect);

// A shape's Softness: 0 to 100 (ours: the edge softened by up to 100 pixels of a frame 1920 wide), 0 at first.
double shapeSoftness(const IntraframeObject& shape);
void setShapeSoftness(IntraframeObject& shape, double softness);

// The Effect Editor with one shape selected (FX Guide p.699; 6.10e3b3): the effect as that shape sees it, the
// effect's own parameters with the shape's "Shapes" and "Shape Tracking" categories; and a change made to that view
// kept, those two categories going to the shape and every other one to the effect.
bool isShapeOwnCategory(const QString& category);
// A new shape takes the effect's own parameters (model::addIntraframeObject); a shape keeps only its own categories,
// its place ("Intraframe Object") and their keyframes, the corrections and options staying the effect's.
void trimShapeParameters(QList<IntraframeObject>& shapes);
Effect correctionShapeView(const Effect& effect, const IntraframeObject& shape);
void keepCorrectionShapeChange(Effect& effect, const QString& shapeId, const Effect& changed);

} // namespace lumenis::model
