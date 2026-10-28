#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/ale.h"
#include "lumenis/interop/edl.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"
#include "lumenis/interop/avb_export.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/interop/avb_import.h"
#include "lumenis/ui/main_window.h"

#include "lumenis/media/audio_input.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/playback.h"

#include "lumenis/core/branding.h"
#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"
#include "lumenis/media/ltc.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/audio_suite.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/auto_sequence.h"
#include "lumenis/model/clip_audio.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/clip_audio_dialogs.h"
#include "lumenis/ui/audio_eq.h"
#include "lumenis/ui/audio_punch_in.h"
#include "lumenis/ui/modify_dialog.h"
#include "lumenis/ui/audio_suite.h"
#include "lumenis/ui/audio_track_effect.h"
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
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/multi_mix_dialog.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/interchange_dialogs.h"
#include "lumenis/ui/list_tool.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/keyboard_palette.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/clip_notes.h"
#include "lumenis/ui/find_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/relink_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timecode_window.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QPixmap>
#include <QProgressDialog>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <limits>

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
    // Shared bins are locked in the User profile's name (p.302).
    binContainer_->setLockUser(settings_->user());
    connect(settings_, &Settings::userChanged, this, [this] { binContainer_->setLockUser(settings_->user()); });
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
    // The bins are saved now, while this window is whole: BinContainer saves what is left when it
    // is deleted (after this destructor), and the signals that saving sends reach lambdas of this
    // window connected with other context objects (the Source Browser's bin list).
    if (binContainer_) {
        binContainer_->saveAll();
        disconnect(binContainer_, nullptr, this, nullptr);
        if (sourceBrowser_) disconnect(binContainer_, nullptr, sourceBrowser_.data(), nullptr);
    }
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
// The bin's clips in the view's order; AutoSequence itself is model::autoSequence.
QString MainWindow::autoSequence(bool noFiller, QString* error) {
    auto fail = [error](const QString& text) {
        if (error) *error = text;
        return QString();
    };
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty()) return fail(tr("Select clips in a bin first."));
    BinDocument& doc = view->document();
    if (doc.isReadOnly()) return fail(tr("The bin is locked by another user."));
    const QStringList selected = view->selectedIds();
    QList<model::BinItem> clips;
    for (const QString& id : view->visibleIds()) {
        if (const model::BinItem* item = selected.contains(id) ? doc.bin().item(id) : nullptr) clips << *item;
    }
    const model::AutoSequenceResult result = model::autoSequence(clips, doc.nodeId(), noFiller);
    if (!result.ok()) return fail(result.error);
    const model::BinItem* first = nullptr;
    for (const model::BinItem& c : clips) {
        if (c.durationFrames > 0 && c.kind != model::BinItemKind::Sequence) {
            first = &c;
            break;
        }
    }
    const Rational rate = first ? first->editRate : Rational(25);
    const TimecodeFormat format = TimecodeFormat::forRate(rate, first && model::isDropFrame(*first));
    model::BinItem item = model::newSequence(doc.bin(), rate, QDateTime::currentDateTimeUtc(),
                                             QString::fromStdString(Timecode(result.start, format).toString()));
    // "The same name as the tape name ... or the same name as the source file name ... with a .xx (.01,
    // .02, .03) extension" (p.500).
    QStringList names;
    for (const model::BinItem& i : doc.bin().items) names << i.name;
    for (int n = 1;; ++n) {
        item.name = QStringLiteral("%1.%2").arg(result.name).arg(n, 2, 10, QLatin1Char('0'));
        if (!names.contains(item.name)) break;
    }
    result.sequence.storeIn(item);
    doc.addItem(item);
    view->selectIds({item.id});
    // "The new sequence appears in the Record monitor and in the Timeline" (p.500).
    composer_->load(MonitorId::Record, doc.nodeId(), item);
    return item.id;
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
    // Bin items are looked up in the bins open in the Bin Container; items in closed bins are not found.
    composer_->setItemResolver([this](const QString& binId, const QString& itemId) -> std::optional<model::BinItem> {
        const BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
        if (!item) return std::nullopt;
        return *item;
    });
    composer_->setMobResolver([this](const MobId& mob) -> std::optional<std::pair<QString, model::BinItem>> {
        for (const BinDocument* doc : binContainer_->openDocuments()) {
            for (const model::BinItem& item : doc->bin().items) {
                if (item.kind != model::BinItemKind::Sequence && model::mobIdOf(item) == mob) return std::pair{doc->nodeId(), item};
            }
        }
        return std::nullopt;
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
            const auto answer = binDeleteHook_(clips, static_cast<int>(files.size()));
            if (!answer) return std::nullopt;
            choice = *answer;
        } else {
            QDialog dialog(this);
            dialog.setObjectName(QStringLiteral("deleteDialog"));
            dialog.setWindowTitle(tr("Delete"));
            auto* layout = new QVBoxLayout(&dialog);
            auto* clipBox = new QCheckBox(tr("Delete %n clip(s)", "", clips), &dialog);
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
    // Files dropped into a bin are linked (p.317); OpenTimelineIO files among them are imported
    // (p.969 step 1), and so are FCP 7 XML files (ours).
    connect(binContainer_, &BinContainer::filesDropped, this, [this](const QString& binId, const QStringList& paths) {
        QStringList media;
        QStringList timelines;
        QStringList fcp;
        for (const QString& p : paths) {
            (interop::otio::isOtioFile(p) ? timelines : interop::fcpxml::isFcpXmlFile(p) ? fcp : media) << p;
        }
        importOtio(binId, timelines);
        importFcpXml(binId, fcp);
        linkFiles(binId, media);
    });
    // Favorite Bins (p.293) and saved bin views (p.249) are User settings:
    // kept in the active Bin setting, and read back in applySettings().
    connect(binContainer_, &BinContainer::favoritesChanged, this, [this] {
        settings_->setValue(QStringLiteral("Bin"), QStringLiteral("favoriteBins"), FavoriteBins::toJson());
    });
    connect(binContainer_, &BinContainer::binViewsChanged, this, [this] {
        settings_->setValue(QStringLiteral("Bin"), QStringLiteral("binViews"), BinViewLibrary::toJson());
    });
    // A change to a bin someone else holds, refused (p.304): said in the
    // Console, as it can come from any edit.
    connect(binContainer_, &BinContainer::lockRefused, this, [this](const QString& binId) {
        if (!console_) return;
        const auto lock = binContainer_->lockOf(binId);
        const QString name = binContainer_->nodeName(binId);
        console_->log()->appendPlainText(lock ? tr("The bin \"%1\" is locked by %2; it cannot be changed.").arg(name, lock->describe())
                                              : tr("The bin \"%1\" is locked; it cannot be changed.").arg(name));
    });
    // A bin that could not be saved. Not modal: it may come from the
    // auto-save timer, or while the project closes.
    connect(binContainer_, &BinContainer::saveFailed, this, [this](const QString& bin, const QString& error) {
        const QString text = bin.isEmpty() ? tr("The bins could not be saved.\n%1").arg(error)
                                           : tr("The bin \"%1\" could not be saved; it stays open.\n%2").arg(bin, error);
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Save Bin"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("binSaveFailed"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
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
    // The View Menu (p.604).
    connect(timeline_, &Timeline::viewRequested, this, &MainWindow::chooseTimelineView);
    connect(timeline_, &Timeline::viewSaveRequested, this, &MainWindow::saveTimelineView);
    connect(timeline_, &Timeline::clipColorOptionsChanged, this, [this](const model::ClipColorOptions& o) {
        settings_->setValue(QStringLiteral("Timeline"), QStringLiteral("clipColor"), o.toJson());
    });
    // Mark Clip with segments selected in the Timeline marks "an In point at the start and an Out
    // point at the end of the selected segments" (p.622): from the earliest selected segment's first
    // frame to the latest one's last. The selection is the record sequence's, so it marks the Record
    // monitor only; with nothing selected the Composer marks the segment under the position.
    composer_->setMarkResolver([this](MonitorId monitor) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        const model::MonitorState& state = composer_->state(monitor);
        if (monitor != MonitorId::Record || timeline_->selection().isEmpty() || state.isEmpty() ||
            state.current()->item.kind != model::BinItemKind::Sequence) {
            return std::nullopt;
        }
        const model::Sequence seq = model::Sequence::fromItem(state.current()->item);
        std::optional<std::pair<std::int64_t, std::int64_t>> range;
        for (const model::SegmentRef& ref : timeline_->selection()) {
            const auto segment = model::segmentFor(seq, ref);
            if (!segment || segment->length <= 0) continue;
            const std::int64_t last = ref.start + segment->length - 1;
            range = range ? std::make_pair(std::min(range->first, ref.start), std::max(range->second, last))
                          : std::make_pair(ref.start, last);
        }
        return range;
    });
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
        model::BinItem item = model::newSequence(doc->bin(), project_->settings.format.editRate,
                                                 QDateTime::currentDateTimeUtc(), binContainer_->defaultStartTimecode());
        // Default Sync Locks On (p.1220).
        if (timeline_->options().defaultSyncLocks) model::syncLockAllTracks(item);
        return std::make_pair(doc->nodeId(), item);
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
    timeline_->setProjectHeight(project.settings.format.height);
    // "You will see a WaveformCache folder in the Avid Projects directory" (p.677): ours is in the
    // project's folder.
    timeline_->setWaveformFolder(QDir(project.folder).filePath(QStringLiteral("WaveformCache")));
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
    handle("file.open_bin", [this] {
        // "Bins have the file name extension .avb" (p.296): Media Composer's, and ours.
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Open a Bin"), {},
            tr("Bins (*.cthbin *.avb);;%1 bins (*.cthbin);;Media Composer bins (*.avb)").arg(QString::fromUtf8(branding::kProductName)));
        if (file.isEmpty()) return;
        QString error;
        if (openBinFile(file, &error).isEmpty()) QMessageBox::warning(this, tr("Open Bin"), error);
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
    };
    for (const auto& [id, handler] : binCommands) handle(id, handler);
    // Ctrl+L and Ctrl+K: Enlarge and Reduce Frame in a bin (p.250), Enlarge and Reduce Track with the
    // Timeline active (p.594).
    for (const auto& [id, step] : {std::pair{"edit.enlarge_frame", 1}, std::pair{"edit.reduce_frame", -1}}) {
        handle(id, [this, bins, steps = step] {
            // The Timeline Clip Notes window's pictures (p.485-486).
            if (clipNotesWindow_ && clipNotesWindow_->isActiveWindow()) {
                clipNotesWindow_->resizeFrames(steps);
                return;
            }
            const Panel* active = panels_->activePanel();
            if (!binsActive() && active && active->id() == QLatin1String("Timeline")) {
                timeline_->resizeTracks(steps);
                return;
            }
            BinView* v = bins->activeView();
            if (!v || v->mode() == model::BinViewMode::Text) return;
            if (steps > 0) {
                v->enlargeFrame();
            } else {
                v->reduceFrame();
            }
        });
    }
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
            // An Audio Multi-Mix setting opens the tool to change it, without Mix (p.734).
            if (tool == QLatin1String("multiMix")) multipleMixes(false);
            // A Timeline View setting: made active, which applies it (applySettings, p.604).
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

QStringList MainWindow::timelineViews() const {
    const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    QStringList names;
    for (int i : f.versions(QStringLiteral("Timeline View"))) {
        const QString n = f.entries.at(i).name.trimmed();
        if (!n.isEmpty() && !names.contains(n, Qt::CaseInsensitive)) names << n;
    }
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    return names;
}

QStringList MainWindow::timelineViewButtons() const {
    const QStringList views = timelineViews();
    const QJsonArray chosen = settings_->value(QStringLiteral("Timeline"), QStringLiteral("viewButtons")).toArray();
    QStringList out;
    for (int i = 0; i < 8; ++i) {
        const QString c = chosen.at(i).toString();
        out << (!c.isEmpty() ? c : views.value(i));
    }
    return out;
}

void MainWindow::chooseTimelineView(const QString& name) {
    model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    for (int i : f.versions(QStringLiteral("Timeline View"))) {
        if (f.entries.at(i).name.trimmed().compare(name, Qt::CaseInsensitive) != 0) continue;
        appliedTimelineView_.clear(); // apply it even if it was the last one applied
        f.setActive(i);
        settings_->commit(model::SettingsScope::User);
        return;
    }
}

void MainWindow::saveTimelineView(const QString& name, bool replace) {
    const QString kind = QStringLiteral("Timeline View");
    model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    const QJsonObject values{{QStringLiteral("display"), Timeline::displayToJson(timeline_->display())},
                             {QStringLiteral("clipColor"), timeline_->clipColorOptions().toJson()}};
    int index = -1;
    for (int i : f.versions(kind)) {
        if (f.entries.at(i).name.trimmed().compare(name, Qt::CaseInsensitive) == 0) index = i;
    }
    if (index < 0 && replace) return;
    if (index >= 0) {
        f.entries[index].values = values;
    } else {
        index = f.add(model::SettingEntry{kind, name, false, values});
    }
    // It is in use already: applySettings() leaves the Timeline as it is.
    appliedTimelineView_ = f.entries.at(index).name;
    f.setActive(index);
    settings_->commit(model::SettingsScope::User);
}

void MainWindow::updateTimelineViews() {
    const QStringList views = timelineViews();
    const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    const int active = f.activeIndex(QStringLiteral("Timeline View"));
    timeline_->setViews(views, active >= 0 ? f.entries.at(active).name.trimmed() : QString());
    // A TV button shows the first two characters of its view, as the W buttons do (p.90).
    const QStringList buttons = timelineViewButtons();
    for (int i = 0; i < 8; ++i) {
        const QString name = buttons.value(i);
        registerCommandIcon(QStringLiteral("timeline.view.tv%1").arg(i + 1),
                            name.isEmpty() ? labelIcon(QStringLiteral("TV%1").arg(i + 1))
                                           : labelIcon(name.left(2), !views.contains(name, Qt::CaseInsensitive)));
    }
    ButtonMapper::instance().refreshIcons();
    QJsonArray chosen = settings_->value(QStringLiteral("Timeline"), QStringLiteral("viewButtons")).toArray();
    QStringList assigned;
    for (int i = 0; i < 8; ++i) assigned << chosen.at(i).toString();
    commandPalette_->setTimelineViews(views, assigned);
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
    // Applying Effects opens Effect Editor (Timeline settings, p.1220).
    connect(timeline_, &Timeline::effectApplied, this, [this] {
        if (timeline_->options().openEffectEditor) showTool(QStringLiteral("EffectEditor"));
    });
    effectEditor_ = new EffectEditor(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("EffectEditor"), tr("Effect Editor"), effectEditor_));
    // Audio (ch. 21) and media (p.344-348).
    audioMixer_ = new AudioMixer(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("AudioMixer"), tr("Audio Mixer"), audioMixer_));
    // The Track Control panel's Solo and Mute are the mixer's (p.602, p.698-699).
    timeline_->setSoloMuteState([this](const QString& track, bool solo) {
        return solo ? audioMixer_->isSoloed(track) : audioMixer_->isMuted(track);
    });
    connect(timeline_, &Timeline::soloMuteRequested, audioMixer_, [this](const QString& track, bool solo) {
        audioMixer_->setSoloMute(track, solo, !(solo ? audioMixer_->isSoloed(track) : audioMixer_->isMuted(track)));
    });
    connect(audioMixer_, &AudioMixer::soloMuteChanged, timeline_->view(), qOverload<>(&QWidget::update));
    // The mode and mix are Audio Project settings; Live Mix is not saved
    // (p.692).
    connect(audioMixer_, &AudioMixer::modeChanged, this, [this](AudioMixer::Mode mode) {
        if (mode == AudioMixer::Mode::Live) return;
        settings_->setValue(QStringLiteral("Audio Project"), QStringLiteral("mixerMode"), AudioMixer::modeName(mode));
    });
    connect(audioMixer_, &AudioMixer::mixModeChanged, this, [this](const QString& value) {
        settings_->setValue(QStringLiteral("Audio Project"), QStringLiteral("mixMode"), value);
    });
    audioEq_ = new AudioEqTool(*timeline_);
    audioEq_->setSiteTemplates([this] { return siteEqTemplates(); });
    panels_->addPanel(new Panel(QStringLiteral("AudioEQ"), tr("Audio EQ"), audioEq_));
    // Audio Punch-In (p.747-753): the take becomes managed media in the chosen bin; the target tracks
    // are silent while it runs; monitoring plays the input through the output.
    audioPunchIn_ = new AudioPunchInTool(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("AudioPunchIn"), tr("Audio Punch-In"), audioPunchIn_));
    audioPunchIn_->setClipMaker([this](const AudioPunchInTool::Take& take, QString* error) { return punchInClip(take, error); });
    connect(audioPunchIn_, &AudioPunchInTool::audioToolRequested, this, [this] { showTool(QStringLiteral("AudioTool")); });
    connect(audioPunchIn_, &AudioPunchInTool::activeChanged, this, [this](bool active) {
        audio_->setMonitor(active && audioPunchIn_->monitorsInput() ? &audioPunchIn_->input() : nullptr);
    });
    // Audio Track Effects (p.790-795): changes made while playing are heard at once (p.793), by
    // starting play again where it is.
    audioTrackEffect_ = new AudioTrackEffectTool(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("AudioTrackEffect"), tr("Audio Track Effect"), audioTrackEffect_));
    auto* replay = new QTimer(this);
    replay->setSingleShot(true);
    replay->setInterval(60);
    connect(replay, &QTimer::timeout, this, [this] {
        if (composer_->isPlaying() && !composer_->isSlow() && std::abs(composer_->speed()) <= 3) composer_->play(composer_->speed());
    });
    connect(audioTrackEffect_, &AudioTrackEffectTool::previewChanged, replay, qOverload<>(&QTimer::start));
    // So are Live Mix moves (p.724), and changes made during Audio Loop Play (p.712).
    connect(audioMixer_, &AudioMixer::liveMixChanged, replay, qOverload<>(&QTimer::start));
    connect(audioMixer_, &AudioMixer::modeChanged, replay, qOverload<>(&QTimer::start));
    connect(timeline_, &Timeline::sequenceEdited, replay, [this, replay] {
        if (composer_->isLooping()) replay->start();
    });
    connect(audioMixer_, &AudioMixer::trackEffectToolRequested, this, [this](const QString& track) {
        audioTrackEffect_->select(track, 0);
        showTool(QStringLiteral("AudioTrackEffect"));
    });
    audioMixer_->setRenderer([this](const model::Segment& s, QString* error) { return renderAudioSuite(s, QString(), error); });
    // Split Track to Mono keeps a copy of the sequence as it was in its bin (p.735).
    connect(timeline_, &Timeline::sequenceCopyRequested, this, [this](const QString& binId, const model::BinItem& sequence) {
        BinDocument* doc = binContainer_->document(binId);
        if (!doc || doc->isReadOnly()) return;
        doc->addItem(model::duplicateItem(doc->bin(), sequence, QDateTime::currentDateTimeUtc()));
    });
    // Create Sequence Based on Selection (p.614): "A Select a bin dialog box opens (if you have more
    // than one bin open) ... A new sequence is placed in the selected bin, highlighted and ready to be
    // renamed." Ours: the record sequence's bin is offered first; the name is "<sequence>.Copy.NN".
    connect(timeline_, &Timeline::sequenceFromSelectionRequested, this, [this](const QString& binId, const model::BinItem& sequence) {
        QList<BinDocument*> open;
        for (BinDocument* doc : binContainer_->openDocuments()) {
            if (!doc->isReadOnly()) open << doc;
        }
        if (open.isEmpty()) return;
        std::stable_sort(open.begin(), open.end(), [&binId](BinDocument* a, BinDocument* b) {
            return a->nodeId() == binId && b->nodeId() != binId;
        });
        BinDocument* target = open.first();
        if (open.size() > 1) {
            QStringList names;
            for (BinDocument* doc : open) names << doc->name();
            int index = -1;
            if (binChoiceHook_) {
                index = binChoiceHook_(names);
            } else {
                bool ok = false;
                const QString chosen = QInputDialog::getItem(this, tr("Select a Bin"), tr("Bin:"), names, 0, false, &ok);
                if (ok) index = static_cast<int>(names.indexOf(chosen));
            }
            if (index < 0 || index >= open.size()) return;
            target = open.at(index);
        }
        model::BinItem item = model::duplicateItem(target->bin(), sequence, QDateTime::currentDateTimeUtc());
        target->addItem(item);
        if (BinView* view = binContainer_->viewFor(target)) view->selectIds({item.id});
    });
    connect(timeline_, &Timeline::trackEffectToolRequested, this, [this](const QString& track) {
        audioTrackEffect_->select(track, 0);
        showTool(QStringLiteral("AudioTrackEffect"));
    });
    // "When you select an insert button in the Track Control panel ... the controls for the plug-in
    // appear in the Audio Track Effect tool window" (p.792).
    connect(timeline_, &Timeline::trackInsertRequested, this, [this](const QString& track, int slot) {
        audioTrackEffect_->select(track, slot);
        showTool(QStringLiteral("AudioTrackEffect"));
    });
    // AudioSuite (p.796-800): effects on segments, rendered into managed media on the tool's drive.
    audioSuite_ = new AudioSuiteTool(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("AudioSuite"), tr("AudioSuite"), audioSuite_));
    audioSuite_->setRenderer([this](const model::Segment& s, const QString& drive, QString* error) {
        return renderAudioSuite(s, drive, error);
    });
    // Master clip processing (p.800-804): new clips and templates in the target bin; dropped items
    // and templates' clips are found in the bins.
    audioSuite_->setClipMaker([this](const AudioSuiteTool::MasterClipRender& r, QString* error) {
        return makeAudioSuiteClip(r, error);
    });
    audioSuite_->setItemResolver([this](const QString& binId, const QString& id) { return composer_->resolve(binId, id); });
    audioSuite_->setTemplateSource([this] {
        QList<model::BinItem> out;
        for (const BinDocument* d : binContainer_->openDocuments()) {
            for (const model::BinItem& i : d->bin().items) {
                if (model::audioSuiteTemplateOf(i)) out << i;
            }
        }
        return out;
    });
    connect(audioSuite_, &AudioSuiteTool::sourceRequested, this,
            [this](const QString& binId, const model::BinItem& clip) { composer_->load(MonitorId::Source, binId, clip); });
    audioTool_ = new AudioTool;
    panels_->addPanel(new Panel(QStringLiteral("AudioTool"), tr("Audio Tool"), audioTool_));
    connect(audioTool_, &AudioTool::playToneRequested, this, [this] { playCalibrationTone(); });
    connect(audioTool_, &AudioTool::createToneMediaRequested, this, [this] { createToneMedia(); });
    // Reset Peak also stops the tone (p.157).
    connect(audioTool_, &AudioTool::resetPeakClicked, this, &MainWindow::stopCalibrationTone);
    // The meters follow the output about 20 times a second.
    auto* meterTimer = new QTimer(this);
    meterTimer->setInterval(50);
    connect(meterTimer, &QTimer::timeout, this, &MainWindow::updateMeters);
    meterTimer->start();
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
    // Tools > List Tool (p.944): Load takes the Record monitor's sequence (p.945).
    listTool_ = new ListTool;
    panels_->addPanel(new Panel(QStringLiteral("ListTool"), tr("List Tool"), listTool_));
    connect(listTool_, &ListTool::loadRequested, this, [this] {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
        if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) {
            ConsoleLog::instance().append(tr("List Tool: load a sequence into the Record monitor first."));
            return;
        }
        listTool_->setSequence(loaded->item, clipResolver(loaded->item));
    });
    connect(listTool_, &ListTool::message, this, [](const QString& line) { ConsoleLog::instance().append(line); });
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
    // TV1-TV8 (p.605): the view chosen in the button's menu.
    connect(commandPalette_, &CommandPalette::timelineViewAssigned, this, [this](int index, const QString& name) {
        QJsonArray buttons = settings_->value(QStringLiteral("Timeline"), QStringLiteral("viewButtons")).toArray();
        while (buttons.size() <= index) buttons.append(QString());
        buttons[index] = name;
        settings_->setValue(QStringLiteral("Timeline"), QStringLiteral("viewButtons"), buttons);
    });
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
    auto pieces = media::audioPieces(
        loaded.item, [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); },
        monitored, alternating);
    // Bypass RT EQ (p.737): the EQ effects are not heard.
    if (audioEq_ && audioEq_->bypassed()) {
        for (media::AudioPiece& piece : pieces) piece.eq.reset();
    }
    // Live mode (p.724): "When you play the audio, the system uses your new settings" — each
    // track's Live Mix level on top of what it plays, like an external mixer's fader (ours), and
    // its Live Mix pan, once set, in place of its pans. Surround pans and multichannel tracks keep
    // theirs.
    if (audioMixer_ && audioMixer_->mode() == AudioMixer::Mode::Live) {
        for (media::AudioPiece& piece : pieces) {
            const QString track = piece.track.section(QLatin1Char('#'), 0, 0);
            if (track.isEmpty()) continue;
            const auto [db, pan] = audioMixer_->liveValue(track);
            if (audioMixer_->hasLivePan(track) && piece.speaker < 0 && !piece.surround) {
                piece.pan = pan;
                piece.panKeys.clear();
                media::panGains(pan, piece.gainDb, &piece.left, &piece.right);
            }
            if (db != 0.0) {
                const float gain = db <= model::kGainOff ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
                piece.left *= gain;
                piece.right *= gain;
                piece.scale *= gain;
            }
        }
    }
    // The Audio Mixer's Master fader, a gain on the whole mix (`scale` for automated pieces).
    const double master = audioMixer_ ? audioMixer_->masterGain() : 0.0;
    if (master != 0.0) {
        const float gain = master <= model::kGainOff ? 0.0f : static_cast<float>(std::pow(10.0, master / 20.0));
        for (media::AudioPiece& piece : pieces) {
            piece.left *= gain;
            piece.right *= gain;
            piece.scale *= gain;
        }
    }
    // The Mono mix "pans all the currently monitored tracks to center and ignores pan effects"
    // (p.691): each panned piece in the middle at its own gain.
    if (audioMixer_ && audioMixer_->monitorMix() == media::MonitorMix::Mono) {
        for (media::AudioPiece& piece : pieces) {
            if (piece.speaker >= 0) continue;
            if (piece.surround) {
                // A surround pan's volume is in `left`; in the middle, panned as the others.
                piece.surround.reset();
                piece.monoGain = false;
            }
            const float g = std::sqrt(piece.left * piece.left + piece.right * piece.right);
            piece.left = g * 0.70710678f;
            piece.right = g * 0.70710678f;
            piece.pan = 0;
            piece.panKeys.clear();
        }
    }
    return pieces;
}

