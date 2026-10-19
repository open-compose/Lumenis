#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/ui/main_window.h"

#include "lumenis/media/consolidate.h"
#include "lumenis/media/playback.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/full_screen.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/consolidate_dialog.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/keyboard_palette.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/relink_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QPixmap>
#include <QProgressDialog>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

// MainWindow: the application frame and the place where the parts of the editor are wired together.
// It owns the panel framework (PanelManager, panels.*) and the workspaces (workspace*), creates every
// panel and tool (createPanels, createTools), connects the Composer, Timeline, bins and tools to each
// other, and gives the commands declared in resources/commands.json their handlers
// (installHandlers, installPanelHandlers, installProjectHandlers). A command without a handler stays
// in the menus but disabled (CommandRegistry::setHandler), which is how project-only commands turn
// off when no project is open.
//
// It also carries the work that spans several parts and so belongs to no single panel:
//   - settings: applySettings() pushes every setting (settings.*, resources/settings.json) into the
//     windows whenever Settings::changed fires; workspaces are stored as User settings;
//   - playback sound and digital audio scrub, through media::AudioPlayer (src/media/playback.*);
//   - linking, importing, AAF import and export, relinking, Consolidate/Transcode and the media
//     database (src/media/link.*, managed.*, consolidate.*, relink.*, src/interop/aaf_*).
// The menus themselves are built by buildBuiltinMenus (menu_builder.*) from menus.json.
namespace lumenis::ui {

// Interface settings, Show ToolTips (p.1200): with it off, no tooltips.
class ToolTipFilter : public QObject {
public:
    using QObject::QObject;
    bool enabled = true;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!enabled && event->type() == QEvent::ToolTip) return true;
        return QObject::eventFilter(watched, event);
    }
};

namespace {

// Building blocks of a panel layout as PanelManager::restoreLayout reads it: a tab group holding one
// panel, and a split of children with relative sizes ("h" side by side, "v" one above the other).
QJsonObject tabs(const QString& id) {
    return {{QStringLiteral("tabs"), QJsonArray{id}}, {QStringLiteral("current"), 0}};
}

QJsonObject split(const char* orientation, const QJsonArray& sizes, const QJsonArray& children) {
    return {{QStringLiteral("split"), QLatin1String(orientation)},
            {QStringLiteral("sizes"), sizes},
            {QStringLiteral("children"), children}};
}

} // namespace

// The order matters: createTools() uses the Composer, Timeline and KeyboardMap made by createPanels();
// the bin and input menus are handed to the Bin Container only once buildBuiltinMenus() has made them;
// and the default layout is restored once every panel exists.
MainWindow::MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers, QWidget* parent)
    : QMainWindow(parent), commands_(commands), providers_(providers) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    resize(1280, 800);

    // Every workspace has the same default layout (p.74).
    panels_ = new PanelManager(this);
    workspaces_ = new WorkspaceManager(*panels_, this);
    for (const QString& name : workspaces_->names()) {
        workspaces_->setDefaultLayout(name, defaultLayout());
    }

    // The central area: the docked panels, with the workspace toolbar along its right edge.
    auto* area = new QWidget(this);
    area->setObjectName(QStringLiteral("WorkspaceArea"));
    auto* layout = new QHBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(panels_->mainHost(), 1);
    bar_ = new WorkspaceBar(*workspaces_, area);
    layout->addWidget(bar_);
    setCentralWidget(area);

    // With the toolbar hidden, a workspace button at the top right brings
    // up the workspace menu (p.88-89).
    workspaceButton_ = new QToolButton(area);
    workspaceButton_->setObjectName(QStringLiteral("workspaceButton"));
    workspaceButton_->setPopupMode(QToolButton::InstantPopup);
    workspaceButton_->setAutoRaise(true);
    workspaceButton_->setFixedSize(26, kToolButtonHeight);
    workspaceButton_->setMenu(new QMenu(workspaceButton_));
    area->installEventFilter(this);

    // Pictures of linked clips, for the monitors, bins and Source Browser;
    // sound for playback.
    frames_ = new MediaFrames(this);
    audio_ = std::make_unique<media::AudioPlayer>();
    createPanels();
    createTools();
    settings_ = new Settings(this);
    toolTips_ = new ToolTipFilter(this);
    qApp->installEventFilter(toolTips_);
    connect(settings_, &Settings::changed, this, &MainWindow::applySettings);
    applySettings();
    installHandlers();
    buildBuiltinMenus(menuBar(), commands_);
    installPanelHandlers();
    binContainer_->setBinMenu(menuBar()->findChild<QMenu*>(QStringLiteral("menu:bin")));
    binContainer_->setInputMenu(menuBar()->findChild<QMenu*>(QStringLiteral("menu:file_input")));
    installProjectHandlers(false);
    panels_->restoreLayout(defaultLayout());
    // Workspaces follow the User settings: written back whenever they change, read again when another
    // user's settings load, and a switch applies the new workspace's Link to Named settings.
    connect(workspaces_, &WorkspaceManager::changed, this, &MainWindow::syncWorkspaceUi);
    connect(workspaces_, &WorkspaceManager::changed, this, [this] {
        writeWorkspaces();
        updateWorkspaceButtons();
        if (workspaces_->active() != linkedWorkspace_) linkSettings(workspaces_->active());
    });
    connect(settings_, &Settings::aboutToChangeUser, this, &MainWindow::writeWorkspaces);
    connect(settings_, &Settings::userChanged, this, &MainWindow::readWorkspaces);
    syncWorkspaceUi();
}

MainWindow::~MainWindow() {
    // An edit in progress (a bin being named) is committed now, while every
    // view is alive, not by the focus change as the window comes down.
    if (QWidget* focused = QApplication::focusWidget(); focused && isAncestorOf(focused)) focused->clearFocus();
    // The sound goes before the Composer (a child, deleted later): it must
    // not reach the player as it stops.
    if (composer_) {
        disconnect(composer_, nullptr, this, nullptr);
        composer_->setPlayClock({});
    }
    if (audio_) audio_->stop();
    // The mapper outlives this window's commands.
    if (ButtonMapper::instance().registry() == &commands_) ButtonMapper::instance().setRegistry(nullptr);
    commands_.setCapture({});
}

QJsonObject MainWindow::defaultLayout() {
    // Proportions measured from the picture on p.74.
    const QJsonObject top = split("h", {38, 62}, {tabs(QStringLiteral("BinContainer")), tabs(QStringLiteral("Composer"))});
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("main"), split("v", {52, 48}, {top, tabs(QStringLiteral("Timeline"))})},
            {QStringLiteral("floating"), QJsonArray()},
            {QStringLiteral("active"), QStringLiteral("Composer")},
            {QStringLiteral("blankHomes"), QJsonObject()}};
}

