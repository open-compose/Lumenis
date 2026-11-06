#pragma once

// The Angle control (FX Guide p.162; ROADMAP 6.11c1): "The Angle control lets you alter an effect's angle or direction
// value by dragging within a graphic representation of a circle. Values can be positive or negative and are not
// limited to a single 360º rotation." It appears in OFX plug-in effects for their angle parameters
// (kOfxParamDoubleTypeAngle; media/ofx_host.h), in the Effect Editor.
//
// Ours: a circle with a line from its centre at the angle, 0° pointing up and positive angles clockwise; dragging
// around the circle turns the value by the angle swept, so whole turns add up; the arrow keys change it by a degree
// (Shift: ten).

#include <QWidget>

namespace lumenis::ui {

class AngleDial : public QWidget {
    Q_OBJECT

public:
    explicit AngleDial(QWidget* parent = nullptr);

    // The angle in degrees, any value.
    double value() const { return value_; }
    // Sets it without emitting valueChanged.
    void setValue(double degrees);
    // Whether a drag is in progress (the value is kept when it ends, as sliders are).
    bool isDragging() const { return dragging_; }

    QSize sizeHint() const override { return {40, 40}; }

signals:
    // While dragging and on each key press.
    void valueChanged(double degrees);
    // When a drag ends, and after a key press: the value to keep.
    void editingFinished(double degrees);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // The pointer's direction from the centre, in degrees (0 up, clockwise).
    double directionOf(const QPointF& point) const;

    double value_ = 0.0;
    bool dragging_ = false;
    double lastDirection_ = 0.0;
};

} // namespace lumenis::ui