// The pieces mixed for the Mix Mode Selection's speakers (p.689-692): a sequence in its Sequence Mix
// Format, a clip as a stereo one, in the channel order it was captured in (p.689).
std::shared_ptr<media::AudioMix> MainWindow::mixOf(const model::LoadedClip& loaded, std::vector<media::AudioPiece> pieces) const {
    auto mix = std::make_shared<media::AudioMix>(std::move(pieces), insertsOf(loaded));
    const model::AudioFormat format = loaded.item.kind == model::BinItemKind::Sequence
                                          ? model::Sequence::fromItem(loaded.item).mixFormat()
                                          : model::AudioFormat::Stereo;
    mix->setOutput(format, audioMixer_ ? audioMixer_->monitorMix() : media::MonitorMix::Stereo);
    // Convert Sample Rates When Playing (p.1148-1149): Never leaves other rates silent.
    if (settings_->text(QStringLiteral("Audio Project"), QStringLiteral("convertSampleRates")) == QLatin1String("never")) {
        mix->setRequiredRate(projectSampleRate());
    }
    return mix;
}

// The Audio Track Effects that play (p.790-795): the sequence's inserts, none with Ctrl+Bypass, and
// the value the Audio Track Effect tool is dragging in place of the saved one.
media::TrackInserts MainWindow::insertsOf(const model::LoadedClip& loaded) const {
    const bool all = audioTrackEffect_ && audioTrackEffect_->bypassedAll();
    media::TrackInserts inserts = media::trackInserts(loaded.item, all);
    if (all || !audioTrackEffect_ || !audioTrackEffect_->preview() || loaded.item.kind != model::BinItemKind::Sequence) {
        return inserts;
    }
    // Rebuild the previewed track's list from its slots, with the dragged one swapped in.
    const AudioTrackEffectTool::Preview& p = *audioTrackEffect_->preview();
    const model::Sequence seq = model::Sequence::fromItem(loaded.item);
    if (const model::Track* track = seq.track(p.track)) {
        QList<model::TrackInsert> list;
        for (int slot = 0; slot < model::kTrackInserts; ++slot) {
            const auto& in = slot == p.slot ? std::optional(p.insert) : track->inserts[static_cast<std::size_t>(slot)];
            if (in && !in->bypass) list << *in;
        }
        if (list.isEmpty()) {
            inserts.erase(p.track);
        } else {
            inserts[p.track] = list;
        }
    }
    return inserts;
}

