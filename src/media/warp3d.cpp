#include "lumenis/media/compositor.h"
#include "lumenis/media/effect_handles.h"
#include "lumenis/media/tracking.h"

#include "effect_drawing.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
// screen; Perspective moves that point. The shapes (Ball, Page Fold, Slats) bend the plane, which is then
// drawn as a mesh of triangles into a depth buffer. Trail, Stamp, Erode/Expand and Spill Suppression
// came in 6.7a3; the forward geometry at the end serves direct manipulation (effect_handles.cpp, 6.7a4).
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
    Vec col2{}; // where a local Z goes (a shape's depth)
    double targetX = 0, targetY = 0, targetSize = 1;
    // Corner Pin: screen -> plane by homography instead.
    bool pinned = false;
    std::array<double, 9> pin{}, unpin{};
    Vec pivot{}; // where the Axis lands before it is seen (Axis plus Position), for its handle

    // Where a point in space (centred, Y up, Z towards the viewer) is seen on the screen (centred, Y up), with
    // Target; nothing behind the viewer.
    std::optional<std::pair<double, double>> project(const Vec& point) const {
        const double dz = viewer[2] - point[2];
        if (dz <= 1e-9) return std::nullopt;
        const double t = viewer[2] / dz;
        const double qx = viewer[0] + t * (point[0] - viewer[0]);
        const double qy = viewer[1] + t * (point[1] - viewer[1]);
        return std::pair{targetX + targetSize * qx, targetY + targetSize * qy};
    }
    // The screen point (centred, Y up) a plane point (centred, Y up) is seen at: planePoint()'s inverse.
    std::optional<std::pair<double, double>> screenPoint(double x, double y) const {
        if (pinned) {
            const double X = pin[0] * x + pin[1] * y + pin[2];
            const double Y = pin[3] * x + pin[4] * y + pin[5];
            const double Z = pin[6] * x + pin[7] * y + pin[8];
            if (std::abs(Z) < 1e-12) return std::nullopt;
            return std::pair{X / Z, Y / Z};
        }
        return project(add(add(Vec{col0[0] * x, col0[1] * x, col0[2] * x}, Vec{col1[0] * y, col1[1] * y, col1[2] * y}), c));
    }

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
            p.pin = pin;
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
    p.col2 = {linear[0][2], linear[1][2], linear[2][2]};
    p.c = add(sub(axis, apply(rotation, axis)), position);
    p.pivot = add(axis, position);
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
    int borderType = 0; // BorderType
    bool highlight = false;
    double hx = 0, hy = 0, hr = 1, hsoft = 0.5, hintensity = 0.5, hangle = 0, haspect = 0;
    float highlightColour[3] = {1, 1, 1};
    float opacity = 1.0f;
};

