// The Dynamic Media Folders window and the Profile Editor (Editing Guide p.372-376; 8.4c,
// docs/spec/background-services.md). Both edit copies of media::DynamicMediaFolders: the window says what changed with
// foldersChanged(), the editor gives its profile back with profile(). Section headings are buttons that open and close
// their section, as the guide's "Menu bars" do (p.372).
#include "lumenis/ui/dynamic_media_folders_window.h"

#include "lumenis/media/managed.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

using Kind = media::DmfAction::Kind;
const QList<Kind> kKinds{Kind::CopyToFolder, Kind::Consolidate, Kind::Transcode};

// The Default FPS menu's rates (ours: the project rates the Link setting offers).
const QList<std::pair<const char*, Rational>> kFps{
    {"23.976", Rational(24000, 1001)}, {"24", Rational(24, 1)},     {"25", Rational(25, 1)},
    {"29.97", Rational(30000, 1001)},  {"30", Rational(30, 1)},     {"47.95", Rational(48000, 1001)},
    {"48", Rational(48, 1)},           {"50", Rational(50, 1)},     {"59.94", Rational(60000, 1001)},
    {"60", Rational(60, 1)}};

QComboBox* combo(const QString& name, const QStringList& items) {
    auto* c = new QComboBox;
    c->setObjectName(name);
    c->addItems(items);
    return c;
}

QCheckBox* check(const QString& name, const QString& text) {
    auto* c = new QCheckBox(text);
    c->setObjectName(name);
    return c;
}

} // namespace

// ---------------------------------------------------------------- DmfProfileEditor

DmfProfileEditor::DmfProfileEditor(const media::DmfProfile& profile, const QStringList& drives, QWidget* parent)
    : QDialog(parent), profile_(profile) {
    setObjectName(QStringLiteral("dmfProfileEditor"));
    setWindowTitle(tr("Profile Editor"));
    build(drives);
    showProfile();
}

