#pragma once

// Drawing and selecting Intraframe objects in the Effect Preview (ROADMAP 6.9b1; FX Guide p.395-404, p.413-417;
// docs/spec/intraframe.md): the Rectangle, Oval, Polygon, Curve and Brush tools and the Selection tool working on the
// Record monitor through Composer::OverlayInput, the selection and its handles, moving, scaling, lassoing, deleting and
// the layer buttons. The Effect Editor owns it and hands it the effect shown; changes go back through the editor as
// previews while a drag goes on and as undo steps when it ends.

#include "lumenis/model/intraframe.h"
#include "lumenis/ui/composer.h"

#include <QObject>
#include <QPointF>
#include <QStringList>

#include <functional>
#include <optional>

namespace lumenis::ui {

class EffectEditor;

class IntraframeTool : public QObject, public Composer::OverlayInput {
    Q_OBJECT

public:
    // "Top to bottom: Rectangle, Oval, Polygon, Curve, and Brush tools" (p.396) and the Selection tool (p.394).
    // ... and (6.9b2) the Z-Rotation and Reshape tools (p.394, p.413-415).
    // ... and (6.9c2) a 24-fps film project's Previsualization Marker tool (p.412).
    enum class Tool { Selection, Rectangle, Oval, Polygon, Curve, Brush, ZRotation, Reshape, Marker };

    explicit IntraframeTool(EffectEditor& editor);

    Tool tool() const { return tool_; }
    // Choosing a tool ends a polygon being drawn (its points so far become the object when there are three).
    void setTool(Tool tool);
    // The selected objects (ids); setting it shows the one selected object's parameters in the editor.
    const QStringList& selection() const { return selected_; }
    void select(const QStringList& ids);
    // The editor's selection after objects went (an undo): kept without telling the editor back.
    void keepSelection(const QStringList& ids) { selected_ = ids; }
    // Forgets the selection and anything being drawn, for another effect.
    void reset();
    // A drag or a polygon is under way (its change not yet an undo step).
    bool busy() const { return drag_ != Drag::None || !polygon_.isEmpty(); }

    // The layer buttons (p.394, p.417) on the selection: Bring Forward and Send Backward, with `all` (Alt+click) to the
    // front or back; Delete (p.403). One undo step each; false when nothing changes.
    bool bringForward(bool all);
    bool sendBackward(bool all);
    bool removeSelected();
    // "Click one of the following buttons: Go to Next Event, Go to Previous Event" (p.403): the next object in layer
    // order.
    bool selectAdjacent(bool next);
    // The Object menu (p.417-418): Group, UnGroup, Lock and Unlock on the selection; one undo step each.
    bool group();
    bool ungroup();
    bool lock(bool locked);

    // Reshape (6.9b2): the selected object's control points selected (indices), and "To drag the control points only,
    // Alt+click ... the Reshape tool to hide the direction handles" (p.414).
    const QList<int>& selectedPoints() const { return points_; }
    void setHandlesHidden(bool hidden);
    bool handlesHidden() const { return handlesHidden_; }

    // The objects' outlines (selected ones and the one being drawn), the selection's handles.
    void fillOverlay(Composer::EffectPreview& preview) const;

    // Composer::OverlayInput (points are fractions of the picture).
    bool overlayPress(const QPointF& at, Qt::KeyboardModifiers modifiers, bool doubleClick) override;
    void overlayMove(const QPointF& at, Qt::KeyboardModifiers modifiers) override;
    void overlayRelease(const QPointF& at, Qt::KeyboardModifiers modifiers) override;
    bool overlayKey(QKeyEvent* event) override;
    bool overlayWantsKey(const QKeyEvent* event) const override;

signals:
    void changed(); // the tool or the selection

private:
    enum class Drag { None, Shape, Freehand, Move, Scale, Lasso, Points, Handle, Rotate, Center };

    // The effect being worked on: a drag's preview, else the shown one.
    std::optional<model::Effect> effect() const;
    double aspect() const;
    // The topmost object under a point; empty when none.
    QString objectAt(const QPointF& at) const;
    // The selection's bounds (placed), in fractions.
    QRectF selectionBounds() const;
    QList<QPointF> handlePoints() const;
    // An object's number in its "Intraframe Object" category set at the position: a keyframe when the parameter has
    // them, else its value.
    void setObjectNumber(model::Effect& e, const QString& id, const QString& name, double value) const;
    double objectNumber(const model::Effect& e, const model::IntraframeObject& o, const QString& name, double fallback) const;
    // Adds an object of `kind` and selects it; the drawing tools then return to Selection, the Brush stays (p.396).
    void addObject(model::IntraframeKind kind, const QList<model::ControlPoint>& points, bool closed);
    void finishPolygon();
    // Reshape and Z-Rotation (intraframe_reshape.cpp).
    std::optional<model::IntraframeObject> reshaped() const; // the one selected object, unlocked
    bool reshapePress(const QPointF& at, Qt::KeyboardModifiers modifiers);
    bool rotatePress(const QPointF& at);
    void reshapeMove(const QPointF& at);
    void fillReshapeOverlay(Composer::EffectPreview& preview) const;
    bool nudgePoints(const QPointF& by);
    bool removeSelectedPoints();
    bool selectAdjacentPoint(bool next);
    // An edit of the selected object's editable shape (model::editableShape), as a preview or an undo step.
    bool editShape(const QString& description, const std::function<void(model::IntraframeObject&, int shape)>& change, bool preview);
    QPointF objectCentre(const model::Effect& e, const model::IntraframeObject& o) const;
    // The change while dragging, and its undo step when it ends.
    void preview(const model::Effect& e);
    bool commit(const QString& description, const model::Effect& e);

    EffectEditor& editor_;
    Tool tool_ = Tool::Selection;
    QStringList selected_;
    Drag drag_ = Drag::None;
    QPointF from_, to_;
    std::optional<model::Effect> start_; // the effect when the drag began
    std::optional<model::Effect> working_;
    QRectF startBounds_;
    QPointF anchor_;
    QList<QPointF> stroke_;
    QList<model::ControlPoint> polygon_;
    bool polygonHandle_ = false; // the last polygon point is being given direction handles (a drag)
    QList<int> points_;          // Reshape: the selected control points
    int handlePoint_ = -1;       // the point whose direction handle is dragged
    bool handleOut_ = false;
    bool handlesHidden_ = false;
    double startRotation_ = 0.0;
};

// Freehand points as a smooth closed curve (the Curve tool, p.403): every few pixels' point kept, with smooth joins
// whose handles follow the neighbours (Catmull-Rom). Fractions of the frame in and out.
QList<model::ControlPoint> smoothCurve(const QList<QPointF>& points, bool closed);

} // namespace lumenis::ui