// Foreground (3D)'s key types (p.574-575), worked on the foreground before it is placed: what is keyed out
// becomes transparent so the background shows. Ours: Thresh, Soft and Range in percent of the brightness;
// H Tol. and H Soft in percent of half the colour wheel; S Low in percent of the saturation; L Tol. keeps what
// is brighter than it (100: nothing).
void keyForeground(const model::Effect& e, Picture& fg, double time, const model::EffectSpan& span) {
    const QString cat = QStringLiteral("Foreground (3D)");
    const QString type = e.parameters.value(cat).toObject().value(QStringLiteral("Type")).toString();
    const bool invert = flag(e, cat, QStringLiteral("Invert"));
    if (type == QLatin1String("SpectraMatte Key")) {
        // "access all the parameters of the high-quality SpectraMatte chroma key effect" (p.574).
        fg = detail::spectraMattePicture(e, fg, time, span);
    } else if (type == QLatin1String("Luma Key") || type == QLatin1String("Chroma Key")) {
        const bool luma = type == QLatin1String("Luma Key");
        auto n = [&](const QString& name) { return number(e, cat, name, time, span); };
        const double thresh = n(QStringLiteral("Thresh")) / 100.0;
        const double soft = std::max(n(QStringLiteral("Soft")) / 100.0, 1e-3);
        const bool lumRange = flag(e, cat, luma ? QStringLiteral("Lum Range") : QStringLiteral("Additional Parameters for Chroma Key Key Type: Lum Range"));
        const double range = n(luma ? QStringLiteral("Range") : QStringLiteral("Additional Parameters for Chroma Key Key Type: Range")) / 100.0;
        const QColor keyColour = colourOf(e, cat, QStringLiteral("Color"), time, span, Qt::blue);
        const double keyHue = keyColour.hsvHueF() < 0 ? 0.0 : static_cast<double>(keyColour.hsvHueF()) * 360.0;
        const double hTol = n(QStringLiteral("H Tol.")) / 100.0 * 180.0;
        const double hSoft = std::max(n(QStringLiteral("H Soft")) / 100.0 * 180.0, 1e-3);
        const double sLow = n(QStringLiteral("S Low")) / 100.0;
        const double lTol = n(QStringLiteral("L Tol.")) / 100.0;
        for (std::size_t i = 0; i < fg.data().size(); i += 4) {
            float* px = &fg.data()[i];
            if (px[3] <= 0.0f) continue;
            const double r = static_cast<double>(px[0] / px[3]), g = static_cast<double>(px[1] / px[3]), b = static_cast<double>(px[2] / px[3]);
            const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            double keep = 1.0; // 1: foreground shows
            if (luma) {
                // "All brightness values above the Thresh value display"; with Lum Range, Thresh is the middle of
                // the range kept (p.575).
                keep = lumRange ? std::clamp((range / 2.0 - std::abs(y - thresh)) / soft + 0.5, 0.0, 1.0)
                                : std::clamp((y - thresh) / soft + 0.5, 0.0, 1.0);
            } else {
                const QColor c = QColor::fromRgbF(static_cast<float>(std::clamp(r, 0.0, 1.0)), static_cast<float>(std::clamp(g, 0.0, 1.0)),
                                                  static_cast<float>(std::clamp(b, 0.0, 1.0)));
                const double hue = c.hsvHueF() < 0 ? 0.0 : static_cast<double>(c.hsvHueF()) * 360.0;
                double dh = std::abs(hue - keyHue);
                dh = std::min(dh, 360.0 - dh);
                const double hueKey = std::clamp((hTol + hSoft - dh) / hSoft, 0.0, 1.0);
                const double satKey = std::clamp((static_cast<double>(c.hsvSaturationF()) - sLow) / 0.05 + 0.5, 0.0, 1.0);
                double key = hueKey * satKey;
                if (y > lTol) key = 0.0;
                if (lumRange && std::abs(y - 0.5) > range / 2.0) key = 0.0;
                keep = 1.0 - key;
            }
            if (invert) keep = 1.0 - keep;
            for (int c = 0; c < 4; ++c) px[c] *= static_cast<float>(keep);
        }
    } else if (invert) {
        // Full Key inverted: what was opaque is clear and the reverse.
        for (std::size_t i = 0; i < fg.data().size(); i += 4) {
            float* px = &fg.data()[i];
            const float a = px[3];
            const float scale = a > 0.0f ? (1.0f - a) / a : 0.0f;
            for (int c = 0; c < 3; ++c) px[c] *= scale;
            px[3] = 1.0f - a;
        }
    }
    // "Show Alpha: Displays the grayscale alpha channel Media Composer uses to apply the key effect" (p.574).
    if (flag(e, cat, QStringLiteral("Show Alpha"))) {
        for (std::size_t i = 0; i < fg.data().size(); i += 4) {
            float* px = &fg.data()[i];
            px[0] = px[1] = px[2] = px[3];
            px[3] = 1.0f;
        }
    }
}

// The Border (3D) types (p.570-571): the colour of the band at `t` (0 at the picture's outer edge, 1 at the
// band's inner edge), `nearLight` when the nearest edge is the top or left. "The default border color is
// black for Plain borders and shades of gray for other border types." Ours: Metal a brushed grey, Bevel 1 lit
// from the top left and Bevel 2 from the bottom right, 3D Frame both in turn, Round a rounded profile, Video
// the picture itself at half brightness.
enum class BorderType { Plain, Metal, Bevel1, Bevel2, Frame3D, Round, Video };

