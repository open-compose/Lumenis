// Direct manipulation in the Effect Preview monitor (ROADMAP 6.7a4; FX Guide p.92-93, p.292-293;
// docs/spec/3d-effects.md, "Direct manipulation"): the outline, handles, drags and motion path of the effects
// that place a picture. The 2D geometry repeats image_effects.cpp's Picture-in-Picture, Resize and Mask
// placement; the 3D geometry comes from warp3d.cpp's forward functions. A drag is solved numerically: the
// parameters a handle stands for are changed until the handle lies under the pointer, so the handles follow
// the pointer whatever else the effect does to the picture.
#include "lumenis/media/effect_handles.h"

#include "lumenis/media/tracking.h"

#include "lumenis/media/compositor.h"

#include <QJsonObject>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

namespace lumenis::media {

namespace {

bool is(const model::Effect& e, const char* category, const char* name) {
    return e.category == QLatin1String(category) && e.name == QLatin1String(name);
}

// The Film masks' width over height (p.461), as image_effects.cpp draws them; nothing for the plain masks.
std::optional<double> maskRatio(const model::Effect& e) {
    if (e.category != QLatin1String("Film")) return std::nullopt;
    if (e.name == QLatin1String("1.66 Mask")) return 1.66;
    if (e.name == QLatin1String("1.85 Mask")) return 1.85;
    if (e.name == QLatin1String("16:9 Mask")) return 16.0 / 9.0;
    if (e.name == QLatin1String("Anamorphic Mask")) return 2.35;
    return std::nullopt;
}

bool isMask(const model::Effect& e) {
    return is(e, "Image", "Mask") || is(e, "Film", "Mask") || maskRatio(e).has_value();
}

bool is2D(const model::Effect& e) {
    return is(e, "Blend", "Picture-in-Picture") || is(e, "Image", "Resize") || isMask(e);
}

double number(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, category, name, time, span).value_or(parameterDefault(e, category, name).toDouble(0.0));
}

// A 3D parameter category is on once a value or keyframe is set (as warp3d.cpp has it).
bool enabled(const model::Effect& e, const QString& category) {
    if (e.parameters.contains(category)) return true;
    const QString prefix = category + QLatin1Char('/');
    const QStringList keyed = model::keyframedParameters(e);
    return std::any_of(keyed.begin(), keyed.end(), [&](const QString& k) { return k.startsWith(prefix); });
}

QPointF middle(const QPointF& a, const QPointF& b) {
    return (a + b) / 2.0;
}

// image_effects.cpp's placement: the cropped picture (or the mask's rectangle) scaled about the frame's
// centre and moved by Position (pixels of a 1920-wide picture, Y down).
std::optional<EffectOutline> outline2D(const model::Effect& e, double time, const model::EffectSpan& span, double W, double H) {
    const QString scaling = QStringLiteral("Scaling");
    const QString position = QStringLiteral("Position");
    const double sx = std::max(number(e, scaling, QStringLiteral("X"), time, span), 0.0) / 100.0;
    const double sy = std::max(number(e, scaling, QStringLiteral("Y"), time, span), 0.0) / 100.0;
    const double dx = number(e, position, QStringLiteral("X"), time, span) * W / 1920.0;
    const double dy = number(e, position, QStringLiteral("Y"), time, span) * W / 1920.0;
    double x0 = 0, y0 = 0, x1 = W, y1 = H;
    if (isMask(e)) {
        // The mask's rectangle: the whole picture, or as wide as it and centred at the film ratio.
        double rh = H;
        if (const auto ratio = maskRatio(e)) rh = std::min(H, W / *ratio);
        y0 = (H - rh) / 2.0;
        y1 = y0 + rh;
    } else {
        // Crop (p.494): T 0 to 999 from the top, B -999 to 0 from the bottom, L and R alike.
        const QString crop = QStringLiteral("Crop");
        y0 = std::clamp(number(e, crop, QStringLiteral("T"), time, span), 0.0, 999.0) / 999.0 * H;
        y1 = H + std::clamp(number(e, crop, QStringLiteral("B"), time, span), -999.0, 0.0) / 999.0 * H;
        x0 = std::clamp(number(e, crop, QStringLiteral("L"), time, span), 0.0, 999.0) / 999.0 * W;
        x1 = W + std::clamp(number(e, crop, QStringLiteral("R"), time, span), -999.0, 0.0) / 999.0 * W;
    }
    auto place = [&](double x, double y) { return QPointF((x - W / 2.0) * sx + W / 2.0 + dx, (y - H / 2.0) * sy + H / 2.0 + dy); };
    EffectOutline o;
    o.corners = {place(x0, y0), place(x1, y0), place(x1, y1), place(x0, y1)};
    o.center = place((x0 + x1) / 2.0, (y0 + y1) / 2.0);
    return o;
}

std::optional<EffectOutline> outline3D(const model::Effect& e, double time, const model::EffectSpan& span, const QSizeF& size) {
    const auto [l, r, b, t] = warp3dCropBox(e, time, span, size);
    const std::array<std::pair<double, double>, 4> plane{{{l, t}, {r, t}, {r, b}, {l, b}}};
    EffectOutline o;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto p = warp3dScreenPoint(e, time, span, size, plane[i].first, plane[i].second);
        if (!p) return std::nullopt;
        o.corners[i] = *p;
    }
    const auto c = warp3dScreenPoint(e, time, span, size, (l + r) / 2.0, (b + t) / 2.0);
    if (!c) return std::nullopt;
    o.center = *c;
    return o;
}

