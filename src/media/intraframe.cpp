// The Intraframe effects' pictures (ROADMAP 6.9a; FX Guide ch.15, p.391-441, and the parameter reference p.493-497,
// p.532-548; docs/spec/intraframe.md): each object's coverage (its outline or Brush stroke, Magic Mask, Feathering,
// opacity and visibility) and what it does there: the Paint Effect's modes, the Blur and Mosaic effects within their
// objects, and AniMatte's Key In and Key Out mattes. The compositor calls intraframePicture for a segment's effect and for
// an AniMatte transition.
#include "effect_drawing.h"

#include "lumenis/media/color_correction.h"
#include "lumenis/media/compositor.h"
#include "lumenis/model/correction_shapes.h"
#include "lumenis/model/intraframe.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace lumenis::media {

namespace {

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;

double number(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span, double fallback) {
    const QJsonValue d = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(d.isDouble() ? d.toDouble() : fallback);
}

// A flag at a time: a keyframed one takes the last keyframe at or before `time` ("Object Visible ... have a painted
// object suddenly 'pop' onto the screen at the position marked by the keyframe", p.537); otherwise its value, or
// `fallback` until set.
bool flagAt(const model::Effect& e, const QString& category, const QString& name, double time, bool fallback) {
    const model::ParameterKeyframes keys = model::keyframesOf(e, category, name);
    if (!keys.keys.isEmpty()) {
        bool v = keys.keys.constFirst().value.toBool(fallback);
        for (const model::EffectKeyframe& k : keys.keys) {
            if (k.at <= time + 1e-9) v = k.value.toBool(v);
        }
        return v;
    }
    const QJsonValue v = e.parameters.value(category).toObject().value(name);
    return v.isBool() ? v.toBool() : fallback;
}

QString text(const model::Effect& e, const QString& category, const QString& name, const QString& fallback) {
    const QString v = e.parameters.value(category).toObject().value(name).toString();
    if (!v.isEmpty()) return v;
    const QJsonValue d = parameterDefault(e, category, name);
    return d.isString() && !d.toString().isEmpty() ? d.toString() : fallback;
}

// The category with the object's mode, opacity and visibility, by effect (p.532-548).
QString modeCategory(const model::Effect& e) {
    if (model::isAniMatte(e)) return QStringLiteral("AniMatte Effect Mode Parameters");
    if (e.name == QLatin1String("Blur Effect")) return QStringLiteral("Blur Effect Blur Parameters");
    if (e.name == QLatin1String("Mosaic Effect")) return QStringLiteral("Mosaic Effect Mosaic Parameters");
    if (e.name == QLatin1String("Scratch Removal")) return QStringLiteral("Scratch Removal Effect Scratch Parameters");
    return QStringLiteral("Paint Effect Mode Parameters");
}

QString visibleName(const model::Effect& e) {
    if (e.name == QLatin1String("Blur Effect")) return QStringLiteral("Blur Visible");
    if (e.name == QLatin1String("Mosaic Effect")) return QStringLiteral("Mosaic Visible");
    return QStringLiteral("Object Visible");
}

// A box blur of a mask, radius r across or down, three times over (near-Gaussian), edges held.
void blurMask(std::vector<float>& m, int w, int h, int r, bool horizontal) {
    if (r <= 0 || w <= 0 || h <= 0) return;
    const int n = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    std::vector<float> line(static_cast<std::size_t>(n));
    auto index = [&](int l, int i) {
        return horizontal ? static_cast<std::size_t>(l) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i)
                          : static_cast<std::size_t>(i) * static_cast<std::size_t>(w) + static_cast<std::size_t>(l);
    };
    for (int pass = 0; pass < 3; ++pass) {
        for (int l = 0; l < lines; ++l) {
            for (int i = 0; i < n; ++i) line[static_cast<std::size_t>(i)] = m[index(l, i)];
            auto at = [&](int i) { return static_cast<double>(line[static_cast<std::size_t>(std::clamp(i, 0, n - 1))]); };
            double sum = 0.0;
            for (int i = -r; i <= r; ++i) sum += at(i);
            const double count = 2.0 * r + 1.0;
            for (int i = 0; i < n; ++i) {
                m[index(l, i)] = static_cast<float>(sum / count);
                sum += at(i + r + 1) - at(i - r);
            }
        }
    }
}

// A mask drawn by `paint` into a w x h image, as coverage 0 to 1.
template <typename Paint>
std::vector<float> drawnMask(int w, int h, bool antialias, Paint paint) {
    QImage image(w, h, QImage::Format_Grayscale8);
    image.fill(0);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        paint(p);
    }
    std::vector<float> m(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < w; ++x) m[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = line[x] / 255.0f;
    }
    return m;
}

