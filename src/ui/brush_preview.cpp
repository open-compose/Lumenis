// The Brush Preview window (brush_preview.h; FX Guide p.397-399).

#include "lumenis/ui/brush_preview.h"

#include "lumenis/media/compositor.h"
#include "lumenis/ui/bin_dock.h"

#include <QDragEnterEvent>
#include <QJsonDocument>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

QString brushTemplateMime() {
    return QStringLiteral("application/x-cutthulhu-brush-template");
}

BrushPreview::BrushPreview(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("brushPreview"));
    setFixedSize(96, 96);
    setAcceptDrops(true);
    setToolTip(tr("Brush Preview"));
    head_ = model::brushHeadPoints({});
}

void BrushPreview::setBrush(const QJsonObject& brush) {
    if (drag_ != Drag::None) return;
    const bool shapeChanged = brush.value(QStringLiteral("Shape")) != brush_.value(QStringLiteral("Shape")) ||
                              brush.value(QStringLiteral("Head")) != brush_.value(QStringLiteral("Head"));
    brush_ = brush;
    head_ = model::brushHeadPoints(brush);
    if (shapeChanged && selected_ >= head_.size()) selected_ = -1;
    update();
}

double BrushPreview::headScale() const {
    // Size is 0 to 100 pixels of a frame 1920 wide (p.493); ours: the window shows 100 as its full width.
    const double size = brush_.value(QStringLiteral("Size")).toDouble(20.0);
    return std::max(8.0, size / 100.0 * (std::min(width(), height()) - 8));
}

QTransform BrushPreview::transform() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    // The renderer turns the head by -Spin in a frame whose y goes down (intraframe.cpp): the same here.
    t.rotate(-brush_.value(QStringLiteral("Spin")).toDouble());
    t.scale(headScale(), headScale());
    return t;
}

QPointF BrushPreview::toWidget(const QPointF& unit) const {
    return transform().map(unit);
}

QPointF BrushPreview::fromWidget(const QPointF& point) const {
    return transform().inverted().map(point);
}

void BrushPreview::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    const QPainterPath head = transform().map(media::intraframePath(head_, true, QSizeF(1.0, 1.0)));
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::WindowText));
    p.drawPath(head);
    if (!editing_) return;
    // The control points, and the selected one's direction bars and handles (p.398, p.405).
    const QColor mark = palette().color(QPalette::Highlight);
    for (qsizetype i = 0; i < head_.size(); ++i) {
        const model::ControlPoint& c = head_.at(i);
        const QPointF at = toWidget(c.p);
        if (i == selected_ && c.join != model::JoinType::Straight) {
            p.setPen(QPen(mark, 1));
            for (const QPointF& h : {c.p + c.in, c.p + c.out}) {
                const QPointF hw = toWidget(h);
                p.drawLine(at, hw);
                p.setBrush(mark);
                p.drawEllipse(hw, 2.5, 2.5);
            }
        }
        p.setPen(QPen(mark, 1));
        p.setBrush(i == selected_ ? mark : palette().color(QPalette::Base));
        p.drawRect(QRectF(at.x() - 2.5, at.y() - 2.5, 5.0, 5.0));
    }
}

void BrushPreview::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
    const QPointF at = event->position();
    moved_ = false;
    auto near = [&](const QPointF& unit) { return QLineF(toWidget(unit), at).length() <= 5.0; };
    if (editing_) {
        // A handle of the selected point first, then a point.
        if (selected_ >= 0 && selected_ < head_.size() && head_.at(selected_).join != model::JoinType::Straight) {
            const model::ControlPoint& c = head_.at(selected_);
            if (near(c.p + c.out)) { drag_ = Drag::Out; return; }
            if (near(c.p + c.in)) { drag_ = Drag::In; return; }
        }
        for (qsizetype i = 0; i < head_.size(); ++i) {
            if (!near(head_.at(i).p)) continue;
            selected_ = static_cast<int>(i);
            drag_ = Drag::Point;
            update();
            return;
        }
    }
    // "In the Brush Preview window, click the brush head. Control points appear on the brush head" (p.398).
    const QPainterPath head = transform().map(media::intraframePath(head_, true, QSizeF(1.0, 1.0)));
    editing_ = head.contains(at) || (editing_ && head.boundingRect().adjusted(-6, -6, 6, 6).contains(at));
    if (!editing_) selected_ = -1;
    update();
}

void BrushPreview::mouseMoveEvent(QMouseEvent* event) {
    if (drag_ == Drag::None || selected_ < 0 || selected_ >= head_.size()) return;
    moved_ = true;
    const QPointF unit = fromWidget(event->position());
    model::ControlPoint& c = head_[selected_];
    if (drag_ == Drag::Point) c.p = unit;
    else model::setDirectionHandle(c, drag_ == Drag::Out, unit - c.p);
    update();
}

void BrushPreview::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(event);
    const bool changed = drag_ != Drag::None && moved_;
    drag_ = Drag::None;
    if (changed) emit headChanged(head_);
}

std::optional<QJsonObject> BrushPreview::droppedBrush(const QMimeData* mime) const {
    if (!mime) return std::nullopt;
    if (mime->hasFormat(brushTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(mime->data(brushTemplateMime())).object();
        if (o.value(QStringLiteral("brush")).isObject()) return o.value(QStringLiteral("brush")).toObject();
        return std::nullopt;
    }
    if (!mime->hasFormat(binItemsMime()) || !resolve_) return std::nullopt;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() != 2) return std::nullopt;
    const auto item = resolve_(lines.at(0), lines.at(1));
    return item ? model::brushTemplateOf(*item) : std::nullopt;
}

void BrushPreview::dragEnterEvent(QDragEnterEvent* event) {
    if (droppedBrush(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void BrushPreview::dragMoveEvent(QDragMoveEvent* event) {
    if (droppedBrush(event->mimeData())) event->acceptProposedAction();
    else event->ignore();
}

void BrushPreview::dropEvent(QDropEvent* event) {
    // "Click the template in the bin, drag it to the Brush Preview window, and release the mouse button" (p.399).
    if (const auto brush = droppedBrush(event->mimeData())) {
        event->acceptProposedAction();
        emit templateDropped(*brush);
    }
}

} // namespace lumenis::ui
