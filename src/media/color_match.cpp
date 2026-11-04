// The Color Match control's corrections (color_match.h; FX Guide p.624-631). The guide says which components each
// Match Type matches; which controls take the change is ours (docs/spec/color-correction.md):
//
// * HSL Controls: Hue turns the range's Hue control by the hue between the two colours, Saturation scales its
//   Saturation by their chroma, Luminance scales its Gain by their brightness above black.
// * HSL Hue Offsets: the chosen wheel moves by the difference of the colours' chroma, over the share of that range the
//   input's brightness has (Master: all of it).
// * Channels: each matched channel's own component amount scales by the output's value over the input's; Master scales
//   all three by the luma.
// * Levels: the Master tab's input point takes the input's brightness and its output point the output's.
// * Curves: a point on each matched curve takes the input's value (followed back through the curve) to the output's.
// * Secondary: the selected vector's output hue and saturation move by the difference of the two colours on the wheel.

#include "lumenis/media/color_match.h"

#include "lumenis/media/color_correction.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace lumenis::media {

namespace {

using model::ColorCorrection;
using Rgb = std::array<double, 3>;

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;

struct Ycc {
    double y = 16, cb = 0, cr = 0; // y on the 8-bit video scale, cb and cr -0.5 to 0.5
};

Rgb rgbOf(const QColor& c) {
    return {static_cast<double>(c.redF()), static_cast<double>(c.greenF()), static_cast<double>(c.blueF())};
}

Ycc toYcc(const Rgb& c) {
    const double y = kR * c[0] + kG * c[1] + kB * c[2];
    return {16.0 + 219.0 * y, (c[2] - y) / (2.0 * (1.0 - kB)), (c[0] - y) / (2.0 * (1.0 - kR))};
}

QColor fromYcc(const Ycc& v) {
    const double y = (v.y - 16.0) / 219.0;
    const double r = y + 2.0 * (1.0 - kR) * v.cr, b = y + 2.0 * (1.0 - kB) * v.cb;
    const double g = (y - kR * r - kB * b) / kG;
    return QColor::fromRgbF(static_cast<float>(std::clamp(r, 0.0, 1.0)), static_cast<float>(std::clamp(g, 0.0, 1.0)),
                            static_cast<float>(std::clamp(b, 0.0, 1.0)));
}

double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

double wrap(double deg) {
    return std::remainder(deg, 360.0);
}

double inverseCurve(const QList<QPointF>& curve, double output) {
    double lo = 0.0, hi = 255.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (model::curveValue(curve, mid) < output) lo = mid;
        else hi = mid;
    }
    return (lo + hi) / 2.0;
}

// Adds a point at `x` taking it to `y`, or moves the point already within 2 of it; nothing past the 16 point limit.
bool addCurvePoint(ColorCorrection& c, const QString& graph, double x, double y) {
    QList<QPointF> curve = model::colorCurve(c, graph);
    bool moved = false;
    for (QPointF& p : curve) {
        if (std::abs(p.x() - x) <= 2.0) {
            p.setY(y);
            moved = true;
            break;
        }
    }
    if (!moved) {
        if (curve.size() >= model::kMaxCurvePoints) return false;
        curve << QPointF(x, y);
    }
    model::setColorCurve(c, graph, curve);
    return true;
}

// The channels a Channels or Curves Match Type names: 0 red, 1 green, 2 blue.
QList<int> channelsOf(const QString& type) {
    if (type == QLatin1String("rgb")) return {0, 1, 2};
    if (type == QLatin1String("red")) return {0};
    if (type == QLatin1String("green")) return {1};
    if (type == QLatin1String("blue")) return {2};
    if (type == QLatin1String("rg")) return {0, 1};
    if (type == QLatin1String("rb")) return {0, 2};
    if (type == QLatin1String("gb")) return {1, 2};
    return {};
}

const char* const kChannels[] = {"red", "green", "blue"};

double luma(const Rgb& c) {
    return kR * c[0] + kG * c[1] + kB * c[2];
}

void matchHslControls(ColorCorrection& c, const Ycc& in, const Ycc& out, const ColorMatch& m) {
    const QString base = QStringLiteral("hsl/controls/") + m.range + QLatin1Char('/');
    const QString& t = m.type;
    const bool hue = t == QLatin1String("hsl") || t == QLatin1String("hue") || t == QLatin1String("hs") || t == QLatin1String("hl");
    const bool sat = t == QLatin1String("hsl") || t == QLatin1String("saturation") || t == QLatin1String("hs") || t == QLatin1String("sl");
    const bool lum = t == QLatin1String("hsl") || t == QLatin1String("luminance") || t == QLatin1String("hl") || t == QLatin1String("sl");
    const double inChroma = std::hypot(in.cb, in.cr), outChroma = std::hypot(out.cb, out.cr);
    if (hue && inChroma > 0.005 && outChroma > 0.005) {
        const double turn = degrees(std::atan2(out.cr, out.cb) - std::atan2(in.cr, in.cb));
        c.set(base + QStringLiteral("hue"), std::round(wrap(c.number(base + QStringLiteral("hue")) + turn) * 100.0) / 100.0);
    }
    if (sat && inChroma > 0.005) {
        c.set(base + QStringLiteral("saturation"), std::round(c.number(base + QStringLiteral("saturation")) * outChroma / inChroma * 100.0) / 100.0);
    }
    if (lum && in.y - 16.0 > 1.0) {
        c.set(base + QStringLiteral("gain"), std::round(c.number(base + QStringLiteral("gain")) * (out.y - 16.0) / (in.y - 16.0) * 100.0) / 100.0);
    }
}

