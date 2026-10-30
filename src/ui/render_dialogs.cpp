#include "lumenis/ui/render_dialogs.h"

#include "lumenis/media/managed.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

// The Render Effect and Clear Renders dialogs (render_dialogs.h). MainWindow runs them and renders or
// clears (MainWindow::renderEffects, clearRenders).
namespace lumenis::ui {

RenderEffectDialog::RenderEffectDialog(int effects, const QString& drive, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Render Effect"));
    auto* layout = new QVBoxLayout(this);
    auto* count = new QLabel(tr("%n effect(s) to render.", "", effects), this);
    count->setObjectName(QStringLiteral("count"));
    layout->addWidget(count);
    auto* form = new QFormLayout;
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("drive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drive_->addItem(d.name == d.root ? d.root : d.name, d.root);
    const int at = drive_->findData(drive);
    if (at >= 0) drive_->setCurrentIndex(at);
    form->addRow(tr("Drive:"), drive_);
    processing_ = new QComboBox(this);
    processing_->setObjectName(QStringLiteral("processing"));
    processing_->addItems({tr("Application"), tr("Background Process"), tr("Distributed Processing")});
    // Only rendering within the application for now; the others are dimmed (p.59).
    if (auto* model = qobject_cast<QStandardItemModel*>(processing_->model())) {
        model->item(1)->setEnabled(false);
        model->item(2)->setEnabled(false);
    }
    form->addRow(tr("Processing:"), processing_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(drive_->count() > 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString RenderEffectDialog::drive() const {
    return drive_->currentData().toString();
}

ClearRendersDialog::ClearRendersDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Clear Renders"));
    auto* layout = new QVBoxLayout(this);
    motion_ = new QCheckBox(tr("Skip Motion Effects"), this);
    motion_->setObjectName(QStringLiteral("skipMotionEffects"));
    motion_->setChecked(true);
    unknown_ = new QCheckBox(tr("Skip Unknown Effects"), this);
    unknown_->setObjectName(QStringLiteral("skipUnknownEffects"));
    unknown_->setChecked(true);
    layout->addWidget(motion_);
    layout->addWidget(unknown_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool ClearRendersDialog::skipMotionEffects() const {
    return motion_->isChecked();
}

bool ClearRendersDialog::skipUnknownEffects() const {
    return unknown_->isChecked();
}

ExpertRenderDialog::ExpertRenderDialog(const std::function<int(model::ExpertMode)>& count, bool recommended,
                                       const QString& drive, QWidget* parent)
    : QDialog(parent), count_(count), hasRecommended_(recommended) {
    setWindowTitle(tr("Expert Render"));
    auto* layout = new QVBoxLayout(this);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("message"));
    layout->addWidget(message_);
    recommended_ = new QRadioButton(tr("Render recommended ranges"), this);
    recommended_->setObjectName(QStringLiteral("recommended"));
    recommended_->setVisible(recommended);
    entire_ = new QRadioButton(tr("Render entire selection"), this);
    entire_->setObjectName(QStringLiteral("entire"));
    digitalCut_ = new QRadioButton(tr("Prepare effects for Digital Cut"), this);
    digitalCut_->setObjectName(QStringLiteral("digitalCut"));
    (recommended ? recommended_ : entire_)->setChecked(true);
    for (QRadioButton* b : {recommended_, entire_, digitalCut_}) {
        layout->addWidget(b);
        connect(b, &QRadioButton::toggled, this, [this] { showCount(); });
    }
    auto* form = new QFormLayout;
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("drive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drive_->addItem(d.name == d.root ? d.root : d.name, d.root);
    if (const int at = drive_->findData(drive); at >= 0) drive_->setCurrentIndex(at);
    form->addRow(tr("Drive:"), drive_);
    auto* processing = new QComboBox(this);
    processing->setObjectName(QStringLiteral("processing"));
    processing->addItems({tr("Application"), tr("Background Process"), tr("Distributed Processing")});
    if (auto* model = qobject_cast<QStandardItemModel*>(processing->model())) {
        model->item(1)->setEnabled(false);
        model->item(2)->setEnabled(false);
    }
    form->addRow(tr("Processing:"), processing);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* modify = buttons->addButton(tr("Modify Selection"), QDialogButtonBox::ActionRole);
    modify->setObjectName(QStringLiteral("modify"));
    QPushButton* ok = buttons->addButton(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] { choose(Choice::Cancel); });
    connect(modify, &QPushButton::clicked, this, [this] { choose(Choice::Modify); });
    connect(ok, &QPushButton::clicked, this, [this] { choose(Choice::Ok); });
    layout->addWidget(buttons);
    showCount();
}

model::ExpertMode ExpertRenderDialog::mode() const {
    if (hasRecommended_ && recommended_->isChecked()) return model::ExpertMode::RecommendedRanges;
    return digitalCut_->isChecked() ? model::ExpertMode::DigitalCut : model::ExpertMode::EntireSelection;
}

void ExpertRenderDialog::setMode(model::ExpertMode mode) {
    switch (mode) {
    case model::ExpertMode::RecommendedRanges: recommended_->setChecked(true); break;
    case model::ExpertMode::EntireSelection: entire_->setChecked(true); break;
    case model::ExpertMode::DigitalCut: digitalCut_->setChecked(true); break;
    }
}

QString ExpertRenderDialog::drive() const {
    return drive_->currentData().toString();
}

void ExpertRenderDialog::choose(Choice c) {
    choice_ = c;
    if (c == Choice::Cancel) {
        reject();
    } else {
        accept();
    }
}

// "The message in the Expert Render dialog box changes to indicate the number of effects" (p.144).
void ExpertRenderDialog::showCount() {
    const int n = count_ ? count_(mode()) : 0;
    message_->setText(n == 0 ? tr("No effects need rendering.") : tr("%n effect(s) need rendering.", "", n));
}

} // namespace lumenis::ui
