#pragma once

// The Brush Preview window of the Effect Editor's Brush category (ROADMAP 6.9c1; FX Guide p.397-399, p.493;
// docs/spec/intraframe.md): the brush head at its Size and turned by Spin. Clicking the head shows its control
// points; dragging a point, or a direction handle of a rounded shape's selected point, makes a custom head ("you
// cannot use the modifier key variations", p.398). A brush template dropped on it gives the brush its parameters
// (p.399). The Effect Editor owns it, hands it the Brush category shown and stores what it changes.

#include "lumenis/model/bin.h"
#include "lumenis/model/intraframe.h"

#include <QJsonObject>
#include <QWidget>

class QMimeData;

#include <functional>
#include <optional>

namespace lumenis::ui {

class BrushPreview : public QWidget {
    Q_OBJECT

public:
    explicit BrushPreview(QWidget* parent = nullptr);

    // The Brush category shown (Size, Spin, Shape, Head); a drag under way keeps its own head.
    void setBrush(const QJsonObject& brush);
    // The head shown, in its unit box (model::brushHeadPoints).
    const QList<model::ControlPoint>& head() const { return head_; }
    // The head was clicked: its control points show (and the selected point's direction handles).
    bool editing() const { return editing_; }
    int selectedPoint() const { return selected_; }
    // Resolves a bin item dropped on the window (its bin and item ids), for brush templates from a bin.
    using ItemResolver = std::function<std::optional<model::BinItem>(const QString& bin, const QString& item)>;
    void setItemResolver(ItemResolver resolver) { resolve_ = std::move(resolver); }
    // A drop's brush template: the Brush Template icon's (brushTemplateMime) or a brush template item's.
    std::optional<QJsonObject> droppedBrush(const QMimeData* mime) const;

    // Where a point of the unit box shows in the widget, and back.
    QPointF toWidget(const QPointF& unit) const;
    QPointF fromWidget(const QPointF& point) const;

signals:
    // A drag of a point or handle ended with this head (one undo step for the editor).
    void headChanged(const QList<model::ControlPoint>& head);
    void templateDropped(const QJsonObject& brush);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // The head's width in widget pixels: Size percent of the window (at least 8, so a small head can be clicked).
    double headScale() const;
    QTransform transform() const;

    QJsonObject brush_;
    QList<model::ControlPoint> head_;
    bool editing_ = false;
    int selected_ = -1;
    enum class Drag { None, Point, In, Out } drag_ = Drag::None;
    bool moved_ = false;
    ItemResolver resolve_;
};

// The MIME type of a brush template dragged from the Brush Template icon: {"name": ..., "brush": {...}}.
QString brushTemplateMime();

} // namespace lumenis::ui