// A sequence's audio track is heard when it is monitored in the Track Selector panel (p.395), not
// muted in the Audio Mixer, and soloed whenever any track is (p.698).
bool MainWindow::isHeard(const QString& track) const {
    if (!timeline_->isMonitored(track)) return false;
    if (audioPunchIn_ && audioPunchIn_->silencedTracks().contains(track)) return false;
    if (!audioMixer_) return true;
    if (audioMixer_->isMuted(track)) return false;
    return !audioMixer_->anySoloed() || audioMixer_->isSoloed(track);
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
        monitored = [this](const QString& track) { return isHeard(track); };
    }
    auto pieces = soundOf(*loaded, monitored);
    if (pieces.empty()) return;
    const Rational rate = loaded->item.editRate;
    media::AudioPlayer::PlayOptions options;
    options.limit = media::framesToSamples(out + in, rate);
    scrubStart_ = media::framesToSamples(position - out, rate);
    playingTone_ = false;
    if (!audio_->start(mixOf(*loaded, std::move(pieces)), scrubStart_, options)) return;
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
    playingTone_ = false;
    // Smooth audio scrub: sound up to three times normal speed, either
    // way; faster, it cuts out (p.675).
    if (speed == 0 || std::abs(speed) > 3) return;
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (!loaded) return;
    // A sequence plays the audio tracks that are heard (isHeard).
    std::function<bool(const QString&)> monitored;
    if (loaded->item.kind == model::BinItemKind::Sequence) {
        monitored = [this](const QString& track) { return isHeard(track); };
    }
    auto pieces = soundOf(*loaded, monitored);
    if (pieces.empty()) return;
    const std::int64_t at = media::framesToSamples(composer_->state(monitor).position(), loaded->item.editRate);
    media::AudioPlayer::PlayOptions options;
    options.speed = speed;
    // Audio settings (p.1147-1148): Audio Loop Play takes the tool buffer, other play the play buffer.
    options.bufferFrames = settings_->number(QStringLiteral("Audio"), composer_->isLooping() ? QStringLiteral("toolBuffer")
                                                                                             : QStringLiteral("playBuffer"));
    options.pitchCorrect = settings_->flag(QStringLiteral("Audio"), QStringLiteral("pitchCorrection"));
    if (!audio_->start(mixOf(*loaded, std::move(pieces)), at, options)) {
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

int MainWindow::modifySelection() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    QList<model::BinItem> clips;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* found = view->document().bin().item(id)) clips << *found;
    }
    if (clips.isEmpty()) return 0;
    ModifyDialog dialog(clips, this);
    const bool ok = modifyHook_ ? modifyHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return 0;
    QStringList errors;
    const QList<model::BinItem> changed = dialog.apply(clips, &errors);
    for (const model::BinItem& item : changed) storeClip(view->document().nodeId(), item);
    for (const QString& e : errors) ConsoleLog::instance().append(tr("Modify: %1").arg(e));
    if (!errors.isEmpty() && !modifyHook_) {
        QMessageBox::warning(this, tr("Modify"), errors.join(QLatin1Char('\n')));
    }
    return static_cast<int>(changed.size());
}

// The gain the dialog starts at: the first selected clip's (ours; the guide does not say).
int MainWindow::applyClipGain() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    QList<model::BinItem> clips;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* found = view->document().bin().item(id); found && model::takesClipAudio(*found)) clips << *found;
    }
    if (clips.isEmpty()) return 0;
    const double current = model::clipGain(clips.first());
    std::optional<double> db;
    if (clipAudioHooks_.gain) {
        db = clipAudioHooks_.gain(current);
    } else {
        ApplyGainDialog dialog(current, this);
        if (dialog.exec() == QDialog::Accepted) db = dialog.gain();
    }
    if (!db) return 0;
    for (model::BinItem& item : clips) {
        model::applyClipGain(item, *db);
        storeClip(view->document().nodeId(), item);
    }
    return static_cast<int>(clips.size());
}

int MainWindow::centerPan() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    QList<model::BinItem> clips;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* found = view->document().bin().item(id); found && model::takesClipAudio(*found)) clips << *found;
    }
    if (clips.isEmpty()) return 0;
    // "A dialog box opens and asks you to confirm the pan" (p.714).
    const bool ok = clipAudioHooks_.confirmCenterPan
                        ? clipAudioHooks_.confirmCenterPan()
                        : QMessageBox::question(this, tr("Center Pan"), tr("Pan the selected clips to the center?"),
                                                QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok;
    if (!ok) return 0;
    for (model::BinItem& item : clips) {
        model::centerClipPan(item);
        storeClip(view->document().nodeId(), item);
    }
    return static_cast<int>(clips.size());
}

// The new sequence goes into the same bin; the original "is retained and is unaffected" (p.754).
int MainWindow::autoVo() {
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return 0;
    const QStringList ids = view->selectedIds();
    const model::BinItem* found = ids.size() == 1 ? view->document().bin().item(ids.first()) : nullptr;
    if (!found || found->kind != model::BinItemKind::Sequence) {
        if (!clipAudioHooks_.autoVo) QMessageBox::information(this, tr("Auto VO"), tr("Select one sequence in the bin."));
        return 0;
    }
    const model::BinItem item = *found;
    const model::Sequence seq = model::Sequence::fromItem(item);
    QStringList tracks;
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Audio) tracks << t.name();
    }
    if (tracks.isEmpty()) return 0;
    std::optional<std::pair<QString, bool>> choice;
    if (clipAudioHooks_.autoVo) {
        choice = clipAudioHooks_.autoVo(tracks);
    } else {
        AutoVoDialog dialog(tracks, this);
        if (dialog.exec() == QDialog::Accepted) choice = std::pair{dialog.track(), dialog.keep()};
    }
    if (!choice) return 0;
    const auto made = model::autoVo(seq, choice->first, choice->second);
    if (!made) return 0;
    model::BinItem copy = model::duplicateItem(view->document().bin(), item, QDateTime::currentDateTimeUtc());
    copy.name = model::autoVoName(item.name);
    made->storeIn(copy);
    view->document().addItem(copy);
    return 1;
}

int MainWindow::readAudioTimecode() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    auto tell = [this](const QString& text) {
        ConsoleLog::instance().append(tr("Read Audio Timecode: %1").arg(text));
        if (clipAudioHooks_.message) {
            clipAudioHooks_.message(text);
        } else {
            QMessageBox::information(this, tr("Read Audio Timecode"), text);
        }
    };
    QList<model::BinItem> clips;
    int ignored = 0;
    int tracks = 1;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* found = view->document().bin().item(id);
        if (!found) continue;
        if (!model::takesClipAudio(*found)) {
            ++ignored;
            continue;
        }
        clips << *found;
        if (const auto link = media::linkOf(*found)) tracks = std::max(tracks, link->channels);
    }
    // "If any of the selected clips are not master clips or subclips, you will get a message, saying
    // that they will be ignored" (p.668).
    if (ignored > 0) tell(tr("%n selected item(s) that are not master clips or subclips will be ignored.", "", ignored));
    if (clips.isEmpty()) return 0;
    std::optional<ReadTimecodeChoice> choice;
    if (clipAudioHooks_.readTimecode) {
        choice = clipAudioHooks_.readTimecode(tracks);
    } else {
        ReadAudioTimecodeDialog dialog(tracks, this);
        if (dialog.exec() == QDialog::Accepted) {
            choice = ReadTimecodeChoice{dialog.userBits(), dialog.sourceTrack(), dialog.destination(), dialog.fill()};
        }
    }
    if (!choice) return 0;
    int read = 0;
    QStringList failures;
    for (model::BinItem& item : clips) {
        QString error;
        const auto tc = media::ltc::readClipTimecode(item, choice->track, choice->userBits, choice->fill, &error);
        if (!tc) {
            failures << QStringLiteral("%1: %2").arg(item.name, error);
            continue;
        }
        item.fields.insert(choice->destination, QString::fromStdString(tc->toString()));
        storeClip(view->document().nodeId(), item);
        ++read;
    }
    if (!failures.isEmpty()) tell(failures.join(QLatin1Char('\n')));
    return read;
}

// Each clip is converted once, however many selected sequences use it; it keeps its bin.
int MainWindow::changeSampleRate() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    QList<std::pair<QString, model::BinItem>> clips; // bin, clip
    QSet<QString> seen;
    auto add = [&](const QString& binId, const model::BinItem& clip) {
        const auto link = media::linkOf(clip);
        if (!link || link->channels <= 0 || seen.contains(clip.id)) return;
        seen.insert(clip.id);
        clips.append({binId, clip});
    };
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* found = view->document().bin().item(id);
        if (!found) continue;
        if (found->kind == model::BinItemKind::Sequence) {
            const model::Sequence seq = model::Sequence::fromItem(*found);
            for (const model::Track& t : seq.tracks()) {
                if (t.kind != model::TrackKind::Audio) continue;
                for (const model::Segment& s : t.segments) {
                    if (s.isFiller()) continue;
                    if (const auto clip = composer_->resolve(s.binId, s.clipId)) add(s.binId, *clip);
                }
            }
        } else if (model::takesClipAudio(*found)) {
            add(view->document().nodeId(), *found);
        }
    }
    if (clips.isEmpty()) return 0;
    std::optional<SampleRateChoice> choice;
    if (clipAudioHooks_.sampleRate) {
        choice = clipAudioHooks_.sampleRate();
    } else {
        QList<std::pair<QString, QString>> drives;
        for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.name, d.root});
        ChangeSampleRateDialog dialog(drives, this);
        if (dialog.exec() == QDialog::Accepted) {
            choice = SampleRateChoice{dialog.sampleRate(), dialog.filterSize(), dialog.deleteOriginal(), dialog.drive()};
        }
    }
    if (!choice) return 0;
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    if (!choice->drive.isEmpty()) {
        creation.videoDrive = choice->drive;
        creation.audioDrive = choice->drive;
    }
    creation.sampleRate = choice->rate;
    creation.resampleFilter = choice->filterSize;
    int changed = 0;
    QStringList failures;
    QStringList old;
    for (const auto& [binId, clip] : clips) {
        QString error;
        QStringList files;
        const auto made = media::resampleClipAudio(clip, creation, &files, &error);
        if (!made) {
            failures << QStringLiteral("%1: %2").arg(clip.name, error);
            continue;
        }
        storeClip(binId, *made);
        old << files;
        ++changed;
    }
    // "The system automatically deletes the original media after the conversion process completes"
    // (p.731): only managed media; a linked clip's own files are left alone (ours).
    if (choice->deleteOriginal && !old.isEmpty()) media::deleteMediaFiles(old, &mediaDb_);
    if (!failures.isEmpty()) {
        const QString text = failures.join(QLatin1Char('\n'));
        ConsoleLog::instance().append(tr("Change Sample Rate: %1").arg(text));
        if (clipAudioHooks_.message) {
            clipAudioHooks_.message(text);
        } else {
            QMessageBox::warning(this, tr("Change Sample Rate"), text);
        }
    }
    return changed;
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
    // "Defines the audio sample rate for the entire system for playing and recording" (p.1148): new
    // audio media is made at it.
    c.sampleRate = projectSampleRate();
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
            if (linkByMobId(item)) ++online;
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

bool MainWindow::linkByMobId(model::BinItem& item) const {
    if (item.kind != model::BinItemKind::MasterClip && item.kind != model::BinItemKind::AudioClip) return false;
    const auto set = mediaDb_.setOf(item.mobId);
    if (!set) return false;
    item.link = set->link().toJson();
    return true;
}

