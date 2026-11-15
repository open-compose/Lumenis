#include "lumenis/ui/consolidate_dialog.h"

#include "lumenis/ui/tool_windows.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

// The Consolidate/Transcode dialog and the Copying Media Files question (consolidate_dialog.h). The
// options map onto media::ConsolidateOptions (src/media/consolidate.h); the drives and resolutions come
// from src/media/managed.*. MainWindow (main_window.cpp) opens the dialog and runs the jobs.

namespace lumenis::ui {

namespace {

// Controls for features not implemented yet are shown disabled, with this tooltip.
QString laterTip() {
    return QObject::tr("Not available yet");
}

void later(QWidget* w) {
    w->setEnabled(false);
    w->setToolTip(laterTip());
}

// The drives, the one with the most space in bold (p.135).
QListWidget* driveList(QWidget* parent, const QString& name) {
    auto* list = new QListWidget(parent);
    list->setObjectName(name);
    const QList<media::MediaDrive> drives = media::mediaDrives();
    const int best = media::mostSpace(drives);
    for (int i = 0; i < drives.size(); ++i) {
        auto* item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(drives.at(i).name, freeSpaceText(drives.at(i).freeBytes)),
                                         list);
        item->setData(Qt::UserRole, drives.at(i).root);
        if (i == best) {
            QFont bold = list->font();
            bold.setBold(true);
            item->setFont(bold);
        }
    }
    if (best >= 0) list->setCurrentRow(best);
    list->setMinimumHeight(90);
    return list;
}

// The root folder of the drive chosen in `list`; empty if none is.
QString chosen(const QListWidget* list) {
    const QListWidgetItem* item = list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

} // namespace

// Lays out the whole dialog from top to bottom: mode, drives, handles and options, the Consolidate- or
// Transcode-only options, Audio, Processing Options and the buttons. update() then shows the parts
// for the mode.
ConsolidateDialog::ConsolidateDialog(const Rational& editRate, bool hasSequences, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("consolidateDialog"));
    setWindowTitle(tr("Consolidate/Transcode"));
    auto* layout = new QVBoxLayout(this);

    // Consolidate or Transcode, in the upper left corner (p.351, p.354).
    auto* modes = new QHBoxLayout;
    consolidate_ = new QRadioButton(tr("Consolidate"), this);
    consolidate_->setObjectName(QStringLiteral("consolidateMode"));
    transcode_ = new QRadioButton(tr("Transcode"), this);
    transcode_->setObjectName(QStringLiteral("transcodeMode"));
    auto* group = new QButtonGroup(this);
    group->addButton(consolidate_);
    group->addButton(transcode_);
    consolidate_->setChecked(true);
    modes->addWidget(consolidate_);
    modes->addWidget(transcode_);
    modes->addStretch();
    layout->addLayout(modes);

    // Target Drive(s).
    auto* drives = new QGroupBox(tr("Target Drive(s)"), this);
    auto* dv = new QVBoxLayout(drives);
    videoDrives_ = driveList(drives, QStringLiteral("targetDrives"));
    dv->addWidget(videoDrives_);
    audioLabel_ = new QLabel(tr("Audio:"), drives);
    dv->addWidget(audioLabel_);
    audioDrives_ = driveList(drives, QStringLiteral("audioDrives"));
    dv->addWidget(audioDrives_);
    sameDrive_ = new QCheckBox(tr("Video and audio on same drive(s)"), drives);
    sameDrive_->setObjectName(QStringLiteral("sameDrive"));
    sameDrive_->setChecked(true);
    dv->addWidget(sameDrive_);
    layout->addWidget(drives);

    // The options.
    auto* handleRow = new QHBoxLayout;
    handleRow->addWidget(new QLabel(tr("Handle length"), this));
    handles_ = new QSpinBox(this);
    handles_->setObjectName(QStringLiteral("handleLength"));
    handles_->setRange(0, 9999);
    handles_->setValue(static_cast<int>(media::defaultHandles(editRate)));
    handleRow->addWidget(handles_);
    handleRow->addWidget(new QLabel(tr("frames"), this));
    handleRow->addStretch();
    layout->addLayout(handleRow);
    newSequences_ = new QCheckBox(tr("Create new sequence(s)"), this);
    newSequences_->setObjectName(QStringLiteral("newSequences"));
    newSequences_->setEnabled(hasSequences);
    layout->addWidget(newSequences_);
    linkedOnly_ = new QCheckBox(this);
    linkedOnly_->setObjectName(QStringLiteral("linkedOnly"));
    layout->addWidget(linkedOnly_);