// The brush head (p.397-398, p.493): its outline in a unit box, the Brush category's custom head or its Shape's
// (model::brushHeadPoints).
QPainterPath brushHead(const QJsonObject& brush) {
    QPainterPath head = intraframePath(model::brushHeadPoints(brush), true, QSizeF(1.0, 1.0));
    head.setFillRule(Qt::WindingFill);
    return head;
}

// The points along a polyline at a spacing, up to `fraction` of its length (Path: "the percentage of paint ... visible
// from the initial point of pressure to the completion of the brush stroke", p.493).
QList<QPointF> strokeStamps(const QPolygonF& line, double spacing, double fraction) {
    QList<QPointF> out;
    if (line.isEmpty()) return out;
    double total = 0.0;
    for (qsizetype i = 1; i < line.size(); ++i) total += std::hypot(line.at(i).x() - line.at(i - 1).x(), line.at(i).y() - line.at(i - 1).y());
    const double limit = total * std::clamp(fraction, 0.0, 1.0);
    if (fraction <= 0.0) return out;
    out << line.constFirst();
    double travelled = 0.0, next = spacing;
    for (qsizetype i = 1; i < line.size(); ++i) {
        const QPointF a = line.at(i - 1), b = line.at(i);
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        while (len > 0.0 && next <= travelled + len && next <= limit) {
            const double f = (next - travelled) / len;
            out << a + (b - a) * f;
            next += spacing;
        }
        travelled += len;
        if (travelled > limit) break;
    }
    return out;
}

// Magic Mask (p.497): how much of each pixel near the picked colour is selected. Gain is the tolerance and Soft the
// ramp beyond it, "relative to the RGB values of the color you picked" (ours: 0 to 63 as four 8-bit steps each).
std::vector<float> magicMask(const model::Effect& e, const Picture& picture, double time, const model::EffectSpan& span) {
    const QString cat = QStringLiteral("Magic Mask");
    const auto key = model::colorAt(e, cat, QStringLiteral("Color controls"), time, span);
    if (!key) return {};
    const double gain = std::clamp(number(e, cat, QStringLiteral("Gain"), time, span, 0.0), 0.0, 63.0) * 4.0 / 255.0;
    const double soft = std::clamp(number(e, cat, QStringLiteral("Soft"), time, span, 0.0), 0.0, 63.0) * 4.0 / 255.0;
    std::vector<float> m(static_cast<std::size_t>(picture.width()) * static_cast<std::size_t>(picture.height()));
    for (std::size_t i = 0; i < m.size(); ++i) {
        const float* px = &picture.data()[i * 4];
        const double a = px[3] > 0.0f ? static_cast<double>(px[3]) : 1.0;
        const double d = std::max({std::abs(static_cast<double>(px[0]) / a - key->red), std::abs(static_cast<double>(px[1]) / a - key->green), std::abs(static_cast<double>(px[2]) / a - key->blue)});
        double v = d <= gain ? 1.0 : (soft > 0.0 ? 1.0 - (d - gain) / soft : 0.0);
        m[i] = static_cast<float>(std::clamp(v, 0.0, 1.0));
    }
    return m;
}

// Unpremultiplied RGB of a pixel, and back.
std::array<double, 3> rgbOf(const float* px) {
    const double a = static_cast<double>(px[3]);
    if (a <= 0.0) return {0.0, 0.0, 0.0};
    return {static_cast<double>(px[0]) / a, static_cast<double>(px[1]) / a, static_cast<double>(px[2]) / a};
}

void setRgb(float* px, const std::array<double, 3>& rgb, double alpha) {
    for (int c = 0; c < 3; ++c) px[c] = static_cast<float>(std::clamp(rgb[static_cast<std::size_t>(c)], 0.0, 1.0) * alpha);
    px[3] = static_cast<float>(alpha);
}

