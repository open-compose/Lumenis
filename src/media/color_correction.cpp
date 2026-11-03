// Colour correction pictures (color_correction.h). The guide gives each control's range and what it does (p.633-638);
// the formulas are ours and docs/spec/color-correction.md lists them.

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

} // namespace

std::array<double, 3> colorCorrectedPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    if (c.values.isEmpty()) return rgb;
    // Nothing that applies (every button off, say): the pixel exactly as it was.
    bool any = false;
    for (auto it = c.values.constBegin(); it != c.values.constEnd() && !any; ++it) any = c.applies(it.key());
    if (!any) return rgb;
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
