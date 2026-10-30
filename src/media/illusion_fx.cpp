#include "effect_drawing.h"

#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// The Illusion FX segment effects (ROADMAP 6.3d2-3; docs/spec/illusion-fx.md): those that colour,
// texture or draw (Color Mix, Film Grain, Crystal, Flare, Pattern Generator, Lightning, Sparkler,
// Kaleidoscope, Motion Blur) and the distortions (Pinch, Radial Blur, Ripple, Sphere, Swirl, Twist,
// Wave) with the Illusion FX Border (FX Guide p.464-465, parameters p.508-527). The reference says
// what each control does but rarely its range, and never a formula: the ranges are ours ("ours" in
// effect_parameters.json), the values until set are in parameterDefault(), and the pictures below are
// ours too. Random effects are the same for the same Random Seed on every platform (our own hash).
namespace lumenis::media::detail {

namespace {

constexpr double kPi = std::numbers::pi;

double num(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    const QJsonValue d = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(d.isDouble() ? d.toDouble() : 0.0);
}

QColor colourOf(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    if (const auto c = model::colorAt(e, category, name, time, span)) {
        return QColor::fromRgbF(static_cast<float>(c->red), static_cast<float>(c->green), static_cast<float>(c->blue));
    }
    const QColor d(parameterDefault(e, category, name).toString());
    return d.isValid() ? d : QColor(Qt::white);
}

QString text(const model::Effect& e, const QString& category, const QString& name) {
    const QJsonValue v = e.parameters.value(category).toObject().value(name);
    return v.isString() ? v.toString() : parameterDefault(e, category, name).toString();
}

// The frame of the effect `time` is at, counted from its start.
std::int64_t frameOf(double time, const model::EffectSpan& span) {
    return span.length > 1 ? static_cast<std::int64_t>(std::lround(time * static_cast<double>(span.length - 1))) : 0;
}

// A hash of integers to 0..1, the same everywhere.
double hash01(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t seed) {
    // (Written out, not looped over a braced list: MSVC 14.44 has failed on such loops; BUILDING.md.)
    std::uint64_t h = 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(seed);
    auto mixIn = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    };
    mixIn(a);
    mixIn(b);
    mixIn(c);
    return static_cast<double>(h >> 11) / 9007199254740992.0; // 2^53
}

// A small random generator for the drawn effects (lightning, sparks), the same everywhere.
struct Random {
    std::uint64_t state;
    explicit Random(std::uint64_t seed) : state(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    double next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / 9007199254740992.0;
    }
};

// A checkbox, `fallback` until set.
bool flag(const model::Effect& e, const QString& category, const QString& name, bool fallback) {
    const QJsonValue v = e.parameters.value(category).toObject().value(name);
    return v.isBool() ? v.toBool() : fallback;
}

// The Illusion FX Border's Left, Right, Top and Bottom (p.508), "to prevent black edges of the source
// clip from warping into the effect": ours, each insets the source by percent of the picture (0 to 50),
// and a sample is held inside what is left.
struct SourceBox {
    double left, right, top, bottom;
    static SourceBox of(const model::Effect& e, double time, const model::EffectSpan& span, double W, double H) {
        const QString border = QStringLiteral("Border Parameter for Illusion FX Effects");
        auto inset = [&](const char* side) { return std::clamp(num(e, border, QLatin1String(side), time, span), 0.0, 50.0) / 100.0; };
        return {inset("Left") * W + 0.5, W - inset("Right") * W - 0.5, inset("Top") * H + 0.5, H - inset("Bottom") * H - 0.5};
    }
    double x(double v) const { return std::clamp(v, left, std::max(left, right)); }
    double y(double v) const { return std::clamp(v, top, std::max(top, bottom)); }
};

// The pixel of `p` nearest to (x, y), for Anti-alias off ("whether or not transformed areas are
// smoothed").
void nearest(const Picture& p, double x, double y, float out[4]) {
    const int ix = std::clamp(static_cast<int>(std::floor(x)), 0, p.width() - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(y)), 0, p.height() - 1);
    std::copy(p.pixel(ix, iy), p.pixel(ix, iy) + 4, out);
}

// A distortion: each pixel of the result takes the source at map(x, y) (pixel centres), held inside
// the Border's box, smoothed or not. `map` returns false where nothing shows (transparent).
template <typename Map>
Picture warp(const Picture& layer, const SourceBox& box, bool antiAlias, Map map) {
    Picture out(layer.width(), layer.height());
    float in[4];
    for (int y = 0; y < layer.height(); ++y) {
        for (int x = 0; x < layer.width(); ++x) {
            double sx = x + 0.5;
            double sy = y + 0.5;
            if (!map(sx, sy)) continue;
            if (antiAlias) {
                sample(layer, box.x(sx), box.y(sy), in);
            } else {
                nearest(layer, box.x(sx), box.y(sy), in);
            }
            std::copy(in, in + 4, out.pixel(x, y));
        }
    }
    return out;
}

// Adds `light` (premultiplied) to `picture`, as light adds: each channel capped at 1, the alpha the
// larger of the two.
void addLight(Picture& picture, const QImage& light) {
    const Picture l = Picture::fromImage(light, picture.width(), picture.height());
    auto& o = picture.data();
    const auto& a = l.data();
    for (std::size_t i = 0; i < o.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) o[i + c] = std::min(o[i + c] + a[i + c], 1.0f);
        o[i + 3] = std::max(o[i + 3], a[i + 3]);
    }
}

} // namespace

bool isIllusionSegmentEffect(const model::Effect& e) {
    if (e.category != QLatin1String("Illusion FX")) return false;
    static const QStringList names = {QStringLiteral("Color Mix"),         QStringLiteral("Film Grain"), QStringLiteral("Crystal"),
                                      QStringLiteral("Flare"),             QStringLiteral("Pattern Generator"),
                                      QStringLiteral("Lightning"),         QStringLiteral("Sparkler"),   QStringLiteral("Kaleidoscope"),
                                      QStringLiteral("Motion Blur"),       QStringLiteral("Pinch"),      QStringLiteral("Radial Blur"),
                                      QStringLiteral("Ripple"),            QStringLiteral("Sphere"),     QStringLiteral("Swirl"),
                                      QStringLiteral("Twist"),             QStringLiteral("Wave")};
    return names.contains(e.name);
}