// Creates the main panels (Bin Container, Project, Composer, Timeline) and connects them: the
// Composer's resolvers find bin items and media frames, its play signals drive the sound, and the
// Timeline stores its sequences in bins. The tools come after, in createTools().
void MainWindow::createPanels() {
    // Its tab reads "Bins" in the pictures on p.240-242.
    binContainer_ = new BinContainer;
    panels_->addPanel(new Panel(QStringLiteral("BinContainer"), tr("Bins"), binContainer_));
    // Tools > Project: a Sidebar-only Bin Container named Project (p.243). As
    // a panel it takes part in workspaces, floating or not.
    panels_->addPanel(new Panel(QStringLiteral("Project"), tr("Project"), new BinSidebar(*binContainer_)));
    // The Composer window (ch. 13).
    composer_ = new Composer;
    // Bin items are looked up in the bins open in the Bin Container; items in closed bins are not found.
    composer_->setItemResolver([this](const QString& binId, const QString& itemId) -> std::optional<model::BinItem> {
        const BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
        if (!item) return std::nullopt;
        return *item;
    });
    // The monitors show the frames of linked clips; a sequence shows its top
    // video track's (p.432-434 orders the tracks the same way).
    composer_->setPictureResolver([this](MonitorId monitor, QSize size, bool* offline) -> QImage {
        const model::MonitorState& state = composer_->state(monitor);
        const auto shown = frameAt(monitor, state.position());
        if (!shown) return {};
        const auto& [item, frame] = *shown;
        if (frames_->isOffline(item)) {
            if (offline) *offline = true;
            return {};
        }
        // Playing: the next frames are decoded while this one shows.
        if (composer_->isPlaying() && monitor == composer_->activeMonitor()) {
            const int step = composer_->speed() > 0 ? 1 : -1;
            for (int k = 1; k <= 2; ++k) {
                if (const auto next = frameAt(monitor, state.position() + k * step * std::abs(composer_->speed()))) {
                    frames_->prefetch(next->first, next->second, size);
                }
            }
        }
        return frames_->frame(item, frame, size);
    });
    // Sound: at normal speed forward the monitor's audio plays, and its
    // progress paces the picture (A/V sync).
    connect(composer_, &Composer::playStarted, this, [this](MonitorId monitor, int speed) { startSound(monitor, speed); });
    connect(composer_, &Composer::playStopped, this, [this] {
        audio_->stop();
        // Frames the picture could not keep up with (the real-time gate).
        const auto& stats = composer_->playStats();
        if (stats.dropped > 0) {
            ConsoleLog::instance().append(
                tr("Playback: %1 frames shown, %2 dropped.").arg(stats.shown).arg(stats.dropped));
        }
    });
    // The sound's clock in program time: faster or backward scrub moves
    // through the program that many times as fast (p.675).
    composer_->setPlayClock([this]() -> std::optional<double> {
        if (!audio_->isPlaying() || scrubbing_) return std::nullopt;
        return static_cast<double>(audio_->clock()) * audio_->speed() / media::kPlaybackRate;
    });
    // Digital audio scrub (p.676): moving the position while stopped plays
    // the frames around it, with Shift held or the Digital Audio Scrub
    // button on.
    connect(composer_, &Composer::changed, this, [this] { scrubIfMoved(); });
    // A clip loaded from the Source Browser goes into the target bin when it
    // is edited into a sequence (p.316).
    composer_->setCommitter([this](const model::BinItem& item) -> QString {
        BinDocument* doc = nullptr;
        if (sourceBrowser_ && !sourceBrowser_->targetBin().isEmpty()) {
            doc = binContainer_->document(sourceBrowser_->targetBin());
            if (!doc) doc = binContainer_->openBin(sourceBrowser_->targetBin());
        }
        if (!doc) doc = binContainer_->activeDocument();
        if (!doc) return {};
        doc->addItem(item);
        return doc->nodeId();
    });
    connect(composer_, &Composer::filesDropped, this, [this](MonitorId monitor, const QStringList& paths) {
        loadFiles(paths, monitor == MonitorId::Record);
    });
    // Deleting managed clips can delete their media files too (p.359).
    // The filter returns the ids the bin goes on to delete: all of them when no managed media is among
    // them, none when only the media files go, and nothing (std::nullopt) on Cancel.
    binContainer_->setDeleteFilter([this](const BinDocument& doc, const QStringList& ids) -> std::optional<QStringList> {
        QStringList files;
        int clips = 0;
        for (const QString& id : ids) {
            const model::BinItem* item = doc.bin().item(id);
            const auto link = item ? media::linkOf(*item) : std::nullopt;
            if (!link || !link->isManaged()) continue;
            ++clips;
            files += link->files();
        }
        files.removeDuplicates();
        if (files.isEmpty()) return ids;
        BinDeleteChoice choice;
        if (binDeleteHook_) {
            const auto answer = binDeleteHook_(static_cast<int>(ids.size()), static_cast<int>(files.size()));
            if (!answer) return std::nullopt;
            choice = *answer;
        } else {
            QDialog dialog(this);
            dialog.setObjectName(QStringLiteral("deleteDialog"));
            dialog.setWindowTitle(tr("Delete"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* clipBox = new QCheckBox(tr("Delete %n clip(s)", "", static_cast<int>(ids.size())), &dialog);
            clipBox->setChecked(true);
            auto* mediaBox = new QCheckBox(tr("Delete %n media file(s)", "", static_cast<int>(files.size())), &dialog);
            layout->addWidget(new QLabel(tr("%n of the clips have media files:", "", clips), &dialog));
            layout->addWidget(clipBox);
            layout->addWidget(mediaBox);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() != QDialog::Accepted) return std::nullopt;
            choice = {clipBox->isChecked(), mediaBox->isChecked()};
        }
        if (choice.media) {
            frames_->clear();
            media::deleteMediaFiles(files, &mediaDb_);
        }
        return choice.clips ? ids : QStringList{};
    });
    // Bins opening find their managed clips' media in the database (p.358).
    binContainer_->setBinLoader([this](model::Bin& bin) {
        for (model::BinItem& item : bin.items) {
            const auto link = media::linkOf(item);
            if (!link || !link->isManaged() || link->unlinked) continue;
            const QStringList files = link->files();
            if (std::all_of(files.begin(), files.end(), [](const QString& f) { return QFileInfo::exists(f); })) continue;
            mediaDb_.resolve(item);
        }
    });
    connect(binContainer_, &BinContainer::filesDropped, this,
            [this](const QString& binId, const QStringList& paths) { linkFiles(binId, paths); });
    // The Keyboard setting's map, starting from the declared shortcuts; the Composer and the Timeline
    // run the keys pressed in them through it.
    keyboard_ = std::make_unique<KeyboardMap>();
    keyboard_->loadBuiltin(commands_);
    composer_->setKeyResolver([this](const QKeySequence& key) { return keyboard_->commandFor(key); });
    panels_->addPanel(new Panel(QStringLiteral("Composer"), tr("Composer"), composer_));
    // Double-click in a bin loads the item (p.394).
    connect(binContainer_, &BinContainer::itemActivated, this, [this](const QString& binId, const QString& itemId) {
        const BinDocument* doc = binContainer_->document(binId);
        if (const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr) {
            composer_->loadDefault(binId, *item);
            panels_->showPanel(panels_->panel(QStringLiteral("Composer")));
        }
    });
    // The Timeline (ch. 19), showing the Record monitor's sequence.
    timeline_ = new Timeline(*composer_);
    timeline_->setKeyResolver([this](const QKeySequence& key) { return keyboard_->commandFor(key); });
    timeline_->setSequenceStore([this](const QString& binId, const model::BinItem& item, bool isNew) {
        BinDocument* doc = binContainer_->document(binId);
        if (!doc) doc = binContainer_->openBin(binId);
        if (!doc) return false;
        if (isNew) {
            doc->addItem(item);
            return true;
        }
        return doc->replaceItem(item);
    });
    timeline_->setSequenceMaker([this]() -> std::optional<std::pair<QString, model::BinItem>> {
        // The first edit makes a sequence in the active bin (p.463).
        BinDocument* doc = binContainer_->activeDocument();
        if (!doc && !binContainer_->openDocuments().isEmpty()) doc = binContainer_->openDocuments().first();
        if (!doc || !project_) return std::nullopt;
        return std::make_pair(doc->nodeId(), model::newSequence(doc->bin(), project_->settings.format.editRate,
                                                            QDateTime::currentDateTimeUtc(),
                                                            binContainer_->defaultStartTimecode()));
    });
    connect(timeline_, &Timeline::videoDisplaySettingsRequested, this, [this] {
        const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
        const int i = f.activeIndex(QStringLiteral("Video Display"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::Project);
            w->list(model::SettingsScope::Project)->openEntry(i);
        }
    });
    connect(timeline_, &Timeline::message, this, [](const QString& text) { QToolTip::showText(QCursor::pos(), text); });
    // The Timeline takes its own commands from the Composer; the rest are
    // menu commands.
    connect(composer_, &Composer::commandRequested, this, [this](const QString& id) {
        if (Timeline::handles(id)) return;
        if (QAction* a = commands_.action(id)) a->trigger();
    });
    connect(timeline_, &Timeline::commandRequested, this, [this](const QString& id) {
        if (QAction* a = commands_.action(id)) a->trigger();
    });
    panels_->addPanel(new Panel(QStringLiteral("Timeline"), tr("Timeline"), timeline_));
}

// Opening a project: settings, bins, monitors and undo history are reset for it, the project-only
// commands get their handlers, and the media database is read.
void MainWindow::setProject(const model::ProjectEntry& project) {
    project_ = project;
    setWindowTitle(QStringLiteral("%1 - %2").arg(QString::fromUtf8(branding::kProductName), project.settings.name));
    commands_.setHandler(QStringLiteral("file.close_project"), [this] { emit closeProjectRequested(); });
    // Project settings first: they include the new-sequence timecode.
    settings_->setProject(project.folder);
    if (settingsWindow_) settingsWindow_->setProject(project);
    binContainer_->setProject(project.folder, project.settings.name, project.settings.format.editRate);
    composer_->clear();
    composer_->setAspectRatio(project.settings.aspect);
    timeline_->history().clear();
    projectOpened_ = QDateTime::currentDateTimeUtc();
    installProjectHandlers(true);
    // The media database, read as the project opens (ROADMAP 2.4).
    loadMediaDatabase();
}

// Every item in every bin of the project, for the Media Tool, Help > About's Usage and background
// jobs. Closed bins are read from disk; a bin that cannot be read is skipped but still counted.
QList<model::BinItem> MainWindow::projectItems(int* binCount) const {
    QList<model::BinItem> items;
    int count = 0;
    if (const model::ProjectBins* bins = binContainer_->bins()) {
        for (const model::BinNode* node : bins->bins()) {
            ++count;
            // An open bin as it is now, unsaved changes too.
            if (const BinDocument* doc = binContainer_->document(node->id)) {
                items += doc->bin().items;
            } else if (const auto bin = model::BinFile::read(bins->binPath(node->id))) {
                items += bin->items;
            }
        }
    }
    if (binCount) *binCount = count;
    return items;
}

void MainWindow::showMediaTool() {
    if (!project_) return;
    // The Media Tool Display dialog first (p.346): the media drives, and
    // the projects with media on them.
    loadMediaDatabase();
    QList<QPair<QString, QString>> drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.name, d.root});
    MediaToolDisplayDialog dialog(project_->settings.name, drives, mediaDb_.projects(), mediaTool_->display(), this);
    if (mediaToolDisplay_) {
        if (!mediaToolDisplay_(dialog)) return;
    } else if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    mediaTool_->setItems(projectItems(), dialog.display(), mediaDb_.files());
    showTool(QStringLiteral("MediaTool"));
}

// Configuration lists the registered extension providers; Usage counts the open project's bins,
// sequences and other items, and how long it has been open.
AboutInfoDialog* MainWindow::createAboutInfo() {
    QStringList plugins;
    for (const auto& p : providers_.all()) {
        const ext::ProviderInfo info = p->info();
        plugins << QString::fromStdString(info.displayName + " " + info.version);
    }
    ProjectUsage usage;
    if (project_) {
        usage.project = project_->settings.name;
        usage.secondsOpen = projectOpened_.secsTo(QDateTime::currentDateTimeUtc());
        for (const model::BinItem& item : projectItems(&usage.bins)) {
            if (item.kind == model::BinItemKind::Sequence) {
                ++usage.sequences;
            } else {
                ++usage.clips;
            }
        }
    }
    return new AboutInfoDialog(plugins, usage, this);
}

// Closing the project: the reverse of setProject(), and the Settings windows are hidden.
void MainWindow::clearProject() {
    project_.reset();
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    commands_.setHandler(QStringLiteral("file.close_project"), {});
    binContainer_->setProject({}, {}, Rational(24000, 1001));
    composer_->clear();
    timeline_->history().clear();
    if (settingsWindow_) {
        settingsWindow_->hide();
        settingsWindow_->setProject(std::nullopt);
    }
    if (siteSettingsWindow_) siteSettingsWindow_->hide();
    settings_->setProject({});
    installProjectHandlers(false);
}

bool MainWindow::binsActive() const {
    // A floating Bin Container (p.241) is a window of its own.
    if (qobject_cast<FloatingBinContainer*>(QApplication::activeWindow())) return true;
    const Panel* p = panels_->activePanel();
    return p && (p->id() == QLatin1String("BinContainer") || p->id() == QLatin1String("Project"));
}

// Commands that need an open project (bins, sequences, Settings, the Edit commands on bins). Called
// with false at start-up and on close, which clears every handler here and so disables the commands,
// and with true when a project opens.
void MainWindow::installProjectHandlers(bool projectOpen) {
    auto handle = [this, projectOpen](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), projectOpen ? std::move(handler) : std::function<void()>());
    };
    BinContainer* bins = binContainer_;
    // File menu: opening, closing and saving bins.
    handle("file.open_bin", [this, bins] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Open a Bin"), {}, tr("Bins (*.cthbin)"));
        if (file.isEmpty()) return;
        QString error;
        if (bins->openOtherBin(file, &error).isEmpty()) QMessageBox::warning(this, tr("Open Bin"), error);
    });
    // Both open the selected bins as tabs; separate Bin Containers come later.
    handle("file.open_selected_bins", [bins] {
        for (const QString& id : bins->selectedNodes()) bins->openBin(id);
    });
    handle("file.open_selected_bins_in_one_window", [bins] {
        for (const QString& id : bins->selectedNodes()) bins->openBin(id);
    });
    handle("file.close_bin", [bins] { bins->closeBin(bins->activeDocument()); });
    handle("file.save_bin", [this, bins] {
        QString error;
        if (BinDocument* d = bins->activeDocument(); d && !bins->saveBin(d, &error)) {
            QMessageBox::warning(this, tr("Save Bin"), error);
        }
    });
    auto saveAll = [this, bins] {
        QString error;
        if (!bins->saveAll(&error)) QMessageBox::warning(this, tr("Save All"), error);
    };
    handle("file.save_all", saveAll);
    handle("file.save_all_bins", saveAll);
    // Bin > Show Sidebar Only is a check item mirroring the Bin Container's state.
    QAction* sidebarOnly = commands_.action(QStringLiteral("bin.show_sidebar_only"));
    sidebarOnly->setCheckable(true);
    sidebarOnly->setChecked(bins->isSidebarOnly());
    handle("bin.show_sidebar_only", [bins, sidebarOnly] { bins->setSidebarOnly(sidebarOnly->isChecked()); });
    handle("windows.close_all_bins_but_active", [bins] { bins->closeAllBinsButActive(); });
    // New Sequence: in the active bin, or in a bin chosen in the Select dialog.
    handle("timeline.new.sequence", [this, bins] {
        QString target;
        if (!bins->activeDocument()) {
            // With no active bin, the Select dialog asks for one (p.457).
            QStringList names;
            QStringList ids;
            for (const model::BinNode* b : bins->bins()->bins()) {
                names << b->name;
                ids << b->id;
            }
            if (names.isEmpty()) return;
            bool ok = false;
            const QString choice = QInputDialog::getItem(this, tr("Select"), tr("Bin:"), names, 0, false, &ok);
            if (!ok) return;
            target = ids.value(static_cast<int>(names.indexOf(choice)));
        }
        const QString id = bins->newSequence(target);
        // In the bin, the Record monitor and the Timeline (p.457).
        BinDocument* doc = target.isEmpty() ? bins->activeDocument() : bins->document(target);
        if (const model::BinItem* item = doc ? doc->bin().item(id) : nullptr) {
            composer_->load(MonitorId::Record, doc->nodeId(), *item);
        }
        panels_->showPanel(panels_->panel(QStringLiteral("BinContainer")));
    });
    // File > Settings (p.1136) and Windows > Site Settings (p.1141).
    handle("file.settings", [this] { showSettings(); });
    handle("windows.site_settings", [this] { showSiteSettings(); });
    // Edit menu commands act on the bin when the Bin Container is active,
    // or on the settings in a Settings window (p.1138, p.1140).
    handle("edit.duplicate", [this, bins] {
        if (SettingsList* list = activeSettingsList()) {
            list->duplicateSelection();
        } else if (binsActive()) {
            bins->duplicateSelection();
        }
    });
    handle("edit.delete", [this, bins] {
        if (SettingsList* list = activeSettingsList()) {
            list->deleteSelection();
        } else if (binsActive()) {
            bins->deleteSelection();
        } else if (const Panel* active = panels_->activePanel(); active && active->id() == QLatin1String("MediaTool")) {
            // Edit > Delete in the Media Tool: the Delete Media dialog (p.347).
            mediaTool_->deleteSelected();
        } else if (active && active->id() == QLatin1String("Timeline")) {
            // Segments or tracks in the Timeline (p.621, p.636).
            timeline_->perform(QStringLiteral("edit.delete"));
        }
    });
    handle("edit.select_all", [this, bins] {
        if (binsActive()) bins->selectAll();
    });

    // The active bin's views, columns, sorting and sifting (ch. 10).
    // Each of these acts on the active bin's view, and does nothing when no bin is active.
    auto view = [bins](const char* id, std::function<void(BinView&)> act) {
        return std::pair<const char*, std::function<void()>>(id, [bins, act = std::move(act)] {
            if (BinView* v = bins->activeView()) act(*v);
        });
    };
    using Mode = model::BinViewMode;
    const std::pair<const char*, std::function<void()>> binCommands[] = {
        view("bin.choose_columns", [](BinView& v) { v.chooseColumns(); }),
        view("bin.hide_column", [](BinView& v) { v.hideColumns(v.selectedHeadings()); }),
        view("bin.sort", [](BinView& v) { v.sortSelected(); }),
        view("bin.sort_again", [](BinView& v) { v.sortAgain(); }),
        view("bin.sift_bin_contents", [](BinView& v) { v.siftBinContents(); }),
        view("bin.sift_selected_items", [](BinView& v) { v.siftSelected(); }),
        view("bin.show_sifted", [](BinView& v) { v.setSiftShown(true); }),
        view("bin.show_unsifted", [](BinView& v) { v.setSiftShown(false); }),
        view("bin.select.reverse", [](BinView& v) { v.reverseSelection(); }),
        view("bin.align_and_fill.align_columns", [](BinView& v) { v.alignColumns(); }),
        view("bin.align_and_fill.align", [](BinView& v) { v.frameView()->alignToGrid(false); }),
        view("bin.align_and_fill.align_selected", [](BinView& v) { v.frameView()->alignToGrid(true); }),
        view("bin.align_and_fill.fill_window", [](BinView& v) { v.frameView()->fillWindow(); }),
        view("bin.align_and_fill.fill_sorted", [](BinView& v) { v.frameView()->fillSorted(); }),
        view("clip.lock_bin_selection", [](BinView& v) { v.setLocked(true); }),
        view("clip.unlock_bin_selection", [](BinView& v) { v.setLocked(false); }),
        view("edit.enlarge_frame", [](BinView& v) {
            if (v.mode() != Mode::Text) v.enlargeFrame();
        }),
        view("edit.reduce_frame", [](BinView& v) {
            if (v.mode() != Mode::Text) v.reduceFrame();
        }),
    };
    for (const auto& [id, handler] : binCommands) handle(id, handler);
    handle("bin.set_bin_display", [bins] {
        if (BinView* v = bins->activeView()) v->setBinDisplay();
    });
    // Bin > Add Bin to Favorites (p.293).
    handle("bin.add_bin_to_favorites", [bins] { bins->addToFavorites(bins->activeDocument()); });
    // Tools > Project (p.243).
    handle("tools.project", [this] {
        Panel* project = panels_->panel(QStringLiteral("Project"));
        if (panels_->isOpen(project)) {
            panels_->showPanel(project);
            return;
        }
        const QPoint at = mapToGlobal(QPoint(40, 60));
        panels_->floatPanel(project, QRect(at, QSize(kProjectWidth, kProjectHeight)));
        panels_->activate(project);
    });
}

