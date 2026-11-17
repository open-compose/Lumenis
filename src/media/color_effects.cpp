#include "effect_drawing.h"
#include "parallel.h"

#include "lumenis/model/safe_color_limiter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

// The Image effects that change a picture's colours or detail (ROADMAP 6.3b2;
// docs/spec/image-effects.md): the Color Effect (FX Guide p.469, p.533-536), and the Blur Effect and
// Mosaic Effect (p.468, p.532-536) over the whole picture until Intraframe objects (6.9) can choose an
// area. imageEffectPicture (image_effects.cpp) calls these.
namespace lumenis::media::detail {

namespace {

double value(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span, double fallback) {
    const QJsonValue d = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(d.isDouble() ? d.toDouble() : fallback);
}

// Video code values (FX Guide p.533-535 speak in 8-bit values, black 16 and white 235) and our 0 to 1.
constexpr double kBlack = 16.0;
constexpr double kWhite = 235.0;
double toCode(double v) { return kBlack + v * (kWhite - kBlack); }
double fromCode(double c) { return (c - kBlack) / (kWhite - kBlack); }

// Rec. 709 luma weights, for the luma and chroma adjustments.
constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;

// A separable box blur of radius r (in pixels) along x or y, three times over for a near-Gaussian; edges are held, so
// a plain picture stays plain. Lines are shared out among threads (10.1b), and down the picture each thread takes
// `kColumns` columns at a time into a buffer of its own, so it reads whole cache lines instead of one pixel a row; the
// sums are the same as one line at a time, value for value.
void boxBlur(Picture& p, int r, bool horizontal) {
    if (r <= 0) return;
    const int w = p.width();
    const int h = p.height();
    if (w <= 0 || h <= 0) return;
    const int n = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    constexpr int kColumns = 16;
    const int group = horizontal ? 1 : kColumns;
    const std::size_t groups = static_cast<std::size_t>((lines + group - 1) / group);
    const double scale = 1.0 / (2.0 * r + 1.0);
    float* data = p.data().data();
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    // Where value c of pixel i of line l is.
    auto offset = [&](int l, int i) {
        return horizontal ? static_cast<std::size_t>(l) * stride + static_cast<std::size_t>(i) * 4
                          : static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(l) * 4;
    };
    parallelRange(groups, [&](std::size_t from, std::size_t to) {
        const std::size_t width = static_cast<std::size_t>(group) * 4; // values of one i across the group's lines
        std::vector<float> line(static_cast<std::size_t>(n) * width);
        std::vector<double> sum(width);
        for (std::size_t gi = from; gi < to; ++gi) {
            const int first = static_cast<int>(gi) * group;
            const int in = std::min(group, lines - first);
            const std::size_t used = static_cast<std::size_t>(in) * 4;
            for (int pass = 0; pass < 3; ++pass) {
                for (int i = 0; i < n; ++i) {
                    // The group's lines side by side: in the vertical case `in` neighbouring pixels of row i.
                    std::copy_n(data + offset(first, i), used, &line[static_cast<std::size_t>(i) * width]);
                }
                auto at = [&](int i) { return &line[static_cast<std::size_t>(std::clamp(i, 0, n - 1)) * width]; };
                std::fill(sum.begin(), sum.end(), 0.0);
                for (int i = -r; i <= r; ++i) {
                    const float* v = at(i);
                    for (std::size_t k = 0; k < used; ++k) sum[k] += static_cast<double>(v[k]);
                }
                // One output and the window moved on, for pixel i; the sum times the window's reciprocal (a division
                // per value was a third of the blur's time).
                auto step = [&](int i, const float* add, const float* drop) {
                    float* o = data + offset(first, i);
                    for (std::size_t k = 0; k < used; ++k) {
                        o[k] = static_cast<float>(sum[k] * scale);
                        sum[k] += static_cast<double>(add[k]) - static_cast<double>(drop[k]);
                    }
                };
                // The edges, where the window reaches past the line and is held at its end, apart from the middle,
                // where it does not and needs no clamping.
                const int lo = std::min(r, n);
                const int hi = std::max(lo, n - r - 1);
                for (int i = 0; i < lo; ++i) step(i, at(i + r + 1), at(i - r));
                if (lo < hi) {
                    const float* add = line.data() + static_cast<std::size_t>(lo + r + 1) * width;
                    const float* drop = line.data() + static_cast<std::size_t>(lo - r) * width;
                    for (int i = lo; i < hi; ++i, add += width, drop += width) step(i, add, drop);
                }
                for (int i = hi; i < n; ++i) step(i, at(i + r + 1), at(i - r));
            }
        }
    }, 1);
}

} // namespace

void blurPicture(Picture& picture, int rx, int ry) {
    boxBlur(picture, rx, true);
    boxBlur(picture, ry, false);
}

Picture safeColorLimiterPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    const model::SafeLimiting limiting = model::safeColorLimiting(e, time, span);
    if (!limiting.compositeLuma && !limiting.rgb) return layer;
    Picture out = layer;
    PixelData& data = out.data();
    for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
        const float a = data[i + 3];
        if (a <= 0.0f) continue;
        std::array<double, 3> levels;
        for (std::size_t k = 0; k < 3; ++k) levels[k] = 16.0 + 219.0 * static_cast<double>(data[i + k] / a);
        const std::array<double, 3> limited = model::limitSafeLevels(levels, limiting);
        for (std::size_t k = 0; k < 3; ++k) data[i + k] = static_cast<float>(std::clamp((limited[k] - 16.0) / 219.0, 0.0, 1.0)) * a;
    }
    return out;
}

