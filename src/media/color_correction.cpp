// Colour correction pictures (color_correction.h). The guide gives each control's range and what it does (p.633-638,
// p.644-667); the formulas are ours and docs/spec/color-correction.md lists them. The HSL group works on Y'CbCr, the
// Channels, Levels and Curves groups on R'G'B', one group after another in the tool's order.

#include "lumenis/media/color_correction.h"

#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

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

Ycc blend(const Ycc& a, const Ycc& b, double t) {
    return {a.y + (b.y - a.y) * t, a.cb + (b.cb - a.cb) * t, a.cr + (b.cr - a.cr) * t};
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

// A control's value when it applies (its value is set and no Enable button above it is off), else nothing.
std::optional<double> applied(const ColorCorrection& c, const QString& path) {
    if (!c.applies(path)) return std::nullopt;
    return c.number(path);
}

bool on(const ColorCorrection& c, const QString& path) {
    return c.applies(path) && c.number(path) > 0.5;
}

// One range's Controls tab (p.633-634), read.
struct Controls {
    std::optional<double> hue, saturation, contrast, brightness, gain, gamma, setup;
    double hueCos = 1.0, hueSin = 0.0; // the hue rotation's, worked out once (10.1b)
    explicit Controls(const ColorCorrection& c, const QString& base) {
        auto read = [&](const char* name) { return applied(c, base + QLatin1String(name)); };
        hue = read("hue");
        saturation = read("saturation");
        contrast = read("contrast");
        brightness = read("brightness");
        gain = read("gain");
        gamma = read("gamma");
        setup = read("setup");
        if (hue) {
            hueCos = std::cos(rad(*hue));
            hueSin = std::sin(rad(*hue));
        }
    }
    Controls() = default;
    // Whether any control is set: one with none leaves a pixel exactly as it is.
    bool any() const { return hue || saturation || contrast || brightness || gain || gamma || setup; }
};

// One range's Controls tab adjustments (p.633-634) on a pixel, fully: the caller blends by the range's share.
Ycc controls(const Controls& k, Ycc p) {
    if (k.hue) {
        // "Shifts the hues in the image around the color wheel": a rotation of the chroma.
        const double cb = p.cb * k.hueCos - p.cr * k.hueSin, cr = p.cb * k.hueSin + p.cr * k.hueCos;
        p.cb = cb;
        p.cr = cr;
    }
    if (k.saturation) {
        const double s = *k.saturation / 100.0;
        p.cb *= s;
        p.cr *= s;
    }
    if (k.contrast) {
        // -100 maps every pixel to neutral grey, 100 is "maximum contrast" (ours: a slope of 20), about mid grey.
        const double v = *k.contrast / 100.0;
        const double f = v <= 0.0 ? 1.0 + v : 1.0 / std::max(0.05, 1.0 - 0.95 * v);
        p.y = 125.5 + (p.y - 125.5) * f;
        p.cb *= std::max(0.0, std::min(1.0, f));
        p.cr *= std::max(0.0, std::min(1.0, f));
    }
    if (k.brightness) p.y += *k.brightness; // "adds 100 to the luminance value of every pixel"
    if (k.gain || k.gamma || k.setup) {
        // Gain "based on a percentage of the original luminance", Gamma on the midtones (raising it lightens them),
        // Setup the black point in 8-bit steps; on the 16-235 range.
        double l = (p.y - 16.0) / 219.0;
        if (k.gain) l *= *k.gain / 100.0;
        if (k.gamma && l > 0.0) l = std::pow(l, 1.0 / std::max(0.1, *k.gamma));
        if (k.setup) l += *k.setup / 219.0;
        p.y = 16.0 + 219.0 * l;
    }
    return p;
}

// A ChromaWheel's offset (p.636-638), read: its angle and amount.
struct Wheel {
    bool used = false;
    double amount = 0, angle = 0;
    double cb = 0, cr = 0; // cos(angle) * amount and sin(angle) * amount, worked out once (10.1b)
};

// What a Channels component (p.647) is made of.
enum class Component { Red, Green, Blue, Luma, Cr, Cb, Offset, None };

Component componentOf(const QString& type) {
    if (type == QLatin1String("red")) return Component::Red;
    if (type == QLatin1String("green")) return Component::Green;
    if (type == QLatin1String("blue")) return Component::Blue;
    if (type == QLatin1String("luma")) return Component::Luma;
    if (type == QLatin1String("cr")) return Component::Cr;
    if (type == QLatin1String("cb")) return Component::Cb;
    if (type == QLatin1String("offset")) return Component::Offset;
    return Component::None;
}

} // namespace

