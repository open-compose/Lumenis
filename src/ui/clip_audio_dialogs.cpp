// The Apply Clip Gain, Auto VO, Read Audio Timecode and Change Sample Rate dialogs (clip_audio_dialogs.h).
#include "lumenis/ui/clip_audio_dialogs.h"
#include "lumenis/model/universal_mastering.h"

#include "lumenis/model/clip_audio.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

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

ReadAudioTimecodeDialog::ReadAudioTimecodeDialog(int audioTracks, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("readAudioTimecode"));
    setWindowTitle(tr("Read Audio Timecode"));
    userBits_ = new QCheckBox(tr("User Bit Timecode"), this);
    source_ = new QComboBox(this);
    source_->setObjectName(QStringLiteral("audioTimecodeSource"));
    for (int t = 1; t <= std::max(audioTracks, 1); ++t) source_->addItem(QStringLiteral("A%1").arg(t));
    destination_ = new QComboBox(this);
    destination_->setObjectName(QStringLiteral("destinationTrack"));
    for (int i = 1; i <= 5; ++i) destination_->addItem(QStringLiteral("Auxiliary TC%1").arg(i));
    fill_ = new QCheckBox(tr("Fill Undecodeable Frames"), this);
    fill_->setChecked(true);
    auto* form = new QFormLayout;
    form->addRow(userBits_);
    form->addRow(tr("Audio Timecode Source:"), source_);
    form->addRow(tr("Destination Track:"), destination_);
    form->addRow(fill_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
}

bool ReadAudioTimecodeDialog::userBits() const {
    return userBits_->isChecked();
}

int ReadAudioTimecodeDialog::sourceTrack() const {
    return source_->currentIndex();
}

QString ReadAudioTimecodeDialog::destination() const {
    return destination_->currentText();
}

bool ReadAudioTimecodeDialog::fill() const {
    return fill_->isChecked();
}

ChangeSampleRateDialog::ChangeSampleRateDialog(const QList<std::pair<QString, QString>>& drives, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("changeSampleRate"));
    setWindowTitle(tr("Change Sample Rate"));
    rate_ = new QComboBox(this);
    rate_->setObjectName(QStringLiteral("sampleRate"));
    rate_->addItem(tr("32 kHz"), 32000);
    rate_->addItem(tr("44.1 kHz"), 44100);
    rate_->addItem(tr("48 kHz"), 48000);
    // HD Universal Mastering (p.1297-1298; 8.7c2a): "Click the Sample Rate menu, and select the appropriate conversion
    // option", each at its remastered rate (written as WAV beside the MXF media, as the other rates off 48 kHz are).
    for (const model::MasteringConversion& c : model::masteringConversions()) rate_->addItem(c.label, c.sampleRate);
    rate_->setCurrentIndex(2); // "The broadcast standard ... is 48 kHz"
    quality_ = new QComboBox(this);
    quality_->setObjectName(QStringLiteral("quality"));
    quality_->addItem(tr("High"), 64);
    quality_->addItem(tr("Medium"), 32);
    quality_->addItem(tr("Low"), 8);
    delete_ = new QCheckBox(tr("Delete Original Media"), this);
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("targetDrive"));
    for (const auto& [name, root] : drives) drive_->addItem(name, root);
    auto* form = new QFormLayout;
    form->addRow(tr("Sample Rate:"), rate_);
    form->addRow(tr("Quality:"), quality_);
    form->addRow(delete_);
    form->addRow(tr("Target Drive:"), drive_);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(okCancel(this));
}

int ChangeSampleRateDialog::sampleRate() const {
    return rate_->currentData().toInt();
}

int ChangeSampleRateDialog::filterSize() const {
    return quality_->currentData().toInt();
}

bool ChangeSampleRateDialog::deleteOriginal() const {
    return delete_->isChecked();
}

QString ChangeSampleRateDialog::drive() const {
    return drive_->currentData().toString();
}

} // namespace lumenis::ui
