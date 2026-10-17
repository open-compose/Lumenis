#include "lumenis/ui/main_window.h"

#include "lumenis/media/playback.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/full_screen.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
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
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStorageInfo>
#include <QPixmap>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

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

QJsonObject tabs(const QString& id) {
    return {{QStringLiteral("tabs"), QJsonArray{id}}, {QStringLiteral("current"), 0}};
}

QJsonObject split(const char* orientation, const QJsonArray& sizes, const QJsonArray& children) {
    return {{QStringLiteral("split"), QLatin1String(orientation)},
            {QStringLiteral("sizes"), sizes},
            {QStringLiteral("children"), children}};
}

} // namespace

MainWindow::MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers, QWidget* parent)
    : QMainWindow(parent), commands_(commands), providers_(providers) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    resize(1280, 800);

    panels_ = new PanelManager(this);
    workspaces_ = new WorkspaceManager(*panels_, this);
    for (const QString& name : workspaces_->names()) {
        workspaces_->setDefaultLayout(name, defaultLayout());
    }

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

void MainWindow::createPanels() {
    // Its tab reads "Bins" in the pictures on p.240-242.
    binContainer_ = new BinContainer;
    panels_->addPanel(new Panel(QStringLiteral("BinContainer"), tr("Bins"), binContainer_));
    // Tools > Project: a Sidebar-only Bin Container named Project (p.243). As
    // a panel it takes part in workspaces, floating or not.
    panels_->addPanel(new Panel(QStringLiteral("Project"), tr("Project"), new BinSidebar(*binContainer_)));
    // The Composer window (ch. 13).
    composer_ = new Composer;
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
    connect(binContainer_, &BinContainer::filesDropped, this,
            [this](const QString& binId, const QStringList& paths) { linkFiles(binId, paths); });
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
}

QList<model::BinItem> MainWindow::projectItems(int* binCount) const {
    QList<model::BinItem> items;
    int count = 0;
    if (const model::ProjectBins* bins = binContainer_->bins()) {
        for (const model::BinNode* node : bins->bins()) {
            ++count;
            if (const auto bin = model::BinFile::read(bins->binPath(node->id))) items += bin->items;
        }
    }
    if (binCount) *binCount = count;
    return items;
}

void MainWindow::showMediaTool() {
    if (!project_) return;
    // The Media Tool Display dialog first (p.346). The drives are the
    // mounted volumes; media drives come with Phase 2.
    QStringList drives;
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
        if (v.isValid() && v.isReady() && !v.isReadOnly()) drives << (v.displayName().isEmpty() ? v.rootPath() : v.displayName());
    }
    MediaToolDisplayDialog dialog(project_->settings.name, drives, mediaTool_->display(), this);
    if (mediaToolDisplay_) {
        if (!mediaToolDisplay_(dialog)) return;
    } else if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    mediaTool_->setItems(projectItems(), dialog.display());
    showTool(QStringLiteral("MediaTool"));
}

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

void MainWindow::installProjectHandlers(bool projectOpen) {
    auto handle = [this, projectOpen](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), projectOpen ? std::move(handler) : std::function<void()>());
    };
    BinContainer* bins = binContainer_;
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
    QAction* sidebarOnly = commands_.action(QStringLiteral("bin.show_sidebar_only"));
    sidebarOnly->setCheckable(true);
    sidebarOnly->setChecked(bins->isSidebarOnly());
    handle("bin.show_sidebar_only", [bins, sidebarOnly] { bins->setSidebarOnly(sidebarOnly->isChecked()); });
    handle("windows.close_all_bins_but_active", [bins] { bins->closeAllBinsButActive(); });
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
        } else if (const Panel* p = panels_->activePanel(); p && p->id() == QLatin1String("Timeline")) {
            // Segments or tracks in the Timeline (p.621, p.636).
            timeline_->perform(QStringLiteral("edit.delete"));
        }
    });
    handle("edit.select_all", [this, bins] {
        if (binsActive()) bins->selectAll();
    });

    // The active bin's views, columns, sorting and sifting (ch. 10).
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

void MainWindow::closeEvent(QCloseEvent* event) {
    // Where each workspace was left, for the next session.
    writeWorkspaces();
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        emit closed();
    }
}

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

SettingsList* MainWindow::activeSettingsList() const {
    const QWidget* active = QApplication::activeWindow();
    if (settingsWindow_ && active == settingsWindow_) return settingsWindow_->currentList();
    if (siteSettingsWindow_ && active == siteSettingsWindow_) return siteSettingsWindow_->list();
    return nullptr;
}

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
    mediaTool_ = new MediaTool;
    panels_->addPanel(new Panel(QStringLiteral("MediaTool"), tr("Media Tool"), mediaTool_));
    // File > Input > Source Browser (p.312-318).
    sourceBrowser_ = new SourceBrowser;
    auto* browserPanel = new Panel(QStringLiteral("SourceBrowser"), tr("Source Browser"), sourceBrowser_);
    panels_->addPanel(browserPanel);
    connect(sourceBrowser_, &SourceBrowser::linkRequested, this,
            [this](const QString& binId, const QStringList& paths) { linkFiles(binId, paths); });
    connect(sourceBrowser_, &SourceBrowser::volumeBinRequested, this, [this](const QString& folder) { linkVolume(folder); });
    connect(sourceBrowser_, &SourceBrowser::loadRequested, this, [this](const QStringList& paths) { loadFiles(paths); });
    connect(sourceBrowser_, &SourceBrowser::settingsRequested, this, [this] {
        // The Settings button opens the Link settings (p.317).
        const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
        const int i = f.activeIndex(QStringLiteral("Link"));
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

std::vector<media::AudioPiece> MainWindow::soundOf(const model::LoadedClip& loaded,
                                                  const std::function<bool(const QString&)>& monitored) const {
    // Audio settings, Default Pan for mono tracks (p.1147).
    const bool alternating = settings_->text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
    return media::audioPieces(
        loaded.item, [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); },
        monitored, alternating);
}

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
            if (media::isLinked(item) && (ids.isEmpty() || ids.contains(item.id))) clips.append({doc->nodeId(), item});
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == centralWidget() && event->type() == QEvent::Resize) {
        placeWorkspaceButton();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::installHandlers() {
    commands_.setHandler(QStringLiteral("file.exit"), [] { QApplication::closeAllWindows(); });
    commands_.setHandler(QStringLiteral("help.about"), [this] { showAbout(); });
    commands_.setHandler(QStringLiteral("help.about_qt"), [] { QApplication::aboutQt(); });
}

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
    auto giveCommands = [this](QWidget* window) {
        for (const QString& id : commands_.ids()) window->addAction(commands_.action(id));
    };
    connect(panels_, &PanelManager::floatingWindowCreated, this, giveCommands);
    connect(binContainer_, &BinContainer::floatingContainerCreated, this, giveCommands);
}

void MainWindow::syncWorkspaceUi() {
    const bool barShown = workspaces_->barMode() != WorkspaceManager::BarMode::Hidden;
    commands_.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"))->setChecked(barShown);
    commands_.action(QStringLiteral("windows.workspaces.delete_workspace"))->setEnabled(workspaces_->canRemoveActive());

    // Windows > Workspaces lists the workspaces above its commands (p.86).
    if (auto* menu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_workspaces"))) {
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