// The Paint Effect's modes (p.537-544) applied to the whole picture `cur`; the caller keeps the result to the object's
// coverage. `original` is the segment's own picture (Erase), `bounds` the object's outline bounds in pixels (Gradient).
Picture paintMode(const model::Effect& oe, const QString& mode, const Picture& cur, const Picture& original, const QRectF& bounds, double dx,
                  double dy, double time, const model::EffectSpan& span) {
    const int w = cur.width(), h = cur.height();
    const double unit = w / 1920.0;
    const QString cat = QStringLiteral("Paint Effect Mode Parameters");
    const QString colorCat = QStringLiteral("Paint Effect Color Parameters");
    const model::Rgb colour = model::colorAt(oe, colorCat, QStringLiteral("Color controls"), time, span).value_or(model::Rgb{1.0, 1.0, 1.0});
    const QColor q = QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green), static_cast<float>(colour.blue));
    const double hor = std::max(0.0, number(oe, cat, QStringLiteral("Hor"), time, span, 10.0)) * unit;
    const double vert = std::max(0.0, number(oe, cat, QStringLiteral("Vert"), time, span, 10.0)) * unit;
    const double amount = number(oe, cat, QStringLiteral("Amt"), time, span, 50.0);
    const double angle = number(oe, cat, QStringLiteral("Angle"), time, span, 0.0) * std::numbers::pi / 180.0;
    Picture out = cur;
    auto perPixel = [&](auto f) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float* px = out.pixel(x, y);
                const double a = px[3] > 0.0f ? static_cast<double>(px[3]) : 1.0;
                std::array<double, 3> rgb = rgbOf(px);
                f(rgb, x, y);
                setRgb(px, rgb, a);
            }
        }
    };
    auto hsl = [](const std::array<double, 3>& rgb) {
        return QColor::fromRgbF(static_cast<float>(std::clamp(rgb[0], 0.0, 1.0)), static_cast<float>(std::clamp(rgb[1], 0.0, 1.0)),
                                static_cast<float>(std::clamp(rgb[2], 0.0, 1.0)));
    };
    auto fromHsl = [](float hue, float sat, float light) {
        const QColor c = QColor::fromHslF(std::clamp(hue, 0.0f, 1.0f), std::clamp(sat, 0.0f, 1.0f), std::clamp(light, 0.0f, 1.0f));
        return std::array<double, 3>{static_cast<double>(c.redF()), static_cast<double>(c.greenF()), static_cast<double>(c.blueF())};
    };
    if (mode == QLatin1String("Solid") || mode == QLatin1String("Outline")) {
        // "Applies paint as a solid color superimposed on the video background" (p.538); Outline draws the colour along
        // the outline (the caller's coverage is the outline's).
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) setRgb(out.pixel(x, y), {colour.red, colour.green, colour.blue}, 1.0);
        }
    } else if (mode == QLatin1String("Erase")) {
        // "Removes paint from a frame" (p.538): the segment's own picture again.
        out = original;
    } else if (mode == QLatin1String("Clone") || mode == QLatin1String("Scratch Removal")) {
        // "Duplicates an area from a frame and applies it to another area of the frame ... An oval object in Clone mode
        // that has been moved from higher in the image, taking an area of the upper part of the image with it" (p.538):
        // the picture from where the object was drawn, before it was moved, and Hor/Vert away from there. Scratch
        // Removal's `original` is the frame or fields it takes its material from (scratchRemovalSource).
        const bool scratch = mode == QLatin1String("Scratch Removal");
        // Scratch Removal's Hor and Vert: "the position of the replacement material relative to the shape ... The default
        // value is 0" (p.547).
        const double sx = -dx + (scratch ? number(oe, cat, QStringLiteral("Hor"), time, span, 0.0) * unit : 0.0);
        const double sy = -dy + (scratch ? number(oe, cat, QStringLiteral("Vert"), time, span, 0.0) * unit : 0.0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) detail::sample(original, x + 0.5 + sx, y + 0.5 + sy, out.pixel(x, y));
        }
    } else if (mode == QLatin1String("Colorize")) {
        // "Tints the selected area using the value in the Color parameter category" (p.538): its hue and saturation, the
        // picture's lightness.
        perPixel([&](std::array<double, 3>& rgb, int, int) { rgb = fromHsl(q.hslHueF() < 0 ? 0.0f : q.hslHueF(), q.hslSaturationF(), hsl(rgb).lightnessF()); });
    } else if (mode == QLatin1String("Hue") || mode == QLatin1String("Saturation") || mode == QLatin1String("Luminance")) {
        // Each changes one of the picture's hue, saturation or luminance to the Color category's (p.539).
        perPixel([&](std::array<double, 3>& rgb, int, int) {
            const QColor c = hsl(rgb);
            float hue = c.hslHueF() < 0 ? 0.0f : c.hslHueF(), sat = c.hslSaturationF(), light = c.lightnessF();
            if (mode == QLatin1String("Hue")) hue = q.hslHueF() < 0 ? 0.0f : q.hslHueF();
            else if (mode == QLatin1String("Saturation")) sat = q.hslSaturationF();
            else light = q.lightnessF();
            rgb = fromHsl(hue, sat, light);
        });
    } else if (mode == QLatin1String("Darken Only") || mode == QLatin1String("Lighten Only")) {
        // "Reduces the luminance levels ... only if the result would be darker than the background color" (p.540).
        const bool darken = mode == QLatin1String("Darken Only");
        perPixel([&](std::array<double, 3>& rgb, int, int) {
            const std::array<double, 3> c{colour.red, colour.green, colour.blue};
            for (std::size_t k = 0; k < 3; ++k) rgb[k] = darken ? std::min(rgb[k], c[k]) : std::max(rgb[k], c[k]);
        });
    } else if (mode == QLatin1String("Darken") || mode == QLatin1String("Lighten")) {
        // "Luminance in the selected oval is reduced by 50" (p.541): Amt in 8-bit steps.
        const double step = (mode == QLatin1String("Darken") ? -amount : amount) / 255.0;
        perPixel([&](std::array<double, 3>& rgb, int, int) {
            for (double& v : rgb) v += step;
        });
    } else if (mode == QLatin1String("Add") || mode == QLatin1String("Subtract")) {
        // "Adds color to the colors in a selected area using the value of the Color parameter category" (p.541).
        const double sign = mode == QLatin1String("Add") ? 1.0 : -1.0;
        perPixel([&](std::array<double, 3>& rgb, int, int) {
            rgb[0] += sign * colour.red;
            rgb[1] += sign * colour.green;
            rgb[2] += sign * colour.blue;
        });
    } else if (mode == QLatin1String("Invert")) {
        // "Inverts the RGB values of the colors in a selected area" (p.542).
        perPixel([&](std::array<double, 3>& rgb, int, int) {
            for (double& v : rgb) v = 1.0 - v;
        });
    } else if (mode == QLatin1String("Mosaic") || mode == QLatin1String("Blur")) {
        // As the Mosaic and Blur effects, with this category's Hor and Vert (p.542).
        QJsonObject p = oe.parameters;
        const QString target = mode == QLatin1String("Blur") ? QStringLiteral("Blur Effect Blur Parameters") : QStringLiteral("Mosaic Effect Mosaic Parameters");
        QJsonObject values{{QStringLiteral("Hor"), hor / unit}, {QStringLiteral("Vert"), vert / unit}};
        p.insert(target, values);
        out = detail::detailEffectPicture(model::Effect{QStringLiteral("Image"), mode == QLatin1String("Blur") ? QStringLiteral("Blur Effect") : QStringLiteral("Mosaic Effect"), p},
                                          cur, time, span);
    } else if (mode == QLatin1String("Median")) {
        // "Takes the nine values surrounding each pixel, ranks them, and replaces the pixel with the value in the middle"
        // (p.543).
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    std::array<float, 9> v{};
                    int k = 0;
                    for (int j = -1; j <= 1; ++j) {
                        for (int i = -1; i <= 1; ++i) v[static_cast<std::size_t>(k++)] = cur.pixel(std::clamp(x + i, 0, w - 1), std::clamp(y + j, 0, h - 1))[c];
                    }
                    std::nth_element(v.begin(), v.begin() + 4, v.end());
                    out.pixel(x, y)[c] = v[4];
                }
            }
        }
    } else if (mode == QLatin1String("Unsharp Mask")) {
        // "Sharpens a selected area of an image to create more defined edges with horizontal and vertical controls"
        // (p.543): the picture plus Amt percent of its difference from a blur of Hor by Vert.
        Picture blurred = cur;
        detail::blurPicture(blurred, static_cast<int>(std::lround(hor)), static_cast<int>(std::lround(vert)));
        const float k = static_cast<float>(amount / 100.0);
        for (std::size_t i = 0; i < out.data().size(); ++i) {
            if (i % 4 == 3) continue;
            out.data()[i] = std::clamp(cur.data()[i] + k * (cur.data()[i] - blurred.data()[i]), 0.0f, cur.data()[i - i % 4 + 3]);
        }
    } else if (mode == QLatin1String("Gradient")) {
        // "Dissipates the paint in a gradient. Use the Angle slider to set the direction of dissipation. 100% of the value
        // in the Color parameter category appears at one end of the gradient, 0% at the other end" (p.543): across the
        // object's bounds.
        const QPointF c = bounds.center();
        const double ux = std::cos(angle), uy = -std::sin(angle);
        const double half = std::max(1.0, (std::abs(ux) * bounds.width() + std::abs(uy) * bounds.height()) / 2.0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double s = ((x + 0.5 - c.x()) * ux + (y + 0.5 - c.y()) * uy) / half; // -1 to 1
                const double f = std::clamp((1.0 - s) / 2.0, 0.0, 1.0);
                float* px = out.pixel(x, y);
                const std::array<double, 3> rgb = rgbOf(cur.pixel(x, y));
                const double a = static_cast<double>(cur.pixel(x, y)[3]);
                setRgb(px, {rgb[0] + (colour.red - rgb[0]) * f, rgb[1] + (colour.green - rgb[1]) * f, rgb[2] + (colour.blue - rgb[2]) * f}, std::max(a, f));
            }
        }
    } else if (mode == QLatin1String("Emboss")) {
        // "Creates a three-dimensional extrusion effect in a selected area. Use the Angle slider to adjust the direction"
        // (p.543): grey from the luma's slope along the angle.
        const int ox = static_cast<int>(std::lround(std::cos(angle))), oy = static_cast<int>(std::lround(-std::sin(angle)));
        auto luma = [&](int x, int y) {
            const std::array<double, 3> rgb = rgbOf(cur.pixel(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1)));
            return kR * rgb[0] + kG * rgb[1] + kB * rgb[2];
        };
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const double v = 0.5 + 2.0 * (luma(x + ox, y + oy) - luma(x - ox, y - oy));
                setRgb(out.pixel(x, y), {v, v, v}, cur.pixel(x, y)[3] > 0.0f ? static_cast<double>(cur.pixel(x, y)[3]) : 1.0);
            }
        }
    } else if (mode == QLatin1String("Color Adjust") || mode == QLatin1String("Luma Range") || mode == QLatin1String("Luma Clip") ||
               mode == QLatin1String("Color Style") || mode == QLatin1String("Color Gain")) {
        // These take the Color Effect's categories (p.544, p.533-536).
        out = detail::colorEffectPicture(model::Effect{QStringLiteral("Image"), QStringLiteral("Color Effect"), oe.parameters}, cur, time, span);
    }
    // Color Match (p.544, p.624) comes with colour correction (6.10): ours until then, no change.
    return out;
}

} // namespace