void DmfProfileEditor::build(const QStringList& drives) {
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* layout = new QVBoxLayout(body);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    // A menu bar and its section; the bars start open (ours), as the steps open each in turn.
    auto section = [&](const QString& key, const QString& title, QWidget* content) {
        auto* bar = new QToolButton;
        bar->setObjectName(key + QStringLiteral("Bar"));
        bar->setText(title);
        bar->setCheckable(true);
        bar->setChecked(true);
        bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        bar->setArrowType(Qt::DownArrow);
        bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(bar, &QToolButton::toggled, this, [this, key](bool on) { openSection(key, on); });
        layout->addWidget(bar);
        layout->addWidget(content);
        bars_.insert(key, bar);
        sections_.insert(key, content);
    };

    // Profile summary: the name and what the profile does, in words (ours).
    summary_ = new QLabel;
    summary_->setObjectName(QStringLiteral("summary"));
    static_cast<QLabel*>(summary_)->setWordWrap(true);
    section(QStringLiteral("summary"), tr("Profile Summary"), summary_);

    // Link Settings (p.373-374).
    auto* link = new QWidget;
    auto* form = new QFormLayout(link);
    plugin_ = combo(QStringLiteral("plugin"), {QStringLiteral("Autodetect"), media::linkPluginName()});
    form->addRow(tr("Link Using"), plugin_);
    bin_ = combo(QStringLiteral("bin"), {tr("Use active bin"), tr("Create a new bin")});
    form->addRow(tr("Bins"), bin_);
    naming_ = combo(QStringLiteral("naming"), {tr("Default bin naming convention"), tr("Volume name"), tr("Specify bin name")});
    form->addRow(QString(), naming_);
    binName_ = new QLineEdit;
    binName_->setObjectName(QStringLiteral("binName"));
    form->addRow(tr("Bin name"), binName_);
    multichannel_ = check(QStringLiteral("multichannel"), tr("Multichannel Audio"));
    trackFormats_ = new QLineEdit;
    trackFormats_->setObjectName(QStringLiteral("trackFormats"));
    trackFormats_->setPlaceholderText(QStringLiteral("Stereo A1A2"));
    form->addRow(multichannel_, trackFormats_);
    audioStartTime_ = check(QStringLiteral("audioStartTime"), tr("Audio Start-Time Option (for Broadcast Wave)"));
    form->addRow(tr("AIS"), audioStartTime_);
    reel_ = combo(QStringLiteral("reel"), {tr("Source file name"), tr("Source folder name")});
    reelLevel_ = new QSpinBox;
    reelLevel_->setObjectName(QStringLiteral("reelLevel"));
    reelLevel_->setRange(1, 8);
    auto* reelRow = new QHBoxLayout;
    reelRow->addWidget(reel_);
    reelRow->addWidget(reelLevel_);
    form->addRow(tr("Reel name for Labroll column based on:"), reelRow);
    frameCount_ = combo(QStringLiteral("frameCount"), {tr("Start frame count at 0"), tr("Start frame count at 1"),
                                                       tr("Convert timecode to frames"), tr("From the File name")});
    form->addRow(tr("Frame Count for DPX column:"), frameCount_);
    timecode_ = combo(QStringLiteral("timecode"), {tr("Embedded in source file"), tr("From file name")});
    form->addRow(tr("Timecode for Start column:"), timecode_);
    fps_ = new QComboBox;
    fps_->setObjectName(QStringLiteral("defaultFps"));
    for (const auto& [label, rate] : kFps) fps_->addItem(QString::fromLatin1(label));
    form->addRow(tr("Default FPS"), fps_);
    section(QStringLiteral("link"), tr("Link Settings"), link);

    // Actions (p.374-375): the list, Actions +, -, the selected row's Menu button and its options.
    auto* actions = new QWidget;
    auto* actionsLayout = new QVBoxLayout(actions);
    auto* buttons = new QHBoxLayout;
    auto* add = new QToolButton;
    add->setObjectName(QStringLiteral("addAction"));
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Add an action"));
    connect(add, &QToolButton::clicked, this, &DmfProfileEditor::addAction);
    auto* remove = new QToolButton;
    remove->setObjectName(QStringLiteral("removeAction"));
    remove->setText(QStringLiteral("-"));
    connect(remove, &QToolButton::clicked, this, [this] { removeAction(actions_->currentRow()); });
    actionMenu_ = new QToolButton;
    actionMenu_->setObjectName(QStringLiteral("actionMenu"));
    actionMenu_->setText(tr("Menu"));
    actionMenu_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(actionMenu_);
    for (Kind k : kKinds) {
        menu->addAction(media::DmfAction::kindName(k), this, [this, k] { setActionKind(actions_->currentRow(), k); });
    }
    actionMenu_->setMenu(menu);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addWidget(actionMenu_);
    buttons->addStretch();
    actionsLayout->addLayout(buttons);
    actions_ = new QListWidget;
    actions_->setObjectName(QStringLiteral("actions"));
    actions_->setDragDropMode(QAbstractItemView::InternalMove);
    actions_->setMaximumHeight(110);
    actionsLayout->addWidget(actions_);
    // A row moved by dragging: the profile's list follows the rows (each row keeps its action's index).
    connect(actions_->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (filling_) return;
        QList<media::DmfAction> order;
        for (int i = 0; i < actions_->count(); ++i) {
            const int index = actions_->item(i)->data(Qt::UserRole).toInt();
            if (index >= 0 && index < profile_.actions.size()) order << profile_.actions[index];
        }
        if (order.size() == profile_.actions.size()) profile_.actions = order;
        shownAction_ = -1;
        refreshRows();
    });
    connect(actions_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (filling_) return;
        storeAction(shownAction_);
        showAction(row);
    });

    actionPages_ = new QStackedWidget;
    // Copy to Folder.
    auto* copyPage = new QWidget;
    auto* copyForm = new QFormLayout(copyPage);
    copyTo_ = new QLineEdit;
    copyTo_->setObjectName(QStringLiteral("copyTo"));
    auto* set = new QPushButton(tr("Set"));
    set->setObjectName(QStringLiteral("setCopyTo"));
    connect(set, &QPushButton::clicked, this, [this] {
        const QString folder = folderChooser_ ? folderChooser_()
                                              : QFileDialog::getExistingDirectory(this, tr("Copy to"), copyTo_->text());
        if (!folder.isEmpty()) copyTo_->setText(folder);
    });
    auto* copyRow = new QHBoxLayout;
    copyRow->addWidget(copyTo_);
    copyRow->addWidget(set);
    copyForm->addRow(tr("Copy to:"), copyRow);
    autoRelink_ = check(QStringLiteral("autoRelink"), tr("Auto Relink when complete"));
    copyForm->addRow(autoRelink_);
    // Ours: no Production Management to check in to.
    checkin_ = check(QStringLiteral("checkin"), tr("Checkin to Production Management"));
    checkin_->setEnabled(false);
    copyForm->addRow(checkin_);
    actionPages_->addWidget(copyPage);
    // Consolidate and Transcode share one page; the kind's own options show for it.
    auto* mediaPage = new QWidget;
    auto* mediaForm = new QFormLayout(mediaPage);
    skipOnTarget_ = check(QStringLiteral("skipOnTarget"), tr("Skip media files already on the target drive"));
    relinkBeforeSkip_ = check(QStringLiteral("relinkBeforeSkip"), tr("Relink selected clips to target drive before skipping"));
    mediaForm->addRow(skipOnTarget_);
    mediaForm->addRow(relinkBeforeSkip_);
    resolution_ = new QComboBox;
    resolution_->setObjectName(QStringLiteral("resolution"));
    resolution_->addItem(tr("Media Creation setting"), QString());
    for (const media::Resolution& r : media::resolutions()) resolution_->addItem(r.name, r.id);
    mediaForm->addRow(tr("Video Resolution"), resolution_);
    reformat_ = check(QStringLiteral("reformat"), tr("Apply Reformatting option (compatibility mode)"));
    colorTransforms_ = check(QStringLiteral("colorTransforms"), tr("Apply color transformations"));
    mediaForm->addRow(reformat_);
    mediaForm->addRow(colorTransforms_);
    sampleRate_ = combo(QStringLiteral("sampleRate"), {tr("Keep"), QStringLiteral("32 kHz"), QStringLiteral("44.1 kHz"), QStringLiteral("48 kHz")});
    mediaForm->addRow(tr("Convert Audio Sample Rate"), sampleRate_);
    bitDepth_ = combo(QStringLiteral("bitDepth"), {tr("Keep"), tr("16 Bit"), tr("24 Bit")});
    mediaForm->addRow(tr("Convert Audio Bit Depth"), bitDepth_);
    audioFormat_ = combo(QStringLiteral("audioFormat"), {tr("Keep"), QStringLiteral("OMF (WAVE)"), QStringLiteral("OMF (AIFF-C)"), QStringLiteral("MXF (PCM)")});
    mediaForm->addRow(tr("Convert Audio Format"), audioFormat_);
    videoDrive_ = new QComboBox;
    videoDrive_->setObjectName(QStringLiteral("videoDrive"));
    audioDrive_ = new QComboBox;
    audioDrive_->setObjectName(QStringLiteral("audioDrive"));
    for (QComboBox* c : {videoDrive_, audioDrive_}) {
        c->addItem(tr("Media Creation setting"), QString());
        for (const QString& d : drives) c->addItem(d, d);
    }
    mediaForm->addRow(tr("Video Drive"), videoDrive_);
    mediaForm->addRow(tr("Audio Drive"), audioDrive_);
    actionPages_->addWidget(mediaPage);
    actionPages_->addWidget(new QWidget); // no action selected
    actionsLayout->addWidget(actionPages_);
    section(QStringLiteral("actions"), tr("Actions"), actions);
    layout->addStretch();

    auto* bottom = new QHBoxLayout;
    auto* saveButton = new QPushButton(tr("Save"));
    saveButton->setObjectName(QStringLiteral("save"));
    connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
    auto* cancel = new QPushButton(tr("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addStretch();
    bottom->addWidget(cancel);
    bottom->addWidget(saveButton);
    outer->addLayout(bottom);

    // The controls that only apply to some choices.
    auto follow = [this] {
        naming_->setEnabled(bin_->currentIndex() == 1);
        binName_->setEnabled(bin_->currentIndex() == 1 && naming_->currentIndex() == 2);
        trackFormats_->setEnabled(multichannel_->isChecked());
        // "When Source folder name is enabled, a pulldown menu becomes active" (p.373).
        reelLevel_->setEnabled(reel_->currentIndex() == 1);
        refreshSummary();
    };
    connect(bin_, &QComboBox::currentIndexChanged, this, follow);
    connect(naming_, &QComboBox::currentIndexChanged, this, follow);
    connect(multichannel_, &QCheckBox::toggled, this, follow);
    connect(reel_, &QComboBox::currentIndexChanged, this, follow);
    follow();
    resize(520, 640);
}

void DmfProfileEditor::openSection(const QString& section, bool open) {
    QWidget* w = sections_.value(section);
    QToolButton* bar = bars_.value(section);
    if (!w || !bar) return;
    w->setVisible(open);
    bar->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    if (bar->isChecked() != open) bar->setChecked(open);
}

bool DmfProfileEditor::isSectionOpen(const QString& section) const {
    const QToolButton* bar = bars_.value(section);
    return bar && bar->isChecked();
}

void DmfProfileEditor::showProfile() {
    filling_ = true;
    const media::DmfLinkSettings& l = profile_.link;
    plugin_->setCurrentIndex(l.plugin == QLatin1String("Autodetect") ? 0 : 1);
    bin_->setCurrentIndex(l.bin == media::DmfLinkSettings::Bin::Active ? 0 : 1);
    naming_->setCurrentIndex(static_cast<int>(l.naming));
    binName_->setText(l.binName);
    multichannel_->setChecked(l.multichannel);
    trackFormats_->setText(l.trackFormats);
    audioStartTime_->setChecked(l.audioStartTime);
    reel_->setCurrentIndex(static_cast<int>(l.reel));
    reelLevel_->setValue(l.reelLevel);
    frameCount_->setCurrentIndex(static_cast<int>(l.frameCount));
    timecode_->setCurrentIndex(static_cast<int>(l.timecode));
    for (int i = 0; i < kFps.size(); ++i) {
        if (kFps[i].second == l.defaultFps) fps_->setCurrentIndex(i);
    }
    filling_ = false;
    refreshRows();
    selectAction(profile_.actions.isEmpty() ? -1 : 0);
}

void DmfProfileEditor::refreshRows() {
    filling_ = true;
    const int current = actions_->currentRow();
    actions_->clear();
    for (int i = 0; i < profile_.actions.size(); ++i) {
        const media::DmfAction& a = profile_.actions[i];
        QString text = media::DmfAction::kindName(a.kind);
        if (a.kind == Kind::CopyToFolder && !a.copyTo.isEmpty()) text += QStringLiteral(": ") + a.copyTo;
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, i);
        actions_->addItem(item);
    }
    actions_->setCurrentRow(std::min(current, static_cast<int>(actions_->count()) - 1));
    filling_ = false;
    refreshSummary();
}

