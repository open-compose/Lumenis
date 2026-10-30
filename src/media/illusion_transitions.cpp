#include "illusion_common.h"

#include <vector>

// The Illusion FX transitions (ROADMAP 6.3d4; docs/spec/illusion-fx.md): Iris, Melt, Pagecurl, Particle
// Blast, Particle Orbit, Particle Wind, Random Blend, Rollup and Shear (FX Guide p.466-467, parameters
// p.508-525). The foreground is the outgoing clip and the background the incoming one; each effect's
// Swap Sources ("whether to apply the effect to incoming or outgoing video") swaps them. The reference
// says what each control does but rarely its range, and never a formula: the ranges are ours ("ours" in
// effect_parameters.json), the values until set are in illusionTransitionDefault(), and the pictures are
// ours too. Amount "Typical keyframing has a value of 0 at the first keyframe and 100 at the last" (p.512):
// until it is keyframed or set, Amount follows the transition from 0 to 100.
namespace lumenis::media::detail {

using namespace illusion;

namespace {

// The effect's Input Parameters category.
QString inputCategory(const model::Effect& e) {
    return e.name + QStringLiteral(" Effect Input Parameters");
}

// Amount, 0 to 1: the parameter's value when it has one, else the transition's progress.
double amountOf(const model::Effect& e, double progress, double time, const model::EffectSpan& span) {
    const auto v = model::numberAt(e, inputCategory(e), QStringLiteral("Amount"), time, span);
    return std::clamp(v ? *v / 100.0 : progress, 0.0, 1.0);
}

// The picture's corners projected on the unit direction (dx, dy): the least and the greatest.
void extent(double dx, double dy, double W, double H, double& least, double& most) {
    const double a = 0.0;
    const double b = W * dx;
    const double c = H * dy;
    const double d = W * dx + H * dy;
    least = std::min(std::min(a, b), std::min(c, d));
    most = std::max(std::max(a, b), std::max(c, d));
}

bool inside(double x, double y, double W, double H) {
    return x >= 0 && y >= 0 && x <= W && y <= H;
}

// A sample of the foreground, held inside the Border's box, smoothed or not.
void take(const Picture& p, const SourceBox& box, bool antiAlias, double x, double y, float out[4]) {
    if (antiAlias) {
        sample(p, box.x(x), box.y(y), out);
    } else {
        nearest(p, box.x(x), box.y(y), out);
    }
}

// `o` = `in` scaled by `light` (1 unchanged) and then lifted toward white by `white`, alpha kept.
void shade(float* o, const float in[4], float light, float white) {
    for (int c = 0; c < 3; ++c) o[c] = std::clamp(in[c] * light + (in[3] - in[c] * light) * white, 0.0f, in[3]);
    o[3] = in[3];
}

// Smooth random values along a line, -1 to 1: a few sines with random wavelengths and phases.
struct Noise {
    double k[4], phase[4];
    Noise(std::int64_t seed, double width) {
        for (int i = 0; i < 4; ++i) {
            k[i] = 2.0 * kPi / (width * (0.08 + 0.4 * hash01(i, 11, 0, seed)));
            phase[i] = hash01(i, 12, 0, seed) * 2.0 * kPi;
        }
    }
    double at(double x) const {
        double v = 0.0;
        for (int i = 0; i < 4; ++i) v += std::sin(x * k[i] + phase[i]);
        return v / 4.0;
    }
};

Picture iris(const model::Effect& e, const Picture& fg, const Picture& bg, double amount, double time, const model::EffectSpan& span) {
    // "wipes the foreground clip away to reveal the background clip, using a circle that radiates out
    // from the center": Amount, Softness "the size of the area over which the pixels are faded", X and
    // Y (p.466, p.512). Ours: at Amount 100 the circle reaches the farthest corner; Softness 100 is a
    // quarter of the picture's width.
    const QString cat = inputCategory(e);
    const double W = fg.width();
    const double H = fg.height();
    const double cx = num(e, cat, QStringLiteral("X"), time, span) / 100.0 * W;
    const double cy = num(e, cat, QStringLiteral("Y"), time, span) / 100.0 * H;
    const double soft = std::max(num(e, cat, QStringLiteral("Softness"), time, span), 0.0) / 100.0 * W / 4.0 + 1.0;
    const double reach = std::max(std::hypot(std::max(cx, W - cx), std::max(cy, H - cy)), 1.0);
    const double radius = amount * (reach + soft); // the outer edge of the soft band
    Picture out(fg.width(), fg.height());
    for (int y = 0; y < fg.height(); ++y) {
        for (int x = 0; x < fg.width(); ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            const auto t = static_cast<float>(std::clamp((radius - d) / soft, 0.0, 1.0)); // 1 inside
            const float* a = fg.pixel(x, y);
            const float* b = bg.pixel(x, y);
            float* o = out.pixel(x, y);
            for (int c = 0; c < 4; ++c) o[c] = a[c] + (b[c] - a[c]) * t;
        }
    }
    return out;
}

Picture melt(const model::Effect& e, const Picture& fg, const Picture& bg, double amount, double time, const model::EffectSpan& span) {
    // "The foreground clip 'melts' downward off the screen, while being distorted in a random 'liquid'
    // manner along the horizontal lines; the vertical lines are not distorted": Bleed "a more dramatic
    // effect", Melt Up, Amount, Strength, Random Seed, Anti-alias (p.466, p.515). Ours: each column
    // falls by its own smoothly random distance, at Amount 100 at least the picture's height; Strength
    // 100 lets the columns differ by the height; Bleed smears the top of the falling picture down
    // behind it, fading out by Amount 100.
    const QString cat = inputCategory(e);
    const int width = fg.width();
    const int height = fg.height();
    const double H = height;
    const double strength = std::clamp(num(e, cat, QStringLiteral("Strength"), time, span), 0.0, 100.0) / 100.0;
    const auto seed = static_cast<std::int64_t>(std::lround(num(e, cat, QStringLiteral("Random Seed"), time, span)));
    const bool up = flag(e, cat, QStringLiteral("Melt Up"), false);
    const bool bleed = flag(e, cat, QStringLiteral("Bleed"), false);
    const bool antiAlias = flag(e, cat, QStringLiteral("Anti-alias"), true);
    const SourceBox box = SourceBox::of(e, time, span, width, H);
    const Noise noise(seed, width);
    Picture out = bg;
    float in[4];
    for (int x = 0; x < width; ++x) {
        const double drop = amount * H * (1.0 + strength * (noise.at(x + 0.5) + 1.0));
        for (int y = 0; y < height; ++y) {
            // Distance from the edge that leads the melt (the top, or the bottom with Melt Up).
            const double along = up ? H - (y + 0.5) : y + 0.5;
            double from = along - drop;
            float level = 1.0f;
            if (from < 0) {
                // Bleed's smear: half the fall behind the edge, fading, and gone by Amount 100.
                const double smear = drop * 0.5 * (1.0 - amount);
                if (!bleed || smear <= 0 || from < -smear) continue;
                level = static_cast<float>(1.0 + from / smear);
                from = 0.5;
            }
            take(fg, box, antiAlias, x + 0.5, up ? H - from : from, in);
            float* o = out.pixel(x, y);
            for (int c = 0; c < 4; ++c) o[c] = in[c] * level + o[c] * (1.0f - in[3] * level);
        }
    }
    return out;
}

// The page of Pagecurl and Rollup: `page` lifted along the unit direction (dx, dy) from the side where
// the projection is least, its fold `s` pixels along, on a cylinder of radius r, over `under`.
// Pagecurl's lifted part lies back flat over the page (a curl); Rollup's winds round the cylinder (a
// roll). `highlight` brightens the curled edge.
Picture curl(const Picture& page, const Picture& under, double dx, double dy, double s, double r, bool roll, bool highlight,
             const SourceBox& box, bool antiAlias) {
    const double W = page.width();
    const double H = page.height();
    double least = 0.0;
    double most = 0.0;
    extent(dx, dy, W, H, least, most);
    Picture out(page.width(), page.height());
    float in[4];
    for (int y = 0; y < page.height(); ++y) {
        for (int x = 0; x < page.width(); ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w = px * dx + py * dy - least - s; // ahead of the fold: positive
            float* o = out.pixel(x, y);
            // The layers that can lie over this pixel, top first: arc lengths a back from the fold
            // of the page points there, and whether the back of the page shows.
            double a[3];
            bool back[3];
            int n = 0;
            if (roll) {
                // Wound round the cylinder (axis over the fold): the outermost turn there is.
                if (w >= -r && w <= r) {
                    const double phi = kPi - std::asin(std::clamp(-w / r, -1.0, 1.0)); // the upper side
                    const double turn = 2.0 * kPi * r;
                    double arc = r * phi;
                    if (s >= arc) arc += turn * std::floor((s - arc) / turn);
                    a[n] = arc;
                    back[n++] = true;
                }
            } else {
                if (w >= 0) {
                    a[n] = w + kPi * r; // lying flat over the page, its back up
                    back[n++] = true;
                }
                if (w >= -r && w <= 0) {
                    const double phi = std::asin(std::clamp(-w / r, 0.0, 1.0));
                    a[n] = r * (kPi - phi); // the upper half of the curl, its back
                    back[n++] = true;
                    a[n] = r * phi; // the lower half, its front
                    back[n++] = false;
                }
            }
            bool covered = false;
            for (int i = 0; i < n && !covered; ++i) {
                if (a[i] > s) continue; // more than has been lifted
                const double shift = w + a[i]; // back along the direction to the point's place
                const double sx = px - shift * dx;
                const double sy = py - shift * dy;
                if (!inside(sx, sy, W, H)) continue;
                take(page, box, antiAlias, sx, sy, in);
                // On the curl the light falls off toward its sides; the edge (w = -r) catches it.
                float light = 1.0f;
                float white = back[i] ? 0.25f : 0.0f; // the paper's back
                if (w >= -r && w <= r && r > 0) {
                    const double side = std::clamp(-w / r, -1.0, 1.0);
                    light = static_cast<float>(0.7 + 0.3 * std::sqrt(1.0 - side * side * 0.5));
                    if (highlight && side > 0) {
                        // A narrow band at the edge: side^6, multiplied out (no pow in a pixel loop; MSVC).
                        const double cube = side * side * side;
                        white += static_cast<float>(0.5 * cube * cube);
                    }
                }
                shade(o, in, light, white);
                covered = true;
            }
            // Beneath: the page still lying flat ahead of the fold, else what it uncovered; under a
            // lifted layer it shows through that layer's transparency.
            const float* u = w >= 0 ? page.pixel(x, y) : under.pixel(x, y);
            if (!covered) {
                std::copy(u, u + 4, o);
                continue;
            }
            const float rest = 1.0f - o[3];
            for (int c = 0; c < 4; ++c) o[c] += u[c] * rest;
        }
    }
    return out;
}

Picture particles(const model::Effect& e, const Picture& fg, const Picture& bg, double amount, double time, const model::EffectSpan& span) {
    // "divides the foreground image into circular particles": Size "the radius of the particles",
    // Spacing "the space between the center of the original position of the particles", Bumpiness
    // "a shadow to the edge of the particles"; Particle Blast "blows away the particles from the center
    // outward", Particle Wind "blows the particles away directionally" (Angle "the source direction of
    // the wind"), with Front Speed, Front Randomness, Speed, Speed Randomness and Acceleration;
    // Particle Orbit's particles "orbit around their original position" at Speed (p.466, p.517-519).
    // Ours: sizes in pixels; a particle's start is up to 0.9 of Amount later at Front Speed 100 for
    // those farthest from the centre (Blast) or the side the wind comes from (Wind), plus up to a
    // quarter at random with Front Randomness (at most 0.9 in all); Speed 100 moves four times as far as
    // the farthest a particle has to go (half the diagonal, or across the picture with the wind) over
    // Amount 0 to 100;
    // Acceleration 100 reaches it at once, 0 over a third of Amount. Orbit's particles circle up to three
    // spacings out and shrink to nothing with Amount, as the reference's example keyframes them.
    const QString cat = inputCategory(e);
    const int width = fg.width();
    const int height = fg.height();
    const double W = width;
    const double H = height;
    const double unit = W / 1920.0;
    const double size = std::max(num(e, cat, QStringLiteral("Size"), time, span), 0.0) * unit;
    const double spacing = std::max(num(e, cat, QStringLiteral("Spacing"), time, span) * unit, 1.0);
    const double bump = std::clamp(num(e, cat, QStringLiteral("Bumpiness"), time, span), 0.0, 100.0) / 100.0;
    const double speed = std::max(num(e, cat, QStringLiteral("Speed"), time, span), 0.0) / 100.0;
    const double frontSpeed = std::clamp(num(e, cat, QStringLiteral("Front Speed"), time, span), 0.0, 100.0) / 100.0;
    const double frontRandom = std::clamp(num(e, cat, QStringLiteral("Front Randomness"), time, span), 0.0, 100.0) / 100.0;
    const double speedRandom = std::clamp(num(e, cat, QStringLiteral("Speed Randomness"), time, span), 0.0, 100.0) / 100.0;
    const double ramp = 0.01 + (1.0 - std::clamp(num(e, cat, QStringLiteral("Acceleration"), time, span), 0.0, 100.0) / 100.0) / 3.0;
    const bool orbit = e.name == QLatin1String("Particle Orbit");
    const bool wind = e.name == QLatin1String("Particle Wind");
    const double angle = num(e, cat, QStringLiteral("Angle"), time, span) * kPi / 180.0;
    // The wind blows away from where it comes from (0 degrees: 3 o'clock, counterclockwise, y down).
    const double wx = -std::cos(angle);
    const double wy = std::sin(angle);
    double least = 0.0;
    double most = 0.0;
    extent(wx, wy, W, H, least, most);
    const double diagonal = std::hypot(W, H);
    // The farthest a particle has to go: from the centre to a corner, or across the picture with the
    // wind; Speed is in twice that.
    const double reach = wind ? 2.0 * (most - least) : diagonal;
    const double radius = orbit ? size * (1.0 - amount) : size;
    const SourceBox box = SourceBox::of(e, time, span, W, H);
    Picture out = bg;
    if (radius <= 0) return out;
    const int columns = static_cast<int>(std::ceil(W / spacing)) + 1;
    const int rows = static_cast<int>(std::ceil(H / spacing)) + 1;
    float in[4];
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const double ox = column * spacing;
            const double oy = row * spacing;
            double cx = ox;
            double cy = oy;
            if (orbit) {
                const double turn = hash01(column, row, 1, 0) * 2.0 * kPi + speed * 4.0 * kPi * amount;
                const double circle = amount * 3.0 * spacing * (0.5 + hash01(column, row, 2, 0));
                cx += circle * std::cos(turn);
                cy += circle * std::sin(turn);
            } else {
                double dx = wx;
                double dy = wy;
                double front = 0.0; // 0 where the blast or wind starts, 1 farthest from it
                if (!wind) {
                    const double rx = ox - W / 2.0;
                    const double ry = oy - H / 2.0;
                    const double r = std::hypot(rx, ry);
                    const double a = r > 0 ? std::atan2(ry, rx) : hash01(column, row, 3, 0) * 2.0 * kPi;
                    dx = std::cos(a);
                    dy = std::sin(a);
                    front = r / (diagonal / 2.0);
                } else {
                    front = most > least ? (ox * wx + oy * wy - least) / (most - least) : 0.0;
                }
                const double start = std::min(frontSpeed * front * 0.9 + frontRandom * hash01(column, row, 4, 0) * 0.25, 0.9);
                const double t = amount - start;
                if (t > 0) {
                    const double v = speed * 2.0 * reach * (1.0 + speedRandom * (hash01(column, row, 5, 0) - 0.5));
                    const double moved = v * (t < ramp ? t * t / (2.0 * ramp) : t - ramp / 2.0);
                    cx += moved * dx;
                    cy += moved * dy;
                }
            }
            const int x0 = std::max(static_cast<int>(std::floor(cx - radius)), 0);
            const int x1 = std::min(static_cast<int>(std::ceil(cx + radius)), width - 1);
            const int y0 = std::max(static_cast<int>(std::floor(cy - radius)), 0);
            const int y1 = std::min(static_cast<int>(std::ceil(cy + radius)), height - 1);
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double px = x + 0.5 - cx;
                    const double py = y + 0.5 - cy;
                    const double d = std::hypot(px, py) / radius;
                    if (d >= 1.0 + 0.5 / radius) continue;
                    const double sx = ox + px;
                    const double sy = oy + py;
                    if (!inside(sx, sy, W, H)) continue;
                    sample(fg, box.x(sx), box.y(sy), in);
                    // An anti-aliased rim, a pixel wide and centred on the edge.
                    const auto edge = static_cast<float>(std::clamp((1.0 - d) * radius + 0.5, 0.0, 1.0));
                    const auto light = static_cast<float>(1.0 - bump * 0.6 * d * d);
                    float* o = out.pixel(x, y);
                    for (int c = 0; c < 3; ++c) o[c] = in[c] * light * edge + o[c] * (1.0f - in[3] * edge);
                    o[3] = in[3] * edge + o[3] * (1.0f - in[3] * edge);
                }
            }
        }
    }
    return out;
}