QPainterPath intraframePath(const QList<model::ControlPoint>& points, bool closed, const QSizeF& frame) {
    QPainterPath path;
    if (points.isEmpty()) return path;
    auto at = [&](QPointF f) { return QPointF(f.x() * frame.width(), f.y() * frame.height()); };
    auto handle = [&](const model::ControlPoint& c, bool out) { return c.join == model::JoinType::Straight ? c.p : c.p + (out ? c.out : c.in); };
    path.moveTo(at(points.constFirst().p));
    const qsizetype n = points.size();
    const qsizetype segments = closed ? n : n - 1;
    for (qsizetype i = 0; i < segments; ++i) {
        const model::ControlPoint& a = points.at(i);
        const model::ControlPoint& b = points.at((i + 1) % n);
        if (a.join == model::JoinType::Straight && b.join == model::JoinType::Straight) path.lineTo(at(b.p));
        else path.cubicTo(at(handle(a, true)), at(handle(b, false)), at(b.p));
    }
    if (closed) path.closeSubpath();
    return path;
}

std::vector<float> intraframeMask(const model::Effect& effect, const model::IntraframeObject& object, const Picture& picture, double time,
                                  const model::EffectSpan& span) {
    const int w = picture.width(), h = picture.height();
    const std::size_t count = static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0));
    if (count == 0) return {};
    const model::Effect oe = model::objectEffect(effect, object);
    const QString cat = modeCategory(effect);
    if (!flagAt(oe, cat, visibleName(effect), time, true)) return std::vector<float>(count, 0.0f);
    const double unit = w / 1920.0;
    const QString feather = QStringLiteral("Feathering");
    const bool antialias = flagAt(oe, feather, QStringLiteral("Anti-alias"), time, false);
    const QList<model::ControlPoint> points = model::placedShapeAt(effect, object, time, span, h > 0 ? static_cast<double>(w) / h : 16.0 / 9.0);
    const QPainterPath outline = intraframePath(points, object.closed, QSizeF(w, h));
    const QString mode = text(oe, cat, QStringLiteral("Fast menu"), QStringLiteral("Solid"));
    std::vector<float> m;
    if (object.kind == model::IntraframeKind::Marker) {
        // A Previsualization Marker (p.412): ours, a disc 20 pixels across (of a frame 1920 wide) at its point.
        const QPointF at = points.isEmpty() ? QPointF() : QPointF(points.constFirst().p.x() * w, points.constFirst().p.y() * h);
        const double r = std::max(2.0, 10.0 * unit);
        m = drawnMask(w, h, true, [&](QPainter& p) { p.drawEllipse(at, r, r); });
    } else if (object.kind == model::IntraframeKind::Brush || !object.closed) {
        // A Brush stroke: the head stamped along the path, Size pixels (of a frame 1920 wide) large, turned by Spin,
        // softened by Soft, as far as Path (p.493).
        const QString brush = QStringLiteral("Brush");
        const double size = std::max(1.0, number(oe, brush, QStringLiteral("Size"), time, span, 20.0) * unit);
        const double soft = std::clamp(number(oe, brush, QStringLiteral("Soft"), time, span, 0.0), 0.0, 100.0) / 100.0;
        const double spin = number(oe, brush, QStringLiteral("Spin"), time, span, 0.0);
        const double path = std::clamp(number(oe, brush, QStringLiteral("Path"), time, span, 100.0), 0.0, 100.0) / 100.0;
        const QPainterPath head = brushHead(oe.parameters.value(brush).toObject());
        QPolygonF line;
        for (const QPolygonF& poly : outline.toSubpathPolygons()) line << poly;
        const QList<QPointF> stamps = strokeStamps(line, std::max(1.0, size / 4.0), path);
        m = drawnMask(w, h, true, [&](QPainter& p) {
            for (const QPointF& s : stamps) {
                QTransform t;
                t.translate(s.x(), s.y());
                t.rotate(-spin);
                t.scale(size, size);
                p.drawPath(t.map(head));
            }
        });
        blurMask(m, w, h, static_cast<int>(std::lround(soft * size / 4.0)), true);
        blurMask(m, w, h, static_cast<int>(std::lround(soft * size / 4.0)), false);
    } else if (mode == QLatin1String("Outline") && !model::isAniMatte(effect)) {
        // "Creates an outline that is useful for tracing images in a frame" (p.538): two pixels wide (ours).
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(2.0, 2.0 * unit));
        const QPainterPath edge = stroker.createStroke(outline);
        m = drawnMask(w, h, antialias, [&](QPainter& p) { p.drawPath(edge); });
    } else {
        m = drawnMask(w, h, antialias, [&](QPainter& p) { p.drawPath(outline); });
    }
    // Magic Mask keeps the object to the picked colour (p.497).
    const std::vector<float> magic = magicMask(oe, picture, time, span);
    if (!magic.empty()) {
        for (std::size_t i = 0; i < count; ++i) m[i] *= magic[i];
    }
    // Feathering (p.495): Hor and Vert soften the edge by up to 63 pixels each way; Bias moves the softening from
    // outside the edge (0) to inside it (100), 50 centring it on the edge.
    const int fh = static_cast<int>(std::lround(std::clamp(number(oe, feather, QStringLiteral("Hor"), time, span, 0.0), 0.0, 63.0) * unit / 2.0));
    const int fv = static_cast<int>(std::lround(std::clamp(number(oe, feather, QStringLiteral("Vert"), time, span, 0.0), 0.0, 63.0) * unit / 2.0));
    if (fh > 0 || fv > 0) {
        blurMask(m, w, h, fh, true);
        blurMask(m, w, h, fv, false);
        const double k = std::clamp(number(oe, feather, QStringLiteral("Bias"), time, span, 50.0), 0.0, 100.0) / 100.0;
        for (float& v : m) {
            double x = static_cast<double>(v);
            x = k <= 0.5 ? x / (0.5 + k) : (x - (k - 0.5)) / (1.5 - k);
            v = static_cast<float>(std::clamp(x, 0.0, 1.0));
        }
    }
    // Opacity (Blur and Mosaic apply theirs as their strength, color_effects.cpp).
    if (effect.name != QLatin1String("Blur Effect") && effect.name != QLatin1String("Mosaic Effect")) {
        const float opacity = static_cast<float>(std::clamp(number(oe, cat, QStringLiteral("Opac."), time, span, 100.0), 0.0, 100.0) / 100.0);
        for (float& v : m) v *= opacity;
    }
    return m;
}