// A .cthbin is listed as it is. Writing .avb comes with Phase 3.4, so a Media Composer bin is read
// once into a bin of ours, kept in the project folder under "Other Bins/<key>/<name>.cthbin" (the key
// is from the .avb's path, so that bins of the same name from different projects do not meet), and
// that is listed under Other Bins. Opening the same .avb again while it is listed opens the bin
// made before, with whatever was changed in it since.
QString MainWindow::openBinFile(const QString& path, QString* error) {
    BinContainer* bins = binContainer_;
    if (!bins->bins()) {
        if (error) *error = tr("No project is open.");
        return {};
    }
    if (!path.endsWith(QLatin1String(".avb"), Qt::CaseInsensitive)) return bins->openOtherBin(path, error);
    const QFileInfo info(path);
    const QString key =
        QString::fromLatin1(QCryptographicHash::hash(info.absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    const QString converted = QDir(bins->bins()->folder())
                                  .filePath(QStringLiteral("Other Bins/%1/%2%3").arg(key, info.completeBaseName(), model::BinFile::suffix()));
    if (const QString existing = bins->bins()->binForPath(converted); !existing.isEmpty()) {
        bins->openBin(existing);
        return existing;
    }
    interop::avb::ImportOptions options;
    options.when = QDateTime::currentDateTimeUtc();
    const auto imported = interop::avb::importFile(path, options, error);
    if (!imported) return {};
    // An empty bin first: the sequences' segments name the bin by its Sidebar id, which it has once
    // it is listed.
    model::Bin bin;
    bin.name = info.completeBaseName();
    bin.display.customColumns = imported->customColumns;
    if (!QDir().mkpath(QFileInfo(converted).absolutePath())) {
        if (error) *error = tr("Cannot make the folder %1.").arg(QDir::toNativeSeparators(QFileInfo(converted).absolutePath()));
        return {};
    }
    if (!model::BinFile::write(converted, bin, error)) return {};
    const QString id = bins->openOtherBin(converted, error);
    BinDocument* doc = id.isEmpty() ? nullptr : bins->document(id);
    if (!doc) return {};
    // Master clips find their managed media by MobID, as with AAF (p.547).
    loadMediaDatabase();
    QSet<QString> itemIds;
    for (const model::BinItem& item : imported->items) itemIds.insert(item.id);
    int online = 0;
    for (model::BinItem item : imported->items) {
        if (linkByMobId(item)) ++online;
        if (item.kind == model::BinItemKind::Sequence) {
            model::Sequence sequence = model::Sequence::fromItem(item);
            for (const QString& name : sequence.trackNames()) {
                for (model::Segment& s : sequence.track(name)->segments) {
                    if (itemIds.contains(s.clipId)) s.binId = id;
                }
            }
            sequence.storeIn(item);
        }
        doc->addItem(item);
    }
    QString saveError;
    if (!bins->saveBin(doc, &saveError)) ConsoleLog::instance().append(tr("Open Bin: %1").arg(saveError));
    ConsoleLog::instance().append(tr("Open Bin: %1: %n item(s), %2 with media", "", static_cast<int>(imported->items.size()))
                                      .arg(info.fileName())
                                      .arg(online));
    for (const QString& w : imported->warnings) ConsoleLog::instance().append(tr("Open Bin: %1: %2").arg(info.fileName(), w));
    return id;
}

// Steps: collect the selection and the clips its sequences use, make sure an Export setting exists,
// ask for the file and setting (Export As dialog), then write the AAF with interop::aaf::exportItems,
// or, when the Export setting's Export As is Media Composer Bin (ours, docs/spec/avb.md "Export"), a
// bin with interop::avb::exportBin. Results and warnings go to the Console. The last folder exported
// to is remembered in QSettings.
void MainWindow::exportToFile() {
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty()) {
        ConsoleLog::instance().append(tr("Export to File: select the clips or sequences to export in a bin."));
        return;
    }
    const BinDocument& doc = view->document();
    QList<model::BinItem> selected;
    QStringList ids;
    for (const model::BinItem& item : doc.bin().items) {
        if (!view->selectedIds().contains(item.id)) continue;
        selected << item;
        ids << item.id;
    }
    // A sequence's clips go with it, so that it is not offline where it is read: in AAF as items of
    // their own, in a bin as reference clips (found through the resolver below).
    QList<model::BinItem> clips;
    QHash<QString, QString> binOf; // clip id -> the bin its segments name
    for (const model::BinItem& item : selected) {
        if (item.kind != model::BinItemKind::Sequence) continue;
        const model::Sequence sequence = model::Sequence::fromItem(item);
        for (const model::Track& t : sequence.tracks()) {
            for (const model::Segment& s : t.segments) {
                if (s.isFiller() || ids.contains(s.clipId)) continue;
                binOf.insert(s.clipId, s.binId);
                if (const auto clip = composer_->resolve(s.binId, s.clipId)) {
                    clips << *clip;
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
    // The file type of an Export As choice: its extension and its name in the save dialog.
    auto suffixOf = [](const QString& exportAs) {
        return exportAs == QLatin1String("avb")      ? QStringLiteral("avb")
               : exportAs == QLatin1String("ale")    ? QStringLiteral("ale")
               : exportAs == QLatin1String("fcpxml") ? QStringLiteral("xml")
                                                     : QStringLiteral("aaf");
    };
    auto filterOf = [](const QString& exportAs) {
        return exportAs == QLatin1String("avb")      ? tr("Media Composer bins (*.avb)")
               : exportAs == QLatin1String("ale")    ? tr("Avid Log Exchange (*.ale)")
               : exportAs == QLatin1String("fcpxml") ? tr("Final Cut Pro 7 XML (*.xml)")
                                                     : tr("AAF files (*.aaf)");
    };
    const QString exportAs = settings_->text(QStringLiteral("Export"), QStringLiteral("exportAs"));
    QSettings remembered;
    QString folder = remembered.value(QStringLiteral("export/folder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // The first item's name, with characters that are not allowed in file names replaced.
    QString base = selected.first().name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    ExportAsDialog dialog(QDir(folder).filePath(base + QLatin1Char('.') + suffixOf(exportAs)), names, active, this);
    dialog.setFileType(suffixOf(exportAs), filterOf(exportAs));
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
    const QString format = settings_->text(QStringLiteral("Export"), QStringLiteral("exportAs"));
    if (format != QLatin1String("aaf") && format != QLatin1String("avb") && format != QLatin1String("ale") &&
        format != QLatin1String("fcpxml")) {
        ConsoleLog::instance().append(tr("Export to File: only AAF, Avid Log Exchange, Media Composer bins and Final Cut Pro 7 XML "
                                         "can be exported so far (Export As in the Export setting)."));
        return;
    }
    const model::BinItem* firstSequence = nullptr;
    for (const model::BinItem& item : selected) {
        if (item.kind == model::BinItemKind::Sequence && !firstSequence) firstSequence = &item;
    }
    if (format == QLatin1String("fcpxml") && !firstSequence) {
        ConsoleLog::instance().append(tr("Export to File: Final Cut Pro 7 XML holds a sequence: select one."));
        return;
    }
    // The file name extension stays the format's (p.869), also when the setting was changed in the
    // dialog.
    QString path = dialog.path();
    const QString suffix = suffixOf(format);
    if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
        const QString old = QFileInfo(path).suffix();
        if (old.compare(QLatin1String("aaf"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("avb"), Qt::CaseInsensitive) == 0 ||
            old.compare(QLatin1String("ale"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0) {
            path.chop(old.size() + 1);
        }
        path += QLatin1Char('.') + suffix;
    }
    remembered.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    const bool includeVideo = settings_->flag(QStringLiteral("Export"), QStringLiteral("includeVideo"));
    const bool includeAudio = settings_->flag(QStringLiteral("Export"), QStringLiteral("includeAudio"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool written = false;
    QString error;
    QStringList warnings;
    qsizetype count = selected.size();
    if (format == QLatin1String("ale")) {
        // A shot log of the master clips and subclips selected (p.130, p.1174), with the bin's
        // columns.
        interop::ale::ExportOptions options;
        options.columns = doc.bin().display.columns + doc.bin().display.customColumns;
        const interop::ale::ExportResult result = interop::ale::exportFile(selected, path, options);
        written = result.ok;
        error = result.error;
        warnings = result.warnings;
        count = result.clips;
    } else if (format == QLatin1String("fcpxml")) {
        // The first sequence selected (an FCP XML file of ours holds one), its clips found in the bins.
        interop::otio::ExportOptions options;
        if (!includeVideo || !includeAudio) {
            const model::Sequence sequence = model::Sequence::fromItem(*firstSequence);
            for (const model::Track& t : sequence.tracks()) {
                if ((t.kind == model::TrackKind::Video && includeVideo) || (t.kind == model::TrackKind::Audio && includeAudio)) {
                    options.tracks << t.name();
                }
            }
            if (options.tracks.isEmpty()) options.tracks << QStringLiteral("-");
        }
        const model::Bin& source = doc.bin();
        options.resolve = [&](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
            if (!clipId.isEmpty()) {
                if (auto clip = composer_->resolve(binOf.value(clipId, doc.nodeId()), clipId)) return clip;
            }
            for (const model::BinItem& item : source.items) {
                if (model::mobIdOf(item) == mob) return item;
            }
            return std::nullopt;
        };
        const interop::otio::ExportResult result = interop::fcpxml::exportFile(*firstSequence, path, options);
        written = result.ok;
        error = result.error;
        warnings = result.warnings;
        count = 1;
        if (std::count_if(selected.begin(), selected.end(), [](const model::BinItem& i) { return i.kind == model::BinItemKind::Sequence; }) > 1) {
            warnings << tr("only the first sequence selected was written");
        }
    } else if (format == QLatin1String("avb")) {
        // A bin named for the file, with the source bin's columns and view; the sequences' clips
        // that are not selected, and subclips' masters, as reference clips.
        model::Bin bin;
        bin.name = QFileInfo(path).completeBaseName();
        bin.items = selected;
        bin.display = doc.bin().display;
        interop::avb::ExportOptions options;
        options.includeVideo = includeVideo;
        options.includeAudio = includeAudio;
        const model::Bin& source = doc.bin();
        options.resolve = [&](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
            if (!clipId.isEmpty()) {
                if (auto clip = composer_->resolve(binOf.value(clipId, doc.nodeId()), clipId)) return clip;
            }
            for (const model::BinItem& item : source.items) {
                if (model::mobIdOf(item) == mob) return item;
            }
            return std::nullopt;
        };
        const interop::avb::ExportResult result = interop::avb::exportBin(bin, path, options);
        written = result.ok;
        error = result.error;
        warnings = result.warnings;
        count += result.referenceClips;
    } else {
        interop::aaf::ExportOptions options;
        options.includeVideo = includeVideo;
        options.includeAudio = includeAudio;
        const interop::aaf::ExportResult result = interop::aaf::exportItems(selected + clips, path, options);
        written = result.ok;
        error = result.error;
        warnings = result.warnings;
        count += clips.size();
    }
    QApplication::restoreOverrideCursor();
    if (!written) {
        ConsoleLog::instance().append(tr("Export to File: %1").arg(error));
        return;
    }
    ConsoleLog::instance().append(tr("Export to File: %1: %n item(s)", "", static_cast<int>(count)).arg(QDir::toNativeSeparators(path)));
    for (const QString& w : warnings) ConsoleLog::instance().append(tr("Export to File: %1").arg(w));
}

// Each file is imported on its own, as importAaf() does with AAF files. The media a timeline names
// that a clip of the bin already stands for (the same MobID, interop::otio::mediaMobId) is that clip;
// the rest are offline master clips, to be relinked (p.969).
QStringList MainWindow::importOtio(const QString& binId, const QStringList& paths) {
    return importTimelines(binId, paths, false);
}

// FCP 7 XML goes through the OTIO import (interop/fcp_xml.h): the same rules, the same clips.
QStringList MainWindow::importFcpXml(const QString& binId, const QStringList& paths) {
    return importTimelines(binId, paths, true);
}

QStringList MainWindow::importTimelines(const QString& binId, const QStringList& paths, bool fcp) {
    if (paths.isEmpty()) return {};
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc) return {};
    QStringList ids;
    for (const QString& path : paths) {
        QString error;
        interop::otio::ImportOptions options;
        options.binId = doc->nodeId();
        options.when = QDateTime::currentDateTimeUtc();
        auto imported = fcp ? interop::fcpxml::importFile(path, options, &error) : interop::otio::importFile(path, options, &error);
        if (!imported) {
            ConsoleLog::instance().append(tr("Import: %1").arg(error));
            continue;
        }
        interop::otio::reuseClips(*imported, doc->bin().items);
        int sequences = 0;
        for (const model::BinItem& item : imported->items) {
            if (item.kind == model::BinItemKind::Sequence) ++sequences;
            doc->addItem(item);
            ids << item.id;
        }
        ConsoleLog::instance().append(tr("Import: %1: %n sequence(s), %2 clip(s)", "", sequences)
                                          .arg(QFileInfo(path).fileName())
                                          .arg(imported->items.size() - sequences));
        for (const QString& w : imported->warnings) ConsoleLog::instance().append(tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
    }
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    return ids;
}

// Each log is imported on its own, as importAaf() does with AAF files; the logs' custom columns are
// added to the bin's display.
QStringList MainWindow::importShotLogs(const QString& binId, const QStringList& paths) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || paths.isEmpty()) return {};
    QStringList ids;
    model::BinDisplay display = doc->bin().display;
    for (const QString& path : paths) {
        QString error;
        interop::ale::ImportOptions options;
        options.when = QDateTime::currentDateTimeUtc();
        const auto imported = interop::ale::importFile(path, options, &error);
        if (!imported) {
            ConsoleLog::instance().append(tr("Import: %1").arg(error));
            continue;
        }
        for (const model::BinItem& item : imported->items) {
            doc->addItem(item);
            ids << item.id;
        }
        for (const QString& column : imported->customColumns) {
            if (!display.customColumns.contains(column)) display.customColumns << column;
        }
        ConsoleLog::instance().append(
            tr("Import: %1: %n clip(s) from the shot log", "", static_cast<int>(imported->items.size())).arg(QFileInfo(path).fileName()));
        for (const QString& w : imported->warnings) ConsoleLog::instance().append(tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
    }
    if (display.customColumns != doc->bin().display.customColumns) doc->setDisplay(display);
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    return ids;
}

// The steps of p.951: the file (the system's open dialog first, unless a hook answers), the Frame
// Rate and the bin (New Bin makes one). The reels' sources already in the bin are used again
// (edl::reuseSources), so importing a list twice does not make two sources of one tape.
QStringList MainWindow::importEdl() {
    if (!project_ || !binContainer_->bins()) {
        ConsoleLog::instance().append(tr("Import EDL: open a project first."));
        return {};
    }
    QList<ImportEdlDialog::Bin> bins;
    for (const model::BinNode* n : binContainer_->bins()->bins()) bins.append({n->id, n->name});
    const BinDocument* active = binContainer_->activeDocument();
    QSettings remembered;
    ImportEdlDialog dialog(QString(), project_->settings.format.editRate, bins, active ? active->nodeId() : QString(), this);
    bool ok = false;
    if (importEdlHook_) {
        ok = importEdlHook_(dialog);
    } else {
        const QString chosen = QFileDialog::getOpenFileName(this, tr("Select File to Open"),
                                                            remembered.value(QStringLiteral("import/edlFolder")).toString(),
                                                            tr("Edit decision lists (*.edl);;All files (*)"));
        if (chosen.isEmpty()) return {};
        dialog.setPath(chosen);
        ok = dialog.exec() == QDialog::Accepted;
    }
    if (!ok || dialog.path().isEmpty()) return {};
    const QString path = dialog.path();
    remembered.setValue(QStringLiteral("import/edlFolder"), QFileInfo(path).absolutePath());
    QString binId = dialog.bin();
    if (binId.isEmpty()) binId = binContainer_->newBin();
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc) {
        ConsoleLog::instance().append(tr("Import EDL: the bin cannot be opened."));
        return {};
    }
    interop::edl::ImportOptions options;
    options.binId = doc->nodeId();
    options.when = QDateTime::currentDateTimeUtc();
    options.rate = dialog.rate();
    QString error;
    auto imported = interop::edl::importFile(path, options, &error);
    if (!imported) {
        ConsoleLog::instance().append(tr("Import EDL: %1").arg(error));
        return {};
    }
    interop::edl::reuseSources(*imported, doc->bin().items);
    QStringList ids;
    QString sequenceId;
    for (const model::BinItem& item : imported->items) {
        doc->addItem(item);
        ids << item.id;
        if (item.kind == model::BinItemKind::Sequence) sequenceId = item.id;
    }
    const QString name = QFileInfo(path).fileName();
    ConsoleLog::instance().append(tr("Import EDL: %1: a sequence and %n new source(s)", "", static_cast<int>(ids.size()) - 1).arg(name));
    // "A message displays asking you to Decompose the sequence before trying to use it" (p.951):
    // its segments name the tapes' sources, which have no media until Decompose (later).
    ConsoleLog::instance().append(tr("Import EDL: %1: the sequence uses its tapes' sources; Decompose it before using it.").arg(name));
    for (const QString& w : imported->warnings) ConsoleLog::instance().append(tr("Import EDL: %1: %2").arg(name, w));
    if (BinView* view = binContainer_->viewFor(doc); view && !sequenceId.isEmpty()) view->selectIds({sequenceId});
    return ids;
}

// The bin's Text view columns and custom columns go out, so the log holds what the bin shows (p.129
// step 1: "click the Text tab to display all clip information").
void MainWindow::exportBin() {
    BinView* view = binContainer_->activeView();
    if (!view) {
        ConsoleLog::instance().append(tr("Export Bin: open the bin to export first."));
        return;
    }
    const model::Bin& bin = view->document().bin();
    QSettings remembered;
    QString folder = remembered.value(QStringLiteral("export/folder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString base = bin.name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    ExportBinDialog dialog(QDir(folder).filePath(base + QStringLiteral(".ale")), this);
    const bool ok = exportBinHook_ ? exportBinHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || dialog.path().isEmpty()) return;
    QString path = dialog.path();
    if (QFileInfo(path).suffix().compare(QLatin1String("ale"), Qt::CaseInsensitive) != 0) path += QStringLiteral(".ale");
    remembered.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    interop::ale::ExportOptions options;
    // "The system creates a shot log of only the master clips in the bin" (p.130).
    options.subclips = false;
    options.columns = bin.display.columns + bin.display.customColumns;
    const interop::ale::ExportResult result = interop::ale::exportFile(bin.items, path, options);
    if (!result.ok) {
        ConsoleLog::instance().append(tr("Export Bin: %1").arg(result.error));
        return;
    }
    ConsoleLog::instance().append(tr("Export Bin: %1: %n clip(s)", "", result.clips).arg(QDir::toNativeSeparators(path)));
    for (const QString& w : result.warnings) ConsoleLog::instance().append(tr("Export Bin: %1").arg(w));
}

// "select a sequence in a bin, and when you open a new List Tool window, the old sequence will be
// populated by the selected sequence" (p.948 step 3); without one, the Record monitor's.
void MainWindow::showListTool() {
    showTool(QStringLiteral("ListTool"));
    if (BinView* view = binContainer_->activeView()) {
        for (const model::BinItem& item : view->document().bin().items) {
            if (item.kind == model::BinItemKind::Sequence && view->selectedIds().contains(item.id)) {
                listTool_->setSequence(item, clipResolver(item));
                return;
            }
        }
    }
    if (!listTool_->hasSequence()) emit listTool_->loadRequested();
}

// A sequence's segments name their clips' bins; a clip not found there is looked for by MobID in
// the open bins.
std::function<std::optional<model::BinItem>(const MobId&, const QString&)> MainWindow::clipResolver(const model::BinItem& sequence) const {
    QHash<QString, QString> binOf;
    const model::Sequence s = model::Sequence::fromItem(sequence);
    for (const model::Track& t : s.tracks()) {
        for (const model::Segment& seg : t.segments) {
            if (!seg.isFiller()) binOf.insert(seg.clipId, seg.binId);
        }
    }
    QPointer<Composer> composer = composer_;
    QPointer<BinContainer> bins = binContainer_;
    return [binOf, composer, bins](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
        if (composer && binOf.contains(clipId)) {
            if (auto clip = composer->resolve(binOf.value(clipId), clipId)) return clip;
        }
        if (!bins) return std::nullopt;
        for (const BinDocument* doc : bins->openDocuments()) {
            for (const model::BinItem& item : doc->bin().items) {
                if (item.kind != model::BinItemKind::Sequence && (item.id == clipId || model::mobIdOf(item) == mob)) return item;
            }
        }
        return std::nullopt;
    };
}

// Import transcodes each file into new managed media (media::transcodeClip) with a progress dialog.
// A file that fails is named in the Console; once Cancel was clicked, the first failure ends the
// import. Returns the new items' ids, the AAF ones first.
QStringList MainWindow::importFiles(const QString& binId, const QStringList& allPaths, const QString& resolution) {
    // AAF files are compositions, not media (p.547); shot logs are clip information (p.131-132);
    // OpenTimelineIO files are timelines (p.969), and so are FCP 7 XML files (ours).
    QStringList paths;
    QStringList aaf;
    QStringList logs;
    QStringList timelines;
    QStringList fcp;
    for (const QString& p : allPaths) {
        (p.endsWith(QLatin1String(".aaf"), Qt::CaseInsensitive) ? aaf
         : interop::ale::isShotLog(p)                           ? logs
         : interop::otio::isOtioFile(p)                         ? timelines
         : interop::fcpxml::isFcpXmlFile(p)                     ? fcp
                                                                : paths)
            << p;
    }
    QStringList fromAaf = importAaf(binId, aaf) + importShotLogs(binId, logs) + importOtio(binId, timelines) + importFcpXml(binId, fcp);
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
    const QString imp = QStringLiteral("Import");
    // "Autodetect Broadcast Wave Monophonic Groups" (p.1199): name_1.wav, name_2.wav ... as one clip.
    if (settings_->flag(imp, QStringLiteral("autodetectBwfGroups"))) clips = media::groupMonoWavs(clips);
    const int seconds = std::max(settings_->number(QStringLiteral("Import"), QStringLiteral("frameImportDuration")), 1);
    const media::MediaCreation projectCreation = mediaCreation(QStringLiteral("import"));
    media::MediaCreation creation = projectCreation;
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
        // The Audio tab (p.1198-1199), clip by clip.
        creation.sampleRate = projectCreation.sampleRate;
        creation.bitDepth = projectCreation.bitDepth;
        creation.relabelPullRates = false;
        creation.audioShift = 0;
        if (link && link->channels > 0) {
            const int sourceRate = clip.fields.value(QStringLiteral("Audio SR")).toInt();
            if (settings_->flag(imp, QStringLiteral("convertSampleRate"))) {
                // "Do not convert sources with pullup or pulldown rates".
                creation.relabelPullRates = settings_->flag(imp, QStringLiteral("skipPullRates"));
            } else if (media::isImportRate(sourceRate)) {
                // "Media Composer imports audio media at the source sample rate. If Media Composer does
                // not support the source sample rate, it autoconverts".
                creation.sampleRate = sourceRate;
            }
            if (!settings_->flag(imp, QStringLiteral("convertBitDepth"))) {
                const int bits = clip.fields.value(QStringLiteral("Audio Bit Depth")).section(QLatin1Char(' '), 0, 0).toInt();
                if (bits == 16 || bits == 24) creation.bitDepth = bits;
            }
            // "Subframe Alignment to Broadcast WAV Start Time" (p.1199): on the nearest frame.
            if (settings_->flag(imp, QStringLiteral("bwfAlignment")) && link->videoStream < 0 && sourceRate > 0) {
                const auto info = media::probe(link->audioFile(0));
                if (const auto tr = info ? media::bwfTimeReference(*info) : std::nullopt) {
                    const media::BwfAlignment a = media::alignToFrame(*tr, sourceRate, clip.editRate);
                    creation.audioShift = a.shift;
                    clip.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(a.startFrame, TimecodeFormat::forRate(clip.editRate, false)).toString()));
                }
            }
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
        model::BinItem item = *made;
        // "Automatically center pan monophonic clips" (p.1198).
        if (settings_->flag(imp, QStringLiteral("centerPanMono")) && item.fields.value(QStringLiteral("Tracks")) == QLatin1String("A1")) {
            model::centerClipPan(item);
        }
        doc->addItem(item);
        ids << item.id;
    }
    progress.reset();
    ids = fromAaf + ids;
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    if (skipped > 0) ConsoleLog::instance().append(tr("Import: %n file(s) are not media that can be imported.", "", skipped));
    return ids;
}

// The dialog (from the Record monitor's sequence, its marks and the selected tracks), then the mix
// with a progress dialog, the new clip into its bin (after the premix copy, when asked for) and the
// clip overwritten into the targets, new tracks added when there are none.
void MainWindow::audioMixdown() {
    auto fail = [this](const QString& text) {
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Audio Mixdown"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("mixdownFailed"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || !project_) {
        fail(tr("Load a sequence into the Record monitor first."));
        return;
    }
    const model::BinItem sequence = rec->item;
    const model::Sequence seq = model::Sequence::fromItem(sequence);
    // "Click the Track buttons ... to select the audio tracks you want to mix down" (p.731).
    QStringList tracks;
    for (const QString& t : timeline_->patch().selectedRecordTracks(seq)) {
        const model::Track* track = seq.track(t);
        if (track && track->kind == model::TrackKind::Audio) tracks << t;
    }
    if (tracks.isEmpty()) {
        fail(tr("Select the audio tracks to mix down."));
        return;
    }
    // "If you do not mark the section ... the system mixes down all of the selected audio tracks."
    const model::MonitorState& marks = composer_->state(MonitorId::Record);
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (marks.in() || marks.out()) {
        start = marks.markedStart();
        end = marks.markedEnd() + 1;
    }
    if (end <= start) {
        fail(tr("There is nothing to mix down."));
        return;
    }
    auto tc = [this](std::int64_t frame) {
        return QString::fromStdString(Timecode(timeline_->startFrame() + frame, timeline_->format()).toString());
    };
    AudioMixdownDialog::Setup setup;
    setup.sourceTracks = tracks;
    setup.rangeStart = tc(start);
    setup.rangeEnd = tc(end);
    setup.emptyTracks = model::emptyAudioTracks(seq, start, end, tracks);
    for (const QString& t : setup.emptyTracks) {
        if (const model::Track* track = seq.track(t)) setup.formats.insert(t, track->format);
    }
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) setup.bins << qMakePair(d->nodeId(), d->name());
    }
    setup.activeBin = rec->binId;
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    setup.drive = creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive;
    AudioMixdownDialog dialog(setup, this);
    const bool ok = mixdownHook_ ? mixdownHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    BinDocument* doc = binContainer_->document(dialog.bin());
    if (!doc || doc->isReadOnly()) {
        fail(tr("Choose a bin that can be changed."));
        return;
    }
    // "Select Save Premix Sequence if you want to save the sequence before mixing down" (p.732).
    if (dialog.savePremixSequence()) {
        model::BinItem premix = model::duplicateItem(doc->bin(), sequence, QDateTime::currentDateTimeUtc());
        doc->addItem(premix);
    }
    // "If you do not render the effect manually, Media Composer automatically renders the effect
    // before it creates an audio mixdown" (p.800): an undo step of its own, on the mixdown's drive.
    if (timeline_->audioSuiteToRender(tracks, start, end) > 0) {
        const QString drive = dialog.drive();
        const auto rendered = timeline_->renderAudioSuite(tracks, start, end, [this, drive](const model::Segment& s, QString* error) {
            return renderAudioSuite(s, drive, error);
        });
        if (!rendered) {
            fail(timeline_->lastMessage());
            return;
        }
    }
    media::MixdownRequest request;
    request.sequence = composer_->state(MonitorId::Record).current()->item;
    request.tracks = tracks;
    request.start = start;
    request.end = end;
    request.format = dialog.format();
    request.includeMaster = dialog.includeMasterFader();
    request.masterGainDb = audioMixer_ ? audioMixer_->masterGain() : 0.0;
    request.alternatingPan = settings_->text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
    request.clips = [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); };
    creation.videoDrive = dialog.drive();
    creation.audioDrive = dialog.drive();
    QProgressDialog progress(tr("Mixing down..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("mixdownProgress"));
    creation.progress = progressOn(progress);
    QString error;
    const auto made = media::mixdownClip(request, tr("%1 Mixdown").arg(sequence.name), tc(start), creation, &error);
    progress.reset();
    if (!made) {
        fail(error);
        return;
    }
    doc->addItem(*made);
    // "Media Composer mixes down the audio, displays the new master clip in the bin, and edits the
    // mixdown clip into the sequence."
    // A stereo or surround mixdown is one track of its format; Mono and Dual Mono one mono track for
    // each channel.
    QStringList targets = dialog.targetTracks();
    const model::AudioFormat format = media::mixdownTrackFormat(request.format);
    const int targetCount = format == model::AudioFormat::Mono ? media::mixdownChannels(request.format) : 1;
    const QString binId = doc->nodeId();
    timeline_->changeRecordSequence(tr("Audio Mixdown"), [&](model::Sequence& s) {
        while (targets.size() < targetCount) {
            const auto added = s.addTrack(model::TrackKind::Audio, format);
            if (!added) return false;
            targets << *added;
        }
        for (int c = 0; c < targetCount; ++c) {
            model::Segment seg;
            seg.clipId = made->id;
            seg.binId = binId;
            seg.sourceMob = made->mobId;
            seg.name = made->name;
            seg.sourceTrack = QStringLiteral("A%1").arg(c + 1);
            seg.length = end - start;
            // Dual Mono: the left panned left, the right panned right (p.734).
            if (request.format == media::MixdownFormat::DualMono) seg.pan = c == 0 ? -model::kPanMax : model::kPanMax;
            s.overwrite(targets.at(c), start, {seg});
        }
        return true;
    });
}

QMap<QString, media::MultiMix> MainWindow::savedMultiMixes() const {
    QMap<QString, media::MultiMix> out;
    const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    for (int i : f.versions(QStringLiteral("Audio Multi-Mix"))) {
        const model::SettingEntry& e = f.entries.at(i);
        if (!e.name.isEmpty()) out.insert(e.name, media::MultiMix::fromJson(e.values.value(QStringLiteral("mix")).toObject()));
    }
    return out;
}

// "The saved setting will appear in the Select Setting field, as well as an Audio Multi-Mix setting in
// the Project tab of the Settings dialog box" (p.734): a version of that name, made active.
void MainWindow::saveMultiMix(const QString& name, const media::MultiMix& mix) {
    if (!settings_->hasProject() || name.isEmpty()) return;
    model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    const QString kind = QStringLiteral("Audio Multi-Mix");
    int index = -1;
    for (int i : f.versions(kind)) {
        if (f.entries.at(i).name == name) index = i;
    }
    if (index < 0) {
        model::SettingEntry e;
        e.kind = kind;
        e.name = name;
        e.active = false;
        index = f.add(e);
    }
    f.entries[index].values.insert(QStringLiteral("mix"), mix.toJson());
    f.setActive(index);
    settings_->commit(model::SettingsScope::Project);
}

int MainWindow::projectSampleRate() const {
    const int rate = settings_ ? settings_->text(QStringLiteral("Audio Project"), QStringLiteral("sampleRate")).toInt() : 0;
    return rate == 44100 || rate == 96000 ? rate : 48000;
}

QString MainWindow::siteEqBinPath() const {
    const QString site = settings_ ? settings_->siteFolder() : QString();
    if (site.isEmpty()) return {};
    const QString path = QDir(site).filePath(QStringLiteral("Site_Effects/Site_EQs_Bin") + model::BinFile::suffix());
    if (!QFileInfo::exists(path)) {
        model::Bin bin;
        bin.name = QStringLiteral("Site_EQs_Bin");
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !model::BinFile::write(path, bin)) return {};
    }
    return path;
}

QList<model::BinItem> MainWindow::siteEqTemplates() const {
    const QString path = siteEqBinPath();
    if (path.isEmpty()) return {};
    QList<model::BinItem> out;
    QList<model::BinItem> items;
    // The open bin as it is now; else the file.
    bool open = false;
    if (const model::ProjectBins* project = binContainer_->bins()) {
        for (const BinDocument* d : binContainer_->openDocuments()) {
            if (QFileInfo(project->binPath(d->nodeId())) == QFileInfo(path)) {
                items = d->bin().items;
                open = true;
            }
        }
    }
    if (!open) {
        if (const auto bin = model::BinFile::read(path)) items = bin->items;
    }
    for (const model::BinItem& i : items) {
        if (model::eqTemplateOf(i)) out << i;
    }
    return out;
}

// The tone on every output channel at its level (a piece centred at full gain on L and R), for as
// long as a session could last.
bool MainWindow::playCalibrationTone(bool device) {
    if (composer_->isPlaying()) composer_->stop();
    audio_->stop();
    scrubbing_ = false;
    media::AudioPiece tone;
    tone.tone = audioTool_->calibrationTone();
    tone.length = std::numeric_limits<std::int64_t>::max() / 4;
    media::AudioPlayer::PlayOptions options;
    options.device = device;
    options.bufferFrames = settings_->number(QStringLiteral("Audio"), QStringLiteral("toolBuffer"));
    playingTone_ = audio_->start(std::make_shared<media::AudioMix>(std::vector<media::AudioPiece>{tone}), 0, options);
    if (!playingTone_) ConsoleLog::instance().append(tr("Play Calibration Tone: no audio output could be opened."));
    return playingTone_;
}

void MainWindow::stopCalibrationTone() {
    if (!playingTone_) return;
    audio_->stop();
    playingTone_ = false;
}

void MainWindow::updateMeters() {
    if (!audioTool_) return;
    AudioMeters* meters = audioTool_->meters();
    if (!audio_->isPlaying()) {
        if (!metersLive_) return;
        for (int c = 0; c < meters->channels(); ++c) meters->setLevel(c, -90);
        metersLive_ = false;
        return;
    }
    const auto peaks = audio_->takePeaks();
    const int outs = audio_->channels();
    for (int c = 0; c < meters->channels(); ++c) {
        const float peak = c < outs && c < static_cast<int>(peaks.size()) && audioTool_->isOutput(c) ? peaks[static_cast<std::size_t>(c)] : 0.0f;
        meters->setLevel(c, peak > 0 ? std::max(20.0 * std::log10(static_cast<double>(peak)), -90.0) : -90.0);
    }
    metersLive_ = true;
}

// "Open a bin", then PH > Create Tone Media (p.158-159): the tone's options start at the calibration
// tone; the target bin is the active one and the drive the Media Creation setting's audio drive.
std::optional<model::BinItem> MainWindow::createToneMedia() {
    auto fail = [this](const QString& text) {
        ConsoleLog::instance().append(tr("Create Tone Media: %1").arg(text));
        if (toneMediaHook_) return;
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Create Tone Media"), text, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    QList<QPair<QString, QString>> bins;
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) bins << qMakePair(d->nodeId(), d->name());
    }
    if (bins.isEmpty() || !project_) {
        fail(tr("Open a bin first."));
        return std::nullopt;
    }
    const BinDocument* active = binContainer_->activeDocument();
    QList<QPair<QString, QString>> drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.root, d.name});
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    const QString drive = creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive;
    CreateToneMediaDialog dialog(audioTool_->calibrationTone(), bins, active ? active->nodeId() : QString(), drives, drive, this);
    const bool ok = toneMediaHook_ ? toneMediaHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || !dialog.isValid()) return std::nullopt;
    const ToneMediaChoice choice = dialog.choice();
    BinDocument* doc = binContainer_->document(choice.bin);
    if (!doc || doc->isReadOnly()) {
        fail(tr("Choose a bin that can be changed."));
        return std::nullopt;
    }
    creation.videoDrive = choice.drive;
    creation.audioDrive = choice.drive;
    const Rational rate = project_->settings.format.editRate;
    const auto frames = static_cast<std::int64_t>(std::llround(choice.seconds * rate.toDouble()));
    const media::Tone tone = choice.tone;
    auto render = [tone](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
        for (std::vector<float>& track : out) media::renderTone(tone, at, count, media::kPlaybackRate, track.data());
    };
    QString error;
    const auto clip = media::renderedAudioClip(media::toneClipName(tone, choice.tracks), rate, frames, choice.tracks, render,
                                               QString(), creation, &error);
    if (!clip) {
        fail(error);
        return std::nullopt;
    }
    doc->addItem(*clip);
    return clip;
}

int MainWindow::multipleMixes(bool canMix) {
    auto fail = [this](const QString& text) {
        ConsoleLog::instance().append(tr("Multiple Mixes: %1").arg(text));
        if (multiMixHook_) return;
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Multiple Mixes"), text, QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    const bool haveSequence = rec && rec->item.kind == model::BinItemKind::Sequence;
    if (canMix && (!haveSequence || !project_)) {
        fail(tr("Load a sequence into the Record monitor first."));
        return 0;
    }
    MultiMixDialog::Setup setup;
    const model::Sequence seq = haveSequence ? model::Sequence::fromItem(rec->item) : model::Sequence{};
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Audio) setup.tracks << t.name();
    }
    // From the Settings window without a sequence: the tracks a default sequence has (ours).
    if (setup.tracks.isEmpty()) setup.tracks = {QStringLiteral("A1"), QStringLiteral("A2")};
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) setup.bins << qMakePair(d->nodeId(), d->name());
    }
    setup.activeBin = haveSequence ? rec->binId : QString();
    for (const media::MediaDrive& d : media::mediaDrives()) setup.drives.append({d.name, d.root});
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    setup.drive = creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive;
    if (std::none_of(setup.drives.cbegin(), setup.drives.cend(), [&](const auto& d) { return d.second == setup.drive; }) &&
        !setup.drive.isEmpty()) {
        setup.drives.prepend({QFileInfo(setup.drive).fileName(), setup.drive});
    }
    setup.settings = savedMultiMixes();
    {
        const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
        const int active = f.activeIndex(QStringLiteral("Audio Multi-Mix"));
        if (active >= 0) setup.setting = f.entries.at(active).name;
    }
    setup.canMix = canMix;
    MultiMixDialog dialog(setup, this);
    dialog.save = [this](const QString& name, const media::MultiMix& mix) { saveMultiMix(name, mix); };
    const bool ok = multiMixHook_ ? multiMixHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || !canMix) return 0;
    const media::MultiMix mix = dialog.mix();
    if (const QString problem = media::multiMixProblem(mix); !problem.isEmpty()) {
        fail(problem);
        return 0;
    }
    BinDocument* doc = binContainer_->document(dialog.bin());
    if (!doc || doc->isReadOnly()) {
        fail(tr("Choose a bin that can be changed."));
        return 0;
    }
    // "When this option is selected, Media Composer uses current IN and OUT points in the selected
    // sequence to determine starting and ending frames for the mixdown" (p.734).
    const model::MonitorState& marks = composer_->state(MonitorId::Record);
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (mix.useMarks && (marks.in() || marks.out())) {
        start = marks.markedStart();
        end = marks.markedEnd() + 1;
    }
    if (end <= start) {
        fail(tr("There is nothing to mix down."));
        return 0;
    }
    const QString drive = dialog.drive().isEmpty() ? setup.drive : dialog.drive();
    creation.videoDrive = drive;
    creation.audioDrive = drive;
    // AudioSuite effects are rendered first, as for Audio Mixdown (p.800).
    QStringList inputs;
    for (const media::MixOutput& o : mix.outputs) {
        for (const QString& t : o.inputs) {
            if (!inputs.contains(t)) inputs << t;
        }
    }
    if (timeline_->audioSuiteToRender(inputs, start, end) > 0) {
        const auto rendered = timeline_->renderAudioSuite(inputs, start, end, [this, drive](const model::Segment& s, QString* error) {
            return renderAudioSuite(s, drive, error);
        });
        if (!rendered) {
            fail(timeline_->lastMessage());
            return 0;
        }
    }
    const model::BinItem sequence = composer_->state(MonitorId::Record).current()->item;
    auto tc = [this](std::int64_t frame) {
        return QString::fromStdString(Timecode(timeline_->startFrame() + frame, timeline_->format()).toString());
    };
    QProgressDialog progress(tr("Mixing..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("multiMixProgress"));
    QList<model::BinItem> made;
    for (int o = 0; o < mix.outputs.size(); ++o) {
        const media::MixOutput& out = mix.outputs.at(o);
        media::MixdownRequest request;
        request.sequence = sequence;
        request.tracks = out.inputs;
        request.start = start;
        request.end = end;
        request.format = out.format;
        request.includeMaster = out.masterFader;
        request.masterGainDb = audioMixer_ ? audioMixer_->masterGain() : 0.0;
        request.alternatingPan = settings_->text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
        request.clips = [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); };
        const QString name = out.name.isEmpty() ? tr("%1 Mix %2").arg(sequence.name).arg(media::multiMixTrackNumber(mix, o)) : out.name;
        progress.setLabelText(tr("Mixing %1...").arg(name));
        creation.progress = progressOn(progress);
        QString error;
        const auto clip = media::mixdownClip(request, name, tc(start), creation, &error);
        if (!clip) {
            progress.reset();
            fail(error);
            return static_cast<int>(made.size());
        }
        doc->addItem(*clip);
        made << *clip;
    }
    progress.reset();
    // "If you selected Create New Sequence, these clips will also be built into a new sequence with a
    // track for each mix. The video and data tracks are also copied into the new sequence unchanged,
    // unless you selected Exclude Video and Data Tracks" (p.734). The mixes sit where their range is.
    if (mix.newSequence) {
        model::Sequence ns;
        ns.setTimecodeTrack(seq.hasTimecodeTrack());
        ns.setMixFormat(seq.mixFormat());
        if (!mix.excludeVideoAndData) {
            for (const model::Track& t : seq.tracks()) {
                if (t.kind != model::TrackKind::Audio) ns.restoreTrack(t);
            }
        }
        for (int o = 0; o < made.size(); ++o) {
            const media::MixdownFormat format = mix.outputs.at(o).format;
            const int count = format == media::MixdownFormat::DualMono ? 2 : 1;
            for (int c = 0; c < count; ++c) {
                const auto track = ns.addTrack(model::TrackKind::Audio, media::mixdownTrackFormat(format));
                if (!track) break;
                model::Segment s;
                s.clipId = made.at(o).id;
                s.binId = doc->nodeId();
                s.sourceMob = made.at(o).mobId;
                s.name = made.at(o).name;
                s.sourceTrack = QStringLiteral("A%1").arg(c + 1);
                s.length = end - start;
                if (format == media::MixdownFormat::DualMono) {
                    s.pan = c == 0 ? -model::kPanMax : model::kPanMax;
                    s.hasPan = true;
                }
                ns.overwrite(*track, start, {s});
            }
        }
        model::BinItem item = model::newSequence(doc->bin(), sequence.editRate, QDateTime::currentDateTimeUtc(),
                                                 sequence.fields.value(QStringLiteral("Start")));
        item.name = mix.sequenceName.isEmpty() ? sequence.name + QStringLiteral(".MultiMix") : mix.sequenceName;
        ns.storeIn(item);
        doc->addItem(item);
    }
    return static_cast<int>(made.size());
}

