// The automatic corrections (auto_correct.h; FX Guide p.618-622). The guide says what each button adjusts and what it
// aims for; how the frame is measured and the controls solved for is ours (docs/spec/color-correction.md):
//
// * The HSL tonal corrections treat the Master range's Gain and Setup as the linear map y' - 16 = Gain/100 * (y - 16)
//   + Setup (Gamma and the other ranges left as they are) and solve it so that the darkest pixels land on 16 and the
//   brightest on 235.
// * The HSL Auto Balance moves each of the Shadows, Midtones and Highlights ChromaWheels so that the pixels of its
//   range average to no chroma, weighted by their share of the range, in three passes.
// * The Curves corrections measure the picture before the Curves group: Auto Contrast puts the Master curve's ends at
//   the darkest and brightest luma, Auto Balance maps each channel's darkest and brightest values to the luma's.
// * Remove Color Cast moves one wheel by the sampled colour's chroma over the share its brightness has of that range,
//   or adds to each colour curve a point that takes the sample's channel to its luma.

#include "lumenis/media/auto_correct.h"

#include "lumenis/media/color_correction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace lumenis::media {

namespace {

using model::ColorCorrection;
using Rgb = std::array<double, 3>;

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;
// The share of pixels left out at each end of the tonal range.
constexpr double kTail = 0.005;

struct Ycc {
    double y = 16, cb = 0, cr = 0; // y in 8-bit video levels, cb and cr -0.5 to 0.5
};

Ycc toYcc(const Rgb& c) {
    const double y = kR * c[0] + kG * c[1] + kB * c[2];
    return {16.0 + 219.0 * y, (c[2] - y) / (2.0 * (1.0 - kB)), (c[0] - y) / (2.0 * (1.0 - kR))};
}

// The angle of red in the Cb/Cr plane, where the ChromaWheels' 0 degrees is (as media::colorCorrectedPixel has it).
double redAngle() {
    const Ycc red = toYcc({1.0, 0.0, 0.0});
    return std::atan2(red.cr, red.cb);
}

// The frame as at most 96 by 54 pixels of full-range R'G'B'.
std::vector<Rgb> pixels(const QImage& frame) {
    std::vector<Rgb> out;
    if (frame.isNull()) return out;
    const QImage small = frame.scaled(std::min(frame.width(), 96), std::min(frame.height(), 54), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    out.reserve(static_cast<std::size_t>(small.width() * small.height()));
    for (int y = 0; y < small.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) out.push_back({qRed(line[x]) / 255.0, qGreen(line[x]) / 255.0, qBlue(line[x]) / 255.0});
    }
    return out;
}

// The value below which `share` of `values` lie.
double percentile(std::vector<double> values, double share) {
    if (values.empty()) return 0.0;
    const auto k = static_cast<std::size_t>(std::clamp(share, 0.0, 1.0) * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k), values.end());
    return values[k];
}

// The darkest and brightest luma of the pixels through `c`, as 8-bit video levels.
std::pair<double, double> tonalRange(const ColorCorrection& c, const std::vector<Rgb>& source) {
    std::vector<double> y;
    y.reserve(source.size());
    const CorrectionPlan plan(c); // read once for all the pixels
    for (const Rgb& p : source) y.push_back(toYcc(plan.pixel(p)).y);
    return {percentile(y, kTail), percentile(y, 1.0 - kTail)};
}

bool tonal(ColorCorrection& c, const std::vector<Rgb>& source, AutoCorrection kind, bool hueOffsets) {
    const auto [lo, hi] = tonalRange(c, source);
    const QString base = hueOffsets ? QStringLiteral("hsl/hueOffsets/") : QStringLiteral("hsl/controls/master/");
    const QString gainPath = base + QStringLiteral("gain"), setupPath = base + QStringLiteral("setup");
    const double g = c.number(gainPath) / 100.0, s = c.number(setupPath);
    if (g <= 0.0) return false;
    // Each measured level as the input the Gain multiplies: y' - 16 = g * a + s.
    const double aLo = (lo - 16.0 - s) / g, aHi = (hi - 16.0 - s) / g;
    double gain = g, setup = s;
    switch (kind) {
    case AutoCorrection::HslBlack: setup = -g * aLo; break;
    case AutoCorrection::HslWhite:
        if (aHi <= 1.0) return false;
        gain = (219.0 - s) / aHi;
        break;
    default:
        if (aHi - aLo <= 1.0) return false;
        gain = 219.0 / (aHi - aLo);
        setup = -gain * aLo;
        break;
    }
    const ColorCorrection before = c;
    if (kind != AutoCorrection::HslBlack) c.set(gainPath, std::round(gain * 10000.0) / 100.0);
    if (kind != AutoCorrection::HslWhite) c.set(setupPath, std::round(setup * 100.0) / 100.0);
    return c != before;
}

