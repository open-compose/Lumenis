// The Send To dialog box and the Pro Tools options (lumenis/ui/send_to_dialog.h; Editing Guide p.863-867).
#include "lumenis/ui/send_to_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace lumenis::ui {

SendToDialog::SendToDialog(const model::SendToTemplate& t, const QString& fileName, QWidget* parent)
    : QDialog(parent), template_(t), folder_(t.folder) {
    setObjectName(QStringLiteral("sendToDialog"));
    setWindowTitle(tr("Send To: %1").arg(t.name));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(fileName, this);
    name_->setObjectName(QStringLiteral("sendToFileName"));
    form->addRow(tr("Filename:"), name_);
    auto* folderRow = new QHBoxLayout;
    folderLabel_ = new QLabel(this);
    folderLabel_->setObjectName(QStringLiteral("sendToFolder"));
    auto* set = new QPushButton(tr("Set..."), this);
    set->setObjectName(QStringLiteral("sendToSet"));
    connect(set, &QPushButton::clicked, this, &SendToDialog::chooseFolder);
    folderRow->addWidget(folderLabel_, 1);
    folderRow->addWidget(set);
    form->addRow(tr("Destination:"), folderRow);
    layout->addLayout(form);

    // "If the template you are using can automatically launch the application that handles the exported file, an
    // Auto Launch option appears ... Auto Launch might also have sub-options" (p.865).
    autoLaunch_ = new QCheckBox(tr("Auto Launch"), this);
    autoLaunch_->setObjectName(QStringLiteral("sendToAutoLaunch"));
    autoLaunch_->setChecked(t.autoLaunch);
    auto* appRow = new QHBoxLayout;
    application_ = new QLineEdit(t.application, this);
    application_->setObjectName(QStringLiteral("sendToApplication"));
    auto* add = new QPushButton(tr("Add Item..."), this);
    add->setObjectName(QStringLiteral("sendToAddItem"));
    connect(add, &QPushButton::clicked, this, &SendToDialog::chooseApplication);
    appRow->addWidget(autoLaunch_);
    appRow->addWidget(application_, 1);
    appRow->addWidget(add);
    layout->addLayout(appRow);
    autoLoad_ = new QCheckBox(tr("Auto Load Exported File(s)"), this);
    autoLoad_->setObjectName(QStringLiteral("sendToAutoLoad"));
    autoLoad_->setChecked(t.autoLoad);
    reveal_ = new QCheckBox(tr("Reveal file"), this);
    reveal_->setObjectName(QStringLiteral("sendToReveal"));
    reveal_->setChecked(t.reveal);
    layout->addWidget(autoLoad_);
    layout->addWidget(reveal_);

    // "The Export Setting Summary area at the bottom of the dialog box lists all the settings that affect the current
    // export" (p.865).
    layout->addWidget(new QLabel(tr("Export Setting Summary"), this));
    summary_ = new QPlainTextEdit(this);
    summary_->setObjectName(QStringLiteral("sendToSummary"));
    summary_->setReadOnly(true);
    summary_->setMinimumHeight(120);
    layout->addWidget(summary_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* options = buttons->addButton(tr("Options..."), QDialogButtonBox::ActionRole);
    options->setObjectName(QStringLiteral("sendToOptions"));
    connect(options, &QPushButton::clicked, this, &SendToDialog::optionsRequested);
    auto* saveAs = buttons->addButton(tr("Save As Template..."), QDialogButtonBox::ActionRole);
    saveAs->setObjectName(QStringLiteral("sendToSaveAsTemplate"));
    connect(saveAs, &QPushButton::clicked, this, &SendToDialog::saveAsTemplateRequested);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setFolder(folder_);
}

QString SendToDialog::fileName() const {
    return name_->text().trimmed();
}

void SendToDialog::setFolder(const QString& folder) {
    folder_ = folder;
    folderLabel_->setText(folder.isEmpty() ? tr("(not set)") : QDir::toNativeSeparators(folder));
}

model::SendToTemplate SendToDialog::sendToTemplate() const {
    model::SendToTemplate t = template_;
    t.folder = folder_;
    t.autoLaunch = autoLaunch_->isChecked();
    t.application = application_->text().trimmed();
    t.autoLoad = autoLoad_->isChecked();
    t.reveal = reveal_->isChecked();
    return t;
}

void SendToDialog::setExportValues(const QJsonObject& values) {
    template_.exportValues = values;
}

void SendToDialog::setSummary(const QStringList& lines) {
    summary_->setPlainText(lines.join(QLatin1Char('\n')));
}

void SendToDialog::chooseFolder() {
    // "Click Set to browse to the drive and folder where you want to store the exported file" (p.864).
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Set Destination"), folder_);
    if (!chosen.isEmpty()) setFolder(chosen);
}

void SendToDialog::chooseApplication() {
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Choose the Application"), application_->text());
    if (chosen.isEmpty()) return;
    application_->setText(chosen);
    autoLaunch_->setChecked(true);
}

ProToolsOptionsDialog::ProToolsOptionsDialog(const QString& chosen, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("proToolsOptions"));
    setWindowTitle(tr("Pro Tools"));
    auto* layout = new QVBoxLayout(this);
    group_ = new QButtonGroup(this);
    const QList<model::ProToolsOption>& options = model::proToolsOptions();
    for (int i = 0; i < options.size(); ++i) {
        auto* r = new QRadioButton(options.at(i).label, this);
        r->setObjectName(QStringLiteral("proTools:") + options.at(i).id);
        r->setEnabled(options.at(i).available);
        if (!options.at(i).available) r->setToolTip(tr("Not available yet"));
        group_->addButton(r, i);
        layout->addWidget(r);
    }
    setOption(chosen.isEmpty() ? QStringLiteral("linkVideoAudio") : chosen);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString ProToolsOptionsDialog::option() const {
    const int id = group_->checkedId();
    return id >= 0 ? model::proToolsOptions().at(id).id : QString();
}

void ProToolsOptionsDialog::setOption(const QString& id) {
    const QList<model::ProToolsOption>& options = model::proToolsOptions();
    for (int i = 0; i < options.size(); ++i) {
        if (options.at(i).id == id && options.at(i).available) group_->button(i)->setChecked(true);
    }
}

} // namespace lumenis::ui
