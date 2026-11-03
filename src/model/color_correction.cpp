// Colour corrections' controls (color_correction.h).

#include "lumenis/model/color_correction.h"

#include "lumenis/model/sequence.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

const char* const kRanges[] = {"master", "highlights", "midtones", "shadows"};
const char* const kWheels[] = {"shadows", "midtones", "highlights", "master"};

// The last part of a path names the control.
QString leaf(const QString& path) {
    return path.section(QLatin1Char('/'), -1);
}

// "hsl/controls/master/hue" -> {"hsl", "hsl/controls", "hsl/controls/master", "hsl/controls/master/hue"}.
QStringList ancestorsAndSelf(const QString& path) {
    QStringList out;
    const QStringList parts = path.split(QLatin1Char('/'));
    for (qsizetype i = 1; i <= parts.size(); ++i) out << parts.mid(0, i).join(QLatin1Char('/'));
    return out;
}

bool under(const QString& path, const QString& top) {
    return path == top || path.startsWith(top + QLatin1Char('/'));
}

// The Hue Offsets tab's Gain, Gamma and Setup are the Controls tab's Master ones (p.603).
QString canonical(const QString& path) {
    for (const char* c : {"gain", "gamma", "setup"}) {
        if (path == QStringLiteral("hsl/hueOffsets/") + QLatin1String(c)) return QStringLiteral("hsl/controls/master/") + QLatin1String(c);
    }
    return path;
}

} // namespace

QJsonObject ColorCorrection::toJson() const {
    QJsonObject o;
    if (!values.isEmpty()) o.insert(QStringLiteral("values"), values);
    if (!disabled.isEmpty()) o.insert(QStringLiteral("disabled"), QJsonArray::fromStringList(disabled));
    return o;
}

ColorCorrection ColorCorrection::fromJson(const QJsonObject& o) {
    ColorCorrection c;
    c.values = o.value(QStringLiteral("values")).toObject();
    for (const QJsonValue v : o.value(QStringLiteral("disabled")).toArray()) c.disabled << v.toString();
    return c;
}

double ColorCorrection::number(const QString& path) const {
    const QString p = canonical(path);
    return values.value(p).toDouble(colorControlDefault(p).value_or(0.0));
}

bool ColorCorrection::applies(const QString& path) const {
    const QString p = canonical(path);
    if (!values.contains(p)) return false;
    for (const QString& a : ancestorsAndSelf(p)) {
        if (disabled.contains(a)) return false;
    }
    // The Hue Offsets' Gain, Gamma and Setup stay on while either tab is (p.603): only their own buttons and the HSL
    // tab's turn them off.
    return true;
}

void ColorCorrection::set(const QString& path, const QJsonValue& value) {
    const QString p = canonical(path);
    QJsonValue v = value;
    if (value.isDouble()) {
        if (const auto range = colorControlRange(p)) v = std::clamp(value.toDouble(), range->first, range->second);
    }
    values.insert(p, v);
    for (const QString& a : ancestorsAndSelf(p)) disabled.removeAll(a);
    if (p != path) {
        // Adjusted from the Hue Offsets tab: that tab's button comes on too.
        for (const QString& a : ancestorsAndSelf(path)) disabled.removeAll(a);
    }
}

void ColorCorrection::setEnabled(const QString& path, bool on) {
    const QString p = canonical(path);
    if (on) {
        disabled.removeAll(p);
    } else if (!disabled.contains(p)) {
        disabled << p;
    }
}

bool ColorCorrection::enabled(const QString& path) const {
    return !disabled.contains(canonical(path));
}

void ColorCorrection::reset(const QString& path) {
    const QString p = canonical(path);
    for (const QString& key : values.keys()) {
        if (under(key, p)) values.remove(key);
    }
    disabled.removeIf([&](const QString& d) { return under(d, p); });
}

std::optional<double> colorControlDefault(const QString& path) {
    const QString name = leaf(path);
    if (path.startsWith(QLatin1String("hsl/hueOffsets/")) && (name == QLatin1String("hue") || name == QLatin1String("amount"))) return 0.0;
    if (name == QLatin1String("hue") || name == QLatin1String("brightness") || name == QLatin1String("contrast") ||
        name == QLatin1String("setup"))
        return 0.0;
    if (name == QLatin1String("saturation") || name == QLatin1String("gain")) return 100.0;
    if (name == QLatin1String("gamma")) return 1.0;
    if (name == QLatin1String("clipLow")) return 16.0;
    if (name == QLatin1String("clipHigh")) return 235.0;
    if (name == QLatin1String("invertChroma") || name == QLatin1String("invertLuma")) return 0.0;
    return std::nullopt;
}

