// The Create Proxies and Copy Media windows (proxy_dialogs.h; Editing Guide p.508-513).
#include "lumenis/ui/proxy_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace lumenis::ui {

// ------------------------------------------------------------------ Create Proxies

CreateProxiesDialog::CreateProxiesDialog(const QList<MediaTarget>& targets, QWidget* parent)
    : QDialog(parent), targets_(targets) {
    setObjectName(QStringLiteral("createProxiesDialog"));
    setWindowTitle(tr("Create Proxies"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    drives_ = new QComboBox(this);
    drives_->setObjectName(QStringLiteral("proxyDrive"));
    for (const MediaTarget& t : targets_) drives_->addItem(t.name, t.root);
    form->addRow(tr("Drive:"), drives_);
    processing_ = new QComboBox(this);
    processing_->setObjectName(QStringLiteral("proxyProcessing"));
    processing_->addItems({tr("Application"), tr("Background Process")});
    form->addRow(tr("Processing Options:"), processing_);
    layout->addLayout(form);
    distributed_ = new QCheckBox(tr("Distributed Processing"), this);
    distributed_->setObjectName(QStringLiteral("proxyDistributed"));
    layout->addWidget(distributed_);
    connect(drives_, &QComboBox::currentIndexChanged, this, [this] { updateDistributed(); });
    connect(distributed_, &QCheckBox::toggled, this, [this] { updateDistributed(); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateDistributed();
}

MediaTarget CreateProxiesDialog::target() const {
    const int i = drives_->currentIndex();
    return i >= 0 && i < targets_.size() ? targets_.at(i) : MediaTarget{};
}

QString CreateProxiesDialog::drive() const {
    return drives_->currentData().toString();
}

void CreateProxiesDialog::setTarget(int index) {
    drives_->setCurrentIndex(index);
}

bool CreateProxiesDialog::background() const {
    return processing_->currentIndex() == 1;
}

void CreateProxiesDialog::setBackground(bool on) {
    processing_->setCurrentIndex(on ? 1 : 0);
}

bool CreateProxiesDialog::distributed() const {
    return distributed_->isEnabled() && distributed_->isChecked();
}

void CreateProxiesDialog::setDistributed(bool on) {
    distributed_->setChecked(on);
}

// "The Distributed Processing option is only available after you select an Avid NEXIS workspace" (p.510); ours: one whose
// storage takes jobs.
void CreateProxiesDialog::updateDistributed() {
    const MediaTarget t = target();
    distributed_->setEnabled(t.isWorkspace() && t.distributedProcessing);
    // The jobs run on the workers, so Application and Background Process do not apply to them (ours).
    processing_->setEnabled(!distributed());
}

// ------------------------------------------------------------------ Copy Media

CopyMediaDialog::CopyMediaDialog(const QList<MediaTarget>& drives, media::CopyMediaRequest request, QWidget* parent)
    : QDialog(parent), drives_(drives), request_(std::move(request)) {
    setObjectName(QStringLiteral("copyMediaDialog"));
    setWindowTitle(tr("Copy Media"));
    binsFolder_ = request_.binsTarget;
    auto* layout = new QVBoxLayout(this);
    auto box = [this](const QString& text, const QString& name) {
        auto* b = new QCheckBox(text, this);
        b->setObjectName(name);
        b->setChecked(true);
        connect(b, &QCheckBox::toggled, this, [this] {
            updateControls();
            scheduleRecalculate();
        });
        return b;
    };
    bins_ = box(tr("Bins"), QStringLiteral("copyBins"));
    layout->addWidget(bins_);
    auto* kinds = new QHBoxLayout;
    managed_ = box(tr("Managed media"), QStringLiteral("copyManaged"));
    proxies_ = box(tr("Proxy media"), QStringLiteral("copyProxies"));
    renders_ = box(tr("Renders"), QStringLiteral("copyRenders"));
    kinds->addWidget(managed_);
    kinds->addWidget(proxies_);
    kinds->addWidget(renders_);
    kinds->addStretch();
    layout->addLayout(kinds);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("copyStatus"));
    layout->addWidget(status_);

    auto* grid = new QGridLayout;
    int row = 0;
    copyTo_ = new QComboBox(this);
    copyTo_->setObjectName(QStringLiteral("copyMediaTo"));
    copyTo_->addItems({tr("Media Drive"), tr("Folder")});
    setFolder_ = new QPushButton(tr("Set"), this);
    setFolder_->setObjectName(QStringLiteral("copySetFolder"));
    grid->addWidget(new QLabel(tr("Copy Media to:"), this), row, 0, Qt::AlignRight);
    grid->addWidget(copyTo_, row, 1);
    grid->addWidget(setFolder_, row++, 2);
    driveLabel_ = new QLabel(tr("Drive:"), this);
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("copyDrive"));
    for (const MediaTarget& d : drives_) drive_->addItem(d.name, d.root);
    grid->addWidget(driveLabel_, row, 0, Qt::AlignRight);
    grid->addWidget(drive_, row++, 1);
    folderPath_ = new QLabel(this);
    folderPath_->setObjectName(QStringLiteral("copyFolderPath"));
    grid->addWidget(folderPath_, row++, 1);
    space_ = new QLabel(this);
    space_->setObjectName(QStringLiteral("copySpace"));
    grid->addWidget(space_, row++, 1);
    grid->setRowMinimumHeight(row++, 12);
    sameAsAbove_ = new QCheckBox(tr("Use same as above"), this);
    sameAsAbove_->setObjectName(QStringLiteral("copyBinsSame"));
    sameAsAbove_->setChecked(true);
    setBins_ = new QPushButton(tr("Set"), this);
    setBins_->setObjectName(QStringLiteral("copySetBins"));
    grid->addWidget(new QLabel(tr("Copy Bins to:"), this), row, 0, Qt::AlignRight);
    grid->addWidget(sameAsAbove_, row, 1);
    grid->addWidget(setBins_, row++, 2);
    binsPath_ = new QLabel(this);
    binsPath_->setObjectName(QStringLiteral("copyBinsPath"));
    grid->addWidget(binsPath_, row++, 1);
    binsSpace_ = new QLabel(this);
    binsSpace_->setObjectName(QStringLiteral("copyBinsSpace"));
    grid->addWidget(binsSpace_, row++, 1);
    layout->addLayout(grid);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (canCopy()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(copyTo_, &QComboBox::currentIndexChanged, this, [this] {
        updateControls();
        scheduleRecalculate();
    });
    connect(drive_, &QComboBox::currentIndexChanged, this, [this] {
        updateControls();
        scheduleRecalculate();
    });
    connect(sameAsAbove_, &QCheckBox::toggled, this, [this] {
        updateControls();
        scheduleRecalculate();
    });
    connect(setFolder_, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getExistingDirectory(this, tr("Copy Media to"), folder_);
        if (!f.isEmpty()) setFolder(f);
    });
    connect(setBins_, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getExistingDirectory(this, tr("Copy Bins to"), binsFolder_);
        if (!f.isEmpty()) setBinsFolder(f);
    });
    updateControls();
    scheduleRecalculate();
}

media::CopyMediaRequest CopyMediaDialog::request() const {
    media::CopyMediaRequest r = request_;
    r.bins = bins_->isChecked();
    r.managed = managed_->isChecked();
    r.proxies = proxies_->isChecked();
    r.renders = renders_->isChecked();
    r.toFolder = copyTo_->currentIndex() == 1;
    r.target = r.toFolder ? folder_ : drive_->currentData().toString();
    // "If Folder was selected, you can choose to Copy Bins to the same location as the folder" (p.513).
    r.binsTarget = r.toFolder && sameAsAbove_->isChecked() ? folder_ : binsFolder_;
    return r;
}

QString CopyMediaDialog::status() const {
    return status_->text();
}

bool CopyMediaDialog::canCopy() const {
    if (pending_) return false;
    const media::CopyMediaRequest r = request();
    if (r.target.isEmpty() && (r.managed || r.proxies || r.renders)) return false;
    if (r.bins && r.binsTarget.isEmpty()) return false;
    return !plan_.isEmpty() && media::planFits(plan_, r);
}

void CopyMediaDialog::setChecked(const QString& box, bool on) {
    QCheckBox* b = box == QLatin1String("bins")      ? bins_
                   : box == QLatin1String("managed") ? managed_
                   : box == QLatin1String("proxies") ? proxies_
                   : box == QLatin1String("renders") ? renders_
                                                     : nullptr;
    if (b) b->setChecked(on);
}

void CopyMediaDialog::setCopyToFolder(bool folder) {
    copyTo_->setCurrentIndex(folder ? 1 : 0);
}

void CopyMediaDialog::setDrive(int index) {
    drive_->setCurrentIndex(index);
}

void CopyMediaDialog::setFolder(const QString& folder) {
    folder_ = folder;
    updateControls();
    scheduleRecalculate();
}

void CopyMediaDialog::setBinsFolder(const QString& folder) {
    binsFolder_ = folder;
    updateControls();
    scheduleRecalculate();
}

void CopyMediaDialog::setBinsSameAsAbove(bool same) {
    sameAsAbove_->setChecked(same);
}

// "Calculating Size..." first (p.513), then the sizes once the event loop has drawn it.
void CopyMediaDialog::scheduleRecalculate() {
    status_->setText(tr("Calculating Size..."));
    ok_->setEnabled(false);
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] {
        if (pending_) recalculate();
    });
}

