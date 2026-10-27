// The Apply Clip Gain and Auto VO dialogs (clip_audio_dialogs.h).
#include "lumenis/ui/clip_audio_dialogs.h"

#include "lumenis/model/clip_audio.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

QDialogButtonBox* okCancel(QDialog* dialog) {
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

} // namespace

ApplyGainDialog::ApplyGainDialog(double current, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("applyClipGain"));
    setWindowTitle(tr("Apply Clip Gain"));
    box_ = new QDoubleSpinBox(this);
    box_->setObjectName(QStringLiteral("gain"));
    box_->setRange(model::kClipGainMin, model::kClipGainMax);
    box_->setDecimals(1);
    box_->setSingleStep(1.0);
    box_->setSuffix(tr(" dB"));
    box_->setValue(current);
    auto* form = new QFormLayout;
    form->addRow(tr("Gain:"), box_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
}

double ApplyGainDialog::gain() const {
    return box_->value();
}

AutoVoDialog::AutoVoDialog(const QStringList& audioTracks, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("autoVo"));
    setWindowTitle(tr("Auto VO"));
    track_ = new QComboBox(this);
    track_->setObjectName(QStringLiteral("usingTrack"));
    track_->addItems(audioTracks);
    extract_ = new QRadioButton(tr("Extract segments"), this);
    keep_ = new QRadioButton(tr("Keep segments"), this);
    extract_->setChecked(true);
    auto* form = new QFormLayout;
    form->addRow(tr("Using track:"), track_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(extract_);
    layout->addWidget(keep_);
    layout->addWidget(okCancel(this));
}

QString AutoVoDialog::track() const {
    return track_->currentText();
}

bool AutoVoDialog::keep() const {
    return keep_->isChecked();
}

} // namespace lumenis::ui