void DmfProfileEditor::refreshSummary() {
    // In words: where clips go and what is done, in order (ours).
    const media::DmfProfile p = profile();
    QStringList steps;
    steps << (p.link.bin == media::DmfLinkSettings::Bin::Active ? tr("Links into the active bin")
                                                                 : tr("Links into a new bin"));
    for (const media::DmfAction& a : p.actions) steps << media::DmfAction::kindName(a.kind);
    static_cast<QLabel*>(summary_)->setText((p.name.isEmpty() ? tr("New profile") : p.name) + QStringLiteral(": ") +
                                            steps.join(QStringLiteral(", then ")));
}

void DmfProfileEditor::showAction(int row) {
    shownAction_ = row;
    if (row < 0 || row >= profile_.actions.size()) {
        actionPages_->setCurrentIndex(2);
        actionMenu_->setEnabled(false);
        return;
    }
    filling_ = true;
    actionMenu_->setEnabled(true);
    const media::DmfAction& a = profile_.actions[row];
    actionPages_->setCurrentIndex(a.kind == Kind::CopyToFolder ? 0 : 1);
    copyTo_->setText(a.copyTo);
    autoRelink_->setChecked(a.autoRelink);
    checkin_->setChecked(a.checkin);
    const bool transcode = a.kind == Kind::Transcode;
    skipOnTarget_->setVisible(!transcode);
    relinkBeforeSkip_->setVisible(!transcode);
    resolution_->setVisible(transcode);
    reformat_->setVisible(transcode);
    colorTransforms_->setVisible(transcode);
    skipOnTarget_->setChecked(a.skipOnTarget);
    relinkBeforeSkip_->setChecked(a.relinkBeforeSkip);
    resolution_->setCurrentIndex(std::max(0, resolution_->findData(a.resolution)));
    reformat_->setChecked(a.reformat);
    colorTransforms_->setChecked(a.colorTransforms);
    sampleRate_->setCurrentIndex(a.sampleRate == 32000 ? 1 : a.sampleRate == 44100 ? 2 : a.sampleRate == 48000 ? 3 : 0);
    bitDepth_->setCurrentIndex(a.bitDepth == 16 ? 1 : a.bitDepth == 24 ? 2 : 0);
    audioFormat_->setCurrentIndex(std::max(0, audioFormat_->findText(a.audioFormat)));
    videoDrive_->setCurrentIndex(std::max(0, videoDrive_->findData(a.videoDrive)));
    audioDrive_->setCurrentIndex(std::max(0, audioDrive_->findData(a.audioDrive)));
    filling_ = false;
}

