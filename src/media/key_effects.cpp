#include "effect_drawing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// The Key effects (ROADMAP 6.3c; docs/spec/keys.md): Luma Key and Chroma Key (FX Guide p.473,
// p.548-550), the RGB Keyer (p.474, p.550-551) and SpectraMatte (p.474-475, p.551-561). Each makes an
// alpha matte for the segment's picture from its colours, so the tracks below show where the key colour
// was; Matte Key, which takes its matte from another track, is in the compositor. The guide says what
// each control does but gives no formulas; the mattes below are ours and the spec says so.
namespace lumenis::media::detail {

namespace {

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722; // Rec. 709 luma

struct Ycc {
    double y = 0, cb = 0, cr = 0;
};

Ycc toYcc(double r, double g, double b) {
    Ycc c;
    c.y = kR * r + kG * g + kB * b;
    c.cb = (b - c.y) / 1.8556;
    c.cr = (r - c.y) / 1.5748;
    return c;
}

void fromYcc(const Ycc& c, double out[3]) {
    out[0] = c.y + 1.5748 * c.cr;
    out[2] = c.y + 1.8556 * c.cb;
    out[1] = (c.y - kR * out[0] - kB * out[2]) / kG;
}

double num(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span, double fallback) {
    const QJsonValue d = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(d.isDouble() ? d.toDouble() : fallback);
}

std::optional<model::Rgb> colour(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    if (const auto c = model::colorAt(e, category, name, time, span)) return c;
    const QColor d(parameterDefault(e, category, name).toString());
    if (!d.isValid()) return std::nullopt;
    return model::Rgb{static_cast<double>(d.redF()), static_cast<double>(d.greenF()), static_cast<double>(d.blueF())};
}

bool flag(const model::Effect& e, const QString& category, const QString& name) {
    return e.parameters.value(category).toObject().value(name).toBool();
}

// 0 below `from`, 1 above `to`, linear between (a step when they meet).
double ramp(double v, double from, double to) {
    if (to <= from) return v < from ? 0.0 : 1.0;
    return std::clamp((v - from) / (to - from), 0.0, 1.0);
}

// The unpremultiplied colour of a pixel.
void colourOf(const float* px, double rgb[3]) {
    const double a = static_cast<double>(px[3]);
    for (int k = 0; k < 3; ++k) rgb[k] = a > 0 ? static_cast<double>(px[k]) / a : 0.0;
}

// A matte blurred by a box of `radius` pixels, then kept as it is, or taken at its smaller (Erode) or
// larger (Dilate) value against the original.
enum class MatteBlur { Blur, Erode, Dilate };
void blurMatte(std::vector<double>& matte, int width, int height, int radius, MatteBlur mode) {
    if (radius <= 0 || width <= 0 || height <= 0) return;
    const std::vector<double> original = matte;
    std::vector<double> tmp(matte.size());
    for (int pass = 0; pass < 2; ++pass) {
        const bool horizontal = pass == 0;
        const int n = horizontal ? width : height;
        const int lines = horizontal ? height : width;
        for (int l = 0; l < lines; ++l) {
            auto idx = [&](int i) {
                const int c = std::clamp(i, 0, n - 1);
                return static_cast<std::size_t>(horizontal ? l * width + c : c * width + l);
            };
            double sum = 0;
            for (int i = -radius; i <= radius; ++i) sum += matte[idx(i)];
            for (int i = 0; i < n; ++i) {
                tmp[idx(i)] = sum / (2.0 * radius + 1.0);
                sum += matte[idx(i + radius + 1)] - matte[idx(i - radius)];
            }
        }
        matte.swap(tmp);
    }
    if (mode == MatteBlur::Erode) {
        for (std::size_t i = 0; i < matte.size(); ++i) matte[i] = std::min(matte[i], original[i]);
    } else if (mode == MatteBlur::Dilate) {
        for (std::size_t i = 0; i < matte.size(); ++i) matte[i] = std::max(matte[i], original[i]);
    }
}

// Applies `matte` (the foreground's opacity, 0 to 1) to `picture`, or shows it as a grey picture
// (Show Alpha: "Displays the grayscale alpha channel used to apply the key effect", p.495).
Picture applyMatte(const Picture& picture, const std::vector<double>& matte, bool showAlpha) {
    Picture out = picture;
    for (std::size_t i = 0; i < matte.size(); ++i) {
        float* px = &out.data()[i * 4];
        const float m = static_cast<float>(std::clamp(matte[i], 0.0, 1.0));
        if (showAlpha) {
            px[0] = px[1] = px[2] = m;
            px[3] = 1.0f;
        } else {
            for (int c = 0; c < 4; ++c) px[c] *= m;
        }
    }
    return out;
}

// The Foreground's Invert Key, Show Alpha and Level (p.495), and Scaling, Position and Crop as
// Picture-in-Picture places its picture, at normal size until set.
Picture finish(const model::Effect& e, const Picture& layer, std::vector<double> matte, double time, const model::EffectSpan& span) {
    const QString fg = QStringLiteral("Foreground");
    if (flag(e, fg, QStringLiteral("Invert Key"))) {
        for (double& m : matte) m = 1.0 - m;
    }
    const double level = std::clamp(model::numberAt(e, fg, QStringLiteral("Level"), time, span).value_or(100.0), 0.0, 100.0) / 100.0;
    for (double& m : matte) m *= level;
    Picture keyed = applyMatte(layer, matte, flag(e, fg, QStringLiteral("Show Alpha")));
    const bool placed = e.parameters.contains(QStringLiteral("Scaling")) || e.parameters.contains(QStringLiteral("Position")) ||
                        e.parameters.contains(QStringLiteral("Crop"));
    if (!placed) return keyed;
    model::Effect pip{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
    const QStringList placing = {QStringLiteral("Scaling"), QStringLiteral("Position"), QStringLiteral("Crop")};
    for (const QString& c : placing) {
        if (e.parameters.contains(c)) pip.parameters.insert(c, e.parameters.value(c));
    }
    QJsonObject scaling = pip.parameters.value(QStringLiteral("Scaling")).toObject();
    if (!scaling.contains(QStringLiteral("X"))) scaling.insert(QStringLiteral("X"), 100);
    if (!scaling.contains(QStringLiteral("Y"))) scaling.insert(QStringLiteral("Y"), 100);
    pip.parameters.insert(QStringLiteral("Scaling"), scaling);
    return imageEffectPicture(pip, keyed, time, span);
}

// One pixel of the RGB Keyer: its matte value, and its spill taken out (rgbKeyerPicture). A function
// of its own, with the optimizer off for it under MSVC: MSVC 14.44 failed with an internal compiler
// error (C1001) on this loop inside rgbKeyerPicture (BUILDING.md).
#if defined(_MSC_VER) && !defined(__clang__)
#pragma optimize("", off)
#endif
double rgbKeyPixel(float* px, const model::Rgb& key, double threshold, double soft, double spill, int spillChannel) {
    // No local arrays: with the optimizer off, MSVC warns (C4748) about arrays it cannot guard.
    const double alpha = static_cast<double>(px[3]);
    double r = alpha > 0 ? static_cast<double>(px[0]) / alpha : 0.0;
    double g = alpha > 0 ? static_cast<double>(px[1]) / alpha : 0.0;
    double b = alpha > 0 ? static_cast<double>(px[2]) / alpha : 0.0;
    const double dr = r - key.red;
    const double dg = g - key.green;
    const double db = b - key.blue;
    const double d = std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
    if (spill > 0) {
        double& strongest = spillChannel == 0 ? r : spillChannel == 1 ? g : b;
        const double first = spillChannel == 0 ? g : r;
        const double second = spillChannel == 2 ? g : b;
        const double others = first > second ? first : second;
        if (strongest > others) {
            strongest -= (strongest - others) * spill;
            px[0] = static_cast<float>(r * alpha);
            px[1] = static_cast<float>(g * alpha);
            px[2] = static_cast<float>(b * alpha);
        }
    }
    return threshold <= 0.0 ? 1.0 : ramp(d, threshold, threshold + soft);
}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma optimize("", on)
#endif

} // namespace

// ---------------------------------------------------------------- Luma Key and Chroma Key

Picture lumaChromaKeyPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    // "Key selects the primary key color to be replaced by video in the Chroma Key and Luma Key effects":
    // Gain "Specifies how much of the foreground and the background video displays. Values range from 0
    // (shows only the foreground) to 63 (replaces all the foreground video with the background video)";
    // Soft blends "the bordering colors along the edge of the key" (p.548). Ours: a pixel is keyed when
    // its distance from the key colour (luma for Luma Key; chroma, measured against the key colour's own
    // chroma, for Chroma Key) is under Gain/63 of the largest, with Soft/100 of a quarter of it blended.
    const bool luma = e.name == QLatin1String("Luma Key");
    const QString key = QStringLiteral("Key Effects Key Parameters");
    const QString more = QStringLiteral("Chroma Key Profile, Secondary Key, and Spill Suppression Parameters");
    struct Keyer {
        Ycc colour;
        double threshold = 0;
        double soft = 0;
        bool all = false;  // Gain 63
        bool none = true;  // Gain 0
    };
    const double chromaScale = 2.0; // the largest chroma distance, in units of the key colour's chroma
    auto keyer = [&](const QString& category, const QString& prefix) -> std::optional<Keyer> {
        const auto c = colour(e, category, prefix + QStringLiteral("Color controls"), time, span);
        if (!c) return std::nullopt;
        Keyer k;
        k.colour = toYcc(c->red, c->green, c->blue);
        const double gain = std::clamp(num(e, category, prefix + QStringLiteral("Gain"), time, span, 0.0), 0.0, 63.0);
        const double scale = luma ? 1.0 : chromaScale;
        k.threshold = gain / 63.0 * scale;
        k.soft = std::max(num(e, category, prefix + QStringLiteral("Soft"), time, span, 0.0), 0.0) / 100.0 * 0.25 * scale;
        k.all = gain >= 63.0;
        k.none = gain <= 0.0;
        return k;
    };
    auto distance = [&](const Keyer& k, const Ycc& p) {
        if (luma) return std::abs(p.y - k.colour.y);
        const double kc = std::max(std::hypot(k.colour.cb, k.colour.cr), 0.05);
        return std::hypot(p.cb - k.colour.cb, p.cr - k.colour.cr) / kc;
    };
    auto opacity = [&](const Keyer& k, const Ycc& p) {
        if (k.all) return 0.0;
        if (k.none) return 1.0;
        return ramp(distance(k, p), k.threshold, k.threshold + k.soft);
    };
    const auto primary = keyer(key, QString());
    // Chroma Key's Secondary Key "selects a second background color to key out", and Spill Suppression
    // "neutralizes the selected color without affecting the luminance. The spill color changes to a
    // grayscale" (p.549-550); each only once its colour is set.
    std::optional<Keyer> secondary;
    std::optional<Keyer> spill;
    if (!luma && e.parameters.value(more).toObject().contains(QStringLiteral("Color controls"))) secondary = keyer(more, QString());
    if (!luma && e.parameters.value(more).toObject().contains(QStringLiteral("Spill Suppression Parameters: Color controls"))) {
        spill = keyer(more, QStringLiteral("Spill Suppression Parameters: "));
    }
    Picture work = layer;
    std::vector<double> matte(static_cast<std::size_t>(layer.width()) * static_cast<std::size_t>(layer.height()), 1.0);
    for (std::size_t i = 0; i < matte.size(); ++i) {
        float* px = &work.data()[i * 4];
        double rgb[3];
        colourOf(px, rgb);
        const Ycc p = toYcc(rgb[0], rgb[1], rgb[2]);
        double m = primary ? opacity(*primary, p) : 1.0;
        if (secondary) m = std::min(m, opacity(*secondary, p));
        matte[i] = m;
        if (spill) {
            // The nearer the spill colour, the greyer.
            const double keep = opacity(*spill, p);
            Ycc q = p;
            q.cb *= keep;
            q.cr *= keep;
            double out[3];
            fromYcc(q, out);
            for (int k = 0; k < 3; ++k) px[k] = static_cast<float>(std::clamp(out[k], 0.0, 1.0)) * px[3];
        }
    }
    return finish(e, work, std::move(matte), time, span);
}

// ---------------------------------------------------------------- RGB Keyer

Picture rgbKeyerPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    // "Replaces part of the video image with another video image based on color ... You can also perform
    // post-key color correction" (p.474). Gain "Adjusts the threshold of the key", Soft "Softens the edge",
    // Spill "Suppresses the spill from the key color onto foreground objects", Level "Controls
    // transparency of the foreground elements", Reverse "Inverts the key"; Blur "from (a blurring of 0
    // pixels) to 100 (a blurring of 10 pixels)"; Erode "Decreases ... the outer edges of the blurred
    // matte"; then Hue, Sat, Bright and Cont (p.550-551). Ours: the distance in RGB from the key colour,
    // keyed under Gain/100 of the largest with Soft/100 of a quarter blended; Spill takes that much of
    // the key colour's own channel down to the others' level; Erode/10 pixels taken off the matte's edge.
    const QString cat = QStringLiteral("RGB Keyer Parameters");
    const model::Rgb key = colour(e, cat, QStringLiteral("Key Color"), time, span).value_or(model::Rgb{0, 0, 1});
    const double threshold = std::clamp(num(e, cat, QStringLiteral("Gain"), time, span, 0.0), 0.0, 100.0) / 100.0;
    const double soft = std::clamp(num(e, cat, QStringLiteral("Soft"), time, span, 0.0), 0.0, 100.0) / 100.0 * 0.25;
    const double spill = std::clamp(num(e, cat, QStringLiteral("Spill"), time, span, 0.0), 0.0, 100.0) / 100.0;
    const double level = std::clamp(num(e, cat, QStringLiteral("Level"), time, span, 100.0), 0.0, 100.0) / 100.0;
    const double unit = layer.width() / 1920.0;
    const int blur = static_cast<int>(std::lround(std::clamp(num(e, cat, QStringLiteral("Blur"), time, span, 0.0), 0.0, 100.0) / 10.0 * unit));
    const int erode = static_cast<int>(std::lround(std::max(num(e, cat, QStringLiteral("Erode"), time, span, 0.0), 0.0) / 10.0 * unit));
    // The key colour's strongest channel is the one spill shows in.
    int spillChannel = 0;
    if (key.green > key.red && key.green >= key.blue) spillChannel = 1;
    else if (key.blue > key.red && key.blue > key.green) spillChannel = 2;
    Picture work = layer;
    std::vector<double> matte(static_cast<std::size_t>(layer.width()) * static_cast<std::size_t>(layer.height()), 1.0);
    for (std::size_t i = 0; i < matte.size(); ++i) {
        matte[i] = rgbKeyPixel(&work.data()[i * 4], key, threshold, soft, spill, spillChannel);
    }
    if (flag(e, cat, QStringLiteral("Reverse"))) {
        for (double& m : matte) m = 1.0 - m;
    }
    blurMatte(matte, layer.width(), layer.height(), blur, MatteBlur::Blur);
    if (erode > 0) blurMatte(matte, layer.width(), layer.height(), erode, MatteBlur::Erode);
    for (double& m : matte) m *= level;
    // Post-key colour correction, as the Color Effect does it.
    const QJsonObject params = e.parameters.value(cat).toObject();
    QJsonObject correction;
    const QStringList corrections = {QStringLiteral("Hue"), QStringLiteral("Sat"), QStringLiteral("Bright"), QStringLiteral("Cont")};
    for (const QString& name : corrections) {
        if (params.contains(name)) correction.insert(name, params.value(name));
    }
    if (!correction.isEmpty()) {
        const model::Effect color{QStringLiteral("Image"), QStringLiteral("Color Effect"), QJsonObject{{QStringLiteral("Color Effect Parameters"), correction}}};
        work = colorEffectPicture(color, work, time, span);
    }
    return applyMatte(work, matte, flag(e, cat, QStringLiteral("Show Alpha")));
}

