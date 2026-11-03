// The Intraframe effects' objects (intraframe.h; FX Guide ch.15): their JSON under the effect's "Intraframe"
// category, shapes between keyframes, their place, and the layer, group, lock and delete operations of p.403 and
// p.416-418. Drawing them is media's (intraframe.cpp there).
#include "lumenis/model/intraframe.h"

#include <QJsonArray>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::model {

namespace {

QJsonArray pointJson(const QPointF& p) {
    return QJsonArray{p.x(), p.y()};
}

QPointF pointFrom(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a.at(0).toDouble(), a.at(1).toDouble()) : QPointF();
}

QString joinName(JoinType j) {
    switch (j) {
    case JoinType::Smooth: return QStringLiteral("smooth");
    case JoinType::Corner: return QStringLiteral("corner");
    case JoinType::Straight: break;
    }
    return QStringLiteral("straight");
}

JoinType joinFrom(const QString& s) {
    if (s == QLatin1String("smooth")) return JoinType::Smooth;
    if (s == QLatin1String("corner")) return JoinType::Corner;
    return JoinType::Straight;
}

// The objects' common group when all named objects are in groups, for layer moves of whole groups.
QStringList withGroups(const QList<IntraframeObject>& objects, const QStringList& ids) {
    QStringList groups;
    for (const IntraframeObject& o : objects) {
        if (ids.contains(o.id) && !o.group.isEmpty() && !groups.contains(o.group)) groups << o.group;
    }
    QStringList out = ids;
    for (const IntraframeObject& o : objects) {
        if (!o.group.isEmpty() && groups.contains(o.group) && !out.contains(o.id)) out << o.id;
    }
    return out;
}

} // namespace

bool isIntraframeEffect(const Effect& effect) {
    if (effect.category == QLatin1String("Image")) {
        return effect.name == QLatin1String("Paint Effect") || effect.name == QLatin1String("Blur Effect") ||
               effect.name == QLatin1String("Mosaic Effect") || effect.name == QLatin1String("Scratch Removal");
    }
    return isAniMatte(effect);
}

bool isAniMatte(const Effect& effect) {
    return effect.category == QLatin1String("Key") && effect.name == QLatin1String("AniMatte");
}

QString intraframeKindName(IntraframeKind kind) {
    switch (kind) {
    case IntraframeKind::Oval: return QStringLiteral("oval");
    case IntraframeKind::Polygon: return QStringLiteral("polygon");
    case IntraframeKind::Curve: return QStringLiteral("curve");
    case IntraframeKind::Brush: return QStringLiteral("brush");
    case IntraframeKind::Marker: return QStringLiteral("marker");
    case IntraframeKind::Rectangle: break;
    }
    return QStringLiteral("rectangle");
}

std::optional<IntraframeKind> intraframeKindFrom(const QString& name) {
    for (IntraframeKind k : {IntraframeKind::Rectangle, IntraframeKind::Oval, IntraframeKind::Polygon, IntraframeKind::Curve,
                             IntraframeKind::Brush, IntraframeKind::Marker}) {
        if (intraframeKindName(k) == name) return k;
    }
    return std::nullopt;
}

QJsonObject ControlPoint::toJson() const {
    QJsonObject o{{QStringLiteral("p"), pointJson(p)}};
    if (!in.isNull()) o.insert(QStringLiteral("in"), pointJson(in));
    if (!out.isNull()) o.insert(QStringLiteral("out"), pointJson(out));
    if (join != JoinType::Straight) o.insert(QStringLiteral("join"), joinName(join));
    if (fixedLength) o.insert(QStringLiteral("fixedLength"), true);
    return o;
}

ControlPoint ControlPoint::fromJson(const QJsonObject& o) {
    ControlPoint c;
    c.p = pointFrom(o.value(QStringLiteral("p")));
    c.in = pointFrom(o.value(QStringLiteral("in")));
    c.out = pointFrom(o.value(QStringLiteral("out")));
    c.join = joinFrom(o.value(QStringLiteral("join")).toString());
    c.fixedLength = o.value(QStringLiteral("fixedLength")).toBool();
    return c;
}