void DmfProfileEditor::storeAction(int row) {
    if (row < 0 || row >= profile_.actions.size()) return;
    media::DmfAction& a = profile_.actions[row];
    if (a.kind == Kind::CopyToFolder) {
        a.copyTo = copyTo_->text().trimmed();
        a.autoRelink = autoRelink_->isChecked();
        a.checkin = checkin_->isChecked();
        return;
    }
    if (a.kind == Kind::Consolidate) {
        a.skipOnTarget = skipOnTarget_->isChecked();
        a.relinkBeforeSkip = relinkBeforeSkip_->isChecked();
    } else {
        a.resolution = resolution_->currentData().toString();
        a.reformat = reformat_->isChecked();
        a.colorTransforms = colorTransforms_->isChecked();
    }
    static const int rates[] = {0, 32000, 44100, 48000};
    static const int depths[] = {0, 16, 24};
    a.sampleRate = rates[std::clamp(sampleRate_->currentIndex(), 0, 3)];
    a.bitDepth = depths[std::clamp(bitDepth_->currentIndex(), 0, 2)];
    a.audioFormat = audioFormat_->currentIndex() > 0 ? audioFormat_->currentText() : QString();
    a.videoDrive = videoDrive_->currentData().toString();
    a.audioDrive = audioDrive_->currentData().toString();
}

