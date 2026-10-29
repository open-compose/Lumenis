#include "lumenis/media/compositor.h"

#include "lumenis/model/effect_keyframes.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// The 2D transitions (ROADMAP 6.3a; docs/spec/wipes.md): the Box Wipe, Conceal, Edge Wipe, L-Conceal,
// Matrix Wipe, Push, Sawtooth Wipe, Shape Wipe, Spin and Squeeze effects of the Effects Reference (FX
// Guide p.459-490). Each is a geometry: for every pixel, how far it lies from the incoming picture's
// region (negative inside), and where in the incoming and outgoing pictures it comes from. One routine
// then draws the outgoing picture, the border band around the incoming region (Border, p.493) and the
// incoming picture, with soft edges. Where the guide shows a picture but no numbers, the geometry is ours
// and is said so.
namespace lumenis::media {

namespace {

constexpr double kOutside = 1e9; // the distance of a pixel no region is near

// One pixel's place in a wipe.
struct Place {
    double distance = kOutside; // to the incoming region, in pixels; negative inside it
    double inX = 0, inY = 0;    // where it is in the incoming picture
    double outX = 0, outY = 0;  // and in the outgoing one
    bool outgoingShown = true;  // false where the outgoing picture has been squeezed away (Spin)
};

// Signed distance from (x, y) to the box [x0, x1] x [y0, y1].
double boxDistance(double x, double y, double x0, double y0, double x1, double y1) {
    const double dx = std::max(x0 - x, x - x1);
    const double dy = std::max(y0 - y, y - y1);
    if (dx <= 0 && dy <= 0) return std::max(dx, dy);
    return std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
}

// Distance from (x, y) to the ray from (cx, cy) in direction (ux, uy) (a unit vector).
double rayDistance(double x, double y, double cx, double cy, double ux, double uy) {
    const double t = std::max((x - cx) * ux + (y - cy) * uy, 0.0);
    return std::hypot(x - (cx + ux * t), y - (cy + uy * t));
}

// The corner or side an effect starts from, by its name: "Top Left to Bottom Right" and "Top Left"
// start top left, "Bottom Box" at the bottom; as fractions of the picture (0, 0.5 or 1).
struct Anchor {
    double x = 0.5, y = 0.5;
};

Anchor anchorOf(const QString& name) {
    const QString start = name.section(QStringLiteral(" to "), 0, 0);
    Anchor a;
    if (start.contains(QLatin1String("Left"))) a.x = 0.0;
    if (start.contains(QLatin1String("Right"))) a.x = 1.0;
    if (start.contains(QLatin1String("Top")) || start.contains(QLatin1String("Upper"))) a.y = 0.0;
    if (start.contains(QLatin1String("Bottom")) || start.contains(QLatin1String("Lower"))) a.y = 1.0;
    return a;
}

// A Matrix Wipe's order of its cells (p.476): the place in the order of each cell, row by row.
QList<int> matrixOrder(const QString& name, int columns, int rows) {
    const int n = columns * rows;
    QList<int> cells; // cells in the order they are revealed
    cells.reserve(n);
    if (name == QLatin1String("Spiral")) {
        // Ours: round the edge clockwise from the top left, then inwards.
        int top = 0, bottom = rows - 1, left = 0, right = columns - 1;
        while (top <= bottom && left <= right) {
            for (int c = left; c <= right; ++c) cells.append(top * columns + c);
            for (int r = top + 1; r <= bottom; ++r) cells.append(r * columns + right);
            if (top < bottom) {
                for (int c = right - 1; c >= left; --c) cells.append(bottom * columns + c);
            }
            if (left < right) {
                for (int r = bottom - 1; r > top; --r) cells.append(r * columns + left);
            }
            ++top, --bottom, ++left, --right;
        }
    } else if (name == QLatin1String("Zig-Zag")) {
        // Ours: rows left to right, then right to left.
        for (int r = 0; r < rows; ++r) {
            for (int i = 0; i < columns; ++i) cells.append(r * columns + (r % 2 ? columns - 1 - i : i));
        }
    } else {
        for (int i = 0; i < n; ++i) cells.append(i);
        if (name == QLatin1String("Speckle")) {
            // Ours: a shuffle by a fixed generator, the same on every platform.
            std::uint32_t seed = 0x2545F491u;
            for (int i = n - 1; i > 0; --i) {
                seed = seed * 1664525u + 1013904223u;
                std::swap(cells[i], cells[static_cast<int>((seed >> 8) % static_cast<std::uint32_t>(i + 1))]);
            }
        }
    }
    QList<int> order(n, 0);
    for (int i = 0; i < n; ++i) order[cells.at(i)] = i;
    return order;
}

// The bilinear sample of `p` at (x, y), pixel centres at whole numbers + 0.5; transparent outside it.
void sample(const Picture& p, double x, double y, float out[4]) {
    std::fill(out, out + 4, 0.0f);
    const double fx = x - 0.5;
    const double fy = y - 0.5;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float ax = static_cast<float>(fx - x0);
    const float ay = static_cast<float>(fy - y0);
    // Edge pixels are held out to the picture's edge, so a whole picture is opaque to its border.
    if (x < 0 || y < 0 || x > p.width() || y > p.height()) return;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const int px = std::clamp(x0 + i, 0, p.width() - 1);
            const int py = std::clamp(y0 + j, 0, p.height() - 1);
            const float w = (i ? ax : 1.0f - ax) * (j ? ay : 1.0f - ay);
            const float* s = p.pixel(px, py);
            for (int c = 0; c < 4; ++c) out[c] += s[c] * w;
        }
    }
}

