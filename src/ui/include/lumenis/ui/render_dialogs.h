#pragma once

#include "lumenis/model/effect_render.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;

namespace lumenis::ui {

// The Render Effect dialog box (FX Guide p.58-59; docs/spec/rendering.md): "Click the Drive button, and
// select a drive for the rendered media" and "In the Processing field, select the processing method";
// "Greyed-out options are not available". Ours: the drives are the media drives (the one given first
// chosen), the count of effects to render shown above; Application is the only processing available
// until background rendering (ROADMAP 6.4d2), Background Process and Distributed Processing are dimmed.
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

// The Expert Render dialog box (FX Guide p.142-145): it "indicates how many of the effects require
// rendering"; after a playback attempt (red indicators in TC1) it offers "Render recommended ranges",
// "Render entire selection" and "Prepare effects for Digital Cut", and its message changes with the
// choice; the Drive and Processing fields; Cancel, Modify Selection and OK. Ours: the three choices
// are offered always, recommended ranges only when there are red indicators; `count` gives the number
// of effects for a choice.
class ExpertRenderDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice { Cancel, Modify, Ok };
    ExpertRenderDialog(const std::function<int(model::ExpertMode)>& count, bool recommended, const QString& drive,
                       QWidget* parent = nullptr);

    model::ExpertMode mode() const;
    void setMode(model::ExpertMode mode);
    QString drive() const;
    Choice choice() const { return choice_; }
    void choose(Choice c);
    QLabel* message() const { return message_; }

private:
    // Shows the count for the chosen option.
    void showCount();

    std::function<int(model::ExpertMode)> count_;
    bool hasRecommended_ = false;
    QLabel* message_ = nullptr;
    QRadioButton* recommended_ = nullptr;
    QRadioButton* entire_ = nullptr;
    QRadioButton* digitalCut_ = nullptr;
    QComboBox* drive_ = nullptr;
    Choice choice_ = Choice::Cancel;
};

} // namespace lumenis::ui
