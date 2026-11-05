// A colour correction shown in the Effect Editor (FX Guide p.683-686): its sliders as effect parameters and back. See
// lumenis/model/correction_effect.h.

#include "lumenis/model/correction_effect.h"

#include "lumenis/model/correction_keyframes.h"
#include "lumenis/model/effect_keyframes.h"

#include <cmath>

namespace lumenis::model {

namespace {

QString capitalised(const QString& s) {
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}

CorrectionParameter make(const QString& category, const QString& name, const QString& path) {
    CorrectionParameter p;
    p.category = category;
    p.name = name;
    p.path = path;
    if (const auto r = colorControlRange(path)) {
        p.min = r->first;
        p.max = r->second;
    }
    p.defaultValue = colorControlDefault(path).value_or(0.0);
    if (path.endsWith(QLatin1String("/gamma"))) p.decimals = 2;
    // "Advanced keyframe graphs use a larger range of numerical values for Hue parameters than the Color Correction tool.
    // In the Effect Editor, values range from -360 degrees to +360 degrees" (p.685).
    if (path.endsWith(QLatin1String("/hue"))) {
        p.min = -360.0;
        p.max = 360.0;
    }
    return p;
}

QList<CorrectionParameter> build() {
    QList<CorrectionParameter> out;
    // The Controls tab's ranges (p.632-634).
    const std::pair<const char*, const char*> controls[] = {{"hue", "Hue"},       {"saturation", "Saturation"}, {"brightness", "Brightness"},
                                                            {"contrast", "Contrast"}, {"gain", "Gain"},           {"gamma", "Gamma"},
                                                            {"setup", "Setup"}};
    for (const char* range : {"master", "highlights", "midtones", "shadows"}) {
        const QString category = QStringLiteral("HSL ") + capitalised(QLatin1String(range));
        for (const auto& [key, text] : controls)
            out << make(category, QLatin1String(text), QStringLiteral("hsl/controls/%1/%2").arg(QLatin1String(range), QLatin1String(key)));
        if (QLatin1String(range) == QLatin1String("master")) {
            out << make(category, QStringLiteral("Clip Low"), QStringLiteral("hsl/controls/master/clipLow"));
            out << make(category, QStringLiteral("Clip High"), QStringLiteral("hsl/controls/master/clipHigh"));
        }
    }
    // "Each ChromaWheel in the Color Correction tool is represented as two sliders in the Effect Editor, one for the Hue
    // and the other for the Hue Amount" (p.685).
    for (const char* wheel : {"highlights", "midtones", "shadows"}) {
        const QString w = capitalised(QLatin1String(wheel));
        out << make(QStringLiteral("HSL Hue Offsets"), w + QStringLiteral(" Hue"), QStringLiteral("hsl/hueOffsets/%1/hue").arg(QLatin1String(wheel)));
        out << make(QStringLiteral("HSL Hue Offsets"), w + QStringLiteral(" Hue Amount"), QStringLiteral("hsl/hueOffsets/%1/amount").arg(QLatin1String(wheel)));
    }
    // The Levels and Curves tabs as keyframe tracks only (p.685), in the tool's tab order: each Levels tab's points
    // (p.656-660) under that tab's Enable button, each Curves graph (p.661-663) under its own.
    auto track = [](const QString& category, const QString& name, const QString& path, const QString& enable) {
        CorrectionParameter p = make(category, name, path);
        p.trackOnly = true;
        p.enablePath = enable;
        return p;
    };
    const std::pair<const char*, const char*> points[] = {{"inBlack", "Input Black"},   {"inGray", "Input Gray"},   {"inWhite", "Input White"},
                                                          {"outBlack", "Output Black"}, {"outGray", "Output Gray"}, {"outWhite", "Output White"}};
    for (const QString& tab : levelsTabs()) {
        const QString enable = QStringLiteral("levels/") + tab;
        const QStringList shown = levelsPoints(tab);
        for (const auto& [key, text] : points) {
            if (shown.contains(QLatin1String(key))) out << track(QStringLiteral("Levels ") + capitalised(tab), QLatin1String(text), enable + QLatin1Char('/') + QLatin1String(key), enable);
        }
    }
    for (const QString& graph : curveGraphs()) {
        const QString path = QStringLiteral("curves/") + graph;
        out << track(QStringLiteral("Curves"), capitalised(graph), path, path);
    }
    // The Curves tab's Master sliders are the HSL Master controls (canonicalControlPath), shown there with graphs.
    // The Secondary tab's sliders, vector by vector (p.668-676).
    for (const QString& vector : secondaryVectors()) {
        const QString category = QStringLiteral("Secondary ") + (isStandardVector(vector) ? capitalised(vector) : QStringLiteral("Custom ") + vector);
        const std::pair<const char*, const char*> inputs[] = {
            {"hue", "Input Hue"}, {"saturation", "Input Saturation"}, {"hueWidth", "Input Hue Width"},
            {isStandardVector(vector) ? "satMin" : "satWidth", isStandardVector(vector) ? "Input Saturation Minimum" : "Input Saturation Width"},
            {"softness", "Input Softness"}};
        for (const auto& [key, text] : inputs) out << make(category, QLatin1String(text), vectorPath(vector, QStringLiteral("input"), QLatin1String(key)));
        for (const auto& [key, text] : {std::pair{"hue", "Output Hue"}, std::pair{"saturation", "Output Saturation"}, std::pair{"luminance", "Output Luminance"}})
            out << make(category, QLatin1String(text), vectorPath(vector, QStringLiteral("output"), QLatin1String(key)));
    }
    return out;
}

} // namespace

const QList<CorrectionParameter>& correctionParameters() {
    static const QList<CorrectionParameter> all = build();
    return all;
}

QStringList correctionParameterCategories() {
    QStringList out;
    for (const CorrectionParameter& p : correctionParameters()) {
        if (!out.contains(p.category)) out << p.category;
    }
    return out;
}

std::optional<CorrectionParameter> correctionParameterOf(const QString& category, const QString& name) {
    for (const CorrectionParameter& p : correctionParameters()) {
        if (p.category == category && p.name == name) return p;
    }
    return std::nullopt;
}

std::optional<CorrectionParameter> correctionParameterAtPath(const QString& path) {
    const QString canonical = canonicalControlPath(path);
    for (const CorrectionParameter& p : correctionParameters()) {
        if (p.path == canonical) return p;
    }
    return std::nullopt;
}

double toolHue(double editorHue) {
    double h = std::fmod(editorHue, 360.0);
    if (h > 180.0) h -= 360.0;
    if (h < -180.0) h += 360.0;
    return h;
}

bool isCorrectionModeEffect(const Effect& effect) {
    return effect.category == kCorrectionModeCategory;
}

Effect correctionEffect(const ColorCorrection& correction) {
    Effect e;
    e.category = kCorrectionModeCategory;
    e.name = QStringLiteral("Color Correction");
    for (const CorrectionParameter& p : correctionParameters()) {
        if (correction.values.contains(p.path)) {
            QJsonObject cat = e.parameters.value(p.category).toObject();
            cat.insert(p.name, correction.values.value(p.path));
            e.parameters.insert(p.category, cat);
        }
        const ParameterKeyframes keys = correctionKeyframesOf(correction, p.path);
        if (!keys.keys.isEmpty()) setKeyframes(e, p.category, p.name, keys);
    }
    return e;
}

ColorCorrection correctionFromEffect(const Effect& effect, const ColorCorrection& base) {
    ColorCorrection out = base;
    for (const CorrectionParameter& p : correctionParameters()) {
        const QJsonValue v = effect.parameters.value(p.category).toObject().value(p.name);
        ParameterKeyframes keys = keyframesOf(effect, p.category, p.name);
        // Hue keyframes beyond the tool's range turn round to it (p.685); the value between them is the editor's.
        const bool hue = p.path.endsWith(QLatin1String("/hue"));
        if (p.trackOnly) {
            // Only the track can change here (p.685): the values are the tool's.
            if (correctionKeyframesOf(base, p.path) == keys) continue;
            if (keys.keys.isEmpty()) {
                // The last keyframe going leaves the control at the value it had there.
                const ParameterKeyframes before = correctionKeyframesOf(base, p.path);
                if (!before.keys.isEmpty() && !base.values.contains(p.path)) out.values.insert(p.path, before.keys.last().value);
                out.keyframes.remove(p.path);
            } else {
                keys.interpolation = Interpolation::Shelf;
                setCorrectionKeyframes(out, p.path, keys);
            }
            continue;
        }
        if (keys.keys.isEmpty()) {
            out.keyframes.remove(p.path);
            if (v.isUndefined() || (v.isDouble() && v.toDouble() == p.defaultValue)) {
                out.values.remove(p.path);
            } else if (base.values.value(p.path) != v) {
                out.set(p.path, hue && v.isDouble() ? QJsonValue(toolHue(v.toDouble())) : v);
            }
            continue;
        }
        if (correctionKeyframesOf(base, p.path) != keys) {
            for (const QString& d : QStringList(out.disabled)) {
                if (p.path == d || p.path.startsWith(d + QLatin1Char('/'))) out.disabled.removeAll(d);
            }
        }
        setCorrectionKeyframes(out, p.path, keys);
        if (!v.isUndefined()) out.values.insert(p.path, v);
    }
    return out;
}

} // namespace lumenis::model