// Every control the pixel functions use, read from the correction once.
struct CorrectionPlan::Data {
    bool empty = true;
    bool hsl = false, channels = false, levels = false, curves = false, secondary = false;
    // HSL (p.631-644).
    bool invertChroma = false, invertLuma = false;
    std::array<QList<QPointF>, 3> lumaCurves;
    Controls master;
    std::array<Controls, 3> ranges;
    std::array<Wheel, 4> wheels; // shadows, midtones, highlights, master
    bool rangesUsed = false;     // a Shadows, Midtones or Highlights control or wheel is set
    std::optional<double> clipLow, clipHigh;
    // Channels (p.644-648).
    struct Channel {
        bool adjusted = false;
        struct Part {
            double amount = 0;
            Component type = Component::None;
            bool invert = false;
        };
        std::array<Part, model::kChannelComponents> parts;
    };
    std::array<Channel, 3> channelRows;
    // Levels (p.648-659): each adjusted tab's points.
    std::optional<model::LevelsPoints> levelsMaster, levelsLuma, levelsComposite;
    std::array<std::optional<model::LevelsPoints>, 3> levelsChannels;
    // Curves (p.659-667).
    std::optional<QList<QPointF>> curveMaster;
    std::array<std::optional<QList<QPointF>>, 3> curveChannels;
    // Secondary (p.668-683): the enabled vectors in order, the last one's complement, Isolate's vector.
    QList<model::SecondaryVector> vectors;
    bool invertLast = false, isolate = false;
    model::SecondaryVector selected;
    bool selectedIsLast = false; // Isolate's vector is the last enabled one (Invert Last Vector applies to it)
};