// The Audio Punch-In tool's menus: the inputs the system offers, the media drives (the Media Creation
// setting's audio drive first chosen) and the bins that can be changed (the sequence's own first chosen).
void MainWindow::preparePunchIn() {
    QList<QPair<QString, int>> inputs;
    const QList<media::AudioInputDevice> devices = media::audioInputDevices();
    int defaultChannels = 2;
    for (const media::AudioInputDevice& d : devices) {
        if (d.isDefault) defaultChannels = d.channels;
    }
    inputs.append({tr("Default"), defaultChannels});
    for (const media::AudioInputDevice& d : devices) inputs.append({d.name, d.channels});
    audioPunchIn_->setInputs(inputs);
    QList<QPair<QString, QString>> drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.name, d.root});
    const media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    QList<QPair<QString, QString>> bins;
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) bins.append({d->nodeId(), d->name()});
    }
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    audioPunchIn_->setTargets(drives, creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive, bins,
                              rec ? rec->binId : QString());
    audioPunchIn_->refresh();
}

void MainWindow::prepareAudioSuite() {
    QList<QPair<QString, QString>> drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.name, d.root});
    const media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    audioSuite_->setDrives(drives, creation.audioDrive.isEmpty() ? creation.videoDrive : creation.audioDrive);
    QList<QPair<QString, QString>> bins;
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) bins.append({d->nodeId(), d->name()});
    }
    const model::LoadedClip* source = composer_->state(MonitorId::Source).current();
    audioSuite_->setBins(bins, source ? source->binId : QString());
    audioSuite_->refresh();
}

