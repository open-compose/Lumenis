#include "lumenis/ui/interchange_dialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

// The dialogs of the EDL and shot log entry points (interchange_dialogs.h): Import EDL (p.951) and
// Export Bin (p.130). They only collect choices; MainWindow (main_window.cpp) runs them and reads or
// writes the files through src/interop/edl.* and ale.*.

namespace lumenis::ui {

namespace {

// "23.976", "25", "29.97": a rate as the menu shows it.
QString rateText(const Rational& r) {
    return r.den() == 1 ? QString::number(r.num()) : QString::number(r.toDouble(), 'f', r == Rational(24000, 1001) ? 3 : 2);
}

// A dimmed entry in a combo box: shown, not chosen.
void dim(QComboBox* box, int index) {
    if (auto* model = qobject_cast<QStandardItemModel*>(box->model())) {
        if (QStandardItem* item = model->item(index)) item->setEnabled(false);
    }
}

} // namespace

const QList<Rational>& ImportEdlDialog::rates() {
    static const QList<Rational> r{Rational(24000, 1001), Rational(24), Rational(25), Rational(30000, 1001), Rational(30),
                                   Rational(50), Rational(60000, 1001), Rational(60)};
    return r;
}

ImportEdlDialog::ImportEdlDialog(const QString& path, const Rational& rate, const QList<Bin>& bins, const QString& current,
                                 QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("importEdlDialog"));
    setWindowTitle(tr("Import EDL"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(path, this);
    path_->setObjectName(QStringLiteral("edlPath"));
    path_->setMinimumWidth(360);
    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &ImportEdlDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow(tr("File:"), fileRow);
    // Step 4: Frame Rate, Project Type and Audio Mapping.
    rate_ = new QComboBox(this);
    rate_->setObjectName(QStringLiteral("edlRate"));
    for (const Rational& r : rates()) rate_->addItem(rateText(r));
    setRate(rate);
    form->addRow(tr("Frame Rate:"), rate_);
    auto* projectType = new QComboBox(this);
    projectType->addItem(tr("Project"));
    projectType->setEnabled(false);
    form->addRow(tr("Project Type:"), projectType);
    auto* audioMapping = new QComboBox(this);
    audioMapping->addItem(tr("As in the list"));
    audioMapping->setEnabled(false);
    form->addRow(tr("Audio Mapping:"), audioMapping);
    // Step 6: the bin, or New Bin.
    bins_ = new QComboBox(this);
    bins_->setObjectName(QStringLiteral("edlBin"));
    bins_->addItem(tr("New Bin"), QString());
    for (const Bin& b : bins) bins_->addItem(b.name, b.id);
    setBin(current);
    form->addRow(tr("Bin:"), bins_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_, &QLineEdit::textChanged, this, [this](const QString& text) { ok_->setEnabled(!text.trimmed().isEmpty()); });
    ok_->setEnabled(!path.trimmed().isEmpty());
    layout->addWidget(buttons);
}

QString ImportEdlDialog::path() const {
    return path_->text().trimmed();
}

void ImportEdlDialog::setPath(const QString& path) {
    path_->setText(path);
}

Rational ImportEdlDialog::rate() const {
    const int i = rate_->currentIndex();
    return i >= 0 && i < rates().size() ? rates().at(i) : rates().first();
}

// A rate not in the menu chooses the first, 23.976.
void ImportEdlDialog::setRate(const Rational& rate) {
    rate_->setCurrentIndex(std::max(0, static_cast<int>(rates().indexOf(rate))));
}

QString ImportEdlDialog::bin() const {
    return bins_->currentData().toString();
}

void ImportEdlDialog::setBin(const QString& id) {
    bins_->setCurrentIndex(std::max(0, bins_->findData(id)));
}

// The system's open dialog, for EDLs (step 2, "Select File to Open").
void ImportEdlDialog::browse() {
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Select File to Open"), path(),
                                                        tr("Edit decision lists (*.edl);;All files (*)"));
    if (!chosen.isEmpty()) setPath(chosen);
}

ExportBinDialog::ExportBinDialog(const QString& path, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("exportBinDialog"));
    setWindowTitle(tr("Export Bin As"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("exportBinFormat"));
    format_->addItem(tr("Avid Log Exchange"), QStringLiteral("ale"));
    format_->addItem(tr("Tab Delimited"), QStringLiteral("tabDelimited"));
    dim(format_, 1);
    form->addRow(tr("Export Bin As:"), format_);
    auto* fileRow = new QHBoxLayout;
    path_ = new QLineEdit(path, this);
    path_->setObjectName(QStringLiteral("exportBinPath"));
    path_->setMinimumWidth(360);
    auto* browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &ExportBinDialog::browse);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    form->addRow(tr("File name:"), fileRow);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_, &QLineEdit::textChanged, this, [this](const QString& text) { ok_->setEnabled(!text.trimmed().isEmpty()); });
    ok_->setEnabled(!path.trimmed().isEmpty());
    layout->addWidget(buttons);
}

QString ExportBinDialog::path() const {
    return path_->text().trimmed();
}

void ExportBinDialog::setPath(const QString& path) {
    path_->setText(path);
}

QString ExportBinDialog::format() const {
    return format_->currentData().toString();
}

// The system's save dialog; the file name keeps the .ale extension.
void ExportBinDialog::browse() {
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Export Bin As"), path(), tr("Avid Log Exchange (*.ale)"));
    if (chosen.isEmpty()) return;
    setPath(QFileInfo(chosen).suffix().compare(QLatin1String("ale"), Qt::CaseInsensitive) == 0 ? chosen
                                                                                               : chosen + QStringLiteral(".ale"));
}

} // namespace lumenis::ui
