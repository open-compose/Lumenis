#pragma once

#include "lumenis/model/mask_margins.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

// The Target Settings dialog (Editing Guide p.550-551; 8.5e3): the Format tab's Mask Margins button. "Select one of the
// mask presets or set the margins manually by selecting the appropriate percentage of the image to be occluded" (p.551):
// a "Mask margins preset" menu (model::maskPresets) and top, bottom, left and right percentages, with OK, Cancel and
// Apply as the guide's picture shows.
namespace lumenis::ui {

class TargetSettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `margins` as they are; `pictureRatio` the project's picture width over height, for the presets.
    TargetSettingsDialog(const model::MaskMargins& margins, double pictureRatio, QWidget* parent = nullptr);

    model::MaskMargins margins() const;
    // Chooses a preset as the menu does: its margins fill the boxes; Custom... keeps them.
    void choosePreset(const QString& preset);

signals:
    // Apply was clicked (OK also accepts the dialog).
    void applied(const lumenis::model::MaskMargins& margins);

private:
    void fill(const model::MaskMargins& margins);

    double ratio_ = 16.0 / 9.0;
    bool filling_ = false;
    QComboBox* preset_ = nullptr;
    QDoubleSpinBox* top_ = nullptr;
    QDoubleSpinBox* bottom_ = nullptr;
    QDoubleSpinBox* left_ = nullptr;
    QDoubleSpinBox* right_ = nullptr;
};

} // namespace lumenis::ui