int gridValue(const model::Effect& e, const QString& name, int fallback) {
    const int v = e.parameters.value(QStringLiteral("Custom Grid")).toObject().value(name).toInt(fallback);
    return std::max(v, 2); // "The minimum number of rows and columns is 2 x 2" (p.494)
}

} // namespace

bool isWipe(const model::Effect& effect) {
    static const QStringList categories = {
        QStringLiteral("Box Wipe"),      QStringLiteral("Conceal"),    QStringLiteral("Edge Wipe"),
        QStringLiteral("L-Conceal"),     QStringLiteral("Matrix Wipe"), QStringLiteral("Push"),
        QStringLiteral("Sawtooth Wipe"), QStringLiteral("Shape Wipe"), QStringLiteral("Spin"),
        QStringLiteral("Squeeze")};
    return categories.contains(effect.category);
}

Picture wipePicture(const model::Effect& e, Picture outgoing, Picture incoming, double p, double time,
                    const model::EffectSpan& span) {
    const int width = incoming.width();
    const int height = incoming.height();
    Picture result(width, height);
    if (result.isNull()) return result;
    const QString foreground = QStringLiteral("Foreground");
    auto flag = [&](const QString& category, const QString& name) {
        return e.parameters.value(category).toObject().value(name).toBool();
    };
    // "Reverses the entire effect, including the direction of movement as well as the incoming and
    // outgoing sources" (p.495).
    if (flag(foreground, QStringLiteral("Reverse Animation"))) {
        std::swap(outgoing, incoming);
        p = 1.0 - p;
    }
    p = std::clamp(p, 0.0, 1.0);
    const double W = width;
    const double H = height;
    const QString& category = e.category;
    const QString& name = e.name;
    const Anchor anchor = anchorOf(name);
    const int columns = gridValue(e, QStringLiteral("Columns"), 8); // "the default number is 8 x 6" (p.494)
    const int rows = gridValue(e, QStringLiteral("Rows"), 6);
    const QList<int> order = category == QLatin1String("Matrix Wipe") ? matrixOrder(name, columns, rows) : QList<int>{};

    // Border (p.493). Ours: Width 100 and Soft 255 are each a sixteenth of the picture's width; the
    // border's colour runs from Color at the incoming edge to Blend Color at its outer edge unless
    // Fixed Border is on ("Turns off the Blending option").
    const QString border = QStringLiteral("Border");
    const double unit = W / 16.0;
    const double borderWidth = std::max(model::numberAt(e, border, QStringLiteral("Width"), time, span).value_or(0.0), 0.0) / 100.0 * unit;
    const double soft = std::max(model::numberAt(e, border, QStringLiteral("Soft"), time, span).value_or(0.0), 0.0) / 255.0 * unit;
    const model::Rgb white{1.0, 1.0, 1.0};
    const model::Rgb colour = model::colorAt(e, border, QStringLiteral("Color"), time, span).value_or(white);
    const bool fixedBorder = flag(border, QStringLiteral("Fixed Border"));
    const model::Rgb blend = fixedBorder ? colour : model::colorAt(e, border, QStringLiteral("Blend Color"), time, span).value_or(colour);
    // Edges always have a pixel of softness, so they are not jagged.
    const double edge = std::max(soft, 1.0);

    auto place = [&](double x, double y) {
        Place pl;
        pl.inX = pl.outX = x;
        pl.inY = pl.outY = y;
        if (category == QLatin1String("Box Wipe")) {
            // "Incoming video begins as a box and wipes out the outgoing video as the box expands to
            // fill the screen" from the corner or side it is named for (p.459).
            const double x0 = anchor.x * (1.0 - p) * W;
            const double y0 = anchor.y * (1.0 - p) * H;
            pl.distance = boxDistance(x, y, x0, y0, x0 + p * W, y0 + p * H);
        } else if (category == QLatin1String("Conceal") || category == QLatin1String("Push") ||
                   category == QLatin1String("L-Conceal")) {
            // Conceal: "Incoming video slides on top of the outgoing video" (p.459); Push: the incoming
            // video pushes the outgoing out (p.479).
            double dx = (anchor.x - 0.5) * 2.0 * (1.0 - p) * W;
            double dy = (anchor.y - 0.5) * 2.0 * (1.0 - p) * H;
            if (category == QLatin1String("L-Conceal")) {
                // "Incoming video slides vertically over one half of the screen and then slides
                // horizontally to fill the remaining half" (p.475), from the corner it is named for.
                const double q = std::min(p * 2.0, 1.0);
                const double r = std::max(p * 2.0 - 1.0, 0.0);
                dx = (anchor.x - 0.5) * (1.0 - r) * W;
                dy = (anchor.y - 0.5) * 2.0 * (1.0 - q) * H;
            }
            pl.inX = x - dx;
            pl.inY = y - dy;
            pl.distance = boxDistance(x, y, dx, dy, dx + W, dy + H);
            if (category == QLatin1String("Push")) {
                pl.outX = x - (dx - (anchor.x - 0.5) * 2.0 * W);
                pl.outY = y - (dy - (anchor.y - 0.5) * 2.0 * H);
            }
        } else if (category == QLatin1String("Squeeze")) {
            // "beginning as a point or line at a specific location on the screen and expanding to fill
            // the screen" (p.483); Right to Left keeps the full height (p.484).
            const bool line = name == QLatin1String("Right to Left");
            const Anchor a = name == QLatin1String("Centered Zoom") ? Anchor{} : anchor;
            const double sx = p;
            const double sy = line ? 1.0 : p;
            const double x0 = a.x * (1.0 - sx) * W;
            const double y0 = a.y * (1.0 - sy) * H;
            pl.distance = boxDistance(x, y, x0, y0, x0 + sx * W, y0 + sy * H);
            pl.inX = sx > 0 ? (x - x0) / sx : -1.0;
            pl.inY = sy > 0 ? (y - y0) / sy : -1.0;
        } else if (category == QLatin1String("Spin") && name != QLatin1String("Z Spin")) {
            // "Outgoing video is squeezed until it appears as a line (vertical for X Spin, horizontal
            // for Y Spin). Incoming video then expands from that line until it fills the screen" (p.482).
            const bool x_ = name == QLatin1String("X Spin");
            const double q = p < 0.5 ? 1.0 - p * 2.0 : p * 2.0 - 1.0;
            const double sx = x_ ? q : 1.0;
            const double sy = x_ ? 1.0 : q;
            const double x0 = (1.0 - sx) * W / 2.0;
            const double y0 = (1.0 - sy) * H / 2.0;
            const double lx = sx > 0 ? (x - x0) / sx : -1.0;
            const double ly = sy > 0 ? (y - y0) / sy : -1.0;
            if (p < 0.5) {
                pl.outX = lx;
                pl.outY = ly;
            } else {
                pl.outgoingShown = false;
                pl.inX = lx;
                pl.inY = ly;
                pl.distance = boxDistance(x, y, x0, y0, x0 + sx * W, y0 + sy * H);
            }
        } else if (name == QLatin1String("Z Spin")) {
            // "Incoming video begins as a dot in the center of the screen and then enlarges to full
            // screen while rotating counterclockwise one full revolution" (p.483).
            const double angle = -2.0 * std::numbers::pi * (1.0 - p); // y is down: negative turns anticlockwise
            const double c = std::cos(angle);
            const double s = std::sin(angle);
            const double rx = x - W / 2.0;
            const double ry = y - H / 2.0;
            // Back through the turn and the scale into the incoming picture.
            const double lx = (rx * c + ry * s) / std::max(p, 1e-6);
            const double ly = (-rx * s + ry * c) / std::max(p, 1e-6);
            pl.inX = lx + W / 2.0;
            pl.inY = ly + H / 2.0;
            pl.distance = p > 0 ? boxDistance(lx, ly, -W / 2.0, -H / 2.0, W / 2.0, H / 2.0) * p : kOutside;
        } else if (category == QLatin1String("Edge Wipe")) {
            // "Incoming video begins at the edge or center of the screen and moves out to wipe away the
            // outgoing video" (p.460). Ours: Horizontal moves left to right, Vertical top to bottom, the
            // Open ones from the centre out, and a diagonal from its corner to the opposite one.
            if (name == QLatin1String("Horizontal")) {
                pl.distance = x - p * W;
            } else if (name == QLatin1String("Horizontal Open")) {
                pl.distance = std::abs(x - W / 2.0) - p * W / 2.0;
            } else if (name == QLatin1String("Vertical")) {
                pl.distance = y - p * H;
            } else if (name == QLatin1String("Vertical Open")) {
                pl.distance = std::abs(y - H / 2.0) - p * H / 2.0;
            } else {
                const double u = anchor.x > 0.5 ? W - x : x;
                const double v = anchor.y > 0.5 ? H - y : y;
                pl.distance = (u / W + v / H - 2.0 * p) / std::hypot(1.0 / W, 1.0 / H);
            }
        } else if (category == QLatin1String("Shape Wipe")) {
            // "Incoming video uses a geometric shape to fill the screen and wipe out the outgoing video"
            // (p.481); the shapes as the illustrations on p.482 show them.
            const double cx = W / 2.0;
            const double cy = H / 2.0;
            if (name == QLatin1String("4 Corners")) {
                const double u = x < cx ? x : W - x;
                const double v = y < cy ? y : H - y;
                pl.distance = boxDistance(u, v, -kOutside, -kOutside, p * cx, p * cy);
            } else if (name == QLatin1String("Center Box")) {
                pl.distance = boxDistance(x, y, cx - p * cx, cy - p * cy, cx + p * cx, cy + p * cy);
            } else if (name == QLatin1String("Circle")) {
                pl.distance = std::hypot(x - cx, y - cy) - p * std::hypot(cx, cy);
            } else if (name == QLatin1String("Ellipse")) {
                // Ours: the picture's shape, reaching its corners at the end.
                const double a = std::max(p * cx * std::numbers::sqrt2, 1e-6);
                const double b = std::max(p * cy * std::numbers::sqrt2, 1e-6);
                pl.distance = (std::hypot((x - cx) / a, (y - cy) / b) - 1.0) * std::min(a, b);
            } else if (name == QLatin1String("Diamond")) {
                pl.distance = (std::abs(x - cx) / W + std::abs(y - cy) / H - p) / std::hypot(1.0 / W, 1.0 / H);
            } else if (name == QLatin1String("Clock")) {
                // Ours: a hand sweeping clockwise from twelve o'clock.
                double a = std::atan2(x - cx, cy - y);
                if (a < 0) a += 2.0 * std::numbers::pi;
                const double sweep = 2.0 * std::numbers::pi * p;
                const double d = std::min(rayDistance(x, y, cx, cy, 0.0, -1.0),
                                          rayDistance(x, y, cx, cy, std::sin(sweep), -std::cos(sweep)));
                pl.distance = p >= 1.0 ? -kOutside : (a < sweep ? -d : d);
            } else if (name == QLatin1String("Horizontal Bands")) {
                // Ours: bands of the Custom Grid's rows, from the left and the right in turn.
                const int band = std::clamp(static_cast<int>(y / H * rows), 0, rows - 1);
                pl.distance = band % 2 ? (W - p * W) - x : x - p * W;
            } else if (name == QLatin1String("Horizontal Blinds")) {
                const double h = H / rows;
                pl.distance = std::fmod(y, h) - p * h;
            } else if (name == QLatin1String("Vertical Blinds")) {
                const double w = W / columns;
                pl.distance = std::fmod(x, w) - p * w;
            }
        } else if (category == QLatin1String("Matrix Wipe")) {
            // "Incoming video begins as blocks or bands that follow a pattern while wiping out the
            // outgoing video" (p.476), in the Custom Grid's cells.
            const double cw = W / columns;
            const double ch = H / rows;
            const int col = std::clamp(static_cast<int>(x / cw), 0, columns - 1);
            const int row = std::clamp(static_cast<int>(y / ch), 0, rows - 1);
            const double x0 = col * cw;
            const double y0 = row * ch;
            // Grid: every cell grows from its centre at once (the illustration). The others: one cell
            // after another in their order, each growing from its centre in its turn (ours).
            double grow = p;
            if (name != QLatin1String("Grid")) {
                const double at = p * static_cast<double>(order.size()) - order.at(row * columns + col);
                grow = std::clamp(at, 0.0, 1.0);
            }
            if (grow >= 1.0) {
                pl.distance = -kOutside;
            } else if (grow > 0.0) {
                const double hx = cw / 2.0 * grow;
                const double hy = ch / 2.0 * grow;
                pl.distance = boxDistance(x, y, x0 + cw / 2.0 - hx, y0 + ch / 2.0 - hy, x0 + cw / 2.0 + hx, y0 + ch / 2.0 + hy);
            }
        } else if (category == QLatin1String("Sawtooth Wipe")) {
            // "Incoming video appears with a sawtooth edge and expands to fill the screen" (p.480):
            // Horizontal moves left to right, Vertical top to bottom. Ours: a tooth for each row (or
            // column) of the Custom Grid, a column (row) deep.
            const bool horizontal = name.startsWith(QLatin1String("Horizontal"));
            const bool open = name.contains(QLatin1String("Open"));
            const double along = horizontal ? x : y;
            const double across = horizontal ? y : x;
            const double length = horizontal ? W : H;
            const double teeth = horizontal ? rows : columns;
            const double depth = length / (horizontal ? columns : rows);
            const double phase = std::fmod(across / ((horizontal ? H : W) / teeth), 1.0);
            const double tooth = 1.0 - std::abs(phase * 2.0 - 1.0); // 0 at a tooth's sides, 1 at its point
            if (open) {
                pl.distance = std::abs(along - length / 2.0) - (p * (length / 2.0 + depth) - depth * (1.0 - tooth));
            } else {
                pl.distance = along - (p * (length + depth) - depth * (1.0 - tooth));
            }
        }
        return pl;
    };

    float in[4];
    float out[4];
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Place pl = place(x + 0.5, y + 0.5);
            float* o = result.pixel(x, y);
            if (pl.outgoingShown) {
                sample(outgoing, pl.outX, pl.outY, out);
                std::copy(out, out + 4, o);
            }
            // The border band, outside the incoming region and under its soft edge.
            if (borderWidth > 0 && pl.distance < borderWidth + edge) {
                const float a = static_cast<float>(std::clamp(0.5 - (pl.distance - borderWidth) / edge, 0.0, 1.0));
                const double t = std::clamp(pl.distance / borderWidth, 0.0, 1.0);
                const float rgb[3] = {static_cast<float>(colour.red + (blend.red - colour.red) * t),
                                      static_cast<float>(colour.green + (blend.green - colour.green) * t),
                                      static_cast<float>(colour.blue + (blend.blue - colour.blue) * t)};
                for (int c = 0; c < 3; ++c) o[c] = rgb[c] * a + o[c] * (1.0f - a);
                o[3] = a + o[3] * (1.0f - a);
            }
            const float a = static_cast<float>(std::clamp(0.5 - pl.distance / edge, 0.0, 1.0));
            if (a > 0.0f) {
                sample(incoming, pl.inX, pl.inY, in);
                for (int c = 0; c < 4; ++c) o[c] = in[c] * a + o[c] * (1.0f - a * in[3]);
            }
        }
    }
    return result;
}

} // namespace lumenis::media
