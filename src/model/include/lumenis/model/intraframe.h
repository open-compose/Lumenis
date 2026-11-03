#pragma once

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>

// The Intraframe effects' objects (ROADMAP 6.9; FX Guide ch.15, p.391-441; docs/spec/intraframe.md): the Paint
// Effect, AniMatte, Blur Effect, Mosaic Effect and Scratch Removal "draw vector-based objects ... composed of
// mathematically described lines and Bézier curves" (p.391). "You can create multiple paint or matte shapes within a
// single effect. You can also add keyframes and apply effects separately to each of the shapes" (p.391).
//
// Ours: an effect keeps its objects, bottom layer first, under the parameter category "Intraframe" ({"objects":
// [...]}). Each object has its own parameter categories (Mode, Color, Feathering, Magic Mask, Brush, Foreground, the
// effect's Blur/Mosaic/Scratch category) and their keyframes, as an effect's parameters are kept, so objectEffect()
// gives the effect as one object sees it. Where an object is: its shape (control points as fractions of the frame,
// x to the right and y down, keyframed as a whole: "Shape of an object", p.416) and its "Intraframe Object" category
// (X and Y offsets in pixels of a frame 1920 wide, Z rotation in degrees about Center X/Y, Scale in percent: the
// keyframeable "Position", "Z-axis rotation" and "Scale" of p.416).
namespace lumenis::model {

// The effects that draw objects: Image's Paint Effect, Blur Effect, Mosaic Effect and Scratch Removal, and Key's
// AniMatte (p.391, p.468-472).
bool isIntraframeEffect(const Effect& effect);
bool isAniMatte(const Effect& effect);

// The parameter category with an object's place (ours) and the one holding the objects.
inline const QString kIntraframeObjectCategory = QStringLiteral("Intraframe Object");
inline const QString kIntraframeCategory = QStringLiteral("Intraframe");

// "Top to bottom: Rectangle, Oval, Polygon, Curve, and Brush tools" (p.396); a film project's Previsualization Marker
// (p.412).
enum class IntraframeKind { Rectangle, Oval, Polygon, Curve, Brush, Marker };
QString intraframeKindName(IntraframeKind kind);
std::optional<IntraframeKind> intraframeKindFrom(const QString& name);

// "Left to right: straight-edge join, smooth join, and corner join" (p.405). A smooth join's direction bars stay in
// line; a corner join's move independently. `fixedLength` is the Shift+click variant of a smooth join: "the
// direction bar on the opposite side of the control point moves in unison but does not change in length" (p.409).
enum class JoinType { Straight, Smooth, Corner };

// A control point and its direction handles, the handles relative to the point (fractions of the frame).
struct ControlPoint {
    QPointF p;
    QPointF in;  // towards the previous point
    QPointF out; // towards the next point
    JoinType join = JoinType::Straight;
    bool fixedLength = false;

    QJsonObject toJson() const;
    static ControlPoint fromJson(const QJsonObject& o);
    bool operator==(const ControlPoint&) const = default;
};

// The shape at a keyframe, `at` in the effect's time (EffectSpan; Elastic, ours).
struct IntraframeShape {
    double at = 0.0;
    QList<ControlPoint> points;
    bool operator==(const IntraframeShape&) const = default;
};

struct IntraframeObject {
    QString id;
    IntraframeKind kind = IntraframeKind::Rectangle;
    // Closed outlines (the drawing tools' objects) or an open stroke (Brush: the brush head drawn along it).
    bool closed = true;
    QList<IntraframeShape> shapes; // sorted by `at`; at least one
    // Its parameter categories and their "_keyframes", as Effect::parameters.
    QJsonObject parameters;
    // "Object > Group" (p.417): objects with one group id move and scale as one; empty when not grouped. Locked
    // objects cannot be changed (p.417-418).
    QString group;
    bool locked = false;

    QJsonObject toJson() const;
    static IntraframeObject fromJson(const QJsonObject& o);
    bool operator==(const IntraframeObject&) const = default;
};

// The effect's objects, bottom layer first ("Media Composer renders painted objects to the screen starting with the
// bottom layer", p.392), and replacing them.
QList<IntraframeObject> intraframeObjects(const Effect& effect);
void setIntraframeObjects(Effect& effect, const QList<IntraframeObject>& objects);
// A new object of `kind` with `points` (its first shape) and the effect's own parameters as its own (the values the
// Effect Editor shows before anything is selected, ours), added on top with a new id. Its id.
QString addIntraframeObject(Effect& effect, IntraframeKind kind, const QList<ControlPoint>& points);

// The effect as object `object` sees it: its parameters in place of the effect's.
Effect objectEffect(const Effect& effect, const IntraframeObject& object);

// Shapes the drawing tools make (p.400): a rectangle's four straight corners clockwise from the top left; an oval's
// four smooth points (Bézier quarter circles) at its top, right, bottom and left.
QList<ControlPoint> rectangleShape(const QRectF& rect);
QList<ControlPoint> ovalShape(const QRectF& rect);
// A polyline (Brush strokes, freehand curves before smoothing) as straight joins.
QList<ControlPoint> polylineShape(const QList<QPointF>& points);

// The object's control points at effect time `t`: its shapes between keyframes, linearly (ours), held before the
// first and after the last; between shapes of different point counts, the earlier one.
QList<ControlPoint> shapeAt(const IntraframeObject& object, double t);
// Its place at `t` (the "Intraframe Object" category): offsets in pixels of a frame 1920 wide, rotation in degrees,
// scale in percent, and the centre of rotation and scale as fractions of the frame (until set, the shape's bounds'
// centre, p.415).
struct ObjectPlace {
    double x = 0.0, y = 0.0, rotation = 0.0, scale = 100.0;
    std::optional<QPointF> center;
};
ObjectPlace objectPlaceAt(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span);
// The control points at `t` placed (moved, rotated and scaled about the centre), still fractions of the frame, for a
// frame of `aspect` (width over height). The outline they make is media::intraframePath.
QList<ControlPoint> placedShapeAt(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect);
// The bounds of control points (handles left out).
QRectF shapeBounds(const QList<ControlPoint>& points);

// Layers (p.417): one layer forward or back, or to the front or back; false when it cannot move.
enum class IntraframeLayer { Forward, Backward, Front, Back };
bool reorderIntraframeObject(Effect& effect, const QString& id, IntraframeLayer to);
// Object > Group, UnGroup ("If you then click the new group and select UnGroup, all objects become ungrouped
// completely", p.417), Lock and Unlock (p.418) on the objects named (and the groups they are in).
void groupIntraframeObjects(Effect& effect, const QStringList& ids);
void ungroupIntraframeObjects(Effect& effect, const QStringList& ids);
void lockIntraframeObjects(Effect& effect, const QStringList& ids, bool locked);
// Deleting objects ("Press the Delete key", p.403); locked ones stay. The number deleted.
int deleteIntraframeObjects(Effect& effect, const QStringList& ids);

} // namespace lumenis::model
