#include "lumenis/ui/render_dialogs.h"

#include "lumenis/media/managed.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
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

} // namespace lumenis::ui