const char* const kWheels[] = {"shadows", "midtones", "highlights"};

// A wheel's offset in the Cb/Cr plane, and setting it back as hue and amount ("Amount 100 is a quarter of the chroma
// range", as the picture reads it).
QPointF wheelVector(const ColorCorrection& c, int range) {
    const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(kWheels[range]) + QLatin1Char('/');
    const double amount = c.number(base + QStringLiteral("amount")) / 100.0 * 0.25;
    const double angle = redAngle() + c.number(base + QStringLiteral("hue")) * std::numbers::pi / 180.0;
    return {std::cos(angle) * amount, std::sin(angle) * amount};
}

void setWheelVector(ColorCorrection& c, int range, QPointF v) {
    const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(kWheels[range]) + QLatin1Char('/');
    const double amount = std::min(100.0, std::hypot(v.x(), v.y()) / 0.25 * 100.0);
    double hue = (std::atan2(v.y(), v.x()) - redAngle()) * 180.0 / std::numbers::pi;
    hue = std::remainder(hue, 360.0);
    c.set(base + QStringLiteral("amount"), std::round(amount * 100.0) / 100.0);
    c.set(base + QStringLiteral("hue"), std::round(hue * 100.0) / 100.0);
}

bool hslBalance(ColorCorrection& c, const std::vector<Rgb>& source) {
    const ColorCorrection before = c;
    for (int pass = 0; pass < 3; ++pass) {
        std::array<double, 3> weight{}, weight2{};
        std::array<QPointF, 3> chroma{};
        const CorrectionPlan plan(c);
        const auto curves = model::lumaRangeCurves(c);
        for (const Rgb& p : source) {
            // The ranges' shares go by the brightness before the corrections, as the picture weighs them.
            const auto w = model::lumaRangeWeights(curves, std::clamp(toYcc(p).y, 0.0, 255.0));
            const Ycc out = toYcc(plan.pixel(p));
            for (std::size_t r = 0; r < 3; ++r) {
                weight[r] += w[r];
                weight2[r] += w[r] * w[r];
                chroma[r] += QPointF(out.cb, out.cr) * w[r];
            }
        }
        for (int r = 0; r < 3; ++r) {
            const auto k = static_cast<std::size_t>(r);
            if (weight[k] <= 1e-6 || weight2[k] <= 1e-9) continue;
            // The wheel adds its vector times each pixel's share: the range's average moves by vector * sum(w^2) /
            // sum(w).
            const QPointF mean = chroma[k] / weight[k];
            setWheelVector(c, r, wheelVector(c, r) - mean * (weight[k] / weight2[k]));
        }
    }
    return c != before;
}

// The pixels through every group before Curves.
std::vector<Rgb> beforeCurves(const ColorCorrection& c, const std::vector<Rgb>& source) {
    ColorCorrection without = c;
    without.reset(QStringLiteral("curves"));
    without.reset(QStringLiteral("secondary"));
    std::vector<Rgb> out;
    out.reserve(source.size());
    const CorrectionPlan plan(without);
    for (const Rgb& p : source) out.push_back(plan.pixel(p));
    return out;
}

double luma255(const Rgb& p) {
    return 255.0 * (kR * p[0] + kG * p[1] + kB * p[2]);
}

bool curvesContrast(ColorCorrection& c, const std::vector<Rgb>& source) {
    const std::vector<Rgb> in = beforeCurves(c, source);
    std::vector<double> y;
    for (const Rgb& p : in) y.push_back(luma255(p));
    const double lo = percentile(y, kTail), hi = percentile(y, 1.0 - kTail);
    if (hi - lo < 8.0) return false;
    const ColorCorrection before = c;
    model::setColorCurve(c, QStringLiteral("master"), {QPointF(std::round(lo), 0), QPointF(std::round(hi), 255)});
    return c != before;
}