Picture colorEffectPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    const QString cat = QStringLiteral("Color Effect Parameters");
    auto num = [&](const char* name, double fallback) { return value(e, cat, QLatin1String(name), time, span, fallback); };
    auto flag = [&](const QString& name) { return e.parameters.value(cat).toObject().value(name).toBool(); };
    const double bright = num("Bright", 0.0);
    const double cont = num("Cont", 0.0);
    const bool lumaInvert = flag(QStringLiteral("Invert"));
    const bool fullRange = e.parameters.value(cat).toObject().value(QStringLiteral("Fast Menu")).toString() == QLatin1String("0 to 255");
    const double whitePoint = num("W Point", 235.0);
    const double blackPoint = num("B Point", 16.0);
    const double gamma = num("Gamma", 0.0);
    const double high = num("High", 235.0);
    const double low = num("Low", 16.0);
    const double hue = num("Hue", 0.0);
    const double sat = num("Sat", 0.0);
    const bool chromaInvert = flag(QStringLiteral("Chroma Adjust Parameters: Invert"));
    const double post = std::clamp(num("Post", 0.0), 0.0, 25.0);
    const double solar = std::clamp(num("Solar", 0.0), 0.0, 255.0);
    const double gain[3] = {num("Red", 100.0) / 100.0, num("Green", 100.0) / 100.0, num("Blue", 100.0) / 100.0};
    // Ours, where the guide gives only the ends of a range: Bright moves by up to half the range;
    // Cont scales about the middle grey, up to 100 times at +100 and to flat at -100; Gamma's
    // exponent is 2^(-Gamma/50); Post keeps round(2 + 254 (1 - Post/25)^3) steps a channel; Hue turns
    // the chroma (Cb, Cr) by its degrees; Sat scales it by 1 + Sat/100.
    const double contrast = cont >= 0 ? 1.0 / std::max(1.0 - cont / 100.0, 0.01) : 1.0 + cont / 100.0;
    const double exponent = std::pow(2.0, -gamma / 50.0);
    const double steps = std::round(2.0 + 254.0 * std::pow(1.0 - post / 25.0, 3.0));
    const double angle = hue * std::numbers::pi / 180.0;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    // Luma Range: "16 to 235 — This option is the default for video images"; "0 to 255 — ... expand a
    // high-contrast image to the full dynamic range" (p.534). The black and white points map to the
    // range's ends.
    const double rangeLow = fullRange ? 0.0 : 16.0;
    const double rangeHigh = fullRange ? 255.0 : 235.0;
    const double span_ = std::max(whitePoint - blackPoint, 1.0);

    Picture out = layer;
    for (std::size_t i = 0; i < out.data().size(); i += 4) {
        float* px = &out.data()[i];
        const float alpha = px[3];
        if (alpha <= 0.0f) continue;
        double c[3];
        for (int k = 0; k < 3; ++k) c[k] = toCode(static_cast<double>(px[k] / alpha));
        for (double& v : c) {
            // Luma Range: black and white points (p.534), then Gamma about them.
            double t = std::clamp((v - blackPoint) / span_, 0.0, 1.0);
            t = std::pow(t, exponent);
            v = rangeLow + t * (rangeHigh - rangeLow);
            // Luma Adjust (p.533).
            v = 125.5 + (v - 125.5) * contrast + bright / 100.0 * 109.5;
        }
        double y = kR * c[0] + kG * c[1] + kB * c[2];
        // Luma Adjust's Invert: "Reverses the image's brightness" — the luma turned over, the chroma kept.
        if (lumaInvert) {
            const double inverted = kWhite + kBlack - y;
            for (double& v : c) v += inverted - y;
            y = inverted;
        }
        // Chroma Adjust (p.535): Hue, Sat and Invert on the colour difference.
        double cb = c[2] - y;
        double cr = c[0] - y;
        const double scale = 1.0 + sat / 100.0;
        double ncb = (cb * cosA - cr * sinA) * scale;
        double ncr = (cb * sinA + cr * cosA) * scale;
        if (chromaInvert) {
            ncb = -ncb;
            ncr = -ncr;
        }
        c[0] = y + ncr;
        c[2] = y + ncb;
        c[1] = (y - kR * c[0] - kB * c[2]) / kG;
        // Luma Clip (p.535): "No pixel in the image can be brighter than the High value" or darker
        // than the Low one.
        for (double& v : c) v = std::clamp(v, low, high);
        // Color Style (p.535-536): Post limits the steps; Solar inverts values above a threshold, "0 is
        // normal luminance and 255 inverts all luminance values". Ours: both within black to white, the
        // threshold 255 - Solar of the way through it.
        for (double& v : c) {
            double f = std::clamp(fromCode(v), 0.0, 1.0);
            if (post > 0) f = std::round(f * (steps - 1.0)) / (steps - 1.0);
            if (solar > 0 && f * 255.0 > 255.0 - solar) f = 1.0 - f;
            v = toCode(f);
        }
        // Color Gain (p.536): 100 leaves a channel unchanged.
        for (int k = 0; k < 3; ++k) {
            const double v = std::clamp(fromCode(c[k] * gain[k] + kBlack * (1.0 - gain[k])), 0.0, 1.0);
            px[k] = static_cast<float>(v) * alpha;
        }
    }
    return out;
}