// ---------------------------------------------------------------- solving drags

// The effect with the unknown parameters set to `values` (their keyframes taken away, so that the values
// hold at every time).
using Maker = std::function<model::Effect(const std::vector<double>&)>;
// Where the dragged handle is on an effect; nothing when it cannot be seen.
using Locator = std::function<std::optional<QPointF>(const model::Effect&)>;

// Damped Gauss-Newton: the values near `start` that put the handle at `target`. `xOnly` matches only the
// horizontal coordinate. Steps that lose the handle (behind the viewer) are halved.
std::vector<double> solve(const Maker& make, const Locator& locate, std::vector<double> v, const QPointF& target, bool xOnly = false) {
    auto residual = [&](const std::vector<double>& values) -> std::optional<QPointF> {
        const auto p = locate(make(values));
        if (!p) return std::nullopt;
        QPointF r = target - *p;
        if (xOnly) r.setY(0.0);
        return r;
    };
    auto norm = [](const QPointF& p) { return std::hypot(p.x(), p.y()); };
    auto r = residual(v);
    if (!r) return v;
    // One or two values (fixed arrays, so the compiler sees no empty vector).
    const std::size_t n = v.size();
    if (n < 1 || n > 2) return v;
    for (int iteration = 0; iteration < 24 && norm(*r) > 1e-3; ++iteration) {
        // The handle's movement per unit of each value.
        std::array<QPointF, 2> J{};
        for (std::size_t i = 0; i < n; ++i) {
            std::vector<double> w = v;
            const double h = std::max(std::abs(v[i]) * 1e-4, 1e-3);
            w[i] += h;
            auto rw = residual(w);
            if (!rw) return v;
            double dh = h;
            if (std::hypot(rw->x() - r->x(), rw->y() - r->y()) < 1e-12) {
                // At the end of a clamped range (Crop's 999): look the other way.
                w[i] = v[i] - h;
                rw = residual(w);
                if (!rw) return v;
                dh = -h;
            }
            J[i] = (*r - *rw) / dh; // d(position)/d(value)
        }
        // (JᵀJ + λ) δ = Jᵀ r, for one or two values.
        double a = QPointF::dotProduct(J[0], J[0]);
        const double g0 = QPointF::dotProduct(J[0], *r);
        std::array<double, 2> step{};
        if (n == 1) {
            if (a < 1e-12) return v;
            step[0] = g0 / a;
        } else {
            double d = QPointF::dotProduct(J[1], J[1]);
            const double b = QPointF::dotProduct(J[0], J[1]);
            const double g1 = QPointF::dotProduct(J[1], *r);
            const double damping = 1e-9 * std::max(a + d, 1e-12);
            a += damping;
            d += damping;
            const double det = a * d - b * b;
            if (std::abs(det) < 1e-18) return v;
            step[0] = (d * g0 - b * g1) / det;
            step[1] = (a * g1 - b * g0) / det;
        }
        // Take the step, or less of it, as long as the handle comes closer.
        bool better = false;
        for (double scale = 1.0; scale > 1e-3; scale /= 2.0) {
            std::vector<double> w = v;
            for (std::size_t i = 0; i < n; ++i) w[i] += step[i] * scale;
            const auto rw = residual(w);
            if (rw && norm(*rw) < norm(*r)) {
                v = w;
                r = rw;
                better = true;
                break;
            }
        }
        if (!better) break;
    }
    return v;
}