    consolidateBox_ = new QWidget(this);
    auto* cv = new QVBoxLayout(consolidateBox_);
    cv->setContentsMargins(0, 0, 0, 0);
    deleteOriginals_ = new QCheckBox(tr("Delete original media files when done"), consolidateBox_);
    deleteOriginals_->setObjectName(QStringLiteral("deleteOriginals"));
    cv->addWidget(deleteOriginals_);
    skipOnTarget_ = new QCheckBox(tr("Skip media files already on the target drive"), consolidateBox_);
    skipOnTarget_->setObjectName(QStringLiteral("skipOnTarget"));
    cv->addWidget(skipOnTarget_);
    // "This option appears when you select Skip media files already on the target drive" (p.352).
    relinkFirst_ = new QCheckBox(tr("Relink selected clips to target drive before skipping"), consolidateBox_);
    relinkFirst_->setObjectName(QStringLiteral("relinkBeforeSkipping"));
    cv->addWidget(relinkFirst_);
    connect(skipOnTarget_, &QCheckBox::toggled, this, [this] { update(); });
    groupEdits_ = new QCheckBox(tr("Consolidate all clips in a group edit"), consolidateBox_);
    groupEdits_->setObjectName(QStringLiteral("groupEdits"));
    cv->addWidget(groupEdits_);
    layout->addWidget(consolidateBox_);

    // Transcode: the resolution and raster (p.354-355).
    transcodeBox_ = new QWidget(this);
    auto* tv = new QVBoxLayout(transcodeBox_);
    tv->setContentsMargins(0, 0, 0, 0);
    auto* resRow = new QHBoxLayout;
    resRow->addWidget(new QLabel(tr("Target Video Resolution:"), transcodeBox_));
    resolution_ = new QComboBox(transcodeBox_);
    resolution_->setObjectName(QStringLiteral("targetResolution"));
    for (const media::Resolution& r : media::resolutions()) resolution_->addItem(r.name, r.id);
    resolution_->setCurrentIndex(resolution_->findData(media::defaultResolution().id));
    resRow->addWidget(resolution_);
    resRow->addStretch();
    tv->addLayout(resRow);
    auto* rasterRow = new QHBoxLayout;
    rasterRow->addWidget(new QLabel(tr("Raster Dimensions:"), transcodeBox_));
    raster_ = new QComboBox(transcodeBox_);
    raster_->setObjectName(QStringLiteral("rasterDimensions"));
    raster_->addItem(tr("Project dimensions (full raster)"), static_cast<int>(media::ConsolidateOptions::Raster::Project));
    raster_->addItem(tr("Source dimension"), static_cast<int>(media::ConsolidateOptions::Raster::Source));
    raster_->addItem(tr("Source 1/4"), static_cast<int>(media::ConsolidateOptions::Raster::SourceQuarter));
    raster_->addItem(tr("Source 1/16"), static_cast<int>(media::ConsolidateOptions::Raster::SourceSixteenth));
    rasterRow->addWidget(raster_);
    rasterRow->addStretch();
    tv->addLayout(rasterRow);
    // Keep Source's Frame Rate or Convert to Project Frame Rate (p.355); "this option will be reset after each transcode
    // operation": a new dialog starts with the source's.
    auto* rateRow = new QHBoxLayout;
    keepSourceRate_ = new QRadioButton(tr("Keep Source's Frame Rate"), transcodeBox_);
    keepSourceRate_->setObjectName(QStringLiteral("keepSourceRate"));
    keepSourceRate_->setChecked(true);
    convertProjectRate_ = new QRadioButton(tr("Convert to Project Frame Rate"), transcodeBox_);
    convertProjectRate_->setObjectName(QStringLiteral("convertProjectRate"));
    auto* rateGroup = new QButtonGroup(transcodeBox_);
    rateGroup->addButton(keepSourceRate_);
    rateGroup->addButton(convertProjectRate_);
    rateRow->addWidget(keepSourceRate_);
    rateRow->addWidget(convertProjectRate_);
    rateRow->addStretch();
    tv->addLayout(rateRow);
    // Apply source transformations (p.355, p.544): Color encoding, and Frame Flex shown as Framing (9.8).
    auto* transformsRow = new QHBoxLayout;
    transformsRow->addWidget(new QLabel(tr("Apply source transformations:"), transcodeBox_));
    applyColorEncoding_ = new QCheckBox(tr("Color encoding"), transcodeBox_);
    applyColorEncoding_->setObjectName(QStringLiteral("applyColorEncoding"));
    transformsRow->addWidget(applyColorEncoding_);
    applyFraming_ = new QCheckBox(tr("Framing"), transcodeBox_);
    applyFraming_->setObjectName(QStringLiteral("applyFraming"));
    transformsRow->addWidget(applyFraming_);
    transformsRow->addStretch();
    tv->addLayout(transformsRow);
    // Create new clips (p.354-355): on by default, as it is what happens without Dynamic Relink.
    newClips_ = new QCheckBox(tr("Create new clips"), transcodeBox_);
    newClips_->setObjectName(QStringLiteral("createNewClips"));
    newClips_->setChecked(true);
    newClips_->setEnabled(false);
    tv->addWidget(newClips_);
    layout->addWidget(transcodeBox_);

