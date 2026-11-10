// The Create Proxies window (proxy_dialogs.h; Editing Guide p.508).
#include "lumenis/ui/proxy_dialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>

namespace lumenis::ui {

CreateProxiesDialog::CreateProxiesDialog(const QList<std::pair<QString, QString>>& drives, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("createProxiesDialog"));
    setWindowTitle(tr("Create Proxies"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    drives_ = new QComboBox(this);
    drives_->setObjectName(QStringLiteral("proxyDrive"));
    for (const auto& [name, root] : drives) drives_->addItem(name, root);
    form->addRow(tr("Drive:"), drives_);
    processing_ = new QComboBox(this);
    processing_->setObjectName(QStringLiteral("proxyProcessing"));
    processing_->addItems({tr("Application"), tr("Background Process")});
    form->addRow(tr("Processing Options:"), processing_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString CreateProxiesDialog::drive() const {
    return drives_->currentData().toString();
}

bool CreateProxiesDialog::background() const {
    return processing_->currentIndex() == 1;
}

void CreateProxiesDialog::setBackground(bool on) {
    processing_->setCurrentIndex(on ? 1 : 0);
}

} // namespace lumenis::ui
