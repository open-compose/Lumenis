// The Batch Import dialog box (batch_import_dialog.h; Editing Guide p.235-237).
#include "lumenis/ui/batch_import_dialog.h"

#include "lumenis/media/batch_import.h"
#include "lumenis/media/managed.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace lumenis::ui {

BatchImportDialog::BatchImportDialog(QList<Entry> entries, QWidget* parent) : QDialog(parent), entries_(std::move(entries)) {
    setWindowTitle(tr("Batch Import"));
    setObjectName(QStringLiteral("batchImportDialog"));
    auto* layout = new QVBoxLayout(this);

    // Selected Clips: Clip Name and Import File Location (the picture on p.236).
    caption_ = new QLabel(this);
    caption_->setObjectName(QStringLiteral("batchImportCaption"));
    layout->addWidget(caption_);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("batchImportClips"));
    list_->setHeaderLabels({tr("Clip Name"), tr("Import File Location")});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->header()->setStretchLastSection(true);
    list_->setColumnWidth(0, 180);
    list_->setMinimumSize(460, 160);
    layout->addWidget(list_);
    auto* listButtons = new QHBoxLayout;
    auto* skip = new QPushButton(tr("Skip This Clip"), this);
    skip->setObjectName(QStringLiteral("skipThisClip"));
    auto* setLocation = new QPushButton(tr("Set File Location..."), this);
    setLocation->setObjectName(QStringLiteral("setFileLocation"));
    listButtons->addWidget(skip);
    listButtons->addStretch();
    listButtons->addWidget(setLocation);
    layout->addLayout(listButtons);
    connect(skip, &QPushButton::clicked, this, &BatchImportDialog::skipSelected);
    connect(setLocation, &QPushButton::clicked, this, &BatchImportDialog::locate);

    // Import Target.
    auto* target = new QGroupBox(tr("Import Target"), this);
    auto* targetLayout = new QVBoxLayout(target);
    auto* resolutionRow = new QHBoxLayout;
    resolutionRow->addWidget(new QLabel(tr("Video Resolution"), target));
    resolution_ = new QComboBox(target);
    resolution_->setObjectName(QStringLiteral("batchImportResolution"));
    for (const media::Resolution& r : media::resolutions()) resolution_->addItem(r.name, r.id);
    resolutionRow->addWidget(resolution_, 1);
    targetLayout->addLayout(resolutionRow);
    // The Single/Dual Drives button beside the drive menus (p.230's picture): one menu for video and audio, or two.
    auto* driveRow = new QHBoxLayout;
    dual_ = new QToolButton(target);
    dual_->setObjectName(QStringLiteral("singleDualDrives"));
    dual_->setCheckable(true);
    dual_->setText(tr("Single/Dual Drives"));
    dual_->setToolTip(tr("Separate video and audio onto different drives"));
    driveRow->addWidget(dual_);
    auto* menus = new QVBoxLayout;
    videoDrive_ = new QComboBox(target);
    videoDrive_->setObjectName(QStringLiteral("batchImportVideoDrive"));
    audioDrive_ = new QComboBox(target);
    audioDrive_->setObjectName(QStringLiteral("batchImportAudioDrive"));
    const QList<media::MediaDrive> drives = media::mediaDrives();
    int roomiest = 0;
    for (int i = 0; i < drives.size(); ++i) {
        videoDrive_->addItem(drives.at(i).name, drives.at(i).root);
        audioDrive_->addItem(drives.at(i).name, drives.at(i).root);
        if (drives.at(i).freeBytes > drives.at(roomiest).freeBytes) roomiest = i;
    }
    videoDrive_->setCurrentIndex(roomiest);
    audioDrive_->setCurrentIndex(roomiest);
    menus->addWidget(videoDrive_);
    menus->addWidget(audioDrive_);
    driveRow->addLayout(menus, 1);
    targetLayout->addLayout(driveRow);
    layout->addWidget(target);
    connect(dual_, &QToolButton::toggled, this, &BatchImportDialog::setDualDrives);
    setDualDrives(false);

    // Import Options: "Contains global settings that affect all the files you are importing."
    auto* options = new QGroupBox(tr("Import Options"), this);
    auto* optionsLayout = new QVBoxLayout(options);
    // For OMFI files (p.237), which we do not import.
    sourceCompression_ = new QCheckBox(tr("Use source compression"), options);
    sourceCompression_->setObjectName(QStringLiteral("useSourceCompression"));
    sourceCompression_->setEnabled(false);
    sourceCompression_->setToolTip(tr("For OMFI files, which Cutthulhu does not import"));
    override_ = new QCheckBox(tr("Override clip settings with current settings"), options);
    override_->setObjectName(QStringLiteral("overrideClipSettings"));
    auto* current = new QPushButton(tr("Current Settings..."), options);
    current->setObjectName(QStringLiteral("currentImportSettings"));
    optionsLayout->addWidget(sourceCompression_);
    optionsLayout->addWidget(override_);
    optionsLayout->addWidget(current, 0, Qt::AlignLeft);
    layout->addWidget(options);
    connect(current, &QPushButton::clicked, this, &BatchImportDialog::currentSettingsRequested);

    auto* buttons = new QDialogButtonBox(this);
    import_ = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    import_->setObjectName(QStringLiteral("batchImportGo"));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refill();
}