void CopyMediaDialog::recalculate() {
    pending_ = false;
    const media::CopyMediaRequest r = request();
    plan_ = media::planCopyMedia(r);
    QStringList lines;
    if (r.managed || r.proxies || r.renders) lines << tr("Media size required: %1").arg(media::megabytes(plan_.mediaBytes));
    if (r.bins) lines << tr("Bins size required: %1").arg(media::megabytes(plan_.binBytes));
    if (plan_.isEmpty() && plan_.existing > 0) lines << tr("All selected files already exist at the target location");
    else if (plan_.isEmpty()) lines << tr("There is nothing to copy");
    if (!plan_.missing.isEmpty()) lines << tr("%n file(s) offline, not copied", nullptr, int(plan_.missing.size()));
    if (!plan_.isEmpty() && !media::planFits(plan_, r)) lines << tr("There is not enough space at the target location");
    status_->setText(lines.join(QLatin1Char('\n')));
    updateControls();
}

void CopyMediaDialog::updateControls() {
    const bool folder = copyTo_->currentIndex() == 1;
    driveLabel_->setVisible(!folder);
    drive_->setVisible(!folder);
    setFolder_->setVisible(folder);
    folderPath_->setVisible(folder);
    folderPath_->setText(QDir::toNativeSeparators(folder_));
    const media::CopyMediaRequest r = request();
    auto spaceText = [](const QString& path) {
        const std::int64_t free = path.isEmpty() ? -1 : media::availableSpace(path);
        return free < 0 ? QString() : CopyMediaDialog::tr("Available Space: %1").arg(media::gigabytes(free));
    };
    space_->setText(spaceText(r.target));
    // "If you selected Bins to be copied, the 'Copy Bins to' option is available" (p.513).
    sameAsAbove_->setVisible(folder);
    sameAsAbove_->setEnabled(r.bins);
    setBins_->setEnabled(r.bins && !(folder && sameAsAbove_->isChecked()));
    binsPath_->setEnabled(r.bins);
    binsPath_->setText(QDir::toNativeSeparators(r.binsTarget));
    binsSpace_->setText(r.bins ? spaceText(r.binsTarget) : QString());
    ok_->setEnabled(canCopy());
}

} // namespace lumenis::ui