Picture detailEffectPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    // Blur: "Opac. (opacity) Controls the relative strength of the blur. Values range from 100 (maximum
    // blur) to 0 (minimum blur)"; Hor and Vert "Defines the horizontal and vertical dimensions" (p.533).
    // Mosaic: the same with tiles (p.536). Ours: the whole picture; Hor and Vert are the blur's radius
    // or the tiles' size in pixels of a picture 1920 wide; Opac. mixes the result with the picture.
    const bool blur = e.name == QLatin1String("Blur Effect");
    const QString cat = blur ? QStringLiteral("Blur Effect Blur Parameters") : QStringLiteral("Mosaic Effect Mosaic Parameters");
    const double unit = layer.width() / 1920.0;
    const double opacity = std::clamp(value(e, cat, QStringLiteral("Opac."), time, span, 100.0), 0.0, 100.0) / 100.0;
    const double hor = std::max(value(e, cat, QStringLiteral("Hor"), time, span, 10.0), 0.0) * unit;
    const double vert = std::max(value(e, cat, QStringLiteral("Vert"), time, span, 10.0), 0.0) * unit;
    Picture done = layer;
    if (blur) {
        boxBlur(done, static_cast<int>(std::lround(hor)), true);
        boxBlur(done, static_cast<int>(std::lround(vert)), false);
    } else {
        const int tw = std::max(1, static_cast<int>(std::lround(hor)));
        const int th = std::max(1, static_cast<int>(std::lround(vert)));
        // Rows of tiles on threads (10.1b): each writes only its own tiles.
        const std::size_t rows = static_cast<std::size_t>((layer.height() + th - 1) / th);
        parallelRange(rows, [&](std::size_t fromRow, std::size_t toRow) {
        for (int ty = static_cast<int>(fromRow) * th; ty < static_cast<int>(toRow) * th && ty < layer.height(); ty += th) {
            for (int tx = 0; tx < layer.width(); tx += tw) {
                double sum[4] = {0, 0, 0, 0};
                int count = 0;
                const int x1 = std::min(tx + tw, layer.width());
                const int y1 = std::min(ty + th, layer.height());
                for (int y = ty; y < y1; ++y) {
                    for (int x = tx; x < x1; ++x) {
                        for (int c = 0; c < 4; ++c) sum[c] += static_cast<double>(layer.pixel(x, y)[c]);
                        ++count;
                    }
                }
                for (int y = ty; y < y1; ++y) {
                    for (int x = tx; x < x1; ++x) {
                        for (int c = 0; c < 4; ++c) done.pixel(x, y)[c] = static_cast<float>(sum[c] / count);
                    }
                }
            }
        }
        }, 1);
    }
    return opacity >= 1.0 ? done : mix(layer, done, opacity);
}

} // namespace lumenis::media::detail
