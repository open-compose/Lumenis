#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
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

// ---- Reshaping (6.9b2; p.405-412)
// The inverse of placedShapeAt's mapping for one point or one handle vector at `t`: where in the object's own (unplaced)
// shape a placed point lies.
QPointF unplacedPoint(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect, const QPointF& placed);
QPointF unplacedVector(const Effect& effect, const IntraframeObject& object, double t, const EffectSpan& span, double aspect, const QPointF& placed);
// The shape an edit at `t` changes: the shape keyframe at `t` (within half a frame), the only shape when there is one,
// else a new shape keyframe at `t` holding the shape there ("If you change a control point at a selected keyframe or
// add a keyframe to make changes, the changes are interpolated between the selected keyframe and adjacent keyframes",
// p.411). Its index in `object.shapes`.
int editableShape(IntraframeObject& object, double t, const EffectSpan& span);
// "Click the selected object where you want to add a new control point. A new control point appears and is calculated so
// that the object does not change dramatically in shape. The new control point appears as part of the object for all
// keyframes" (p.410): the segment after control point `after` split at `fraction` along it (de Casteljau for curves), in
// every shape.
void insertControlPoint(IntraframeObject& object, int after, double fraction);
// The segment nearest an unplaced point in the shape at `t` and how far along it (0 to 1); nothing beyond `reach`.
std::optional<std::pair<int, double>> nearestSegment(const IntraframeObject& object, double t, const QPointF& at, double reach);
// "Click the control point, and press the Delete key" (p.411): the points from every shape, keeping at least three of a
// closed outline (two of a stroke). False when they cannot go.
bool removeControlPoints(IntraframeObject& object, QList<int> indices);
// The modifier clicks of p.409-410: Alt+click on a point turns a straight-edge join into a smooth one (handles a third
// of the way towards its neighbours) and back; Alt+click on a handle turns a smooth join into a corner join and back;
// Shift+click on a handle keeps the opposite bar's length (fixedLength) or not. In the shape `shape`.
void toggleSmooth(IntraframeObject& object, int shape, int point, bool closed);
void toggleCorner(IntraframeObject& object, int shape, int point);
void toggleFixedLength(IntraframeObject& object, int shape, int point);
// Dragging a direction handle to `handle` (relative to its point): a smooth join's opposite bar turns with it, and
// changes length in unison unless fixedLength; a corner join's does not move (p.409). `out` names the handle dragged.
void setDirectionHandle(ControlPoint& point, bool out, const QPointF& handle);

// Brush heads (ROADMAP 6.9c1; p.397-399, p.493). The Brush category's "Shape" names one of the Brush Shape buttons
// (ours: Circle, Oval, Ellipse, Square); "Head", when there, is a custom head made in the Brush Preview window: its
// control points in a unit box centred on 0 (x to the right, y down), closed.
inline const QString kBrushCategory = QStringLiteral("Brush");
// The shapes of the Brush Shape buttons, in their order.
QStringList brushShapes();
// A Brush Shape button's head as control points (rounded shapes as smooth Bézier joins, the square as corners).
QList<ControlPoint> brushShapePoints(const QString& shape);
// The head a Brush category gives: its custom "Head", else its Shape's.
QList<ControlPoint> brushHeadPoints(const QJsonObject& brush);
// The Brush category with a custom head; an empty list goes back to the Shape's.
void setBrushHead(QJsonObject& brush, const QList<ControlPoint>& head);
// "click a Brush Shape button": the shape, dropping a custom head (it was based on another shape).
void setBrushShape(QJsonObject& brush, const QString& shape);

// The Previsualization Marker (ROADMAP 6.9c2; p.412-413): "a marker in a segment to represent an effect that will be
// processed at an optical facility", 24-fps film projects only. A Marker object with one control point where it was
// clicked; keyframes on its "Intraframe Object" X and Y move it, its Color category gives its colour.
QString addPrevisualizationMarker(Effect& effect, const QPointF& at);
// "When you create an optical list, each marker, along with its position in the segment on a keyframe-by-keyframe basis,
// appears as an optical event": per marker (in layer order), its colour and its place at each keyframe of its position
// (one event at the start without keyframes). Positions are fractions of the frame; the optical list itself (film cut
// lists, Effect Grid coordinates) comes with the List Tool's film lists.
struct PrevisualizationEvent {
    QString marker;   // the object's id
    int keyframe = 1; // numbered from 1 per marker
    double at = 0.0;  // effect time
    QPointF position;
    QString color;    // "#rrggbb"
    bool operator==(const PrevisualizationEvent&) const = default;
};
QList<PrevisualizationEvent> previsualizationEvents(const Effect& effect, const EffectSpan& span, double aspect);

// Brush templates (p.399): "click the Brush Template icon, and then drag it to a bin. A brush template appears in the
// bin, and the name of the template defaults to Brush." An Effect item holding the Brush category under "brush".
BinItem brushTemplateItem(const QJsonObject& brush, const QString& name, const QDateTime& when);
// The Brush category a brush template holds; nothing for other items.
std::optional<QJsonObject> brushTemplateOf(const BinItem& item);

} // namespace lumenis::model
