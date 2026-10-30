#include "lumenis/ui/motion_effect_dialogs.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/motion_effect.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

// The Freeze Frame and Motion Effect dialogs (motion_effect_dialogs.h). MainWindow runs them and makes
// the motion effect clips (MainWindow::freezeFrame, motionEffect; lumenis/model/motion_effect.h).
namespace lumenis::ui {

FreezeFrameDialog::FreezeFrameDialog(const Rational& rate, std::int64_t duration, QWidget* parent)
    : QDialog(parent), rate_(rate) {
    setWindowTitle(tr("Freeze Frame"));
    auto* layout = new QFormLayout(this);
    duration_ = new QLineEdit(this);
    duration_->setObjectName(QStringLiteral("duration"));
    duration_->setText(QString::fromStdString(Timecode(duration, TimecodeFormat::forRate(rate, false)).toString()));
    layout->addRow(tr("Duration:"), duration_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(duration_, &QLineEdit::textChanged, this,
            [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(this->duration().has_value()); });
    layout->addRow(buttons);
}

// A timecode ("00:00:02:00") or a plain number of frames.
std::optional<std::int64_t> FreezeFrameDialog::duration() const {
    const QString text = duration_->text().trimmed();
    bool ok = false;
    const qlonglong frames = text.toLongLong(&ok);
    if (ok) return frames > 0 ? std::optional<std::int64_t>(frames) : std::nullopt;
    const auto tc = Timecode::parse(text.toStdString(), TimecodeFormat::forRate(rate_, false));
    if (!tc || tc->frameCount() <= 0) return std::nullopt;
    return tc->frameCount();
}

MotionEffectDialog::MotionEffectDialog(std::int64_t length, const Rational& rate, std::optional<std::int64_t> fitLength,
                                       const QString& drive, QWidget* parent)
    : QDialog(parent), length_(std::max<std::int64_t>(length, 1)), rate_(rate), fitLength_(fitLength) {
    setWindowTitle(tr("Motion Effect"));
    auto* layout = new QVBoxLayout(this);
    variable_ = new QCheckBox(tr("Variable Speed"), this);
    variable_->setObjectName(QStringLiteral("variableSpeed"));
    variable_->setChecked(true);
    layout->addWidget(variable_);
    auto* speed = new QFormLayout;
    frames_ = new QSpinBox(this);
    frames_->setObjectName(QStringLiteral("frames"));
    frames_->setRange(1, 1000000);
    fps_ = new QDoubleSpinBox(this);
    fps_->setObjectName(QStringLiteral("fps"));
    fps_->setDecimals(2);
    fps_->setRange(-rate.toDouble() * 100, rate.toDouble() * 100);
    percent_ = new QDoubleSpinBox(this);
    percent_->setObjectName(QStringLiteral("percent"));
    percent_->setDecimals(2);
    percent_->setRange(-10000, 10000);
    // "Duration n Frames", "Rate n FPS", "% Speed" (p.563).
    speed->addRow(tr("Duration (frames):"), frames_);
    speed->addRow(tr("Rate (FPS):"), fps_);
    speed->addRow(tr("% Speed:"), percent_);
    fit_ = new QCheckBox(tr("Fit To Fill"), this);
    fit_->setObjectName(QStringLiteral("fitToFill"));
    fit_->setEnabled(fitLength_.has_value() && *fitLength_ > 0);
    speed->addRow(fit_);
    layout->addLayout(speed);
    strobe_ = new QCheckBox(tr("Strobe Motion"), this);
    strobe_->setObjectName(QStringLiteral("strobeMotion"));
    layout->addWidget(strobe_);
    auto* strobeRow = new QFormLayout;
    strobeFrames_ = new QSpinBox(this);
    strobeFrames_->setObjectName(QStringLiteral("strobeFrames"));
    strobeFrames_->setRange(2, 1000);
    strobeFrames_->setValue(5);
    strobeFrames_->setEnabled(false);
    strobeRow->addRow(tr("Update every (frames):"), strobeFrames_);
    layout->addLayout(strobeRow);
    auto* driveRow = new QFormLayout;
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("drive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drive_->addItem(d.name == d.root ? d.root : d.name, d.root);
    if (const int at = drive_->findData(drive); at >= 0) drive_->setCurrentIndex(at);
    driveRow->addRow(tr("Target Drive:"), drive_);
    layout->addLayout(driveRow);

    // "All of the values change to reflect the rate you specify" (p.169).
    connect(frames_, &QSpinBox::valueChanged, this, [this](int frames) {
        if (updating_) return;
        const double magnitude = static_cast<double>(length_) / std::max(frames, 1);
        showSpeed(speed_ < 0 ? -magnitude : magnitude, frames_);
    });
    connect(fps_, &QDoubleSpinBox::valueChanged, this, [this](double fps) {
        if (!updating_) showSpeed(fps / rate_.toDouble(), fps_);
    });
    connect(percent_, &QDoubleSpinBox::valueChanged, this, [this](double percent) {
        if (!updating_) showSpeed(percent / 100.0, percent_);
    });
    connect(fit_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && fitLength_) showSpeed(static_cast<double>(length_) / static_cast<double>(*fitLength_), fit_);
    });
    connect(variable_, &QCheckBox::toggled, this, [this](bool on) {
        frames_->setEnabled(on);
        fps_->setEnabled(on);
        percent_->setEnabled(on);
        fit_->setEnabled(on && fitLength_.has_value() && *fitLength_ > 0);
    });
    connect(strobe_, &QCheckBox::toggled, strobeFrames_, &QWidget::setEnabled);
    showSpeed(1.0, nullptr);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* create = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    create->setObjectName(QStringLiteral("create"));
    QPushButton* render = buttons->addButton(tr("Create and Render"), QDialogButtonBox::AcceptRole);
    render->setObjectName(QStringLiteral("createAndRender"));
    render->setEnabled(drive_->count() > 0);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(create, &QPushButton::clicked, this, [this] { choose(Choice::Create); });
    connect(render, &QPushButton::clicked, this, [this] { choose(Choice::CreateAndRender); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { choose(Choice::Cancel); });
    layout->addWidget(buttons);
}

void MotionEffectDialog::showSpeed(double speed, const QObject* from) {
    speed_ = speed;
    updating_ = true;
    if (from != frames_) frames_->setValue(static_cast<int>(model::motionEffectLength(0, length_ - 1, speed)));
    if (from != fps_) fps_->setValue(model::motionFps(speed, rate_));
    if (from != percent_) percent_->setValue(speed * 100.0);
    // Fit To Fill stays checked only while the speed is the one that fills the marks.
    if (from != fit_) fit_->setChecked(false);
    updating_ = false;
}

double MotionEffectDialog::speed() const {
    return variable_->isChecked() ? speed_ : 1.0;
}

int MotionEffectDialog::strobe() const {
    return strobe_->isChecked() ? strobeFrames_->value() : 1;
}

QString MotionEffectDialog::drive() const {
    return drive_->currentData().toString();
}

void MotionEffectDialog::choose(Choice c) {
    choice_ = c;
    if (c == Choice::Cancel) {
        reject();
    } else {
        accept();
    }
}

} // namespace lumenis::ui