QJsonValue illusionDefault(const model::Effect& e, const QString& category, const QString& name) {
    // The values until set: ours, chosen so each effect shows itself when applied.
    if (category == QLatin1String("Color Mix Effect Input Parameters")) {
        if (name == QLatin1String("Color Level")) return 50;
        if (name == QLatin1String("Color controls")) return QStringLiteral("#ff8000");
    }
    if (category == QLatin1String("Film Grain Effect Input Parameters")) {
        if (name == QLatin1String("Level")) return 30;
        if (name == QLatin1String("Size")) return 2;
        if (name == QLatin1String("Color") || name == QLatin1String("Hilite Grain") || name == QLatin1String("Shadow Grain")) return 50;
    }
    if (category == QLatin1String("Crystal Effect Input Parameters")) {
        if (name == QLatin1String("Size")) return 20;
        if (name == QLatin1String("Jitter")) return 50;
    }
    if (category == QLatin1String("Flare Effect Input Parameters")) {
        if (name == QLatin1String("Brightness")) return 70;
        if (name == QLatin1String("Size")) return 30;
        if (name == QLatin1String("Width")) return 10;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
    }
    if (category == QLatin1String("Kaleidoscope Effect Input, Center, and Offset Parameters")) {
        if (name == QLatin1String("Symmetry")) return 6;
        if (name == QLatin1String("Scale")) return 100;
        if (name.endsWith(QLatin1String("X")) || name.endsWith(QLatin1String("Y"))) return 50;
    }
    if (category == QLatin1String("Lightning Effect Generation, Core, and Glow Parameters")) {
        if (name == QLatin1String("Amount")) return 100;
        if (name == QLatin1String("Branch Probability")) return 30;
        if (name == QLatin1String("Width")) return 2;
        if (name == QLatin1String("Color controls")) return QStringLiteral("#ffffff");
        if (name == QLatin1String("Glow Parameters: Width")) return 10;
        if (name == QLatin1String("Glow Parameters: Color controls")) return QStringLiteral("#8080ff");
        if (name == QLatin1String("Render Mode")) return QStringLiteral("Anti-aliased");
    }
    if (category == QLatin1String("Motion Blur Effect Input Parameters")) {
        if (name == QLatin1String("Amount")) return 20;
    }
    if (category == QLatin1String("Pattern Generator Effect Background, Number, and Circle Parameters")) {
        if (name == QLatin1String("Pattern Type")) return QStringLiteral("Color bars (75%)");
        if (name == QLatin1String("Axis")) return QStringLiteral("X axis");
        if (name == QLatin1String("Color controls")) return QStringLiteral("#808080");
        if (name == QLatin1String("Text Size")) return 72;
        if (name.endsWith(QLatin1String("Color controls"))) return QStringLiteral("#ffffff");
        if (name.endsWith(QLatin1String("X")) || name.endsWith(QLatin1String("Y"))) return 50;
        if (name == QLatin1String("Size")) return 20;
        if (name == QLatin1String("Width")) return 2;
    }
    if (category == QLatin1String("Sparkler Effect Generation, Movement, Core, and Glow Parameters")) {
        if (name == QLatin1String("Birth Rate")) return 20;
        if (name == QLatin1String("Lifetime")) return 30;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
        if (name == QLatin1String("Length")) return 1;
        if (name == QLatin1String("Speed") || name == QLatin1String("Gravity")) return 10;
        if (name == QLatin1String("Core Radius")) return 2;
        if (name == QLatin1String("Glow Radius")) return 6;
        if (name == QLatin1String("Color controls")) return QStringLiteral("#ffcc66");
    }
    if (category == QLatin1String("Pinch Effect Input Parameters")) {
        if (name == QLatin1String("Amount")) return 50;
        if (name == QLatin1String("Size")) return 60;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
    }
    if (category == QLatin1String("Radial Blur Effect Input Parameters")) {
        if (name == QLatin1String("Zoom")) return 20;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
        if (name == QLatin1String("Render Mode")) return QStringLiteral("Render Medium Quality");
    }
    if (category == QLatin1String("Ripple Effect Motion, Generation, and Illumination Parameters")) {
        if (name == QLatin1String("Amount") || name == QLatin1String("Wave Speed")) return 50;
        if (name == QLatin1String("Ripple Spread") || name == QLatin1String("Strength")) return 30;
        if (name == QLatin1String("Length")) return 40;
        if (name == QLatin1String("Number of Ripples")) return 3;
        if (name == QLatin1String("Color controls")) return QStringLiteral("#ffffff");
        if (name == QLatin1String("Angle")) return 45;
    }
    if (category == QLatin1String("Sphere Effect Input Parameters") || category == QLatin1String("Swirl Effect Input Parameters")) {
        if (name == QLatin1String("Amount")) return 50;
        if (name == QLatin1String("Size")) return 60;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
    }
    if (category == QLatin1String("Twist Effect Input Parameters")) {
        if (name == QLatin1String("Amount")) return 30;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
        if (name == QLatin1String("Axis")) return QStringLiteral("X axis");
    }
    if (category == QLatin1String("Wave Effect Input Parameters")) {
        if (name == QLatin1String("Amount")) return 50;
        if (name == QLatin1String("Minimum Size")) return 40;
        if (name == QLatin1String("Maximum Size")) return 160;
        if (name == QLatin1String("Horizontal Factor") || name == QLatin1String("Vertical Factor")) return 50;
        if (name == QLatin1String("Number of Waves")) return 3;
    }
    (void)e;
    return {};
}

