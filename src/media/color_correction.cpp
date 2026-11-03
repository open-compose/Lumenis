// Colour correction pictures (color_correction.h). The guide gives each control's range and what it does (p.633-638,
// p.644-667); the formulas are ours and docs/spec/color-correction.md lists them. The HSL group works on Y'CbCr, the
// Channels, Levels and Curves groups on R'G'B', one group after another in the tool's order.

#include "lumenis/media/color_correction.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::media {

namespace {

using model::ColorCorrection;

// Rec. 709 luma coefficients.
constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;

struct Ycc {
    double y = 0, cb = 0, cr = 0; // y in 8-bit video levels (16-235), cb and cr -0.5..0.5 of full range
};

Ycc toYcc(const std::array<double, 3>& rgb) {
    const double y = kR * rgb[0] + kG * rgb[1] + kB * rgb[2];
    return {16.0 + 219.0 * y, (rgb[2] - y) / (2.0 * (1.0 - kB)), (rgb[0] - y) / (2.0 * (1.0 - kR))};
}

std::array<double, 3> toRgb(const Ycc& c) {
    const double y = (c.y - 16.0) / 219.0;
    const double r = y + 2.0 * (1.0 - kR) * c.cr;
    const double b = y + 2.0 * (1.0 - kB) * c.cb;
    const double g = (y - kR * r - kB * b) / kG;
    return {std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0)};
}

// The vectorscope's angle of red in the CbCr plane: the ChromaWheels' 0 degrees, "Red is at the 0° point on the wheel"
// with "green is at +120°" counterclockwise (p.636).
double redAngle() {
    const Ycc red = toYcc({1.0, 0.0, 0.0});
    return std::atan2(red.cr, red.cb);
}