bool MainWindow::makeAudioSuiteClip(const AudioSuiteTool::MasterClipRender& r, QString* error) {
    BinDocument* doc = binContainer_->document(r.targetBin);
    if (!doc || doc->isReadOnly()) {
        if (error) *error = tr("Choose a bin that can be changed.");
        return false;
    }
#ifdef Q_OS_MACOS
    constexpr bool mac = true;
#else
    constexpr bool mac = false;
#endif
    media::AudioSuiteClipRequest request;
    request.clip = r.source.item;
    request.tracks = r.tracks;
    request.in = r.in.value_or(0);
    request.out = r.out.value_or(-1);
    request.handleSeconds = r.handleSeconds;
    request.effect = r.effect.effect;
    request.params = r.effect.params;
    request.name = model::audioSuiteClipName(r.source.item.name, r.effect.name, mac);
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    if (!r.drive.isEmpty()) {
        creation.videoDrive = r.drive;
        creation.audioDrive = r.drive;
    }
    const auto made = media::audioSuiteClip(request, creation, error);
    if (!made) return false;
    // "Media Composer creates a new master clip in the target bin ... also creates an AudioSuite
    // effect template in the bin" (p.803-804).
    doc->addItem(*made);
    model::AudioSuiteTemplate t;
    t.effect = r.effect;
    t.sourceBin = r.source.binId;
    t.sourceClip = r.source.item.id;
    t.tracks = r.tracks;
    t.in = r.in;
    t.out = r.out;
    t.handleSeconds = r.handleSeconds;
    doc->addItem(model::audioSuiteTemplateItem(t, model::audioSuiteTemplateName(r.source.item.name, r.effect.name, mac),
                                               QDateTime::currentDateTimeUtc()));
    // "When enabled, instructs the system to automatically load the resulting master clip into the
    // Source monitor" (p.802).
    if (r.loadResult) composer_->load(MonitorId::Source, doc->nodeId(), *made);
    return true;
}