// AniMatte's matte (p.548): objects combine in their layer order (ours): Key In adds to the foreground's area, Key Out
// takes from it; with only Key Out objects the foreground shows everywhere else. Invert Key and Level apply.
std::vector<float> aniMatteMatte(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span) {
    const QList<model::IntraframeObject> objects = model::intraframeObjects(effect);
    const int w = layer.width(), h = layer.height();
    const std::size_t count = static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0));
    const QString cat = modeCategory(effect);
    bool anyKeyIn = false;
    for (const model::IntraframeObject& o : objects) {
        anyKeyIn = anyKeyIn || text(model::objectEffect(effect, o), cat, QStringLiteral("Fast menu"), QStringLiteral("Key In")) != QLatin1String("Key Out");
    }
    std::vector<float> matte(count, anyKeyIn || objects.isEmpty() ? 0.0f : 1.0f);
    for (const model::IntraframeObject& o : objects) {
        const std::vector<float> m = intraframeMask(effect, o, layer, time, span);
        const bool keyOut = text(model::objectEffect(effect, o), cat, QStringLiteral("Fast menu"), QStringLiteral("Key In")) == QLatin1String("Key Out");
        for (std::size_t i = 0; i < count; ++i) matte[i] = keyOut ? matte[i] * (1.0f - m[i]) : matte[i] + m[i] * (1.0f - matte[i]);
    }
    const QString fg = QStringLiteral("Foreground");
    const float level = static_cast<float>(std::clamp(number(effect, fg, QStringLiteral("Level"), time, span, 100.0), 0.0, 100.0) / 100.0);
    const bool invert = flagAt(effect, fg, QStringLiteral("Invert Key"), time, false);
    for (float& v : matte) v = (invert ? 1.0f - v : v) * level;
    return matte;
}

