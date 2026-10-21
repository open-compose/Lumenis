#include "lumenis/ui/export_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

// The Export As dialog of File > Output > Export to File (ExportAsDialog in export_dialog.h). It only
// collects a file name and an Export setting; MainWindow (main_window.cpp) builds the list of Export
// settings, runs the dialog and writes the file through the AAF exporter (src/interop/aaf_export.*) or,
// for the Export As choice Media Composer Bin, the bin exporter (src/interop/avb_export.*).

namespace lumenis::ui {

// Builds the form: the file name with Browse, the Export setting with Options, then Save and Cancel.
ExportAsDialog::ExportAsDialog(const QString& path, const QStringList& settings, int active, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("exportAsDialog"));
    setWindowTitle(tr("Export As"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(path, this);
    path_->setObjectName(QStringLiteral("exportPath"));
    path_->setMinimumWidth(360);
    auto* browse = new QPushButton(tr("Browse..."), this);
    browse->setObjectName(QStringLiteral("exportBrowse"));
    connect(browse, &QPushButton::clicked, this, &ExportAsDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow(tr("File name:"), fileRow);
    auto* settingRow = new QHBoxLayout;
    settings_ = new QComboBox(this);
    settings_->setObjectName(QStringLiteral("exportSetting"));
    settings_->addItems(settings);
    settings_->setCurrentIndex(active);
    options_ = new QPushButton(tr("Options..."), this);
    options_->setObjectName(QStringLiteral("exportOptions"));
    connect(options_, &QPushButton::clicked, this, [this] { emit optionsRequested(setting()); });
    settingRow->addWidget(settings_, 1);
    settingRow->addWidget(options_);
    form->addRow(tr("Export Setting:"), settingRow);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    save_ = buttons->button(QDialogButtonBox::Save);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_, &QLineEdit::textChanged, this, [this](const QString& text) { save_->setEnabled(!text.trimmed().isEmpty()); });
    save_->setEnabled(!path.trimmed().isEmpty());
    layout->addWidget(buttons);
}

QString ExportAsDialog::path() const {
    return path_->text().trimmed();
}

void ExportAsDialog::setPath(const QString& path) {
    path_->setText(path);
}

int ExportAsDialog::setting() const {
    return settings_->currentIndex();
}

void ExportAsDialog::setSetting(int index) {
    settings_->setCurrentIndex(index);
}

void ExportAsDialog::setFileType(const QString& suffix, const QString& filter) {
    suffix_ = suffix;
    filter_ = filter;
}

// Browse: picks a file with the system's save dialog and makes sure it ends in the type's extension.
void ExportAsDialog::browse() {
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Export As"), path(), filter_);
    if (chosen.isEmpty()) return;
    // The file name extension stays the format's (p.869).
    setPath(QFileInfo(chosen).suffix().compare(suffix_, Qt::CaseInsensitive) == 0 ? chosen
                                                                                  : chosen + QLatin1Char('.') + suffix_);
}

} // namespace lumenis::ui