std::optional<model::AudioSuiteEffect> MainWindow::renderAudioSuite(const model::Segment& segment, const QString& drive,
                                                                    QString* error) {
    const auto clip = composer_->resolve(segment.binId, segment.clipId);
    if (!clip) {
        if (error) *error = tr("The clip %1 of the AudioSuite effect is not found.").arg(segment.name);
        return std::nullopt;
    }
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    if (!drive.isEmpty()) {
        creation.videoDrive = drive;
        creation.audioDrive = drive;
    }
    return media::renderAudioSuite(segment, *clip, rec ? rec->item.editRate : clip->editRate, creation, error);
}

// A punch-in take as managed media: "<sequence> Voice Over.<n>" (the picture on p.751), n one more than
// the bin's voice-overs of that sequence, starting at 00:00:00:00.
std::optional<model::BinItem> MainWindow::punchInClip(const AudioPunchInTool::Take& take, QString* error) {
    BinDocument* doc = binContainer_->document(take.binId);
    if (!doc || doc->isReadOnly()) {
        if (error) *error = tr("Choose a bin that can be changed.");
        return std::nullopt;
    }
    const QString prefix = tr("%1 Voice Over.").arg(take.sequenceName);
    int n = 0;
    for (const model::BinItem& item : doc->bin().items) {
        if (item.name.startsWith(prefix)) n = std::max(n, item.name.mid(prefix.size()).toInt());
    }
    std::size_t samples = 0;
    for (const auto& c : take.channels) samples = std::max(samples, c.size());
    const auto frames = static_cast<std::int64_t>(std::floor(media::samplesToFrames(static_cast<std::int64_t>(samples), take.editRate)));
    if (frames <= 0 || take.channels.empty()) {
        if (error) *error = tr("Nothing was recorded.");
        return std::nullopt;
    }
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    if (!take.drive.isEmpty()) {
        creation.videoDrive = take.drive;
        creation.audioDrive = take.drive;
    }
    const auto& channels = take.channels;
    const auto render = [&channels](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
        for (std::size_t c = 0; c < channels.size(); ++c) {
            out[c].assign(static_cast<std::size_t>(count), 0.0f);
            for (int i = 0; i < count; ++i) {
                const auto k = static_cast<std::size_t>(at + i);
                if (k < channels[c].size()) out[c][static_cast<std::size_t>(i)] = channels[c][k];
            }
        }
    };
    const auto made = media::renderedAudioClip(prefix + QString::number(n + 1), take.editRate, frames,
                                               static_cast<int>(channels.size()), render, QString(), creation, error);
    if (made) doc->addItem(*made);
    return made;
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
    if (panelId == QLatin1String("AudioEQ")) size = QSize(620, 300);
    if (panelId == QLatin1String("AudioTrackEffect")) size = QSize(460, 360);
    if (panelId == QLatin1String("AudioSuite")) size = QSize(480, 160);
    if (panelId == QLatin1String("AudioTool")) size = QSize(300, 340);
    if (panelId == QLatin1String("MediaTool")) size = QSize(640, 320);
    if (panelId == QLatin1String("SourceBrowser")) size = QSize(960, 600);
    if (panelId == QLatin1String("ListTool")) size = QSize(980, 560);
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
void MainWindow::adoptLegacyBinLists(const QJsonValue& favorites, const QJsonValue& views) {
    legacyFavorites_ = favorites;
    legacyBinViews_ = views;
    writeLegacyBinLists();
}

bool MainWindow::writeLegacyBinLists() {
    // Only into a user's settings file: before one is chosen they live in memory and would be lost.
    if (settings_->user().isEmpty() || settings_->usersRoot().isEmpty()) return false;
    bool wrote = false;
    const auto adopt = [&](QJsonValue& legacy, const QString& key) {
        if (legacy.isNull()) return;
        const QJsonValue now = settings_->value(QStringLiteral("Bin"), key);
        if (!now.isNull() && !now.isUndefined()) {
            legacy = QJsonValue(); // the user has lists of their own already
            return;
        }
        settings_->setValue(QStringLiteral("Bin"), key, legacy);
        // Kept until it reads back, in case the setting cannot be written.
        if (settings_->value(QStringLiteral("Bin"), key) == legacy) {
            legacy = QJsonValue();
            wrote = true;
        }
    };
    adopt(legacyFavorites_, QStringLiteral("favoriteBins"));
    adopt(legacyBinViews_, QStringLiteral("binViews"));
    return wrote;
}

void MainWindow::applySettings() {
    // Writing them fires Settings::changed, which applies everything again with them in place.
    if (legacyBinListsPending() && writeLegacyBinLists()) return;
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
    // The user's Favorite Bins (p.293) and saved bin views (p.249).
    if (FavoriteBins::fromJson(s.value(QStringLiteral("Bin"), QStringLiteral("favoriteBins")))) binContainer_->refreshTree();
    if (BinViewLibrary::fromJson(s.value(QStringLiteral("Bin"), QStringLiteral("binViews")))) binContainer_->refreshViewMenus();
    // Audio Project settings (p.692, p.1150).
    const QString mixerMode = s.text(QStringLiteral("Audio Project"), QStringLiteral("mixerMode"));
    if (const auto mode = AudioMixer::modeFromName(mixerMode); mode && mixerMode != appliedMixerMode_) {
        appliedMixerMode_ = mixerMode;
        audioMixer_->setMode(*mode);
    }
    const QString mix = s.text(QStringLiteral("Audio Project"), QStringLiteral("mixMode"));
    if (!mix.isEmpty()) audioMixer_->setMixMode(mix);
    // New pan keyframes take the pan that plays (Audio setting, Default Pan, p.1147).
    timeline_->setAlternatingPan(s.text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered"));
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
    o.markedWaveforms = s.flag(tl, QStringLiteral("showMarkedWaveforms"));
    o.dupeHandles = s.text(tl, QStringLiteral("dupeHandles")).toDouble();
    o.startFillerSeconds = s.value(tl, QStringLiteral("startFiller")).toInt();
    o.flashFrames = s.value(tl, QStringLiteral("flashFrames")).toInt(10);
    const QString text = s.value(tl, QStringLiteral("timelineText")).toString();
    o.textColor = text == QLatin1String("black")  ? Timeline::Options::TextColor::Black
                  : text == QLatin1String("both") ? Timeline::Options::TextColor::Both
                                                  : Timeline::Options::TextColor::White;
    o.wireframeDragging = s.value(tl, QStringLiteral("wireframeDragging")).toBool();
    o.fastScrub = s.value(tl, QStringLiteral("fastScrub")).toBool();
    o.autoPatching = s.value(tl, QStringLiteral("autoPatching")).toBool();
    o.autoMonitoring = s.value(tl, QStringLiteral("autoMonitoring")).toBool();
    o.segmentDragSyncLocks = s.value(tl, QStringLiteral("segmentDragSyncLocks")).toBool();
    o.positionBarSnap = s.value(tl, QStringLiteral("positionBarSnap")).toBool();
    o.defaultSyncLocks = s.value(tl, QStringLiteral("defaultSyncLocks")).toBool();
    o.selectFiller = s.value(tl, QStringLiteral("selectFiller")).toBool();
    o.openEffectEditor = s.value(tl, QStringLiteral("openEffectEditor")).toBool();
    o.tcDisablesSmartTools = s.value(tl, QStringLiteral("tcDisablesSmartTools")).toBool();
    o.lassoSegmentMode = s.value(tl, QStringLiteral("lassoTransitions")).toString() == QLatin1String("segment");
    o.mapOverlay = s.value(tl, QStringLiteral("mapOverlay")).toBool();
    o.mapClickHides = s.value(tl, QStringLiteral("mapClickHides")).toBool();
    o.mapWaveforms = s.value(tl, QStringLiteral("mapWaveforms")).toBool();
    o.mapEffects = s.value(tl, QStringLiteral("mapEffects")).toBool();
    o.proToolsChannelOrder = s.text(QStringLiteral("Audio"), QStringLiteral("channelOrder")) == QLatin1String("protools");
    const QString movement = s.text(tl, QStringLiteral("movementDuringPlay"));
    o.movement = movement == QLatin1String("scroll") ? Timeline::Options::Movement::Scroll
                 : movement == QLatin1String("none") ? Timeline::Options::Movement::None
                                                     : Timeline::Options::Movement::Page;
    o.onlyOneSegmentTool = s.flag(tl, QStringLiteral("onlyOneSegmentTool"));
    o.defaultSegmentTool = s.text(tl, QStringLiteral("defaultSegmentTool")) == QLatin1String("insert")
                               ? model::EditKind::SpliceIn
                               : model::EditKind::Overwrite;
    o.activateRecordAfterEdit = s.flag(composer, QStringLiteral("activateRecordAfterEdit"));
    o.syncPointEditing = s.flag(composer, QStringLiteral("syncPointEditing"));
    o.singleMarkEditing = s.flag(composer, QStringLiteral("singleMarkEditing"));
    o.phantomMarks = s.flag(composer, QStringLiteral("phantomMarks"));
    o.autoCreateTracks = s.flag(composer, QStringLiteral("autoCreateTracks"));
    o.autoEnableSourceTracks = s.flag(composer, QStringLiteral("autoEnableSourceTracks"));
    o.copySourceMarkers = s.flag(composer, QStringLiteral("copySourceMarkers"));
    o.stopAtHead = s.flag(composer, QStringLiteral("stopAtHead"));
    o.stopAtTail = s.flag(composer, QStringLiteral("stopAtTail"));
    o.stopAtMarkers = s.flag(composer, QStringLiteral("stopAtMarkers"));
    o.ignoreTrackSelectors = s.flag(composer, QStringLiteral("ignoreTrackSelectors"));
    const QString trim = QStringLiteral("Trim");
    o.trimAutoFocus = s.flag(trim, QStringLiteral("autoFocus"));
    o.trimPreroll = s.value(trim, QStringLiteral("preroll")).toInt(2);
    o.trimPostroll = s.value(trim, QStringLiteral("postroll")).toInt(2);
    o.trimIntermission = s.value(trim, QStringLiteral("intermission")).toInt(0);
    o.jklTrim = s.flag(trim, QStringLiteral("jklTrim"));
    binContainer_->setDefaultSyncLocks(o.defaultSyncLocks);
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
    timeline_->setSourceRecordColors(s.flag(ui, QStringLiteral("timelineColors")));
    timeline_->setMismatchedRates(s.flag(QStringLiteral("Audio Project"), QStringLiteral("showMismatchedRates")), projectSampleRate());
    // The Timeline's Clip Color choices (p.594-597), kept with the Timeline setting.
    {
        // From the setting: not written back to it.
        const QSignalBlocker quiet(timeline_);
        timeline_->setClipColorOptions(model::ClipColorOptions::fromJson(s.value(tl, QStringLiteral("clipColor")).toObject()));
    }
    // The Timeline view in use (p.604): applied when another one is chosen, here or in the Settings
    // list. Its Clip Color choices become the Timeline setting's ("Clip color options are saved when
    // you save a customized Timeline View", p.595).
    {
        const model::SettingsFile& user = s.file(model::SettingsScope::User);
        const int v = user.activeIndex(QStringLiteral("Timeline View"));
        const QString name = v >= 0 ? user.entries.at(v).name.trimmed() : QString();
        if (!name.isEmpty() && name != appliedTimelineView_) {
            const QJsonObject values = user.entries.at(v).values;
            appliedTimelineView_ = name;
            timeline_->setDisplay(Timeline::displayFromJson(values.value(QStringLiteral("display")).toObject()));
            if (values.contains(QStringLiteral("clipColor"))) {
                const auto colors = model::ClipColorOptions::fromJson(values.value(QStringLiteral("clipColor")).toObject());
                QMetaObject::invokeMethod(this, [this, colors] { timeline_->setClipColorOptions(colors); }, Qt::QueuedConnection);
            }
        }
        if (name.isEmpty()) appliedTimelineView_.clear();
        updateTimelineViews();
    }
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
    // TV1-TV8 switch to their Timeline views (p.605). "You must create at least one Timeline view to
    // use the Timeline View buttons."
    for (int i = 0; i < 8; ++i) {
        commands_.setHandler(QStringLiteral("timeline.view.tv%1").arg(i + 1), [this, i] {
            const QString name = timelineViewButtons().value(i);
            if (!name.isEmpty()) chooseTimelineView(name);
        });
    }
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
    // Clip > Lock Project Bin and Unlock Project Bin (p.302): the bins
    // selected in the Bin Container, or else the active bin.
    auto projectBins = [this](bool lock) {
        QStringList ids;
        for (const QString& id : binContainer_->selectedNodes()) {
            if (const model::ProjectBins* b = binContainer_->bins(); b && b->node(id) && b->node(id)->kind == model::BinNode::Kind::Bin) {
                ids << id;
            }
        }
        if (ids.isEmpty()) {
            if (BinDocument* d = binContainer_->activeDocument()) ids << d->nodeId();
        }
        QString error;
        const bool ok = lock ? binContainer_->lockProjectBins(ids, model::BinLockKind::Locked, &error)
                             : binContainer_->unlockProjectBins(ids, &error);
        if (!ok && !error.isEmpty()) {
            auto* box = new QMessageBox(QMessageBox::Warning, lock ? tr("Lock Project Bin") : tr("Unlock Project Bin"), error,
                                        QMessageBox::Ok, this);
            box->setObjectName(QStringLiteral("binLockFailed"));
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
    };
    handle("clip.lock_project_bin", [projectBins] { projectBins(true); });
    handle("clip.unlock_project_bin", [projectBins] { projectBins(false); });
    // Tools (ch.5).
    // Tools menu: each opens its panel through showTool().
    handle("tools.command_palette", [this] { showTool(QStringLiteral("CommandPalette")); });
    handle("tools.calculator", [this] { showTool(QStringLiteral("Calculator")); });
    handle("tools.console", [this] { showTool(QStringLiteral("Console")); });
    handle("tools.effect_palette", [this] { showTool(QStringLiteral("EffectPalette")); });
    handle("tools.effect_editor", [this] { showTool(QStringLiteral("EffectEditor")); });
    handle("tools.audio_mixer", [this] { showTool(QStringLiteral("AudioMixer")); });
    handle("tools.audio_eq", [this] { showTool(QStringLiteral("AudioEQ")); });
    handle("tools.audio_tool", [this] { showTool(QStringLiteral("AudioTool")); });
    handle("tools.audio_track_effect", [this] { showTool(QStringLiteral("AudioTrackEffect")); });
    handle("tools.audio_punch_in", [this] {
        preparePunchIn();
        showTool(QStringLiteral("AudioPunchIn"));
    });
    handle("tools.audiosuite", [this] {
        prepareAudioSuite();
        showTool(QStringLiteral("AudioSuite"));
    });
    handle("tools.media_tool", [this] { showMediaTool(); });
    // Tools > List Tool (p.944), File > Input > Import EDL (p.951), File > Output > Export Bin (p.130).
    handle("tools.list_tool", [this] { showListTool(); });
    handle("file.input.import_edl", [this] { importEdl(); });
    handle("file.output.export_bin", [this] { exportBin(); });
    // File > Input > Source Browser, Tools > Source Browser (p.315, p.327).
    handle("file.input.source_browser", [this] { showSourceBrowser(); });
    handle("clip.relink.linked_media", [this] { relinkLinkedMedia(); });
    handle("clip.relink.managed_media", [this] { relinkManagedMedia(); });
    // Managed media (p.213, p.134, p.349-359).
    handle("file.input.import_media", [this] {
        showSourceBrowser();
        if (SourceBrowser* browser = sourceBrowser_.data()) browser->setImportMode(true);
    });
    handle("clip.consolidate_transcode", [this] { consolidateTranscode(); });
    handle("timeline.mixdown.audio.to_sequence", [this] { audioMixdown(); });
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
    handle("clip.modify.modify_clip", [this] { modifySelection(); });
    handle("clip.audio.apply_gain", [this] { applyClipGain(); });
    handle("clip.audio.center_pan", [this] { centerPan(); });
    handle("clip.auto_vo", [this] { autoVo(); });
    handle("clip.read_audio_timecode", [this] { readAudioTimecode(); });
    handle("timeline.mixdown.audio.multiple_mixes", [this] { multipleMixes(); });
    handle("clip.audio.change_sample_rate", [this] { changeSampleRate(); });
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
    // Tools > Timeline Clip Notes (p.484), one window, kept.
    handle("tools.timeline_clip_notes", [this] {
        if (!clipNotesWindow_) clipNotesWindow_ = new ClipNotesWindow(*timeline_, this);
        clipNotesWindow_->refresh();
        clipNotesWindow_->show();
        clipNotesWindow_->raise();
    });
    // Edit > Find (Ctrl+F, p.417, p.429), one window, kept; Ctrl+G finds the next occurrence (p.429, p.431).
    handle("edit.find", [this] {
        if (!findWindow_) {
            findWindow_ = new FindWindow(*binContainer_, *composer_, *timeline_, this);
            connect(findWindow_, &FindWindow::showInBin, this, [this](const QString& binId, const QString& itemId) {
                if (BinDocument* doc = binContainer_->openBin(binId)) {
                    if (BinView* view = binContainer_->viewFor(doc)) view->selectIds({itemId});
                }
            });
        }
        findWindow_->show();
        findWindow_->raise();
        findWindow_->activateWindow();
        findWindow_->findBox()->setFocus();
    });
    for (const char* id : {"bin.autosequence", "clip.autosequence"}) {
        handle(id, [this] {
            QString error;
            if (autoSequence(QApplication::keyboardModifiers() & Qt::AltModifier, &error).isEmpty() && !error.isEmpty()) {
                QMessageBox::warning(this, tr("AutoSequence"), error);
            }
        });
    }
    handle("edit.find_again", [this] {
        if (findWindow_) findWindow_->findNext();
    });
    // Tools > Timecode Window (p.389), one window, kept.
    handle("tools.timecode_window", [this] {
        if (!timecodeWindow_) timecodeWindow_ = new TimecodeWindow(*composer_, this);
        timecodeWindow_->refresh();
        timecodeWindow_->show();
        timecodeWindow_->raise();
    });
    // Clip > Loop Selected Clips (p.393): the active bin's selected clips, in the bin's order; Alt plays
    // each from start to end instead of IN to OUT.
    handle("clip.loop_selected_clips", [this] {
        BinView* view = binContainer_->activeView();
        if (!view) return;
        const QStringList selected = view->selectedIds();
        QList<Composer::LoopClip> clips;
        for (const QString& id : view->visibleIds()) {
            const model::BinItem* item = selected.contains(id) ? view->document().bin().item(id) : nullptr;
            if (item && item->kind != model::BinItemKind::Effect) clips.append({view->document().nodeId(), *item});
        }
        if (clips.isEmpty()) return;
        composer_->playClipLoop(clips, QApplication::keyboardModifiers() & Qt::AltModifier);
    });
    // Tools > Markers (p.420), one window, kept.
    handle("tools.markers", [this] {
        if (!markersWindow_) markersWindow_ = new MarkersWindow(*timeline_, this);
        markersWindow_->refresh();
        markersWindow_->show();
        markersWindow_->raise();
    });
    // Playing, moving and marking in the Composer (ch. 13).
    for (const QString& id : commands_.ids()) {
        if (Composer::handles(id)) {
            handle(id.toLatin1().constData(), [this, id] {
                // With the Audio Mixer active in Auto mode, Go to Next/Previous Event goes to the
                // next or previous audio keyframe (p.720).
                const bool event = id == QLatin1String("move.go_to_next_event") || id == QLatin1String("move.go_to_previous_event");
                const Panel* active = panels_->activePanel();
                if (event && active && active->id() == QLatin1String("AudioMixer") &&
                    audioMixer_->goToKeyframe(id == QLatin1String("move.go_to_next_event") ? 1 : -1)) {
                    return;
                }
                composer_->perform(id);
            });
        }
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
    for (const char* id : {"windows.next_window", "windows.previous_window", "windows.next_tab", "edit.find_again",
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
    // Each button by its untranslated key (its object name and AboutInfoDialog::showTab's key) and
    // its translated text.
    const std::pair<QString, QString> tabs[] = {{QStringLiteral("Configuration"), tr("Configuration")},
                                                {QStringLiteral("Usage"), tr("Usage")},
                                                {QStringLiteral("Hardware"), tr("Hardware")}};
    for (const auto& [key, text] : tabs) {
        QPushButton* b = box->addButton(text, QMessageBox::ActionRole);
        b->setObjectName(QStringLiteral("about") + key);
        connect(b, &QPushButton::clicked, this, [this, key] {
            AboutInfoDialog* info = createAboutInfo();
            info->setAttribute(Qt::WA_DeleteOnClose);
            info->showTab(key);
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
