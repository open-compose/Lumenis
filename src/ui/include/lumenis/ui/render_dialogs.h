#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

namespace lumenis::ui {

// The Render Effect dialog box (FX Guide p.58-59; docs/spec/rendering.md): "Click the Drive button, and
// select a drive for the rendered media" and "In the Processing field, select the processing method";
// "Greyed-out options are not available". Ours: the drives are the media drives (the one given first
// chosen), the count of effects to render shown above; Application is the only processing available
// until background rendering (ROADMAP 6.4d), Background Process and Distributed Processing are dimmed.
class RenderEffectDialog : public QDialog {
    Q_OBJECT

public:
    RenderEffectDialog(int effects, const QString& drive, QWidget* parent = nullptr);

    // The chosen drive's root.
    QString drive() const;
    QComboBox* driveMenu() const { return drive_; }
    QComboBox* processingMenu() const { return processing_; }

private:
    QComboBox* drive_ = nullptr;
    QComboBox* processing_ = nullptr;
};

// The Clear Renders dialog box (p.60-61): Skip Motion Effects "Retains all motion effects" and Skip
// Unknown Effects "Retains all effects with blank effect icons", both selected until changed (p.152
// has them deselected to purge every render).
class ClearRendersDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClearRendersDialog(QWidget* parent = nullptr);

    bool skipMotionEffects() const;
    bool skipUnknownEffects() const;
    QCheckBox* motionBox() const { return motion_; }
    QCheckBox* unknownBox() const { return unknown_; }

private:
    QCheckBox* motion_ = nullptr;
    QCheckBox* unknown_ = nullptr;
};

} // namespace lumenis::ui