QJsonObject IntraframeObject::toJson() const {
    QJsonArray shapeArray;
    for (const IntraframeShape& s : shapes) {
        QJsonArray points;
        for (const ControlPoint& c : s.points) points.append(c.toJson());
        shapeArray.append(QJsonObject{{QStringLiteral("at"), s.at}, {QStringLiteral("points"), points}});
    }
    QJsonObject o{{QStringLiteral("id"), id}, {QStringLiteral("kind"), intraframeKindName(kind)}, {QStringLiteral("shapes"), shapeArray}};
    if (!closed) o.insert(QStringLiteral("closed"), false);
    if (!parameters.isEmpty()) o.insert(QStringLiteral("parameters"), parameters);
    if (!group.isEmpty()) o.insert(QStringLiteral("group"), group);
    if (locked) o.insert(QStringLiteral("locked"), true);
    return o;
}

IntraframeObject IntraframeObject::fromJson(const QJsonObject& o) {
    IntraframeObject obj;
    obj.id = o.value(QStringLiteral("id")).toString();
    obj.kind = intraframeKindFrom(o.value(QStringLiteral("kind")).toString()).value_or(IntraframeKind::Rectangle);
    obj.closed = o.value(QStringLiteral("closed")).toBool(true);
    for (const QJsonValue& v : o.value(QStringLiteral("shapes")).toArray()) {
        IntraframeShape s;
        s.at = v.toObject().value(QStringLiteral("at")).toDouble();
        for (const QJsonValue& p : v.toObject().value(QStringLiteral("points")).toArray()) s.points << ControlPoint::fromJson(p.toObject());
        obj.shapes << s;
    }
    std::sort(obj.shapes.begin(), obj.shapes.end(), [](const IntraframeShape& a, const IntraframeShape& b) { return a.at < b.at; });
    obj.parameters = o.value(QStringLiteral("parameters")).toObject();
    obj.group = o.value(QStringLiteral("group")).toString();
    obj.locked = o.value(QStringLiteral("locked")).toBool();
    return obj;
}

QList<IntraframeObject> intraframeObjects(const Effect& effect) {
    QList<IntraframeObject> out;
    for (const QJsonValue& v : effect.parameters.value(kIntraframeCategory).toObject().value(QStringLiteral("objects")).toArray()) {
        IntraframeObject o = IntraframeObject::fromJson(v.toObject());
        if (!o.shapes.isEmpty()) out << o;
    }
    return out;
}

void setIntraframeObjects(Effect& effect, const QList<IntraframeObject>& objects) {
    if (objects.isEmpty()) {
        effect.parameters.remove(kIntraframeCategory);
        return;
    }
    QJsonArray a;
    for (const IntraframeObject& o : objects) a.append(o.toJson());
    effect.parameters.insert(kIntraframeCategory, QJsonObject{{QStringLiteral("objects"), a}});
}

QString addIntraframeObject(Effect& effect, IntraframeKind kind, const QList<ControlPoint>& points) {
    IntraframeObject o;
    o.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    o.kind = kind;
    o.closed = kind != IntraframeKind::Brush && kind != IntraframeKind::Marker;
    o.shapes = {IntraframeShape{0.0, points}};
    // The effect's own parameters, without the objects and the tracking data.
    o.parameters = effect.parameters;
    o.parameters.remove(kIntraframeCategory);
    o.parameters.remove(QStringLiteral("Trackers"));
    QList<IntraframeObject> all = intraframeObjects(effect);
    all << o;
    setIntraframeObjects(effect, all);
    return o.id;
}

Effect objectEffect(const Effect& effect, const IntraframeObject& object) {
    return Effect{effect.category, effect.name, object.parameters};
}

QList<ControlPoint> rectangleShape(const QRectF& rect) {
    const QRectF r = rect.normalized();
    return {ControlPoint{r.topLeft(), {}, {}, JoinType::Straight, false}, ControlPoint{r.topRight(), {}, {}, JoinType::Straight, false},
            ControlPoint{r.bottomRight(), {}, {}, JoinType::Straight, false}, ControlPoint{r.bottomLeft(), {}, {}, JoinType::Straight, false}};
}

QList<ControlPoint> ovalShape(const QRectF& rect) {
    // A quarter circle as a cubic Bézier: handles 0.5523 of the radius (4/3 tan(pi/8)).
    const QRectF r = rect.normalized();
    constexpr double k = 0.5522847498;
    const double rx = r.width() / 2.0, ry = r.height() / 2.0;
    const QPointF c = r.center();
    auto point = [](QPointF p, QPointF in, QPointF out) { return ControlPoint{p, in, out, JoinType::Smooth, false}; };
    return {point(QPointF(c.x(), r.top()), QPointF(-k * rx, 0), QPointF(k * rx, 0)), point(QPointF(r.right(), c.y()), QPointF(0, -k * ry), QPointF(0, k * ry)),
            point(QPointF(c.x(), r.bottom()), QPointF(k * rx, 0), QPointF(-k * rx, 0)), point(QPointF(r.left(), c.y()), QPointF(0, k * ry), QPointF(0, -k * ry))};
}