void lookAt(const Picture& fg, const Placement& p, const Look& look, double x, double y, float out[4]) {
    std::fill(out, out + 4, 0.0f);
    if (x < p.left || x > p.right || y < p.bottom || y > p.top) return;
    detail::sample(fg, p.W / 2.0 + x, p.H / 2.0 - y, out);
    if (look.border > 0.0) {
        // An internal border along the cropped edges (ours: the Plain type).
        const double inside = std::min({x - p.left, p.right - x, y - p.bottom, p.top - y});
        if (inside < look.border + look.borderSoft) {
            const double w = look.borderSoft > 0 ? std::clamp((look.border + look.borderSoft - inside) / look.borderSoft, 0.0, 1.0) : 1.0;
            float band[4] = {look.borderColour[0], look.borderColour[1], look.borderColour[2], 1.0f};
            const auto type = static_cast<BorderType>(look.borderType);
            if (type != BorderType::Plain) {
                const double t = std::clamp(inside / std::max(look.border, 1e-6), 0.0, 1.0);
                const bool nearLight = std::min(x - p.left, p.top - y) < std::min(p.right - x, y - p.bottom);
                double grey = 0.6;
                switch (type) {
                case BorderType::Metal: grey = 0.65 + 0.15 * std::sin(t * std::numbers::pi * 6.0); break;
                case BorderType::Bevel1: grey = nearLight ? 0.85 : 0.3; break;
                case BorderType::Bevel2: grey = nearLight ? 0.3 : 0.85; break;
                case BorderType::Frame3D: grey = (t < 0.5) == nearLight ? 0.85 : 0.3; break;
                case BorderType::Round: grey = 0.25 + 0.65 * std::sin(t * std::numbers::pi); break;
                case BorderType::Video:
                case BorderType::Plain: break;
                }
                if (type == BorderType::Video) {
                    for (int c = 0; c < 3; ++c) band[c] = out[c] * 0.5f;
                    band[3] = out[3];
                } else {
                    for (int c = 0; c < 3; ++c) band[c] = static_cast<float>(grey);
                }
            }
            for (int c = 0; c < 4; ++c) out[c] = out[c] * static_cast<float>(1.0 - w) + band[c] * static_cast<float>(w);
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

// ---------------------------------------------------------------- shapes (6.7a2)

// "3D shape effects let you map the foreground video channel to various geometric shapes" (p.579): the
// plane is bent, so it is drawn as a fine mesh of triangles (each vertex bent, placed and projected) into a
// depth buffer instead of being met ray by ray.
enum class ShapeType { None, PageFold, Ball, Slats };

struct Shape {
    ShapeType type = ShapeType::None;
    // Page Fold: the curl's direction (where it starts, unit), how far the fold line has come in the plane's
    // units, the cylinder's radius, and the Tuck and Flap choice (0 over, 1 under, 2 top only, 3 bottom only).
    double dx = 0, dy = 1, line = 0, radius = 1;
    int tuck = 0;
    // Ball: centre, radius, depth (Curve, 0-1), the aspect's stretch along `angle`.
    double cx = 0, cy = 0, ballRadius = 1, curve = 1, stretch = 1, angle = 0;
    // Slats: the direction the slats move (unit), their width across it, how far they move, offset, weave.
    double mx = 1, my = 0, slat = 1, amplitude = 0, offset = 0, weave = 0, tilt = 0;
    // Auto Highlight (p.581): shade by how the surface faces the viewer; else manual.
    bool autoHighlight = true;
};

// The point a plane point (centred, Y up) goes to, with its depth; nothing where the shape hides it.
std::optional<Vec> bend(const Shape& sh, double x, double y) {
    switch (sh.type) {
    case ShapeType::None:
        return Vec{x, y, 0.0};
    case ShapeType::PageFold: {
        // The part of the page past the fold line rolls over a cylinder and, past half a turn, lies flat on top.
        const double along = x * sh.dx + y * sh.dy;
        const double excess = along - sh.line;
        if (excess <= 0) {
            if (sh.tuck == 3) return std::nullopt; // Show Bottom Only: the flap alone
            return Vec{x, y, 0.0};
        }
        if (sh.tuck == 2) return std::nullopt; // Show Top Only
        const double side = sh.tuck == 1 ? -1.0 : 1.0;
        double back = 0, z = 0;
        if (excess < std::numbers::pi * sh.radius) {
            const double theta = excess / sh.radius;
            back = sh.radius * std::sin(theta);
            z = sh.radius * (1.0 - std::cos(theta));
        } else {
            back = -(excess - std::numbers::pi * sh.radius);
            z = 2.0 * sh.radius;
        }
        const double newAlong = sh.line + back;
        return Vec{x + (newAlong - along) * sh.dx, y + (newAlong - along) * sh.dy, side * z};
    }
    case ShapeType::Ball: {
        // "a rubbery, rectangular sheet stretched around a sphere ... If the radius is small, the corners
        // disappear, wrapping completely around the sphere's surface" (p.580).
        const double ca = std::cos(sh.angle), sa = std::sin(sh.angle);
        const double u = ((x - sh.cx) * ca + (y - sh.cy) * sa) / sh.stretch;
        const double v = -(x - sh.cx) * sa + (y - sh.cy) * ca;
        const double rho = std::hypot(u, v);
        const double phi = rho / std::max(sh.ballRadius, 1e-6);
        if (phi >= std::numbers::pi) return std::nullopt;
        const double k = rho > 1e-9 ? sh.ballRadius * std::sin(phi) / rho : 1.0;
        const double bu = u * k * sh.stretch, bv = v * k;
        const double z = sh.curve * sh.ballRadius * (std::cos(phi) - 1.0);
        return Vec{sh.cx + bu * ca - bv * sa, sh.cy + bu * sa + bv * ca, z};
    }
    case ShapeType::Slats: {
        // "Splits the image into alternating bands of video that enter the screen from opposite directions"
        // (p.581): each band moves along the slats' direction, every other one the other way.
        const double across = -x * sh.my + y * sh.mx + sh.offset;
        const auto band = static_cast<long long>(std::floor(across / sh.slat));
        const double dir = (band % 2 == 0) ? 1.0 : -1.0;
        const double along = x * sh.mx + y * sh.my;
        const double z = sh.weave * dir * std::sin(along / std::max(sh.slat, 1e-6) * std::numbers::pi) +
                         sh.tilt * (across - (static_cast<double>(band) + 0.5) * sh.slat);
        return Vec{x + dir * sh.amplitude * sh.mx, y + dir * sh.amplitude * sh.my, z};
    }
    }
    return std::nullopt;
}

double parameterOr(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span,
                   double fallback) {
    return model::numberAt(e, category, name, time, span).value_or(fallback);
}

QString menuValue(const model::Effect& e, const QString& category, const QString& name) {
    return e.parameters.value(category).toObject().value(name).toString();
}

// The shape the effect asks for at `time`. The Xpress 3D effects name theirs; 3D Warp's Shape category is
// on once set, a Page Fold until another Type is chosen. "Each shape effect contains a default template ...
// when you first apply a Page Fold shape effect, Media Composer creates a simple page curl" (p.580). Ours:
// until set, the curl (Page Fold), the slats' Ampl and the ball's Radius run over the effect, taking the
// picture off the screen on a segment and bringing the incoming one on over a transition.
Shape shapeOf(const model::Effect& e, double time, const model::EffectSpan& span, double W, double H) {
    Shape sh;
    const QString cat = QStringLiteral("Shape");
    QString type = menuValue(e, cat, QStringLiteral("Type"));
    if (type.isEmpty()) {
        if (e.name == QLatin1String("3D Ball")) type = QStringLiteral("Ball");
        else if (e.name == QLatin1String("3D Page Fold")) type = QStringLiteral("Page Fold");
        else if (e.name == QLatin1String("3D Slats")) type = QStringLiteral("Slats");
        else if (enabled(e, cat)) type = QStringLiteral("Page Fold");
    }
    const double progress = span.transition ? 1.0 - time : time;
    const QString highlight = menuValue(e, cat, QStringLiteral("Highlight"));
    sh.autoHighlight = highlight.isEmpty() || highlight == QLatin1String("Auto Highlight");
    const double half = std::hypot(W, H) / 2.0;
    if (type == QLatin1String("Page Fold")) {
        sh.type = ShapeType::PageFold;
        // Angle: 0 folds the top edge down, 90 the right edge, 45 the top right corner (the pictures on p.583).
        const double a = radians(parameterOr(e, cat, QStringLiteral("Angle"), time, span, 45.0));
        sh.dx = std::sin(a);
        sh.dy = std::cos(a);
        // Radius 0-100, "15 is the default"; ours, 100 is a quarter of the picture's height.
        sh.radius = std::max(parameterOr(e, cat, QStringLiteral("Radius"), time, span, 15.0), 0.5) / 100.0 * H / 4.0;
        // Curl 0-100, "0 represents no rollup"; 100 takes the page off the screen (p.581).
        const double curl = std::clamp(parameterOr(e, cat, QStringLiteral("Curl"), time, span, progress * 100.0), 0.0, 100.0) / 100.0;
        double hi = -1e300, lo = 1e300;
        for (const double cx : {-W / 2.0, W / 2.0}) {
            for (const double cy : {-H / 2.0, H / 2.0}) {
                const double s = cx * sh.dx + cy * sh.dy;
                hi = std::max(hi, s);
                lo = std::min(lo, s);
            }
        }
        // At 100 the fold line is half a turn past the far edge: the whole page has rolled over and its flap
        // lies beyond that edge, off the screen.
        sh.line = hi - curl * (hi - lo + std::numbers::pi * sh.radius);
        const QString tuck = menuValue(e, cat, QStringLiteral("Tuck and Flap"));
        sh.tuck = tuck == QLatin1String("Tuck Under") ? 1 : tuck == QLatin1String("Show Top Only") ? 2 : tuck == QLatin1String("Show Bottom Only") ? 3 : 0;
    } else if (type == QLatin1String("Ball")) {
        sh.type = ShapeType::Ball;
        const QString sub = QStringLiteral("Additional Parameters for the Ball Shape: ");
        // Ours: Radius 100 is half the picture's diagonal; Curve 0-100 the depth; X Pos and Y Pos ±999 to the
        // edges, Y Pos increasing down (p.581).
        sh.ballRadius = std::max(parameterOr(e, cat, sub + QStringLiteral("Radius"), time, span, (1.0 - progress) * 100.0), 0.5) / 100.0 * half;
        sh.curve = std::clamp(parameterOr(e, cat, QStringLiteral("Curve"), time, span, 50.0), 0.0, 100.0) / 100.0;
        sh.cx = parameterOr(e, cat, QStringLiteral("X Pos"), time, span, 0.0) / 999.0 * W / 2.0;
        sh.cy = -parameterOr(e, cat, QStringLiteral("Y Pos"), time, span, 0.0) / 999.0 * H / 2.0;
        const double aspect = std::clamp(parameterOr(e, cat, QStringLiteral("Aspct"), time, span, 0.0), -100.0, 100.0) / 100.0;
        sh.stretch = aspect >= 0 ? 1.0 + aspect * 3.0 : 1.0 / (1.0 - aspect * 3.0);
        sh.angle = radians(parameterOr(e, cat, sub + QStringLiteral("Angle"), time, span, 0.0));
    } else if (type == QLatin1String("Slats")) {
        sh.type = ShapeType::Slats;
        const QString sub = QStringLiteral("Additional Parameters for the Slats Shape: ");
        // "Use 0 for horizontal and 90 for vertical" (p.582). Ours: Freq is how many slats cross the picture
        // (4 until set), Ampl 100 moves them a picture's width (off the screen), Offset 100 a slat's width,
        // Sk Amp the weave's depth (100: a slat's width) and Sk Angl each slat's tilt in degrees.
        const double a = radians(parameterOr(e, cat, sub + QStringLiteral("Angle"), time, span, 0.0));
        sh.mx = std::cos(a);
        sh.my = std::sin(a);
        const double freq = std::max(parameterOr(e, cat, QStringLiteral("Freq"), time, span, 4.0), 1.0);
        const double extent = std::abs(-W / 2.0 * sh.my) + std::abs(H / 2.0 * sh.mx);
        sh.slat = 2.0 * extent / freq;
        sh.amplitude = parameterOr(e, cat, QStringLiteral("Ampl"), time, span, progress * 100.0) / 100.0 * std::hypot(W, H);
        sh.offset = parameterOr(e, cat, QStringLiteral("Offset"), time, span, 0.0) / 100.0 * sh.slat;
        sh.weave = parameterOr(e, cat, QStringLiteral("Sk Amp"), time, span, 0.0) / 100.0 * sh.slat;
        sh.tilt = std::tan(radians(std::clamp(parameterOr(e, cat, QStringLiteral("Sk Angl"), time, span, 0.0), -80.0, 80.0)));
    }
    return sh;
}

// A mesh vertex: where it lands on the screen (pixels), its depth (larger is nearer), the plane point it
// shows, and a shade from how it faces the viewer.
struct Vertex {
    double sx = 0, sy = 0, depth = 0, u = 0, v = 0, shade = 1;
    bool shown = false;
};

// Draws the bent plane into `layer` (premultiplied, transparent where it does not cover).
void drawMesh(Picture& layer, const Picture& fg, const Placement& p, const Look& look, const Shape& sh, double lightAmount) {
    const int W = layer.width();
    const int H = layer.height();
    const int cols = std::max(8, W / 6);
    const int rows = std::max(8, H / 6);
    std::vector<Vertex> verts(static_cast<std::size_t>((cols + 1) * (rows + 1)));
    auto index = [cols](int i, int j) { return static_cast<std::size_t>(j * (cols + 1) + i); };
    auto place = [&](const Vec& local, Vertex& out) {
        const Vec world = add(add(add({p.col0[0] * local[0], p.col0[1] * local[0], p.col0[2] * local[0]},
                                      Vec{p.col1[0] * local[1], p.col1[1] * local[1], p.col1[2] * local[1]}),
                                  Vec{p.col2[0] * local[2], p.col2[1] * local[2], p.col2[2] * local[2]}),
                              p.c);
        const double denom = p.viewer[2] - world[2];
        if (denom <= 1e-6) return false;
        const double f = p.viewer[2] / denom;
        const double x = p.viewer[0] + (world[0] - p.viewer[0]) * f;
        const double y = p.viewer[1] + (world[1] - p.viewer[1]) * f;
        out.sx = p.targetX + x * p.targetSize + W / 2.0;
        out.sy = H / 2.0 - (p.targetY + y * p.targetSize);
        out.depth = world[2];
        return true;
    };
    for (int j = 0; j <= rows; ++j) {
        for (int i = 0; i <= cols; ++i) {
            Vertex& v = verts[index(i, j)];
            v.u = p.left + (p.right - p.left) * i / cols;
            v.v = p.top - (p.top - p.bottom) * j / rows;
            const auto bent = bend(sh, v.u, v.v);
            v.shown = bent && place(*bent, v);
        }
    }
    // Shading from the bent surface's slope (Auto Highlight): flat parts as they are, slopes darker, the
    // tops of curls brighter.
    if (sh.autoHighlight && lightAmount > 0) {
        for (int j = 0; j <= rows; ++j) {
            for (int i = 0; i <= cols; ++i) {
                const auto here = bend(sh, verts[index(i, j)].u, verts[index(i, j)].v);
                const auto right = bend(sh, verts[index(std::min(i + 1, cols), j)].u + (i == cols ? 1e-3 : 0), verts[index(std::min(i + 1, cols), j)].v);
                const auto down = bend(sh, verts[index(i, std::min(j + 1, rows))].u, verts[index(i, std::min(j + 1, rows))].v - (j == rows ? 1e-3 : 0));
                if (!here || !right || !down) continue;
                const Vec n = cross(sub(*right, *here), sub(*down, *here));
                const double len = std::sqrt(dot(n, n));
                if (len < 1e-12) continue;
                const double facing = std::abs(n[2]) / len;
                verts[index(i, j)].shade = 1.0 + lightAmount * (facing - 0.85);
            }
        }
    }
    std::vector<double> depth(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), -1e300);
    auto triangle = [&](const Vertex& a, const Vertex& b, const Vertex& c) {
        if (!a.shown || !b.shown || !c.shown) return;
        const double area = (b.sx - a.sx) * (c.sy - a.sy) - (c.sx - a.sx) * (b.sy - a.sy);
        if (std::abs(area) < 1e-12) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.sx, b.sx, c.sx}))));
        const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({a.sx, b.sx, c.sx}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.sy, b.sy, c.sy}))));
        const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({a.sy, b.sy, c.sy}))));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                const double w0 = ((b.sx - px) * (c.sy - py) - (c.sx - px) * (b.sy - py)) / area;
                const double w1 = ((c.sx - px) * (a.sy - py) - (a.sx - px) * (c.sy - py)) / area;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                const double z = w0 * a.depth + w1 * b.depth + w2 * c.depth;
                double& zb = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)];
                if (z <= zb) continue;
                float s[4];
                lookAt(fg, p, look, w0 * a.u + w1 * b.u + w2 * c.u, w0 * a.v + w1 * b.v + w2 * c.v, s);
                if (s[3] <= 0.0f) continue;
                zb = z;
                const float shade = static_cast<float>(std::clamp(w0 * a.shade + w1 * b.shade + w2 * c.shade, 0.0, 2.0));
                float* o = layer.pixel(x, y);
                for (int k = 0; k < 3; ++k) o[k] = std::min(s[3], s[k] * shade);
                o[3] = s[3];
            }
        }
    };
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            triangle(verts[index(i, j)], verts[index(i + 1, j)], verts[index(i + 1, j + 1)]);
            triangle(verts[index(i, j)], verts[index(i + 1, j + 1)], verts[index(i, j + 1)]);
        }
    }
}

} // namespace

