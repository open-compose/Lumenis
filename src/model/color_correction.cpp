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

// The vectors' predefined input values (ours, but for the standard vectors' 30 degrees of hue, p.671, custom vector 3's
// saturation 35 and width 40, p.671, and the Softness of 30, p.676). Hue: "0 ... represents red and –180 or 180 ...
// cyan" (p.675), green +120 and blue -120 as on the ChromaWheels (p.636).
struct VectorDefault {
    const char* name;
    double hue, saturation;
};
const VectorDefault kVectors[] = {
    {"green", 120, 60}, {"yellow", 60, 60}, {"red", 0, 60}, {"magenta", -60, 60}, {"blue", -120, 60}, {"cyan", 180, 60},
    {"1", 25, 35},      {"2", 100, 35},     {"3", -150, 35}, {"4", 45, 50},      {"5", -90, 35},   {"6", 150, 35},
};
constexpr double kStandardSatMin = 10;

const VectorDefault* vectorDefault(const QString& name) {
    for (const VectorDefault& v : kVectors) {
        if (name == QLatin1String(v.name)) return &v;
    }
    return nullptr;
}

// "secondary/vectors/red/input/hue" -> {"red", "input"}; empty when the path is not a vector control's.
std::pair<QString, QString> vectorOf(const QString& path) {
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() != 5 || parts.at(0) != QLatin1String("secondary") || parts.at(1) != QLatin1String("vectors")) return {};
    return {parts.at(2), parts.at(3)};
}

