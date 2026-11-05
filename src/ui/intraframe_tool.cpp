// The Intraframe drawing tools in the Effect Preview (intraframe_tool.h; FX Guide p.395-404, p.413-417): drawing
// rectangles, ovals, polygons, freehand curves and Brush strokes; selecting with clicks, Shift+clicks and a lasso; moving
// and scaling the selection; Delete; the layer buttons. The objects themselves are model/intraframe.h's.
#include "lumenis/ui/intraframe_tool.h"

#include "lumenis/media/compositor.h"
#include "lumenis/ui/effect_editor.h"

#include <QKeyEvent>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

constexpr double kHandleReach = 0.02; // how near a handle a press must be, in fractions of the width (ours)
constexpr double kStrokeReach = 0.012;

double distance(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

double toSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF d = b - a;
    const double len2 = d.x() * d.x() + d.y() * d.y();
    if (len2 <= 0.0) return distance(p, a);
    const double t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2, 0.0, 1.0);
    return distance(p, a + d * t);
}

QList<QPointF> polylineOf(const QPainterPath& path) {
    QList<QPointF> out;
    for (const QPolygonF& poly : path.toSubpathPolygons()) {
        for (const QPointF& p : poly) out << p;
    }
    return out;
}

} // namespace

QList<model::ControlPoint> smoothCurve(const QList<QPointF>& points, bool closed) {
    // Keep a point every 1.5% of the width or so (ours), then Catmull-Rom tangents as handles.
    QList<QPointF> kept;
    for (const QPointF& p : points) {
        if (kept.isEmpty() || distance(kept.constLast(), p) >= 0.015) kept << p;
    }
    if (kept.size() < 2) return model::polylineShape(kept);
    QList<model::ControlPoint> out;
    const qsizetype n = kept.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF prev = kept.at(closed ? (i + n - 1) % n : std::max<qsizetype>(i - 1, 0));
        const QPointF next = kept.at(closed ? (i + 1) % n : std::min(i + 1, n - 1));
        const QPointF tangent = (next - prev) / 6.0;
        // "When you drag one of the direction handles, the direction bar on the opposite side of the control point moves in
        // unison but the length does not change in unison" (p.413): fixed length.
        out << model::ControlPoint{kept.at(i), -tangent, tangent, model::JoinType::Smooth, true};
    }
    return out;
}

IntraframeTool::IntraframeTool(IntraframeHost& host, QObject* parent) : QObject(parent), editor_(host) {}

void IntraframeTool::setTool(Tool tool) {
    if (tool != Tool::Polygon) finishPolygon();
    if (tool != Tool::Reshape) points_.clear();
    tool_ = tool;
    emit changed();
}

void IntraframeTool::select(const QStringList& ids) {
    const bool same = ids == selected_;
    if (!same) points_.clear();
    selected_ = ids;
    editor_.setIntraframeSelection(selected_);
    if (!same) emit changed();
}

void IntraframeTool::reset() {
    drag_ = Drag::None;
    polygon_.clear();
    working_.reset();
    start_.reset();
    selected_.clear();
}

std::optional<model::Effect> IntraframeTool::effect() const {
    if (working_) return working_;
    return editor_.intraframeEffect();
}

double IntraframeTool::aspect() const {
    return editor_.previewAspect();
}

QString IntraframeTool::objectAt(const QPointF& at) const {
    const auto e = effect();
    if (!e) return {};
    const QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
    const double t = editor_.positionTime();
    // Topmost first.
    for (auto it = objects.crbegin(); it != objects.crend(); ++it) {
        const QList<model::ControlPoint> placed = model::placedShapeAt(*e, *it, t, editor_.span(), aspect());
        // "Click the marker to highlight it" (p.413): within reach of its point.
        if (it->kind == model::IntraframeKind::Marker) {
            if (!placed.isEmpty() && distance(at, placed.constFirst().p) <= kStrokeReach * 2.0) return it->id;
            continue;
        }
        const QPainterPath path = media::intraframePath(placed, it->closed, QSizeF(1.0, 1.0));
        if (it->closed && path.contains(at)) return it->id;
        const QList<QPointF> line = polylineOf(path);
        for (qsizetype i = 1; i < line.size(); ++i) {
            if (toSegment(at, line.at(i - 1), line.at(i)) <= kStrokeReach) return it->id;
        }
    }
    return {};
}