struct Unknown {
    QString category;
    QString name;
};

// `effect` with `unknowns` set to `values`, their keyframes removed.
model::Effect withValues(const model::Effect& effect, const QList<Unknown>& unknowns, const std::vector<double>& values) {
    model::Effect e = effect;
    for (qsizetype i = 0; i < unknowns.size(); ++i) {
        const Unknown& u = unknowns.at(i);
        model::setKeyframes(e, u.category, u.name, {});
        QJsonObject group = e.parameters.value(u.category).toObject();
        group.insert(u.name, values[static_cast<std::size_t>(i)]);
        e.parameters.insert(u.category, group);
    }
    return e;
}

QList<HandleValue> valuesOf(const QList<Unknown>& unknowns, const std::vector<double>& values) {
    QList<HandleValue> out;
    for (qsizetype i = 0; i < unknowns.size(); ++i) out << HandleValue{unknowns.at(i).category, unknowns.at(i).name, values[static_cast<std::size_t>(i)]};
    return out;
}

// Moves `unknowns` (starting from their values at `time`) until the handle `locate` finds is `to - from`
// away from where it was.
QList<HandleValue> follow(const model::Effect& effect, const QList<Unknown>& unknowns, const Locator& locate, const QPointF& from,
                          const QPointF& to, double time, const model::EffectSpan& span, bool xOnly = false) {
    const auto start = locate(effect);
    if (!start) return {};
    std::vector<double> v;
    for (const Unknown& u : unknowns) v.push_back(number(effect, u.category, u.name, time, span));
    const Maker make = [&](const std::vector<double>& values) { return withValues(effect, unknowns, values); };
    return valuesOf(unknowns, solve(make, locate, v, *start + (to - from), xOnly));
}

// The scaled pair's "corner" handle: one factor on both, "maintaining the aspect ratio" (p.92).
QList<HandleValue> scaleCorner(const model::Effect& effect, const QString& category, const Locator& locate, const QPointF& from,
                               const QPointF& to, double time, const model::EffectSpan& span) {
    const auto start = locate(effect);
    if (!start) return {};
    const QList<Unknown> both{{category, QStringLiteral("X")}, {category, QStringLiteral("Y")}};
    const double x0 = number(effect, category, QStringLiteral("X"), time, span);
    const double y0 = number(effect, category, QStringLiteral("Y"), time, span);
    const Maker make = [&](const std::vector<double>& f) { return withValues(effect, both, {x0 * f[0], y0 * f[0]}); };
    const double f = solve(make, locate, {1.0}, *start + (to - from))[0];
    return {{category, QStringLiteral("X"), x0 * f}, {category, QStringLiteral("Y"), y0 * f}};
}

double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

// A handle's place on an outline, by name.
std::optional<QPointF> handleOn(const EffectOutline& o, const QString& id) {
    const auto& c = o.corners;
    if (id == QLatin1String("move")) return o.center;
    if (id == QLatin1String("top") || id == QLatin1String("crop:T") || id == QLatin1String("rotX:top")) return middle(c[0], c[1]);
    if (id == QLatin1String("crop:B") || id == QLatin1String("rotX:bottom")) return middle(c[3], c[2]);
    if (id == QLatin1String("side") || id == QLatin1String("crop:R") || id == QLatin1String("rotY:right")) return middle(c[1], c[2]);
    if (id == QLatin1String("crop:L") || id == QLatin1String("rotY:left")) return middle(c[0], c[3]);
    if (id == QLatin1String("corner")) return c[1];
    if (id == QLatin1String("rotZ")) return c[3];
    if (id.startsWith(QLatin1String("corner"))) {
        const int n = id.mid(6).toInt();
        if (n >= 1 && n <= 4) return c[static_cast<std::size_t>(n - 1)];
    }
    return std::nullopt;
}