// closed() fires only once the close is accepted.
void MainWindow::closeEvent(QCloseEvent* event) {
    // Where each workspace was left, for the next session.
    writeWorkspaces();
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        emit closed();
    }
}

// The Settings window is made on first use and kept; it is a top-level window parented to this one.
SettingsWindow* MainWindow::showSettings() {
    if (!settingsWindow_) {
        settingsWindow_ = new SettingsWindow(*settings_, this);
        connect(settingsWindow_, &SettingsWindow::toolRequested, this, [this](const QString& tool) {
            if (tool == QLatin1String("toolPalette")) showTool(QStringLiteral("ToolPalette"));
            if (tool == QLatin1String("keyboard")) showTool(QStringLiteral("Keyboard"));
        });
        // Its own window: give it the commands so Edit's shortcuts work.
        for (const QString& id : commands_.ids()) settingsWindow_->addAction(commands_.action(id));
    }
    settingsWindow_->setProject(project_);
    settingsWindow_->show();
    settingsWindow_->raise();
    settingsWindow_->activateWindow();
    return settingsWindow_;
}

SiteSettingsWindow* MainWindow::showSiteSettings() {
    if (!siteSettingsWindow_) {
        siteSettingsWindow_ = new SiteSettingsWindow(*settings_, this);
        for (const QString& id : commands_.ids()) siteSettingsWindow_->addAction(commands_.action(id));
    }
    siteSettingsWindow_->show();
    siteSettingsWindow_->raise();
    siteSettingsWindow_->activateWindow();
    return siteSettingsWindow_;
}

// The settings list Edit > Duplicate and Delete act on: the one in the active Settings or Site
// Settings window, or null when neither is active.
SettingsList* MainWindow::activeSettingsList() const {
    const QWidget* active = QApplication::activeWindow();
    if (settingsWindow_ && active == settingsWindow_) return settingsWindow_->currentList();
    if (siteSettingsWindow_ && active == siteSettingsWindow_) return siteSettingsWindow_->list();
    return nullptr;
}

// Replaces the User settings' Workspace View entries with the workspaces as they are now: their saved
// and last known layouts and what each is based on, the active one marked active. syncingWorkspaces_
// stops the commit (which fires Settings::changed and so applySettings) from coming back here.
void MainWindow::writeWorkspaces() {
    if (syncingWorkspaces_) return;
    syncingWorkspaces_ = true;
    model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    const QString kind = QStringLiteral("Workspace View");
    // Keep each workspace's own options (Link to Named settings).
    QHash<QString, QJsonObject> kept;
    for (int i : f.versions(kind)) kept.insert(f.entries.at(i).name.toLower(), f.entries.at(i).values);
    f.remove(f.versions(kind));
    const QString active = workspaces_->active();
    for (const WorkspaceManager::Entry& e : workspaces_->entries()) {
        QJsonObject v = kept.value(e.name.toLower());
        v.insert(QStringLiteral("saved"), e.saved);
        v.insert(QStringLiteral("lastKnown"), e.lastKnown);
        v.insert(QStringLiteral("basedOn"), e.basedOn);
        f.entries << model::SettingEntry{kind, e.name, e.name == active, v};
    }
    // The workspace toolbar's look goes with them (p.88).
    if (const int i = f.activeIndex(QStringLiteral("Interface")); i >= 0) {
        f.entries[i].values.insert(QStringLiteral("workspaceBar"), static_cast<int>(workspaces_->barMode()));
    }
    settings_->commit(model::SettingsScope::User);
    syncingWorkspaces_ = false;
}

// The reverse of writeWorkspaces(), when another user's settings load. It writes them back at the end
// so that a new user's settings get the default workspaces.
void MainWindow::readWorkspaces() {
    syncingWorkspaces_ = true;
    const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    QList<WorkspaceManager::Entry> entries;
    QString active;
    for (int i : f.versions(QStringLiteral("Workspace View"))) {
        const model::SettingEntry& e = f.entries.at(i);
        if (e.name.isEmpty()) continue;
        entries << WorkspaceManager::Entry{e.name, e.values.value(QStringLiteral("basedOn")).toString(),
                                           e.values.value(QStringLiteral("saved")).toObject(),
                                           e.values.value(QStringLiteral("lastKnown")).toObject()};
        if (e.active) active = e.name;
    }
    // A new user starts from the default workspaces.
    workspaces_->setEntries(entries, active);
    const int bar = f.activeValues(QStringLiteral("Interface")).value(QStringLiteral("workspaceBar")).toInt();
    workspaces_->setBarMode(bar == 1   ? WorkspaceManager::BarMode::IconOnly
                            : bar == 2 ? WorkspaceManager::BarMode::Hidden
                                       : WorkspaceManager::BarMode::IconAndText);
    linkedWorkspace_ = workspaces_->active();
    syncingWorkspaces_ = false;
    writeWorkspaces();
}

// Index i is the workspace of button W(i+1); an empty name leaves the button unassigned.
QStringList MainWindow::workspaceButtons() const {
    const QJsonValue v = settings_->value(QStringLiteral("Interface"), QStringLiteral("workspaceButtons"));
    if (!v.isArray()) {
        // Ours: the built-in workspaces, in toolbar order.
        return {QStringLiteral("Edit"), QStringLiteral("Color"), QStringLiteral("Effects"), QStringLiteral("Audio")};
    }
    QStringList out;
    for (const QJsonValue& n : v.toArray()) out << n.toString();
    return out;
}

void MainWindow::updateWorkspaceButtons() {
    // A workspace button shows the first two characters of its workspace;
    // italic if that workspace is gone (p.90).
    const QStringList buttons = workspaceButtons();
    for (int i = 0; i < WorkspaceManager::kMaxWorkspaces; ++i) {
        const QString name = buttons.value(i);
        registerCommandIcon(QStringLiteral("workspace.w%1").arg(i + 1),
                            name.isEmpty() ? labelIcon(QStringLiteral("W%1").arg(i + 1))
                                           : labelIcon(name.left(2), !workspaces_->contains(name)));
    }
    ButtonMapper::instance().refreshIcons();
    commandPalette_->setWorkspaces(workspaces_->names(), buttons);
}

// Called when the active workspace changes. Only for a workspace whose Workspace View has Link to
// Named settings on (linkNamed): every other kind of User setting switches to the version of that
// name, if it has one that is not already active.
void MainWindow::linkSettings(const QString& workspace) {
    linkedWorkspace_ = workspace;
    model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    const QString kind = QStringLiteral("Workspace View");
    QJsonObject values;
    for (int i : f.versions(kind)) {
        if (f.entries.at(i).name.compare(workspace, Qt::CaseInsensitive) == 0) values = f.entries.at(i).values;
    }
    if (!values.value(QStringLiteral("linkNamed")).toBool()) return;
    // The settings named in the box; with nothing typed, the unnamed ones
    // (p.90). Only User settings are linked.
    const QString target = values.value(QStringLiteral("linkName")).toString().trimmed();
    bool any = false;
    for (const QString& k : f.kinds()) {
        if (k == kind) continue;
        for (int i : f.versions(k)) {
            if (f.entries.at(i).name.trimmed().compare(target, Qt::CaseInsensitive) == 0 && !f.entries.at(i).active) {
                f.setActive(i);
                any = true;
                break;
            }
        }
    }
    if (any) settings_->commit(model::SettingsScope::User);
}