double rad(double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

// One range's Controls tab adjustments (p.633-634) on a pixel, fully: the caller blends by the range's share.
Ycc controls(const ColorCorrection& c, const QString& base, Ycc p) {
    auto applies = [&](const char* name) { return c.applies(base + QLatin1String(name)); };
    auto value = [&](const char* name) { return c.number(base + QLatin1String(name)); };
    if (applies("hue")) {
        // "Shifts the hues in the image around the color wheel": a rotation of the chroma.
        const double a = rad(value("hue"));
        const double cb = p.cb * std::cos(a) - p.cr * std::sin(a), cr = p.cb * std::sin(a) + p.cr * std::cos(a);
        p.cb = cb;
        p.cr = cr;
    }
    if (applies("saturation")) {
        const double s = value("saturation") / 100.0;
        p.cb *= s;
        p.cr *= s;
    }
    if (applies("contrast")) {
        // -100 maps every pixel to neutral grey, 100 is "maximum contrast" (ours: a slope of 20), about mid grey.
        const double v = value("contrast") / 100.0;
        const double k = v <= 0.0 ? 1.0 + v : 1.0 / std::max(0.05, 1.0 - 0.95 * v);
        p.y = 125.5 + (p.y - 125.5) * k;
        p.cb *= std::max(0.0, std::min(1.0, k));
        p.cr *= std::max(0.0, std::min(1.0, k));
    }
    if (applies("brightness")) p.y += value("brightness"); // "adds 100 to the luminance value of every pixel"
    if (applies("gain") || applies("gamma") || applies("setup")) {
        // Gain "based on a percentage of the original luminance", Gamma on the midtones (raising it lightens them),
        // Setup the black point in 8-bit steps; on the 16-235 range.
        double l = (p.y - 16.0) / 219.0;
        if (applies("gain")) l *= value("gain") / 100.0;
        if (applies("gamma") && l > 0.0) l = std::pow(l, 1.0 / std::max(0.1, value("gamma")));
        if (applies("setup")) l += value("setup") / 219.0;
        p.y = 16.0 + 219.0 * l;
    }
    return p;
}

Ycc blend(const Ycc& a, const Ycc& b, double t) {
    return {a.y + (b.y - a.y) * t, a.cb + (b.cb - a.cb) * t, a.cr + (b.cr - a.cr) * t};
}

// The HSL group (p.631-644).
std::array<double, 3> hslPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    Ycc p = toYcc(rgb);
    const QString master = QStringLiteral("hsl/controls/master/");
    // Invert Chroma, "the equivalent of setting the Hue control to 180", and Invert Luma (p.634).
    if (c.applies(master + QStringLiteral("invertChroma")) && c.number(master + QStringLiteral("invertChroma")) > 0.5) {
        p.cb = -p.cb;
        p.cr = -p.cr;
    }
    if (c.applies(master + QStringLiteral("invertLuma")) && c.number(master + QStringLiteral("invertLuma")) > 0.5) p.y = 251.0 - p.y;
    // The ranges' shares of this pixel, by its brightness before the corrections (ours).
    const std::array<double, 3> w = model::lumaRangeWeights(c, std::clamp(p.y, 0.0, 255.0));
    // Master, then Shadows, Midtones and Highlights.
    p = controls(c, master, p);
    const char* const ranges[] = {"shadows", "midtones", "highlights"};
    for (int r = 0; r < 3; ++r) {
        const QString base = QStringLiteral("hsl/controls/") + QLatin1String(ranges[r]) + QLatin1Char('/');
        p = blend(p, controls(c, base, p), w[static_cast<std::size_t>(r)]);
    }
    // The ChromaWheels: an offset of hue and amount in the chroma plane (p.636-638), each wheel by its range's share,
    // Master over all. Ours: Amount 100 is a quarter of the chroma range.
    const char* const wheels[] = {"shadows", "midtones", "highlights", "master"};
    for (int i = 0; i < 4; ++i) {
        const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(wheels[i]) + QLatin1Char('/');
        if (!c.applies(base + QStringLiteral("hue")) && !c.applies(base + QStringLiteral("amount"))) continue;
        const double amount = c.number(base + QStringLiteral("amount")) / 100.0 * 0.25;
        const double angle = redAngle() + rad(c.number(base + QStringLiteral("hue")));
        const double share = i == 3 ? 1.0 : w[static_cast<std::size_t>(i)];
        p.cb += std::cos(angle) * amount * share;
        p.cr += std::sin(angle) * amount * share;
    }
    // Clip Low and Clip High: "All pixels with the Low clip value or less are clipped to black" (p.634).
    const bool clipLow = c.applies(master + QStringLiteral("clipLow")), clipHigh = c.applies(master + QStringLiteral("clipHigh"));
    if (clipLow && p.y <= c.number(master + QStringLiteral("clipLow"))) p = {16.0, 0.0, 0.0};
    if (clipHigh && p.y >= c.number(master + QStringLiteral("clipHigh"))) p = {235.0, 0.0, 0.0};
    return toRgb(p);
}

// The Channels group (p.644-648): each output channel the sum of its four components' percentages ("The total of the
// percentages you set for a channel does not need to be 100", p.647). Components are on 0-1: R, G and B as they are,
// Luma Y' (Rec. 709), Cr and Cb centred on 0.5 so that neutral is mid grey, as in their monochrome previews, and Offset
// 1 (ours).
std::array<double, 3> channelsPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    const double y = kR * rgb[0] + kG * rgb[1] + kB * rgb[2];
    const Ycc ycc = toYcc(rgb);
    auto component = [&](const QString& type) {
        if (type == QLatin1String("red")) return rgb[0];
        if (type == QLatin1String("green")) return rgb[1];
        if (type == QLatin1String("blue")) return rgb[2];
        if (type == QLatin1String("luma")) return y;
        if (type == QLatin1String("cr")) return 0.5 + ycc.cr;
        if (type == QLatin1String("cb")) return 0.5 + ycc.cb;
        if (type == QLatin1String("offset")) return 1.0;
        return 0.0;
    };
    std::array<double, 3> out = rgb;
    const QStringList channels = model::colorChannels();
    for (int ch = 0; ch < 3; ++ch) {
        const QString& channel = channels.at(ch);
        if (!c.adjusted(QStringLiteral("channels/") + channel)) continue;
        double v = 0.0;
        for (int n = 1; n <= model::kChannelComponents; ++n) {
            auto path = [&](const char* control) { return model::channelComponentPath(channel, n, QLatin1String(control)); };
            // An unset or turned-off component keeps its default: the formula is the channel's whole row.
            const double amount = c.applies(path("amount")) ? c.number(path("amount")) : model::colorControlDefault(path("amount")).value_or(0.0);
            const QString type = c.applies(path("type")) ? c.text(path("type")) : model::colorControlTextDefault(path("type"));
            // Invert "subtracts the component from the overall formula instead of adding it" (p.647).
            const bool invert = c.applies(path("invert")) && c.number(path("invert")) > 0.5;
            v += (invert ? -1.0 : 1.0) * amount / 100.0 * component(type);
        }
        out[static_cast<std::size_t>(ch)] = std::clamp(v, 0.0, 1.0);
    }
    return out;
}

