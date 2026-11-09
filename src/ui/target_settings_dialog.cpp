// The Target Settings dialog (8.5e3); see the header. Laid out as the guide's picture on p.551: a Mask Margins group with
// the preset menu over the four percentages, then OK, Cancel and Apply.
#include "lumenis/ui/target_settings_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace lumenis::ui {

TargetSettingsDialog::TargetSettingsDialog(const model::MaskMargins& margins, double pictureRatio, QWidget* parent)
    : QDialog(parent), ratio_(pictureRatio > 0.0 ? pictureRatio : 16.0 / 9.0) {
    setObjectName(QStringLiteral("targetSettings"));
    setWindowTitle(tr("Target Settings"));
    auto* group = new QGroupBox(tr("Mask Margins"));
    auto* form = new QVBoxLayout(group);
    form->addWidget(new QLabel(tr("Mask margins preset")));
    preset_ = new QComboBox;
    preset_->setObjectName(QStringLiteral("maskPreset"));
    preset_->addItems(model::maskPresets());
    form->addWidget(preset_);
    auto* boxes = new QFormLayout;
    auto box = [&](const char* name, const QString& label) {
        auto* spin = new QDoubleSpinBox;
        spin->setObjectName(QString::fromLatin1(name));
        spin->setRange(0.0, 100.0);
        spin->setDecimals(2);
        spin->setSuffix(QStringLiteral(" %"));
        boxes->addRow(label, spin);
        // A value typed by hand makes the margins Custom (ours).
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!filling_) preset_->setCurrentText(model::customMaskPreset());
        });
        return spin;
    };
    top_ = box("maskTop", tr("top"));
    bottom_ = box("maskBottom", tr("bottom"));
    left_ = box("maskLeft", tr("left"));
    right_ = box("maskRight", tr("right"));
    form->addLayout(boxes);
    connect(preset_, &QComboBox::activated, this, [this](int index) { choosePreset(preset_->itemText(index)); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { emit applied(this->margins()); });
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(buttons);

    // Without margins the menu shows the picture's own shape when it is a preset (the picture on p.551 shows 16:9 with
    // no margins), else Custom... (ours).
    model::MaskMargins shown = margins;
    if (shown.preset.isEmpty()) {
        shown.preset = model::customMaskPreset();
        for (const QString& p : model::maskPresets()) {
            const auto r = model::maskPresetRatio(p);
            if (r && std::abs(*r - ratio_) < 0.005) shown.preset = p;
        }
    }
    fill(shown);
}

model::MaskMargins TargetSettingsDialog::margins() const {
    model::MaskMargins m;
    m.preset = preset_->currentText();
    m.top = top_->value();
    m.bottom = bottom_->value();
    m.left = left_->value();
    m.right = right_->value();
    return m;
}

void TargetSettingsDialog::choosePreset(const QString& preset) {
    if (preset == model::customMaskPreset()) {
        model::MaskMargins m = margins();
        m.preset = preset;
        fill(m);
        return;
    }
    fill(model::maskForPreset(preset, ratio_));
}

void TargetSettingsDialog::fill(const model::MaskMargins& margins) {
    filling_ = true;
    preset_->setCurrentText(margins.preset.isEmpty() ? model::customMaskPreset() : margins.preset);
    top_->setValue(margins.top);
    bottom_->setValue(margins.bottom);
    left_->setValue(margins.left);
    right_->setValue(margins.right);
    filling_ = false;
}

} // namespace lumenis::ui
