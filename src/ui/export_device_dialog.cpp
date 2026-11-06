// The XDCAM and P2 Export Settings dialog boxes (lumenis/ui/export_device_dialog.h).
#include "lumenis/ui/export_device_dialog.h"

#include "lumenis/media/movie_export.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

ExportDeviceDialog::ExportDeviceDialog(const QString& kind, std::vector<std::shared_ptr<ext::ExportDeviceProvider>> devices,
                                       bool useMarks, bool useSelectedTracks, QWidget* parent)
    : QDialog(parent) {
    const bool p2 = kind == QLatin1String("p2");
    setObjectName(QStringLiteral("exportDeviceDialog"));
    // "The XDCAM Export Settings dialog box opens" (p.883); "The P2 Export Settings dialog box opens" (p.901).
    setWindowTitle(p2 ? tr("P2 Export Settings") : tr("XDCAM Export Settings"));
    for (const auto& device : devices) {
        if (!device || device->deviceKind() != kind.toStdString()) continue;
        for (const ext::ExportDeviceTarget& t : device->targets()) entries_.push_back({device, t});
    }
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    useMarks_ = new QCheckBox(tr("Use Marks"), this);
    useMarks_->setObjectName(QStringLiteral("deviceUseMarks"));
    useMarks_->setChecked(useMarks);
    useSelectedTracks_ = new QCheckBox(tr("Use Selected Tracks"), this);
    useSelectedTracks_->setObjectName(QStringLiteral("deviceUseSelectedTracks"));
    useSelectedTracks_->setChecked(useSelectedTracks);
    form->addRow(useMarks_);
    form->addRow(useSelectedTracks_);
    targets_ = new QComboBox(this);
    targets_->setObjectName(QStringLiteral("deviceTargets"));
    for (const Entry& e : entries_) targets_->addItem(QString::fromStdString(e.target.name));
    form->addRow(p2 ? tr("P2 Device:") : tr("Target XDCAM Disk:"), targets_);
    formats_ = new QComboBox(this);
    formats_->setObjectName(QStringLiteral("deviceFormats"));
    form->addRow(tr("Video Format:"), formats_);
    depths_ = new QComboBox(this);
    depths_->setObjectName(QStringLiteral("deviceBitDepths"));
    form->addRow(tr("Bit Depth:"), depths_);
    layout->addLayout(form);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("deviceNote"));
    note_->setWordWrap(true);
    layout->addWidget(note_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(targets_, &QComboBox::currentIndexChanged, this, [this] { fillFormats(); });
    fillFormats();
    if (entries_.empty()) {
        for (QWidget* w : {static_cast<QWidget*>(useMarks_), static_cast<QWidget*>(useSelectedTracks_), static_cast<QWidget*>(targets_),
                           static_cast<QWidget*>(formats_), static_cast<QWidget*>(depths_)})
            w->setEnabled(false);
        ok_->setEnabled(false);
        note_->setText(p2 ? tr("No P2 device is connected.") : tr("No XDCAM disk is connected."));
    }
}

void ExportDeviceDialog::fillFormats() {
    formats_->clear();
    depths_->clear();
    const auto chosen = device();
    if (!chosen) return;
    // The device's formats this build can write.
    for (const std::string& id : chosen->videoFormats()) {
        const media::MovieCompression* c = media::findMovieCompression(QString::fromStdString(id));
        if (!c || !media::movieCompressionAvailable(*c)) continue;
        formats_->addItem(c->label, c->id);
    }
    for (int depth : chosen->bitDepths()) depths_->addItem(tr("%1 bits").arg(depth), depth);
    const ext::ExportDeviceTarget t = target();
    note_->setText(t.audioChannels > 0 ? tr("The clips on this disk have %n audio track(s).", "", t.audioChannels) : QString());
}

std::shared_ptr<ext::ExportDeviceProvider> ExportDeviceDialog::device() const {
    const int i = targets_ ? targets_->currentIndex() : -1;
    return i >= 0 && i < static_cast<int>(entries_.size()) ? entries_[static_cast<std::size_t>(i)].device : nullptr;
}

ext::ExportDeviceTarget ExportDeviceDialog::target() const {
    const int i = targets_ ? targets_->currentIndex() : -1;
    return i >= 0 && i < static_cast<int>(entries_.size()) ? entries_[static_cast<std::size_t>(i)].target : ext::ExportDeviceTarget{};
}

QString ExportDeviceDialog::videoFormat() const { return formats_->currentData().toString(); }
int ExportDeviceDialog::bitDepth() const { return depths_->currentData().toInt(); }
bool ExportDeviceDialog::useMarks() const { return useMarks_->isChecked(); }
bool ExportDeviceDialog::useSelectedTracks() const { return useSelectedTracks_->isChecked(); }

bool ExportDeviceDialog::chooseTarget(const QString& name) {
    const int i = targets_->findText(name);
    if (i < 0) return false;
    targets_->setCurrentIndex(i);
    return true;
}

bool ExportDeviceDialog::chooseFormat(const QString& id) {
    const int i = formats_->findData(id);
    if (i < 0) return false;
    formats_->setCurrentIndex(i);
    return true;
}

} // namespace lumenis::ui