media::DmfProfile DmfProfileEditor::profile() const {
    media::DmfProfile p = profile_;
    // The shown action's controls are read without changing the editor (this is const).
    const_cast<DmfProfileEditor*>(this)->storeAction(shownAction_);
    p.actions = profile_.actions;
    media::DmfLinkSettings& l = p.link;
    l.plugin = plugin_->currentText();
    l.bin = bin_->currentIndex() == 0 ? media::DmfLinkSettings::Bin::Active : media::DmfLinkSettings::Bin::New;
    l.naming = static_cast<media::DmfLinkSettings::Naming>(naming_->currentIndex());
    l.binName = binName_->text().trimmed();
    l.multichannel = multichannel_->isChecked();
    l.trackFormats = trackFormats_->text().trimmed();
    l.audioStartTime = audioStartTime_->isChecked();
    l.reel = static_cast<media::DmfLinkSettings::Reel>(reel_->currentIndex());
    l.reelLevel = reelLevel_->value();
    l.frameCount = static_cast<media::DmfLinkSettings::FrameCount>(frameCount_->currentIndex());
    l.timecode = static_cast<media::DmfLinkSettings::Timecode>(timecode_->currentIndex());
    l.defaultFps = kFps[std::clamp(fps_->currentIndex(), 0, static_cast<int>(kFps.size()) - 1)].second;
    return p;
}

void DmfProfileEditor::addAction() {
    storeAction(shownAction_);
    media::DmfAction a;
    profile_.actions << a;
    refreshRows();
    selectAction(static_cast<int>(profile_.actions.size()) - 1);
}