// Pan and Scan's wire frame (p.236-237): "You cannot change the shape of the wire frame, which is determined by
// the Target aspect ratio. Media Composer maintains the shape during scaling", so each resize handle scales both
// ways; dragging inside it moves it along "the most important axis" (across when the source is wider than the
// Target, else up and down), Shift in all directions; and "You cannot drag the wire frame off the screen"
// (ours: it stays within the source picture; centred where it is larger).
QList<HandleValue> dragPanScan(const model::Effect& effect, const QString& id, const QPointF& from, const QPointF& to, double time,
                               const model::EffectSpan& span, const QSizeF& size, bool shift) {
    const Locator onOutline = [&](const model::Effect& e) -> std::optional<QPointF> {
        const auto o = effectOutline(e, time, span, size);
        return o ? handleOn(*o, id) : std::nullopt;
    };
    const QString position = QStringLiteral("Position");
    if (id == QLatin1String("top") || id == QLatin1String("side") || id == QLatin1String("corner")) {
        return scaleCorner(effect, QStringLiteral("Scaling"), onOutline, from, to, time, span);
    }
    if (id != QLatin1String("move")) return {};
    const auto f = panScanFrames(effect, time, span, size);
    if (!f) return {};
    const bool across = f->source.width() / f->source.height() - f->wire.width() / f->wire.height() >= -1e-9;
    QPointF by = to - from;
    if (!shift) {
        if (across) by.setY(0.0);
        else by.setX(0.0);
    }
    // The wire frame's centre, kept inside the source picture.
    QPointF centre = f->wire.center() + by;
    auto keep = [](double c, double half, double low, double high) {
        return high - low <= 2.0 * half ? (low + high) / 2.0 : std::clamp(c, low + half, high - half);
    };
    centre.setX(keep(centre.x(), f->wire.width() / 2.0, f->source.left(), f->source.right()));
    centre.setY(keep(centre.y(), f->wire.height() / 2.0, f->source.top(), f->source.bottom()));
    const double unit = 1920.0 / size.width();
    const QPointF offset = (centre - f->source.center()) * unit;
    QList<HandleValue> out;
    if (shift || across) out << HandleValue{position, QStringLiteral("X"), offset.x()};
    if (shift || !across) out << HandleValue{position, QStringLiteral("Y"), offset.y()};
    return out;
}

} // namespace

bool hasEffectOutline(const model::Effect& effect) {
    return is2D(effect) || isPanAndScan(effect) || is3DEffect(effect) || isPanAndZoom(effect) || isRegionStabilize(effect);
}

std::optional<EffectOutline> effectOutline(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size) {
    if (size.width() <= 0.0 || size.height() <= 0.0) return std::nullopt;
    if (isPanAndZoom(effect)) {
        // The field of view over the whole image (Display Source); no outline over the result (Display Target).
        const auto v = panZoomView(effect, time, span, size);
        if (!v || !v->sourceDisplay) return std::nullopt;
        EffectOutline o;
        o.corners = {v->field.topLeft(), v->field.topRight(), v->field.bottomRight(), v->field.bottomLeft()};
        o.center = v->field.center();
        return o;
    }
    if (isRegionStabilize(effect)) {
        // "A wire frame appears in the Effect Preview monitor to indicate the region of interest" (p.280).
        const QRectF r = regionOfInterest(effect, time, span);
        const QRectF at(r.x() * size.width(), r.y() * size.height(), r.width() * size.width(), r.height() * size.height());
        EffectOutline o;
        o.corners = {at.topLeft(), at.topRight(), at.bottomRight(), at.bottomLeft()};
        o.center = at.center();
        return o;
    }
    if (const auto f = panScanFrames(effect, time, span, size)) {
        // Pan and Scan's wire frame over the original picture (p.233-236).
        EffectOutline o;
        o.corners = {f->wire.topLeft(), f->wire.topRight(), f->wire.bottomRight(), f->wire.bottomLeft()};
        o.center = f->wire.center();
        return o;
    }
    if (is2D(effect)) return outline2D(effect, time, span, size.width(), size.height());
    if (is3DEffect(effect)) return outline3D(effect, time, span, size);
    return std::nullopt;
}