// Creates the tool panels (ch.5) and connects them: Command Palette, Tool Palette, Calculator,
// Console, Effect Palette and Editor, Audio Mixer and Tool, Background Queue, Media Tool, Source
// Browser and the Keyboard palette. All start closed; showTool() floats them on first use. Changes made
// in the tools (mixer modes, keys, mapped buttons, workspace buttons) are saved into the settings.
void MainWindow::createTools() {
    ButtonMapper::instance().setRegistry(&commands_);
    // Menu to Button (p.98): with a target button picked, the next menu
    // command is mapped onto it instead of running.
    commands_.setCapture([](const QString& id) {
        ButtonMapper& mapper = ButtonMapper::instance();
        if (mapper.mode() != ButtonMapper::Mode::MenuToButton || !mapper.target()) return false;
        return mapper.mapMenuCommand(id);
    });
    commandPalette_ = new CommandPalette(commands_);
    panels_->addPanel(new Panel(QStringLiteral("CommandPalette"), tr("Command Palette"), commandPalette_));
    toolPalette_ = new ToolPalette;
    panels_->addPanel(new Panel(QStringLiteral("ToolPalette"), tr("Tool Palette"), toolPalette_));
    calculator_ = new CalculatorWindow;
    panels_->addPanel(new Panel(QStringLiteral("Calculator"), tr("Calculator"), calculator_));
    console_ = new ConsoleWindow;
    panels_->addPanel(new Panel(QStringLiteral("Console"), tr("Console"), console_));
    // Effects (FX guide p.37, p.83).
    effectPalette_ = new EffectPalette;
    panels_->addPanel(new Panel(QStringLiteral("EffectPalette"), tr("Effect Palette"), effectPalette_));
    connect(effectPalette_, &EffectPalette::effectActivated, this,
            [this](const EffectInfo& effect) { timeline_->applyEffectToSelection(effect); });
    effectEditor_ = new EffectEditor(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("EffectEditor"), tr("Effect Editor"), effectEditor_));
    // Audio (ch. 21) and media (p.344-348).
    audioMixer_ = new AudioMixer(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("AudioMixer"), tr("Audio Mixer"), audioMixer_));
    // The mode and mix are Audio Project settings; Live Mix is not saved
    // (p.692).
    connect(audioMixer_, &AudioMixer::modeChanged, this, [this](AudioMixer::Mode mode) {
        if (mode == AudioMixer::Mode::Live) return;
        settings_->setValue(QStringLiteral("Audio Project"), QStringLiteral("mixerMode"), AudioMixer::modeName(mode));
    });
    connect(audioMixer_, &AudioMixer::mixModeChanged, this, [this](const QString& value) {
        settings_->setValue(QStringLiteral("Audio Project"), QStringLiteral("mixMode"), value);
    });
    audioTool_ = new AudioTool;
    panels_->addPanel(new Panel(QStringLiteral("AudioTool"), tr("Audio Tool"), audioTool_));
    // Tools > Background Queue Window (p.356-357).
    backgroundQueue_ = new BackgroundQueue(this);
    panels_->addPanel(new Panel(QStringLiteral("BackgroundQueue"), tr("Background Queue Window"),
                                new BackgroundQueueWindow(*backgroundQueue_)));
    connect(backgroundQueue_, &BackgroundQueue::finished, this,
            [this](const QString& binId, const media::ConsolidateResult& result) { applyConsolidate(binId, result); });
    mediaTool_ = new MediaTool;
    // Delete Media (p.347-348): managed media files only; clips that used
    // them show Media Offline.
    mediaTool_->deleter = [this](const QStringList& paths) {
        frames_->clear();
        const QStringList deleted = media::deleteMediaFiles(paths, &mediaDb_);
        if (project_) mediaTool_->setItems(projectItems(), mediaTool_->display(), mediaDb_.files());
        ConsoleLog::instance().append(tr("Delete Media: %n file(s) deleted.", "", static_cast<int>(deleted.size())));
        return deleted;
    };
    panels_->addPanel(new Panel(QStringLiteral("MediaTool"), tr("Media Tool"), mediaTool_));
    // File > Input > Source Browser (p.312-318).
    sourceBrowser_ = new SourceBrowser;
    auto* browserPanel = new Panel(QStringLiteral("SourceBrowser"), tr("Source Browser"), sourceBrowser_);
    panels_->addPanel(browserPanel);
    connect(sourceBrowser_, &SourceBrowser::linkRequested, this,
            [this](const QString& binId, const QStringList& paths) { linkFiles(binId, paths); });
    connect(sourceBrowser_, &SourceBrowser::importRequested, this,
            [this](const QString& binId, const QStringList& paths, const QString& resolution) {
                importFiles(binId, paths, resolution);
            });
    connect(sourceBrowser_, &SourceBrowser::volumeBinRequested, this, [this](const QString& folder) { linkVolume(folder); });
    connect(sourceBrowser_, &SourceBrowser::loadRequested, this, [this](const QStringList& paths) { loadFiles(paths); });
    connect(sourceBrowser_, &SourceBrowser::settingsRequested, this, [this] {
        // The Settings button opens the Link settings (p.317), or the
        // Import settings when importing (p.214).
        const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
        const bool import = sourceBrowser_ && sourceBrowser_->isImportMode();
        const int i = f.activeIndex(import ? QStringLiteral("Import") : QStringLiteral("Link"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::User);
            w->list(model::SettingsScope::User)->openEntry(i);
        }
    });
    connect(sourceBrowser_, &SourceBrowser::closeRequested, this, [this, browserPanel] {
        panels_->closePanel(browserPanel);
    });
    // "Clear Source Monitor Upon Closing Source Browser" (p.315): the clips
    // it loaded without a bin go.
    // Guarded: panel layout changes still arrive while the window is being taken down.
    QPointer<Composer> composer = composer_;
    connect(panels_, &PanelManager::layoutChanged, sourceBrowser_, [this, browserPanel, composer] {
        if (!composer) return;
        const bool open = panels_->isOpen(browserPanel);
        if (sourceBrowserOpen_ && !open && sourceBrowser_->clearsSourceOnClose()) {
            composer->clearUncommitted();
        }
        sourceBrowserOpen_ = open;
    });
    connect(binContainer_, &BinContainer::activeBinChanged, sourceBrowser_, [this] { refreshSourceBins(); });
    connect(binContainer_, &BinContainer::dirtyChanged, sourceBrowser_, [this] { refreshSourceBins(); });
    // The Keyboard setting's palette (p.1202).
    keyboardPalette_ = new KeyboardPalette(*keyboard_);
    panels_->addPanel(new Panel(QStringLiteral("Keyboard"), tr("Keyboard"), keyboardPalette_));
    connect(keyboardPalette_, &KeyboardPalette::keyboardChanged, this, [this] {
        settings_->setValue(QStringLiteral("Keyboard"), QStringLiteral("keys"), keyboard_->differences());
    });
    // The Workspaces tab's menus (p.91).
    connect(commandPalette_, &CommandPalette::workspaceAssigned, this, [this](int index, const QString& name) {
        QStringList buttons = workspaceButtons();
        while (buttons.size() <= index) buttons << QString();
        buttons[index] = name;
        settings_->setValue(QStringLiteral("Interface"), QStringLiteral("workspaceButtons"),
                            QJsonArray::fromStringList(buttons));
    });

    connect(toolPalette_, &ToolPalette::commandClicked, this, [this](const QString& id) {
        if (QAction* a = commands_.action(id); a && a->isEnabled()) a->trigger();
    });
    // Mapped buttons are saved in the settings: the Composer's with the
    // Composer settings, the Tool palette's in its own (p.97, p.1221).
    connect(toolPalette_, &ToolPalette::commandsChanged, this, [this] {
        settings_->setValue(QStringLiteral("Tool Palette"), QStringLiteral("buttons"),
                            QJsonArray::fromStringList(toolPalette_->commands()));
    });
    connect(composer_, &Composer::buttonsChanged, this, [this] {
        settings_->setValue(QStringLiteral("Composer"), QStringLiteral("buttons"), composer_->buttonLayout());
    });
}

std::optional<std::pair<model::BinItem, std::int64_t>> MainWindow::frameAt(MonitorId monitor,
                                                                           std::int64_t position) const {
    // A clip's own frame; a sequence's from its top video track, in the
    // order Match Frame uses (p.432-434).
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (!loaded || position < 0) return std::nullopt;
    model::BinItem item = loaded->item;
    std::int64_t frame = position;
    if (item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(item);
        QStringList video;
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video) video << t.name();
        }
        if (video.isEmpty()) return std::nullopt;
        const auto match = model::matchFrame(seq, video, frame);
        const auto clip = match ? composer_->resolve(match->segment.binId, match->segment.clipId) : std::nullopt;
        if (!clip) return std::nullopt;
        item = *clip;
        frame = match->frame;
    } else if (frame >= item.durationFrames) {
        return std::nullopt;
    }
    if (!media::isLinked(item)) return std::nullopt;
    return std::make_pair(item, frame);
}

// The audio of a loaded clip or sequence as pieces for media::AudioMix (media::audioPieces).
// `monitored`, when set, picks the sequence tracks heard (the Track Selector panel).
std::vector<media::AudioPiece> MainWindow::soundOf(const model::LoadedClip& loaded,
                                                  const std::function<bool(const QString&)>& monitored) const {
    // Audio settings, Default Pan for mono tracks (p.1147).
    const bool alternating = settings_->text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
    return media::audioPieces(
        loaded.item, [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); },
        monitored, alternating);
}

// The Full Screen Playback view is made once and kept (guarded by a QPointer); applySettings() gives it
// the Full Screen Playback settings before it opens.
void MainWindow::toggleFullScreen() {
    FullScreenView* view = fullScreen_.data();
    if (view && view->isVisible()) {
        view->close();
        return;
    }
    if (!view) {
        view = new FullScreenView(*composer_, this);
        fullScreen_ = view;
        connect(view, &FullScreenView::closed, this, [this] { activateWindow(); });
    }
    applySettings();
    view->open(this);
}

FullScreenView* MainWindow::fullScreenView() const {
    return fullScreen_.data();
}

// Runs on every Composer::changed. A burst of sound plays only when the active monitor still shows
// the same item but its position moved, playback is stopped, and Shift is held or the Digital Audio
// Scrub button is on. The first change after loading another item only records the position.
void MainWindow::scrubIfMoved() {
    const MonitorId monitor = composer_->activeMonitor();
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    const std::int64_t position = loaded ? composer_->state(monitor).position() : -1;
    const QString key = loaded ? loaded->item.id : QString();
    const bool moved = key == scrubItem_ && position != scrubPosition_;
    scrubItem_ = key;
    scrubPosition_ = position;
    if (!loaded || !moved || composer_->isPlaying()) return;
    const bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;
    QAction* button = commands_.action(QStringLiteral("play.digital_audio_scrub"));
    if (!shift && !(button && button->isChecked())) return;
    // Audio settings: frames before (outgoing) and after (incoming) the
    // position, per monitor; 0 and 1 by default (p.675, p.1147).
    const bool source = monitor == MonitorId::Source;
    const int out = settings_->number(QStringLiteral("Audio"), source ? QStringLiteral("sourceScrubOut") : QStringLiteral("recordScrubOut"));
    const int in = settings_->number(QStringLiteral("Audio"), source ? QStringLiteral("sourceScrubIn") : QStringLiteral("recordScrubIn"));
    if (out + in <= 0) return;
    std::function<bool(const QString&)> monitored;
    if (loaded->item.kind == model::BinItemKind::Sequence) {
        monitored = [this](const QString& track) { return timeline_->isMonitored(track); };
    }
    auto pieces = soundOf(*loaded, monitored);
    if (pieces.empty()) return;
    const Rational rate = loaded->item.editRate;
    media::AudioPlayer::PlayOptions options;
    options.limit = media::framesToSamples(out + in, rate);
    scrubStart_ = media::framesToSamples(position - out, rate);
    if (!audio_->start(std::make_shared<media::AudioMix>(std::move(pieces)), scrubStart_, options)) return;
    scrubbing_ = true;
    // The device is let go once the burst has played.
    const int token = ++scrubToken_;
    const int ms = static_cast<int>(1000.0 * (out + in) * static_cast<double>(rate.den()) /
                                    static_cast<double>(std::max<std::int64_t>(rate.num(), 1))) +
                   250;
    // The burst's length plus a quarter second; a later burst (a new token) or playback keeps the device.
    QTimer::singleShot(ms, this, [this, token] {
        if (token == scrubToken_ && scrubbing_ && !composer_->isPlaying()) {
            audio_->stop();
            scrubbing_ = false;
        }
    });
}

bool MainWindow::isPlayingSound() const {
    return audio_ && audio_->isPlaying();
}

std::int64_t MainWindow::soundClock() const {
    return audio_ ? audio_->clock() : 0;
}

