// The AncData Import dialog box (lumenis/ui/ancdata_import_dialog.h; Editing Guide p.232).
#include "lumenis/ui/ancdata_import_dialog.h"

#include "lumenis/core/timecode.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace lumenis::ui {

AncDataImportDialog::AncDataImportDialog(const QString& fileName, std::int64_t startFrame, bool dropFrame, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("ancDataImport"));
    setWindowTitle(tr("AncData Import"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(fileName, this));
    auto* form = new QFormLayout;
    start_ = new QLineEdit(QString::fromStdString(Timecode(startFrame, TimecodeFormat{30, dropFrame}).toString()), this);
    start_->setObjectName(QStringLiteral("startTimecode"));
    form->addRow(tr("Starting Timecode:"), start_);
    dropFrame_ = new QCheckBox(tr("Drop Frame"), this);
    dropFrame_->setObjectName(QStringLiteral("dropFrame"));
    dropFrame_->setChecked(dropFrame);
    form->addRow(QString(), dropFrame_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AncDataImportDialog::setStartTimecode(const QString& text) {
    start_->setText(text);
}

void AncDataImportDialog::setDropFrame(bool on) {
    dropFrame_->setChecked(on);
}

bool AncDataImportDialog::dropFrame() const {
    return dropFrame_->isChecked();
}

std::optional<std::int64_t> AncDataImportDialog::startFrame() const {
    const auto t = Timecode::parse(start_->text().trimmed().toStdString(), TimecodeFormat{30, dropFrame()});
    if (!t) return std::nullopt;
    return t->frameCount();
}

} // namespace lumenis::ui