Picture randomBlend(const model::Effect& e, const Picture& fg, const Picture& bg, double amount, double time, const model::EffectSpan& span) {
    // "uses a random pattern to blend the image of the incoming and outgoing video": Amount, Random
    // Scale "the amount of randomness ... different portions of the image at different rates" (p.466,
    // p.522). Ours: cells two pixels of a 1920 picture square, each with its own threshold; Random
    // Scale 0 is a plain dissolve; the higher, the more each cell's moment varies and the quicker it
    // blends (Random Scale 100: at once).
    const QString cat = inputCategory(e);
    const double scale = std::clamp(num(e, cat, QStringLiteral("Random Scale"), time, span), 0.0, 100.0) / 100.0;
    const double cell = std::max(2.0 * fg.width() / 1920.0, 1.0);
    const double window = std::max(1.0 - scale, 0.02); // how much of Amount each cell blends over
    Picture out(fg.width(), fg.height());
    for (int y = 0; y < fg.height(); ++y) {
        for (int x = 0; x < fg.width(); ++x) {
            // Each cell starts at its own moment, so that all have blended at Amount 1.
            const double start = hash01(static_cast<std::int64_t>(x / cell), static_cast<std::int64_t>(y / cell), 7, 0) * (1.0 - window);
            const auto t = static_cast<float>(std::clamp((amount - start) / window, 0.0, 1.0));
            const float* a = fg.pixel(x, y);
            const float* b = bg.pixel(x, y);
            float* o = out.pixel(x, y);
            for (int c = 0; c < 4; ++c) o[c] = a[c] + (b[c] - a[c]) * t;
        }
    }
    return out;
}