QRectF IntraframeTool::selectionBounds() const {
    const auto e = effect();
    if (!e) return {};
    QRectF bounds;
    const double t = editor_.positionTime();
    for (const model::IntraframeObject& o : model::intraframeObjects(*e)) {
        if (!selected_.contains(o.id)) continue;
        const QList<model::ControlPoint> placed = model::placedShapeAt(*e, o, t, editor_.span(), aspect());
        // A marker's bounds are its small square (fillOverlay), so that its handles do not all lie on its point.
        const QRectF r = o.kind == model::IntraframeKind::Marker && !placed.isEmpty()
                             ? QRectF(placed.constFirst().p - QPointF(0.008, 0.008 * aspect()), QSizeF(0.016, 0.016 * aspect()))
                             : media::intraframePath(placed, o.closed, QSizeF(1.0, 1.0)).boundingRect();
        bounds = bounds.isNull() ? r : bounds.united(r);
    }
    return bounds;
}

QList<QPointF> IntraframeTool::handlePoints() const {
    const QRectF b = selectionBounds();
    if (b.isNull()) return {};
    // "Four selection points appear around the object" (p.413): its corners, clockwise from the top left.
    return {b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft()};
}

double IntraframeTool::objectNumber(const model::Effect& e, const model::IntraframeObject& o, const QString& name, double fallback) const {
    return model::numberAt(model::objectEffect(e, o), model::kIntraframeObjectCategory, name, editor_.positionTime(), editor_.span()).value_or(fallback);
}

void IntraframeTool::setObjectNumber(model::Effect& e, const QString& id, const QString& name, double value) const {
    QList<model::IntraframeObject> objects = model::intraframeObjects(e);
    for (model::IntraframeObject& o : objects) {
        if (o.id != id) continue;
        model::Effect oe = model::objectEffect(e, o);
        const QString& cat = model::kIntraframeObjectCategory;
        if (!model::keyframesOf(oe, cat, name).keys.isEmpty()) {
            model::setKeyframe(oe, cat, name, editor_.positionTime(), editor_.span(), value);
        } else {
            QJsonObject c = oe.parameters.value(cat).toObject();
            c.insert(name, value);
            oe.parameters.insert(cat, c);
        }
        o.parameters = oe.parameters;
    }
    model::setIntraframeObjects(e, objects);
}

void IntraframeTool::addObject(model::IntraframeKind kind, const QList<model::ControlPoint>& points, bool closed) {
    auto e = editor_.intraframeEffect();
    if (!e || points.size() < (closed ? 3 : 2)) return;
    const QString id = model::addIntraframeObject(*e, kind, points);
    if (!closed) {
        QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
        for (model::IntraframeObject& o : objects) {
            if (o.id == id) o.closed = false;
        }
        model::setIntraframeObjects(*e, objects);
    }
    // The tools' names as their undo steps (ours).
    static const char* const kNames[] = {"Rectangle", "Oval", "Polygon", "Curve", "Brush", "Marker"};
    if (commit(tr("Add %1").arg(QLatin1String(kNames[static_cast<int>(kind)])), *e)) {
        select({id});
        // "After you use the Rectangle, Oval, Polygon, and Curve tools, Media Composer reverts to the Selection tool ...
        // After you use the Brush tool, it remains the active tool" (p.396).
        if (kind != model::IntraframeKind::Brush) setTool(Tool::Selection);
    }
}

void IntraframeTool::finishPolygon() {
    if (polygon_.isEmpty()) return;
    const QList<model::ControlPoint> points = polygon_;
    polygon_.clear();
    polygonHandle_ = false;
    working_.reset();
    if (points.size() >= 3) addObject(model::IntraframeKind::Polygon, points, true);
    else editor_.previewIntraframe(std::nullopt);
}

void IntraframeTool::preview(const model::Effect& e) {
    working_ = e;
    editor_.previewIntraframe(e);
}

bool IntraframeTool::commit(const QString& description, const model::Effect& e) {
    working_.reset();
    editor_.previewIntraframe(std::nullopt);
    return editor_.changeIntraframe(description, e);
}

