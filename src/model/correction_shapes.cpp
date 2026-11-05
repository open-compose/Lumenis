// Shape-based colour correction's data on the Color Correction effect (FX Guide p.697-699). See
// lumenis/model/correction_shapes.h.

#include "lumenis/model/correction_shapes.h"

#include "lumenis/model/effect_keyframes.h"

#include <QJsonObject>

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

CorrectionShapeView shapeViewFor(CorrectionLayer layer, bool showAlpha, bool showResult) {
    if (showAlpha) return CorrectionShapeView::Alpha;
    if (showResult) return CorrectionShapeView::Result;
    return layer == CorrectionLayer::Shapes ? CorrectionShapeView::Shapes : CorrectionShapeView::Base;
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

bool isShapeOwnCategory(const QString& category) {
    return category == kShapeCategory || category == kObjectTrackingCategory;
}

void trimShapeParameters(QList<IntraframeObject>& shapes) {
    for (IntraframeObject& o : shapes) {
        QJsonObject kept;
        for (auto it = o.parameters.constBegin(); it != o.parameters.constEnd(); ++it) {
            if (isShapeOwnCategory(it.key()) || it.key() == kIntraframeObjectCategory) kept.insert(it.key(), it.value());
        }
        // Keyframes of those categories only (kKeyframesKey: {"<category>/<parameter>": [...]}).
        QJsonObject keys;
        const QJsonObject all = o.parameters.value(QLatin1String(kKeyframesKey)).toObject();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            const QString category = it.key().section(QLatin1Char('/'), 0, 0);
            if (isShapeOwnCategory(category) || category == kIntraframeObjectCategory) keys.insert(it.key(), it.value());
        }
        if (!keys.isEmpty()) kept.insert(QLatin1String(kKeyframesKey), keys);
        o.parameters = kept;
    }
}

Effect correctionShapeView(const Effect& effect, const IntraframeObject& shape) {
    Effect view = effect;
    for (const QString& c : {kShapeCategory, kObjectTrackingCategory}) {
        if (shape.parameters.contains(c)) view.parameters.insert(c, shape.parameters.value(c));
        else view.parameters.remove(c);
    }
    return view;
}

void keepCorrectionShapeChange(Effect& effect, const QString& shapeId, const Effect& changed) {
    QList<IntraframeObject> shapes = intraframeObjects(effect);
    const QJsonValue objects = effect.parameters.value(kIntraframeCategory);
    for (IntraframeObject& o : shapes) {
        if (o.id != shapeId) continue;
        for (const QString& c : {kShapeCategory, kObjectTrackingCategory}) {
            if (changed.parameters.contains(c)) o.parameters.insert(c, changed.parameters.value(c));
            else o.parameters.remove(c);
        }
    }
    // The rest is the effect's: what the view showed of the shape's own categories stays as the effect had it.
    QJsonObject rest = changed.parameters;
    for (const QString& c : {kShapeCategory, kObjectTrackingCategory}) {
        if (effect.parameters.contains(c)) rest.insert(c, effect.parameters.value(c));
        else rest.remove(c);
    }
    if (!objects.isUndefined()) rest.insert(kIntraframeCategory, objects);
    effect.parameters = rest;
    setIntraframeObjects(effect, shapes);
}

} // namespace lumenis::model