// The Levels group (p.648-659), each tab that is adjusted in turn: Master on all three channels, then Red, Green and
// Blue, then Luma on Y' and Composite last (ours). Channels are on the 8-bit video scale, 0-1 as 16-235.
std::array<double, 3> levelsPixel(const ColorCorrection& c, std::array<double, 3> rgb) {
    auto level = [&](const QString& tab, double v) { return (model::levelsValue(c, tab, 16.0 + 219.0 * v) - 16.0) / 219.0; };
    if (c.adjusted(QStringLiteral("levels/master"))) {
        for (double& v : rgb) v = level(QStringLiteral("master"), v);
    }
    const QStringList channels = model::colorChannels();
    for (int ch = 0; ch < 3; ++ch) {
        if (c.adjusted(QStringLiteral("levels/") + channels.at(ch))) rgb[static_cast<std::size_t>(ch)] = level(channels.at(ch), rgb[static_cast<std::size_t>(ch)]);
    }
    const bool luma = c.adjusted(QStringLiteral("levels/luma")), composite = c.adjusted(QStringLiteral("levels/composite"));
    if (luma || composite) {
        Ycc p = toYcc(rgb);
        if (luma) p.y = model::levelsValue(c, QStringLiteral("luma"), p.y);
        if (composite) {
            // The composite signal swings from Y' less the chroma's amplitude to Y' plus it (the chroma's 8-bit range
            // is 224 steps, ours); both ends go through the levels and Y' and the chroma follow them.
            const double amplitude = 224.0 * std::hypot(p.cb, p.cr);
            const double low = model::levelsValue(c, QStringLiteral("composite"), p.y - amplitude);
            const double high = model::levelsValue(c, QStringLiteral("composite"), p.y + amplitude);
            const double scale = amplitude > 0.0 ? (high - low) / 2.0 / amplitude : 0.0;
            p.y = (low + high) / 2.0;
            p.cb *= scale;
            p.cr *= scale;
        }
        rgb = toRgb(p);
    }
    for (double& v : rgb) v = std::clamp(v, 0.0, 1.0);
    return rgb;
}

// The Curves group (p.659-667): the Master graph on all three channels, then each channel's own, on 0-255 (the graphs'
// "8 Bit" scale, p.660).
std::array<double, 3> curvesPixel(const ColorCorrection& c, std::array<double, 3> rgb) {
    auto apply = [&](const QString& graph, double v) {
        return std::clamp(model::curveValue(model::colorCurve(c, graph), v * 255.0) / 255.0, 0.0, 1.0);
    };
    if (c.applies(QStringLiteral("curves/master"))) {
        for (double& v : rgb) v = apply(QStringLiteral("master"), v);
    }
    const QStringList channels = model::colorChannels();
    for (int ch = 0; ch < 3; ++ch) {
        if (c.applies(QStringLiteral("curves/") + channels.at(ch))) rgb[static_cast<std::size_t>(ch)] = apply(channels.at(ch), rgb[static_cast<std::size_t>(ch)]);
    }
    return rgb;
}

// A chroma's place on the wheels: hue from red counterclockwise, saturation 100 at half the chroma range (ours).
std::pair<double, double> wheelPlace(const Ycc& p) {
    double hue = (std::atan2(p.cr, p.cb) - redAngle()) * 180.0 / std::numbers::pi;
    hue = std::remainder(hue, 360.0);
    return {hue, std::hypot(p.cb, p.cr) / 0.5 * 100.0};
}