CorrectionPlan::CorrectionPlan(const ColorCorrection& c) {
    auto d = std::make_shared<Data>();
    d->empty = c.values.isEmpty();
    if (!d->empty) {
        // Each group only when something in it counts, so that nothing that applies (every button off, say) leaves
        // the pixel exactly as it was.
        d->hsl = c.adjusted(QStringLiteral("hsl"));
        d->channels = c.adjusted(QStringLiteral("channels"));
        d->levels = c.adjusted(QStringLiteral("levels"));
        d->curves = c.adjusted(QStringLiteral("curves"));
        d->secondary = c.adjusted(QStringLiteral("secondary"));
    }
    const QStringList channels = model::colorChannels();
    if (d->hsl) {
        const QString master = QStringLiteral("hsl/controls/master/");
        d->invertChroma = on(c, master + QStringLiteral("invertChroma"));
        d->invertLuma = on(c, master + QStringLiteral("invertLuma"));
        d->lumaCurves = model::lumaRangeCurves(c);
        d->master = Controls(c, master);
        const char* const ranges[] = {"shadows", "midtones", "highlights"};
        for (int r = 0; r < 3; ++r)
            d->ranges[static_cast<std::size_t>(r)] = Controls(c, QStringLiteral("hsl/controls/") + QLatin1String(ranges[r]) + QLatin1Char('/'));
        // Ours: Amount 100 is a quarter of the chroma range.
        const char* const wheels[] = {"shadows", "midtones", "highlights", "master"};
        for (int i = 0; i < 4; ++i) {
            const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(wheels[i]) + QLatin1Char('/');
            Wheel& w = d->wheels[static_cast<std::size_t>(i)];
            w.used = c.applies(base + QStringLiteral("hue")) || c.applies(base + QStringLiteral("amount"));
            if (!w.used) continue;
            w.amount = c.number(base + QStringLiteral("amount")) / 100.0 * 0.25;
            w.angle = redAngle() + rad(c.number(base + QStringLiteral("hue")));
            w.cb = std::cos(w.angle) * w.amount;
            w.cr = std::sin(w.angle) * w.amount;
        }
        // The ranges' shares are needed only for a range's controls or wheel (10.1b: else a walk of three curves a pixel).
        for (std::size_t r = 0; r < 3; ++r) d->rangesUsed = d->rangesUsed || d->ranges[r].any() || d->wheels[r].used;
        d->clipLow = applied(c, master + QStringLiteral("clipLow"));
        d->clipHigh = applied(c, master + QStringLiteral("clipHigh"));
    }
    if (d->channels) {
        for (int ch = 0; ch < 3; ++ch) {
            const QString& channel = channels.at(ch);
            Data::Channel& row = d->channelRows[static_cast<std::size_t>(ch)];
            row.adjusted = c.adjusted(QStringLiteral("channels/") + channel);
            if (!row.adjusted) continue;
            for (int n = 1; n <= model::kChannelComponents; ++n) {
                auto path = [&](const char* control) { return model::channelComponentPath(channel, n, QLatin1String(control)); };
                Data::Channel::Part& part = row.parts[static_cast<std::size_t>(n - 1)];
                // An unset or turned-off component keeps its default: the formula is the channel's whole row.
                part.amount = c.applies(path("amount")) ? c.number(path("amount")) : model::colorControlDefault(path("amount")).value_or(0.0);
                part.type = componentOf(c.applies(path("type")) ? c.text(path("type")) : model::colorControlTextDefault(path("type")));
                // Invert "subtracts the component from the overall formula instead of adding it" (p.647).
                part.invert = on(c, path("invert"));
            }
        }
    }
    if (d->levels) {
        auto tab = [&](const QString& name) -> std::optional<model::LevelsPoints> {
            if (!c.adjusted(QStringLiteral("levels/") + name)) return std::nullopt;
            return model::levelsPointsOf(c, name);
        };
        d->levelsMaster = tab(QStringLiteral("master"));
        for (int ch = 0; ch < 3; ++ch) d->levelsChannels[static_cast<std::size_t>(ch)] = tab(channels.at(ch));
        d->levelsLuma = tab(QStringLiteral("luma"));
        d->levelsComposite = tab(QStringLiteral("composite"));
    }
    if (d->curves) {
        auto graph = [&](const QString& name) -> std::optional<QList<QPointF>> {
            if (!c.applies(QStringLiteral("curves/") + name)) return std::nullopt;
            return model::colorCurve(c, name);
        };
        d->curveMaster = graph(QStringLiteral("master"));
        for (int ch = 0; ch < 3; ++ch) d->curveChannels[static_cast<std::size_t>(ch)] = graph(channels.at(ch));
    }
    if (d->secondary) {
        const QStringList enabled = model::enabledVectors(c);
        for (const QString& name : enabled) d->vectors << model::secondaryVector(c, name);
        d->isolate = on(c, QStringLiteral("secondary/isolate"));
        d->invertLast = on(c, QStringLiteral("secondary/invertLast"));
        if (d->isolate) {
            const QString selected = c.text(QStringLiteral("secondary/selected"));
            d->selected = model::secondaryVector(c, selected);
            d->selectedIsLast = !enabled.isEmpty() && enabled.constLast() == selected;
        }
    }
    d_ = std::move(d);
}

CorrectionPlan::~CorrectionPlan() = default;
CorrectionPlan::CorrectionPlan(const CorrectionPlan&) = default;
CorrectionPlan& CorrectionPlan::operator=(const CorrectionPlan&) = default;

bool CorrectionPlan::isEmpty() const {
    return d_->empty;
}