QList<EffectHandle> effectHandles(const model::Effect& effect, HandleTool tool, double time, const model::EffectSpan& span,
                                  const QSizeF& size) {
    if (isPanAndZoom(effect)) {
        const auto v = panZoomView(effect, time, span, size);
        if (!v) return {};
        const QPointF at = v->sourceDisplay ? v->field.center() : QPointF(size.width() / 2.0, size.height() / 2.0);
        return {EffectHandle{QStringLiteral("crosshair"), at, std::nullopt}};
    }
    const auto o = effectOutline(effect, time, span, size);
    if (!o) return {};
    QStringList ids;
    if (is2D(effect) || isPanAndScan(effect) || isRegionStabilize(effect)) {
        ids = {QStringLiteral("move"), QStringLiteral("top"), QStringLiteral("side"), QStringLiteral("corner")};
    } else {
        switch (tool) {
        case HandleTool::None: break;
        case HandleTool::CornerPin:
            ids = {QStringLiteral("corner1"), QStringLiteral("corner2"), QStringLiteral("corner3"), QStringLiteral("corner4")};
            break;
        case HandleTool::Crop: ids = {QStringLiteral("crop:T"), QStringLiteral("crop:B"), QStringLiteral("crop:L"), QStringLiteral("crop:R")}; break;
        case HandleTool::Scale: ids = {QStringLiteral("top"), QStringLiteral("side"), QStringLiteral("corner")}; break;
        case HandleTool::Axis: ids = {QStringLiteral("axis")}; break;
        case HandleTool::XRotation: ids = {QStringLiteral("rotX:top"), QStringLiteral("rotX:bottom")}; break;
        case HandleTool::YRotation: ids = {QStringLiteral("rotY:left"), QStringLiteral("rotY:right")}; break;
        case HandleTool::ZRotation: ids = {QStringLiteral("rotZ")}; break;
        case HandleTool::XYPosition:
        case HandleTool::XZPosition: ids = {QStringLiteral("move")}; break;
        }
    }
    QList<EffectHandle> out;
    for (const QString& id : ids) {
        if (id == QLatin1String("axis")) {
            if (const auto a = warp3dAxisPoint(effect, time, span, size)) out << EffectHandle{id, *a, std::nullopt};
            continue;
        }
        const auto at = handleOn(*o, id);
        if (!at) continue;
        // The rotation handles hang on lines from the centre (p.293's illustrations).
        const bool line = id.startsWith(QLatin1String("rot"));
        out << EffectHandle{id, *at, line ? std::optional<QPointF>(o->center) : std::nullopt};
    }
    return out;
}

QString effectHandleAt(const model::Effect& effect, HandleTool tool, double time, const model::EffectSpan& span, const QSizeF& size,
                       const QPointF& point, double reach) {
    QString best;
    double nearest = reach;
    bool moves = false;
    for (const EffectHandle& h : effectHandles(effect, tool, time, span, size)) {
        if (h.id == QLatin1String("move")) moves = true;
        const double d = std::hypot(h.at.x() - point.x(), h.at.y() - point.y());
        if (d <= nearest) {
            nearest = d;
            best = h.id;
        }
    }
    if (!best.isEmpty() || !moves) return best;
    // "Drag the wire frame to reposition the effect" (p.92): anywhere inside it.
    const auto o = effectOutline(effect, time, span, size);
    if (!o) return {};
    const QPolygonF quad{o->corners[0], o->corners[1], o->corners[2], o->corners[3]};
    return quad.containsPoint(point, Qt::OddEvenFill) ? QStringLiteral("move") : QString();
}