QImage aniMatteImage(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span) {
    const std::vector<float> matte = aniMatteMatte(effect, layer, time, span);
    QImage image(layer.width(), layer.height(), QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            // "a high-contrast matte with transparent (black) and opaque (white) components" (p.418), its alpha the same.
            const int v = std::clamp(static_cast<int>(std::lround(matte[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()) + static_cast<std::size_t>(x)] * 255.0f)), 0, 255);
            line[x] = qRgba(v, v, v, v);
        }
    }
    return image;
}

Picture scratchRemovalSource(const model::Effect& oe, const QString& cat, const Picture& layer, const IntraframeFrames* frames, double time,
                             const model::EffectSpan& span) {
    if (!frames || !frames->pictureAt || frames->length <= 0) return layer;
    // "The default value of -1 provides an instant correction when you are working with the normal two-frame effect
    // created by the Scratch Removal button" (p.547); the Paint Effect's mode names its menu "Offset Fast menu" (p.537).
    const int amount = static_cast<int>(std::lround(number(oe, cat, QStringLiteral("Frame/Field"), time, span, -1.0)));
    const model::ScratchOffset offset =
        model::scratchOffsetFrom(text(oe, cat, QStringLiteral("Offset Fast Menu"), text(oe, cat, QStringLiteral("Offset Fast menu"), QStringLiteral("From Start"))));
    const int w = layer.width(), h = layer.height();
    auto frameAt = [&](std::int64_t f) -> Picture {
        if (f == frames->offset) return layer;
        Picture p = frames->pictureAt(f);
        return p.width() == w && p.height() == h ? p : layer;
    };
    // "Frame processing is the default processing method"; "The button does not appear when you are working with
    // single-field material" (p.420, p.547).
    if (!frames->interlaced || flagAt(oe, cat, QStringLiteral("Frame Processing"), time, true)) {
        return frameAt(model::scratchSourceIndex(offset, amount, false, frames->offset, frames->length));
    }
    // Field processing: each field of this frame from the field the slider names, counted in fields from the segment's
    // start (field 1 of frame n is field 2n).
    Picture out = layer;
    for (int f = 0; f < 2; ++f) {
        const std::int64_t s = model::scratchSourceIndex(offset, amount, true, frames->offset * 2 + f, frames->length * 2);
        const Picture source = frameAt(s / 2);
        const int sourceParity = frames->topFieldFirst ? static_cast<int>(s % 2) : 1 - static_cast<int>(s % 2);
        const int targetParity = frames->topFieldFirst ? f : 1 - f;
        for (int y = targetParity; y < h; y += 2) {
            const int sy = std::min(h - 1, y - targetParity + sourceParity);
            std::copy(source.pixel(0, sy), source.pixel(0, sy) + static_cast<std::ptrdiff_t>(w) * 4, out.pixel(0, y));
        }
    }
    return out;
}

