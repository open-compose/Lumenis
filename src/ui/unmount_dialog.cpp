#include "lumenis/ui/unmount_dialog.h"

#include "lumenis/media/managed.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

// The Unmount Disk or Drives dialog (unmount_dialog.h). MainWindow runs it and unmounts what it returns.

namespace lumenis::ui {

UnmountDialog::UnmountDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Unmount Disk or Drives"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Select the drives to unmount:"), this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("drives"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const media::MediaDrive& d : media::mediaDrives()) {
        auto* item = new QListWidgetItem(d.name == d.root ? d.root : QStringLiteral("%1 (%2)").arg(d.name, d.root), list_);
        item->setData(Qt::UserRole, d.root);
    }
    layout->addWidget(list_);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* unmount = buttons->addButton(tr("Unmount"), QDialogButtonBox::AcceptRole);
    unmount->setObjectName(QStringLiteral("unmount"));
    QPushButton* mountAll = buttons->addButton(tr("Mount All"), QDialogButtonBox::ActionRole);
    mountAll->setObjectName(QStringLiteral("mountAll"));
    mountAll->setEnabled(!media::unmountedDrives().isEmpty());
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mountAll, &QPushButton::clicked, this, [this] {
        mountAll_ = true;
        list_->clearSelection();
        accept();
    });
    // Unmount needs a selection.
    unmount->setEnabled(false);
    connect(list_, &QListWidget::itemSelectionChanged, this, [this, unmount] { unmount->setEnabled(!list_->selectedItems().isEmpty()); });
    layout->addWidget(buttons);
}

QStringList UnmountDialog::selectedRoots() const {
    QStringList out;
    for (QListWidgetItem* item : list_->selectedItems()) out << item->data(Qt::UserRole).toString();
    return out;
}

} // namespace lumenis::ui
