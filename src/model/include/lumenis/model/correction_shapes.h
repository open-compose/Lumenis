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
// its own "Shape" category; the shape layer's correction is the "Shape Correction" category, kept as the base one; Serial
// Mode and Invert Alpha are the "Shape Options" category. The picture (media::colorCorrectedShapesPicture): the base
// correction on the layer; the shape correction on that (Serial Mode) or on the layer; the two mixed by the shapes'
// coverage, softened and inverted as set.

#include "lumenis/model/color_correction.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"

namespace lumenis::model {

inline const QString kShapeCorrectionCategory = QStringLiteral("Shape Correction");
inline const QString kShapeOptionsCategory = QStringLiteral("Shape Options");
inline const QString kShapeCategory = QStringLiteral("Shape");

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

// Whether the effect has shapes; the shape layer counts only then.
bool hasCorrectionShapes(const Effect& effect);

// A shape's Softness: 0 to 100 (ours: the edge softened by up to 100 pixels of a frame 1920 wide), 0 at first.
double shapeSoftness(const IntraframeObject& shape);
void setShapeSoftness(IntraframeObject& shape, double softness);

} // namespace lumenis::model