Picture intraframePicture(const model::Effect& effect, const Picture& layer, const Picture& below, double time, const model::EffectSpan& span,
                          const IntraframeFrames* frames) {
    const QList<model::IntraframeObject> objects = model::intraframeObjects(effect);
    const int w = layer.width(), h = layer.height();
    const std::size_t count = static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0));
    if (model::isAniMatte(effect)) {
        // AniMatte (p.548): "Key In ... displays the image on the higher track (foreground) within the matte selection
        // while revealing the image on the lower track (background) outside"; Key Out the reverse. Objects combine in
        // their layer order (ours): Key In adds to the foreground's area, Key Out takes from it; with only Key Out objects
        // the foreground shows everywhere else.
        const std::vector<float> matte = aniMatteMatte(effect, layer, time, span);
        const QString fg = QStringLiteral("Foreground");
        const bool swap = flagAt(effect, fg, QStringLiteral("Swap Sources"), time, false);
        const bool showAlpha = flagAt(effect, fg, QStringLiteral("Show Alpha"), time, false);
        const Picture& front = swap ? below : layer;
        const Picture& back = swap ? layer : below;
        Picture out(w, h);
        for (std::size_t i = 0; i < count; ++i) {
            const float a = matte[i];
            float* o = &out.data()[i * 4];
            if (showAlpha) {
                o[0] = o[1] = o[2] = a;
                o[3] = 1.0f;
                continue;
            }
            for (std::size_t c = 0; c < 4; ++c) o[c] = back.data()[i * 4 + c] * (1.0f - a) + front.data()[i * 4 + c] * a;
        }
        return out;
    }
    // Paint, Blur, Mosaic and Scratch Removal: each object changes the picture within its coverage, bottom layer first
    // (p.392). Without objects the segment shows as it is ("Parameter sliders do not appear until you select one of the
    // corresponding tools", p.393).
    Picture cur = layer;
    const QString cat = modeCategory(effect);
    for (const model::IntraframeObject& o : objects) {
        const model::Effect oe = model::objectEffect(effect, o);
        const std::vector<float> m = intraframeMask(effect, o, cur, time, span);
        Picture mod;
        if (effect.name == QLatin1String("Blur Effect") || effect.name == QLatin1String("Mosaic Effect")) {
            mod = detail::detailEffectPicture(oe, cur, time, span);
        } else {
            const model::ObjectPlace place = model::objectPlaceAt(effect, o, time, span);
            const double unit = w / 1920.0;
            // A Previsualization Marker shows as its colour (ours: Solid whatever the mode).
            const QString mode = effect.name == QLatin1String("Scratch Removal") ? QStringLiteral("Scratch Removal")
                                 : o.kind == model::IntraframeKind::Marker   ? QStringLiteral("Solid")
                                                                             : text(oe, cat, QStringLiteral("Fast menu"), QStringLiteral("Solid"));
            const QList<model::ControlPoint> placed = model::placedShapeAt(effect, o, time, span, h > 0 ? static_cast<double>(w) / h : 16.0 / 9.0);
            const QRectF bounds = intraframePath(placed, o.closed, QSizeF(w, h)).boundingRect();
            model::Effect modeEffect = oe;
            if (effect.name == QLatin1String("Scratch Removal")) {
                // The Scratch category's Hor and Vert as the Paint mode's (p.546).
                const QJsonObject scratch = oe.parameters.value(cat).toObject();
                QJsonObject paint = modeEffect.parameters.value(QStringLiteral("Paint Effect Mode Parameters")).toObject();
                for (const char* key : {"Hor", "Vert"}) {
                    if (scratch.contains(QLatin1String(key))) paint.insert(QLatin1String(key), scratch.value(QLatin1String(key)));
                    else paint.insert(QLatin1String(key), 0);
                }
                modeEffect.parameters.insert(QStringLiteral("Paint Effect Mode Parameters"), paint);
            }
            // Scratch Removal's material comes from the frame or fields its slider and menu name (6.9d).
            const Picture original = mode == QLatin1String("Scratch Removal") ? scratchRemovalSource(oe, cat, layer, frames, time, span) : layer;
            mod = paintMode(modeEffect, mode, cur, original, bounds, place.x * unit, place.y * unit, time, span);
        }
        for (std::size_t i = 0; i < count; ++i) {
            const float a = m[i];
            if (a <= 0.0f) continue;
            for (std::size_t c = 0; c < 4; ++c) cur.data()[i * 4 + c] += (mod.data()[i * 4 + c] - cur.data()[i * 4 + c]) * a;
        }
    }
    return over(cur, below);
}