Ycc fromWheel(double y, double hue, double saturation) {
    const double a = redAngle() + rad(hue), m = saturation / 100.0 * 0.5;
    return {y, std::cos(a) * m, std::sin(a) * m};
}

// The Secondary group (p.668-683): each enabled vector in the panel's order, on the result of those before it ("your
// original green object is red in the final image", p.683), blended by the pixel's membership; Invert Last Vector
// takes the last enabled vector's complement (p.682); Isolate shows what is outside the selected vector in grey
// (p.679).
std::array<double, 3> secondaryPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    const QStringList vectors = model::enabledVectors(c);
    const bool isolate = c.applies(QStringLiteral("secondary/isolate")) && c.number(QStringLiteral("secondary/isolate")) > 0.5;
    if (vectors.isEmpty() && !isolate) return rgb;
    const bool invertLast = c.applies(QStringLiteral("secondary/invertLast")) && c.number(QStringLiteral("secondary/invertLast")) > 0.5;
    const QString last = vectors.isEmpty() ? QString() : vectors.constLast();
    const Ycc original = toYcc(rgb);
    auto membership = [&](const QString& name, const Ycc& p) {
        const auto [hue, saturation] = wheelPlace(p);
        const double w = model::vectorMembership(model::secondaryVector(c, name), hue, saturation);
        return invertLast && name == last ? 1.0 - w : w;
    };
    Ycc p = original;
    for (const QString& name : vectors) {
        const double w = membership(name, p);
        if (w <= 0.0) continue;
        const model::SecondaryVector v = model::secondaryVector(c, name);
        const auto [hue, saturation] = wheelPlace(p);
        // The output moves the colour by the vector's change of hue and saturation; Luminance "reduces the luminance by
        // 10% of the full black-to-white luminance range" at -10 (p.681).
        const double outHue = hue + std::remainder(v.outHue - v.hue, 360.0);
        const double outSaturation = std::max(0.0, saturation + v.outSaturation - v.saturation);
        const Ycc corrected = fromWheel(p.y + v.outLuminance / 100.0 * 219.0, outHue, outSaturation);
        p = blend(p, corrected, w);
    }
    if (isolate) {
        const QString selected = c.text(QStringLiteral("secondary/selected"));
        const double w = membership(selected, original);
        p = blend({p.y, 0.0, 0.0}, p, w);
    }
    return toRgb(p);
}

} // namespace

QPointF wheelHueSaturation(const std::array<double, 3>& rgb) {
    const auto [hue, saturation] = wheelPlace(toYcc(rgb));
    return {hue, saturation};
}

std::array<double, 3> colorCorrectedPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    if (c.values.isEmpty()) return rgb;
    // Each group only when something in it counts, so that nothing that applies (every button off, say) leaves the
    // pixel exactly as it was.
    std::array<double, 3> out = rgb;
    if (c.adjusted(QStringLiteral("hsl"))) out = hslPixel(c, out);
    if (c.adjusted(QStringLiteral("channels"))) out = channelsPixel(c, out);
    if (c.adjusted(QStringLiteral("levels"))) out = levelsPixel(c, out);
    if (c.adjusted(QStringLiteral("curves"))) out = curvesPixel(c, out);
    if (c.adjusted(QStringLiteral("secondary"))) out = secondaryPixel(c, out);
    return out;
}

Picture colorCorrectedPicture(const ColorCorrection& c, const Picture& picture) {
    if (c.values.isEmpty()) return picture;
    Picture out = picture;
    for (std::size_t i = 0; i + 3 < out.data().size(); i += 4) {
        float* px = &out.data()[i];
        const double a = static_cast<double>(px[3]);
        if (a <= 0.0) continue;
        const auto rgb = colorCorrectedPixel(c, {static_cast<double>(px[0]) / a, static_cast<double>(px[1]) / a, static_cast<double>(px[2]) / a});
        for (int k = 0; k < 3; ++k) px[k] = static_cast<float>(rgb[static_cast<std::size_t>(k)] * a);
    }
    return out;
}

} // namespace lumenis::media
