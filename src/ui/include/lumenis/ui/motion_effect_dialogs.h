#pragma once

#include "lumenis/core/rational.h"

#include <QDialog>

#include <cstdint>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace lumenis::ui {

// Composer > Freeze Frame > Other (FX Guide p.166-167, p.561-562): "type a custom duration in the
// dialog box, and then click OK". Ours: the duration as timecode at the clip's rate (or a number of
// frames), two seconds until changed. The Two Field Freeze Frames choices are not offered: our projects
// are progressive ("There are no two-field rendering options ... in a progressive project", p.174).
class FreezeFrameDialog : public QDialog {
    Q_OBJECT

public:
    FreezeFrameDialog(const Rational& rate, std::int64_t duration, QWidget* parent = nullptr);
    // The duration in frames; nothing when the text is not a duration.
    std::optional<std::int64_t> duration() const;
    QLineEdit* durationField() const { return duration_; }

private:
    Rational rate_;
    QLineEdit* duration_ = nullptr;
};

// The Motion Effect dialog box (FX Guide p.168-171, p.562-563): Variable Speed by "Duration n Frames",
// "Rate n FPS", "% Speed" or "Fit To Fill", which change together; Strobe Motion's "Update every n
// frames"; the Target Drive; Create, Create and Render, or Cancel. `length` is the source frames the
// effect is made from (the Source monitor's marks, or the whole clip); `fitLength` the Record monitor's
// marked frames, when there are both marks (Fit To Fill is dimmed without them). As for Freeze Frame,
// no two-field options in our progressive projects.
class MotionEffectDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice { Cancel, Create, CreateAndRender };
    MotionEffectDialog(std::int64_t length, const Rational& rate, std::optional<std::int64_t> fitLength, const QString& drive,
                       QWidget* parent = nullptr);

    // Source frames per frame (1 when Variable Speed is off); negative for reverse.
    double speed() const;
    // The strobe's update rate, 1 when Strobe Motion is off.
    int strobe() const;
    QString drive() const;
    Choice choice() const { return choice_; }
    void choose(Choice c);

    QCheckBox* variableSpeedBox() const { return variable_; }
    QSpinBox* framesField() const { return frames_; }
    QDoubleSpinBox* fpsField() const { return fps_; }
    QDoubleSpinBox* percentField() const { return percent_; }
    QCheckBox* fitToFillBox() const { return fit_; }
    QCheckBox* strobeBox() const { return strobe_; }
    QSpinBox* strobeField() const { return strobeFrames_; }

private:
    // Sets the speed's three fields from `speed`, but not the one being typed in (`from`).
    void showSpeed(double speed, const QObject* from);

    std::int64_t length_;
    Rational rate_;
    std::optional<std::int64_t> fitLength_;
    double speed_ = 1.0;
    bool updating_ = false;
    QCheckBox* variable_ = nullptr;
    QSpinBox* frames_ = nullptr;
    QDoubleSpinBox* fps_ = nullptr;
    QDoubleSpinBox* percent_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QCheckBox* strobe_ = nullptr;
    QSpinBox* strobeFrames_ = nullptr;
    QComboBox* drive_ = nullptr;
    Choice choice_ = Choice::Cancel;
};

} // namespace lumenis::ui