// ---------------------------------------------------------------- shape-based colour correction (p.697-699)

std::vector<float> correctionShapesMask(const model::Effect& effect, int width, int height, double time, const model::EffectSpan& span) {
    const std::size_t count = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    std::vector<float> m(count, 0.0f);
    if (count == 0) return m;
    const Picture blank(width, height);
    for (const model::IntraframeObject& shape : model::intraframeObjects(effect)) {
        std::vector<float> one = intraframeMask(effect, shape, blank, time, span);
        if (one.size() != count) continue;
        // "Use the slider to add softness to the selected shape. Each shape can have a different softness" (p.699):
        // ours, a blur of up to 100 pixels of a frame 1920 wide.
        const int r = static_cast<int>(std::lround(model::shapeSoftness(shape) * width / 1920.0));
        if (r > 0) {
            blurMask(one, width, height, r, true);
            blurMask(one, width, height, r, false);
        }
        for (std::size_t i = 0; i < count; ++i) m[i] = std::max(m[i], one[i]);
    }
    if (model::shapeOptionsOf(effect).invertAlpha) {
        for (float& v : m) v = 1.0f - v;
    }
    return m;
}

Picture colorCorrectedShapesPicture(const model::Effect& effect, const Picture& layer, const model::ColorCorrection& base,
                                    const model::ColorCorrection& shapes, double time, const model::EffectSpan& span) {
    Picture out = colorCorrectedPicture(base, layer);
    if (!model::hasCorrectionShapes(effect) || out.isNull()) return out;
    // "Serial Mode - when selected, any changes you have made to the base will be included" (p.698).
    const Picture shaped = colorCorrectedPicture(shapes, model::shapeOptionsOf(effect).serial ? out : layer);
    const std::vector<float> m = correctionShapesMask(effect, out.width(), out.height(), time, span);
    std::vector<float>& d = out.data();
    const std::vector<float>& s = shaped.data();
    for (std::size_t i = 0; i < m.size() && i * 4 + 3 < d.size() && i * 4 + 3 < s.size(); ++i) {
        for (std::size_t c = 0; c < 4; ++c) d[i * 4 + c] += (s[i * 4 + c] - d[i * 4 + c]) * m[i];
    }
    return out;
}

} // namespace lumenis::media