void DmfProfileEditor::setActionKind(int row, Kind kind) {
    if (row < 0 || row >= profile_.actions.size()) return;
    storeAction(shownAction_);
    // A new kind starts from that kind's defaults.
    media::DmfAction a;
    a.kind = kind;
    profile_.actions[row] = a;
    shownAction_ = -1;
    refreshRows();
    selectAction(row);
}

void DmfProfileEditor::moveAction(int from, int to) {
    if (from < 0 || from >= profile_.actions.size() || to < 0 || to >= profile_.actions.size() || from == to) return;
    storeAction(shownAction_);
    profile_.actions.move(from, to);
    shownAction_ = -1;
    refreshRows();
    selectAction(to);
}

void DmfProfileEditor::removeAction(int row) {
    if (row < 0 || row >= profile_.actions.size()) return;
    storeAction(shownAction_);
    profile_.actions.removeAt(row);
    shownAction_ = -1;
    refreshRows();
    selectAction(std::min(row, static_cast<int>(profile_.actions.size()) - 1));
}

void DmfProfileEditor::selectAction(int row) {
    filling_ = true;
    actions_->setCurrentRow(row);
    filling_ = false;
    showAction(row);
}

bool DmfProfileEditor::save() {
    // "Click Save to save the Profile. Name the Profile and click OK" (p.375).
    const QString current = profile_.name;
    QString name;
    if (nameChooser_) {
        name = nameChooser_(current);
    } else {
        bool ok = false;
        name = QInputDialog::getText(this, tr("Save Profile"), tr("Profile name:"), QLineEdit::Normal, current, &ok);
        if (!ok) return false;
    }
    name = name.trimmed();
    if (name.isEmpty()) return false;
    storeAction(shownAction_);
    profile_ = profile();
    profile_.name = name;
    accept();
    return true;
}

// ---------------------------------------------------------------- DynamicMediaFoldersWindow

DynamicMediaFoldersWindow::DynamicMediaFoldersWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("dynamicMediaFolders"));
    setWindowTitle(tr("Dynamic Media Folders"));
    auto* layout = new QVBoxLayout(this);
    auto* buttons = new QHBoxLayout;
    auto* add = new QToolButton;
    add->setObjectName(QStringLiteral("addFolder"));
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Add a Dynamic Media Folder"));
    connect(add, &QToolButton::clicked, this, &DynamicMediaFoldersWindow::addFolder);
    remove_ = new QPushButton(QStringLiteral("-"));
    remove_->setObjectName(QStringLiteral("removeFolder"));
    connect(remove_, &QPushButton::clicked, this, &DynamicMediaFoldersWindow::removeSelectedFolder);
    profileEditor_ = new QPushButton(tr("Profile Editor"));
    profileEditor_->setObjectName(QStringLiteral("profileEditor"));
    connect(profileEditor_, &QPushButton::clicked, this, [this] {
        const int row = selectedFolder();
        if (row < 0) return;
        const media::DmfFolder& f = folders_.folders[row];
        emit profileEditorRequested(f.path, f.profile);
    });
    buttons->addWidget(add);
    buttons->addWidget(remove_);
    buttons->addStretch();
    buttons->addWidget(profileEditor_);
    layout->addLayout(buttons);
    list_ = new QTreeWidget;
    list_->setObjectName(QStringLiteral("folders"));
    list_->setRootIsDecorated(false);
    list_->setHeaderLabels({tr("Enable"), tr("Folder"), tr("Profile"), tr("Acquire")});
    list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] {
        profileEditor_->setEnabled(selectedFolder() >= 0);
        remove_->setEnabled(selectedFolder() >= 0);
    });
    connect(list_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (filling_ || column != 0) return;
        setEnabled(list_->indexOfTopLevelItem(item), item->checkState(0) == Qt::Checked);
    });
    layout->addWidget(list_);
    resize(640, 260);
    refresh();
}

void DynamicMediaFoldersWindow::setFolders(const media::DynamicMediaFolders& folders, const QHash<QString, AssetCount>& assets) {
    folders_ = folders;
    assets_ = assets;
    refresh();
}