// Runs on Composer::playStarted, also when only the speed changed: the sound restarts from the
// current position at the new speed. With no output device, play goes on silently.
void MainWindow::startSound(MonitorId monitor, int speed) {
    audio_->stop();
    scrubbing_ = false;
    // Smooth audio scrub: sound up to three times normal speed, either
    // way; faster, it cuts out (p.675).
    if (speed == 0 || std::abs(speed) > 3) return;
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (!loaded) return;
    // A sequence plays the audio tracks monitored in the Track Selector
    // panel (p.395).
    std::function<bool(const QString&)> monitored;
    if (loaded->item.kind == model::BinItemKind::Sequence) {
        monitored = [this](const QString& track) { return timeline_->isMonitored(track); };
    }
    auto pieces = soundOf(*loaded, monitored);
    if (pieces.empty()) return;
    const std::int64_t at = media::framesToSamples(composer_->state(monitor).position(), loaded->item.editRate);
    media::AudioPlayer::PlayOptions options;
    options.speed = speed;
    if (!audio_->start(std::make_shared<media::AudioMix>(std::move(pieces)), at, options)) {
        ConsoleLog::instance().append(tr("Playback: no audio output could be opened; playing without sound."));
    }
}

// Without a project, the edit rate stays the LinkOptions default.
media::LinkOptions MainWindow::linkOptions() const {
    media::LinkOptions o;
    if (project_) o.editRate = project_->settings.format.editRate;
    o.when = QDateTime::currentDateTimeUtc();
    o.sdWidescreen = settings_ && settings_->flag(QStringLiteral("Link"), QStringLiteral("sdWidescreen"));
    return o;
}

SourceBrowser& MainWindow::sourceBrowser() const {
    return *sourceBrowser_.data();
}

// Gives the Source Browser the project's bins for its target bin menu.
void MainWindow::refreshSourceBins() {
    SourceBrowser* browser = sourceBrowser_.data();
    if (!browser) return;
    QList<QPair<QString, QString>> bins;
    if (model::ProjectBins* project = binContainer_->bins()) {
        for (const model::BinNode* b : project->bins()) bins.append({b->id, b->name});
    }
    // The active bin, unless another was chosen (p.317).
    const BinDocument* active = binContainer_->activeDocument();
    const QString keep = browser->targetBin();
    browser->setBins(bins, keep.isEmpty() && active ? active->nodeId() : keep);
}

void MainWindow::showSourceBrowser() {
    refreshSourceBins();
    if (SourceBrowser* browser = sourceBrowser_.data(); browser && browser->folder().isEmpty()) browser->home();
    showTool(QStringLiteral("SourceBrowser"));
}

// The bin is opened if it is not; the new clips are selected in it.
QStringList MainWindow::linkFiles(const QString& binId, const QStringList& paths) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || paths.isEmpty()) return {};
    int skipped = 0;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<model::BinItem> items = media::linkFiles(paths, linkOptions(), &skipped);
    QApplication::restoreOverrideCursor();
    QStringList ids;
    for (const model::BinItem& item : items) {
        doc->addItem(item);
        ids << item.id;
    }
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    if (skipped > 0) {
        ConsoleLog::instance().append(tr("Link: %n file(s) are not media that can be linked.", "", skipped));
    }
    return ids;
}

// The clips are loaded with an empty bin id: they are in no bin until Composer::commitSource() puts
// them in one (p.316).
void MainWindow::loadFiles(const QStringList& paths, bool recordMonitor) {
    const QList<model::BinItem> items = media::linkFiles(paths, linkOptions());
    for (const model::BinItem& item : items) {
        composer_->load(recordMonitor ? MonitorId::Record : MonitorId::Source, QString(), item);
    }
    if (!items.isEmpty()) panels_->showPanel(panels_->panel(QStringLiteral("Composer")));
}

void MainWindow::storeClip(const QString& binId, const model::BinItem& item) {
    BinDocument* doc = binContainer_->document(binId);
    if (doc) doc->replaceItem(item);
    composer_->updateItem(item);
}

void MainWindow::relinkLinkedMedia() {
    // The linked clips selected in the bin; with none selected, those of
    // the bins selected in the Sidebar, or of every open bin (p.364: "with
    // or without an item selected").
    QList<LinkedMediaDialog::Clip> clips;
    auto add = [&](const BinDocument* doc, const QStringList& ids) {
        for (const model::BinItem& item : doc->bin().items) {
            // Linked media only: managed media relinks through its
            // database (p.361).
            const auto link = media::linkOf(item);
            if (link && !link->isManaged() && (ids.isEmpty() || ids.contains(item.id))) clips.append({doc->nodeId(), item});
        }
    };
    if (BinView* view = binContainer_->activeView(); view && !view->selectedIds().isEmpty()) {
        add(&view->document(), view->selectedIds());
    }
    if (clips.isEmpty()) {
        for (const QString& id : binContainer_->selectedNodes()) {
            if (const BinDocument* doc = binContainer_->document(id)) add(doc, {});
        }
    }
    if (clips.isEmpty()) {
        for (const BinDocument* doc : binContainer_->openDocuments()) add(doc, {});
    }
    LinkedMediaDialog dialog(clips, this);
    connect(&dialog, &LinkedMediaDialog::relinked, this,
            [this](const QString& binId, const model::BinItem& item) { storeClip(binId, item); });
    connect(&dialog, &LinkedMediaDialog::notRelinked, this, [](const QString& name, const QString& reason) {
        // "Open the Console window to see the name of the file or files
        // that were not relinked" (p.320).
        ConsoleLog::instance().append(tr("Relink: %1: %2").arg(name, reason));
    });
    if (linkedMediaHook_) {
        linkedMediaHook_(dialog);
    } else {
        dialog.exec();
    }
}

// Re-reads the media database and relinks the chosen clips to the media files that match them;
// changed clips are stored back, new ".relinked" sequences are added to their bin and selected, and
// items with no match are named in the Console.
void MainWindow::relinkManagedMedia() {
    if (!project_) return;
    ManagedRelinkDialog dialog(project_->settings.name, this);
    const bool ok = managedRelinkHook_ ? managedRelinkHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    const media::RelinkOptions options = dialog.options();
    // The selected items: the active bin's, or every open bin's (p.362).
    QList<std::pair<BinDocument*, QStringList>> selected;
    if (dialog.allOpenBins()) {
        for (BinDocument* doc : binContainer_->openDocuments()) {
            if (BinView* v = binContainer_->viewFor(doc); v && !v->selectedIds().isEmpty()) {
                selected.append({doc, v->selectedIds()});
            }
        }
    } else if (BinView* v = binContainer_->activeView(); v && !v->selectedIds().isEmpty()) {
        selected.append({&v->document(), v->selectedIds()});
    }
    if (selected.isEmpty()) return;
    // The media on the drives as they are now, and the open bins' master
    // clips, which sequences relink to.
    loadMediaDatabase();
    QList<media::BinClip> candidates;
    for (const BinDocument* doc : binContainer_->openDocuments()) {
        for (const model::BinItem& item : doc->bin().items) candidates.append({doc->nodeId(), item});
    }
    const media::ClipLookup lookup = [this](const QString& binId, const QString& clipId) {
        return composer_->resolve(binId, clipId);
    };
    QStringList notRelinked;
    for (const auto& [doc, ids] : selected) {
        QList<model::BinItem> items;
        QStringList taken;
        for (const model::BinItem& item : doc->bin().items) {
            taken << item.name;
            if (ids.contains(item.id)) items << item;
        }
        const media::RelinkResult result = media::relinkManaged(items, mediaDb_, candidates, lookup, options, taken);
        for (const model::BinItem& item : result.changed) storeClip(doc->nodeId(), item);
        QStringList added;
        for (const model::BinItem& item : result.added) {
            doc->addItem(item);
            added << item.id;
        }
        if (BinView* v = binContainer_->viewFor(doc); v && !added.isEmpty()) v->selectIds(added);
        notRelinked += result.notRelinked;
    }
    frames_->clear();
    for (const QString& name : notRelinked) ConsoleLog::instance().append(tr("Relink: %1: no matching media.").arg(name));
}

int MainWindow::unlinkSelection() {
    // Clip > Modify > Unlink Media: the selected master clips (p.368).
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    int unlinked = 0;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* found = view->document().bin().item(id);
        if (!found) continue;
        model::BinItem item = *found;
        if (!media::unlinkClip(item)) continue;
        storeClip(view->document().nodeId(), item);
        ++unlinked;
    }
    return unlinked;
}

// ---- Managed media (Phase 2.4)

namespace {

// A progress dialog for making media, and the callback that moves it; it
// returns false when Cancel was clicked.
std::function<bool(double)> progressOn(QProgressDialog& dialog) {
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(500);
    dialog.setRange(0, 1000);
    dialog.setValue(0);
    return [&dialog](double done) {
        dialog.setValue(static_cast<int>(done * 1000));
        QCoreApplication::processEvents();
        return !dialog.wasCanceled();
    };
}

} // namespace

// The Media Creation setting's keys are prefixed by the tab: "importResolution", "transcodeVideoDrive"
// and so on. A resolution that is not found keeps the MediaCreation default.
media::MediaCreation MainWindow::mediaCreation(const QString& tab) const {
    media::MediaCreation c;
    const QString kind = QStringLiteral("Media Creation");
    if (project_) {
        c.raster = QSize(project_->settings.format.width, project_->settings.format.height);
        c.project = project_->settings.name;
    }
    if (const media::Resolution* r = media::findResolution(settings_->text(kind, tab + QStringLiteral("Resolution")))) {
        c.resolution = *r;
    }
    c.videoDrive = media::chooseDrive(settings_->text(kind, tab + QStringLiteral("VideoDrive")));
    c.audioDrive = media::chooseDrive(settings_->text(kind, tab + QStringLiteral("AudioDrive")));
    c.bitDepth = settings_->text(QStringLiteral("Audio Project"), QStringLiteral("bitDepth")).toInt() == 16 ? 16 : 24;
    c.when = QDateTime::currentDateTimeUtc();
    return c;
}

// Rescans every media drive from scratch (a wait cursor shows meanwhile) and points offline managed
// clips in open bins at their files. Clips in bins opened later are resolved by the bin loader set in
// createPanels().
void MainWindow::loadMediaDatabase() {
    // Every drive's Avid MediaFiles folder (p.358-359).
    QStringList roots;
    for (const media::MediaDrive& d : media::mediaDrives()) roots << d.root;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    mediaDb_.clear();
    mediaDb_.scan(roots);
    QApplication::restoreOverrideCursor();
    // Open bins' managed clips that are offline find their files.
    int found = 0;
    for (BinDocument* doc : binContainer_->openDocuments()) {
        for (const model::BinItem& item : doc->bin().items) {
            const auto link = media::linkOf(item);
            if (!link || !link->isManaged() || !frames_->isOffline(item)) continue;
            model::BinItem fixed = item;
            if (mediaDb_.resolve(fixed) && fixed.link != item.link) {
                storeClip(doc->nodeId(), fixed);
                ++found;
            }
        }
    }
    if (found > 0) frames_->clear();
    ConsoleLog::instance().append(tr("Media database: %n media file(s) on the drives.", "", mediaDb_.fileCount()));
}

// Each file is imported on its own: one that fails is reported in the Console and the rest go on.
// The AAF's custom bin columns are added to the bin's display.
QStringList MainWindow::importAaf(const QString& binId, const QStringList& paths) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || paths.isEmpty()) return {};
    // Master clips find their managed media by MobID, so the media of a
    // clip exported elsewhere comes online (p.547).
    loadMediaDatabase();
    QStringList ids;
    model::BinDisplay display = doc->bin().display;
    for (const QString& path : paths) {
        QString error;
        interop::aaf::ImportOptions options;
        options.binId = doc->nodeId();
        options.when = QDateTime::currentDateTimeUtc();
        const auto imported = interop::aaf::importFile(path, options, &error);
        if (!imported) {
            ConsoleLog::instance().append(tr("Import: %1").arg(error));
            continue;
        }
        int online = 0;
        for (model::BinItem item : imported->items) {
            const bool clip = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
            if (clip) {
                if (const auto set = mediaDb_.setOf(item.mobId)) {
                    item.link = set->link().toJson();
                    ++online;
                }
            }
            doc->addItem(item);
            ids << item.id;
        }
        for (const QString& column : imported->customColumns) {
            if (!display.customColumns.contains(column)) display.customColumns << column;
        }
        ConsoleLog::instance().append(tr("Import: %1: %n item(s), %2 with media", "", static_cast<int>(imported->items.size()))
                                          .arg(QFileInfo(path).fileName())
                                          .arg(online));
        for (const QString& w : imported->warnings) ConsoleLog::instance().append(tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
    }
    if (display.customColumns != doc->bin().display.customColumns) doc->setDisplay(display);
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    return ids;
}

