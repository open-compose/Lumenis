// The Angle control (angle_dial.h): a circle dragged around to turn an angle without limit (FX Guide p.162).

#include "lumenis/ui/angle_dial.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace lumenis::ui {

AngleDial::AngleDial(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(28, 28);
}

void AngleDial::setValue(double degrees) {
    if (value_ == degrees) return;
    value_ = degrees;
    update();
}

double AngleDial::directionOf(const QPointF& point) const {
    const QPointF c = QRectF(rect()).center();
    // atan2 of (x, -y): 0 straight up, increasing clockwise on screen.
    return std::atan2(point.x() - c.x(), c.y() - point.y()) * 180.0 / std::numbers::pi;
}

void AngleDial::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double side = std::min(width(), height()) - 4.0;
    const QRectF circle(QRectF(rect()).center() - QPointF(side / 2, side / 2), QSizeF(side, side));
    // Palette colours, so the text-free screenshots stay alike (CLAUDE.md).
    p.setPen(QPen(palette().color(QPalette::WindowText), 1));
    p.setBrush(palette().color(hasFocus() ? QPalette::Highlight : QPalette::Button));
    p.drawEllipse(circle);
    const double a = value_ * std::numbers::pi / 180.0;
    const QPointF c = circle.center();
    const QPointF tip(c.x() + std::sin(a) * side / 2, c.y() - std::cos(a) * side / 2);
    p.setPen(QPen(palette().color(QPalette::BrightText), 2));
    p.drawLine(c, tip);
}

void AngleDial::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
    dragging_ = true;
    lastDirection_ = directionOf(event->position());
}

void AngleDial::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return QWidget::mouseMoveEvent(event);
    const double direction = directionOf(event->position());
    // The angle swept since the last move, the short way round: whole turns add up.
    double delta = direction - lastDirection_;
    while (delta > 180.0) delta -= 360.0;
    while (delta < -180.0) delta += 360.0;
    lastDirection_ = direction;
    if (delta == 0.0) return;
    value_ += delta;
    update();
    emit valueChanged(value_);
}

void AngleDial::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(event);
    dragging_ = false;
    emit editingFinished(value_);
}

void AngleDial::keyPressEvent(QKeyEvent* event) {
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    double change = 0.0;
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Right) change = step;
    else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Left) change = -step;
    if (change == 0.0) return QWidget::keyPressEvent(event);
    value_ += change;
    update();
    emit valueChanged(value_);
    emit editingFinished(value_);
}

} // namespace lumenis::ui