Picture illusionSegmentPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    const int width = layer.width();
    const int height = layer.height();
    if (layer.isNull()) return layer;
    const double W = width;
    const double H = height;
    const double unit = W / 1920.0; // pixels of a picture 1920 wide, in this picture's pixels
    const std::int64_t frame = frameOf(time, span);
    const QString& name = e.name;

    if (name == QLatin1String("Color Mix")) {
        // "Color Mix lets you blend a solid color with the image": Color Level "0 (no color) to 100
        // (maximum color)" (p.464, p.508).
        const QString cat = QStringLiteral("Color Mix Effect Input Parameters");
        const float level = static_cast<float>(std::clamp(num(e, cat, QStringLiteral("Color Level"), time, span), 0.0, 100.0) / 100.0);
        const QColor c = colourOf(e, cat, QStringLiteral("Color controls"), time, span);
        const float rgb[3] = {c.redF(), c.greenF(), c.blueF()};
        Picture out = layer;
        auto& o = out.data();
        for (std::size_t i = 0; i < o.size(); i += 4) {
            for (std::size_t k = 0; k < 3; ++k) o[i + k] += (rgb[k] * o[i + 3] - o[i + k]) * level;
        }
        return out;
    }

    if (name == QLatin1String("Film Grain")) {
        // "adds a grainy texture": Level the amount, Size the grain, Color more colour (higher) or more
        // luminance (lower) imperfections, Hilite and Shadow Grain grain in the brighter and darker
        // areas; the grain changes every frame (Field Mode: every field; ours are progressive) (p.509).
        const QString cat = QStringLiteral("Film Grain Effect Input Parameters");
        const double level = std::clamp(num(e, cat, QStringLiteral("Level"), time, span), 0.0, 100.0) / 100.0 * 0.25;
        const double size = std::max(num(e, cat, QStringLiteral("Size"), time, span) * unit, 1.0);
        const double colour = std::clamp(num(e, cat, QStringLiteral("Color"), time, span), 0.0, 100.0) / 100.0;
        const double hilite = std::clamp(num(e, cat, QStringLiteral("Hilite Grain"), time, span), 0.0, 100.0) / 100.0;
        const double shadow = std::clamp(num(e, cat, QStringLiteral("Shadow Grain"), time, span), 0.0, 100.0) / 100.0;
        Picture out = layer;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float* px = out.pixel(x, y);
                const double a = static_cast<double>(px[3]);
                if (a <= 0) continue;
                const auto gx = static_cast<std::int64_t>(std::floor(x / size));
                const auto gy = static_cast<std::int64_t>(std::floor(y / size));
                double rgb[3];
                for (int k = 0; k < 3; ++k) rgb[k] = static_cast<double>(px[k]) / a;
                const double luma = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
                const double weight = shadow * (1.0 - luma) + hilite * luma;
                const double lumaNoise = hash01(gx, gy, frame, 1) * 2.0 - 1.0;
                for (int k = 0; k < 3; ++k) {
                    const double colourNoise = hash01(gx, gy, frame, 2 + k) * 2.0 - 1.0;
                    const double n = (1.0 - colour) * lumaNoise + colour * colourNoise;
                    px[k] = static_cast<float>(std::clamp(rgb[k] + n * level * weight * 2.0, 0.0, 1.0) * a);
                }
            }
        }
        return out;
    }

    if (name == QLatin1String("Crystal")) {
        // "uses polygons to create a mosaic effect": Size "the radius of the polygons in pixels"; Jitter
        // "A value of 0 creates regular hexagons — the higher the value, the more random the shapes";
        // Random Seed (p.509). Ours: each polygon takes the colour under its centre, held inside the
        // Illusion FX Border.
        const QString cat = QStringLiteral("Crystal Effect Input Parameters");
        const double r = std::max(num(e, cat, QStringLiteral("Size"), time, span) * unit, 1.0);
        const double jitter = std::clamp(num(e, cat, QStringLiteral("Jitter"), time, span), 0.0, 100.0) / 100.0;
        const auto seed = static_cast<std::int64_t>(num(e, cat, QStringLiteral("Random Seed"), time, span));
        const double dx = std::sqrt(3.0) * r;
        const double dy = 1.5 * r;
        auto centre = [&](std::int64_t row, std::int64_t col, double& cx, double& cy) {
            cx = static_cast<double>(col) * dx + ((row & 1) ? dx / 2.0 : 0.0) + (hash01(row, col, 0, seed) - 0.5) * jitter * r;
            cy = static_cast<double>(row) * dy + (hash01(row, col, 1, seed) - 0.5) * jitter * r;
        };
        const SourceBox box = SourceBox::of(e, time, span, W, H); // the Border (p.465, p.508)
        Picture out(width, height);
        float in[4];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double px = x + 0.5;
                const double py = y + 0.5;
                const auto row = static_cast<std::int64_t>(std::floor(py / dy));
                const auto col = static_cast<std::int64_t>(std::floor(px / dx));
                double best = 1e18;
                double bx = px;
                double by = py;
                for (std::int64_t rr = row - 1; rr <= row + 1; ++rr) {
                    for (std::int64_t cc = col - 2; cc <= col + 1; ++cc) {
                        double cx = 0;
                        double cy = 0;
                        centre(rr, cc, cx, cy);
                        const double d = (cx - px) * (cx - px) + (cy - py) * (cy - py);
                        if (d < best) {
                            best = d;
                            bx = cx;
                            by = cy;
                        }
                    }
                }
                sample(layer, box.x(bx), box.y(by), in);
                std::copy(in, in + 4, out.pixel(x, y));
            }
        }
        return out;
    }

    if (name == QLatin1String("Motion Blur")) {
        // "Amount: Sets the length of the blur. Direction: ... A value of 0 degrees is 3 o'clock. Positive
        // values move counterclockwise" (p.515). Ours: Amount in pixels of a picture 1920 wide, the blur
        // centred on each pixel.
        const QString cat = QStringLiteral("Motion Blur Effect Input Parameters");
        const double length = std::max(num(e, cat, QStringLiteral("Amount"), time, span), 0.0) * unit;
        const double angle = num(e, cat, QStringLiteral("Direction"), time, span) * kPi / 180.0;
        const int taps = std::clamp(static_cast<int>(std::lround(length)), 1, 64);
        if (taps <= 1) return layer;
        const double ux = std::cos(angle) * length / (taps - 1);
        const double uy = -std::sin(angle) * length / (taps - 1);
        Picture out(width, height);
        float in[4];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float sum[4] = {0, 0, 0, 0};
                for (int t = 0; t < taps; ++t) {
                    const double k = t - (taps - 1) / 2.0;
                    sample(layer, std::clamp(x + 0.5 + k * ux, 0.5, W - 0.5), std::clamp(y + 0.5 + k * uy, 0.5, H - 0.5), in);
                    for (int c = 0; c < 4; ++c) sum[c] += in[c];
                }
                float* o = out.pixel(x, y);
                for (int c = 0; c < 4; ++c) o[c] = sum[c] / static_cast<float>(taps);
            }
        }
        return out;
    }

    if (name == QLatin1String("Kaleidoscope")) {
        // "an animation that resembles a kaleidoscope": Symmetry "the number of mirrors", Angle "the angle
        // of the mirrors", Scale; the mirrors' Center X and Y; the Offset "from the original source area"
        // (p.513); Border's Left, Right, Top and Bottom "prevent black edges of the source clip from
        // warping into the effect" (p.508). Ours: the picture folded into Symmetry wedges, each mirrored;
        // positions in percent, Offset's 50 none; Border insets the source by percent.
        const QString cat = QStringLiteral("Kaleidoscope Effect Input, Center, and Offset Parameters");
        const int mirrors = std::max(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Symmetry"), time, span))), 1);
        const double angle = num(e, cat, QStringLiteral("Angle"), time, span) * kPi / 180.0;
        const double scale = std::max(num(e, cat, QStringLiteral("Scale"), time, span), 1.0) / 100.0;
        const double cx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double cy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        const double ox = (num(e, cat, QStringLiteral("Offset Parameters: X"), time, span) - 50.0) / 100.0 * W;
        const double oy = (num(e, cat, QStringLiteral("Offset Parameters: Y"), time, span) - 50.0) / 100.0 * H;
        const SourceBox box = SourceBox::of(e, time, span, W, H);
        const double wedge = 2.0 * kPi / mirrors;
        Picture out(width, height);
        float in[4];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double rx = x + 0.5 - cx;
                const double ry = y + 0.5 - cy;
                const double r = std::hypot(rx, ry) / scale;
                double a = std::atan2(ry, rx) - angle;
                a = std::fmod(a, wedge);
                if (a < 0) a += wedge;
                if (mirrors > 1 && a > wedge / 2.0) a = wedge - a;
                a += angle;
                const double sx = box.x(cx + ox + r * std::cos(a));
                const double sy = box.y(cy + oy + r * std::sin(a));
                sample(layer, sx, sy, in);
                std::copy(in, in + 4, out.pixel(x, y));
            }
        }
        return out;
    }

    if (name == QLatin1String("Pinch") || name == QLatin1String("Sphere") || name == QLatin1String("Swirl")) {
        // Pinch "pinches the image in toward, or pushes it out from a user-defined point"; Sphere "a
        // spherical distortion" ("Negative values distort the image inward. Positive values distort it
        // outward"); Swirl "swirling the pixels within a circular area" ("Negative values swirl the image
        // counterclockwise. Positive values swirl it clockwise") (p.464, p.521, p.526-527). Size is the
        // circle's diameter: percent of the picture's height (ours, for the reference's "normalized
        // proportion of the vertical size"). Ours: Amount -100 to 100, the source radius
        // d(1 + k(1 - d)) for Pinch (k = Amount/100, d the distance over the radius: a pull toward the
        // centre for k > 0), d(1 - k sqrt(1 - d^2)/2) for Sphere (a lens), and Swirl turning by up to
        // k whole turns at the centre, easing to none at the circle's edge.
        const QString cat = name + QStringLiteral(" Effect Input Parameters");
        const double k = std::clamp(num(e, cat, QStringLiteral("Amount"), time, span), -100.0, 100.0) / 100.0;
        const double radius = std::max(num(e, cat, QStringLiteral("Size"), time, span), 0.0) / 200.0 * H;
        const double cx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double cy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        const int kind = name == QLatin1String("Pinch") ? 0 : name == QLatin1String("Sphere") ? 1 : 2;
        return warp(layer, SourceBox::of(e, time, span, W, H), flag(e, cat, QStringLiteral("Anti-alias"), true),
                    [=](double& x, double& y) {
                        const double dx = x - cx;
                        const double dy = y - cy;
                        const double r = std::hypot(dx, dy);
                        if (radius <= 0 || r >= radius || r <= 0) return true;
                        const double d = r / radius;
                        if (kind == 2) {
                            // Clockwise on the screen (y down) is the positive angle: the source is
                            // turned back by it.
                            const double turn = -k * 2.0 * kPi * (1.0 - d) * (1.0 - d);
                            const double c = std::cos(turn);
                            const double s = std::sin(turn);
                            x = cx + dx * c - dy * s;
                            y = cy + dx * s + dy * c;
                            return true;
                        }
                        const double scale = kind == 0 ? 1.0 + k * (1.0 - d) : 1.0 - k * std::sqrt(1.0 - d * d) / 2.0;
                        x = cx + dx * scale;
                        y = cy + dy * scale;
                        return true;
                    });
    }

    if (name == QLatin1String("Twist")) {
        // "Twist distorts the image by twisting the two ends in opposite directions": Amount, the centre,
        // and the Axis menu "whether the twist applies horizontally (X axis) or vertically (Y axis)"
        // (p.464, p.527). Ours: the picture as a ribbon along the axis, turned about it by up to Amount/100
        // half turns at the picture's ends and in opposite directions either side of the centre; seen
        // edge on it is gone, and turned past a quarter it shows mirrored.
        const QString cat = QStringLiteral("Twist Effect Input Parameters");
        const double turns = std::clamp(num(e, cat, QStringLiteral("Amount"), time, span), -100.0, 100.0) / 100.0;
        const double cx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double cy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        const bool alongY = text(e, cat, QStringLiteral("Axis")) == QLatin1String("Y axis");
        return warp(layer, SourceBox::of(e, time, span, W, H), flag(e, cat, QStringLiteral("Anti-alias"), true),
                    [=](double& x, double& y) {
                        const double along = alongY ? (y - cy) / (H / 2.0) : (x - cx) / (W / 2.0);
                        const double c = std::cos(turns * kPi * along);
                        if (std::abs(c) < 1e-3) return false;
                        if (alongY) {
                            x = cx + (x - cx) / c;
                            return x >= 0 && x <= W;
                        }
                        y = cy + (y - cy) / c;
                        return y >= 0 && y <= H;
                    });
    }

    if (name == QLatin1String("Wave")) {
        // "Wave distorts the image by applying a user-defined number of waves to it": Amount, Minimum
        // and Maximum Size "the minimum wavelength and the maximum wavelength", the Horizontal and
        // Vertical Factor "0 (no distortion) to 100 (maximum distortion)", Number of Waves
        // "superimposed upon each other", Speed "the progress of the effect", Random Seed (p.465, p.528).
        // Ours: each wave runs in a random direction with a random wavelength between the sizes (pixels)
        // and phase; at Amount 100 a wave moves pixels by up to a quarter of its wavelength; Speed 100 is
        // four periods.
        const QString cat = QStringLiteral("Wave Effect Input Parameters");
        const double amount = std::clamp(num(e, cat, QStringLiteral("Amount"), time, span), -100.0, 100.0) / 100.0;
        const double minSize = std::max(num(e, cat, QStringLiteral("Minimum Size"), time, span), 1.0) * unit;
        const double maxSize = std::max(num(e, cat, QStringLiteral("Maximum Size"), time, span) * unit, minSize);
        const double hf = std::clamp(num(e, cat, QStringLiteral("Horizontal Factor"), time, span), 0.0, 100.0) / 100.0;
        const double vf = std::clamp(num(e, cat, QStringLiteral("Vertical Factor"), time, span), 0.0, 100.0) / 100.0;
        const int count = std::clamp(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Number of Waves"), time, span))), 0, 20);
        const double progress = num(e, cat, QStringLiteral("Speed"), time, span) / 100.0 * 4.0 * 2.0 * kPi;
        const auto seed = static_cast<std::int64_t>(std::lround(num(e, cat, QStringLiteral("Random Seed"), time, span)));
        struct Wave {
            double ux, uy, k, phase, amplitude;
        };
        std::vector<Wave> waves;
        for (int i = 0; i < count; ++i) {
            const double direction = hash01(i, 1, 0, seed) * 2.0 * kPi;
            const double length = minSize + (maxSize - minSize) * hash01(i, 2, 0, seed);
            waves.push_back({std::cos(direction), std::sin(direction), 2.0 * kPi / length, hash01(i, 3, 0, seed) * 2.0 * kPi + progress,
                             amount * length / 4.0 / std::sqrt(static_cast<double>(count))});
        }
        return warp(layer, SourceBox::of(e, time, span, W, H), flag(e, cat, QStringLiteral("Anti-alias"), true),
                    [&waves, hf, vf](double& x, double& y) {
                        double dx = 0.0;
                        double dy = 0.0;
                        for (const Wave& w : waves) {
                            const double at = (x * w.ux + y * w.uy) * w.k + w.phase;
                            dx += w.amplitude * std::sin(at);
                            dy += w.amplitude * std::cos(at);
                        }
                        x += dx * hf;
                        y += dy * vf;
                        return true;
                    });
    }

    if (name == QLatin1String("Radial Blur")) {
        // "blurs the image inward or outward from a user-defined point, and around a user-defined
        // rotation": Zoom "Smaller values zoom in and larger values zoom out", Angle "the rotation of the
        // blur", the centre, and the Render Mode menu, where "Locate shows the center marker" (p.464,
        // p.522). Ours: Zoom -100 to 100 (0 none) blurs over up to a quarter of the distance to the
        // centre, Angle in degrees; Low, Medium and High Quality take 8, 16 and 32 samples a pixel.
        const QString cat = QStringLiteral("Radial Blur Effect Input Parameters");
        const double zoom = std::clamp(num(e, cat, QStringLiteral("Zoom"), time, span), -100.0, 100.0) / 100.0 * 0.25;
        const double angle = num(e, cat, QStringLiteral("Angle"), time, span) * kPi / 180.0;
        const double cx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double cy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        const QString mode = text(e, cat, QStringLiteral("Render Mode"));
        if (mode == QLatin1String("Locate")) {
            // The picture as it is with a cross at the centre, white edged with black.
            Picture out = layer;
            const double arm = 24.0 * unit;
            const double half = std::max(1.5 * unit, 0.75);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double ax = std::abs(x + 0.5 - cx);
                    const double ay = std::abs(y + 0.5 - cy);
                    const bool line = (ax <= arm && ay <= half) || (ay <= arm && ax <= half);
                    const bool edge = (ax <= arm + half && ay <= 2 * half) || (ay <= arm + half && ax <= 2 * half);
                    if (!line && !edge) continue;
                    float* o = out.pixel(x, y);
                    const float v = line ? 1.0f : 0.0f;
                    o[0] = o[1] = o[2] = v;
                    o[3] = 1.0f;
                }
            }
            return out;
        }
        const int taps = mode == QLatin1String("Render Low Quality") ? 8 : mode == QLatin1String("Render High Quality") ? 32 : 16;
        const SourceBox box = SourceBox::of(e, time, span, W, H);
        Picture out(width, height);
        float in[4];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double dx = x + 0.5 - cx;
                const double dy = y + 0.5 - cy;
                float sum[4] = {0, 0, 0, 0};
                for (int t = 0; t < taps; ++t) {
                    const double f = static_cast<double>(t) / (taps - 1);
                    const double s = 1.0 + zoom * f;
                    const double c = std::cos(angle * f);
                    const double n = std::sin(angle * f);
                    sample(layer, box.x(cx + (dx * c - dy * n) * s), box.y(cy + (dx * n + dy * c) * s), in);
                    for (int i = 0; i < 4; ++i) sum[i] += in[i];
                }
                float* o = out.pixel(x, y);
                for (int i = 0; i < 4; ++i) o[i] = sum[i] / static_cast<float>(taps);
            }
        }
        return out;
    }

    if (name == QLatin1String("Ripple")) {
        // "ripples on the image, as if you dropped a stone into a pond": Amount "the progression of the
        // effect", Wave Speed, Ripple Spread "the width of the ripple from the inner ring to the outer
        // ring", Reflections ("waves reflect off the edges of the image, as though hitting a wall"),
        // Strength "the height of the waves", Length "the distance between the wave peaks", Number of
        // Ripples, Random Seed, and the light's colour and Angle ("0 degrees is 3 o'clock. Positive
        // values move counterclockwise") (p.464, p.523-524). Ours: each ripple starts at a random point
        // and time in the first third of Amount; its front moves out, at Wave Speed 100 across the
        // picture's diagonal twice over Amount 0 to 100; Spread 100 is half the diagonal; Strength 100
        // moves pixels by half of Length (pixels). Reflections add the ripples mirrored in the four edges.
        // The light brightens the slopes facing it and darkens the others.
        const QString cat = QStringLiteral("Ripple Effect Motion, Generation, and Illumination Parameters");
        const double amount = num(e, cat, QStringLiteral("Amount"), time, span) / 100.0;
        const double diagonal = std::hypot(W, H);
        const double speed = std::max(num(e, cat, QStringLiteral("Wave Speed"), time, span), 0.0) / 100.0 * 2.0 * diagonal;
        const double length = std::max(num(e, cat, QStringLiteral("Length"), time, span), 1.0) * unit;
        const double spread = std::max(std::max(num(e, cat, QStringLiteral("Ripple Spread"), time, span), 0.0) / 100.0 * diagonal / 2.0, length);
        const double strength = std::max(num(e, cat, QStringLiteral("Strength"), time, span), 0.0) / 100.0 * length / 2.0;
        const int count = std::clamp(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Number of Ripples"), time, span))), 0, 20);
        const auto seed = static_cast<std::int64_t>(std::lround(num(e, cat, QStringLiteral("Random Seed"), time, span)));
        const bool reflect = flag(e, cat, QStringLiteral("Reflections"), false);
        const QColor light = colourOf(e, cat, QStringLiteral("Color controls"), time, span);
        const double lightAngle = num(e, cat, QStringLiteral("Angle"), time, span) * kPi / 180.0;
        const double lx = std::cos(lightAngle);
        const double ly = -std::sin(lightAngle); // counterclockwise on the screen, y down
        struct Ring {
            double x, y, front;
        };
        std::vector<Ring> rings;
        for (int i = 0; i < count; ++i) {
            const double front = (amount - hash01(i, 3, 0, seed) / 3.0) * speed;
            if (front <= 0) continue;
            const double x = (0.2 + 0.6 * hash01(i, 1, 0, seed)) * W;
            const double y = (0.2 + 0.6 * hash01(i, 2, 0, seed)) * H;
            rings.push_back({x, y, front});
            if (reflect) {
                rings.push_back({-x, y, front});
                rings.push_back({2 * W - x, y, front});
                rings.push_back({x, -y, front});
                rings.push_back({x, 2 * H - y, front});
            }
        }
        const SourceBox box = SourceBox::of(e, time, span, W, H);
        const bool antiAlias = flag(e, cat, QStringLiteral("Anti-alias"), true);
        const float lr = static_cast<float>(light.redF());
        const float lg = static_cast<float>(light.greenF());
        const float lb = static_cast<float>(light.blueF());
        Picture out(width, height);
        float in[4];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                double sx = x + 0.5;
                double sy = y + 0.5;
                double shade = 0.0;
                for (const Ring& ring : rings) {
                    const double dx = x + 0.5 - ring.x;
                    const double dy = y + 0.5 - ring.y;
                    const double r = std::hypot(dx, dy);
                    const double behind = ring.front - r; // how far behind the front this pixel is
                    if (r <= 0 || behind < 0 || behind > spread) continue;
                    const double envelope = std::sin(kPi * behind / spread);
                    const double phase = 2.0 * kPi * behind / length;
                    const double shift = strength * envelope * std::sin(phase);
                    sx += shift * dx / r;
                    sy += shift * dy / r;
                    // The surface's slope along the ring's radius, toward or away from the light.
                    shade += envelope * std::cos(phase) * (strength / length) * (dx * lx + dy * ly) / r;
                }
                if (antiAlias) {
                    sample(layer, box.x(sx), box.y(sy), in);
                } else {
                    nearest(layer, box.x(sx), box.y(sy), in);
                }
                float* o = out.pixel(x, y);
                const float s = static_cast<float>(std::clamp(shade, -1.0, 1.0));
                if (s >= 0) {
                    o[0] = std::min(in[0] + lr * s * in[3], in[3]);
                    o[1] = std::min(in[1] + lg * s * in[3], in[3]);
                    o[2] = std::min(in[2] + lb * s * in[3], in[3]);
                } else {
                    for (int i = 0; i < 3; ++i) o[i] = in[i] * (1.0f + s);
                }
                o[3] = in[3];
            }
        }
        return out;
    }

    // The rest draw: light (Flare, Lightning, Sparkler) added over the picture, or a new picture
    // (Pattern Generator).
    QImage light(width, height, QImage::Format_ARGB32_Premultiplied);
    light.fill(Qt::transparent);

    if (name == QLatin1String("Flare")) {
        // "a random ring flare. You can control the brightness, size, and center of the flare":
        // Brightness "0 (no flare) to 100", Size "the diameter of the flare ring", Width "the distance
        // between the inner and outer edges", Amount "the amount of distortion to the flare ring (0 is no
        // distortion)", Random Seed, X and Y the centre (p.510). Ours: Size and Width in percent of the
        // picture's width, X and Y in percent; a soft ring with a faint glow inside.
        const QString cat = QStringLiteral("Flare Effect Input Parameters");
        const double bright = std::clamp(num(e, cat, QStringLiteral("Brightness"), time, span), 0.0, 100.0) / 100.0;
        const double radius = std::max(num(e, cat, QStringLiteral("Size"), time, span), 0.0) / 100.0 * W / 2.0;
        const double ring = std::max(num(e, cat, QStringLiteral("Width"), time, span) / 100.0 * W / 2.0, 1.0);
        const double amount = std::clamp(num(e, cat, QStringLiteral("Amount"), time, span), 0.0, 100.0) / 100.0;
        const auto seed = static_cast<std::int64_t>(num(e, cat, QStringLiteral("Random Seed"), time, span));
        const double fx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double fy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        double phase[3];
        for (int k = 0; k < 3; ++k) phase[k] = hash01(k, 0, 0, seed) * 2.0 * kPi;
        for (int y = 0; y < height; ++y) {
            auto* line = reinterpret_cast<QRgb*>(light.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const double rx = x + 0.5 - fx;
                const double ry = y + 0.5 - fy;
                const double d = std::hypot(rx, ry);
                const double a = std::atan2(ry, rx);
                const double wobble = 1.0 + amount * 0.3 * (std::sin(3 * a + phase[0]) + std::sin(5 * a + phase[1]) + std::sin(7 * a + phase[2])) / 3.0;
                const double off = (d - radius * wobble) / ring;
                double i = std::exp(-off * off * 2.0) + 0.15 * std::exp(-d * d / (radius * radius + 1.0));
                i = std::clamp(i * bright, 0.0, 1.0);
                const int v = static_cast<int>(std::lround(i * 255.0));
                line[x] = qPremultiply(qRgba(255, 230, 180, v));
            }
        }
        Picture out = layer;
        addLight(out, light);
        return out;
    }

    if (name == QLatin1String("Lightning")) {
        // "a random lightning pattern": Amount "0 (no lightning) to 100 (complete lightning bolt)";
        // Branch Probability "A value of 0 displays no branches"; Random Seed; the Core's and Glow's Width
        // ("the radius ... in pixels") and colour (p.513-514). Ours: the bolt runs from the top of the
        // picture to the bottom, its widths in pixels of a picture 1920 wide; Render Mode is always
        // anti-aliased.
        const QString cat = QStringLiteral("Lightning Effect Generation, Core, and Glow Parameters");
        const double amount = std::clamp(num(e, cat, QStringLiteral("Amount"), time, span), 0.0, 100.0) / 100.0;
        const double branch = std::clamp(num(e, cat, QStringLiteral("Branch Probability"), time, span), 0.0, 100.0) / 100.0;
        const auto seed = static_cast<std::uint64_t>(std::max(num(e, cat, QStringLiteral("Random Seed"), time, span), 0.0));
        const double core = std::max(num(e, cat, QStringLiteral("Width"), time, span), 0.0) * unit;
        const double glow = std::max(num(e, cat, QStringLiteral("Glow Parameters: Width"), time, span), 0.0) * unit;
        const QColor coreColour = colourOf(e, cat, QStringLiteral("Color controls"), time, span);
        const QColor glowColour = colourOf(e, cat, QStringLiteral("Glow Parameters: Color controls"), time, span);
        Random rnd(seed + 1);
        // Midpoint displacement: 2^6 + 1 points down the picture.
        std::vector<QPointF> bolt = {QPointF(W * 0.5, 0.0), QPointF(W * 0.5, H)};
        double spread = W * 0.15;
        for (int level = 0; level < 6; ++level) {
            std::vector<QPointF> finer;
            for (std::size_t i = 0; i + 1 < bolt.size(); ++i) {
                finer.push_back(bolt[i]);
                const QPointF mid = (bolt[i] + bolt[i + 1]) / 2.0;
                finer.push_back(mid + QPointF((rnd.next() - 0.5) * spread, 0.0));
            }
            finer.push_back(bolt.back());
            bolt.swap(finer);
            spread *= 0.55;
        }
        const auto shown = static_cast<std::size_t>(std::lround(amount * static_cast<double>(bolt.size() - 1)));
        QPainter p(&light);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        auto stroke = [&](const std::vector<QPointF>& points, std::size_t count, double thin) {
            if (count < 1) return;
            QPainterPath path(points[0]);
            for (std::size_t i = 1; i <= count && i < points.size(); ++i) path.lineTo(points[i]);
            QColor g = glowColour;
            g.setAlphaF(0.5f);
            if (glow > 0) p.strokePath(path, QPen(g, glow * 2.0 * thin, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            // At least a pixel wide, so a small picture still shows the bolt.
            if (core > 0) p.strokePath(path, QPen(coreColour, std::max(core * 2.0 * thin, 1.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        };
        stroke(bolt, shown, 1.0);
        // Branches from the shown part, thinner and shorter.
        for (std::size_t i = 1; i < shown; ++i) {
            if (rnd.next() >= branch * 0.25) continue;
            std::vector<QPointF> limb = {bolt[i]};
            const double dir = (rnd.next() < 0.5 ? -1.0 : 1.0);
            QPointF at = bolt[i];
            const int steps = 4 + static_cast<int>(rnd.next() * 6);
            for (int s = 0; s < steps; ++s) {
                at += QPointF(dir * (rnd.next() * 0.03 + 0.01) * W, (rnd.next() * 0.03 + 0.005) * H);
                limb.push_back(at);
            }
            stroke(limb, limb.size() - 1, 0.5);
        }
        p.end();
        Picture out = layer;
        addLight(out, light);
        return out;
    }

    if (name == QLatin1String("Sparkler")) {
        // "random sparks or fireworks": Birth Rate "the number of sparks generated every frame", Lifetime
        // "(in frames) how long each spark lives", X and Y the centre, Length "the cumulative exposure"
        // in frames, Speed Bias X and Y added to the directions; Speed, Gravity ("The greater the value,
        // the more the sparks are attracted downward"), Air Resistance, Wind X and Y ("The Air Resistance
        // value must be greater than 0 for Wind to have an effect"), Turbulence; the Core and Glow radii
        // "in pixels" and colour (p.525-526). Ours: a fixed seed; speeds in pixels a frame of a picture
        // 1920 wide; Link Speed waits for keyframed centres to be followed.
        const QString cat = QStringLiteral("Sparkler Effect Generation, Movement, Core, and Glow Parameters");
        const int birth = std::clamp(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Birth Rate"), time, span))), 0, 200);
        const int life = std::clamp(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Lifetime"), time, span))), 1, 300);
        const double sx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
        const double sy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
        const int length = std::clamp(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Length"), time, span))), 1, 10);
        const double biasX = num(e, cat, QStringLiteral("Speed Bias X"), time, span) * 0.1 * unit;
        const double biasY = num(e, cat, QStringLiteral("Speed Bias Y"), time, span) * 0.1 * unit;
        const double speed = std::max(num(e, cat, QStringLiteral("Speed"), time, span), 0.0) * unit;
        const double gravity = num(e, cat, QStringLiteral("Gravity"), time, span) * 0.05 * unit;
        const double drag = std::clamp(num(e, cat, QStringLiteral("Air Resistance"), time, span), 0.0, 100.0) / 100.0 * 0.2;
        const double windX = num(e, cat, QStringLiteral("Wind X"), time, span) * 0.1 * unit;
        const double windY = num(e, cat, QStringLiteral("Wind Y"), time, span) * 0.1 * unit;
        const double turbulence = std::max(num(e, cat, QStringLiteral("Turbulence"), time, span), 0.0) * 0.02 * unit;
        const double coreR = std::max(num(e, cat, QStringLiteral("Core Radius"), time, span), 0.0) * unit;
        const double glowR = std::max(num(e, cat, QStringLiteral("Glow Radius"), time, span), 0.0) * unit;
        const QColor colour = colourOf(e, cat, QStringLiteral("Color controls"), time, span);
        QPainter p(&light);
        p.setRenderHint(QPainter::Antialiasing);
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.setPen(Qt::NoPen);
        auto spark = [&](const QPointF& at, double fade) {
            if (glowR > 0) {
                QRadialGradient g(at, glowR);
                QColor c = colour;
                c.setAlphaF(static_cast<float>(0.6 * fade));
                g.setColorAt(0, c);
                c.setAlphaF(0);
                g.setColorAt(1, c);
                p.setBrush(g);
                p.drawEllipse(at, glowR, glowR);
            }
            if (coreR > 0) {
                QColor c = QColor(255, 255, 240);
                c.setAlphaF(static_cast<float>(fade));
                p.setBrush(c);
                p.drawEllipse(at, coreR, coreR);
            }
        };
        for (std::int64_t born = std::max<std::int64_t>(frame - life + 1, 0); born <= frame; ++born) {
            for (int j = 0; j < birth; ++j) {
                const double angle = hash01(born, j, 0, 7) * 2.0 * kPi;
                const double s = speed * (0.5 + hash01(born, j, 1, 7));
                double vx = s * std::cos(angle) + biasX;
                double vy = -s * std::sin(angle) + biasY;
                double x = sx;
                double y = sy;
                const auto age = static_cast<int>(frame - born);
                for (int step = 0; step < age; ++step) {
                    vy += gravity;
                    vx = vx * (1.0 - drag) + windX * drag;
                    vy = vy * (1.0 - drag) + windY * drag;
                    vx += (hash01(born, j, 100 + step, 7) - 0.5) * turbulence;
                    vy += (hash01(born, j, 200 + step, 7) - 0.5) * turbulence;
                    x += vx;
                    y += vy;
                    // The last Length frames of its path show (the open shutter).
                    if (age - step <= length - 1) spark(QPointF(x - vx, y - vy), 0.5 * (1.0 - double(age) / life));
                }
                spark(QPointF(x, y), 1.0 - double(age) / life);
            }
        }
        p.end();
        Picture out = layer;
        addLight(out, light);
        return out;
    }

    // Pattern Generator: "creates a clip using a solid color, a variety of color bars, or a grid. The
    // frames can be numbered automatically, and a circle can be animated" (p.464): the Pattern Type
    // and Axis menus and the colour; the Number's offset from the frame number, opacity, size, stroke,
    // colour and position; the Circle's size, width, opacity, colour and position (p.520-521). Ours: the
    // bars' levels as the usual test patterns have them; sizes in pixels of a picture 1920 wide (the
    // circle's in percent of the height); the number and circle off (Opacity 0) until set.
    const QString cat = QStringLiteral("Pattern Generator Effect Background, Number, and Circle Parameters");
    const QString type = text(e, cat, QStringLiteral("Pattern Type"));
    const bool vertical = text(e, cat, QStringLiteral("Axis")) == QLatin1String("Y axis");
    const QColor base = colourOf(e, cat, QStringLiteral("Color controls"), time, span);
    QPainter p(&light);
    p.setRenderHint(QPainter::Antialiasing);
    auto bars = [&](const QList<QColor>& colours, double from, double to) {
        // Across the picture (X axis) or down it (Y axis), between `from` and `to` of the other way.
        const int n = static_cast<int>(colours.size());
        for (int i = 0; i < n; ++i) {
            const QRectF r = vertical ? QRectF(from * W, i * H / n, (to - from) * W, H / n + 1)
                                      : QRectF(i * W / n, from * H, W / n + 1, (to - from) * H);
            p.fillRect(r, colours.at(i));
        }
    };
    auto level = [](double v, int r, int g, int b) { return QColor::fromRgbF(float(v * r), float(v * g), float(v * b)); };
    const QList<QColor> seven75 = {level(0.75, 1, 1, 1), level(0.75, 1, 1, 0), level(0.75, 0, 1, 1), level(0.75, 0, 1, 0),
                                   level(0.75, 1, 0, 1), level(0.75, 1, 0, 0), level(0.75, 0, 0, 1)};
    if (type == QLatin1String("Solid color")) {
        p.fillRect(QRectF(0, 0, W, H), base);
    } else if (type == QLatin1String("Color bars (75%)") || type == QLatin1String("EIA bars")) {
        bars(seven75, 0.0, 1.0);
    } else if (type == QLatin1String("Color bars (100%)")) {
        bars({level(1, 1, 1, 1), level(1, 1, 1, 0), level(1, 0, 1, 1), level(1, 0, 1, 0), level(1, 1, 0, 1), level(1, 1, 0, 0), level(1, 0, 0, 1)}, 0.0, 1.0);
    } else if (type == QLatin1String("EBU bars (75%)")) {
        QList<QColor> ebu = seven75;
        ebu[0] = Qt::white;
        ebu << Qt::black;
        bars(ebu, 0.0, 1.0);
    } else if (type == QLatin1String("SMPTE bars")) {
        bars(seven75, 0.0, 0.67);
        bars({level(0.75, 0, 0, 1), Qt::black, level(0.75, 1, 0, 1), Qt::black, level(0.75, 0, 1, 1), Qt::black, level(0.75, 1, 1, 1)}, 0.67, 0.75);
        bars({QColor(0, 33, 76), Qt::white, QColor(50, 0, 106), Qt::black, Qt::black, QColor(5, 5, 5), QColor(10, 10, 10), Qt::black}, 0.75, 1.0);
    } else if (type == QLatin1String("Color ramps")) {
        for (int band = 0; band < 4; ++band) {
            const QColor end = band == 0 ? Qt::red : band == 1 ? Qt::green : band == 2 ? Qt::blue : Qt::white;
            QLinearGradient g(vertical ? QPointF(0, 0) : QPointF(0, 0), vertical ? QPointF(0, H) : QPointF(W, 0));
            g.setColorAt(0, Qt::black);
            g.setColorAt(1, end);
            const QRectF r = vertical ? QRectF(band * W / 4, 0, W / 4 + 1, H) : QRectF(0, band * H / 4, W, H / 4 + 1);
            p.fillRect(r, g);
        }
    } else if (type == QLatin1String("Cross ramp")) {
        for (int y = 0; y < height; ++y) {
            auto* line = reinterpret_cast<QRgb*>(light.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const int v = static_cast<int>(std::lround(((x + 0.5) / W + (y + 0.5) / H) / 2.0 * 255.0));
                line[x] = qRgb(v, v, v);
            }
        }
    } else if (type == QLatin1String("Grid")) {
        // "the background color is black if Base Color has 50% or greater luminance, or white if Base
        // Color has less than 50% luminance" (p.520).
        p.fillRect(QRectF(0, 0, W, H), base.lightnessF() >= 0.5f ? Qt::black : Qt::white);
        p.setPen(QPen(base, std::max(2.0 * unit, 1.0)));
        for (int i = 0; i <= 16; ++i) p.drawLine(QPointF(i * W / 16, 0), QPointF(i * W / 16, H));
        for (int i = 0; i <= 9; ++i) p.drawLine(QPointF(0, i * H / 9), QPointF(W, i * H / 9));
    }
    // The circle.
    const float circleOpacity = static_cast<float>(std::clamp(num(e, cat, QStringLiteral("Circle Parameters: Opacity"), time, span), 0.0, 100.0) / 100.0);
    if (circleOpacity > 0) {
        QColor c = colourOf(e, cat, QStringLiteral("Circle Parameters: Color controls"), time, span);
        c.setAlphaF(circleOpacity);
        const double r = std::max(num(e, cat, QStringLiteral("Size"), time, span), 0.0) / 100.0 * H;
        const QPointF at(num(e, cat, QStringLiteral("Circle Parameters: X"), time, span) / 100.0 * W,
                         num(e, cat, QStringLiteral("Circle Parameters: Y"), time, span) / 100.0 * H);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c, std::max(num(e, cat, QStringLiteral("Width"), time, span), 0.5) * unit * 4.0));
        p.drawEllipse(at, r, r);
    }
    // The number: the frame's, plus the Number Offset.
    const float numberOpacity = static_cast<float>(std::clamp(num(e, cat, QStringLiteral("Opacity"), time, span), 0.0, 100.0) / 100.0);
    if (numberOpacity > 0) {
        QColor c = colourOf(e, cat, QStringLiteral("Number Parameters: Color controls"), time, span);
        c.setAlphaF(numberOpacity);
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPixelSize(std::max(static_cast<int>(std::lround(num(e, cat, QStringLiteral("Text Size"), time, span) * unit)), 4));
        const QString n = QString::number(frame + static_cast<std::int64_t>(std::lround(num(e, cat, QStringLiteral("Number Offset"), time, span))));
        const QFontMetricsF metrics(font);
        const QPointF at(num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W - metrics.horizontalAdvance(n) / 2.0,
                         num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H + metrics.ascent() / 2.0);
        QPainterPath path;
        path.addText(at, font, n);
        const double stroke = std::max(num(e, cat, QStringLiteral("Stroke Width"), time, span), 0.0) * unit;
        if (stroke > 0) p.strokePath(path, QPen(c, stroke));
        p.fillPath(path, c);
    }
    p.end();
    return Picture::fromImage(light, width, height);
}

} // namespace lumenis::media::detail