QList<ControlPoint> polylineShape(const QList<QPointF>& points) {
    QList<ControlPoint> out;
    for (const QPointF& p : points) out << ControlPoint{p, {}, {}, JoinType::Straight, false};
    return out;
}

QList<ControlPoint> shapeAt(const IntraframeObject& object, double t) {
    if (object.shapes.isEmpty()) return {};
    if (t <= object.shapes.constFirst().at) return object.shapes.constFirst().points;
    if (t >= object.shapes.constLast().at) return object.shapes.constLast().points;
    for (qsizetype i = 0; i + 1 < object.shapes.size(); ++i) {
        const IntraframeShape& a = object.shapes.at(i);
        const IntraframeShape& b = object.shapes.at(i + 1);
        if (t < a.at || t > b.at) continue;
        if (a.points.size() != b.points.size() || b.at <= a.at) return a.points;
        const double f = (t - a.at) / (b.at - a.at);
        QList<ControlPoint> out = a.points;
        for (qsizetype k = 0; k < out.size(); ++k) {
            out[k].p = a.points.at(k).p + (b.points.at(k).p - a.points.at(k).p) * f;
            out[k].in = a.points.at(k).in + (b.points.at(k).in - a.points.at(k).in) * f;
            out[k].out = a.points.at(k).out + (b.points.at(k).out - a.points.at(k).out) * f;
        }
        return out;
    }
    return object.shapes.constLast().points;
}