Picture shear(const model::Effect& e, const Picture& fg, const Picture& bg, double amount, double time, const model::EffectSpan& span) {
    // "divides the foreground clip into strips and moves alternate strips away in opposite directions
    // to reveal the background clip": Amount, Min Width and Max Width "of the slats", Width Randomness
    // "the variety of sizes", Spread "the randomness of when individual strips start moving. A value of
    // 0 causes all strips to start at the same time", Angle "the direction of the effect", Random Seed,
    // Anti-alias (p.466, p.524-525). Ours: widths in pixels, the strips running along the direction;
    // with Width Randomness 0 every strip is midway between the widths; Spread 100 starts the strips
    // over the first half of Amount; each strip then moves out of the picture by the end.
    const QString cat = inputCategory(e);
    const double W = fg.width();
    const double H = fg.height();
    const double unit = W / 1920.0;
    const double minWidth = std::max(num(e, cat, QStringLiteral("Min Width"), time, span), 1.0) * unit;
    const double maxWidth = std::max(num(e, cat, QStringLiteral("Max Width"), time, span) * unit, minWidth);
    const double variety = std::clamp(num(e, cat, QStringLiteral("Width Randomness"), time, span), 0.0, 100.0) / 100.0;
    const double spread = std::clamp(num(e, cat, QStringLiteral("Spread"), time, span), 0.0, 100.0) / 100.0;
    const double angle = num(e, cat, QStringLiteral("Angle"), time, span) * kPi / 180.0;
    const auto seed = static_cast<std::int64_t>(std::lround(num(e, cat, QStringLiteral("Random Seed"), time, span)));
    const bool antiAlias = flag(e, cat, QStringLiteral("Anti-alias"), true);
    const double dx = std::cos(angle);
    const double dy = -std::sin(angle); // counterclockwise on the screen, y down
    const double nx = -dy;               // across the strips
    const double ny = dx;
    double least = 0.0;
    double most = 0.0;
    extent(nx, ny, W, H, least, most);
    double alongLeast = 0.0;
    double alongMost = 0.0;
    extent(dx, dy, W, H, alongLeast, alongMost);
    const double travel = alongMost - alongLeast;
    // The strips' edges across the picture, and when each starts.
    std::vector<double> edges{least};
    std::vector<double> starts;
    for (std::int64_t i = 0; edges.back() < most; ++i) {
        const double mid = (minWidth + maxWidth) / 2.0;
        const double w = mid + (hash01(i, 21, 0, seed) - 0.5) * (maxWidth - minWidth) * variety;
        edges.push_back(edges.back() + std::max(w, 1.0));
        starts.push_back(spread * 0.5 * hash01(i, 22, 0, seed));
    }
    if (starts.empty()) starts.push_back(0.0);
    const SourceBox box = SourceBox::of(e, time, span, W, H);
    Picture out(fg.width(), fg.height());
    float in[4];
    for (int y = 0; y < fg.height(); ++y) {
        for (int x = 0; x < fg.width(); ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double across = px * nx + py * ny;
            const auto strip = static_cast<std::size_t>(std::max<std::ptrdiff_t>(std::upper_bound(edges.begin(), edges.end(), across) - edges.begin() - 1, 0));
            const std::size_t i = std::min(strip, starts.size() - 1);
            const double t = std::clamp((amount - starts[i]) / (1.0 - starts[i]), 0.0, 1.0);
            const double shift = (i % 2 == 0 ? 1.0 : -1.0) * t * travel;
            const double sx = px - shift * dx;
            const double sy = py - shift * dy;
            const float* b = bg.pixel(x, y);
            float* o = out.pixel(x, y);
            if (!inside(sx, sy, W, H)) {
                std::copy(b, b + 4, o);
                continue;
            }
            take(fg, box, antiAlias, sx, sy, in);
            for (int c = 0; c < 4; ++c) o[c] = in[c] + b[c] * (1.0f - in[3]);
        }
    }
    return out;
}

} // namespace

} // namespace lumenis::media::detail