bool IntraframeTool::bringForward(bool all) {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.isEmpty()) return false;
    bool moved = false;
    QStringList ids = selected_;
    // Moving forward, the topmost selected goes first so the others follow in order.
    const QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
    std::sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        auto index = [&](const QString& id) { return std::find_if(objects.cbegin(), objects.cend(), [&](const auto& o) { return o.id == id; }) - objects.cbegin(); };
        return index(a) > index(b);
    });
    for (const QString& id : ids) moved = model::reorderIntraframeObject(*e, id, all ? model::IntraframeLayer::Front : model::IntraframeLayer::Forward) || moved;
    return moved && commit(all ? tr("Bring to Front") : tr("Bring Forward"), *e);
}

bool IntraframeTool::sendBackward(bool all) {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.isEmpty()) return false;
    bool moved = false;
    QStringList ids = selected_;
    const QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
    std::sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        auto index = [&](const QString& id) { return std::find_if(objects.cbegin(), objects.cend(), [&](const auto& o) { return o.id == id; }) - objects.cbegin(); };
        return index(a) < index(b);
    });
    for (const QString& id : ids) moved = model::reorderIntraframeObject(*e, id, all ? model::IntraframeLayer::Back : model::IntraframeLayer::Backward) || moved;
    return moved && commit(all ? tr("Send to Back") : tr("Send Backward"), *e);
}

bool IntraframeTool::removeSelected() {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.isEmpty()) return false;
    if (model::deleteIntraframeObjects(*e, selected_) == 0) return false;
    if (!commit(tr("Delete"), *e)) return false;
    select({});
    return true;
}

bool IntraframeTool::selectAdjacent(bool next) {
    // "Click the Go to Next Event button to move in a clockwise fashion to the next control point" (p.411).
    if (tool_ == Tool::Reshape && !points_.isEmpty()) return selectAdjacentPoint(next);
    const auto e = effect();
    if (!e) return false;
    const QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
    if (objects.isEmpty()) return false;
    qsizetype i = -1;
    for (qsizetype k = 0; k < objects.size(); ++k) {
        if (!selected_.isEmpty() && objects.at(k).id == selected_.constLast()) i = k;
    }
    const qsizetype n = objects.size();
    i = i < 0 ? (next ? 0 : n - 1) : (next ? (i + 1) % n : (i + n - 1) % n);
    select({objects.at(i).id});
    return true;
}