bool is3DEffect(const model::Effect& effect) {
    return is3D(effect);
}

namespace {

// Spill Suppression (p.584): "replaces one color with another color" in the foreground. Ours: Target Color
// green until set (or the Chroma Key's colour with Use Key Color), Replace Color grey; Gain 0-63 the hue
// tolerance up to a quarter of the colour wheel, Soft 0-63 a blend over another quarter.
void suppressSpill(const model::Effect& e, Picture& fg, double time, const model::EffectSpan& span) {
    const QString cat = QStringLiteral("Spill Suppression");
    if (!enabled(e, cat)) return;
    const QColor target = flag(e, cat, QStringLiteral("Use Key Color"))
                              ? colourOf(e, QStringLiteral("Foreground (3D)"), QStringLiteral("Color"), time, span, Qt::blue)
                              : colourOf(e, cat, QStringLiteral("Target Color"), time, span, Qt::green);
    const QColor replace = colourOf(e, cat, QStringLiteral("Replace Color"), time, span, QColor(128, 128, 128));
    const double targetHue = target.hsvHueF() < 0 ? 0.0 : static_cast<double>(target.hsvHueF()) * 360.0;
    const double tolerance = std::clamp(number(e, cat, QStringLiteral("Gain"), time, span), 0.0, 63.0) / 63.0 * 90.0;
    const double soft = std::max(std::clamp(number(e, cat, QStringLiteral("Soft"), time, span), 0.0, 63.0) / 63.0 * 90.0, 1.0);
    const float rr = static_cast<float>(replace.redF()), rg = static_cast<float>(replace.greenF()), rb = static_cast<float>(replace.blueF());
    for (std::size_t i = 0; i < fg.data().size(); i += 4) {
        float* px = &fg.data()[i];
        if (px[3] <= 0.0f) continue;
        const QColor c = QColor::fromRgbF(std::clamp(px[0] / px[3], 0.0f, 1.0f), std::clamp(px[1] / px[3], 0.0f, 1.0f),
                                          std::clamp(px[2] / px[3], 0.0f, 1.0f));
        if (c.hsvSaturationF() < 0.1f) continue;
        double dh = std::abs((c.hsvHueF() < 0 ? 0.0 : static_cast<double>(c.hsvHueF()) * 360.0) - targetHue);
        dh = std::min(dh, 360.0 - dh);
        const float w = static_cast<float>(std::clamp((tolerance + soft - dh) / soft, 0.0, 1.0));
        if (w <= 0.0f) continue;
        px[0] += (rr * px[3] - px[0]) * w;
        px[1] += (rg * px[3] - px[1]) * w;
        px[2] += (rb * px[3] - px[2]) * w;
    }
}

// Erode/Expand: "controls the blending of the edges on 3D Warp effects ... not keyframeable" (p.573): Filter
// 1-7 pixels each side, Center 0 (most erosion) to 255 (most expansion), Soft 0 (a vertical slope) to 255.
// Ours: the layer's alpha blurred over the Filter's pixels (of a 1920-wide picture), cut at a level set by
// Center (128 leaves the edge where it was) and softened by Soft.
void erodeExpand(const model::Effect& e, Picture& layer, double time, const model::EffectSpan& span) {
    const QString cat = QStringLiteral("Erode/Expand");
    if (!enabled(e, cat)) return;
    const double unit = layer.width() / 1920.0;
    const int radius = std::max(1, static_cast<int>(std::lround(std::clamp(number(e, cat, QStringLiteral("Filter"), time, span), 1.0, 7.0) * unit)));
    const double center = std::clamp(number(e, cat, QStringLiteral("Center"), time, span), 0.0, 255.0) / 255.0;
    const double soft = std::max(std::clamp(number(e, cat, QStringLiteral("Soft"), time, span), 0.0, 255.0) / 255.0, 0.02);
    Picture blurred = layer;
    blur(blurred, radius);
    // The alpha level kept as the edge: 0.98 at Center 0 (only what is fully inside stays) to 0.02 at 255.
    const double level = 0.98 - 0.96 * center;
    for (std::size_t i = 0; i < layer.data().size(); i += 4) {
        const float* b = &blurred.data()[i];
        float* o = &layer.data()[i];
        const float a = static_cast<float>(std::clamp((static_cast<double>(b[3]) - level) / soft + 0.5, 0.0, 1.0));
        if (b[3] <= 1e-6f) {
            std::fill(o, o + 4, 0.0f);
            continue;
        }
        for (int c = 0; c < 3; ++c) o[c] = b[c] / b[3] * a;
        o[3] = a;
    }
}

// The foreground placed by the effect at `time`, on a transparent picture: spill, key and defocus, then the
// plane or the shape with its border and highlight.
Picture placedLayer(const model::Effect& e, const Picture& source, double time, const model::EffectSpan& span) {
    const int w = source.width();
    const int h = source.height();
    const QString fgCat = QStringLiteral("Foreground (3D)");
    Picture fg = source;
    suppressSpill(e, fg, time, span);
    keyForeground(e, fg, time, span);
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
        static const char* const kTypes[] = {"Plain", "Metal", "Bevel 1", "Bevel 2", "3D Frame", "Round", "Video"};
        const QString type = e.parameters.value(cat).toObject().value(QStringLiteral("Type")).toString();
        for (int k = 0; k < 7; ++k) {
            if (type == QLatin1String(kTypes[k])) look.borderType = k;
        }
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
    Picture layer(w, h);
    const Shape shape = place.pinned ? Shape{} : shapeOf(e, time, span, w, h);
    if (shape.type != ShapeType::None) {
        // A shape: the bent plane drawn as a mesh.
        Look shaped = look;
        if (shape.autoHighlight) shaped.highlight = false; // the manual disc only with Manual Highlight
        const double light = enabled(e, QStringLiteral("Highlight")) ? number(e, QStringLiteral("Highlight"), QStringLiteral("Inty"), time, span) / 50.0 : 1.0;
        drawMesh(layer, fg, place, shaped, shape, light);
        return layer;
    }
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            if (const auto at = place.planePoint(px + 0.5 - w / 2.0, h / 2.0 - (py + 0.5))) lookAt(fg, place, look, at->first, at->second, layer.pixel(px, py));
        }
    }
    return layer;
}

