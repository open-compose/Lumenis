#include "lumenis/media/compositor.h"

#include "effect_drawing.h"

#include <QColor>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

// The 3D effects (ROADMAP 6.7a; FX Guide ch.11, p.287-300, parameters p.568-585; docs/spec/3d-effects.md):
// the 3D Warp effect and the Xpress 3D effects as one renderer. The foreground picture is a flat plane in
// 3D space, moved by the parameters in the order of the 3D parameter hierarchy (p.568-569: "Target,
// Perspective, Position, Rotation, Axis, Scaling, Crop", higher to lower) and seen in perspective. Each
// pixel of the result is a ray from the viewer through the screen, met with the plane and mapped back into
// the foreground; Corner Pin instead pins the plane's four corners to the screen (a homography).
//
// Units (ours, as for the 2D effects, docs/spec/image-effects.md): Position, Axis and Target in pixels of a
// picture 1920 wide, X to the right and Y up (p.577); Crop and Skew from -999 to 999 across the picture;
// rotation in degrees, positive clockwise (p.291). The viewer stands two picture widths in front of the
// screen; Perspective moves that point. Shapes (Ball, Page Fold, Slats), Trail, Stamp and the Luma and
// Chroma key types come in 6.7a2; until then the effect draws as a flat plane, the key type as Full Key.
namespace lumenis::media {

namespace {

using Vec = std::array<double, 3>;
using Mat = std::array<Vec, 3>; // rows

Vec add(const Vec& a, const Vec& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec cross(const Vec& a, const Vec& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
Vec apply(const Mat& m, const Vec& v) { return {dot(m[0], v), dot(m[1], v), dot(m[2], v)}; }
Mat multiply(const Mat& a, const Mat& b) {
    Mat r{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    }
    return r;
}
double radians(double degrees) { return degrees * std::numbers::pi / 180.0; }

bool is3D(const model::Effect& e) {
    return (e.category == QLatin1String("Blend") && e.name == QLatin1String("3D Warp")) ||
           e.category == QLatin1String("Xpress 3D Effect");
}

// Whether a parameter category is on: "When you enable Defocus ..." (p.572). Ours: a category is on once
// one of its values is set (or keyframed), as the Effect Editor stores them.
bool enabled(const model::Effect& e, const QString& category) {
    if (e.parameters.contains(category)) return true;
    const QString prefix = category + QLatin1Char('/');
    for (const QString& k : model::keyframedParameters(e)) {
        if (k.startsWith(prefix)) return true;
    }
    return false;
}

bool flag(const model::Effect& e, const QString& category, const QString& name) {
    return e.parameters.value(category).toObject().value(name).toBool();
}

double number(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, category, name, time, span).value_or(parameterDefault(e, category, name).toDouble(0.0));
}

QColor colourOf(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span,
                const QColor& fallback) {
    if (const auto rgb = model::colorAt(e, category, name, time, span)) {
        return QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue));
    }
    return fallback;
}

// A separable box blur of `radius` pixels, twice (close to a Gaussian), for Defocus.
void blur(Picture& p, int radius) {
    if (radius <= 0 || p.isNull()) return;
    const int w = p.width();
    const int h = p.height();
    std::vector<float> line;
    for (int pass = 0; pass < 2; ++pass) {
        for (int dir = 0; dir < 2; ++dir) {
            const int n = dir == 0 ? w : h;
            const int lines = dir == 0 ? h : w;
            line.assign(static_cast<std::size_t>(n) * 4, 0.0f);
            for (int l = 0; l < lines; ++l) {
                auto at = [&](int i) { return dir == 0 ? p.pixel(i, l) : p.pixel(l, i); };
                for (int i = 0; i < n; ++i) {
                    float sum[4] = {0, 0, 0, 0};
                    int count = 0;
                    for (int k = std::max(0, i - radius); k <= std::min(n - 1, i + radius); ++k) {
                        const float* s = at(k);
                        for (int c = 0; c < 4; ++c) sum[c] += s[c];
                        ++count;
                    }
                    for (int c = 0; c < 4; ++c) line[static_cast<std::size_t>(i) * 4 + static_cast<std::size_t>(c)] = sum[c] / static_cast<float>(count);
                }
                for (int i = 0; i < n; ++i) std::copy_n(&line[static_cast<std::size_t>(i) * 4], 4, at(i));
            }
        }
    }
}

// Solves the 8x8 system for the homography taking the four `from` points to the four `to` points; the
// result maps (x, y, 1) with h[8] = 1. False when the points are degenerate.
bool homography(const std::array<std::array<double, 2>, 4>& from, const std::array<std::array<double, 2>, 4>& to, std::array<double, 9>& h) {
    double a[8][9] = {};
    for (std::size_t i = 0; i < 4; ++i) {
        const double x = from[i][0], y = from[i][1], u = to[i][0], v = to[i][1];
        double* r0 = a[2 * i];
        double* r1 = a[2 * i + 1];
        const double row0[9] = {x, y, 1, 0, 0, 0, -u * x, -u * y, u};
        const double row1[9] = {0, 0, 0, x, y, 1, -v * x, -v * y, v};
        std::copy_n(row0, 9, r0);
        std::copy_n(row1, 9, r1);
    }
    for (std::size_t c = 0; c < 8; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < 8; ++r) {
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        }
        if (std::abs(a[pivot][c]) < 1e-12) return false;
        for (std::size_t k = 0; k < 9; ++k) std::swap(a[c][k], a[pivot][k]);
        for (std::size_t r = 0; r < 8; ++r) {
            if (r == c) continue;
            const double f = a[r][c] / a[c][c];
            for (std::size_t k = c; k < 9; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (std::size_t i = 0; i < 8; ++i) h[i] = a[i][8] / a[i][i];
    h[8] = 1.0;
    return true;
}

bool invert(const std::array<double, 9>& m, std::array<double, 9>& out) {
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 1e-15) return false;
    out = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
           (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
           (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    return true;
}

// The plane as placed by the parameters: screen point -> foreground point, or nothing where the screen
// point does not see it.
struct Placement {
    double W = 0, H = 0, unit = 1;
    // Crop (3D), in plane coordinates (centred, Y up).
    double left = 0, right = 0, bottom = 0, top = 0;
    // The plane: point = L * (x, y, 0) + c, seen from `viewer`.
    Vec col0{}, col1{}, c{}, viewer{};
    double targetX = 0, targetY = 0, targetSize = 1;
    // Corner Pin: screen -> plane by homography instead.
    bool pinned = false;
    std::array<double, 9> unpin{};

    // The plane point (centred, Y up) a screen point (centred, Y up) shows; nothing when none.
    std::optional<std::pair<double, double>> planePoint(double sx, double sy) const {
        if (pinned) {
            const double X = unpin[0] * sx + unpin[1] * sy + unpin[2];
            const double Y = unpin[3] * sx + unpin[4] * sy + unpin[5];
            const double Z = unpin[6] * sx + unpin[7] * sy + unpin[8];
            if (std::abs(Z) < 1e-12) return std::nullopt;
            return std::pair{X / Z, Y / Z};
        }
        // Undo Target ("Post-transformation parameters, such as Target, move the entire path", p.569).
        if (targetSize <= 1e-9) return std::nullopt;
        const Vec q{(sx - targetX) / targetSize, (sy - targetY) / targetSize, 0.0};
        // viewer + t (q - viewer) = L (x, y, 0) + c: x col0 + y col1 - t d = viewer - c.
        const Vec d = sub(q, viewer);
        const Vec nd{-d[0], -d[1], -d[2]};
        const Vec rhs = sub(viewer, c);
        const double det = dot(col0, cross(col1, nd));
        if (std::abs(det) < 1e-9) return std::nullopt;
        const double x = dot(rhs, cross(col1, nd)) / det;
        const double y = dot(col0, cross(rhs, nd)) / det;
        const double t = dot(col0, cross(col1, rhs)) / det;
        if (t <= 0.0) return std::nullopt; // behind the viewer
        return std::pair{x, y};
    }
};

Placement placementOf(const model::Effect& e, double time, const model::EffectSpan& span, int width, int height) {
    Placement p;
    p.W = width;
    p.H = height;
    p.unit = width / 1920.0;
    auto n = [&](const char* category, const char* name) { return number(e, QLatin1String(category), QLatin1String(name), time, span); };
    // Crop (3D): "T (top) Crops the top edge of the image. Values range from -999 (bottom) to 999 (top)" (p.572).
    const double hw = p.W / 2.0;
    const double hh = p.H / 2.0;
    p.left = std::clamp(n("Crop (3D)", "L"), -999.0, 999.0) / 999.0 * hw;
    p.right = std::clamp(n("Crop (3D)", "R"), -999.0, 999.0) / 999.0 * hw;
    p.bottom = std::clamp(n("Crop (3D)", "B"), -999.0, 999.0) / 999.0 * hh;
    p.top = std::clamp(n("Crop (3D)", "T"), -999.0, 999.0) / 999.0 * hh;
    if (enabled(e, QStringLiteral("Corner Pin"))) {
        // "Corner 1 - top left corner of the untransformed image ... If the image is cropped, Media Composer
        // uses the corners of the cropped image" (p.571). Ours: X and Y from -999 to 999 across the screen.
        const std::array<std::array<double, 2>, 4> from{{{p.left, p.top}, {p.right, p.top}, {p.right, p.bottom}, {p.left, p.bottom}}};
        std::array<std::array<double, 2>, 4> to{};
        for (std::size_t i = 0; i < 4; ++i) {
            const QString corner = QStringLiteral("Corner %1").arg(i + 1);
            to[i][0] = std::clamp(number(e, QStringLiteral("Corner Pin"), corner + QStringLiteral(": X"), time, span), -9999.0, 9999.0) / 999.0 * hw;
            to[i][1] = std::clamp(number(e, QStringLiteral("Corner Pin"), corner + QStringLiteral(": Y"), time, span), -9999.0, 9999.0) / 999.0 * hh;
        }
        std::array<double, 9> pin{};
        if (homography(from, to, pin) && invert(pin, p.unpin)) {
            p.pinned = true;
            return p;
        }
    }
    // Scaling (3D), "100 indicates 100 percent scaling" (p.579), then Skew (ours: ±999 a shear of 1).
    const double sx = std::max(n("Scaling (3D)", "X"), 0.0) / 100.0;
    const double sy = std::max(n("Scaling (3D)", "Y"), 0.0) / 100.0;
    const double kx = std::clamp(n("Skew", "X"), -999.0, 999.0) / 999.0;
    const double ky = std::clamp(n("Skew", "Y"), -999.0, 999.0) / 999.0;
    const Mat scaleSkew{{{sx, kx * sx, 0}, {ky * sy, sy, 0}, {0, 0, 1}}};
    // Rotation about the Axis: X first, then Y, then Z; positive clockwise as seen (p.291).
    const double ax = radians(n("Rotation", "X"));
    const double ay = radians(n("Rotation", "Y"));
    const double az = radians(n("Rotation", "Z"));
    const Mat rx{{{1, 0, 0}, {0, std::cos(ax), -std::sin(ax)}, {0, std::sin(ax), std::cos(ax)}}};
    const Mat ry{{{std::cos(ay), 0, std::sin(ay)}, {0, 1, 0}, {-std::sin(ay), 0, std::cos(ay)}}};
    const Mat rz{{{std::cos(az), std::sin(az), 0}, {-std::sin(az), std::cos(az), 0}, {0, 0, 1}}};
    const Mat rotation = multiply(rz, multiply(ry, rx));
    const Mat linear = multiply(rotation, scaleSkew);
    const Vec axis{n("Axis", "X") * p.unit, n("Axis", "Y") * p.unit, n("Axis", "Z") * p.unit};
    const Vec position{n("Position (3D)", "X") * p.unit, n("Position (3D)", "Y") * p.unit, n("Position (3D)", "Z") * p.unit};
    p.col0 = {linear[0][0], linear[1][0], linear[2][0]};
    p.col1 = {linear[0][1], linear[1][1], linear[2][1]};
    p.c = add(sub(axis, apply(rotation, axis)), position);
    // Perspective: "moves the perspective point (where parallel lines appear to converge)" (p.577). Ours: the
    // viewer two widths away; Z (positive closer) brings it in, to a fifth of a width at most.
    const double distance = std::max(2.0 * p.W * (1.0 - n("Perspective", "Z") / 1000.0), 0.2 * p.W);
    p.viewer = {n("Perspective", "X") * p.unit, n("Perspective", "Y") * p.unit, distance};
    p.targetX = n("Target", "X") * p.unit;
    p.targetY = n("Target", "Y") * p.unit;
    p.targetSize = std::max(n("Target", "Size"), 0.0) / 100.0;
    return p;
}

// The foreground as seen through the placement, with its border and highlight: one pixel's colour
// (premultiplied) at screen pixel centre (px, py); `shadowOnly` gives just its alpha.
struct Look {
    double border = 0;    // Border (3D) width in pixels, inside the crop
    double borderSoft = 0;
    float borderColour[4] = {0, 0, 0, 1};
    bool highlight = false;
    double hx = 0, hy = 0, hr = 1, hsoft = 0.5, hintensity = 0.5, hangle = 0, haspect = 0;
    float highlightColour[3] = {1, 1, 1};
    float opacity = 1.0f;
};

void lookAt(const Picture& fg, const Placement& p, const Look& look, double x, double y, float out[4]) {
    std::fill(out, out + 4, 0.0f);
    if (x < p.left || x > p.right || y < p.bottom || y > p.top) return;
    detail::sample(fg, p.W / 2.0 + x, p.H / 2.0 - y, out);
    if (look.border > 0.0) {
        // An internal border along the cropped edges (ours: the Plain type).
        const double inside = std::min({x - p.left, p.right - x, y - p.bottom, p.top - y});
        if (inside < look.border + look.borderSoft) {
            const double w = look.borderSoft > 0 ? std::clamp((look.border + look.borderSoft - inside) / look.borderSoft, 0.0, 1.0) : 1.0;
            for (int c = 0; c < 4; ++c) out[c] = out[c] * static_cast<float>(1.0 - w) + look.borderColour[c] * static_cast<float>(w);
        }
    }
    if (look.highlight && out[3] > 0.0f) {
        // "The default manual highlight creates a blurred disk in the center of the image" (p.576).
        const double cx = x - look.hx;
        const double cy = y - look.hy;
        const double ca = std::cos(look.hangle);
        const double sa = std::sin(look.hangle);
        double u = cx * ca + cy * sa;
        double v = -cx * sa + cy * ca;
        const double stretch = 1.0 + std::abs(look.haspect) * 4.0; // ours: ±100 is a band four times longer
        if (look.haspect > 0) u /= stretch; else if (look.haspect < 0) v /= stretch;
        const double r = std::hypot(u, v) / std::max(look.hr, 1e-6);
        const double edge = std::max(look.hsoft, 1e-3);
        const double w = std::clamp((1.0 - r) / edge, 0.0, 1.0) * look.hintensity;
        for (int c = 0; c < 3; ++c) out[c] = std::min(out[3], out[c] + static_cast<float>(w) * look.highlightColour[c] * out[3]);
    }
    for (int c = 0; c < 4; ++c) out[c] *= look.opacity;
}

} // namespace

bool is3DEffect(const model::Effect& effect) {
    return is3D(effect);
}

Picture warp3dPicture(const model::Effect& e, const Picture& foreground, const Picture& background, double time,
                      const model::EffectSpan& span) {
    const int w = foreground.width();
    const int h = foreground.height();
    // "Reverse Animation: Causes the entire effect to be reversed, including the direction of movement as
    // well as the incoming and outgoing sources" (p.574).
    const QString fgCat = QStringLiteral("Foreground (3D)");
    const bool reverse = flag(e, fgCat, QStringLiteral("Reverse Animation"));
    if (reverse) time = 1.0 - time;
    Picture fg = foreground;
    Picture bg = background;
    // "Swap Sources: Switches the foreground and background sources for the effect" (p.574).
    if (flag(e, fgCat, QStringLiteral("Swap Sources")) != reverse) std::swap(fg, bg);
    // "Use Background Color to replace the bottom video track with a background color" (p.570).
    if (enabled(e, QStringLiteral("Background Color (3D)"))) {
        bg = Picture::solid(w, h, colourOf(e, QStringLiteral("Background Color (3D)"), QStringLiteral("Background Color"), time, span, Qt::black));
    }
    // Defocus, Internal: "blurs the entire image" (p.572). Ours: Horz and V of 15 blur 15 pixels of a 1920-wide picture.
    const double unit = w / 1920.0;
    if (enabled(e, QStringLiteral("Defocus"))) {
        const double horz = number(e, QStringLiteral("Defocus"), QStringLiteral("Horz"), time, span);
        const double vert = number(e, QStringLiteral("Defocus"), QStringLiteral("V"), time, span);
        blur(fg, static_cast<int>(std::lround(std::max(horz, vert) * unit)));
    }
    const Placement place = placementOf(e, time, span, w, h);
    Look look;
    look.opacity = static_cast<float>(std::clamp(number(e, fgCat, QStringLiteral("Opac."), time, span), 0.0, 100.0) / 100.0);
    if (enabled(e, QStringLiteral("Border (3D)"))) {
        // "Wid (width) ... from 0 (no border) to 100, where 50 is the default"; "Sft ... from 0 (hard edge)
        // to 255" (p.571). Ours: width 100 is 40 pixels of a 1920-wide picture, softness 255 is 25.
        const QString cat = QStringLiteral("Border (3D)");
        look.border = std::clamp(number(e, cat, QStringLiteral("Wid"), time, span), 0.0, 100.0) * 0.4 * unit;
        look.borderSoft = std::clamp(number(e, cat, QStringLiteral("Sft"), time, span), 0.0, 255.0) / 255.0 * 25.0 * unit;
        const QColor colour = colourOf(e, cat, QStringLiteral("Color controls"), time, span, Qt::black);
        look.borderColour[0] = static_cast<float>(colour.redF());
        look.borderColour[1] = static_cast<float>(colour.greenF());
        look.borderColour[2] = static_cast<float>(colour.blueF());
    }
    if (enabled(e, QStringLiteral("Highlight"))) {
        // p.576: Soft, Inty and Rad 0-100 (50 default), Angle ±180, Aspct ±100, X and Y ±999. Ours: Rad 100
        // is half the picture's diagonal, X and Y ±999 the picture's edges.
        const QString cat = QStringLiteral("Highlight");
        look.highlight = true;
        look.hx = number(e, cat, QStringLiteral("X"), time, span) / 999.0 * w / 2.0;
        look.hy = number(e, cat, QStringLiteral("Y"), time, span) / 999.0 * h / 2.0;
        look.hr = number(e, cat, QStringLiteral("Rad"), time, span) / 100.0 * std::hypot(w, h) / 2.0;
        look.hsoft = number(e, cat, QStringLiteral("Soft"), time, span) / 100.0;
        look.hintensity = number(e, cat, QStringLiteral("Inty"), time, span) / 100.0;
        look.hangle = radians(number(e, cat, QStringLiteral("Angle"), time, span));
        look.haspect = number(e, cat, QStringLiteral("Aspct"), time, span) / 100.0;
        const QColor colour = colourOf(e, QStringLiteral("Highlight Color"), QStringLiteral("Highlight Color"), time, span, Qt::white);
        look.highlightColour[0] = static_cast<float>(colour.redF());
        look.highlightColour[1] = static_cast<float>(colour.greenF());
        look.highlightColour[2] = static_cast<float>(colour.blueF());
    }
    // Shadow: "places a shadow beneath an image ... offset and transparency" (p.579): X ±100 (50), Y -100 to 0
    // (-50), Opac. 50. Ours: an offset of 100 is 40 pixels of a 1920-wide picture.
    const bool shadow = enabled(e, QStringLiteral("Shadow"));
    const double shadowX = shadow ? number(e, QStringLiteral("Shadow"), QStringLiteral("X"), time, span) * 0.4 * unit : 0.0;
    const double shadowY = shadow ? number(e, QStringLiteral("Shadow"), QStringLiteral("Y"), time, span) * 0.4 * unit : 0.0;
    const float shadowOpacity = shadow ? static_cast<float>(std::clamp(number(e, QStringLiteral("Shadow"), QStringLiteral("Opac."), time, span), 0.0, 100.0) / 100.0) : 0.0f;

    Picture out = bg.isNull() ? Picture(w, h) : bg;
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            const double sx = px + 0.5 - w / 2.0;
            const double sy = h / 2.0 - (py + 0.5);
            float* o = out.pixel(px, py);
            if (shadow) {
                if (const auto at = place.planePoint(sx - shadowX, sy - shadowY)) {
                    float s[4];
                    lookAt(fg, place, look, at->first, at->second, s);
                    const float a = s[3] * shadowOpacity;
                    for (int c = 0; c < 4; ++c) o[c] *= 1.0f - a;
                    o[3] += a;
                }
            }
            if (const auto at = place.planePoint(sx, sy)) {
                float s[4];
                lookAt(fg, place, look, at->first, at->second, s);
                for (int c = 0; c < 4; ++c) o[c] = s[c] + o[c] * (1.0f - s[3]);
            }
        }
    }
    return out;
}

} // namespace lumenis::media