void matchHueOffsets(ColorCorrection& c, const Ycc& in, const Ycc& out, const ColorMatch& m) {
    const QStringList ranges{QStringLiteral("shadows"), QStringLiteral("midtones"), QStringLiteral("highlights")};
    const auto index = ranges.indexOf(m.type);
    const double share = index < 0 ? 1.0 : std::max(0.2, model::lumaRangeWeights(c, std::clamp(in.y, 0.0, 255.0))[static_cast<std::size_t>(index)]);
    const QString base = QStringLiteral("hsl/hueOffsets/") + (index < 0 ? QStringLiteral("master") : m.type) + QLatin1Char('/');
    // The wheel's vector as the picture reads it: Amount 100 a quarter of the chroma range, 0 degrees red.
    const Ycc red = toYcc({1.0, 0.0, 0.0});
    const double redAngle = std::atan2(red.cr, red.cb);
    const double amount = c.number(base + QStringLiteral("amount")) / 100.0 * 0.25;
    const double angle = redAngle + c.number(base + QStringLiteral("hue")) * std::numbers::pi / 180.0;
    const double x = std::cos(angle) * amount + (out.cb - in.cb) / share, y = std::sin(angle) * amount + (out.cr - in.cr) / share;
    c.set(base + QStringLiteral("amount"), std::round(std::min(100.0, std::hypot(x, y) / 0.25 * 100.0) * 100.0) / 100.0);
    c.set(base + QStringLiteral("hue"), std::round(wrap(degrees(std::atan2(y, x) - redAngle)) * 100.0) / 100.0);
}

void matchChannels(ColorCorrection& c, const Rgb& in, const Rgb& out, const QString& type) {
    QList<int> channels = channelsOf(type);
    std::array<double, 3> ratio{};
    if (type == QLatin1String("master")) {
        channels = {0, 1, 2};
        const double r = luma(in) > 0.01 ? luma(out) / luma(in) : 1.0;
        ratio = {r, r, r};
    } else {
        for (int ch : channels) {
            const auto k = static_cast<std::size_t>(ch);
            ratio[k] = in[k] > 0.01 ? out[k] / in[k] : 1.0;
        }
    }
    for (int ch : channels) {
        const QString path = model::channelComponentPath(QLatin1String(kChannels[ch]), 1, QStringLiteral("amount"));
        c.set(path, std::round(c.number(path) * ratio[static_cast<std::size_t>(ch)] * 100.0) / 100.0);
    }
}

void matchLevels(ColorCorrection& c, const Rgb& in, const Rgb& out, const QString& type) {
    const QString point = type == QLatin1String("black") ? QStringLiteral("Black") : type == QLatin1String("gray") ? QStringLiteral("Gray") : QStringLiteral("White");
    c.set(QStringLiteral("levels/master/in") + point, std::round(16.0 + 219.0 * luma(in)));
    c.set(QStringLiteral("levels/master/out") + point, std::round(16.0 + 219.0 * luma(out)));
}

void matchCurves(ColorCorrection& c, const Rgb& in, const Rgb& out, const QString& type) {
    if (type == QLatin1String("master")) {
        const QList<QPointF> master = model::colorCurve(c, QStringLiteral("master"));
        addCurvePoint(c, QStringLiteral("master"), std::round(inverseCurve(master, luma(in) * 255.0)), std::round(luma(out) * 255.0));
        return;
    }
    for (int ch : channelsOf(type)) {
        const QString graph = QLatin1String(kChannels[ch]);
        const auto k = static_cast<std::size_t>(ch);
        addCurvePoint(c, graph, std::round(inverseCurve(model::colorCurve(c, graph), in[k] * 255.0)), std::round(out[k] * 255.0));
    }
}

void matchSecondary(ColorCorrection& c, const Rgb& in, const Rgb& out, const ColorMatch& m) {
    if (m.vector.isEmpty()) return;
    const QPointF a = wheelHueSaturation(in), b = wheelHueSaturation(out);
    const model::SecondaryVector v = model::secondaryVector(c, m.vector);
    if (m.type == QLatin1String("hue") || m.type == QLatin1String("hs"))
        c.set(model::vectorPath(m.vector, QStringLiteral("output"), QStringLiteral("hue")), std::round(wrap(v.outHue + b.x() - a.x()) * 100.0) / 100.0);
    if (m.type == QLatin1String("saturation") || m.type == QLatin1String("hs"))
        c.set(model::vectorPath(m.vector, QStringLiteral("output"), QStringLiteral("saturation")),
              std::round(std::clamp(v.outSaturation + b.y() - a.y(), 0.0, 100.0) * 100.0) / 100.0);
}

} // namespace