void IntraframeTool::fillOverlay(Composer::EffectPreview& preview) const {
    const auto e = effect();
    if (!e) return;
    const double t = editor_.positionTime();
    // Each object as a wire frame (ours: always, so that objects can be found; Outline/Path adds their paths).
    for (const model::IntraframeObject& o : model::intraframeObjects(*e)) {
        const QList<model::ControlPoint> placed = model::placedShapeAt(*e, o, t, editor_.span(), aspect());
        if (o.kind == model::IntraframeKind::Marker) {
            // A marker as a small square about its point (ours).
            if (placed.isEmpty()) continue;
            const QPointF p = placed.constFirst().p;
            const double dx = 0.008, dy = 0.008 * aspect();
            preview.boxes << QList<QPointF>{p + QPointF(-dx, -dy), p + QPointF(dx, -dy), p + QPointF(dx, dy), p + QPointF(-dx, dy)};
            continue;
        }
        const QList<QPointF> line = polylineOf(media::intraframePath(placed, o.closed, QSizeF(1.0, 1.0)));
        if (line.size() < 2) continue;
        if (o.closed) preview.boxes << line;
        else preview.lines << line;
    }
    // "Segments appear as wire frames until you finish painting the object" (p.401); the drag's shape.
    if (!polygon_.isEmpty()) {
        QList<model::ControlPoint> open = polygon_;
        if (drag_ == Drag::None && !polygonHandle_) open << model::ControlPoint{to_, {}, {}, model::JoinType::Straight, false};
        preview.lines << polylineOf(media::intraframePath(open, false, QSizeF(1.0, 1.0)));
    }
    if (drag_ == Drag::Shape) {
        const QRectF r = QRectF(from_, to_).normalized();
        const QList<model::ControlPoint> shape = tool_ == Tool::Oval ? model::ovalShape(r) : model::rectangleShape(r);
        preview.boxes << polylineOf(media::intraframePath(shape, true, QSizeF(1.0, 1.0)));
    }
    if (drag_ == Drag::Freehand && stroke_.size() > 1) preview.lines << stroke_;
    if (drag_ == Drag::Lasso) {
        const QRectF r = QRectF(from_, to_).normalized();
        preview.guides << QList<QPointF>{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
    }
    if (tool_ == Tool::Reshape || tool_ == Tool::ZRotation) {
        fillReshapeOverlay(preview);
        return;
    }
    const QList<QPointF> corners = handlePoints();
    for (int i = 0; i < corners.size(); ++i) {
        preview.handles << Composer::EffectPreview::Handle{QStringLiteral("intraframe:scale:%1").arg(i), corners.at(i), std::nullopt};
    }
}

bool IntraframeTool::overlayPress(const QPointF& at, Qt::KeyboardModifiers modifiers, bool doubleClick) {
    const auto e = editor_.intraframeEffect();
    if (!e) return false;
    from_ = to_ = at;
    switch (tool_) {
    case Tool::Rectangle:
    case Tool::Oval:
        drag_ = Drag::Shape;
        return true;
    case Tool::Marker: {
        // "Move the pointer to the location in the frame where you want to place the marker, and click the mouse. A
        // marker appears in the frame" (p.412). Ours: the tool then returns to Selection, as the shape tools do.
        auto effect = *e;
        const QString id = model::addPrevisualizationMarker(effect, at);
        if (commit(tr("Add Marker"), effect)) {
            select({id});
            setTool(Tool::Selection);
        }
        return true;
    }
    case Tool::Brush:
        // "Press and hold the Alt key ... The pointer becomes an eyedropper ... click it. The color you pick becomes the
        // new default color" (p.397).
        if (modifiers & Qt::AltModifier) return editor_.pickPaintColor(at);
        [[fallthrough]];
    case Tool::Curve:
        drag_ = Drag::Freehand;
        stroke_ = {at};
        return true;
    case Tool::Polygon:
        // "Double-click to create the final control point, or click the initial control point" (p.400).
        if (!polygon_.isEmpty() && (doubleClick || (polygon_.size() >= 3 && distance(at, polygon_.constFirst().p) <= kHandleReach))) {
            if (doubleClick && distance(at, polygon_.constLast().p) > kHandleReach / 4.0) polygon_ << model::ControlPoint{at, {}, {}, model::JoinType::Straight, false};
            finishPolygon();
            return true;
        }
        polygon_ << model::ControlPoint{at, {}, {}, model::JoinType::Straight, false};
        polygonHandle_ = true; // a drag gives it direction handles (p.401)
        editor_.previewIntraframe(*e);
        return true;
    case Tool::Reshape:
        return reshapePress(at, modifiers);
    case Tool::ZRotation:
        return rotatePress(at);
    case Tool::Selection:
        break;
    }
    // A selection handle scales; "Alt+click ... to rescale the object from its center" (p.404).
    const QList<QPointF> corners = handlePoints();
    for (int i = 0; i < corners.size(); ++i) {
        if (distance(at, corners.at(i)) <= kHandleReach) {
            drag_ = Drag::Scale;
            start_ = e;
            startBounds_ = selectionBounds();
            anchor_ = (modifiers & Qt::AltModifier) ? startBounds_.center() : corners.at((i + 2) % 4);
            from_ = corners.at(i);
            return true;
        }
    }
    const QString hit = objectAt(at);
    if (hit.isEmpty()) {
        // Empty space: a lasso, Shift adding to the selection (p.403).
        if (!(modifiers & Qt::ShiftModifier)) select({});
        drag_ = Drag::Lasso;
        return true;
    }
    if (modifiers & Qt::ShiftModifier) {
        QStringList ids = selected_;
        if (ids.contains(hit)) ids.removeAll(hit);
        else ids << hit;
        select(ids);
        return true;
    }
    // A group is selected as one (p.417): "you can manipulate the group as if it were a single object".
    QStringList withGroup{hit};
    for (const model::IntraframeObject& o : model::intraframeObjects(*e)) {
        if (o.id == hit && !o.group.isEmpty()) {
            for (const model::IntraframeObject& m : model::intraframeObjects(*e)) {
                if (m.group == o.group && !withGroup.contains(m.id)) withGroup << m.id;
            }
        }
    }
    if (!selected_.contains(hit)) select(withGroup);
    // "Click the Reshape tool, or double-click the object" (p.406).
    if (doubleClick) {
        select({hit});
        setTool(Tool::Reshape);
        return true;
    }
    // "Click anywhere within the object's outline, and drag the object to a new location" (p.404).
    drag_ = Drag::Move;
    start_ = e;
    return true;
}

void IntraframeTool::overlayMove(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    to_ = at;
    switch (drag_) {
    case Drag::Shape:
    case Drag::Lasso:
        editor_.refreshIntraframeOverlay();
        return;
    case Drag::Freehand:
        stroke_ << at;
        editor_.refreshIntraframeOverlay();
        return;
    case Drag::Move:
    case Drag::Scale: {
        if (!start_) return;
        model::Effect e = *start_;
        const QList<model::IntraframeObject> objects = model::intraframeObjects(*start_);
        const double a = aspect();
        for (const model::IntraframeObject& o : objects) {
            if (!selected_.contains(o.id) || o.locked) continue;
            const double x = objectNumber(*start_, o, QStringLiteral("X"), 0.0);
            const double y = objectNumber(*start_, o, QStringLiteral("Y"), 0.0);
            if (drag_ == Drag::Move) {
                // Offsets in 1920ths of the width (model/intraframe.h).
                setObjectNumber(e, o.id, QStringLiteral("X"), x + (at.x() - from_.x()) * 1920.0);
                setObjectNumber(e, o.id, QStringLiteral("Y"), y + (at.y() - from_.y()) * 1920.0 / a);
                continue;
            }
            // Scaling about the anchor: the scale grows by the handle's distance from it, and the object moves so that
            // the anchor stays (p.415: "anchored by the selection point that is diagonal to the one you are dragging").
            const double before = distance(from_, anchor_);
            if (before <= 0.0) continue;
            const double f = std::max(0.01, distance(at, anchor_) / before);
            const double scale = objectNumber(*start_, o, QStringLiteral("Scale"), 100.0);
            const model::ObjectPlace place = model::objectPlaceAt(*start_, o, editor_.positionTime(), editor_.span());
            const QPointF centre = place.center.value_or(model::shapeBounds(model::shapeAt(o, editor_.positionTime())).center()) +
                                   QPointF(x / 1920.0, y / 1920.0 * a);
            const QPointF shift = (anchor_ - centre) * (1.0 - f);
            setObjectNumber(e, o.id, QStringLiteral("Scale"), scale * f);
            setObjectNumber(e, o.id, QStringLiteral("X"), x + shift.x() * 1920.0);
            setObjectNumber(e, o.id, QStringLiteral("Y"), y + shift.y() * 1920.0 / a);
        }
        preview(e);
        return;
    }
    case Drag::Points:
    case Drag::Handle:
    case Drag::Rotate:
    case Drag::Center:
        reshapeMove(at);
        return;
    case Drag::None:
        break;
    }
    if (tool_ == Tool::Polygon && !polygon_.isEmpty()) {
        if (polygonHandle_) {
            // Dragging after the click: the new point's direction handles follow the pointer (p.401).
            model::ControlPoint& p = polygon_.last();
            p.out = at - p.p;
            p.in = -p.out;
            p.join = distance(at, p.p) > 0.005 ? model::JoinType::Smooth : model::JoinType::Straight;
        }
        editor_.refreshIntraframeOverlay();
    }
    Q_UNUSED(modifiers)
}

void IntraframeTool::overlayRelease(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    to_ = at;
    const Drag drag = drag_;
    drag_ = Drag::None;
    // The polygon point's handles follow the pointer only while the button is down (p.401).
    polygonHandle_ = false;
    switch (drag) {
    case Drag::Shape: {
        const QRectF r = QRectF(from_, at).normalized();
        editor_.refreshIntraframeOverlay();
        if (r.width() < 0.005 || r.height() < 0.005) return;
        addObject(tool_ == Tool::Oval ? model::IntraframeKind::Oval : model::IntraframeKind::Rectangle,
                  tool_ == Tool::Oval ? model::ovalShape(r) : model::rectangleShape(r), true);
        return;
    }
    case Drag::Freehand: {
        stroke_ << at;
        const QList<QPointF> points = stroke_;
        stroke_.clear();
        editor_.refreshIntraframeOverlay();
        if (tool_ == Tool::Brush) addObject(model::IntraframeKind::Brush, smoothCurve(points, false), false);
        else addObject(model::IntraframeKind::Curve, smoothCurve(points, true), true);
        return;
    }
    case Drag::Lasso: {
        // "Drag a lasso around the objects you want to select" (p.403): the objects inside it.
        const QRectF r = QRectF(from_, at).normalized();
        const auto e = effect();
        QStringList ids = (modifiers & Qt::ShiftModifier) ? selected_ : QStringList{};
        if (e && r.width() > 0.002 && r.height() > 0.002) {
            for (const model::IntraframeObject& o : model::intraframeObjects(*e)) {
                const QRectF b = media::intraframePath(model::placedShapeAt(*e, o, editor_.positionTime(), editor_.span(), aspect()), o.closed, QSizeF(1.0, 1.0))
                                     .boundingRect();
                if (r.contains(b) && !ids.contains(o.id)) ids << o.id;
            }
        }
        select(ids);
        editor_.refreshIntraframeOverlay();
        return;
    }
    case Drag::Move:
    case Drag::Scale:
    case Drag::Points:
    case Drag::Handle:
    case Drag::Rotate:
    case Drag::Center: {
        const std::optional<model::Effect> done = working_;
        start_.reset();
        handlePoint_ = -1;
        QString name = tr("Reshape");
        if (drag == Drag::Move) name = tr("Move");
        else if (drag == Drag::Scale) name = tr("Scale");
        else if (drag == Drag::Rotate || drag == Drag::Center) name = tr("Rotate");
        if (done) commit(name, *done);
        return;
    }
    case Drag::None:
        break;
    }
}

bool IntraframeTool::overlayWantsKey(const QKeyEvent* event) const {
    if (tool_ == Tool::Reshape && !selected_.isEmpty()) {
        // Nudging (p.412) and Ctrl+A for every control point (p.411).
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) return !points_.isEmpty();
        if (event->key() == Qt::Key_A && (event->modifiers() & Qt::ControlModifier)) return true;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) return !selected_.isEmpty();
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) return !polygon_.isEmpty();
    return false;
}