namespace lumenis::media {

using namespace detail;
using namespace detail::illusion;

bool isIllusionTransition(const model::Effect& e) {
    if (e.category != QLatin1String("Illusion FX")) return false;
    static const QStringList names = {QStringLiteral("Iris"),           QStringLiteral("Melt"),         QStringLiteral("Pagecurl"),
                                      QStringLiteral("Particle Blast"), QStringLiteral("Particle Orbit"), QStringLiteral("Particle Wind"),
                                      QStringLiteral("Random Blend"),   QStringLiteral("Rollup"),       QStringLiteral("Shear")};
    return names.contains(e.name);
}

namespace detail {

QJsonValue illusionTransitionDefault(const model::Effect& e, const QString& category, const QString& name) {
    // The values until set: ours, chosen so each transition shows itself when applied. Amount has none:
    // it follows the transition (amountOf).
    if (!isIllusionTransition(e) || category != inputCategory(e)) return {};
    if (e.name == QLatin1String("Iris")) {
        if (name == QLatin1String("Softness")) return 5;
        if (name == QLatin1String("X") || name == QLatin1String("Y")) return 50;
    }
    if (e.name == QLatin1String("Melt") && name == QLatin1String("Strength")) return 30;
    if (e.name == QLatin1String("Random Blend") && name == QLatin1String("Random Scale")) return 50;
    if (e.name.startsWith(QLatin1String("Particle"))) {
        if (name == QLatin1String("Size")) return 12;
        if (name == QLatin1String("Spacing")) return 12; // with Size 12 the particles overlap at rest
        if (name == QLatin1String("Speed")) return 50;
        if (name == QLatin1String("Front Speed")) return 80;
        if (name == QLatin1String("Front Randomness") || name == QLatin1String("Speed Randomness")) return 30;
        if (name == QLatin1String("Acceleration")) return 50;
    }
    if (e.name == QLatin1String("Rollup") && name == QLatin1String("Curl Size")) return 30;
    if (e.name == QLatin1String("Shear")) {
        if (name == QLatin1String("Min Width")) return 20;
        if (name == QLatin1String("Max Width")) return 80;
        if (name == QLatin1String("Width Randomness")) return 50;
        if (name == QLatin1String("Spread")) return 30;
    }
    return {};
}

} // namespace detail

Picture illusionTransitionPicture(const model::Effect& e, Picture outgoing, Picture incoming, double progress, double time,
                                  const model::EffectSpan& span) {
    if (outgoing.isNull() || incoming.isNull()) return {};
    const QString cat = inputCategory(e);
    if (flag(e, cat, QStringLiteral("Swap Sources"), false)) std::swap(outgoing, incoming);
    const Picture& fg = outgoing;
    const Picture& bg = incoming;
    const double amount = amountOf(e, progress, time, span);
    const QString& name = e.name;
    const double W = fg.width();
    const double H = fg.height();
    if (name == QLatin1String("Iris")) return iris(e, fg, bg, amount, time, span);
    if (name == QLatin1String("Melt")) return melt(e, fg, bg, amount, time, span);
    if (name == QLatin1String("Random Blend")) return randomBlend(e, fg, bg, amount, time, span);
    if (name == QLatin1String("Shear")) return shear(e, fg, bg, amount, time, span);
    if (name.startsWith(QLatin1String("Particle"))) return particles(e, fg, bg, amount, time, span);
    if (name == QLatin1String("Pagecurl")) {
        // "treats the foreground video channel as though it were a sheet of paper being rolled over to
        // reveal another page beneath ... adds a highlight along the curled edge": Turn Left "the
        // direction of the page curl"; Reverse "incoming video peels on" (p.466, p.515-516). No Amount:
        // over the transition. Ours: the page lifts from the bottom left corner toward the top right, or
        // with Turn Left from the bottom right toward the top left, at 45 degrees, on a curl a tenth of the height
        // across; Reverse runs the curl backward with the other picture as the page.
        const bool left = flag(e, cat, QStringLiteral("Turn Left"), false);
        const bool reverse = flag(e, cat, QStringLiteral("Reverse"), false);
        const double dx = (left ? -1.0 : 1.0) / std::sqrt(2.0); // at 45 degrees, so a corner lifts
        const double dy = -1.0 / std::sqrt(2.0);
        double least = 0.0;
        double most = 0.0;
        extent(dx, dy, W, H, least, most);
        const double r = H / 20.0;
        const double p = reverse ? 1.0 - progress : progress;
        const SourceBox none{0.5, W - 0.5, 0.5, H - 0.5};
        return curl(reverse ? bg : fg, reverse ? fg : bg, dx, dy, p * (most - least + kPi * r), r, false, true, none, true);
    }
    if (name == QLatin1String("Rollup")) {
        // "rolls up the foreground clip to reveal the background clip": Amount, Curl Size "the size of
        // the roll", Direction "0 degrees is 3 o'clock. Positive values move counterclockwise",
        // Anti-alias (p.466, p.524). Ours: the roll moves toward Direction; Curl Size 100 is a roll a
        // quarter of the height across.
        const double size = std::max(num(e, cat, QStringLiteral("Curl Size"), time, span), 0.0) / 100.0 * H / 8.0 + 1.0;
        const double angle = num(e, cat, QStringLiteral("Direction"), time, span) * kPi / 180.0;
        const double dx = std::cos(angle);
        const double dy = -std::sin(angle);
        double least = 0.0;
        double most = 0.0;
        extent(dx, dy, W, H, least, most);
        return curl(fg, bg, dx, dy, amount * (most - least + size), size, true, false, SourceBox::of(e, time, span, W, H),
                    flag(e, cat, QStringLiteral("Anti-alias"), true));
    }
    return {};
}

} // namespace lumenis::media
