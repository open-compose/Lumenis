// Colour corrections' controls (color_correction.h).

#include "lumenis/model/color_correction.h"

#include "lumenis/model/sequence.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <vector>

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

// The Hue Offsets tab's Gain, Gamma and Setup are the Controls tab's Master ones (p.603), and so are the Curves tab's
// Master Saturation, Gain, Gamma and Setup (p.659).
QString canonical(const QString& path) {
    for (const char* c : {"gain", "gamma", "setup"}) {
        if (path == QStringLiteral("hsl/hueOffsets/") + QLatin1String(c)) return QStringLiteral("hsl/controls/master/") + QLatin1String(c);
    }
    static const std::pair<const char*, const char*> curves[] = {
        {"curves/masterSaturation", "saturation"}, {"curves/masterGain", "gain"}, {"curves/masterGamma", "gamma"}, {"curves/masterSetup", "setup"}};
    for (const auto& [alias, name] : curves) {
        if (path == QLatin1String(alias)) return QStringLiteral("hsl/controls/master/") + QLatin1String(name);
    }
    return path;
}

// "channels/red/2/amount" -> 2; 0 when the path is not a channel component's.
int componentOf(const QString& path) {
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() != 4 || parts.at(0) != QLatin1String("channels")) return 0;
    return parts.at(2).toInt();
}