// Steps: collect the selection and the clips its sequences use, make sure an Export setting exists,
// ask for the file and setting (Export As dialog), then write the AAF with interop::aaf::exportItems.
// Results and warnings go to the Console. The last folder exported to is remembered in QSettings.
void MainWindow::exportToFile() {
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty()) {
        ConsoleLog::instance().append(tr("Export to File: select the clips or sequences to export in a bin."));
        return;
    }
    const BinDocument& doc = view->document();
    QList<model::BinItem> items;
    QStringList ids;
    for (const model::BinItem& item : doc.bin().items) {
        if (!view->selectedIds().contains(item.id)) continue;
        items << item;
        ids << item.id;
    }
    // A sequence's clips go with it, so that it is not offline where it is
    // read.
    const QList<model::BinItem> selected = items;
    for (const model::BinItem& item : selected) {
        if (item.kind != model::BinItemKind::Sequence) continue;
        const model::Sequence sequence = model::Sequence::fromItem(item);
        for (const model::Track& t : sequence.tracks()) {
            for (const model::Segment& s : t.segments) {
                if (s.isFiller() || ids.contains(s.clipId)) continue;
                if (const auto clip = composer_->resolve(s.binId, s.clipId)) {
                    items << *clip;
                    ids << clip->id;
                }
            }
        }
    }
    model::SettingsFile& file = settings_->file(model::SettingsScope::User);
    if (file.versions(QStringLiteral("Export")).isEmpty()) {
        file.ensure({QStringLiteral("Export")});
        settings_->commit(model::SettingsScope::User);
    }
    const QList<int> versions = file.versions(QStringLiteral("Export"));
    QStringList names;
    int active = 0;
    for (int i = 0; i < versions.size(); ++i) {
        const model::SettingEntry& e = file.entries.at(versions.at(i));
        names << (e.name.isEmpty() ? tr("Untitled") : e.name);
        if (e.active) active = i;
    }
    QSettings remembered;
    QString folder = remembered.value(QStringLiteral("export/folder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // The first item's name, with characters that are not allowed in file names replaced.
    QString base = items.first().name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    ExportAsDialog dialog(QDir(folder).filePath(base + QLatin1String(".aaf")), names, active, this);
    connect(&dialog, &ExportAsDialog::optionsRequested, this, [this, versions](int i) {
        if (i < 0 || i >= versions.size()) return;
        if (SettingsWindow* w = showSettings()) {
            w->showTab(SettingsWindow::Tab::User);
            w->list(model::SettingsScope::User)->openEntry(versions.at(i));
        }
    });
    const bool ok = exportHook_ ? exportHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || dialog.path().isEmpty()) return;
    // The setting chosen is the one in use from now on.
    if (const int chosen = dialog.setting(); chosen >= 0 && chosen < versions.size() && chosen != active) {
        file.setActive(versions.at(chosen));
        settings_->commit(model::SettingsScope::User);
    }
    if (settings_->text(QStringLiteral("Export"), QStringLiteral("exportAs")) != QLatin1String("aaf")) {
        ConsoleLog::instance().append(tr("Export to File: only AAF can be exported so far (Export As in the Export setting)."));
        return;
    }
    remembered.setValue(QStringLiteral("export/folder"), QFileInfo(dialog.path()).absolutePath());
    interop::aaf::ExportOptions options;
    options.includeVideo = settings_->flag(QStringLiteral("Export"), QStringLiteral("includeVideo"));
    options.includeAudio = settings_->flag(QStringLiteral("Export"), QStringLiteral("includeAudio"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const interop::aaf::ExportResult result = interop::aaf::exportItems(items, dialog.path(), options);
    QApplication::restoreOverrideCursor();
    if (!result.ok) {
        ConsoleLog::instance().append(tr("Export to File: %1").arg(result.error));
        return;
    }
    ConsoleLog::instance().append(tr("Export to File: %1: %n item(s)", "", static_cast<int>(items.size()))
                                      .arg(QDir::toNativeSeparators(dialog.path())));
    for (const QString& w : result.warnings) ConsoleLog::instance().append(tr("Export to File: %1").arg(w));
}

// Import transcodes each file into new managed media (media::transcodeClip) with a progress dialog.
// A file that fails is named in the Console; once Cancel was clicked, the first failure ends the
// import. Returns the new items' ids, the AAF ones first.
QStringList MainWindow::importFiles(const QString& binId, const QStringList& allPaths, const QString& resolution) {
    // AAF files are compositions, not media (p.547).
    QStringList paths;
    QStringList aaf;
    for (const QString& p : allPaths) (p.endsWith(QLatin1String(".aaf"), Qt::CaseInsensitive) ? aaf : paths) << p;
    QStringList fromAaf = importAaf(binId, aaf);
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || paths.isEmpty()) return fromAaf;
    const media::LinkOptions options = linkOptions();
    // Import settings (p.1196-1197): numbered stills as one clip, and how
    // long a single picture lasts.
    QList<model::BinItem> clips;
    int skipped = 0;
    if (settings_->flag(QStringLiteral("Import"), QStringLiteral("autodetectSequences"))) {
        clips = media::linkFiles(paths, options, &skipped);
    } else {
        for (const QString& path : paths) {
            const auto info = media::probe(path);
            const auto clip = info ? media::linkClip(*info, options) : std::nullopt;
            if (clip) {
                clips << *clip;
            } else {
                ++skipped;
            }
        }
    }
    const int seconds = std::max(settings_->number(QStringLiteral("Import"), QStringLiteral("frameImportDuration")), 1);
    media::MediaCreation creation = mediaCreation(QStringLiteral("import"));
    if (const media::Resolution* r = media::findResolution(resolution)) creation.resolution = *r;
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("importProgress"));
    QStringList ids;
    for (int i = 0; i < clips.size(); ++i) {
        model::BinItem clip = clips.at(i);
        const auto link = media::linkOf(clip);
        // A still picture lasts frameImportDuration seconds.
        if (link && link->videoStream >= 0 && link->channels == 0 && link->frames == 1) {
            clip.durationFrames = std::max<std::int64_t>(
                1, seconds * options.editRate.num() / std::max<std::int64_t>(options.editRate.den(), 1));
        }
        progress.setLabelText(tr("Importing %1 (%2 of %3)").arg(clip.name).arg(i + 1).arg(clips.size()));
        const std::function<bool(double)> step = progressOn(progress);
        creation.progress = [&](double done) { return step((i + done) / static_cast<double>(clips.size())); };
        QString error;
        const auto made = media::transcodeClip(clip, 0, -1, creation, &error);
        if (!made) {
            ConsoleLog::instance().append(tr("Import: %1: %2").arg(clip.name, error));
            if (progress.wasCanceled()) break;
            continue;
        }
        doc->addItem(*made);
        ids << made->id;
    }
    progress.reset();
    ids = fromAaf + ids;
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    if (skipped > 0) ConsoleLog::instance().append(tr("Import: %n file(s) are not media that can be imported.", "", skipped));
    return ids;
}

// Consolidate/Transcode on the active bin's selection: the dialog (initialised from the Media
// Creation setting's transcode tab), then the Copying Media Files question for master clips, then the
// work, either now with a progress dialog or as Background Queue jobs. Both end in applyConsolidate().
void MainWindow::consolidateTranscode() {
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty() || !project_) return;
    BinDocument& doc = view->document();
    QList<model::BinItem> selection;
    bool sequences = false;
    bool masters = false;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = doc.bin().item(id)) {
            selection << *item;
            sequences = sequences || item->kind == model::BinItemKind::Sequence;
            masters = masters || item->kind == model::BinItemKind::MasterClip || item->kind == model::BinItemKind::AudioClip;
        }
    }
    ConsolidateDialog dialog(project_->settings.format.editRate, sequences, this);
    const media::MediaCreation transcode = mediaCreation(QStringLiteral("transcode"));
    dialog.setDrive(transcode.videoDrive);
    dialog.setResolution(transcode.resolution.id);
    dialog.setBitDepth(transcode.bitDepth);
    const bool ok = consolidateHook_ ? consolidateHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    media::ConsolidateOptions options = dialog.options();
    const bool background = dialog.isBackground();
    if (options.mode == media::ConsolidateOptions::Mode::Consolidate && masters) {
        // The Copying Media Files dialog (p.352).
        const std::optional<bool> keep =
            copyingMediaHook_ ? copyingMediaHook_() : ConsolidateDialog::askKeepMastersLinked(this);
        if (!keep) return;
        options.keepMastersLinked = *keep;
    }
    options.creation.raster = transcode.raster;
    options.creation.project = transcode.project;
    options.creation.when = QDateTime::currentDateTimeUtc();
    QStringList taken;
    for (const model::BinItem& item : doc.bin().items) taken << item.name;
    if (background) {
        // A job for each item, run while editing goes on (p.356). The jobs
        // read the project's clips as they are now.
        auto clips = std::make_shared<QHash<QString, model::BinItem>>();
        for (const model::BinItem& item : projectItems()) clips->insert(item.id, item);
        for (const BinDocument* open : binContainer_->openDocuments()) {
            for (const model::BinItem& item : open->bin().items) clips->insert(item.id, item);
        }
        const QString binId = doc.nodeId();
        for (const model::BinItem& item : selection) {
            backgroundQueue_->add(item.name, binId, [item, binId, clips, taken, options](
                                                       const std::function<bool(double)>& progress) {
                media::ConsolidateOptions o = options;
                o.creation.progress = progress;
                const media::ClipLookup lookup = [clips](const QString&, const QString& id) -> std::optional<model::BinItem> {
                    const auto it = clips->constFind(id);
                    if (it == clips->constEnd()) return std::nullopt;
                    return *it;
                };
                return media::consolidate({item}, binId, lookup, taken, o);
            });
        }
        showTool(QStringLiteral("BackgroundQueue"));
        return;
    }
    // In the foreground: one run over the whole selection, with the open bins as the clip lookup.
    QProgressDialog progress(dialog.isTranscode() ? tr("Transcoding...") : tr("Consolidating..."), tr("Cancel"), 0, 1000,
                             this);
    progress.setObjectName(QStringLiteral("consolidateProgress"));
    options.creation.progress = progressOn(progress);
    const media::ConsolidateResult result =
        media::consolidate(selection, doc.nodeId(), [this](const QString& binId, const QString& clipId) {
            return composer_->resolve(binId, clipId);
        }, taken, options);
    progress.reset();
    applyConsolidate(doc.nodeId(), result);
}

// Adds the new clips and sequences to the bin (opening it if needed) and selects them, stores the
// changed clips, reports what was skipped, and drops cached frames so the monitors read the new media.
void MainWindow::applyConsolidate(const QString& binId, const media::ConsolidateResult& result) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc) return;
    QStringList taken;
    for (const model::BinItem& item : doc->bin().items) taken << item.name;
    QStringList ids;
    for (model::BinItem item : result.added) {
        // Jobs that ran apart may have chosen the same ".new.01".
        static const QRegularExpression numbered(QStringLiteral("^(.*)\\.(new|old)\\.(\\d+)$"));
        if (taken.contains(item.name)) {
            if (const auto m = numbered.match(item.name); m.hasMatch()) {
                item.name = media::numberedName(m.captured(1), QLatin1Char('.') + m.captured(2), taken);
            }
        }
        taken << item.name;
        doc->addItem(item);
        ids << item.id;
    }
    for (const model::BinItem& item : result.changed) storeClip(binId, item);
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    for (const QString& why : result.skipped) ConsoleLog::instance().append(tr("Consolidate/Transcode: %1").arg(why));
    if (!result.error.isEmpty()) ConsoleLog::instance().append(tr("Consolidate/Transcode: %1").arg(result.error));
    frames_->clear();
}