QRectF shapeBounds(const QList<ControlPoint>& points) {
    if (points.isEmpty()) return {};
    double x0 = points.constFirst().p.x(), x1 = x0, y0 = points.constFirst().p.y(), y1 = y0;
    for (const ControlPoint& c : points) {
        x0 = std::min(x0, c.p.x());
        x1 = std::max(x1, c.p.x());
        y0 = std::min(y0, c.p.y());
        y1 = std::max(y1, c.p.y());
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

ObjectPlace objectPlaceAt(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span) {
    const Effect e = objectEffect(effect, object);
    const QString& cat = kIntraframeObjectCategory;
    ObjectPlace place;
    place.x = numberAt(e, cat, QStringLiteral("X"), t, span).value_or(0.0);
    place.y = numberAt(e, cat, QStringLiteral("Y"), t, span).value_or(0.0);
    place.rotation = numberAt(e, cat, QStringLiteral("Rotation"), t, span).value_or(0.0);
    place.scale = numberAt(e, cat, QStringLiteral("Scale"), t, span).value_or(100.0);
    const auto cx = numberAt(e, cat, QStringLiteral("Center X"), t, span);
    const auto cy = numberAt(e, cat, QStringLiteral("Center Y"), t, span);
    if (cx && cy) place.center = QPointF(*cx, *cy);
    return place;
}

QList<ControlPoint> placedShapeAt(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect) {
    QList<ControlPoint> points = shapeAt(object, t);
    if (points.isEmpty()) return points;
    const ObjectPlace place = objectPlaceAt(effect, object, t, span);
    const QPointF center = place.center.value_or(shapeBounds(points).center());
    // In a square measure (x in widths, y in widths too) so that rotation keeps shapes; offsets in 1920ths of the width.
    const double a = aspect > 0.0 ? aspect : 16.0 / 9.0;
    const double s = place.scale / 100.0;
    const double r = place.rotation * std::numbers::pi / 180.0;
    const double cs = std::cos(r), sn = std::sin(r);
    const QPointF offset(place.x / 1920.0, place.y / 1920.0 * a);
    auto mapPoint = [&](QPointF p) {
        const double dx = (p.x() - center.x()) * s;
        const double dy = (p.y() - center.y()) / a * s;
        const double rx = dx * cs - dy * sn;
        const double ry = dx * sn + dy * cs;
        return QPointF(center.x() + rx, center.y() + ry * a) + offset;
    };
    auto mapVector = [&](QPointF v) {
        const double dx = v.x() * s;
        const double dy = v.y() / a * s;
        return QPointF(dx * cs - dy * sn, (dx * sn + dy * cs) * a);
    };
    for (ControlPoint& c : points) {
        c.p = mapPoint(c.p);
        c.in = mapVector(c.in);
        c.out = mapVector(c.out);
    }
    return points;
}

bool reorderIntraframeObject(Effect& effect, const QString& id, IntraframeLayer to) {
    QList<IntraframeObject> all = intraframeObjects(effect);
    qsizetype i = -1;
    for (qsizetype k = 0; k < all.size(); ++k) {
        if (all.at(k).id == id) i = k;
    }
    if (i < 0) return false;
    qsizetype j = i;
    switch (to) {
    case IntraframeLayer::Forward: j = std::min(i + 1, all.size() - 1); break;
    case IntraframeLayer::Backward: j = std::max<qsizetype>(i - 1, 0); break;
    case IntraframeLayer::Front: j = all.size() - 1; break;
    case IntraframeLayer::Back: j = 0; break;
    }
    if (j == i) return false;
    all.move(i, j);
    setIntraframeObjects(effect, all);
    return true;
}

void groupIntraframeObjects(Effect& effect, const QStringList& ids) {
    QList<IntraframeObject> all = intraframeObjects(effect);
    // Grouping groups puts all their members in the new group (one level, as UnGroup undoes it completely).
    const QStringList members = withGroups(all, ids);
    if (members.size() < 2) return;
    const QString group = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    for (IntraframeObject& o : all) {
        if (members.contains(o.id)) o.group = group;
    }
    setIntraframeObjects(effect, all);
}

void ungroupIntraframeObjects(Effect& effect, const QStringList& ids) {
    QList<IntraframeObject> all = intraframeObjects(effect);
    const QStringList members = withGroups(all, ids);
    for (IntraframeObject& o : all) {
        if (members.contains(o.id)) o.group.clear();
    }
    setIntraframeObjects(effect, all);
}

void lockIntraframeObjects(Effect& effect, const QStringList& ids, bool locked) {
    QList<IntraframeObject> all = intraframeObjects(effect);
    const QStringList members = withGroups(all, ids);
    for (IntraframeObject& o : all) {
        if (members.contains(o.id)) o.locked = locked;
    }
    setIntraframeObjects(effect, all);
}

int deleteIntraframeObjects(Effect& effect, const QStringList& ids) {
    QList<IntraframeObject> all = intraframeObjects(effect);
    const auto before = all.size();
    all.removeIf([&](const IntraframeObject& o) { return ids.contains(o.id) && !o.locked; });
    const int deleted = static_cast<int>(before - all.size());
    if (deleted > 0) setIntraframeObjects(effect, all);
    return deleted;
}

QPointF unplacedPoint(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect, const QPointF& placed) {
    const QList<ControlPoint> points = shapeAt(object, t);
    const ObjectPlace place = objectPlaceAt(effect, object, t, span);
    const QPointF center = place.center.value_or(shapeBounds(points).center());
    const double a = aspect > 0.0 ? aspect : 16.0 / 9.0;
    const double s = place.scale != 0.0 ? place.scale / 100.0 : 1.0;
    const double r = -place.rotation * std::numbers::pi / 180.0;
    const QPointF offset(place.x / 1920.0, place.y / 1920.0 * a);
    const QPointF p = placed - offset - center;
    const double dx = p.x(), dy = p.y() / a;
    const double rx = (dx * std::cos(r) - dy * std::sin(r)) / s;
    const double ry = (dx * std::sin(r) + dy * std::cos(r)) / s;
    return QPointF(center.x() + rx, center.y() + ry * a);
}

QPointF unplacedVector(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect, const QPointF& placed) {
    return unplacedPoint(effect, object, t, span, aspect, placed) - unplacedPoint(effect, object, t, span, aspect, QPointF());
}

int editableShape(IntraframeObject& object, double t, const EffectSpan& span) {
    if (object.shapes.size() == 1) return 0;
    const double half = span.halfFrame();
    for (int i = 0; i < object.shapes.size(); ++i) {
        if (std::abs(object.shapes.at(i).at - t) <= half) return i;
    }
    IntraframeShape s{t, shapeAt(object, t)};
    object.shapes << s;
    std::sort(object.shapes.begin(), object.shapes.end(), [](const IntraframeShape& x, const IntraframeShape& y) { return x.at < y.at; });
    for (int i = 0; i < object.shapes.size(); ++i) {
        if (object.shapes.at(i).at == t) return i;
    }
    return 0;
}

void insertControlPoint(IntraframeObject& object, int after, double fraction) {
    const double f = std::clamp(fraction, 0.0, 1.0);
    for (IntraframeShape& shape : object.shapes) {
        QList<ControlPoint>& pts = shape.points;
        const qsizetype n = pts.size();
        if (after < 0 || after >= n || (!object.closed && after >= n - 1)) continue;
        ControlPoint& a = pts[after];
        ControlPoint& b = pts[(after + 1) % n];
        ControlPoint mid;
        if (a.join == JoinType::Straight && b.join == JoinType::Straight) {
            mid.p = a.p + (b.p - a.p) * f;
        } else {
            // De Casteljau: the curve split at f keeps its shape.
            const QPointF p0 = a.p, p1 = a.p + (a.join == JoinType::Straight ? QPointF() : a.out), p2 = b.p + (b.join == JoinType::Straight ? QPointF() : b.in),
                          p3 = b.p;
            const QPointF q0 = p0 + (p1 - p0) * f, q1 = p1 + (p2 - p1) * f, q2 = p2 + (p3 - p2) * f;
            const QPointF r0 = q0 + (q1 - q0) * f, r1 = q1 + (q2 - q1) * f;
            const QPointF s0 = r0 + (r1 - r0) * f;
            mid = ControlPoint{s0, r0 - s0, r1 - s0, JoinType::Smooth, false};
            if (a.join != JoinType::Straight) a.out = q0 - p0;
            if (b.join != JoinType::Straight) b.in = q2 - p3;
        }
        pts.insert(after + 1, mid);
    }
}

std::optional<std::pair<int, double>> nearestSegment(const IntraframeObject& object, double t, const QPointF& at, double reach) {
    const QList<ControlPoint> pts = shapeAt(object, t);
    const qsizetype n = pts.size();
    const qsizetype segments = object.closed ? n : n - 1;
    std::optional<std::pair<int, double>> best;
    double bestDistance = reach;
    for (qsizetype i = 0; i < segments; ++i) {
        const ControlPoint& a = pts.at(i);
        const ControlPoint& b = pts.at((i + 1) % n);
        const QPointF p1 = a.p + (a.join == JoinType::Straight ? QPointF() : a.out), p2 = b.p + (b.join == JoinType::Straight ? QPointF() : b.in);
        for (int k = 0; k <= 32; ++k) {
            const double f = k / 32.0, g = 1.0 - f;
            const QPointF p = a.p * (g * g * g) + p1 * (3 * g * g * f) + p2 * (3 * g * f * f) + b.p * (f * f * f);
            const double d = std::hypot(p.x() - at.x(), p.y() - at.y());
            if (d < bestDistance) {
                bestDistance = d;
                best = std::pair{static_cast<int>(i), f};
            }
        }
    }
    return best;
}

bool removeControlPoints(IntraframeObject& object, QList<int> indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    const qsizetype keep = object.closed ? 3 : 2;
    for (const IntraframeShape& s : object.shapes) {
        if (s.points.size() - indices.size() < keep) return false;
    }
    for (IntraframeShape& s : object.shapes) {
        for (auto it = indices.crbegin(); it != indices.crend(); ++it) {
            if (*it >= 0 && *it < s.points.size()) s.points.removeAt(*it);
        }
    }
    return !indices.isEmpty();
}

void toggleSmooth(IntraframeObject& object, int shape, int point, bool closed) {
    if (shape < 0 || shape >= object.shapes.size()) return;
    QList<ControlPoint>& pts = object.shapes[shape].points;
    if (point < 0 || point >= pts.size()) return;
    ControlPoint& c = pts[point];
    if (c.join != JoinType::Straight) {
        c.join = JoinType::Straight;
        c.in = c.out = QPointF();
        return;
    }
    // Handles along the line through the neighbours, a third of the way to each (ours).
    const qsizetype n = pts.size();
    const QPointF prev = pts.at(closed ? (point + n - 1) % n : std::max(point - 1, 0)).p;
    const QPointF next = pts.at(closed ? (point + 1) % n : std::min<qsizetype>(point + 1, n - 1)).p;
    const QPointF tangent = (next - prev) / 2.0;
    const double inLen = std::hypot(prev.x() - c.p.x(), prev.y() - c.p.y()) / 3.0;
    const double outLen = std::hypot(next.x() - c.p.x(), next.y() - c.p.y()) / 3.0;
    const double tl = std::hypot(tangent.x(), tangent.y());
    const QPointF unit = tl > 0.0 ? tangent / tl : QPointF(1.0, 0.0);
    c.join = JoinType::Smooth;
    c.in = -unit * inLen;
    c.out = unit * outLen;
}

void toggleCorner(IntraframeObject& object, int shape, int point) {
    if (shape < 0 || shape >= object.shapes.size() || point < 0 || point >= object.shapes.at(shape).points.size()) return;
    ControlPoint& c = object.shapes[shape].points[point];
    if (c.join == JoinType::Smooth) {
        c.join = JoinType::Corner;
    } else if (c.join == JoinType::Corner) {
        // Back in line: the in bar opposite the out bar, its length kept.
        c.join = JoinType::Smooth;
        const double l = std::hypot(c.in.x(), c.in.y());
        const double ol = std::hypot(c.out.x(), c.out.y());
        if (ol > 0.0) c.in = -c.out / ol * l;
    }
}

void toggleFixedLength(IntraframeObject& object, int shape, int point) {
    if (shape < 0 || shape >= object.shapes.size() || point < 0 || point >= object.shapes.at(shape).points.size()) return;
    ControlPoint& c = object.shapes[shape].points[point];
    c.fixedLength = !c.fixedLength;
}

void setDirectionHandle(ControlPoint& point, bool out, const QPointF& handle) {
    QPointF& moved = out ? point.out : point.in;
    QPointF& other = out ? point.in : point.out;
    moved = handle;
    if (point.join != JoinType::Smooth) return;
    const double l = std::hypot(handle.x(), handle.y());
    if (l <= 0.0) return;
    const double otherLength = point.fixedLength ? std::hypot(other.x(), other.y()) : l;
    other = -handle / l * otherLength;
}

// ---------------------------------------------------------------- brush heads

QStringList brushShapes() {
    return {QStringLiteral("Circle"), QStringLiteral("Oval"), QStringLiteral("Ellipse"), QStringLiteral("Square")};
}

QList<ControlPoint> brushShapePoints(const QString& shape) {
    // The square's corners stay straight-edge joins: only its points move (p.398).
    if (shape == QLatin1String("Square")) return rectangleShape(QRectF(-0.5, -0.5, 1.0, 1.0));
    if (shape == QLatin1String("Oval")) return ovalShape(QRectF(-0.5, -0.25, 1.0, 0.5));
    if (shape == QLatin1String("Ellipse")) return ovalShape(QRectF(-0.5, -0.15, 1.0, 0.3));
    return ovalShape(QRectF(-0.5, -0.5, 1.0, 1.0));
}

QList<ControlPoint> brushHeadPoints(const QJsonObject& brush) {
    QList<ControlPoint> head;
    for (const QJsonValue v : brush.value(QStringLiteral("Head")).toArray()) head << ControlPoint::fromJson(v.toObject());
    if (head.size() >= 3) return head;
    return brushShapePoints(brush.value(QStringLiteral("Shape")).toString(QStringLiteral("Circle")));
}

void setBrushHead(QJsonObject& brush, const QList<ControlPoint>& head) {
    if (head.isEmpty()) {
        brush.remove(QStringLiteral("Head"));
        return;
    }
    QJsonArray points;
    for (const ControlPoint& c : head) points.append(c.toJson());
    brush.insert(QStringLiteral("Head"), points);
}

void setBrushShape(QJsonObject& brush, const QString& shape) {
    brush.insert(QStringLiteral("Shape"), shape);
    brush.remove(QStringLiteral("Head"));
}

BinItem brushTemplateItem(const QJsonObject& brush, const QString& name, const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Effect;
    item.name = name.isEmpty() ? QStringLiteral("Brush") : name;
    item.created = when;
    item.modified = when;
    item.composition.insert(QStringLiteral("brush"), brush);
    return item;
}

std::optional<QJsonObject> brushTemplateOf(const BinItem& item) {
    if (item.kind != BinItemKind::Effect || !item.composition.value(QStringLiteral("brush")).isObject()) return std::nullopt;
    return item.composition.value(QStringLiteral("brush")).toObject();
}

} // namespace lumenis::model