namespace {

using Plan = CorrectionPlan::Data;

// The HSL group (p.631-644).
std::array<double, 3> hslPixel(const Plan& d, const std::array<double, 3>& rgb) {
    Ycc p = toYcc(rgb);
    // Invert Chroma, "the equivalent of setting the Hue control to 180", and Invert Luma (p.634).
    if (d.invertChroma) {
        p.cb = -p.cb;
        p.cr = -p.cr;
    }
    if (d.invertLuma) p.y = 251.0 - p.y;
    // The ranges' shares of this pixel, by its brightness before the corrections (ours).
    const std::array<double, 3> w = d.rangesUsed ? model::lumaRangeWeights(d.lumaCurves, std::clamp(p.y, 0.0, 255.0))
                                                 : std::array<double, 3>{0.0, 0.0, 0.0};
    // Master, then Shadows, Midtones and Highlights; a range with nothing set blends the pixel with itself, so it is
    // left out.
    p = controls(d.master, p);
    for (std::size_t r = 0; r < 3; ++r) {
        if (d.ranges[r].any()) p = blend(p, controls(d.ranges[r], p), w[r]);
    }
    // The ChromaWheels: an offset of hue and amount in the chroma plane (p.636-638), each wheel by its range's share,
    // Master over all.
    for (std::size_t i = 0; i < 4; ++i) {
        const Wheel& wheel = d.wheels[i];
        if (!wheel.used) continue;
        const double share = i == 3 ? 1.0 : w[i];
        p.cb += wheel.cb * share;
        p.cr += wheel.cr * share;
    }
    // Clip Low and Clip High: "All pixels with the Low clip value or less are clipped to black" (p.634).
    if (d.clipLow && p.y <= *d.clipLow) p = {16.0, 0.0, 0.0};
    if (d.clipHigh && p.y >= *d.clipHigh) p = {235.0, 0.0, 0.0};
    return toRgb(p);
}

// The Channels group (p.644-648): each output channel the sum of its four components' percentages ("The total of the
// percentages you set for a channel does not need to be 100", p.647). Components are on 0-1: R, G and B as they are,
// Luma Y' (Rec. 709), Cr and Cb centred on 0.5 so that neutral is mid grey, as in their monochrome previews, and Offset
// 1 (ours).
std::array<double, 3> channelsPixel(const Plan& d, const std::array<double, 3>& rgb) {
    const double y = kR * rgb[0] + kG * rgb[1] + kB * rgb[2];
    const Ycc ycc = toYcc(rgb);
    auto component = [&](Component type) {
        switch (type) {
        case Component::Red: return rgb[0];
        case Component::Green: return rgb[1];
        case Component::Blue: return rgb[2];
        case Component::Luma: return y;
        case Component::Cr: return 0.5 + ycc.cr;
        case Component::Cb: return 0.5 + ycc.cb;
        case Component::Offset: return 1.0;
        case Component::None: break;
        }
        return 0.0;
    };
    std::array<double, 3> out = rgb;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        const Plan::Channel& row = d.channelRows[ch];
        if (!row.adjusted) continue;
        double v = 0.0;
        for (const Plan::Channel::Part& part : row.parts) v += (part.invert ? -1.0 : 1.0) * part.amount / 100.0 * component(part.type);
        out[ch] = std::clamp(v, 0.0, 1.0);
    }
    return out;
}