// Every file under the folder, subfolders too, in name order; files that are not media are skipped by
// linkFiles().
QString MainWindow::linkVolume(const QString& folder) {
    if (!binContainer_->hasProject()) return {};
    const QString id = binContainer_->newBin();
    if (id.isEmpty()) return {};
    binContainer_->renameNode(id, QDir(folder).dirName());
    QStringList files;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    files.sort();
    linkFiles(id, files);
    refreshSourceBins();
    return id;
}

// An open panel is brought forward; a closed one floats near the window's top left at its first size.
Panel* MainWindow::showTool(const QString& panelId) {
    Panel* panel = panels_->panel(panelId);
    if (!panel) return nullptr;
    if (panels_->isOpen(panel)) {
        panels_->showPanel(panel);
        return panel;
    }
    // Their first sizes are ours; the guide gives none.
    QSize size(260, 220);
    if (panelId == QLatin1String("CommandPalette")) size = QSize(820, 280);
    if (panelId == QLatin1String("Calculator")) size = QSize(230, 260);
    if (panelId == QLatin1String("Console")) size = QSize(560, 320);
    if (panelId == QLatin1String("Keyboard")) size = QSize(720, 320);
    if (panelId == QLatin1String("EffectPalette")) size = QSize(480, 420);
    if (panelId == QLatin1String("EffectEditor")) size = QSize(340, 520);
    if (panelId == QLatin1String("AudioMixer")) size = QSize(560, 440);
    if (panelId == QLatin1String("AudioTool")) size = QSize(300, 340);
    if (panelId == QLatin1String("MediaTool")) size = QSize(640, 320);
    if (panelId == QLatin1String("SourceBrowser")) size = QSize(960, 600);
    const QPoint at = mapToGlobal(QPoint(80, 90));
    panels_->floatPanel(panel, QRect(at, size));
    panels_->activate(panel);
    return panel;
}

// Writes to the Console, one line per track: filler, nothing, or the segment's clip, its clip frame
// at the position and the segment's first and last frame in the sequence.
void MainWindow::positionInfo() {
    // The sequence in the Record monitor at its position (p.101).
    const model::MonitorState& record = composer_->state(MonitorId::Record);
    ConsoleLog& log = ConsoleLog::instance();
    if (record.isEmpty() || record.current()->item.kind != model::BinItemKind::Sequence) {
        log.append(tr("Get Position Info: no sequence in the Record monitor."));
    } else {
        const model::BinItem& item = record.current()->item;
        const model::Sequence seq = model::Sequence::fromItem(item);
        const std::int64_t at = record.position();
        log.append(tr("Position Info: %1 at %2 (frame %3)")
                       .arg(item.name, record.tracking(model::TrackingFormat::Master))
                       .arg(at));
        for (const QString& name : seq.trackNames()) {
            const model::Track* t = seq.track(name);
            std::int64_t start = 0;
            const int i = t ? t->segmentAt(at, &start) : -1;
            if (i < 0) {
                log.append(QStringLiteral("  %1: -").arg(name));
                continue;
            }
            const model::Segment& seg = t->segments.at(i);
            if (seg.isFiller()) {
                log.append(QStringLiteral("  %1: %2").arg(name, tr("Filler")));
            } else {
                log.append(tr("  %1: %2, clip frame %3, segment %4 to %5")
                               .arg(name, seg.name)
                               .arg(seg.sourceIn + at - start)
                               .arg(start)
                               .arg(start + seg.length - 1));
            }
        }
    }
    showTool(QStringLiteral("Console"));
}

// Pushes the active settings into every window. Runs on each Settings::changed (any setting, any
// scope), so each part here must be cheap and safe to repeat. A new setting in settings.json takes
// effect only once it is read here or by its window.
void MainWindow::applySettings() {
    const Settings& s = *settings_;
    // Video Display (p.1224) and Full Screen Playback (p.1190-1191) settings.
    composer_->setHighQualityScaling(s.flag(QStringLiteral("Video Display"), QStringLiteral("highQualityScaling")));
    if (FullScreenView* view = fullScreen_.data()) {
        view->setReformat(FullScreenView::reformatFrom(s.text(QStringLiteral("Full Screen Playback"), QStringLiteral("reformat"))));
        view->setFullScreenScaling(s.flag(QStringLiteral("Full Screen Playback"), QStringLiteral("fullScreen")));
    }
    // Bin settings (p.1152-1154).
    binContainer_->setAutoSaveMinutes(s.number(QStringLiteral("Bin"), QStringLiteral("autoSaveMinutes")));
    binContainer_->setFavoritesAtTop(s.text(QStringLiteral("Bin"), QStringLiteral("favoritesAt")) != QLatin1String("bottom"));
    binContainer_->setOpenBinsFloating(s.text(QStringLiteral("Bin"), QStringLiteral("openBin")) == QLatin1String("float"));
    binContainer_->setQuickFilterAllColumns(s.text(QStringLiteral("Bin"), QStringLiteral("quickFilter")) == QLatin1String("all"));
    // Audio Project settings (p.692, p.1150).
    const QString mixerMode = s.text(QStringLiteral("Audio Project"), QStringLiteral("mixerMode"));
    if (const auto mode = AudioMixer::modeFromName(mixerMode); mode && mixerMode != appliedMixerMode_) {
        appliedMixerMode_ = mixerMode;
        audioMixer_->setMode(*mode);
    }
    const QString mix = s.text(QStringLiteral("Audio Project"), QStringLiteral("mixMode"));
    if (!mix.isEmpty()) audioMixer_->setMixMode(mix);
    // General settings (p.1192).
    binContainer_->setDefaultStartTimecode(s.text(QStringLiteral("General"), QStringLiteral("defaultStartTc")));

    // Composer settings (p.1158-1160).
    const QString composer = QStringLiteral("Composer");
    Composer::Display d;
    d.infoRow = s.text(composer, QStringLiteral("dataDisplay")) != QLatin1String("off");
    const QString rows = s.text(composer, QStringLiteral("buttonDisplay"));
    d.buttonRows = rows == QLatin1String("off") ? 0 : rows == QLatin1String("one") ? 1 : 2;
    d.centerDuration = s.flag(composer, QStringLiteral("centerDuration"));
    d.markers = s.flag(composer, QStringLiteral("showComposerMarkers"));
    d.spannedMarkers = s.flag(composer, QStringLiteral("showSpannedMarkers"));
    composer_->setDisplay(d);
    // Buttons mapped with the Command Palette (p.97).
    composer_->setButtonLayout(s.value(composer, QStringLiteral("buttons")).toObject());
    QStringList tools;
    for (const QJsonValue& v : s.value(QStringLiteral("Tool Palette"), QStringLiteral("buttons")).toArray()) {
        tools << v.toString();
    }
    toolPalette_->setCommands(tools);
    toolPalette_->setShowLabels(s.flag(QStringLiteral("Interface"), QStringLiteral("toolPaletteLabels")));
    // The Keyboard setting (p.1202): the keys that differ from the default.
    keyboard_->setDifferences(s.value(QStringLiteral("Keyboard"), QStringLiteral("keys")).toObject());
    keyboardPalette_->refresh();
    if (Panel* k = panels_->panel(QStringLiteral("Keyboard"))) {
        const model::SettingsFile& user = s.file(model::SettingsScope::User);
        const int i = user.activeIndex(QStringLiteral("Keyboard"));
        const QString name = i >= 0 && !user.entries.at(i).name.isEmpty() ? user.entries.at(i).name : keyboard_->layoutName();
        k->setTitle(tr("Keyboard - %1").arg(name));
    }
    updateWorkspaceButtons();

    // Timeline settings (p.1218-1220), with the Composer's Edit and Move
    // tabs and the Trim settings (p.1223), which the Timeline carries out.
    const QString tl = QStringLiteral("Timeline");
    Timeline::Options o;
    o.toolbar = s.flag(tl, QStringLiteral("showToolbar"));
    o.markedRegion = s.flag(tl, QStringLiteral("showMarkedRegion"));
    const QString movement = s.text(tl, QStringLiteral("movementDuringPlay"));
    o.movement = movement == QLatin1String("scroll") ? Timeline::Options::Movement::Scroll
                 : movement == QLatin1String("none") ? Timeline::Options::Movement::None
                                                     : Timeline::Options::Movement::Page;
    o.onlyOneSegmentTool = s.flag(tl, QStringLiteral("onlyOneSegmentTool"));
    o.defaultSegmentTool = s.text(tl, QStringLiteral("defaultSegmentTool")) == QLatin1String("insert")
                               ? model::EditKind::SpliceIn
                               : model::EditKind::Overwrite;
    o.activateRecordAfterEdit = s.flag(composer, QStringLiteral("activateRecordAfterEdit"));
    o.stopAtHead = s.flag(composer, QStringLiteral("stopAtHead"));
    o.stopAtTail = s.flag(composer, QStringLiteral("stopAtTail"));
    o.stopAtMarkers = s.flag(composer, QStringLiteral("stopAtMarkers"));
    o.ignoreTrackSelectors = s.flag(composer, QStringLiteral("ignoreTrackSelectors"));
    o.trimAutoFocus = s.flag(QStringLiteral("Trim"), QStringLiteral("autoFocus"));
    timeline_->setOptions(o);

    // Choosing another Workspace View version in the Settings list switches
    // to that workspace.
    if (!syncingWorkspaces_) {
        const model::SettingsFile& user = s.file(model::SettingsScope::User);
        const int w = user.activeIndex(QStringLiteral("Workspace View"));
        const QString name = w >= 0 ? user.entries.at(w).name : QString();
        if (!name.isEmpty() && name != workspaces_->active() && workspaces_->contains(name)) {
            QMetaObject::invokeMethod(this, [this, name] { workspaces_->activate(name); }, Qt::QueuedConnection);
        }
    }
    panels_->setBlankPanelOnClose(s.flag(QStringLiteral("Interface"), QStringLiteral("closingDockedToolLeavesBlank")));

    // Interface settings (p.1200-1201).
    const QString ui = QStringLiteral("Interface");
    toolTips_->enabled = s.flag(ui, QStringLiteral("showToolTips"));
    composer_->setSourceRecordColors(s.flag(ui, QStringLiteral("composerColors")));
    workspaces_->setMoveBehavior(s.text(ui, QStringLiteral("workspaceMove")) == QLatin1String("saved")
                                     ? WorkspaceManager::MoveBehavior::LastSavedState
                                     : WorkspaceManager::MoveBehavior::LastKnownState);
}

// Keeps the workspace button in the top right corner as the central area resizes.
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == centralWidget() && event->type() == QEvent::Resize) {
        placeWorkspaceButton();
    }
    return QMainWindow::eventFilter(watched, event);
}

// Commands that work whether or not a project is open and need nothing else.
void MainWindow::installHandlers() {
    commands_.setHandler(QStringLiteral("file.exit"), [] { QApplication::closeAllWindows(); });
    commands_.setHandler(QStringLiteral("help.about"), [this] { showAbout(); });
    commands_.setHandler(QStringLiteral("help.about_qt"), [] { QApplication::aboutQt(); });
}