QList<HandleValue> dragEffectHandle(const model::Effect& effect, HandleTool tool, const QString& id, const QPointF& from,
                                    const QPointF& to, double time, const model::EffectSpan& span, const QSizeF& size, bool shift) {
    if (!effectOutline(effect, time, span, size)) return {};
    const Locator onOutline = [&](const model::Effect& e) -> std::optional<QPointF> {
        const auto o = effectOutline(e, time, span, size);
        return o ? handleOn(*o, id) : std::nullopt;
    };
    const QString X = QStringLiteral("X");
    const QString Y = QStringLiteral("Y");
    const QString Z = QStringLiteral("Z");
    if (isPanAndScan(effect)) return dragPanScan(effect, id, from, to, time, span, size, shift);
    if (isRegionStabilize(effect)) {
        // "Click the wire frame, and drag it" (p.280): inside it moves the region, the handles size it about its
        // centre (ours).
        const QString region = QStringLiteral("Region of Interest");
        if (id == QLatin1String("move")) return follow(effect, {{region, X}, {region, Y}}, onOutline, from, to, time, span);
        if (id == QLatin1String("top")) return follow(effect, {{region, QStringLiteral("Height")}}, onOutline, from, to, time, span);
        if (id == QLatin1String("side")) return follow(effect, {{region, QStringLiteral("Width")}}, onOutline, from, to, time, span);
        if (id == QLatin1String("corner")) {
            return follow(effect, {{region, QStringLiteral("Width")}, {region, QStringLiteral("Height")}}, onOutline, from, to, time, span);
        }
        return {};
    }
    if (isPanAndZoom(effect)) {
        // The crosshair pans: in Display Source the field of view follows it over the image; in Display Target
        // (where the crosshair stays in the middle) the view moves by what the drag covers at the zoom.
        const auto v = panZoomView(effect, time, span, size);
        if (!v || id != QLatin1String("crosshair")) return {};
        const QPointF by = (to - from) / (v->sourceDisplay ? 1.0 : v->zoom);
        const QString category = QStringLiteral("Avid Pan & Zoom Effect Parameters");
        return {{category, X, number(effect, category, X, time, span) + by.x() / (v->fitted.width() / 2.0) * 100.0},
                {category, Y, number(effect, category, Y, time, span) + by.y() / (v->fitted.height() / 2.0) * 100.0}};
    }
    if (is2D(effect)) {
        const QString position = QStringLiteral("Position");
        const QString scaling = QStringLiteral("Scaling");
        if (id == QLatin1String("move")) return follow(effect, {{position, X}, {position, Y}}, onOutline, from, to, time, span);
        if (id == QLatin1String("top")) return follow(effect, {{scaling, Y}}, onOutline, from, to, time, span);
        if (id == QLatin1String("side")) return follow(effect, {{scaling, X}}, onOutline, from, to, time, span);
        if (id == QLatin1String("corner")) return scaleCorner(effect, scaling, onOutline, from, to, time, span);
        return {};
    }
    if (!is3DEffect(effect)) return {};
    const QString position = QStringLiteral("Position (3D)");
    const QString scaling = QStringLiteral("Scaling (3D)");
    const QString rotation = QStringLiteral("Rotation");
    const double W = size.width();
    const double H = size.height();
    switch (tool) {
    case HandleTool::None: return {};
    case HandleTool::CornerPin: {
        // Corner N's X and Y run from -999 to 999 across the screen (warp3d.cpp), so they follow the pointer
        // directly. The first drag of an effect without Corner Pin pins all four corners where they are.
        const auto o = effectOutline(effect, time, span, size);
        if (!o || !id.startsWith(QLatin1String("corner"))) return {};
        const int n = id.mid(6).toInt();
        if (n < 1 || n > 4) return {};
        const QString pin = QStringLiteral("Corner Pin");
        const bool on = enabled(effect, pin);
        QList<HandleValue> out;
        for (int i = 1; i <= 4; ++i) {
            if (on && i != n) continue;
            const QString corner = QStringLiteral("Corner %1: ").arg(i);
            QPointF at = on ? QPointF(number(effect, pin, corner + X, time, span), number(effect, pin, corner + Y, time, span))
                            : QPointF((o->corners[static_cast<std::size_t>(i - 1)].x() - W / 2.0) / (W / 2.0) * 999.0,
                                      -(o->corners[static_cast<std::size_t>(i - 1)].y() - H / 2.0) / (H / 2.0) * 999.0);
            if (i == n) at += QPointF((to.x() - from.x()) / (W / 2.0) * 999.0, -(to.y() - from.y()) / (H / 2.0) * 999.0);
            out << HandleValue{pin, corner + X, at.x()} << HandleValue{pin, corner + Y, at.y()};
        }
        return out;
    }
    case HandleTool::Crop: {
        const QString crop = QStringLiteral("Crop (3D)");
        if (!id.startsWith(QLatin1String("crop:"))) return {};
        return follow(effect, {{crop, id.mid(5)}}, onOutline, from, to, time, span);
    }
    case HandleTool::Scale:
        if (id == QLatin1String("top")) return follow(effect, {{scaling, Y}}, onOutline, from, to, time, span);
        if (id == QLatin1String("side")) return follow(effect, {{scaling, X}}, onOutline, from, to, time, span);
        if (id == QLatin1String("corner")) return scaleCorner(effect, scaling, onOutline, from, to, time, span);
        return {};
    case HandleTool::Axis: {
        const QString axis = QStringLiteral("Axis");
        const Locator onAxis = [&](const model::Effect& e) { return warp3dAxisPoint(e, time, span, size); };
        return follow(effect, {{axis, X}, {axis, Y}}, onAxis, from, to, time, span);
    }
    case HandleTool::XRotation: {
        // Ours: half a turn over the picture's height; dragging the top handle down tips the top towards the
        // viewer (positive X), the bottom handle the other way.
        const double sign = id == QLatin1String("rotX:bottom") ? -1.0 : 1.0;
        return {{rotation, X, number(effect, rotation, X, time, span) + sign * (to.y() - from.y()) / H * 180.0}};
    }
    case HandleTool::YRotation: {
        // Ours: half a turn over the width; dragging the right handle left takes the right edge away (positive Y).
        const double sign = id == QLatin1String("rotY:left") ? -1.0 : 1.0;
        return {{rotation, Y, number(effect, rotation, Y, time, span) - sign * (to.x() - from.x()) / W * 180.0}};
    }
    case HandleTool::ZRotation: {
        // The angle swept about the centre, clockwise positive (p.291).
        const auto o = effectOutline(effect, time, span, size);
        if (!o) return {};
        const QPointF a = from - o->center;
        const QPointF b = to - o->center;
        double turn = degrees(std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x()));
        if (turn > 180.0) turn -= 360.0;
        if (turn <= -180.0) turn += 360.0;
        return {{rotation, Z, number(effect, rotation, Z, time, span) + turn}};
    }
    case HandleTool::XYPosition: return follow(effect, {{position, X}, {position, Y}}, onOutline, from, to, time, span);
    case HandleTool::XZPosition: {
        // X follows the pointer across; up and down move the picture in depth (ours: down brings it closer,
        // two units of a 1920-wide picture per pixel dragged).
        QList<HandleValue> out = follow(effect, {{position, X}}, onOutline, from, to, time, span, true);
        out << HandleValue{position, Z, number(effect, position, Z, time, span) + (to.y() - from.y()) * 2.0 * 1920.0 / W};
        return out;
    }
    }
    return {};
}