// The Levels group (p.648-659), each tab that is adjusted in turn: Master on all three channels, then Red, Green and
// Blue, then Luma on Y' and Composite last (ours). Channels are on the 8-bit video scale, 0-1 as 16-235.
std::array<double, 3> levelsPixel(const Plan& d, std::array<double, 3> rgb) {
    auto level = [](const model::LevelsPoints& points, double v) { return (model::levelsValue(points, 16.0 + 219.0 * v) - 16.0) / 219.0; };
    if (d.levelsMaster) {
        for (double& v : rgb) v = level(*d.levelsMaster, v);
    }
    for (std::size_t ch = 0; ch < 3; ++ch) {
        if (d.levelsChannels[ch]) rgb[ch] = level(*d.levelsChannels[ch], rgb[ch]);
    }
    if (d.levelsLuma || d.levelsComposite) {
        Ycc p = toYcc(rgb);
        if (d.levelsLuma) p.y = model::levelsValue(*d.levelsLuma, p.y);
        if (d.levelsComposite) {
            // The composite signal swings from Y' less the chroma's amplitude to Y' plus it (the chroma's 8-bit range
            // is 224 steps, ours); both ends go through the levels and Y' and the chroma follow them.
            const double amplitude = 224.0 * std::hypot(p.cb, p.cr);
            const double low = model::levelsValue(*d.levelsComposite, p.y - amplitude);
            const double high = model::levelsValue(*d.levelsComposite, p.y + amplitude);
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
std::array<double, 3> curvesPixel(const Plan& d, std::array<double, 3> rgb) {
    auto apply = [](const QList<QPointF>& curve, double v) { return std::clamp(model::curveValue(curve, v * 255.0) / 255.0, 0.0, 1.0); };
    if (d.curveMaster) {
        for (double& v : rgb) v = apply(*d.curveMaster, v);
    }
    for (std::size_t ch = 0; ch < 3; ++ch) {
        if (d.curveChannels[ch]) rgb[ch] = apply(*d.curveChannels[ch], rgb[ch]);
    }
    return rgb;
}

// The Secondary group (p.668-683): each enabled vector in the panel's order, on the result of those before it ("your
// original green object is red in the final image", p.683), blended by the pixel's membership; Invert Last Vector
// takes the last enabled vector's complement (p.682); Isolate shows what is outside the selected vector in grey
// (p.679).
std::array<double, 3> secondaryPixel(const Plan& d, const std::array<double, 3>& rgb) {
    if (d.vectors.isEmpty() && !d.isolate) return rgb;
    const Ycc original = toYcc(rgb);
    auto membership = [&](const model::SecondaryVector& vector, bool last, const Ycc& p) {
        const auto [hue, saturation] = wheelPlace(p);
        const double w = model::vectorMembership(vector, hue, saturation);
        return d.invertLast && last ? 1.0 - w : w;
    };
    Ycc p = original;
    for (qsizetype i = 0; i < d.vectors.size(); ++i) {
        const model::SecondaryVector& v = d.vectors.at(i);
        const double w = membership(v, i == d.vectors.size() - 1, p);
        if (w <= 0.0) continue;
        const auto [hue, saturation] = wheelPlace(p);
        // The output moves the colour by the vector's change of hue and saturation; Luminance "reduces the luminance by
        // 10% of the full black-to-white luminance range" at -10 (p.681).
        const double outHue = hue + std::remainder(v.outHue - v.hue, 360.0);
        const double outSaturation = std::max(0.0, saturation + v.outSaturation - v.saturation);
        const Ycc corrected = fromWheel(p.y + v.outLuminance / 100.0 * 219.0, outHue, outSaturation);
        p = blend(p, corrected, w);
    }
    if (d.isolate) {
        const double w = membership(d.selected, d.selectedIsLast, original);
        p = blend({p.y, 0.0, 0.0}, p, w);
    }
    return toRgb(p);
}

} // namespace

std::array<double, 3> wheelColor(double hue, double saturation, double y) {
    return toRgb(fromWheel(16.0 + 219.0 * y, hue, saturation));
}

QPointF wheelHueSaturation(const std::array<double, 3>& rgb) {
    const auto [hue, saturation] = wheelPlace(toYcc(rgb));
    return {hue, saturation};
}

std::array<double, 3> CorrectionPlan::pixel(const std::array<double, 3>& rgb) const {
    const Data& d = *d_;
    if (d.empty) return rgb;
    std::array<double, 3> out = rgb;
    if (d.hsl) out = hslPixel(d, out);
    if (d.channels) out = channelsPixel(d, out);
    if (d.levels) out = levelsPixel(d, out);
    if (d.curves) out = curvesPixel(d, out);
    if (d.secondary) out = secondaryPixel(d, out);
    return out;
}

std::array<double, 3> colorCorrectedPixel(const ColorCorrection& c, const std::array<double, 3>& rgb) {
    return CorrectionPlan(c).pixel(rgb);
}

Picture colorCorrectedPicture(const ColorCorrection& c, const Picture& picture) {
    if (c.values.isEmpty()) return picture;
    const CorrectionPlan plan(c);
    Picture out = picture;
    float* data = out.data().data();
    const std::size_t pixels = out.data().size() / 4;
    auto correct = [&plan, data](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            float* px = data + i * 4;
            const double a = static_cast<double>(px[3]);
            if (a <= 0.0) continue;
            const auto rgb = plan.pixel({static_cast<double>(px[0]) / a, static_cast<double>(px[1]) / a, static_cast<double>(px[2]) / a});
            for (std::size_t k = 0; k < 3; ++k) px[k] = static_cast<float>(rgb[k] * a);
        }
    };
    // Bands of rows on the processor's threads (the plan is only read).
    detail::parallelRange(pixels, correct, 1 << 15);
    return out;
}

} // namespace lumenis::media