int BatchImportDialog::foundCount() const {
    int n = 0;
    for (const Entry& e : entries_) {
        if (!e.path.isEmpty()) ++n;
    }
    return n;
}

QString BatchImportDialog::caption() const {
    return tr("%1 of %2 clips found").arg(foundCount()).arg(entries_.size());
}

void BatchImportDialog::refill() {
    QList<int> selected;
    for (QTreeWidgetItem* item : list_->selectedItems()) selected << list_->indexOfTopLevelItem(item);
    list_->clear();
    for (const Entry& e : entries_) {
        auto* row = new QTreeWidgetItem(list_, {e.name, e.path.isEmpty() ? e.lastPath : e.path});
        // "Found clips display in black. Clips not found in their original location display in red."
        const QColor ink = e.path.isEmpty() ? QColor(220, 40, 40) : palette().color(QPalette::Text);
        row->setForeground(0, ink);
        row->setForeground(1, ink);
    }
    for (int i : selected) {
        if (QTreeWidgetItem* item = list_->topLevelItem(i)) item->setSelected(true);
    }
    caption_->setText(caption());
    import_->setEnabled(foundCount() > 0);
}

void BatchImportDialog::skipSelected() {
    QList<int> rows;
    for (QTreeWidgetItem* item : list_->selectedItems()) rows << list_->indexOfTopLevelItem(item);
    std::sort(rows.begin(), rows.end(), std::greater<>());
    for (int row : rows) {
        if (row >= 0 && row < entries_.size()) entries_.removeAt(row);
    }
    list_->clearSelection();
    refill();
}

int BatchImportDialog::setFileLocation(const QString& chosen) {
    QList<int> rows;
    for (QTreeWidgetItem* item : list_->selectedItems()) rows << list_->indexOfTopLevelItem(item);
    std::sort(rows.begin(), rows.end());
    if (rows.isEmpty() || chosen.isEmpty() || !QFileInfo(chosen).isFile()) return 0;
    Entry& first = entries_[rows.first()];
    const QString firstOld = first.lastPath;
    first.path = QFileInfo(chosen).absoluteFilePath();
    int found = 1;
    for (int i = 1; i < rows.size(); ++i) {
        Entry& e = entries_[rows.at(i)];
        if (!e.path.isEmpty()) continue;
        e.path = media::relocatedSource(firstOld, first.path, e.lastPath);
        if (!e.path.isEmpty()) ++found;
    }
    refill();
    return found;
}

void BatchImportDialog::locate() {
    QList<int> rows;
    for (QTreeWidgetItem* item : list_->selectedItems()) rows << list_->indexOfTopLevelItem(item);
    std::sort(rows.begin(), rows.end());
    if (rows.isEmpty()) return;
    const QString last = entries_.at(rows.first()).lastPath;
    // The Locate File dialog box (p.235).
    const QString chosen = locateHook_ ? locateHook_(last)
                                       : QFileDialog::getOpenFileName(this, tr("Locate File"), QFileInfo(last).absolutePath());
    setFileLocation(chosen);
}

QString BatchImportDialog::resolution() const { return resolution_->currentData().toString(); }

void BatchImportDialog::setResolution(const QString& id) {
    const int i = resolution_->findData(id);
    if (i >= 0) resolution_->setCurrentIndex(i);
}

QString BatchImportDialog::videoDrive() const { return videoDrive_->currentData().toString(); }

QString BatchImportDialog::audioDrive() const {
    return dualDrives() ? audioDrive_->currentData().toString() : videoDrive();
}

void BatchImportDialog::setDrive(const QString& root) {
    const int i = videoDrive_->findData(root);
    if (i < 0) return;
    videoDrive_->setCurrentIndex(i);
    audioDrive_->setCurrentIndex(i);
}

void BatchImportDialog::setDualDrives(bool dual) {
    if (dual_->isChecked() != dual) dual_->setChecked(dual);
    audioDrive_->setVisible(dual);
    videoDrive_->setToolTip(dual ? tr("Video Drive") : tr("Video and Audio Drive"));
}

bool BatchImportDialog::dualDrives() const { return dual_->isChecked(); }

bool BatchImportDialog::overrideSettings() const { return override_->isChecked(); }

void BatchImportDialog::setOverrideSettings(bool on) { override_->setChecked(on); }

} // namespace lumenis::ui