std::optional<std::pair<double, double>> colorControlRange(const QString& path) {
    const QString name = leaf(path);
    if (name == QLatin1String("hue")) return std::pair{-180.0, 180.0};
    if (name == QLatin1String("amount")) return std::pair{0.0, 100.0};
    if (name == QLatin1String("saturation") || name == QLatin1String("gain")) return std::pair{0.0, 200.0};
    if (name == QLatin1String("brightness") || name == QLatin1String("contrast")) return std::pair{-100.0, 100.0};
    if (name == QLatin1String("gamma")) return std::pair{0.1, 10.0};
    if (name == QLatin1String("setup")) return std::pair{-255.0, 255.0};
    if (name == QLatin1String("clipLow") || name == QLatin1String("clipHigh")) return std::pair{0.0, 255.0};
    if (name == QLatin1String("invertChroma") || name == QLatin1String("invertLuma")) return std::pair{0.0, 1.0};
    return std::nullopt;
}

QStringList hslControlPaths() {
    QStringList out;
    for (const char* r : kRanges) {
        const QString base = QStringLiteral("hsl/controls/") + QLatin1String(r) + QLatin1Char('/');
        if (QLatin1String(r) == QLatin1String("master")) out << base + QStringLiteral("invertChroma");
        for (const char* c : {"hue", "saturation", "brightness", "contrast"}) out << base + QLatin1String(c);
        if (QLatin1String(r) == QLatin1String("master")) out << base + QStringLiteral("invertLuma");
        for (const char* c : {"gain", "gamma", "setup"}) out << base + QLatin1String(c);
        if (QLatin1String(r) == QLatin1String("master")) out << base + QStringLiteral("clipLow") << base + QStringLiteral("clipHigh");
    }
    for (const char* w : kWheels) {
        const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(w) + QLatin1Char('/');
        out << base + QStringLiteral("hue") << base + QStringLiteral("amount");
    }
    return out;
}

QString lumaRangeKey(LumaRange range) {
    switch (range) {
    case LumaRange::Shadows: return QStringLiteral("shadows");
    case LumaRange::Midtones: return QStringLiteral("midtones");
    case LumaRange::Highlights: return QStringLiteral("highlights");
    }
    return {};
}

QList<QPointF> defaultLumaRangeCurve(LumaRange range) {
    // Highlights: a smooth step centred on 175 that reaches 95% at 215 (p.642), from 119 to 231; shadows the mirror
    // image about the middle; midtones what is left.
    auto step = [](double x, double a, double b) {
        const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    };
    QList<QPointF> curve;
    for (int x = 0; x <= 255; x += 15) {
        const double xs = std::min(x, 255);
        const double h = step(xs, 119.0, 231.0);
        const double s = 1.0 - step(xs, 24.0, 136.0);
        const double v = range == LumaRange::Highlights ? h : range == LumaRange::Shadows ? s : std::max(0.0, 1.0 - h - s);
        curve << QPointF(xs, std::round(v * 1000.0) / 10.0);
    }
    return curve;
}

QList<QPointF> lumaRangeCurve(const ColorCorrection& c, LumaRange range) {
    const QString path = QStringLiteral("hsl/lumaRanges/") + lumaRangeKey(range);
    if (c.values.contains(path) && c.applies(path)) {
        QList<QPointF> curve;
        for (const QJsonValue v : c.values.value(path).toArray()) {
            const QJsonArray p = v.toArray();
            curve << QPointF(p.at(0).toDouble(), p.at(1).toDouble());
        }
        std::sort(curve.begin(), curve.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        if (curve.size() >= 2) return curve;
    }
    return defaultLumaRangeCurve(range);
}

std::array<double, 3> lumaRangeWeights(const ColorCorrection& c, double brightness) {
    std::array<double, 3> w{};
    for (int r = 0; r < 3; ++r) {
        const QList<QPointF> curve = lumaRangeCurve(c, static_cast<LumaRange>(r));
        double v = curve.constFirst().y();
        if (brightness >= curve.constLast().x()) {
            v = curve.constLast().y();
        } else {
            for (qsizetype i = 1; i < curve.size(); ++i) {
                if (brightness > curve.at(i).x()) continue;
                const QPointF a = curve.at(i - 1), b = curve.at(i);
                const double t = b.x() > a.x() ? (brightness - a.x()) / (b.x() - a.x()) : 0.0;
                v = a.y() + (b.y() - a.y()) * std::clamp(t, 0.0, 1.0);
                break;
            }
        }
        w[static_cast<std::size_t>(r)] = std::max(0.0, v);
    }
    const double sum = w[0] + w[1] + w[2];
    if (sum <= 0.0) return {0.0, 1.0, 0.0};
    for (double& x : w) x /= sum;
    return w;
}

bool isColorCorrectionEffect(const Effect& effect) {
    return effect.category == QLatin1String("Image") && effect.name == QLatin1String("Color Correction");
}

ColorCorrection colorCorrectionOf(const Effect& effect) {
    return ColorCorrection::fromJson(effect.parameters.value(kColorCorrectionCategory).toObject());
}

void setColorCorrection(Effect& effect, const ColorCorrection& correction) {
    if (correction.isEmpty()) effect.parameters.remove(kColorCorrectionCategory);
    else effect.parameters.insert(kColorCorrectionCategory, correction.toJson());
}

} // namespace lumenis::model