bool IntraframeTool::overlayKey(QKeyEvent* event) {
    if (!overlayWantsKey(event)) return false;
    if (event->key() == Qt::Key_Escape) {
        polygon_.clear();
        editor_.previewIntraframe(std::nullopt);
        return true;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishPolygon();
        return true;
    }
    if (tool_ == Tool::Reshape) {
        // "To nudge selected control points to the left, press the Left Arrow key ... up, press the Alt key ... and the
        // Left Arrow key ... down, ... the Alt key ... and the Right Arrow key" (p.412). One pixel of a frame 1920 wide.
        const bool alt = (event->modifiers() & Qt::AltModifier) != 0;
        const double px = 1.0 / 1920.0;
        if (event->key() == Qt::Key_Left) return nudgePoints(alt ? QPointF(0.0, -px * aspect()) : QPointF(-px, 0.0));
        if (event->key() == Qt::Key_Right) return nudgePoints(alt ? QPointF(0.0, px * aspect()) : QPointF(px, 0.0));
        if (event->key() == Qt::Key_A) {
            if (const auto o = reshaped()) {
                points_.clear();
                for (int i = 0; i < model::shapeAt(*o, editor_.positionTime()).size(); ++i) points_ << i;
                editor_.refreshIntraframeOverlay();
            }
            return true;
        }
        if (!points_.isEmpty()) return removeSelectedPoints();
    }
    return removeSelected();
}

bool IntraframeTool::group() {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.size() < 2) return false;
    model::groupIntraframeObjects(*e, selected_);
    return commit(tr("Group"), *e);
}

bool IntraframeTool::ungroup() {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.isEmpty()) return false;
    const model::Effect before = *e;
    model::ungroupIntraframeObjects(*e, selected_);
    return e->parameters != before.parameters && commit(tr("UnGroup"), *e);
}

bool IntraframeTool::lock(bool locked) {
    auto e = editor_.intraframeEffect();
    if (!e || selected_.isEmpty()) return false;
    const model::Effect before = *e;
    model::lockIntraframeObjects(*e, selected_, locked);
    return e->parameters != before.parameters && commit(locked ? tr("Lock") : tr("Unlock"), *e);
}

void IntraframeTool::setHandlesHidden(bool hidden) {
    handlesHidden_ = hidden;
    editor_.refreshIntraframeOverlay();
}

} // namespace lumenis::ui