// a - b in degrees, wrapped to -180..180.
double hueDifference(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

double wrapHue(double h) {
    return hueDifference(h, 0.0);
}

constexpr double kPi = 3.14159265358979323846;
double radians(double degrees) {
    return degrees * kPi / 180.0;
}

// How far a colour lies outside a vector's region, in the vector's own units: below 0 inside, 0 on the edge, 1 a
// vector's size beyond it (ours). Wedges count 90 degrees of hue or 50 of saturation as their size.
double vectorDistance(const SecondaryVector& v, double hue, double saturation) {
    const double dh = hueDifference(hue, v.hue);
    if (v.standard) return std::max((std::abs(dh) - v.hueWidth / 2.0) / 90.0, (v.satMin - saturation) / 50.0);
    // The ellipse about the centre point: satWidth across the radius, hueWidth degrees along it as seen from the
    // wheel's centre, so its hue range narrows as saturation grows.
    const double radial = saturation * std::cos(radians(dh)) - v.saturation;
    const double along = saturation * std::sin(radians(dh));
    const double ar = std::max(0.5, v.satWidth / 2.0), at = std::max(0.5, v.saturation * std::tan(radians(std::min(v.hueWidth, 179.0) / 2.0)));
    return std::hypot(radial / ar, along / at) - 1.0;
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
    if (!values.contains(p) && p.startsWith(QLatin1String("secondary/vectors/")) && p.contains(QLatin1String("/output/"))) {
        // A vector's output hue and saturation are its input's until changed.
        const QString name = leaf(p);
        if (name == QLatin1String("hue") || name == QLatin1String("saturation"))
            return number(QString(p).replace(QLatin1String("/output/"), QLatin1String("/input/")));
    }
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
    if (const auto [vector, side] = vectorOf(path); !vector.isEmpty()) {
        const VectorDefault* v = vectorDefault(vector);
        if (!v) return std::nullopt;
        if (side == QLatin1String("output")) return name == QLatin1String("luminance") ? std::optional<double>(0.0) : std::nullopt;
        if (name == QLatin1String("hue")) return v->hue;
        if (name == QLatin1String("saturation")) return v->saturation;
        if (name == QLatin1String("hueWidth")) return 30.0;
        if (name == QLatin1String("satMin")) return kStandardSatMin;
        if (name == QLatin1String("satWidth")) return 40.0;
        if (name == QLatin1String("softness")) return 30.0;
        return std::nullopt;
    }
    if (path == QLatin1String("secondary/isolate") || path == QLatin1String("secondary/invertLast")) return 0.0;
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
    if (const auto [vector, side] = vectorOf(path); !vector.isEmpty()) {
        // p.675-676 and p.681.
        if (name == QLatin1String("hue")) return std::pair{-180.0, 180.0};
        if (name == QLatin1String("saturation") || name == QLatin1String("satMin") || name == QLatin1String("satWidth"))
            return std::pair{0.0, 100.0};
        if (name == QLatin1String("hueWidth")) return std::pair{0.0, isStandardVector(vector) ? 180.0 : 100.0};
        if (name == QLatin1String("softness")) return std::pair{-255.0, 255.0};
        if (name == QLatin1String("luminance")) return std::pair{-10.0, 10.0};
        return std::nullopt;
    }
    if (path == QLatin1String("secondary/isolate") || path == QLatin1String("secondary/invertLast")) return std::pair{0.0, 1.0};
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
    if (path == QLatin1String("secondary/selected")) return QStringLiteral("green"); // p.674's illustration
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

QStringList secondaryVectors() {
    QStringList out;
    for (const VectorDefault& v : kVectors) out << QLatin1String(v.name);
    return out;
}

bool isStandardVector(const QString& vector) {
    return secondaryVectors().indexOf(vector) < 6;
}

QString vectorPath(const QString& vector, const QString& side, const QString& control) {
    return QStringLiteral("secondary/vectors/%1/%2/%3").arg(vector, side, control);
}

SecondaryVector secondaryVector(const ColorCorrection& c, const QString& vector) {
    auto in = [&](const char* name) { return c.number(vectorPath(vector, QStringLiteral("input"), QLatin1String(name))); };
    auto out = [&](const char* name) { return c.number(vectorPath(vector, QStringLiteral("output"), QLatin1String(name))); };
    SecondaryVector v;
    v.standard = isStandardVector(vector);
    v.hue = in("hue");
    v.saturation = in("saturation");
    v.hueWidth = in("hueWidth");
    v.satMin = in("satMin");
    v.satWidth = in("satWidth");
    v.softness = in("softness");
    v.outHue = out("hue");
    v.outSaturation = out("saturation");
    v.outLuminance = out("luminance");
    return v;
}

QStringList enabledVectors(const ColorCorrection& c) {
    QStringList out;
    for (const QString& v : secondaryVectors()) {
        if (c.adjusted(QStringLiteral("secondary/vectors/") + v)) out << v;
    }
    return out;
}

double vectorMembership(const SecondaryVector& v, double hue, double saturation) {
    const double dist = vectorDistance(v, hue, saturation);
    const double margin = std::abs(v.softness) / 255.0;
    if (margin <= 0.0) return dist <= 0.0 ? 1.0 : 0.0;
    // "Positive values blend the corrected color into areas of the image beyond the limits of the defined input vector
    // ... Negative values blend the uncorrected color into parts of the area defined by the input vector" (p.676).
    if (v.softness > 0.0) return dist <= 0.0 ? 1.0 : std::clamp(1.0 - dist / margin, 0.0, 1.0);
    return dist >= 0.0 ? 0.0 : std::clamp(-dist / margin, 0.0, 1.0);
}

void setVectorInput(ColorCorrection& c, const QString& vector, const QString& control, double value, bool lockVectors) {
    const QString path = vectorPath(vector, QStringLiteral("input"), control);
    const bool hue = control == QLatin1String("hue");
    if (hue) value = wrapHue(value);
    if (lockVectors && (hue || control == QLatin1String("saturation"))) {
        // "any changes you make to the Input Hue and Saturation values ... are matched by equivalent changes to the
        // Output Hue and Saturation values" (p.676).
        const QString outPath = vectorPath(vector, QStringLiteral("output"), control);
        const double old = c.number(path), out = c.number(outPath);
        const double delta = hue ? hueDifference(value, old) : value - old;
        c.set(outPath, hue ? wrapHue(out + delta) : out + delta);
    }
    c.set(path, value);
}

QString syringeVector(ColorCorrection& c, const QList<QPointF>& samples) {
    if (samples.isEmpty()) return {};
    // The samples' centre in the wheel's plane, then their hues about it and their saturations.
    double x = 0, y = 0;
    for (const QPointF& p : samples) {
        x += p.y() * std::cos(radians(p.x()));
        y += p.y() * std::sin(radians(p.x()));
    }
    x /= static_cast<double>(samples.size());
    y /= static_cast<double>(samples.size());
    const double centreHue = std::atan2(y, x) * 180.0 / kPi;
    double lo = 0, hi = 0, satLo = samples.constFirst().y(), satHi = satLo;
    for (const QPointF& p : samples) {
        const double d = hueDifference(p.x(), centreHue);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
        satLo = std::min(satLo, p.y());
        satHi = std::max(satHi, p.y());
    }
    // "Selects and enables the closest vector to the range of colors you have sampled" (p.678): the predefined centre
    // nearest to the samples' centre (ours).
    QString best;
    double bestDistance = 0;
    for (const VectorDefault& v : kVectors) {
        const double d = std::hypot(v.saturation * std::cos(radians(v.hue)) - x, v.saturation * std::sin(radians(v.hue)) - y);
        if (best.isEmpty() || d < bestDistance) {
            best = QLatin1String(v.name);
            bestDistance = d;
        }
    }
    c.reset(QStringLiteral("secondary/vectors/") + best);
    // "Adjusts the Input Vector values for the vector so that it includes the full range of colors" (p.678), with a
    // little room (ours).
    const double hue = wrapHue(centreHue + (lo + hi) / 2.0), span = hi - lo + 2.0;
    auto in = [&](const char* name, double value) { c.set(vectorPath(best, QStringLiteral("input"), QLatin1String(name)), value); };
    in("hue", hue);
    in("saturation", (satLo + satHi) / 2.0);
    if (isStandardVector(best)) {
        in("hueWidth", std::min(180.0, span));
        in("satMin", std::max(0.0, satLo - 2.0));
    } else {
        in("hueWidth", std::min(100.0, span));
        in("satWidth", std::min(100.0, satHi - satLo + 4.0));
    }
    // "Sets the Output Vector values to match the Input Vector values" and "Automatically selects Isolate".
    c.set(vectorPath(best, QStringLiteral("output"), QStringLiteral("hue")), c.number(vectorPath(best, QStringLiteral("input"), QStringLiteral("hue"))));
    c.set(vectorPath(best, QStringLiteral("output"), QStringLiteral("saturation")),
          c.number(vectorPath(best, QStringLiteral("input"), QStringLiteral("saturation"))));
    c.set(QStringLiteral("secondary/isolate"), 1);
    c.set(QStringLiteral("secondary/selected"), best);
    return best;
}

namespace {

// A vector's hue range as offsets from its hue, and its saturation range (custom vectors).
void setHueRange(ColorCorrection& c, const QString& vector, double lo, double hi) {
    const SecondaryVector v = secondaryVector(c, vector);
    const double limit = v.standard ? 180.0 : 100.0;
    const double width = std::clamp(hi - lo, 0.0, limit);
    setVectorInput(c, vector, QStringLiteral("hue"), v.hue + (lo + hi) / 2.0, false);
    c.set(vectorPath(vector, QStringLiteral("input"), QStringLiteral("hueWidth")), width);
}

void setSaturationRange(ColorCorrection& c, const QString& vector, double lo, double hi) {
    c.set(vectorPath(vector, QStringLiteral("input"), QStringLiteral("saturation")), (lo + hi) / 2.0);
    c.set(vectorPath(vector, QStringLiteral("input"), QStringLiteral("satWidth")), std::max(0.0, hi - lo));
}

} // namespace

void addToVector(ColorCorrection& c, const QString& vector, QPointF sample) {
    const SecondaryVector v = secondaryVector(c, vector);
    const double dh = hueDifference(sample.x(), v.hue), s = sample.y();
    // Grown within the vector's shape (p.679): its hue range to the colour's hue, then its saturation range.
    if (std::abs(dh) > v.hueWidth / 2.0)
        setHueRange(c, vector, std::min(-v.hueWidth / 2.0, dh - 1.0), std::max(v.hueWidth / 2.0, dh + 1.0));
    if (v.standard) {
        if (s < v.satMin) c.set(vectorPath(vector, QStringLiteral("input"), QStringLiteral("satMin")), std::max(0.0, s - 1.0));
    } else {
        const double lo = v.saturation - v.satWidth / 2.0, hi = v.saturation + v.satWidth / 2.0;
        if (s < lo || s > hi) setSaturationRange(c, vector, std::min(lo, s - 1.0), std::max(hi, s + 1.0));
    }
    // The ellipse may still leave the colour out at its ends: widen until it holds it.
    for (int i = 0; i < 20 && vectorDistance(secondaryVector(c, vector), sample.x(), s) > 0.0; ++i) {
        const SecondaryVector w = secondaryVector(c, vector);
        setHueRange(c, vector, -w.hueWidth / 2.0 - 2.0, w.hueWidth / 2.0 + 2.0);
        setSaturationRange(c, vector, w.saturation - w.satWidth / 2.0 - 1.0, w.saturation + w.satWidth / 2.0 + 1.0);
    }
}

void subtractFromVector(ColorCorrection& c, const QString& vector, QPointF sample) {
    const SecondaryVector v = secondaryVector(c, vector);
    const double dh = hueDifference(sample.x(), v.hue), s = sample.y();
    if (vectorDistance(v, sample.x(), s) > 0.0) return;
    // Shrunk by the smaller change: the hue range's nearer end to just past the colour, or the saturation range's.
    const double hueCost = (v.hueWidth / 2.0 - std::abs(dh)) / 90.0;
    double satCost = 0;
    double satLo = 0, satHi = 0;
    if (v.standard) {
        satCost = (s - v.satMin) / 50.0;
    } else {
        satLo = v.saturation - v.satWidth / 2.0;
        satHi = v.saturation + v.satWidth / 2.0;
        satCost = std::min(s - satLo, satHi - s) / 50.0;
    }
    if (hueCost <= satCost) {
        if (dh >= 0.0) setHueRange(c, vector, -v.hueWidth / 2.0, std::max(-v.hueWidth / 2.0, dh - 1.0));
        else setHueRange(c, vector, std::min(v.hueWidth / 2.0, dh + 1.0), v.hueWidth / 2.0);
    } else if (v.standard) {
        c.set(vectorPath(vector, QStringLiteral("input"), QStringLiteral("satMin")), std::min(100.0, s + 1.0));
    } else if (s - satLo < satHi - s) {
        setSaturationRange(c, vector, std::min(satHi, s + 1.0), satHi);
    } else {
        setSaturationRange(c, vector, satLo, std::max(satLo, s - 1.0));
    }
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

ColorCorrection mergedCorrection(const ColorCorrection& source, const ColorCorrection& program) {
    if (program.isEmpty()) return source;
    if (source.isEmpty()) return program;
    ColorCorrection out = source;
    for (auto it = program.values.constBegin(); it != program.values.constEnd(); ++it) {
        out.values.insert(it.key(), it.value());
        // A Source button above a control the Program side set does not stop it.
        out.disabled.removeIf([&](const QString& d) { return under(it.key(), d); });
    }
    for (const QString& d : program.disabled) {
        if (!out.disabled.contains(d)) out.disabled << d;
    }
    return out;
}

ColorCorrection segmentCorrection(const Segment& segment) {
    return mergedCorrection(segment.sourceCorrection, segment.programCorrection);
}

ColorCorrection& correctionOf(Segment& segment, CorrectionSide side) {
    return side == CorrectionSide::Source ? segment.sourceCorrection : segment.programCorrection;
}

namespace {

// The track's clip segments as (start, index), in order.
QList<std::pair<std::int64_t, qsizetype>> shotsOf(const Track& track) {
    QList<std::pair<std::int64_t, qsizetype>> out;
    std::int64_t at = 0;
    for (qsizetype i = 0; i < track.segments.size(); ++i) {
        if (!track.segments.at(i).isFiller()) out << std::pair{at, i};
        at += track.segments.at(i).length;
    }
    return out;
}

// The shot at `frame`, or where it would fall between shots: the index of the last shot starting at or before it
// (-1 before the first), and whether `frame` is inside that shot.
std::pair<qsizetype, bool> shotAt(const Track& track, const QList<std::pair<std::int64_t, qsizetype>>& shots, std::int64_t frame) {
    qsizetype k = -1;
    for (qsizetype i = 0; i < shots.size(); ++i) {
        if (shots.at(i).first <= frame) k = i;
    }
    const bool inside = k >= 0 && frame < shots.at(k).first + track.segments.at(shots.at(k).second).length;
    return {k, inside};
}

} // namespace

std::optional<std::int64_t> correctionShotStart(const Track& track, std::int64_t frame, int step) {
    const auto shots = shotsOf(track);
    const auto [k, inside] = shotAt(track, shots, frame);
    if (!inside && step == 0) return std::nullopt;
    // In filler, the shot before it counts as -1 and the one after as +1.
    const qsizetype i = !inside && step < 0 ? k + step + 1 : k + step;
    if (i < 0 || i >= shots.size()) return std::nullopt;
    return shots.at(i).first;
}

std::optional<std::int64_t> uncorrectedShotStart(const Track& track, std::int64_t frame, int direction) {
    const auto shots = shotsOf(track);
    const auto [k, inside] = shotAt(track, shots, frame);
    qsizetype i = direction < 0 ? k - (inside ? 1 : 0) : k + 1;
    for (; i >= 0 && i < shots.size(); i += direction < 0 ? -1 : 1) {
        if (!track.segments.at(shots.at(i).second).hasCorrection()) return shots.at(i).first;
    }
    return std::nullopt;
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
