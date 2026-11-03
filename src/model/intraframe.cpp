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

} // namespace lumenis::model