// [[in, out], ...] as points, sorted by input.
QList<QPointF> pointsOf(const QJsonValue& value) {
    QList<QPointF> curve;
    for (const QJsonValue v : value.toArray()) {
        const QJsonArray p = v.toArray();
        curve << QPointF(p.at(0).toDouble(), p.at(1).toDouble());
    }
    std::stable_sort(curve.begin(), curve.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    return curve;
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

QString ColorCorrection::text(const QString& path) const {
    const QString p = canonical(path);
    return values.value(p).toString(colorControlTextDefault(p));
}

bool ColorCorrection::adjusted(const QString& path) const {
    const QString p = canonical(path);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (under(it.key(), p) && applies(it.key())) return true;
    }
    return false;
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
    if (const int n = componentOf(path)) {
        if (name == QLatin1String("amount")) return n == 1 ? 100.0 : 0.0;
        if (name == QLatin1String("invert")) return 0.0;
        return std::nullopt;
    }
    if (path.startsWith(QLatin1String("levels/"))) {
        if (name.endsWith(QLatin1String("Black"))) return 16.0;
        if (name.endsWith(QLatin1String("Gray"))) return 128.0;
        if (name.endsWith(QLatin1String("White"))) return 235.0;
        return std::nullopt;
    }
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
    if (componentOf(path)) {
        if (name == QLatin1String("amount")) return std::pair{-200.0, 200.0}; // "Values range from –200% to 200%" (p.646)
        if (name == QLatin1String("invert")) return std::pair{0.0, 1.0};
        return std::nullopt;
    }
    if (path.startsWith(QLatin1String("levels/"))) return std::pair{0.0, 255.0}; // "on a scale from 0 to 255" (p.657)
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

QString colorControlTextDefault(const QString& path) {
    const int n = componentOf(path);
    if (n == 0 || leaf(path) != QLatin1String("type")) return {};
    return n == kChannelComponents ? QStringLiteral("offset") : path.section(QLatin1Char('/'), 1, 1);
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

QStringList colorCorrectionGroups() {
    return {QStringLiteral("hsl"), QStringLiteral("channels"), QStringLiteral("levels"), QStringLiteral("curves"), QStringLiteral("secondary")};
}

QStringList colorChannels() {
    return {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue")};
}

QStringList channelComponentTypes() {
    return {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("luma"),
            QStringLiteral("cr"),  QStringLiteral("cb"),    QStringLiteral("offset")};
}

QString channelComponentPath(const QString& channel, int component, const QString& control) {
    return QStringLiteral("channels/%1/%2/%3").arg(channel).arg(component).arg(control);
}

QStringList levelsTabs() {
    return {QStringLiteral("composite"), QStringLiteral("luma"), QStringLiteral("red"),
            QStringLiteral("green"),     QStringLiteral("blue"), QStringLiteral("master")};
}

QStringList levelsPoints(const QString& tab) {
    if (tab == QLatin1String("composite") || tab == QLatin1String("luma"))
        return {QStringLiteral("inBlack"), QStringLiteral("inWhite"), QStringLiteral("outBlack"), QStringLiteral("outWhite")};
    return {QStringLiteral("inBlack"),  QStringLiteral("inGray"),  QStringLiteral("inWhite"),
            QStringLiteral("outBlack"), QStringLiteral("outGray"), QStringLiteral("outWhite")};
}

double levelsValue(const ColorCorrection& c, const QString& tab, double value) {
    const QString base = QStringLiteral("levels/") + tab + QLatin1Char('/');
    auto point = [&](const char* name) { return c.number(base + QLatin1String(name)); };
    const double inBlack = point("inBlack"), inWhite = point("inWhite");
    const double outBlack = point("outBlack"), outWhite = point("outWhite");
    if (value <= inBlack) return outBlack;
    if (value >= inWhite) return outWhite; // also when the white point is at or below the black point
    const double x = (value - inBlack) / (inWhite - inBlack);
    double y = x;
    if (levelsPoints(tab).contains(QStringLiteral("inGray"))) {
        // The gray points as fractions of the black-to-white range: the curve y = x^k passes through both.
        const double g = std::clamp((point("inGray") - inBlack) / (inWhite - inBlack), 0.01, 0.99);
        const double h = outWhite != outBlack ? std::clamp((point("outGray") - outBlack) / (outWhite - outBlack), 0.01, 0.99) : 0.5;
        y = std::pow(x, std::log(h) / std::log(g));
    }
    return outBlack + (outWhite - outBlack) * y;
}

QStringList curveGraphs() {
    return {QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("master")};
}

QList<QPointF> colorCurve(const ColorCorrection& c, const QString& graph) {
    const QString path = QStringLiteral("curves/") + graph;
    const QList<QPointF> curve = pointsOf(c.values.value(path));
    if (curve.size() >= 2) return curve;
    return {QPointF(0, 0), QPointF(255, 255)};
}

void setColorCurve(ColorCorrection& c, const QString& graph, QList<QPointF> points) {
    const QString path = QStringLiteral("curves/") + graph;
    for (QPointF& p : points) p = QPointF(std::clamp(p.x(), 0.0, 255.0), std::clamp(p.y(), 0.0, 255.0));
    std::stable_sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    points = points.mid(0, kMaxCurvePoints);
    if (points.size() < 2) {
        c.reset(path);
        return;
    }
    QJsonArray array;
    for (const QPointF& p : points) array.append(QJsonArray{p.x(), p.y()});
    c.set(path, array);
}

double curveValue(const QList<QPointF>& points, double x) {
    if (points.isEmpty()) return x;
    if (x <= points.constFirst().x()) return points.constFirst().y();
    if (x >= points.constLast().x()) return points.constLast().y();
    // The secants between neighbours (points at the same input have none: the curve steps there) and the tangents at
    // each point, flattened where the curve turns or would overshoot (Fritsch-Carlson).
    const qsizetype n = points.size();
    std::vector<double> secant(static_cast<std::size_t>(n - 1));
    for (qsizetype i = 0; i + 1 < n; ++i) {
        const double dx = points.at(i + 1).x() - points.at(i).x();
        secant[static_cast<std::size_t>(i)] = dx > 0.0 ? (points.at(i + 1).y() - points.at(i).y()) / dx : 0.0;
    }
    auto tangent = [&](qsizetype i) {
        if (i == 0) return secant.front();
        if (i == n - 1) return secant.back();
        const double a = secant[static_cast<std::size_t>(i - 1)], b = secant[static_cast<std::size_t>(i)];
        if (a * b <= 0.0) return 0.0;
        return 2.0 / (1.0 / a + 1.0 / b); // the harmonic mean keeps the curve monotone between monotone points
    };
    qsizetype i = 1;
    while (i < n - 1 && points.at(i).x() < x) ++i;
    const QPointF a = points.at(i - 1), b = points.at(i);
    const double h = b.x() - a.x();
    if (h <= 0.0) return b.y();
    const double t = (x - a.x()) / h;
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * a.y() + (t3 - 2 * t2 + t) * h * tangent(i - 1) + (-2 * t3 + 3 * t2) * b.y() +
           (t3 - t2) * h * tangent(i);
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
        const QList<QPointF> curve = pointsOf(c.values.value(path));
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