// A cheap hash of a pixel and a copy's number, 0 to 1, for Trail's sparkle.
float sparkle(int x, int y, int k) {
    auto v = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u ^ static_cast<std::uint32_t>(k) * 83492791u;
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return static_cast<float>(v & 0xffffu) / 65535.0f;
}

} // namespace

Picture warp3dLayer(const model::Effect& effect, const Picture& foreground, double time, const model::EffectSpan& span) {
    return placedLayer(effect, foreground, time, span);
}

namespace {

QList<double> timesOf(const model::Effect& e, const char* key) {
    QList<double> out;
    for (const QJsonValue& v : e.parameters.value(QStringLiteral("Stamp")).toObject().value(QLatin1String(key)).toArray()) {
        if (v.isDouble()) out << v.toDouble();
    }
    return out;
}

} // namespace

QList<double> stampTimes(const model::Effect& effect) {
    return is3D(effect) ? timesOf(effect, "Stamps") : QList<double>{};
}

QList<double> clearTimes(const model::Effect& effect) {
    return is3D(effect) ? timesOf(effect, "Clears") : QList<double>{};
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
    Picture out = bg.isNull() ? Picture(w, h) : bg;
    Picture layer = placedLayer(e, fg, time, span);
    // Corner Tracking (p.571): the placed picture moved as the trackers move from their reference frame.
    if (const auto map = trackedTransform(e, time, span, h > 0 ? static_cast<double>(w) / h : 1.0)) layer = transformed(layer, *map);
    erodeExpand(e, layer, time, span);
    const double unit = w / 1920.0;
    // "Shadow, Trail, and Stamp are mutually exclusive, so you can have only one enabled at a time" (p.579).
    // Ours: with more than one set, Shadow wins, then Trail.
    const bool shadow = enabled(e, QStringLiteral("Shadow"));
    const bool trail = !shadow && enabled(e, QStringLiteral("Trail"));
    if (shadow) {
        // Shadow: "places a shadow beneath an image ... offset and transparency" (p.579): X ±100 (50), Y -100 to
        // 0 (-50), Opac. 50. Ours: an offset of 100 is 40 pixels of a 1920-wide picture.
        const int ox = static_cast<int>(std::lround(number(e, QStringLiteral("Shadow"), QStringLiteral("X"), time, span) * 0.4 * unit));
        const int oy = static_cast<int>(std::lround(-number(e, QStringLiteral("Shadow"), QStringLiteral("Y"), time, span) * 0.4 * unit));
        const float opacity = static_cast<float>(std::clamp(number(e, QStringLiteral("Shadow"), QStringLiteral("Opac."), time, span), 0.0, 100.0) / 100.0);
        for (int py = 0; py < h; ++py) {
            for (int px = 0; px < w; ++px) {
                const int qx = px - ox, qy = py - oy;
                if (qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                const float a = layer.pixel(qx, qy)[3] * opacity;
                float* o = out.pixel(px, py);
                for (int c = 0; c < 4; ++c) o[c] *= 1.0f - a;
                o[3] += a;
            }
        }
    } else if (trail) {
        // "Use Trail to add a trail to an effect as it moves across the screen ... it is a cumulative effect"
        // (p.585); Decay 0-100 (75). Ours: the effect as it was on the frames before, each copy Decay percent
        // as opaque as the one after it, until they fade below 5%; Use Sparkle breaks the copies into
        // glittering dots. Worked from the effect's own times, so the trail shows when stepping through too.
        const double decay = std::clamp(number(e, QStringLiteral("Trail"), QStringLiteral("Decay"), time, span), 0.0, 99.0) / 100.0;
        const bool sparkly = flag(e, QStringLiteral("Trail"), QStringLiteral("Use Sparkle"));
        const double step = span.timeOf(1.0) - span.timeOf(0.0);
        std::vector<std::pair<int, double>> copies;
        double opacity = decay;
        for (int k = 1; k <= 32 && opacity >= 0.05 && time - k * step >= -1e-9; ++k, opacity *= decay) copies.push_back({k, opacity});
        for (auto it = copies.rbegin(); it != copies.rend(); ++it) {
            Picture copy = faded(placedLayer(e, fg, time - it->first * step, span), it->second);
            if (sparkly) {
                for (int py = 0; py < h; ++py) {
                    for (int px = 0; px < w; ++px) {
                        const float keep = sparkle(px, py, it->first) > 0.7f ? 1.0f : 0.0f;
                        float* c = copy.pixel(px, py);
                        for (int ch = 0; ch < 4; ++ch) c[ch] *= keep;
                    }
                }
            }
            out = over(copy, out);
        }
    }
    return over(layer, out);
}

// ---------------------------------------------------------------- forward geometry (6.7a4)

namespace {

// The placement the picture is drawn with at `time` ("Reverse Animation" runs the effect backwards, as
// warp3dPicture does).
Placement shownPlacement(const model::Effect& e, double time, const model::EffectSpan& span, const QSizeF& size) {
    if (flag(e, QStringLiteral("Foreground (3D)"), QStringLiteral("Reverse Animation"))) time = 1.0 - time;
    return placementOf(e, time, span, static_cast<int>(std::lround(size.width())), static_cast<int>(std::lround(size.height())));
}

QPointF toPixels(const std::pair<double, double>& p, const QSizeF& size) {
    return {size.width() / 2.0 + p.first, size.height() / 2.0 - p.second};
}

} // namespace

std::optional<QPointF> warp3dScreenPoint(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size,
                                         double x, double y) {
    const auto s = shownPlacement(effect, time, span, size).screenPoint(x, y);
    if (!s) return std::nullopt;
    return toPixels(*s, size);
}

std::optional<QPointF> warp3dAxisPoint(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size) {
    const Placement p = shownPlacement(effect, time, span, size);
    if (p.pinned) return std::nullopt;
    const auto s = p.project(p.pivot);
    if (!s) return std::nullopt;
    return toPixels(*s, size);
}

std::array<double, 4> warp3dCropBox(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size) {
    const Placement p = shownPlacement(effect, time, span, size);
    return {p.left, p.right, p.bottom, p.top};
}

} // namespace lumenis::media