void DynamicMediaFoldersWindow::refresh() {
    filling_ = true;
    const int selected = selectedFolder();
    list_->clear();
    for (int row = 0; row < folders_.folders.size(); ++row) {
        const media::DmfFolder& f = folders_.folders[row];
        auto* item = new QTreeWidgetItem(list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, f.enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(1, QDir::toNativeSeparators(f.path));
        // The profile menu (p.375, "choosing the profile from the drop down list").
        auto* profile = new QComboBox;
        profile->setObjectName(QStringLiteral("profile"));
        profile->addItem(tr("None"), QString());
        for (const media::DmfProfile& p : folders_.profiles) profile->addItem(p.name, p.name);
        profile->setCurrentIndex(std::max(0, profile->findData(f.profile)));
        connect(profile, &QComboBox::activated, this, [this, row, profile] { setProfile(row, profile->currentData().toString()); });
        list_->setItemWidget(item, 2, profile);
        // The Acquire icon: green when assets are ready (p.376), dimmed while they are still being processed.
        const AssetCount count = assets_.value(QDir::cleanPath(f.path));
        auto* acquire = new QPushButton;
        acquire->setObjectName(QStringLiteral("acquire"));
        acquire->setFlat(true);
        acquire->setEnabled(count.ready + count.processing > 0);
        acquire->setText(count.ready > 0 ? QStringLiteral("● %1").arg(count.ready)
                                         : count.processing > 0 ? QStringLiteral("○") : QString());
        if (count.ready > 0) acquire->setStyleSheet(QStringLiteral("color: #3cb043;"));
        acquire->setToolTip(count.ready > 0 ? tr("%n asset(s) to acquire", "", count.ready)
                                            : count.processing > 0 ? tr("%n asset(s) being processed", "", count.processing)
                                                                   : QString());
        const QString path = f.path;
        connect(acquire, &QPushButton::clicked, this, [this, path] { emit acquireRequested(path); });
        list_->setItemWidget(item, 3, acquire);
    }
    filling_ = false;
    selectFolder(std::min(selected, static_cast<int>(folders_.folders.size()) - 1));
    profileEditor_->setEnabled(selectedFolder() >= 0);
    remove_->setEnabled(selectedFolder() >= 0);
}

void DynamicMediaFoldersWindow::addFolder() {
    // "Navigate to the folder on which you want to perform the action and click Choose. A DMF is added to the list"
    // (p.372).
    const QString folder = folderChooser_ ? folderChooser_() : QFileDialog::getExistingDirectory(this, tr("Select Folder"));
    if (folder.isEmpty() || !folders_.addFolder(folder)) return;
    refresh();
    selectFolder(static_cast<int>(folders_.folders.size()) - 1);
    emit foldersChanged(folders_);
}

void DynamicMediaFoldersWindow::removeSelectedFolder() {
    const int row = selectedFolder();
    if (row < 0) return;
    folders_.removeFolder(folders_.folders[row].path);
    refresh();
    emit foldersChanged(folders_);
}

void DynamicMediaFoldersWindow::selectFolder(int row) {
    if (row < 0 || row >= list_->topLevelItemCount()) {
        list_->clearSelection();
        return;
    }
    list_->setCurrentItem(list_->topLevelItem(row));
}

int DynamicMediaFoldersWindow::selectedFolder() const {
    QTreeWidgetItem* item = list_->currentItem();
    return item && item->isSelected() ? list_->indexOfTopLevelItem(item) : -1;
}

void DynamicMediaFoldersWindow::setEnabled(int row, bool enabled) {
    if (row < 0 || row >= folders_.folders.size() || folders_.folders[row].enabled == enabled) return;
    folders_.folders[row].enabled = enabled;
    refresh();
    emit foldersChanged(folders_);
}

void DynamicMediaFoldersWindow::setProfile(int row, const QString& profile) {
    if (row < 0 || row >= folders_.folders.size() || folders_.folders[row].profile == profile) return;
    folders_.folders[row].profile = profile;
    refresh();
    emit foldersChanged(folders_);
}

QPushButton* DynamicMediaFoldersWindow::acquireButton(int row) const {
    QTreeWidgetItem* item = list_->topLevelItem(row);
    return item ? qobject_cast<QPushButton*>(list_->itemWidget(item, 3)) : nullptr;
}

} // namespace lumenis::ui