    // Audio: MXF OP-Atom PCM at 48 kHz; the bit depth can be chosen.
    auto* audio = new QGroupBox(tr("Audio"), this);
    auto* av = new QVBoxLayout(audio);
    // Convert Audio Sample Rate and Target Audio Sample Rate (p.352, p.355): the rates media is made at (isImportRate();
    // 48 kHz as MXF, the others as WAV, docs/spec/clip-audio.md).
    auto* sampleRow = new QHBoxLayout;
    convertSampleRate_ = new QCheckBox(tr("Convert Audio Sample Rate"), audio);
    convertSampleRate_->setObjectName(QStringLiteral("convertSampleRate"));
    convertSampleRate_->setChecked(true);
    sampleRow->addWidget(convertSampleRate_);
    sampleRate_ = new QComboBox(audio);
    sampleRate_->setObjectName(QStringLiteral("targetSampleRate"));
    for (const auto& [rate, text] : {std::pair{32000, tr("32 kHz")}, std::pair{44100, tr("44.1 kHz")}, std::pair{48000, tr("48 kHz")},
                                     std::pair{88200, tr("88.2 kHz")}, std::pair{96000, tr("96 kHz")}}) {
        sampleRate_->addItem(text, rate);
    }
    sampleRate_->setCurrentIndex(sampleRate_->findData(media::kManagedSampleRate));
    sampleRow->addWidget(sampleRate_);
    sampleRow->addStretch();
    av->addLayout(sampleRow);
    connect(convertSampleRate_, &QCheckBox::toggled, sampleRate_, &QWidget::setEnabled);
    auto* depthRow = new QHBoxLayout;
    convertBitDepth_ = new QCheckBox(tr("Convert Audio Bit Depth"), audio);
    convertBitDepth_->setObjectName(QStringLiteral("convertBitDepth"));
    convertBitDepth_->setChecked(true);
    depthRow->addWidget(convertBitDepth_);
    bitDepth_ = new QComboBox(audio);
    bitDepth_->setObjectName(QStringLiteral("targetBitDepth"));
    bitDepth_->addItem(tr("16 bit"), 16);
    bitDepth_->addItem(tr("24 bit"), 24);
    bitDepth_->setCurrentIndex(1);
    depthRow->addWidget(bitDepth_);
    depthRow->addStretch();
    av->addLayout(depthRow);
    auto* formatRow = new QHBoxLayout;
    auto* convertFormat = new QCheckBox(tr("Convert Audio Format"), audio);
    convertFormat->setChecked(true);
    later(convertFormat);
    formatRow->addWidget(convertFormat);
    auto* format = new QComboBox(audio);
    format->addItem(tr("MXF (PCM)"));
    later(format);
    formatRow->addWidget(format);
    formatRow->addStretch();
    av->addLayout(formatRow);
    layout->addWidget(audio);

    // Processing Options (p.352).
    auto* processing = new QGroupBox(tr("Processing Options"), this);
    auto* pv = new QHBoxLayout(processing);
    auto* application = new QRadioButton(tr("Application"), processing);
    application->setChecked(true);
    background_ = new QRadioButton(tr("Background Process"), processing);
    background_->setObjectName(QStringLiteral("backgroundProcess"));
    auto* background = background_;
    auto* distributed = new QRadioButton(tr("Distributed Processing"), processing);
    later(distributed);
    pv->addWidget(application);
    pv->addWidget(background);
    pv->addWidget(distributed);
    pv->addStretch();
    layout->addWidget(processing);

    // Consolidate or Transcode in the lower right corner (p.352).
    auto* buttons = new QDialogButtonBox(this);
    go_ = buttons->addButton(tr("Consolidate"), QDialogButtonBox::AcceptRole);
    go_->setObjectName(QStringLiteral("consolidateButton"));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(consolidate_, &QRadioButton::toggled, this, [this] { update(); });
    connect(sameDrive_, &QCheckBox::toggled, this, [this] { update(); });
    connect(videoDrives_, &QListWidget::currentRowChanged, this, [this] { update(); });
    update();
}

