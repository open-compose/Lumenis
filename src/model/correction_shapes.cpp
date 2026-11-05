// Shape-based colour correction's data on the Color Correction effect (FX Guide p.697-699). See
// lumenis/model/correction_shapes.h.

#include "lumenis/model/correction_shapes.h"

#include <algorithm>

namespace lumenis::model {

ColorCorrection shapeCorrectionOf(const Effect& effect) {
    return ColorCorrection::fromJson(effect.parameters.value(kShapeCorrectionCategory).toObject());
}

void setShapeCorrection(Effect& effect, const ColorCorrection& correction) {
    if (correction.isEmpty()) effect.parameters.remove(kShapeCorrectionCategory);
    else effect.parameters.insert(kShapeCorrectionCategory, correction.toJson());
}

ColorCorrection layerCorrectionOf(const Effect& effect, CorrectionLayer layer) {
    return layer == CorrectionLayer::Shapes ? shapeCorrectionOf(effect) : colorCorrectionOf(effect);
}

void setLayerCorrection(Effect& effect, CorrectionLayer layer, const ColorCorrection& correction) {
    if (layer == CorrectionLayer::Shapes) setShapeCorrection(effect, correction);
    else setColorCorrection(effect, correction);
}

CorrectionShapeOptions shapeOptionsOf(const Effect& effect) {
    const QJsonObject o = effect.parameters.value(kShapeOptionsCategory).toObject();
    CorrectionShapeOptions out;
    out.serial = o.value(QStringLiteral("Serial Mode")).toBool(true);
    out.invertAlpha = o.value(QStringLiteral("Invert Alpha")).toBool(false);
    return out;
}

void setShapeOptions(Effect& effect, const CorrectionShapeOptions& options) {
    if (options == CorrectionShapeOptions{}) {
        effect.parameters.remove(kShapeOptionsCategory);
        return;
    }
    effect.parameters.insert(kShapeOptionsCategory,
                             QJsonObject{{QStringLiteral("Serial Mode"), options.serial}, {QStringLiteral("Invert Alpha"), options.invertAlpha}});
}

bool hasCorrectionShapes(const Effect& effect) {
    return !intraframeObjects(effect).isEmpty();
}

double shapeSoftness(const IntraframeObject& shape) {
    return std::clamp(shape.parameters.value(kShapeCategory).toObject().value(QStringLiteral("Softness")).toDouble(0.0), 0.0, 100.0);
}

void setShapeSoftness(IntraframeObject& shape, double softness) {
    QJsonObject cat = shape.parameters.value(kShapeCategory).toObject();
    cat.insert(QStringLiteral("Softness"), std::clamp(softness, 0.0, 100.0));
    shape.parameters.insert(kShapeCategory, cat);
}

} // namespace lumenis::model