QList<QPolygonF> effectGuides(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size) {
    const auto v = panZoomView(effect, time, span, size);
    if (!v || !v->safeAction) return {};
    const QRectF area = v->sourceDisplay ? v->field : QRectF(QPointF(0, 0), size);
    const QRectF safe = area.adjusted(area.width() * 0.05, area.height() * 0.05, -area.width() * 0.05, -area.height() * 0.05);
    return {QPolygonF(safe)};
}

EffectPath effectPath(const model::Effect& effect, const model::EffectSpan& span, const QSizeF& size) {
    EffectPath path;
    if (!hasEffectOutline(effect)) return path;
    // The centre over the effect, frame by frame (at most about 128 points).
    const std::int64_t length = std::max<std::int64_t>(span.length, 1);
    const std::int64_t step = std::max<std::int64_t>(length / 128, 1);
    bool moves = false;
    for (std::int64_t f = 0;; f += step) {
        if (f > length - 1) f = length - 1;
        if (const auto o = effectOutline(effect, span.timeOf(static_cast<double>(f)), span, size)) {
            if (!path.line.isEmpty() && std::hypot(o->center.x() - path.line.first().x(), o->center.y() - path.line.first().y()) > 0.5) {
                moves = true;
            }
            path.line << o->center;
        }
        if (f == length - 1) break;
    }
    if (!moves) path.line.clear();
    // A dot at each keyframe's time.
    QList<double> times;
    for (const QString& key : model::keyframedParameters(effect)) {
        const qsizetype slash = key.indexOf(QLatin1Char('/'));
        if (slash < 0) continue;
        const model::ParameterKeyframes keys = model::keyframesOf(effect, key.left(slash), key.mid(slash + 1));
        for (const model::EffectKeyframe& k : keys.keys) {
            const double t = model::keyframeTime(keys, k, span);
            if (t < -1e-9 || t > 1.0 + 1e-9) continue;
            if (std::none_of(times.begin(), times.end(), [&](double u) { return std::abs(u - t) <= span.halfFrame(); })) times << t;
        }
    }
    std::sort(times.begin(), times.end());
    for (double t : times) {
        if (const auto o = effectOutline(effect, t, span, size)) path.dots << o->center;
    }
    return path;
}

} // namespace lumenis::media