// Handlers for commands that need the panels but not a project: workspaces, tools, media, windows,
// and the Composer's and Timeline's own commands. Installed once, after the menus are built.
void MainWindow::installPanelHandlers() {
    auto handle = [this](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), std::move(handler));
    };
    // W1-W12 switch to their workspaces (p.90-91).
    for (int i = 0; i < WorkspaceManager::kMaxWorkspaces; ++i) {
        commands_.setHandler(QStringLiteral("workspace.w%1").arg(i + 1), [this, i] {
            const QString name = workspaceButtons().value(i);
            if (!name.isEmpty()) workspaces_->activate(name);
        });
    }
    // The modifier key buttons only change keys mapped with them (p.97).
    for (const char* id : {"other.add_alt_key", "other.add_control_key", "other.add_option_key"}) {
        commands_.setHandler(QString::fromLatin1(id), [] {});
    }
    // Tools (ch.5).
    // Tools menu: each opens its panel through showTool().
    handle("tools.command_palette", [this] { showTool(QStringLiteral("CommandPalette")); });
    handle("tools.calculator", [this] { showTool(QStringLiteral("Calculator")); });
    handle("tools.console", [this] { showTool(QStringLiteral("Console")); });
    handle("tools.effect_palette", [this] { showTool(QStringLiteral("EffectPalette")); });
    handle("tools.effect_editor", [this] { showTool(QStringLiteral("EffectEditor")); });
    handle("tools.audio_mixer", [this] { showTool(QStringLiteral("AudioMixer")); });
    handle("tools.audio_tool", [this] { showTool(QStringLiteral("AudioTool")); });
    handle("tools.media_tool", [this] { showMediaTool(); });
    // File > Input > Source Browser, Tools > Source Browser (p.315, p.327).
    handle("file.input.source_browser", [this] { showSourceBrowser(); });
    handle("clip.relink.linked_media", [this] { relinkLinkedMedia(); });
    handle("clip.relink.managed_media", [this] { relinkManagedMedia(); });
    // Managed media (p.213, p.134, p.348-359).
    handle("file.input.import_media", [this] {
        showSourceBrowser();
        if (SourceBrowser* browser = sourceBrowser_.data()) browser->setImportMode(true);
    });
    handle("clip.consolidate_transcode", [this] { consolidateTranscode(); });
    // File > Output > Export to File (p.868).
    handle("file.output.export_to_file", [this] { exportToFile(); });
    handle("tools.background_queue_window", [this] { showTool(QStringLiteral("BackgroundQueue")); });
    handle("file.media.load_media_database", [this] { loadMediaDatabase(); });
    handle("file.media.refresh_media_directories", [this] { loadMediaDatabase(); });
    handle("file.media.media_creation_settings", [this] {
        const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
        const int i = f.activeIndex(QStringLiteral("Media Creation"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::Project);
            w->list(model::SettingsScope::Project)->openEntry(i);
        }
    });
    // Composer > Full Screen Playback (p.391), and again to leave it.
    handle("composer.full_screen_playback", [this] { toggleFullScreen(); });
    // The Digital Audio Scrub button (Play tab, p.676) turns scrubbing on
    // and off.
    commands_.action(QStringLiteral("play.digital_audio_scrub"))->setCheckable(true);
    handle("clip.modify.unlink_media", [this] { unlinkSelection(); });
    handle("tools.source_browser", [this] { showSourceBrowser(); });
    // The Tool Palette button (Other tab, p.392).
    handle("other.tool_palette", [this] { showTool(QStringLiteral("ToolPalette")); });
    // Windows > Workspaces > Properties: the active workspace's Workspace
    // View setting (p.89-90).
    handle("windows.workspaces.properties", [this] {
        writeWorkspaces();
        const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
        const int i = f.activeIndex(QStringLiteral("Workspace View"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::User);
            w->list(model::SettingsScope::User)->openEntry(i);
        }
    });
    // Windows menu: panels and workspaces.
    handle("windows.float_all_panels", [this] { panels_->floatAll(); });
    handle("windows.float_active_panel", [this] { panels_->floatActive(); });
    handle("windows.add_blank_panel", [this] { panels_->addBlankPanel(); });
    handle("windows.workspaces.new_workspace", [this] { newWorkspace(); });
    handle("windows.workspaces.delete_workspace", [this] { deleteWorkspace(); });
    handle("windows.workspaces.restore_to_saved", [this] { workspaces_->restoreToSaved(); });
    handle("windows.workspaces.restore_current_to_default", [this] { restoreCurrentToDefault(); });
    handle("windows.workspaces.save_current", [this] { workspaces_->saveCurrent(); });
    QAction* showBar = commands_.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"));
    showBar->setCheckable(true);
    handle("windows.workspaces.show_workspace_toolbar", [this, showBar] {
        workspaces_->setBarMode(showBar->isChecked() ? WorkspaceManager::BarMode::IconAndText
                                                     : WorkspaceManager::BarMode::Hidden);
    });
    handle("windows.next_window", [this] { panels_->cycleWindows(1); });
    handle("windows.previous_window", [this] { panels_->cycleWindows(-1); });
    handle("windows.next_tab", [this] { panels_->cycleTabs(); });
    // File > Close closes the active window.
    handle("file.close", [this] {
        if (Panel* p = panels_->activePanel()) panels_->closePanel(p);
    });
    handle("tools.timeline", [this] { panels_->showPanel(panels_->panel(QStringLiteral("Timeline"))); });
    handle("file.get_position_info", [this] { positionInfo(); });
    // Tools > Markers (p.420), one window, kept.
    handle("tools.markers", [this] {
        if (!markersWindow_) markersWindow_ = new MarkersWindow(*timeline_, this);
        markersWindow_->refresh();
        markersWindow_->show();
        markersWindow_->raise();
    });
    // Playing, moving and marking in the Composer (ch. 13).
    for (const QString& id : commands_.ids()) {
        if (Composer::handles(id)) handle(id.toLatin1().constData(), [this, id] { composer_->perform(id); });
        // Editing, tracks and undo (ch. 15, 19).
        if (Timeline::handles(id)) {
            handle(id.toLatin1().constData(), [this, id] {
                // Copy, Cut and Paste are the Timeline's unless a bin is active.
                const bool clipboard = id == QLatin1String("edit.copy") || id == QLatin1String("edit.cut") ||
                                       id == QLatin1String("edit.paste");
                if (clipboard && binsActive()) return;
                timeline_->perform(id);
            });
        }
    }

    // Keyboard-only commands work wherever the main window is active.
    for (const char* id : {"windows.next_window", "windows.previous_window", "windows.next_tab",
                           "windows.workspaces.save_current"}) {
        addAction(commands_.action(QString::fromLatin1(id)));
    }
    // Floating windows are separate windows: give them every command so
    // their shortcuts work there too.
    // QAction shortcuts only fire in a window that holds the action.
    auto giveCommands = [this](QWidget* window) {
        for (const QString& id : commands_.ids()) window->addAction(commands_.action(id));
    };
    connect(panels_, &PanelManager::floatingWindowCreated, this, giveCommands);
    connect(binContainer_, &BinContainer::floatingContainerCreated, this, giveCommands);
}

// Brings the menus and the workspace button up to date with the workspaces: check marks, the list of
// workspaces in Windows > Workspaces and in the button's menu, and whether the button shows.
void MainWindow::syncWorkspaceUi() {
    const bool barShown = workspaces_->barMode() != WorkspaceManager::BarMode::Hidden;
    commands_.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"))->setChecked(barShown);
    commands_.action(QStringLiteral("windows.workspaces.delete_workspace"))->setEnabled(workspaces_->canRemoveActive());

    // Windows > Workspaces lists the workspaces above its commands (p.86).
    if (auto* menu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_workspaces"))) {
        // The workspace entries are rebuilt each time; the menu's declared commands stay below them.
        qDeleteAll(workspaceActions_);
        workspaceActions_.clear();
        QAction* first = menu->actions().value(0);
        for (const QString& name : workspaces_->names()) {
            auto* a = new QAction(name, menu);
            a->setCheckable(true);
            a->setChecked(name == workspaces_->active());
            a->setProperty(kDynamicMenuItem, true);
            connect(a, &QAction::triggered, this, [this, name] { workspaces_->activate(name); });
            menu->insertAction(first, a);
            workspaceActions_ << a;
        }
        QAction* separator = new QAction(menu);
        separator->setSeparator(true);
        separator->setProperty(kDynamicMenuItem, true);
        menu->insertAction(first, separator);
        workspaceActions_ << separator;
    }

    // The menu behind the workspace button (p.89).
    QMenu* menu = workspaceButton_->menu();
    menu->clear();
    for (const QString& name : workspaces_->names()) {
        QAction* a = menu->addAction(WorkspaceBar::iconFor(name, false), name.toUpper());
        a->setCheckable(true);
        a->setChecked(name == workspaces_->active());
        connect(a, &QAction::triggered, this, [this, name] { workspaces_->activate(name); });
    }
    menu->addSeparator();
    for (const char* id : {"windows.workspaces.save_current", "windows.workspaces.restore_current_to_default",
                           "windows.workspaces.new_workspace", "windows.workspaces.delete_workspace"}) {
        menu->addAction(commands_.action(QString::fromLatin1(id)));
    }
    menu->addSeparator();
    QAction* show = menu->addAction(tr("Show Workspace Bar"));
    connect(show, &QAction::triggered, this,
            [this] { workspaces_->setBarMode(WorkspaceManager::BarMode::IconAndText); });
    workspaceButton_->setIcon(WorkspaceBar::iconFor(workspaces_->active(), true));
    workspaceButton_->setVisible(!barShown);
    placeWorkspaceButton();
}

void MainWindow::placeWorkspaceButton() {
    if (QWidget* area = centralWidget()) {
        workspaceButton_->move(area->width() - workspaceButton_->width() - 2, 2);
        workspaceButton_->raise();
    }
}

void MainWindow::newWorkspace() {
    NewWorkspaceDialog dialog(*workspaces_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!workspaces_->create(dialog.name(), dialog.basedOn(), &error)) {
        QMessageBox::warning(this, tr("New Workspace"), error);
    }
}

void MainWindow::deleteWorkspace() {
    if (!workspaces_->canRemoveActive()) return;
    const auto answer =
        QMessageBox::question(this, tr("Delete Workspace"),
                              tr("Delete the workspace \"%1\"?").arg(workspaces_->active()),
                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Ok) workspaces_->removeActive();
}

void MainWindow::restoreCurrentToDefault() {
    // "A message box warns you that the action deletes your custom
    // workspace settings" (p.86).
    const auto answer = QMessageBox::warning(
        this, tr("Restore Current to Default"),
        tr("This deletes your custom settings for the workspace \"%1\".").arg(workspaces_->active()),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Ok) workspaces_->restoreCurrentToDefault();
}

// The text names the product, version, licence, extension count and font licence, and disclaims any
// tie to Avid.
QMessageBox* MainWindow::createAboutBox() {
    const QString name = QString::fromUtf8(branding::kProductName);
    auto* box = new QMessageBox(this);
    box->setObjectName(QStringLiteral("aboutBox"));
    box->setWindowTitle(tr("About %1").arg(name));
    box->setTextFormat(Qt::RichText);
    box->setText(tr("<h3>%1 %2</h3><p>%3</p>"
                    "<p>Licensed under the GNU General Public License v3.</p>"
                    "<p>%4 extension providers registered.</p>"
                    "<p>The interface font is Inter, under the SIL Open Font License 1.1.</p>"
                    "<p>Avid and Media Composer are trademarks of Avid Technology, Inc. "
                    "This product is not affiliated with or endorsed by Avid.</p>")
                     .arg(name, QString::fromUtf8(branding::kVersion), QString::fromUtf8(branding::kDescription))
                     .arg(providers_.all().size()));
    // The application's picture, drawn from the 512 px image so it stays
    // sharp on high-density screens.
    QPixmap picture(QStringLiteral(":/lumenis/ui/icons/about.png"));
    const qreal dpr = devicePixelRatioF();
    picture = picture.scaled(QSize(kAboutPictureSize, kAboutPictureSize) * dpr, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    picture.setDevicePixelRatio(dpr);
    box->setIconPixmap(picture);
    box->setStandardButtons(QMessageBox::Ok);
    // Configuration, Usage and Hardware (p.94, p.102-105).
    for (const QString& tab : {tr("Configuration"), tr("Usage"), tr("Hardware")}) {
        QPushButton* b = box->addButton(tab, QMessageBox::ActionRole);
        b->setObjectName(QStringLiteral("about") + tab);
        connect(b, &QPushButton::clicked, this, [this, tab] {
            AboutInfoDialog* info = createAboutInfo();
            info->setAttribute(Qt::WA_DeleteOnClose);
            info->showTab(tab);
            info->show();
        });
    }
    // Room for the text beside the picture.
    if (auto* label = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) label->setMinimumWidth(360);
    return box;
}

void MainWindow::showAbout() {
    QMessageBox* box = createAboutBox();
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->exec();
}

} // namespace lumenis::ui
