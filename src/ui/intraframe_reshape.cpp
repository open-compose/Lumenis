// The Intraframe Reshape and Z-Rotation tools (intraframe_tool.h; ROADMAP 6.9b2; FX Guide p.405-415): the selected
// object's control points and direction handles in the Effect Preview, with the modifier clicks of p.409-410, adding and
// removing control points, nudging and stepping through them; turning an object about its centre and moving the centre.
#include "lumenis/ui/intraframe_tool.h"

#include "lumenis/media/compositor.h"
#include "lumenis/ui/effect_editor.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::ui {

namespace {

constexpr double kReach = 0.015; // how near a point or handle a press must be (fractions of the width, ours)
constexpr double kRotateArm = 0.08; // the Z-Rotation handle's distance from the centre (ours)

double distance(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

std::optional<model::IntraframeObject> IntraframeTool::reshaped() const {
    const auto e = effect();
    if (!e || selected_.size() != 1) return std::nullopt;
    for (const model::IntraframeObject& o : model::intraframeObjects(*e)) {
        if (o.id == selected_.constFirst() && !o.locked) return o;
    }
    return std::nullopt;
}

QPointF IntraframeTool::objectCentre(const model::Effect& e, const model::IntraframeObject& o) const {
    // The centre of rotation and scale, placed: its Center X/Y, else the shape's bounds' centre, moved by X and Y.
    const double t = editor_.positionTime();
    const model::ObjectPlace place = model::objectPlaceAt(e, o, t, editor_.span());
    const QPointF centre = place.center.value_or(model::shapeBounds(model::shapeAt(o, t)).center());
    return centre + QPointF(place.x / 1920.0, place.y / 1920.0 * aspect());
}

bool IntraframeTool::editShape(const QString& description, const std::function<void(model::IntraframeObject&, int)>& change, bool preview) {
    std::optional<model::Effect> base = preview && start_ ? start_ : editor_.intraframeEffect();
    if (!base || selected_.size() != 1) return false;
    QList<model::IntraframeObject> objects = model::intraframeObjects(*base);
    for (model::IntraframeObject& o : objects) {
        if (o.id != selected_.constFirst() || o.locked) continue;
        const int shape = model::editableShape(o, editor_.positionTime(), editor_.span());
        change(o, shape);
        model::Effect e = *base;
        model::setIntraframeObjects(e, objects);
        if (preview) {
            this->preview(e);
            return true;
        }
        return commit(description, e);
    }
    return false;
}

bool IntraframeTool::reshapePress(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    const auto e = editor_.intraframeEffect();
    if (!e) return false;
    const auto o = reshaped();
    if (!o) {
        // Nothing to reshape yet: a click selects the object under it.
        const QString hit = objectAt(at);
        select(hit.isEmpty() ? QStringList{} : QStringList{hit});
        return true;
    }
    const double t = editor_.positionTime();
    const QList<model::ControlPoint> placed = model::placedShapeAt(*e, *o, t, editor_.span(), aspect());
    const bool alt = (modifiers & Qt::AltModifier) != 0;
    const bool shift = (modifiers & Qt::ShiftModifier) != 0;
    // A direction handle of a selected point first.
    if (!handlesHidden_) {
        for (int i : std::as_const(points_)) {
            if (i < 0 || i >= placed.size() || placed.at(i).join == model::JoinType::Straight) continue;
            for (bool out : {false, true}) {
                const QPointF h = placed.at(i).p + (out ? placed.at(i).out : placed.at(i).in);
                if (distance(at, h) > kReach) continue;
                if (alt) {
                    // "Transform a smooth join ... into a corner join ... Alt+click ... one of the direction handles" (p.410).
                    return editShape(tr("Reshape"), [&](model::IntraframeObject& obj, int s) { model::toggleCorner(obj, s, i); }, false);
                }
                if (shift) {
                    // "Shift+click a direction handle ... To change it back, Shift+click the direction handle" (p.409).
                    return editShape(tr("Reshape"), [&](model::IntraframeObject& obj, int s) { model::toggleFixedLength(obj, s, i); }, false);
                }
                drag_ = Drag::Handle;
                handlePoint_ = i;
                handleOut_ = out;
                start_ = e;
                return true;
            }
        }
    }
    for (int i = 0; i < placed.size(); ++i) {
        if (distance(at, placed.at(i).p) > kReach) continue;
        if (alt) {
            // "Transform a straight-edge join ... into a smooth join ... Alt+click ... the control point" (p.409).
            const bool closed = o->closed;
            const bool done = editShape(tr("Reshape"), [&](model::IntraframeObject& obj, int s) { model::toggleSmooth(obj, s, i, closed); }, false);
            points_ = {i};
            return done;
        }
        if (shift) {
            // "Shift+click or lasso additional control points" (p.411).
            if (points_.contains(i)) points_.removeAll(i);
            else points_ << i;
            editor_.refreshIntraframeOverlay();
            return true;
        }
        if (!points_.contains(i)) points_ = {i};
        // "Drag the control point to its new location. The control point's direction bars do not change orientation"
        // (p.411); every selected point moves in unison.
        drag_ = Drag::Points;
        start_ = e;
        editor_.refreshIntraframeOverlay();
        return true;
    }
    // On the outline between points: "Click the selected object where you want to add a new control point" (p.410).
    const QPointF own = model::unplacedPoint(*e, *o, t, editor_.span(), aspect(), at);
    if (const auto segment = model::nearestSegment(*o, t, own, kReach)) {
        auto obj = *o;
        model::insertControlPoint(obj, segment->first, segment->second);
        QList<model::IntraframeObject> objects = model::intraframeObjects(*e);
        for (model::IntraframeObject& each : objects) {
            if (each.id == obj.id) each = obj;
        }
        model::Effect changed = *e;
        model::setIntraframeObjects(changed, objects);
        points_ = {segment->first + 1};
        return commit(tr("Add Control Point"), changed);
    }
    // Elsewhere: another object, or nothing ("click any unselected control point" deselects the group, p.411).
    const QString hit = objectAt(at);
    if (!hit.isEmpty() && hit != o->id) {
        select({hit});
        return true;
    }
    points_.clear();
    editor_.refreshIntraframeOverlay();
    return true;
}

bool IntraframeTool::rotatePress(const QPointF& at) {
    const auto e = editor_.intraframeEffect();
    if (!e) return false;
    const auto o = reshaped();
    if (o) {
        const QPointF centre = objectCentre(*e, *o);
        const double r = objectNumber(*e, *o, QStringLiteral("Rotation"), 0.0) * std::numbers::pi / 180.0;
        const QPointF arm = centre + QPointF(kRotateArm * std::cos(r), kRotateArm * std::sin(r) * aspect());
        // "You can click the + and drag it anywhere on the screen to change the center of the object's rotation" (p.415).
        if (distance(at, centre) <= kReach) {
            drag_ = Drag::Center;
            start_ = e;
            return true;
        }
        // "Click the rotation handle and drag the mouse to rotate the object" (p.415).
        if (distance(at, arm) <= kReach) {
            drag_ = Drag::Rotate;
            start_ = e;
            startRotation_ = objectNumber(*e, *o, QStringLiteral("Rotation"), 0.0);
            return true;
        }
    }
    // "Click the object you want to rotate" (p.414).
    const QString hit = objectAt(at);
    select(hit.isEmpty() ? QStringList{} : QStringList{hit});
    return true;
}

void IntraframeTool::reshapeMove(const QPointF& at) {
    if (!start_ || selected_.size() != 1) return;
    const double t = editor_.positionTime();
    model::IntraframeObject o;
    bool found = false;
    for (const model::IntraframeObject& each : model::intraframeObjects(*start_)) {
        if (each.id == selected_.constFirst()) {
            o = each;
            found = true;
        }
    }
    if (!found) return;
    switch (drag_) {
    case Drag::Points: {
        const QPointF by = model::unplacedVector(*start_, o, t, editor_.span(), aspect(), at - from_);
        const QList<int> chosen = points_;
        editShape(tr("Reshape"), [&](model::IntraframeObject& obj, int s) {
            for (int i : chosen) {
                if (i >= 0 && i < obj.shapes.at(s).points.size()) obj.shapes[s].points[i].p += by;
            }
        }, true);
        return;
    }
    case Drag::Handle: {
        const int i = handlePoint_;
        const bool out = handleOut_;
        editShape(tr("Reshape"), [&](model::IntraframeObject& obj, int s) {
            if (i < 0 || i >= obj.shapes.at(s).points.size()) return;
            model::ControlPoint& c = obj.shapes[s].points[i];
            const QPointF handle = model::unplacedPoint(*start_, o, t, editor_.span(), aspect(), at) - c.p;
            model::setDirectionHandle(c, out, handle);
        }, true);
        return;
    }
    case Drag::Rotate: {
        // The angle turned about the centre, in pixels (y in widths).
        const QPointF centre = objectCentre(*start_, o);
        const double a = aspect();
        const double before = std::atan2((from_.y() - centre.y()) / a, from_.x() - centre.x());
        const double now = std::atan2((at.y() - centre.y()) / a, at.x() - centre.x());
        model::Effect e = *start_;
        setObjectNumber(e, o.id, QStringLiteral("Rotation"), startRotation_ + (now - before) * 180.0 / std::numbers::pi);
        preview(e);
        return;
    }
    case Drag::Center: {
        // The centre in the object's own frame: the point dragged to, without X and Y.
        const model::ObjectPlace place = model::objectPlaceAt(*start_, o, t, editor_.span());
        const QPointF own = at - QPointF(place.x / 1920.0, place.y / 1920.0 * aspect());
        model::Effect e = *start_;
        setObjectNumber(e, o.id, QStringLiteral("Center X"), own.x());
        setObjectNumber(e, o.id, QStringLiteral("Center Y"), own.y());
        preview(e);
        return;
    }
    default:
        return;
    }
}

void IntraframeTool::fillReshapeOverlay(Composer::EffectPreview& preview) const {
    const auto e = effect();
    const auto o = reshaped();
    if (!e || !o) return;
    const double t = editor_.positionTime();
    if (tool_ == Tool::ZRotation) {
        // "Center of rotation (+) and Z-Rotation handle" (p.415).
        const QPointF centre = objectCentre(*e, *o);
        const double r = objectNumber(*e, *o, QStringLiteral("Rotation"), 0.0) * std::numbers::pi / 180.0;
        preview.anchors << centre;
        preview.handles << Composer::EffectPreview::Handle{QStringLiteral("intraframe:rotate"),
                                                           centre + QPointF(kRotateArm * std::cos(r), kRotateArm * std::sin(r) * aspect()), centre};
        return;
    }
    // "The outline of the object becomes highlighted, and control points appear" (p.414); a selected point's direction
    // bars and handles (p.405).
    const QList<model::ControlPoint> placed = model::placedShapeAt(*e, *o, t, editor_.span(), aspect());
    for (int i = 0; i < placed.size(); ++i) {
        preview.handles << Composer::EffectPreview::Handle{QStringLiteral("intraframe:point:%1").arg(i), placed.at(i).p, std::nullopt};
        if (handlesHidden_ || !points_.contains(i) || placed.at(i).join == model::JoinType::Straight) continue;
        preview.handles << Composer::EffectPreview::Handle{QStringLiteral("intraframe:in:%1").arg(i), placed.at(i).p + placed.at(i).in, placed.at(i).p};
        preview.handles << Composer::EffectPreview::Handle{QStringLiteral("intraframe:out:%1").arg(i), placed.at(i).p + placed.at(i).out, placed.at(i).p};
    }
}

bool IntraframeTool::nudgePoints(const QPointF& by) {
    const auto e = editor_.intraframeEffect();
    const auto o = reshaped();
    if (!e || !o || points_.isEmpty()) return false;
    const QPointF own = model::unplacedVector(*e, *o, editor_.positionTime(), editor_.span(), aspect(), by);
    const QList<int> chosen = points_;
    return editShape(tr("Nudge"), [&](model::IntraframeObject& obj, int s) {
        for (int i : chosen) {
            if (i >= 0 && i < obj.shapes.at(s).points.size()) obj.shapes[s].points[i].p += own;
        }
    }, false);
}

bool IntraframeTool::removeSelectedPoints() {
    if (points_.isEmpty()) return false;
    const QList<int> chosen = points_;
    bool removed = false;
    const bool done = editShape(tr("Delete Control Point"), [&](model::IntraframeObject& obj, int) { removed = model::removeControlPoints(obj, chosen); }, false);
    if (done && removed) points_.clear();
    return done && removed;
}

bool IntraframeTool::selectAdjacentPoint(bool next) {
    const auto o = reshaped();
    if (!o || points_.isEmpty()) return false;
    const auto n = static_cast<int>(model::shapeAt(*o, editor_.positionTime()).size());
    if (n == 0) return false;
    const int i = points_.constLast();
    points_ = {next ? (i + 1) % n : (i + n - 1) % n};
    editor_.refreshIntraframeOverlay();
    return true;
}

} // namespace lumenis::ui