// ---------------------------------------------------------------- SpectraMatte

Picture spectraMattePicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    // The key colour defines "a range of color values to use in the key. In the SpectraGraph display,
    // this range appears as a wedge keyed out of the CbCr swatch" (p.552): Tolerance is the wedge's angle
    // (default 90); Key Saturation moves its point out from the centre; Key Sat Line blunts the point;
    // Inner Softness (default 10) narrows the blend from the edges towards the key colour; Outer Softness
    // widens (positive) or narrows (negative) the blend outside; Alpha Offset shifts it (negative
    // expands the wedge); Opacity is the foreground's (p.554-556); Suppress Shadow raises darker pixels'
    // saturation for the key (p.557); Matte Blur blurs, erodes or dilates the matte (p.558); Spill moves
    // colours near the wedge to its safe side (p.559-561). Ours: the geometry below.
    const QString cat = QStringLiteral("SpectraMatte Parameters");
    if (flag(e, cat, QStringLiteral("Bypass"))) return layer; // "you see the original foreground image"
    const model::Rgb keyRgb = colour(e, cat, QStringLiteral("Key Color"), time, span).value_or(model::Rgb{0, 0, 1});
    const Ycc key = toYcc(keyRgb.red, keyRgb.green, keyRgb.blue);
    const double keyAngle = std::atan2(key.cr, key.cb);
    const double keySat = std::max(std::hypot(key.cb, key.cr), 1e-3);
    const double half = std::clamp(num(e, cat, QStringLiteral("Tolerance"), time, span, 90.0), 0.0, 180.0) / 2.0 * std::numbers::pi / 180.0;
    const double tip = std::clamp(num(e, cat, QStringLiteral("Key Saturation"), time, span, 0.0), 0.0, 100.0) / 100.0 * keySat;
    const double line = tip + std::clamp(num(e, cat, QStringLiteral("Key Sat Line"), time, span, 0.0), 0.0, 100.0) / 100.0 * (keySat - tip);
    const double inner = std::clamp(num(e, cat, QStringLiteral("Inner Softness"), time, span, 10.0), 0.0, 100.0);
    const double outer = std::clamp(num(e, cat, QStringLiteral("Outer Softness"), time, span, 0.0), -100.0, 100.0);
    const double offset = std::clamp(num(e, cat, QStringLiteral("Alpha Offset"), time, span, 0.0), -100.0, 100.0) / 100.0;
    const double opacity = std::clamp(num(e, cat, QStringLiteral("Opacity"), time, span, 100.0), 0.0, 100.0) / 100.0;
    const double shadow = std::clamp(num(e, cat, QStringLiteral("Suppress Shadow"), time, span, 0.0), 0.0, 100.0) / 100.0;
    const double spillSat = std::clamp(num(e, cat, QStringLiteral("Spill Saturation"), time, span, 0.0), 0.0, 100.0) / 100.0;
    const double spillAngle = std::clamp(num(e, cat, QStringLiteral("Spill Angle Offset"), time, span, 0.0), 0.0, 100.0) * std::numbers::pi / 180.0;
    // Where the blend starts (1 outside, full foreground) and ends (0, fully keyed), in units of how
    // far a colour is inside the wedge (0 at its edge, 1 at the key colour).
    const double end = std::max(1.0 - inner / 100.0 * 0.95, 0.02);
    const double start = std::min(-outer / 100.0 * 0.5, end - 0.01);
    Picture work = layer;
    std::vector<double> matte(static_cast<std::size_t>(layer.width()) * static_cast<std::size_t>(layer.height()), 1.0);
    for (std::size_t i = 0; i < matte.size(); ++i) {
        float* px = &work.data()[i * 4];
        double rgb[3];
        colourOf(px, rgb);
        Ycc p = toYcc(rgb[0], rgb[1], rgb[2]);
        double sat = std::hypot(p.cb, p.cr);
        double angle = std::atan2(p.cr, p.cb) - keyAngle;
        while (angle > std::numbers::pi) angle -= 2.0 * std::numbers::pi;
        while (angle < -std::numbers::pi) angle += 2.0 * std::numbers::pi;
        const double keySide = sat * (1.0 + shadow * (1.0 - std::clamp(p.y, 0.0, 1.0))); // Suppress Shadow
        // How far inside the wedge: across (its angle) and along (from the Key Sat Line to the key colour).
        const double across = half > 0 ? (half - std::abs(angle)) / half : -1.0;
        const double along = (keySide * std::cos(angle) - line) / std::max(keySat - line, 1e-3);
        const double u = std::min(across, along);
        const double m = std::clamp(1.0 - ramp(u, start, end) + offset, 0.0, 1.0);
        matte[i] = m * opacity;
        // Spill: colours in the zone around the wedge turn to its edge, on the safe side.
        const double zone = half + spillAngle;
        if ((spillSat > 0 || spillAngle > 0) && m > 0 && std::abs(angle) < zone && sat >= tip * (1.0 - spillSat)) {
            const double to = (angle < 0 ? -zone : zone) + keyAngle;
            p.cb = sat * std::cos(to);
            p.cr = sat * std::sin(to);
            double out[3];
            fromYcc(p, out);
            for (int k = 0; k < 3; ++k) px[k] = static_cast<float>(std::clamp(out[k], 0.0, 1.0)) * px[3];
        }
    }
    const int blur = static_cast<int>(std::lround(std::clamp(num(e, cat, QStringLiteral("Matte Blur"), time, span, 0.0), 0.0, 100.0) / 10.0 * layer.width() / 1920.0));
    const QString mode = e.parameters.value(cat).toObject().value(QStringLiteral("Matte Blur mode")).toString();
    blurMatte(matte, layer.width(), layer.height(), blur,
              mode == QLatin1String("Erode") ? MatteBlur::Erode : mode == QLatin1String("Dilate") ? MatteBlur::Dilate : MatteBlur::Blur);
    return applyMatte(work, matte, flag(e, cat, QStringLiteral("Show Alpha")));
}

} // namespace lumenis::media::detail