void ConsolidateDialog::update() {
    const bool transcode = transcode_->isChecked();
    linkedOnly_->setText(transcode ? tr("Transcode only linked media") : tr("Consolidate only linked media"));
    sameDrive_->setText(transcode ? tr("Video and audio on same drive(s)")
                                  : tr("Video, audio and data on same drive(s)"));
    go_->setText(transcode ? tr("Transcode") : tr("Consolidate"));
    transcodeBox_->setVisible(transcode);
    consolidateBox_->setVisible(!transcode);
    relinkFirst_->setVisible(skipOnTarget_->isChecked());
    const bool separate = !sameDrive_->isChecked();
    audioLabel_->setVisible(separate);
    audioDrives_->setVisible(separate);
    go_->setEnabled(videoDrives_->currentItem() != nullptr);
}

void ConsolidateDialog::setTranscode(bool transcode) {
    (transcode ? transcode_ : consolidate_)->setChecked(true);
    update();
}

void ConsolidateDialog::setBackground(bool background) {
    background_->setChecked(background);
}

bool ConsolidateDialog::isBackground() const {
    return background_->isChecked();
}

bool ConsolidateDialog::isTranscode() const {
    return transcode_->isChecked();
}

// The drive is resolved as the Media Creation setting is (media::chooseDrive): a missing one becomes
// the one with the most space.
void ConsolidateDialog::setDrive(const QString& drive) {
    const QString root = media::chooseDrive(drive);
    for (QListWidget* list : {videoDrives_, audioDrives_}) {
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(Qt::UserRole).toString() == root) list->setCurrentRow(i);
        }
    }
}

void ConsolidateDialog::setResolution(const QString& id) {
    const int i = resolution_->findData(id);
    if (i >= 0) resolution_->setCurrentIndex(i);
}

void ConsolidateDialog::setBitDepth(int bits) {
    bitDepth_->setCurrentIndex(bits <= 16 ? 0 : 1);
}

void ConsolidateDialog::setSampleRate(int rate) {
    const int i = sampleRate_->findData(rate);
    if (i >= 0) sampleRate_->setCurrentIndex(i);
}

// Options that do not apply are left off: new sequences without sequences, skipping files on the
// target when transcoding, and a separate audio drive when both go on the same drive.
media::ConsolidateOptions ConsolidateDialog::options() const {
    media::ConsolidateOptions o;
    o.mode = transcode_->isChecked() ? media::ConsolidateOptions::Mode::Transcode
                                     : media::ConsolidateOptions::Mode::Consolidate;
    o.handles = handles_->value();
    o.newSequences = newSequences_->isEnabled() && newSequences_->isChecked();
    o.linkedOnly = linkedOnly_->isChecked();
    o.skipOnTarget = !transcode_->isChecked() && skipOnTarget_->isChecked();
    o.relinkBeforeSkipping = o.skipOnTarget && relinkFirst_->isChecked();
    o.deleteOriginals = !transcode_->isChecked() && deleteOriginals_->isChecked();
    o.groupEdits = !transcode_->isChecked() && groupEdits_->isChecked();
    o.convertToProjectRate = transcode_->isChecked() && convertProjectRate_->isChecked();
    o.applyFraming = transcode_->isChecked() && applyFraming_->isChecked();
    o.convertSampleRate = convertSampleRate_->isChecked();
    o.creation.sampleRate = sampleRate_->currentData().toInt();
    o.raster = static_cast<media::ConsolidateOptions::Raster>(raster_->currentData().toInt());
    o.applyColorEncoding = transcode_->isChecked() && applyColorEncoding_->isChecked();
    o.newClips = !transcode_->isChecked() || !newClips_->isEnabled() || newClips_->isChecked();
    o.creation.videoDrive = chosen(videoDrives_);
    o.creation.audioDrive = sameDrive_->isChecked() ? QString() : chosen(audioDrives_);
    if (const media::Resolution* r = media::findResolution(resolution_->currentData().toString())) {
        o.creation.resolution = *r;
    }
    // Without converting, PCM stays at 24 bits, which holds 16-bit sound
    // as it is.
    o.creation.bitDepth = convertBitDepth_->isChecked() ? bitDepth_->currentData().toInt() : 24;
    return o;
}

void ConsolidateDialog::setDynamicRelink(bool on) {
    newClips_->setEnabled(on);
}

// A small modal dialog of two choices; Keep is the default.
std::optional<bool> ConsolidateDialog::askKeepMastersLinked(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("copyingMediaFiles"));
    dialog.setWindowTitle(tr("Copying Media Files"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("How should the master clips link to the media?"), &dialog));
    auto* keep = new QRadioButton(tr("Keep Master clips linked to media on the original drive"), &dialog);
    keep->setObjectName(QStringLiteral("keepLinked"));
    auto* relink = new QRadioButton(tr("Relink Master clips to media on the target drive"), &dialog);
    relink->setObjectName(QStringLiteral("relinkToTarget"));
    keep->setChecked(true);
    layout->addWidget(keep);
    layout->addWidget(relink);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return keep->isChecked();
}

} // namespace lumenis::ui