QStringList matchTypes(MatchGroup group) {
    switch (group) {
    case MatchGroup::HslControls:
        return {QStringLiteral("hsl"), QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("luminance"),
                QStringLiteral("hs"),  QStringLiteral("hl"),  QStringLiteral("sl")};
    case MatchGroup::HslHueOffsets: return {QStringLiteral("master"), QStringLiteral("highlights"), QStringLiteral("midtones"), QStringLiteral("shadows")};
    case MatchGroup::Channels:
    case MatchGroup::Curves:
        return {QStringLiteral("master"), QStringLiteral("rgb"), QStringLiteral("red"), QStringLiteral("green"),
                QStringLiteral("blue"),   QStringLiteral("rg"),  QStringLiteral("rb"),  QStringLiteral("gb")};
    case MatchGroup::Levels: return {QStringLiteral("black"), QStringLiteral("gray"), QStringLiteral("white")};
    case MatchGroup::Secondary: return {QStringLiteral("hue"), QStringLiteral("saturation"), QStringLiteral("hs")};
    }
    return {};
}

QString matchTypeText(const QString& key, bool natural) {
    static const std::pair<const char*, const char*> texts[] = {
        {"hsl", QT_TRANSLATE_NOOP("ColorMatch", "H+S+L")},         {"hue", QT_TRANSLATE_NOOP("ColorMatch", "Hue")},
        {"saturation", QT_TRANSLATE_NOOP("ColorMatch", "Saturation")}, {"luminance", QT_TRANSLATE_NOOP("ColorMatch", "Luminance")},
        {"hs", QT_TRANSLATE_NOOP("ColorMatch", "H+S")},            {"hl", QT_TRANSLATE_NOOP("ColorMatch", "H+L")},
        {"sl", QT_TRANSLATE_NOOP("ColorMatch", "S+L")},            {"master", QT_TRANSLATE_NOOP("ColorMatch", "Master")},
        {"highlights", QT_TRANSLATE_NOOP("ColorMatch", "Highlights")}, {"midtones", QT_TRANSLATE_NOOP("ColorMatch", "Midtones")},
        {"shadows", QT_TRANSLATE_NOOP("ColorMatch", "Shadows")},   {"rgb", QT_TRANSLATE_NOOP("ColorMatch", "R+G+B")},
        {"red", QT_TRANSLATE_NOOP("ColorMatch", "Red")},           {"green", QT_TRANSLATE_NOOP("ColorMatch", "Green")},
        {"blue", QT_TRANSLATE_NOOP("ColorMatch", "Blue")},         {"rg", QT_TRANSLATE_NOOP("ColorMatch", "R+G")},
        {"rb", QT_TRANSLATE_NOOP("ColorMatch", "R+B")},            {"gb", QT_TRANSLATE_NOOP("ColorMatch", "G+B")},
        {"black", QT_TRANSLATE_NOOP("ColorMatch", "Black Point")}, {"gray", QT_TRANSLATE_NOOP("ColorMatch", "Gray Point")},
        {"white", QT_TRANSLATE_NOOP("ColorMatch", "White Point")}};
    for (const auto& [k, text] : texts) {
        if (key == QLatin1String(k)) {
            const QString shown = QCoreApplication::translate("ColorMatch", text);
            return natural ? shown + QStringLiteral(" (Nat)") : shown;
        }
    }
    return key;
}

bool hasNaturalMatch(MatchGroup group) {
    return group == MatchGroup::Curves || group == MatchGroup::Secondary;
}

QColor naturalMatchColor(const QColor& input, const QColor& output) {
    const Ycc in = toYcc(rgbOf(input)), out = toYcc(rgbOf(output));
    const double scale = out.y - 16.0 > 1.0 ? std::clamp((in.y - 16.0) / (out.y - 16.0), 0.25, 4.0) : 1.0;
    return fromYcc({in.y, out.cb * scale, out.cr * scale});
}

bool matchColor(model::ColorCorrection& correction, const QColor& input, const QColor& output, const ColorMatch& match) {
    if (!input.isValid() || !output.isValid() || !matchTypes(match.group).contains(match.type)) return false;
    const QColor target = match.natural && hasNaturalMatch(match.group) ? naturalMatchColor(input, output) : output;
    const Rgb in = rgbOf(input), out = rgbOf(target);
    const model::ColorCorrection before = correction;
    switch (match.group) {
    case MatchGroup::HslControls: matchHslControls(correction, toYcc(in), toYcc(out), match); break;
    case MatchGroup::HslHueOffsets: matchHueOffsets(correction, toYcc(in), toYcc(out), match); break;
    case MatchGroup::Channels: matchChannels(correction, in, out, match.type); break;
    case MatchGroup::Levels: matchLevels(correction, in, out, match.type); break;
    case MatchGroup::Curves: matchCurves(correction, in, out, match.type); break;
    case MatchGroup::Secondary: matchSecondary(correction, in, out, match); break;
    }
    return correction != before;
}

} // namespace lumenis::media