bool curvesBalance(ColorCorrection& c, const std::vector<Rgb>& source) {
    // After the Master curve, as the colour curves see the picture.
    const QList<QPointF> master = model::colorCurve(c, QStringLiteral("master"));
    std::vector<Rgb> in = beforeCurves(c, source);
    if (c.applies(QStringLiteral("curves/master"))) {
        for (Rgb& p : in) {
            for (double& v : p) v = model::curveValue(master, v * 255.0) / 255.0;
        }
    }
    std::vector<double> y;
    for (const Rgb& p : in) y.push_back(luma255(p));
    const double yLo = percentile(y, kTail), yHi = percentile(y, 1.0 - kTail);
    const ColorCorrection before = c;
    const char* const channels[] = {"red", "green", "blue"};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        std::vector<double> v;
        for (const Rgb& p : in) v.push_back(p[ch] * 255.0);
        const double lo = percentile(v, kTail), hi = percentile(v, 1.0 - kTail);
        if (hi - lo < 8.0) continue;
        QList<QPointF> points;
        if (lo > 2.0) points << QPointF(0, 0);
        points << QPointF(std::round(lo), std::round(yLo)) << QPointF(std::round(hi), std::round(yHi));
        if (hi < 253.0) points << QPointF(255, 255);
        model::setColorCurve(c, QLatin1String(channels[ch]), points);
    }
    return c != before;
}

// The input of a monotone curve that gives `output` (its ends where the curve never reaches it).
double inverseCurve(const QList<QPointF>& curve, double output) {
    double lo = 0.0, hi = 255.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (model::curveValue(curve, mid) < output) lo = mid;
        else hi = mid;
    }
    return (lo + hi) / 2.0;
}

} // namespace

bool autoCorrect(model::ColorCorrection& correction, const QImage& frame, AutoCorrection kind, bool hueOffsets) {
    const std::vector<Rgb> source = pixels(frame);
    if (source.empty()) return false;
    switch (kind) {
    case AutoCorrection::HslBalance: return hslBalance(correction, source);
    case AutoCorrection::HslBlack:
    case AutoCorrection::HslContrast:
    case AutoCorrection::HslWhite: return tonal(correction, source, kind, hueOffsets);
    case AutoCorrection::CurvesBalance: return curvesBalance(correction, source);
    case AutoCorrection::CurvesContrast: return curvesContrast(correction, source);
    }
    return false;
}

bool removeColorCast(model::ColorCorrection& correction, const QColor& sampled, CastRemoval where) {
    const Rgb seen{static_cast<double>(sampled.redF()), static_cast<double>(sampled.greenF()), static_cast<double>(sampled.blueF())};
    const model::ColorCorrection before = correction;
    if (where != CastRemoval::Curves) {
        // "only the parts of the image that fall within the luminance range controlled by that wheel change" (p.622).
        const Ycc c = toYcc(seen);
        const int range = where == CastRemoval::Shadows ? 0 : where == CastRemoval::Midtones ? 1 : 2;
        const double share = model::lumaRangeWeights(correction, std::clamp(c.y, 0.0, 255.0))[static_cast<std::size_t>(range)];
        if (share < 0.05) return false;
        setWheelVector(correction, range, wheelVector(correction, range) - QPointF(c.cb, c.cr) / share);
        return correction != before;
    }
    // A point on each colour curve taking the sample's channel to its luma; the sample is after the curves, so its
    // value is first followed back through each one.
    const double target = luma255(seen);
    const char* const channels[] = {"red", "green", "blue"};
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const QString graph = QLatin1String(channels[ch]);
        QList<QPointF> curve = model::colorCurve(correction, graph);
        const double input = std::round(inverseCurve(curve, seen[ch] * 255.0));
        // A point already near that input moves; else one is added, up to the limit.
        bool moved = false;
        for (QPointF& p : curve) {
            if (std::abs(p.x() - input) <= 2.0) {
                p.setY(std::round(target));
                moved = true;
                break;
            }
        }
        if (!moved) {
            if (curve.size() >= model::kMaxCurvePoints) continue;
            curve << QPointF(input, std::round(target));
        }
        model::setColorCurve(correction, graph, curve);
    }
    return correction != before;
}

} // namespace lumenis::media
