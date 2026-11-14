#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/ale.h"
#include "lumenis/interop/edl.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"
#include "lumenis/interop/avb_export.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/interop/avb_import.h"
#include "lumenis/ui/ofx_inputs_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/translation.h"
#include "lumenis/ui/accessibility.h"
#include "lumenis/ui/find_command.h"

#include "lumenis/model/script.h"
#include "lumenis/ui/script_window.h"

#include "lumenis/ui/ancdata_import_dialog.h"

#include "lumenis/model/safe_color_limiter.h"
#include "lumenis/media/display_color.h"
#include "lumenis/media/target_mask.h"
#include "lumenis/media/color_transform.h"
#include "lumenis/media/frame_flex.h"
#include "lumenis/media/lut_file.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/media/scopes.h"

#include "lumenis/model/safe_colors.h"

#include "lumenis/media/audio_input.h"
#include "lumenis/media/compositor.h"
#include "lumenis/media/fields.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/playback.h"

#include "lumenis/core/branding.h"
#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"
#include "lumenis/media/ltc.h"
#include "lumenis/media/audio_cd.h"
#include "lumenis/media/batch_import.h"
#include "lumenis/media/graphic_import.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/audio_suite.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/auto_sequence.h"
#include "lumenis/model/send_to.h"
#include "lumenis/model/simplify_export.h"
#include "lumenis/model/subcap.h"
#include "lumenis/model/export_volume.h"
#include "lumenis/model/bin_relations.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/sync_clips.h"
#include "lumenis/model/clip_audio.h"
#include "lumenis/model/correction_template.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/output_format.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/titler.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/media/as11.h"
#include "lumenis/media/dcp.h"
#include "lumenis/media/movie_export.h"
#include "lumenis/media/render.h"
#include "lumenis/media/waveform_sync.h"
#include "lumenis/ext/deck.h"
#include "lumenis/ext/export_device.h"
#include "lumenis/ext/video_output.h"
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
#include "lumenis/ui/color_info.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/full_screen.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/correction_monitors.h"
#include "lumenis/ui/correction_shape_host.h"
#include "lumenis/ui/correction_tool.h"
#include "lumenis/ui/consolidate_dialog.h"
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/multi_mix_dialog.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/group_clips_dialog.h"
#include "lumenis/ui/sync_clips_dialog.h"
#include "lumenis/ui/digital_cut_tool.h"
#include "lumenis/ui/export_device_dialog.h"
#include "lumenis/ui/export_volume_dialog.h"
#include "lumenis/ui/video_output_tool.h"
#include "lumenis/ui/subcap_dialogs.h"
#include "lumenis/ui/send_to_dialog.h"
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
#include "lumenis/ui/source_settings_dialog.h"
#include "lumenis/media/stereo.h"
#include "lumenis/model/stereo_clip.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timecode_window.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/title_tool.h"
#include "lumenis/ui/titler_panel.h"
#include "lumenis/ui/motion_effect_dialogs.h"
#include "lumenis/ui/motion_effect_editor.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/unmount_dialog.h"
#include "lumenis/ui/set_font_dialog.h"
#include "lumenis/ui/shortcuts_window.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QAction>
#include <QApplication>
#include <QCursor>
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
#include <QMimeData>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPrinter> // the session's printer, destroyed with the window (main_window_print.cpp)
#include <QPushButton>
#include <QKeySequence>
#include <QShortcut>
#include <QRegularExpression>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <QPixmap>
#include <QProgressDialog>
#include <QUrl>
#include <QDesktopServices>
#include <QProcess>
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
    // Screen readers: every control without a name of its own is named from what it shows (10.3).
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) installAccessibleNames(*app);
    resize(1280, 800);

    // Every workspace has the same default layout (p.74).
    panels_ = new PanelManager(this);
    workspaces_ = new WorkspaceManager(*panels_, this);
    for (const QString& name : workspaces_->names()) {
        workspaces_->setDefaultLayout(name, defaultLayout());
    }
    // "Save single floating Bin Containers in Workspaces" (p.1153): a floating Bin Container holding
    // one bin is kept with the workspace, its bin and place, and opens again with it. Others are left
    // as they are.
    workspaces_->setExtras(
        [this]() -> QJsonObject {
            if (!settings_ || !binContainer_ || !settings_->flag(QStringLiteral("Bin"), QStringLiteral("saveSingleFloating"))) return {};
            QJsonArray single;
            for (const QJsonValue& f : binContainer_->binLayout().value(QStringLiteral("floating")).toArray()) {
                if (f.toObject().value(QStringLiteral("bins")).toArray().size() == 1) single.append(f);
            }
            return single.isEmpty() ? QJsonObject() : QJsonObject{{QStringLiteral("floatingBins"), single}};
        },
        [this](const QJsonObject& extras) {
            if (!settings_ || !binContainer_ || !settings_->flag(QStringLiteral("Bin"), QStringLiteral("saveSingleFloating"))) return;
            for (const QJsonValue& fv : extras.value(QStringLiteral("floatingBins")).toArray()) {
                const QJsonObject f = fv.toObject();
                const QJsonArray g = f.value(QStringLiteral("geometry")).toArray();
                const QRect rect(g.at(0).toInt(), g.at(1).toInt(), g.at(2).toInt(520), g.at(3).toInt(360));
                if (BinDocument* d = binContainer_->openBin(f.value(QStringLiteral("bins")).toArray().at(0).toString())) {
                    binContainer_->floatBin(d, rect);
                }
            }
        });

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
    // The buttons' tooltips are their commands' texts, from the registry: given before any button is made (the Composer's
    // are made in createPanels), or they showed the command's id (10.4b found it).
    ButtonMapper::instance().setRegistry(&commands_);
    createPanels();
    // Show local media icons (p.1154): "Clip icons will appear blue to indicate the media is local or
    // pink to indicate mixed resolution." Ours: local is every one of the clip's files there and none
    // on shared storage (model::isSharedLocation); mixed resolution is video media of the clip's MobID
    // at more than one resolution in the media database.
    // The MultiRez bin headings (p.1116; 8.6c, main_window_relink.cpp).
    BinAppearance::multiRezHeadings() = media::multiRezHeadings();
    BinAppearance::multiRezCell() = [self = QPointer<MainWindow>(this)](const model::BinItem& item, const QString& heading) {
        return self ? self->multiRezCell(item, heading) : QString();
    };
    // The proxy columns (p.509-510; 8.7a, main_window_proxy.cpp).
    BinAppearance::proxyCell() = [self = QPointer<MainWindow>(this)](const model::BinItem& item, const QString& heading) {
        return self ? self->proxyCell(item, heading) : QString();
    };
    BinAppearance::mediaTint() = [self = QPointer<MainWindow>(this)](const model::BinItem& item) -> QColor {
        if (!self) return {};
        if (item.kind != model::BinItemKind::MasterClip && item.kind != model::BinItemKind::Subclip &&
            item.kind != model::BinItemKind::AudioClip) {
            return {};
        }
        if (self->mediaDb_.hasMixedResolution(model::mobIdOf(item))) return QColor(235, 120, 200);
        const auto link = media::linkOf(item);
        if (!link || link->unlinked) return {};
        const QStringList files = link->files();
        if (files.isEmpty()) return {};
        for (const QString& file : files) {
            if (!QFileInfo::exists(file) || model::isSharedLocation(file)) return {};
        }
        return QColor(80, 140, 240);
    };
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
    // File > Output > Send To lists Pro Tools and the user's templates (7.1c2), read again each time it opens.
    refreshSendToMenu();
    if (auto* sendToMenu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:file_output_send_to")))
        connect(sendToMenu, &QMenu::aboutToShow, this, &MainWindow::refreshSendToMenu);
    connect(settings_, &Settings::userChanged, this, &MainWindow::refreshSendToMenu);
    // The background services (8.4a) read their options from the user's settings, so they start after these exist.
    setUpBackgroundServices();
    setUpDynamicMediaFolders();
}

MainWindow::~MainWindow() {
    BinAppearance::mediaTint() = nullptr;
    BinAppearance::multiRezCell() = nullptr;
    BinAppearance::proxyCell() = nullptr;
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
        // The Sidebar's Transcription column asks this window; the container refreshes its list once more when it
        // is deleted, after this destructor.
        binContainer_->setTranscriptionStatus({});
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
QStringList MainWindow::binLayouts() const {
    // "Bin layouts are project settings" (p.92).
    const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    QStringList names;
    for (int i : f.versions(QStringLiteral("Bin Layout"))) {
        const QString n = f.entries.at(i).name.trimmed();
        if (!n.isEmpty() && !names.contains(n, Qt::CaseInsensitive)) names << n;
    }
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return QString::compare(a, b, Qt::CaseInsensitive) < 0; });
    return names;
}

bool MainWindow::newBinLayout(const QString& name) {
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    // "The application saves the bin layout, and the layout appears in the Bin Layout menu and in the
    // Settings list" (p.93).
    model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    const QJsonObject values{{QStringLiteral("layout"), binContainer_->binLayout()}};
    int index = -1;
    for (int i : f.versions(QStringLiteral("Bin Layout"))) {
        if (f.entries.at(i).name.trimmed().compare(n, Qt::CaseInsensitive) == 0) index = i;
    }
    if (index >= 0) {
        f.entries[index].values = values;
    } else {
        index = f.add(model::SettingEntry{QStringLiteral("Bin Layout"), n, false, values});
    }
    f.setActive(index);
    settings_->commit(model::SettingsScope::Project);
    currentBinLayout_ = n;
    return true;
}

bool MainWindow::saveCurrentBinLayout() {
    // "To modify a bin layout: Arrange and size your bins. Select Windows > Bin Layout > Save Current"
    // (p.93): into the layout last opened or made (ours).
    if (currentBinLayout_.isEmpty() || !binLayouts().contains(currentBinLayout_, Qt::CaseInsensitive)) return false;
    return newBinLayout(currentBinLayout_);
}

bool MainWindow::deleteBinLayout(const QString& name) {
    // "If you delete a bin layout, the layout is deleted from your project" (p.92).
    model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    QList<int> gone;
    for (int i : f.versions(QStringLiteral("Bin Layout"))) {
        if (f.entries.at(i).name.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0) gone << i;
    }
    if (gone.isEmpty()) return false;
    f.remove(gone);
    settings_->commit(model::SettingsScope::Project);
    if (currentBinLayout_.compare(name.trimmed(), Qt::CaseInsensitive) == 0) currentBinLayout_.clear();
    return true;
}

bool MainWindow::openBinLayout(const QString& name) {
    const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    for (int i : f.versions(QStringLiteral("Bin Layout"))) {
        const model::SettingEntry& e = f.entries.at(i);
        if (e.name.trimmed().compare(name.trimmed(), Qt::CaseInsensitive) != 0) continue;
        // "Media Composer opens all bins saved in the bin layout and places them in the position
        // configured in the layout" (p.92).
        binContainer_->applyBinLayout(e.values.value(QStringLiteral("layout")).toObject());
        currentBinLayout_ = e.name.trimmed();
        return true;
    }
    return false;
}

bool MainWindow::chooseFont() {
    // "Click the Bin, Composer monitor, Script, or Timeline window to make it active" (p.82).
    const Panel* active = panels_->activePanel();
    BinDocument* doc = binsActive() ? binContainer_->activeDocument() : nullptr;
    BinView* view = doc ? binContainer_->viewFor(doc) : nullptr;
    enum class Target { Bin, Composer, Timeline } target;
    QFont current;
    if (view) {
        target = Target::Bin;
        current = view->font();
        const model::BinDisplay& d = doc->bin().display;
        if (!d.fontFamily.isEmpty()) current.setFamily(d.fontFamily);
        if (d.fontSize > 0) current.setPointSize(d.fontSize);
    } else if (active && active->id() == QLatin1String("Composer")) {
        target = Target::Composer;
        current = composer_->nameFont();
    } else if (active && active->id() == QLatin1String("Timeline")) {
        target = Target::Timeline;
        current = timeline_->clipFont();
    } else {
        return false;
    }
    SetFontDialog dialog(current, this);
    const bool ok = fontHook_ ? fontHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return false;
    const QString family = dialog.family();
    const int size = dialog.pointSize();
    switch (target) {
    case Target::Bin: {
        // "saves as a Bin setting (not a Bin View setting)" (p.81); "Bin fonts are stored in each bin" (p.82).
        model::BinDisplay d = doc->bin().display;
        d.fontFamily = family;
        d.fontSize = size;
        doc->setDisplay(d);
        break;
    }
    case Target::Composer:
        // "saves as a Composer setting" (p.81).
        composer_->setNameFont(family, size);
        settings_->setValue(QStringLiteral("Composer"), QStringLiteral("fontFamily"), family);
        settings_->setValue(QStringLiteral("Composer"), QStringLiteral("fontSize"), size);
        break;
    case Target::Timeline: {
        // "saves as a Timeline View setting" (p.82): kept in the display a Timeline View saves.
        Timeline::Display d = timeline_->display();
        d.clipFontFamily = family;
        d.clipFontSize = size;
        timeline_->setDisplay(d);
        break;
    }
    }
    return true;
}

bool MainWindow::placeCurrentWindow(bool home) {
    Panel* panel = panels_->activePanel();
    if (!panel || !panels_->isFloating(panel)) return false;
    QWidget* window = panel->window();
    if (home) {
        window->resize(window->sizeHint().expandedTo(window->minimumSizeHint()));
        QRect r = window->frameGeometry();
        r.moveCenter(frameGeometry().center());
        window->move(r.topLeft());
    } else {
        const QScreen* screen = window->screen();
        if (!screen) return false;
        QRect r = window->frameGeometry();
        r.moveCenter(screen->availableGeometry().center());
        window->move(r.topLeft());
    }
    return true;
}

void MainWindow::unmountDrives() {
    UnmountDialog dialog(this);
    const bool ok = unmountHook_ ? unmountHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    if (dialog.mountAll()) {
        media::remountAllDrives();
    } else {
        media::unmountDrives(dialog.selectedRoots());
    }
}

QString MainWindow::revealFile() {
    BinView* view = binContainer_->activeView();
    BinDocument* doc = binContainer_->activeDocument();
    if (!view || !doc) return {};
    // "Select the clip in a bin for which you want to find the media file" (p.360): the first selected
    // clip with a media file that exists.
    QString file;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = doc->bin().item(id);
        const auto link = item ? media::linkOf(*item) : std::nullopt;
        if (!link) continue;
        for (const QString& f : link->files()) {
            if (QFileInfo::exists(f)) {
                file = f;
                break;
            }
        }
        if (!file.isEmpty()) break;
    }
    if (file.isEmpty()) return {};
    showInFileManager(file);
    return file;
}

void MainWindow::showInFileManager(const QString& file) {
    if (revealHook_) {
        revealHook_(file);
        return;
    }
    // "opens Windows Explorer or the folder (macOS), and highlights related media files" (p.360).
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(file)});
#elif defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), file});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
#endif
}

int MainWindow::binSelect(const QString& what) {
    BinContainer& bins = *binContainer_;
    if (what == QLatin1String("unrendered_titles")) {
        // "Select Bin > Select > Unrendered Titles. The fast-saved titles are highlighted in the bin" (p.359).
        BinDocument* doc = bins.activeDocument();
        BinView* view = doc ? bins.viewFor(doc) : nullptr;
        if (!view) return 0;
        QStringList ids;
        for (const model::BinItem& item : doc->bin().items) {
            if (model::isUnrenderedTitle(item)) ids << item.id;
        }
        view->selectIds(ids);
        return static_cast<int>(ids.size());
    }
    if (what == QLatin1String("offline_items")) {
        // "The bin highlights all items that are missing media files" (p.266): clips whose media is
        // missing, unlinked or never there, and sequences that use such a clip (ours).
        BinDocument* doc = bins.activeDocument();
        BinView* view = doc ? bins.viewFor(doc) : nullptr;
        if (!view) return 0;
        auto clipOffline = [](const model::BinItem& clip) {
            if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::Subclip &&
                clip.kind != model::BinItemKind::AudioClip) {
                return false;
            }
            const auto link = media::linkOf(clip);
            if (!link || link->unlinked) return true;
            for (const QString& file : link->files()) {
                if (!QFileInfo::exists(file)) return true;
            }
            return false;
        };
        QStringList ids;
        for (const model::BinItem& item : doc->bin().items) {
            bool offline = clipOffline(item);
            if (item.kind == model::BinItemKind::Sequence) {
                const model::Sequence seq = model::Sequence::fromItem(item);
                for (const QString& name : seq.trackNames()) {
                    for (const model::Segment& s : seq.track(name)->segments) {
                        if (offline || s.isFiller()) continue;
                        const auto clip = composer_->resolve(s.binId.isEmpty() ? doc->nodeId() : s.binId, s.clipId);
                        offline = clip && clipOffline(*clip);
                    }
                }
            }
            if (offline) ids << item.id;
        }
        view->selectIds(ids);
        return static_cast<int>(ids.size());
    }
    QList<model::OpenBin> open;
    model::BinSelection selected;
    for (BinDocument* doc : bins.openDocuments()) {
        open.append({doc->nodeId(), &doc->bin()});
        if (BinView* v = bins.viewFor(doc)) selected.insert(doc->nodeId(), v->selectedIds());
    }
    model::BinSelection result;
    if (what == QLatin1String("media_relatives")) {
        result = model::mediaRelatives(open, selected);
    } else if (what == QLatin1String("sources")) {
        result = model::sources(open, selected);
    } else if (what == QLatin1String("unreferenced_clips")) {
        result = model::unreferencedClips(open);
    } else {
        return 0;
    }
    // "The system highlights all related objects in all open bins" (p.267): each open bin's selection
    // becomes what was found in it.
    int count = 0;
    for (BinDocument* doc : bins.openDocuments()) {
        const QStringList ids = result.value(doc->nodeId());
        if (BinView* v = bins.viewFor(doc)) v->selectIds(ids);
        count += static_cast<int>(ids.size());
    }
    return count;
}

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
        // The Titler+ template being edited lives in no bin (editTitleTemplate).
        if (binId == QLatin1String(kTitleTemplateBin)) {
            if (templateEdit_ && templateEdit_->sequence.id == itemId) return templateEdit_->sequence;
            return std::nullopt;
        }
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
    auto plainPicture = [this](MonitorId monitor, std::int64_t at, QSize size, bool* offline) -> QImage {
        // MultiCamera mode: the Source monitor shows the sequence's group (p.1129).
        if (monitor == MonitorId::Source && composer_->multiCameraMode()) return multiCamPicture(size, offline);
        const model::MonitorState& state = composer_->state(monitor);
        // A sequence: its video tracks composited with their effects (Phase 6.1, media::composeImage).
        // A Title Effect clip (6.8d4a) is composited as its one-segment sequence: the title over black. A synced
        // subclip (9.3, sync_clips.h) as its sequence.
        if (const model::LoadedClip* loaded = state.current(); loaded && model::composedSequence(loaded->item)) {
            model::Sequence seq = *model::composedSequence(loaded->item);
            // A title being made shows as it is, before its undo step (EffectEditor::pictureOverride, 6.8b).
            if (monitor == MonitorId::Record && effectEditor_) {
                if (const auto o = effectEditor_->pictureOverride()) {
                    if (model::Effect* e = model::effectFor(seq, o->first)) *e = o->second;
                }
            }
            media::Composition c;
            c.sequence = &seq;
            c.tracks = shownVideoTracks(monitor, seq);
            c.size = size;
            // In a proxy mode "each frame in the sequence is transcoded on-the-fly ... based on the proxy mode" (p.526):
            // never more pixels than the proxy raster; the monitor scales the picture up.
            if (project_ && project_->settings.proxy != 0) {
                const QSize proxy = model::proxyRaster(project_->settings);
                if (size.width() > proxy.width() || size.height() > proxy.height()) c.size = size.scaled(proxy, Qt::KeepAspectRatio);
            }
            fillComposition(c, *loaded);
            c.effects = renderOnTheFly_ || composer_->isPlaying();
            // Dual Split's uncorrected part in Color Correction mode (FX Guide p.592).
            c.corrections = !composer_->uncorrectedPictures();
            // The Color Correction tool's shape panel: Show Alpha, Show Result and the layer worked on (FX Guide p.698).
            if (timeline_) c.shapeView = timeline_->correctionShapeView();
            // The Effect Preview shows Pan and Scan's original picture with its wire frame; "To see the results
            // of the Pan and Scan effect while in Effect mode, click the Play button" (FX Guide p.234).
            c.effectPreview = monitor == MonitorId::Record && composer_->effectPreview().on && !composer_->isPlaying();
            // The FrameFlex effect in the Effect Preview: its segment shows the whole image under the box (p.533).
            framingPreview_.reset();
            if (c.effectPreview && effectEditor_) framingPreview_ = effectEditor_->framingSegment();
            bool missing = false;
            const QImage image = media::composeImage(c, at, &missing);
            framingPreview_.reset();
            if (image.isNull() && missing && offline) *offline = true;
            // A fast-saved title, or one whose precomputes are gone, is "media offline" (FX Guide p.358).
            if (missing && offline && loaded->item.kind == model::BinItemKind::Title) *offline = true;
            if (composer_->isPlaying() && monitor == composer_->activeMonitor() && at == state.position()) {
                const int step = composer_->speed() > 0 ? 1 : -1;
                for (int k = 1; k <= 2; ++k) {
                    if (const auto next = frameAt(monitor, state.position() + k * step * std::abs(composer_->speed()))) {
                        frames_->prefetch(next->first, next->second, size);
                    }
                }
            }
            return image;
        }
        if (const model::LoadedClip* loaded = state.current()) {
            // A group clip in Quad Split or Nine Split Source view (p.1127-1128).
            if (monitor == MonitorId::Source && loaded->item.kind == model::BinItemKind::Group && composer_->groupSplit() > 1) {
                if (model::isMultigroup(loaded->item)) {
                    // A multigroup: the group under the position, split (p.1125).
                    model::GroupSegmentAt seg;
                    const auto group = sourceGroup(at, &seg);
                    if (!group) return {};
                    return groupSplitImage(*group, seg.groupFrame, size, offline, seg.segment.groupAngle, seg.segment.groupOffsets);
                }
                return groupSplitImage(loaded->item, at, size, offline);
            }
            if (auto field = motionFieldFrame(loaded->item, at, size)) return *field;
        }
        const auto shown = frameAt(monitor, at);
        if (!shown) return {};
        const auto& [item, frame] = *shown;
        if (frames_->isOffline(playedClip(item))) {
            if (offline) *offline = true;
            return {};
        }
        // Playing: the next frames are decoded while this one shows.
        if (composer_->isPlaying() && monitor == composer_->activeMonitor() && at == state.position()) {
            const int step = composer_->speed() > 0 ? 1 : -1;
            for (int k = 1; k <= 2; ++k) {
                if (const auto next = frameAt(monitor, state.position() + k * step * std::abs(composer_->speed()))) {
                    frames_->prefetch(next->first, next->second, size);
                }
            }
        }
        // "The new framing of the image will be applied when you drop the clip in the Source viewer" (p.531).
        return adaptedFrame(item, frame, size, model::spatialAdapterOf(item), model::colorAdapterOf(item));
    };
    // Stereoscopic projects (8.6d2): each monitor's S3D View and Overlay over its picture of each eye.
    composer_->setPictureResolver([this, plainPicture](MonitorId monitor, std::int64_t at, QSize size, bool* offline) {
        return stereoPicture(monitor, [&] { return plainPicture(monitor, at, size, offline); });
    });
    // The Safe Color Limiter's analysis display (FX Guide p.729-730; 6.10e4c2): "By default, when the Effect Editor is
    // open, the effect changes the display in the Source monitor to an analysis mode that highlights pixels in the image
    // that fall outside the current safe color limits". The Record monitor's frame without its limiters is analysed
    // with the shown limiter's limits.
    composer_->setMonitorOverride([this](MonitorId monitor, QSize size) -> QImage {
        if (monitor != MonitorId::Source || !effectEditor_ || !effectEditor_->isVisible()) return {};
        const auto limiting = effectEditor_->safeColorAnalysis();
        if (!limiting) return {};
        const model::MonitorState& record = composer_->state(MonitorId::Record);
        const model::LoadedClip* loaded = record.current();
        if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) return {};
        model::Sequence seq = model::Sequence::fromItem(loaded->item);
        media::Composition c;
        c.sequence = &seq;
        c.tracks = shownVideoTracks(MonitorId::Record, seq);
        c.size = size;
        fillComposition(c, *loaded);
        // Computed, not played from the limiter's render, and without the limiting.
        c.rendered = {};
        c.limitSafeColors = false;
        const QImage frame = media::composeImage(c, record.position());
        return media::safeColorAnalysisImage(frame, limiting->limits, limiting->compositeLuma, limiting->rgb);
    });
    // Sound: at normal speed forward the monitor's audio plays, and its
    // progress paces the picture (A/V sync).
    connect(composer_, &Composer::playStarted, this, [this](MonitorId monitor, int speed) { startSound(monitor, speed); });
    connect(composer_, &Composer::playStopped, this, [this] {
        audio_->stop();
        // Frames the picture could not keep up with (the real-time gate).
        const auto& stats = composer_->playStats();
        if (stats.dropped > 0) {
            // Console only: how many frames playback dropped.
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
        // "For clips in a bin, you can delete only the media that was originally associated with the clip" (p.1117):
        // the clip's affinity media, every resolution named by its MobID, listed by resolution.
        QStringList files;
        QMap<QString, QStringList> byResolution;
        int clips = 0;
        for (const QString& id : ids) {
            const model::BinItem* item = doc.bin().item(id);
            if (!item) continue;
            const QMap<QString, QStringList> affinity = media::affinityMediaFiles(*item, mediaDb_);
            if (affinity.isEmpty()) continue;
            ++clips;
            for (auto it = affinity.begin(); it != affinity.end(); ++it) {
                byResolution[it.key()] += it.value();
                files += it.value();
            }
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
            // With media at several resolutions, a box for each ("Select the items you want to delete", p.1117).
            QHash<QString, QCheckBox*> resolutionBoxes;
            if (byResolution.size() > 1) {
                for (auto it = byResolution.begin(); it != byResolution.end(); ++it) {
                    auto* box = new QCheckBox(tr("    %1 media (%n file(s))", "", static_cast<int>(it.value().size())).arg(it.key()), &dialog);
                    box->setObjectName(QStringLiteral("deleteResolution:") + it.key());
                    box->setChecked(true);
                    box->setEnabled(false);
                    connect(mediaBox, &QCheckBox::toggled, box, &QWidget::setEnabled);
                    resolutionBoxes.insert(it.key(), box);
                    layout->addWidget(box);
                }
            }
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            if (dialog.exec() != QDialog::Accepted) return std::nullopt;
            choice = {clipBox->isChecked(), mediaBox->isChecked()};
            if (choice.media && !resolutionBoxes.isEmpty()) {
                files.clear();
                for (auto it = resolutionBoxes.begin(); it != resolutionBoxes.end(); ++it) {
                    if (it.value()->isChecked()) files += byResolution.value(it.key());
                }
                files.removeDuplicates();
            }
        }
        if (choice.media) {
            frames_->clear();
            media::deleteMediaFiles(files, &mediaDb_);
        }
        return choice.clips ? ids : QStringList{};
    });
    // Dynamic Relink "performs a dynamic relink whenever clips are loaded into the source monitor or the Timeline"
    // (p.1107; main_window_relink.cpp).
    composer_->setLoadFilter([this](const QString& binId, const model::BinItem& item) { return dynamicRelinkItem(binId, item); });
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
        QStringList captions;
        for (const QString& p : paths) {
            (p.endsWith(QLatin1String(".scc"), Qt::CaseInsensitive) ? captions
             : interop::otio::isOtioFile(p)                         ? timelines
             : interop::fcpxml::isFcpXmlFile(p)                     ? fcp
                                                                    : media)
                << p;
        }
        importOtio(binId, timelines);
        importFcpXml(binId, fcp);
        importScc(binId, captions);
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
    // Export Volume bins (7.2d): what they refused, and their Properties and Commit buttons.
    connect(binContainer_, &BinContainer::volumeRefused, this, [](const QString&, const QStringList& names) {
        reportFailure(nullptr, tr("Some assets that you selected could not be dragged to the Volume Bin: %1").arg(names.join(QStringLiteral(", "))));
    });
    connect(binContainer_, &BinContainer::exportVolumePropertiesRequested, this, [this](const QString& binId) { editExportVolume(binId); });
    connect(binContainer_, &BinContainer::exportVolumeCommitRequested, this, [this](const QString& binId) { commitExportVolume(binId); });
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
        // A script opens in the Script window (p.570).
        if (const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr; item && model::isScript(*item)) {
            openScript(binId, itemId);
            return;
        }
        // "Ctrl+double-click the Title Effect icon in the bin. The title opens in the Title tool" (p.359).
        if ((QApplication::keyboardModifiers() & Qt::ControlModifier) && reviseTitle(binId, itemId)) return;
        // A Color Correction effect template: "Select the template in the bin ... and then press Enter" in Color
        // Correction mode applies it to the current segment; double-clicked "In editing or Effect mode", to each selected
        // segment (FX Guide p.614-615), both with the Correction Type's relationship (p.615).
        if (const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr) {
            if (const auto t = model::correctionTemplateOf(*item)) {
                if (timeline_->isCorrectionMode()) {
                    timeline_->applyCorrectionTemplate(*t, false);
                    correctionTool_->reload();
                } else {
                    timeline_->applyCorrectionTemplateToSelection(*t);
                }
                return;
            }
        }
        if (const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr) {
            composer_->loadDefault(binId, *item);
            panels_->showPanel(panels_->panel(QStringLiteral("Composer")));
        }
    });
    // The Timeline (ch. 19), showing the Record monitor's sequence.
    timeline_ = new Timeline(*composer_);
    connectMultiRez(); // the MultiRez button through Dynamic Relink (8.6b)
    connectPrinting(); // Page Setup, Print Bin, Print Frame, Print Timeline (9.1)
    // Closed captions on D1 (7.4b): the Timeline's D-track and Show Captions read the clips' caption data.
    timeline_->setCaptionClipData([this](const model::Segment& s) { return captionClipData(s); });
    composer_->setCaptionResolver([this](MonitorId m, std::int64_t f) { return captionOn(m, f); });
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
        // Edits to a Titler+ template being edited stay with it until Save (p.320).
        if (binId == QLatin1String(kTitleTemplateBin)) {
            if (templateEdit_ && templateEdit_->sequence.id == item.id) {
                templateEdit_->sequence = item;
                templateEdit_->dirty = true;
            }
            return true;
        }
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
        item.fields.insert(QStringLiteral("Format"), binContainer_->sequenceFormat());
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
    // The Timeline's refusals (an edit that cannot be made) show at the pointer, where the user is working, and stay in
    // the Console after the tooltip goes (9.7).
    connect(timeline_, &Timeline::message, this, [](const QString& text) {
        QToolTip::showText(QCursor::pos(), text);
        ConsoleLog::instance().append(tr("Timeline: %1").arg(text)); // Console only: the tooltip above shows it.
    });
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
    appliedRelink_.reset(); // the new project's Dynamic Relink setting is read afresh
    timeline_->setProjectHeight(project.settings.format.height);
    // "You will see a WaveformCache folder in the Avid Projects directory" (p.677): ours is in the
    // project's folder.
    timeline_->setWaveformFolder(QDir(project.folder).filePath(QStringLiteral("WaveformCache")));
    setWindowTitle(QStringLiteral("%1 - %2").arg(QString::fromUtf8(branding::kProductName), project.settings.name));
    commands_.setHandler(QStringLiteral("file.close_project"), [this] { emit closeProjectRequested(); });
    // Project settings first: they include the new-sequence timecode.
    settings_->setProject(project.folder);
    if (settingsWindow_) settingsWindow_->setProject(project);
    // Renders are made and found at the project's proxy mode (p.527).
    model::setRenderProxy(project.settings.proxy);
    // The resolutions media can be made in follow the project's format (p.1240-1272; ROADMAP 11.1c).
    tellMediaTheFormat();
    // External LUTs (p.541-542; 8.5d1): the project's and every project's, listed as "External <name>".
    {
        QStringList failures;
        media::registerLutFolders({media::projectLutFolder(project.folder), media::sharedLutFolder()}, &failures);
        for (const QString& f : failures) reportFailure(this, tr("LUT not installed: %1").arg(f));
    }
    binContainer_->setProject(project.folder, project.settings.name, project.settings.format.editRate);
    binContainer_->setSequenceFormat(QString::fromStdString(project.settings.format.label()));
    composer_->clear();
    composer_->setAspectRatio(project.settings.aspect);
    timeline_->history().clear();
    projectOpened_ = QDateTime::currentDateTimeUtc();
    installProjectHandlers(true);
    setUpTranscription(true);
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
    media::setProjectFormat({}, Rational(), false); // every format again, until the next project
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    commands_.setHandler(QStringLiteral("file.close_project"), {});
    binContainer_->setProject({}, {}, Rational(24000, 1001));
    composer_->clear();
    timeline_->history().clear();
    model::setRenderProxy(0);
    if (settingsWindow_) {
        settingsWindow_->hide();
        settingsWindow_->setProject(std::nullopt);
    }
    if (siteSettingsWindow_) siteSettingsWindow_->hide();
    settings_->setProject({});
    installProjectHandlers(false);
    setUpTranscription(false);
}

bool MainWindow::editFromBin(bool splice) {
    BinDocument* doc = binContainer_->activeDocument();
    BinView* view = doc ? binContainer_->viewFor(doc) : nullptr;
    if (!view) return false;
    const QStringList ids = view->selectedIds();
    // "Select a clip in the bin" (p.624): one.
    if (ids.size() != 1) return false;
    const model::BinItem* item = doc->bin().item(ids.first());
    if (!item) return false;
    QMimeData probe;
    probe.setData(binItemsMime(), QStringList{doc->nodeId(), item->id}.join(QLatin1Char('\n')).toUtf8());
    if (!timeline_->droppedClip(&probe)) return false;
    // A failed edit says why through the Timeline, as the Splice-in and Overwrite buttons do.
    timeline_->editClip(splice ? model::EditKind::SpliceIn : model::EditKind::Overwrite, doc->nodeId(), *item);
    return true;
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
    // "To close all but active bins, scripts, and pop-up monitors" (p.296): there are no scripts or
    // pop-up monitors yet, so it closes the other bins.
    handle("windows.close_all_but_active", [bins] { bins->closeAllBinsButActive(); });
    handle("file.reveal_file", [this] {
        if (revealFile().isEmpty()) {
            QMessageBox::information(this, tr("Reveal File"), tr("Select a clip whose media file exists in a bin."));
        }
    });
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
        } else if (const Panel* fx = panels_->activePanel(); fx && fx->id() == QLatin1String("EffectEditor")) {
            // The selected keyframes (FX Guide p.105).
            effectEditor_->deleteKeyframes(EffectEditor::KeyframeMode::AllParameters);
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
            // "Select Edit > Enlarge Frame or Edit > Reduce Frame" enlarges or reduces the slates (p.578).
            if (!binsActive() && active && active->id() == QLatin1String("Script")) {
                ScriptSlateOptions o = scriptWindow_->slateOptions();
                o.frameScale = std::clamp(o.frameScale * (steps > 0 ? 1.25 : 0.8), 0.25, 4.0);
                scriptWindow_->setSlateOptions(o);
                return;
            }
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
    // The Make Subclip button (Edit tab, p.408): "Media Composer creates the subclip and places it in the active bin"
    // from the clip marked in the Source monitor; with Alt "a dialog box ... lets you select the destination bin".
    // Without an active bin, the clip's own bin (ours).
    handle("edit.make_subclip", [this, bins] {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
        if (!loaded || (loaded->item.kind != model::BinItemKind::MasterClip && loaded->item.kind != model::BinItemKind::Subclip) ||
            model::isSyncedSubclip(loaded->item)) {
            reportFailure(this, tr("Make Subclip: load a master clip or subclip into the Source monitor and mark it first."));
            return;
        }
        auto writable = [](const BinDocument* d) { return d && !d->isReadOnly(); };
        QList<BinDocument*> targets;
        for (BinDocument* d : bins->openDocuments()) {
            if (writable(d)) targets << d;
        }
        BinDocument* target = bins->activeDocument();
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            QStringList names;
            for (BinDocument* d : targets) names << d->name();
            bool ok = false;
            const QString chosen = QInputDialog::getItem(this, tr("Make Subclip"), tr("Destination bin:"), names, 0, false, &ok);
            if (!ok) return;
            target = targets.value(static_cast<int>(names.indexOf(chosen)));
        }
        if (!writable(target)) target = bins->document(loaded->binId);
        if (!writable(target)) {
            reportFailure(this, tr("Make Subclip: open a bin for the subclip first."));
            return;
        }
        const model::MonitorState& s = composer_->state(MonitorId::Source);
        const auto sub = media::makeSubclip(target->bin(), loaded->item, s.markedStart(), s.markedEnd() + 1, QDateTime::currentDateTimeUtc());
        if (!sub) return;
        target->addItem(*sub);
        if (BinView* shown = bins->viewFor(target)) shown->selectIds({sub->id});
    });
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
    stopBackgroundServicesOnExit();
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        emit closed();
    }
}

// The Settings window is made on first use and kept; it is a top-level window parented to this one.
SettingsWindow* MainWindow::showSettings() {
    if (!settingsWindow_) {
        settingsWindow_ = new SettingsWindow(*settings_, this);
        connect(settingsWindow_, &SettingsWindow::proxyChanged, this, [this](int mode) { setProxyMode(mode); });
        connect(settingsWindow_, &SettingsWindow::maskMarginsChanged, this, [this](const model::MaskMargins& m) { setMaskMargins(m); });
        connect(settingsWindow_, &SettingsWindow::outputColorSpaceChanged, this, [this](const QString& s) { setOutputColorSpace(s); });
        connect(settingsWindow_, &SettingsWindow::projectFormatChanged, this, [this](const ProjectFormat& f) { setProjectFormat(f); });
        connect(settingsWindow_, &SettingsWindow::toolRequested, this, [this](const QString& tool) {
            if (tool == QLatin1String("toolPalette")) showTool(QStringLiteral("ToolPalette"));
            if (tool == QLatin1String("keyboard")) showTool(QStringLiteral("Keyboard"));
            // An Audio Multi-Mix setting opens the tool to change it, without Mix (p.734).
            if (tool == QLatin1String("multiMix")) multipleMixes(false);
            // A Video Output setting opens the Video Output tool (7.3b).
            if (tool == QLatin1String("videoOutputTool")) openVideoOutput();
            // The Transcript setting's Manage Speakers button (p.445).
            if (tool == QLatin1String("manageSpeakers")) manageSpeakers();
            if (tool == QLatin1String("manageTranscripts")) manageTranscripts();
            // The Color Management setting's Select LUT file (p.539, p.541).
            if (tool == QLatin1String("installLut")) installLutFile();
            // A Bin Layout setting: opened (p.92).
            if (tool == QLatin1String("binLayout")) {
                const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
                const int i = f.activeIndex(QStringLiteral("Bin Layout"));
                if (i >= 0) openBinLayout(f.entries.at(i).name);
            }
            // A Title Style setting: "Open the Title Style Sheet dialog box for the style from the Settings list" (FX Guide
            // p.363).
            if (tool == QLatin1String("titleStyle")) {
                const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
                const int i = f.activeIndex(QStringLiteral("Title Style"));
                if (i >= 0) {
                    model::ClassicTitleStyle style = model::ClassicTitleStyle::fromJson(f.entries.at(i).values);
                    style.name = f.entries.at(i).name.trimmed();
                    if (const auto edited = titleStyleSheet(settingsWindow_, style)) saveTitleStyle(*edited);
                }
            }
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
    // "Click the Bin Layout menu and select a layout you want to link to the workspace" (p.91).
    if (const QString layout = values.value(QStringLiteral("binLayout")).toString().trimmed(); !layout.isEmpty()) {
        openBinLayout(layout);
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
    // The Color Info tool (FX Guide p.696-697): the colour under the pointer, over the video monitors' pictures or, with
    // "Active only for video monitors" off, anywhere on the screen.
    colorInfo_ = new ColorInfoTool;
    colorInfo_->setSampler([this](const QPoint& global, bool monitorsOnly) -> std::optional<QColor> {
        if (const auto c = composer_ ? composer_->colorAtScreen(global) : std::nullopt) return c;
        if (monitorsOnly) return std::nullopt;
        QScreen* screen = QGuiApplication::screenAt(global);
        if (!screen) return std::nullopt;
        const QPoint at = global - screen->geometry().topLeft();
        const QImage pixel = screen->grabWindow(0, at.x(), at.y(), 1, 1).toImage();
        return pixel.isNull() ? std::nullopt : std::optional<QColor>(pixel.pixelColor(0, 0));
    });
    panels_->addPanel(new Panel(QStringLiteral("ColorInfo"), tr("Color Info"), colorInfo_));
    // Tools > Transcript Tool (p.442; main_window_transcripts.cpp).
    setUpTranscriptTool();
    setUpScriptWindow();
    // Effects (FX guide p.37, p.83).
    effectPalette_ = new EffectPalette;
    panels_->addPanel(new Panel(QStringLiteral("EffectPalette"), tr("Effect Palette"), effectPalette_));
    connect(effectPalette_, &EffectPalette::effectActivated, this,
            [this](const EffectInfo& effect) {
                // A Color Correction Preset in Color Correction mode: on the current segment, as a template's Enter
                // (FX Guide p.611, p.614).
                if (!effect.correctionPreset.isEmpty() && timeline_->isCorrectionMode()) {
                    timeline_->applyCorrectionTemplate(model::CorrectionTemplate::fromJson(effect.correctionPreset), false);
                    correctionTool_->reload();
                    return;
                }
                timeline_->applyEffectToSelection(effect);
            });
    // The open bins' effect templates in the palette (FX Guide p.37-38, p.53); double-clicked, as an effect.
    connect(effectPalette_, &EffectPalette::templateActivated, this, [this](const QString& binId, const QString& itemId) {
        // An automatic template: "highlighting it in the Effect Palette and pressing Enter" (FX Guide p.612).
        if (const auto automatic = timeline_->automaticCorrection(binId, itemId)) {
            if (timeline_->isCorrectionMode()) {
                timeline_->applyCorrectionTemplate(*automatic, false);
                correctionTool_->reload();
            } else {
                timeline_->applyCorrectionTemplateToSelection(*automatic);
            }
            return;
        }
        const BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
        if (const auto t = item ? model::effectTemplateOf(*item) : std::nullopt) timeline_->applyTemplateToSelection(*t);
        // A Color Correction effect template: as from the bin (FX Guide p.614-615).
        if (const auto c = item ? model::correctionTemplateOf(*item) : std::nullopt) {
            if (timeline_->isCorrectionMode()) {
                timeline_->applyCorrectionTemplate(*c, false);
                correctionTool_->reload();
            } else {
                timeline_->applyCorrectionTemplateToSelection(*c);
            }
        }
    });
    connect(binContainer_, &BinContainer::binItemsChanged, this, [this] { refreshTemplateBins(); });
    connect(timeline_, &Timeline::automaticCorrectionsChanged, this, [this] { refreshTemplateBins(); });
    connect(binContainer_, &BinContainer::activeBinChanged, this, [this] { refreshTemplateBins(); });
    // Applying Effects opens Effect Editor (Timeline settings, p.1220).
    connect(timeline_, &Timeline::effectApplied, this, [this] {
        if (timeline_->options().openEffectEditor) showTool(QStringLiteral("EffectEditor"));
    });
    effectEditor_ = new EffectEditor(*timeline_);
    // The FrameFlex effect's box is drawn over the segment's whole source frame (8.5b3).
    effectEditor_->setFramingSource([this](const model::Segment& segment) -> QSize {
        auto clip = composer_->resolve(segment.binId, segment.clipId);
        if (!clip || !media::isLinked(*clip) || frames_->isOffline(*clip)) return {};
        return frames_->frame(model::clipForSegment(*clip, segment), segment.sourceIn, QSize()).size();
    });
    // "The Avid Titler+ Dashboard and the Effect Editor open" (FX Guide p.303; the Dashboard comes in 6.8b).
    // Ours: in Effect mode, whose Effect Preview the title is edited in (titler_tool.h).
    connect(timeline_, &Timeline::titlerApplied, this, [this] { openTitleEditing(); });
    // Titler+ templates: Edit Template, a drop on the Record monitor, the Dashboard's Save and Save As (p.319-320).
    connect(binContainer_, &BinContainer::editTemplateRequested, this,
            [this](const QString& binId, const QString& itemId) { editTitleTemplate(binId, itemId); });
    connect(composer_, &Composer::titleTemplateDropped, this,
            [this](const QString& binId, const QString& itemId) { editTitleTemplate(binId, itemId); });
    connect(effectEditor_, &EffectEditor::titleTemplateSaveRequested, this, [this](bool as) { saveTitleTemplate(as); });
    // Edit Title (FX Guide p.372): the Effect Editor's button and the Timeline's context menu.
    // "Open the color info tool when picking color" (Effect Editor setting, Editing Guide p.1172).
    connect(effectEditor_, &EffectEditor::colorPickingStarted, this, [this] {
        if (settings_->flag(QStringLiteral("Effect Editor"), QStringLiteral("openColorInfoTool"))) showTool(QStringLiteral("ColorInfo"));
    });
    connect(effectEditor_, &EffectEditor::editTitleRequested, this, [this](const model::SegmentRef& ref) { editTitleInSequence(ref); });
    connect(effectEditor_, &EffectEditor::subcapActionRequested, this, [this](const QString& action) { subcapAction(action); });
    connect(timeline_, &Timeline::editTitleRequested, this, [this](const model::SegmentRef& ref) { editTitleInSequence(ref); });
    connect(timeline_, &Timeline::sourceSettingsRequested, this, [this](const model::SegmentRef& ref) { sourceSettingsForSegment(ref); });
    connect(composer_, &Composer::changed, this, [this] { checkTitleTemplate(); });
    // AutoStabilize "Opens the Effect Editor (if necessary) and the Tracking window", then tracks (FX Guide p.277).
    // After the drop or double-click that applied the effect has finished.
    connect(timeline_, &Timeline::autoStabilizeRequested, this, [this](const QString& track, std::int64_t frame) {
        QTimer::singleShot(0, this, [this, track, frame] {
            showTool(QStringLiteral("EffectEditor"));
            effectEditor_->autoStabilize(track, frame);
        });
    });
    // Motion tracking's frames (FX Guide p.256, p.262): the video tracks below the effect's own ("you almost
    // always want to track the background"), or with Track Foreground the effect's own segment without its effect.
    // Ours: 480 pixels wide, at the project's shape.
    effectEditor_->setTrackingFrames([this](const model::EffectRef& ref, std::int64_t frame, bool foreground) -> QImage {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
        if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) return {};
        const model::Sequence seq = model::Sequence::fromItem(loaded->item);
        const model::Track* own = seq.track(ref.track);
        if (!own) return {};
        media::Composition c;
        c.sequence = &seq;
        const Rational aspect = composer_->aspectRatio();
        const double ratio = aspect.num() > 0 && aspect.den() > 0 ? static_cast<double>(aspect.num()) / static_cast<double>(aspect.den()) : 16.0 / 9.0;
        c.size = QSize(480, std::max(1, static_cast<int>(std::lround(480.0 / ratio))));
        fillComposition(c, *loaded);
        if (foreground) {
            c.tracks = {ref.track};
            c.effects = false;
        } else {
            for (const model::Track& t : seq.tracks()) {
                if (t.kind == model::TrackKind::Video && t.number < own->number) c.tracks << t.name();
            }
        }
        return media::composeImage(c, std::max<std::int64_t>(frame, 0));
    });
    // "Media Composer opens the Effect Editor and the Record monitor becomes the Effect Preview monitor"
    // (FX Guide p.49).
    connect(timeline_, &Timeline::effectModeChanged, this, [this](bool on) {
        if (on) showTool(QStringLiteral("EffectEditor"));
    });
    // The Effect Editor shortcut menu changes the Effect Editor setting too (p.131).
    // The playback performance indicators (FX Guide p.136-137): after playing the Record monitor's
    // sequence, in TC1, while the setting is on; a new play clears them.
    connect(composer_, &Composer::playStarted, this, [this] { timeline_->setPlaybackIndicators({}); });
    connect(composer_, &Composer::playbackDifficulties, this, [this](MonitorId monitor, const QList<Composer::PlaybackRange>& ranges) {
        if (monitor == MonitorId::Record && highlightRenderAreas_) timeline_->setPlaybackIndicators(ranges);
    });
    connect(effectEditor_, &EffectEditor::renderRequested, this, [this](Qt::KeyboardModifiers m) {
        // While ExpertRender's selection is being modified, the button renders that set (p.145).
        if (timeline_->isModifyingExpertSelection()) {
            renderExpertSelection(m.testFlag(Qt::AltModifier));
            return;
        }
        renderEffects(true, m.testFlag(Qt::ShiftModifier), m.testFlag(Qt::AltModifier));
    });
    connect(effectEditor_, &EffectEditor::optionsChanged, this, [this] {
        const EffectEditor::Options o = effectEditor_->options();
        const QString kind = QStringLiteral("Effect Editor");
        static const char* const modes[] = {"activeParameter", "activeGroup", "openGroups", "enabledGroups", "openGraphs", "allParameters"};
        settings_->setValue(kind, QStringLiteral("indentRows"), o.indentRows);
        settings_->setValue(kind, QStringLiteral("largeText"), o.largeText);
        settings_->setValue(kind, QStringLiteral("setPositionToKeyframe"), o.setPositionToKeyframe);
        settings_->setValue(kind, QStringLiteral("updatePositionWhilePlaying"), o.updatePositionWhilePlaying);
        settings_->setValue(kind, QStringLiteral("showAddKeyframeModeMenu"), o.showModeMenu);
        settings_->setValue(kind, QStringLiteral("addKeyframeCommand"), QString::fromLatin1(modes[static_cast<int>(o.mode)]));
    });
    panels_->addPanel(new Panel(QStringLiteral("EffectEditor"), tr("Effect Editor"), effectEditor_));
    // The Color Correction tool (FX Guide p.595-605): open while the Timeline is in Color Correction mode (ours: it
    // closes when the mode ends), showing the correction at the position.
    correctionTool_ = new CorrectionTool(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("ColorCorrection"), tr("Color Correction"), correctionTool_));
    connect(timeline_, &Timeline::correctionModeChanged, this, [this](bool on) {
        if (on) {
            correctionTool_->reload();
            showTool(QStringLiteral("ColorCorrection"));
        } else if (Panel* p = panels_->panel(QStringLiteral("ColorCorrection")); p && panels_->isOpen(p)) {
            panels_->closePanel(p);
        }
    });
    // The tool's Effect Mode button (FX Guide p.684): "the Effect Mode button simply opens the Effect Editor"; with the
    // editor open it enters "the standard Effect Mode layout". The Effect Mode button elsewhere leaves Color
    // Correction mode for Effect mode, as p.589 has it ("Click a mode button other than Color Correction").
    connect(correctionTool_, &CorrectionTool::effectModeRequested, this, [this] {
        if (Panel* p = panels_->panel(QStringLiteral("EffectEditor")); p && panels_->isOpen(p)) timeline_->perform(QStringLiteral("fx.effect_mode"));
        else showTool(QStringLiteral("EffectEditor"));
    });
    // The Correction Mode Settings button opens the Correction setting (p.606).
    connect(correctionTool_, &CorrectionTool::colorInfoRequested, this, [this] { showTool(QStringLiteral("ColorInfo")); });
    // The shape panel's layer, Show Alpha and Show Result change what the monitors show (FX Guide p.698).
    connect(correctionTool_, &CorrectionTool::shapeDisplayChanged, this, [this] { composer_->correctionMonitors()->refresh(); });
    // The shape tools draw in the Current monitor (p.698; 6.10e3b2).
    composer_->correctionMonitors()->setOverlayInput(correctionTool_->shapeHost()->tool());
    connect(correctionTool_->shapeHost(), &CorrectionShapeHost::overlayChanged, this,
            [this] { composer_->correctionMonitors()->setOverlay(correctionTool_->shapeHost()->overlay()); });
    // "In the Color Correction tool, click the Safe Color Settings button" (FX Guide p.725): the Safe Colors setting.
    connect(correctionTool_, &CorrectionTool::safeColorSettingsRequested, this, [this] {
        const int i = settings_->file(model::SettingsScope::Project).activeIndex(QStringLiteral("Safe Colors"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::Project);
            w->list(model::SettingsScope::Project)->openEntry(i);
        }
    });
    connect(correctionTool_, &CorrectionTool::settingsRequested, this, [this] {
        const int i = settings_->file(model::SettingsScope::User).activeIndex(QStringLiteral("Correction"));
        if (SettingsWindow* w = showSettings(); w && i >= 0) {
            w->showTab(SettingsWindow::Tab::User);
            w->list(model::SettingsScope::User)->openEntry(i);
        }
    });
    auto reloadCorrection = [this] {
        if (timeline_->isCorrectionMode()) correctionTool_->reload();
    };
    connect(timeline_, &Timeline::changed, this, reloadCorrection);
    connect(composer_, &Composer::changed, this, reloadCorrection);
    connect(correctionTool_, &CorrectionTool::lumaPreviewChanged, this, [this](CorrectionTool::LumaPreview p) {
        const auto preview = p == CorrectionTool::LumaPreview::Input       ? CorrectionMonitors::Preview::Input
                             : p == CorrectionTool::LumaPreview::ThreeTone ? CorrectionMonitors::Preview::ThreeTone
                                                                            : CorrectionMonitors::Preview::Output;
        composer_->correctionMonitors()->setPreview(preview, [this] { return correctionTool_->correction(); });
    });
    connect(correctionTool_, &CorrectionTool::channelPreviewChanged, this, [this](CorrectionTool::ChannelPreview p) {
        using C = CorrectionTool::ChannelPreview;
        using P = CorrectionMonitors::Preview;
        const P preview = p == C::RedInput ? P::RedInput : p == C::GreenInput ? P::GreenInput : p == C::BlueInput ? P::BlueInput
                        : p == C::RedOutput ? P::RedOutput : p == C::GreenOutput ? P::GreenOutput : p == C::BlueOutput ? P::BlueOutput
                        : p == C::MasterInput ? P::Input : P::Output;
        composer_->correctionMonitors()->setPreview(preview);
    });
    // The Color Selection buttons sample colours in the monitors (FX Guide p.678-679).
    connect(correctionTool_, &CorrectionTool::colorSelectionChanged, this, [this](CorrectionTool::ColorSelection s) {
        composer_->correctionMonitors()->setSampling(s == CorrectionTool::ColorSelection::None      ? CorrectionMonitors::Sampling::None
                                                     : s == CorrectionTool::ColorSelection::Syringe || s == CorrectionTool::ColorSelection::RemoveColorCast ||
                                                             s == CorrectionTool::ColorSelection::MatchColor
                                                         ? CorrectionMonitors::Sampling::Drag
                                                                                                    : CorrectionMonitors::Sampling::Click);
    });
    connect(composer_->correctionMonitors(), &CorrectionMonitors::colorsSampled, correctionTool_, &CorrectionTool::applySamples);
    // The Levels histograms' pictures (FX Guide p.654): the frame at the position, small, without and with corrections.
    correctionTool_->setItemResolver([this](const QString& bin, const QString& id) { return composer_->resolve(bin, id); });
    // A template dropped on the mode's monitors: the template's relationships (FX Guide p.614-615).
    composer_->correctionMonitors()->setTemplateDrop([this](const QMimeData* mime, bool apply) {
        const auto t = timeline_->droppedCorrectionTemplate(mime);
        if (!t || !apply) return t.has_value();
        const bool ok = timeline_->applyCorrectionTemplate(*t, true);
        correctionTool_->reload();
        return ok;
    });
    // Save Correction: "A new effect template appears in the bin ... By default, Media Composer names the template using
    // the clip name of the segment" (FX Guide p.613). The active bin, else the first open one.
    connect(correctionTool_, &CorrectionTool::saveCorrectionRequested, this, [this](const model::CorrectionTemplate& t, const QString& name) {
        BinDocument* doc = binContainer_->activeDocument();
        if (!doc && !binContainer_->openDocuments().isEmpty()) doc = binContainer_->openDocuments().first();
        if (!doc || doc->isReadOnly()) {
            QMessageBox::information(this, tr("Save Correction"), tr("Open a bin to save the correction in."));
            return;
        }
        doc->addItem(model::correctionTemplateItem(t, name, QDateTime::currentDateTimeUtc()));
    });
    // The Color Correction effect from the Effect Palette makes the AutoCorrect tab's corrections in turn (FX Guide
    // p.695-696), each from the uncorrected frame as the tool's buttons are (6.10e1).
    // An OFX plug-in with optional inputs asks for its Number of Tracks when applied (FX Guide p.161).
    timeline_->setOptionalInputsChooser([this](const media::OfxPluginInfo& plugin) { return OfxOptionalInputsDialog::ask(plugin, this); });
    timeline_->setEffectSeed([this](model::Effect& effect) {
        if (model::isSafeColorLimiter(effect)) model::seedSafeColorLimiter(effect, safeColorLimits());
        // "Its values become the default settings for any new SubCap effect clip" (p.388; ours: at once).
        if (model::isSubCap(effect)) {
            const QString folder = subcapStylesheetFolder();
            const QString name = defaultSubcapStylesheet(folder);
            if (!name.isEmpty()) applySubcapStylesheet(QDir(folder).filePath(name + QStringLiteral(".xml")), effect);
        }
    });
    timeline_->setAutoCorrection([this](std::int64_t frame) {
        model::ColorCorrection c;
        static const std::pair<const char*, media::AutoCorrection> kinds[] = {
            {"hslAutoBalance", media::AutoCorrection::HslBalance},       {"hslAutoBlack", media::AutoCorrection::HslBlack},
            {"hslAutoContrast", media::AutoCorrection::HslContrast},     {"hslAutoWhite", media::AutoCorrection::HslWhite},
            {"curvesAutoBalance", media::AutoCorrection::CurvesBalance}, {"curvesAutoContrast", media::AutoCorrection::CurvesContrast}};
        composer_->setUncorrectedPictures(true);
        const QImage image = composer_->pictureAt(MonitorId::Record, frame, QSize(160, 90));
        composer_->setUncorrectedPictures(false);
        if (image.isNull()) return c;
        for (const char* key : {"autoCorrect1", "autoCorrect2", "autoCorrect3"}) {
            const QString value = settings_->text(QStringLiteral("Correction"), QLatin1String(key));
            for (const auto& [name, kind] : kinds) {
                if (value == QLatin1String(name)) media::autoCorrect(c, image, kind, true);
            }
        }
        return c;
    });
    correctionTool_->setPictureSource([this](bool corrected) {
        const model::MonitorState& r = composer_->state(MonitorId::Record);
        composer_->setUncorrectedPictures(!corrected);
        const QImage image = composer_->pictureAt(MonitorId::Record, r.position(), QSize(160, 90));
        composer_->setUncorrectedPictures(false);
        return image;
    });
    // The Motion Effect Editor (FX Guide p.178-189) and its toolbar (p.184).
    motionEditor_ = new MotionEffectEditor(*timeline_);
    panels_->addPanel(new Panel(QStringLiteral("MotionEffectEditor"), tr("Motion Effect Editor"), motionEditor_));
    connect(motionEditor_, &MotionEffectEditor::renderRequested, this, [this] { renderEffects(true); });
    connect(motionEditor_, &MotionEffectEditor::removeRequested, this, [this] { timeline_->perform(QStringLiteral("fx.remove_effect")); });
    connect(motionEditor_, &MotionEffectEditor::matchFrameRequested, this, [this] {
        if (QAction* a = commands_.action(QStringLiteral("other.match_frame"))) a->trigger();
    });
    connect(motionEditor_, &MotionEffectEditor::playLoopRequested, this,
            [this](std::int64_t start, std::int64_t end) { composer_->playLoop(MonitorId::Record, start, end); });
    connect(motionEditor_, &MotionEffectEditor::playRequested, this, [this] {
        composer_->setActiveMonitor(MonitorId::Record);
        composer_->play(1);
    });
    connect(motionEditor_, &MotionEffectEditor::trimRequested, this, [this] { timeline_->perform(QStringLiteral("trim.trim_mode")); });
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
    // Tools > Video Output Tool (7.3b; docs/spec/video-output.md): its values are the project's Video Output setting.
    videoOutput_ = new VideoOutputTool;
    panels_->addPanel(new Panel(QStringLiteral("VideoOutput"), tr("Video Output Tool"), videoOutput_));
    connect(videoOutput_, &VideoOutputTool::valuesChanged, this, [this](const QJsonObject& values) { storeVideoOutput(values, QString()); });
    connect(videoOutput_, &VideoOutputTool::saveAsRequested, this, [this](const QString& name) { storeVideoOutput(videoOutput_->values(), name); });
    // File > Output > Digital Cut (7.3a; docs/spec/digital-cut.md).
    digitalCut_ = new DigitalCutTool;
    panels_->addPanel(new Panel(QStringLiteral("DigitalCut"), tr("Digital Cut"), digitalCut_));
    connect(digitalCut_, &DigitalCutTool::previewRequested, this, [this] { digitalCut(true); });
    connect(digitalCut_, &DigitalCutTool::playRequested, this, [this] { digitalCut(false); });
    connect(digitalCut_, &DigitalCutTool::haltRequested, this, [this] { digitalCutHalted_ = true; });
    connect(digitalCut_, &DigitalCutTool::outputFormatChanged, this, [this] { updateDigitalCutDuration(); });
    connect(digitalCut_, &DigitalCutTool::checkDecksRequested, this, [this] {
        digitalCut_->setDecks(providers_.usable<ext::DeckProvider>());
    });
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
    // Update/Merge/Flatten/Remove Correction with Create New Sequence (FX Guide p.701-706): "a new sequence appears in
    // the bin with the title [OldTitle].Copy 1" (ours: the bin's .Copy.NN numbering); the merged one "appears in the
    // Timeline" (p.703), and so does each of them (ours).
    connect(timeline_, &Timeline::correctedSequenceCreated, this, [this](const QString& binId, const model::BinItem& sequence) {
        BinDocument* doc = binContainer_->document(binId);
        if (!doc || doc->isReadOnly()) return;
        const model::BinItem item = model::duplicateItem(doc->bin(), sequence, QDateTime::currentDateTimeUtc());
        doc->addItem(item);
        composer_->load(MonitorId::Record, binId, item);
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
    panels_->addPanel(new Panel(QStringLiteral("AudioSuite"), tr("Audio Processing"), audioSuite_));
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
        // Console only: how many files were deleted.
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
            reportFailure(this, tr("List Tool: load a sequence into the Record monitor first."));
            return;
        }
        listTool_->setSequence(loaded->item, clipResolver(loaded->item));
    });
    // A change list's old version, loaded the same way (p.954).
    connect(listTool_, &ListTool::loadOldRequested, this, [this] {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
        if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) {
            reportFailure(this, tr("List Tool: load the old sequence into the Record monitor first."));
            return;
        }
        listTool_->setOldSequence(loaded->item);
    });
    // The List Tool's errors and warnings are failures; "saved <file>" is information.
    connect(listTool_, &ListTool::message, this, [this](const QString& line) {
        if (line.startsWith(tr("List Tool: saved ")))
            ConsoleLog::instance().append(line); // Console only: where the list was saved.
        else
            reportFailure(this, line);
    });
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

// The video tracks a monitor shows, bottom to top: in the Record monitor those at and below the top
// monitored video track ("all tracks below the monitored track are active during playback", Editing
// Guide p.627), the unmonitored ones left out; every one in the Source monitor.
QStringList MainWindow::shownVideoTracks(MonitorId monitor, const model::Sequence& seq) const {
    QStringList video;
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Video) video << t.name();
    }
    if (monitor != MonitorId::Record || !timeline_) return video;
    int top = -1;
    for (int i = 0; i < video.size(); ++i) {
        if (timeline_->isMonitored(video.at(i))) top = i;
    }
    // "When there are multiple video tracks, all tracks below the monitored track are active during playback"
    // (p.627): the topmost monitored track and every one under it, whether or not Auto-Monitoring took the
    // monitor off them (p.629).
    // An expanded nest's rows are the nest's own (p.220): not tracks of their own.
    QStringList shown;
    for (int i = 0; i <= top; ++i) {
        if (!timeline_->isNestedRow(video.at(i))) shown << video.at(i);
    }
    return shown;
}

std::optional<std::pair<model::BinItem, std::int64_t>> MainWindow::frameAt(MonitorId monitor,
                                                                           std::int64_t position) const {
    // A clip's own frame; a sequence's from its top video track, in the
    // order Match Frame uses (p.432-434).
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (!loaded || position < 0) return std::nullopt;
    model::BinItem item = loaded->item;
    std::int64_t frame = position;
    // A multigroup shows as its sequence of groups (p.1125), a synced subclip as its sequence (sync_clips.h).
    if (const auto seq = model::multigroupSequence(item)) item = *seq;
    if (const auto seq = model::syncedSequence(item)) item = *seq;
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
        // A group clip's segment shows its angle (group_clip.h).
        if (item.kind == model::BinItemKind::Group) item = model::groupAngleClip(item, match->segment.groupAngle.value_or(-1));
    } else if (frame >= item.durationFrames) {
        return std::nullopt;
    } else if (item.kind == model::BinItemKind::Group) {
        // A group clip shows its selected angle (Full-Monitor display, p.1127).
        item = model::groupAngleClip(item, -1);
    }
    if (auto source = followMotion(item, frame)) return source;
    if (!media::isLinked(item)) return std::nullopt;
    return std::make_pair(item, frame);
}

// The angles of the shown bank in rows of two or three, the selected one outlined (ours: the guide's pictures show
// the split without saying how the selected angle is marked). Angles past the group's last are black.
QImage MainWindow::groupSplitImage(const model::BinItem& group, std::int64_t at, QSize size, bool* offline, std::optional<int> selected,
                                   const QList<std::int64_t>& offsets) const {
    const auto g = model::groupOf(group);
    if (!g || size.isEmpty()) return {};
    const int shown = selected.value_or(g->selected);
    const int split = composer_->groupSplit();
    const int side = split == 9 ? 3 : 2;
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    const QSize cell(size.width() / side, size.height() / side);
    bool anyOffline = false;
    // "Press the Ctrl key to activate the display of clip names in the multi-split displays" (p.1134).
    const bool names = QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier;
    for (int k = 0; k < split; ++k) {
        const QRect r((k % side) * cell.width(), (k / side) * cell.height(), cell.width(), cell.height());
        // A Multi-angle View menu's choice for this place (p.1134): another angle, or the sequence itself.
        const int angle = composer_->splitCells().value(k, composer_->groupBank() * split + k);
        if (angle == Composer::kSplitSequence) {
            const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
            if (rec) {
                const QImage frame = composer_->pictureAt(MonitorId::Record, composer_->state(MonitorId::Record).position(), cell);
                if (!frame.isNull()) p.drawImage(r, frame.scaled(cell, Qt::KeepAspectRatio));
            }
            continue;
        }
        if (angle < 0 || angle >= g->angles.size()) continue;

        const model::BinItem clip = model::groupAngleClip(group, angle, offsets);
        if (media::isLinked(clip)) {
            if (frames_->isOffline(clip)) {
                anyOffline = true;
            } else if (const QImage frame = frames_->frame(clip, at, cell); !frame.isNull()) {
                const QSize fitted = frame.size().scaled(cell, Qt::KeepAspectRatio);
                p.drawImage(QRect(r.topLeft() + QPoint((cell.width() - fitted.width()) / 2, (cell.height() - fitted.height()) / 2), fitted),
                            frame);
            }
        }
        if (angle == shown) {
            p.setPen(QPen(QColor(255, 210, 0), 3));
            p.drawRect(r.adjusted(1, 1, -2, -2));
        }
        if (names) {
            p.setPen(QColor(Qt::white));
            p.drawText(r.adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignLeft, g->angles[angle].clip.name);
        }
    }
    if (anyOffline && offline) *offline = true;
    return out;
}

// MultiCamera mode's Source monitor (p.1129): the group segment at the Record monitor's position, its angles split
// (the angle it shows outlined) as the Composer's split says; while playing with Split Mode Play set to Fullscreen,
// "the active camera angle, which displays in full-monitor size" (p.1160-1161). Black where there is no group segment.
std::optional<model::GroupSegmentAt> MainWindow::multiCamSegment() const {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || !timeline_) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QStringList tracks = timeline_->groupTracksAt();
    // Show Track in Split View When Playing: V1, or the monitored track (ours, the default) first.
    if (settings_->text(QStringLiteral("Composer"), QStringLiteral("splitTrack")) == QLatin1String("v1")) {
        tracks.removeAll(QStringLiteral("V1"));
        tracks.prepend(QStringLiteral("V1"));
    } else {
        const QStringList shown = shownVideoTracks(MonitorId::Record, seq);
        if (!shown.isEmpty()) {
            tracks.removeAll(shown.last());
            tracks.prepend(shown.last());
        }
    }
    return model::groupSegmentAt(seq, composer_->state(MonitorId::Record).position(), tracks);
}

QImage MainWindow::multiCamPicture(QSize size, bool* offline) const {
    const auto at = multiCamSegment();
    if (!at) return {};
    const auto group = composer_->resolve(at->segment.binId, at->segment.clipId);
    if (!group || !model::isGroupClip(*group)) return {};
    const bool full = composer_->groupSplit() <= 1 ||
                      (composer_->isPlaying() && settings_->text(QStringLiteral("Composer"), QStringLiteral("splitModePlay")) == QLatin1String("fullscreen"));
    if (full) {
        const model::BinItem clip = model::groupAngleClip(*group, at->segment.groupAngle.value_or(-1), at->segment.groupOffsets);
        if (!media::isLinked(clip)) return {};
        if (frames_->isOffline(clip)) {
            if (offline) *offline = true;
            return {};
        }
        return frames_->frame(clip, at->groupFrame, size);
    }
    return groupSplitImage(*group, at->groupFrame, size, offline, at->segment.groupAngle, at->segment.groupOffsets);
}

// A click in a split display (p.1131): "Click a camera angle in the split display during playback to switch to that
// camera angle"; the right button swaps banks (p.1132). In MultiCamera mode the angle goes to the sequence's group
// segment (cut there while playing); in Quad or Nine Split Source view it becomes the group's selected angle (ours).
bool MainWindow::splitClicked(MonitorId monitor, QPointF fraction, Qt::MouseButton button) {
    if (monitor != MonitorId::Source) return false;
    const bool multi = composer_->multiCameraMode();
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (!multi && !(loaded && (model::isGroupClip(loaded->item) || model::isMultigroup(loaded->item)) && composer_->groupSplit() > 1)) return false;
    if (button == Qt::RightButton) {
        if (!multi) return false;
        swapBank();
        return true;
    }
    if (button != Qt::LeftButton || composer_->groupSplit() <= 1) return false;
    const int side = composer_->groupSplit() == 9 ? 3 : 2;
    const int column = std::clamp(static_cast<int>(fraction.x() * side), 0, side - 1);
    const int row = std::clamp(static_cast<int>(fraction.y() * side), 0, side - 1);
    const int place = row * side + column;
    // "Ctrl+click the multi-split display where you want to show the new clip" (p.1134).
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        if (QMenu* menu = multiAngleMenu(place)) {
            menu->setAttribute(Qt::WA_DeleteOnClose);
            menu->popup(QCursor::pos());
        }
        return true;
    }
    const int angle = composer_->splitCells().value(place, composer_->groupBank() * composer_->groupSplit() + place);
    if (angle != Composer::kSplitSequence) chooseAngle(angle);
    return true;
}

// The group the Source monitor shows at its frame `at`: its group clip, or for a multigroup the group segment there on
// V1 (with the segment in *segment). Nothing for other clips.
std::optional<model::BinItem> MainWindow::sourceGroup(std::int64_t at, model::GroupSegmentAt* segment) const {
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (!loaded) return std::nullopt;
    if (model::isGroupClip(loaded->item)) return loaded->item;
    const auto seq = model::multigroupSequence(loaded->item);
    if (!seq) return std::nullopt;
    const auto found = model::groupSegmentAt(model::Sequence::fromItem(*seq), at, {QStringLiteral("V1")});
    if (!found) return std::nullopt;
    if (segment) *segment = *found;
    const auto clip = composer_->resolve(found->segment.binId, found->segment.clipId);
    return clip && model::isGroupClip(*clip) ? clip : std::nullopt;
}

// An angle chosen for the Source monitor's multigroup: its group segment at the position (V1, and A1 with Audio Follow
// Video) switches, kept in the multigroup in its bin (ours: the guide does not say how a multigroup's angles switch on
// the source side).
bool MainWindow::switchMultigroupAngle(int angle, bool step) {
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (!loaded || !model::isMultigroup(loaded->item)) return false;
    model::GroupSegmentAt at;
    const auto group = sourceGroup(composer_->state(MonitorId::Source).position(), &at);
    const auto g = group ? model::groupOf(*group) : std::nullopt;
    if (!g || g->angles.isEmpty()) return false;
    if (step) angle = model::stepAngle(at.segment.groupAngle.value_or(0), angle, static_cast<int>(g->angles.size()));
    model::Sequence seq = model::Sequence::fromItem(*model::multigroupSequence(loaded->item));
    const std::int64_t pos = composer_->state(MonitorId::Source).position();
    if (!model::setGroupAngleAt(seq, QStringLiteral("V1"), pos, angle, false, *g)) return false;
    if (composer_->audioFollowVideo()) model::setGroupAngleAt(seq, QStringLiteral("A1"), pos, angle, false, *g);
    model::BinItem item = loaded->item;
    model::setMultigroupSequence(item, seq);
    storeClip(loaded->binId, item);
    composer_->refreshDisplay();
    return true;
}

// Clip > MultiGroup (p.1125): the bin's selected clips, with the Group Clips options (no Waveform Analysis); the groups and
// the multigroup go into the bin, the multigroup selected.
std::optional<QString> MainWindow::multiGroup() {
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return std::nullopt;
    const QString binId = view->document().nodeId();
    QList<std::pair<QString, model::BinItem>> clips;
    for (const model::BinItem& item : view->document().bin().items) {
        if (view->selectedIds().contains(item.id)) clips.append({binId, item});
    }
    if (clips.isEmpty()) return std::nullopt;
    model::GroupOptions options;
    options.when = QDateTime::currentDateTimeUtc();
    if (groupClipsHook_) {
        const auto chosen = groupClipsHook_();
        if (!chosen) return std::nullopt;
        options.sync = chosen->first;
        options.auxTc = chosen->second;
    } else {
        const int fps = project_ ? TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps : 30;
        GroupClipsDialog dialog(fps == 24 || fps == 25, this, true);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;
        options.sync = dialog.sync();
        options.auxTc = dialog.auxTc();
    }
    options.marks = [this](const model::BinItem& clip, bool in) -> std::optional<std::int64_t> {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Source).loaded(clip.id);
        if (!loaded) return std::nullopt;
        return in ? loaded->in : loaded->out;
    };
    QString error;
    const auto made = model::multiGroup(view->document().bin(), binId, clips, options, &error);
    if (!made) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("Group of Groups: %1").arg(error));
        if (!groupClipsHook_) QMessageBox::warning(this, tr("Group of Groups"), error);
        return std::nullopt;
    }
    for (const model::BinItem& g : made->groups) view->document().addItem(g);
    view->document().addItem(made->multigroup);
    if (made->skipped > 0) reportFailure(this, tr("Group of Groups: %n clip(s) with no other clip of the same take were left out.", "", made->skipped));
    view->selectIds({made->multigroup.id});
    return made->multigroup.id;
}

// The Multi-angle View menu of a place in the split display (p.1134): "The clips in the group are listed", and "You can
// also select Sequence from the Multi-angle View menus to display the entire sequence".
QMenu* MainWindow::multiAngleMenu(int place) {
    std::optional<model::GroupClip> group;
    if (composer_->multiCameraMode()) {
        const auto at = multiCamSegment();
        const auto clip = at ? composer_->resolve(at->segment.binId, at->segment.clipId) : std::nullopt;
        if (clip) group = model::groupOf(*clip);
    } else if (const auto shown = sourceGroup(composer_->state(MonitorId::Source).position())) {
        group = model::groupOf(*shown);
    }
    if (!group) return nullptr;
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("multiAngleViewMenu"));
    for (int i = 0; i < group->angles.size(); ++i) {
        connect(menu->addAction(group->angles[i].clip.name), &QAction::triggered, this, [this, place, i] { composer_->setSplitCell(place, i); });
    }
    menu->addSeparator();
    QAction* sequence = menu->addAction(tr("Sequence"));
    sequence->setEnabled(composer_->state(MonitorId::Record).current() != nullptr);
    connect(sequence, &QAction::triggered, this, [this, place] { composer_->setSplitCell(place, Composer::kSplitSequence); });
    return menu;
}

// Commit Multicam Edits (p.1135): "Media Composer duplicates the sequence, and then replaces each grouped clip in the
// duplicate sequence with its selected clip. The original sequence is unaffected". The duplicates go into the same bin.
QStringList MainWindow::commitMulticamEdits() {
    QStringList made;
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return made;
    const model::GroupClipLookup lookup = [this](const QString& bin, const QString& clip) { return composer_->resolve(bin, clip); };
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* found = view->document().bin().item(id);
        if (!found || found->kind != model::BinItemKind::Sequence) continue;
        model::Sequence seq = model::Sequence::fromItem(*found);
        if (model::commitMulticamEdits(seq, lookup) == 0) continue;
        model::BinItem copy = model::duplicateItem(view->document().bin(), *found, QDateTime::currentDateTimeUtc());
        seq.storeIn(copy);
        view->document().addItem(copy);
        made << copy.id;
    }
    if (!made.isEmpty()) view->selectIds(made);
    return made;
}

void MainWindow::swapBank() {
    if (!composer_->multiCameraMode()) {
        const auto shown = sourceGroup(composer_->state(MonitorId::Source).position());
        const auto g = shown ? model::groupOf(*shown) : std::nullopt;
        composer_->swapCamBank(g ? static_cast<int>(g->angles.size()) : -1);
        return;
    }
    const auto at = multiCamSegment();
    const auto group = at ? composer_->resolve(at->segment.binId, at->segment.clipId) : std::nullopt;
    const auto g = group ? model::groupOf(*group) : std::nullopt;
    if (g) composer_->swapCamBank(static_cast<int>(g->angles.size()));
}

// The Group menu's contents (p.1133): in MultiCamera mode the group segment the mode works on in the sequence, each
// section showing the angle of the group's segment at the position on the sequence track of that name; in the Source
// monitor the group (its GroupClip::trackAngles) or, for a multigroup, its group segments there.
std::optional<Composer::GroupMenuInfo> MainWindow::groupMenuInfo() const {
    std::optional<model::GroupClip> group;
    std::optional<model::Sequence> seq; // where the shown angles are segments
    std::int64_t pos = 0;
    QString clipId;
    if (composer_->multiCameraMode()) {
        const auto at = multiCamSegment();
        const auto clip = at ? composer_->resolve(at->segment.binId, at->segment.clipId) : std::nullopt;
        if (!clip) return std::nullopt;
        group = model::groupOf(*clip);
        seq = model::Sequence::fromItem(composer_->state(MonitorId::Record).current()->item);
        pos = composer_->state(MonitorId::Record).position();
        clipId = clip->id;
    } else {
        pos = composer_->state(MonitorId::Source).position();
        const auto clip = sourceGroup(pos);
        if (!clip) return std::nullopt;
        group = model::groupOf(*clip);
        if (const auto mg = model::multigroupSequence(composer_->state(MonitorId::Source).current()->item)) seq = model::Sequence::fromItem(*mg);
        clipId = clip->id;
    }
    if (!group) return std::nullopt;
    Composer::GroupMenuInfo info;
    for (const model::GroupAngle& a : group->angles) info.angles << a.clip.name;
    for (const QString& track : model::groupTracks(*group)) {
        Composer::GroupMenuSection section;
        section.track = track;
        section.angles = model::anglesWithTrack(*group, track);
        if (seq) {
            // The picture's section follows the segment the mode or the multigroup shows (on V1 or another video track).
            const QStringList on = track == QLatin1String("V1") && composer_->multiCameraMode() ? timeline_->groupTracksAt() : QStringList{track};
            const auto at = model::groupSegmentAt(*seq, pos, on);
            if (at && at->segment.clipId == clipId) section.shown = at->segment.groupAngle.value_or(-1);
        } else {
            section.shown = model::groupTrackAngle(*group, track);
        }
        info.sections << section;
    }
    return info;
}

// A Group menu choice (p.1133): V1's is the camera angle (chooseAngle); an audio track's patches that angle's track:
// the group segment on that track switches (MultiCamera mode, a multigroup), or the group keeps it in
// GroupClip::trackAngles for its next edits and its sound in the Source monitor.
bool MainWindow::chooseGroupTrackAngle(const QString& track, int angle) {
    const auto kind = model::parseTrackName(track);
    if (!kind || kind->kind != model::TrackKind::Audio) return chooseAngle(angle);
    if (composer_->multiCameraMode()) return timeline_ && timeline_->setGroupTrackAngle(track, angle);
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (!loaded) return false;
    if (model::isMultigroup(loaded->item)) {
        const std::int64_t pos = composer_->state(MonitorId::Source).position();
        model::Sequence seq = model::Sequence::fromItem(*model::multigroupSequence(loaded->item));
        const auto at = model::groupSegmentAt(seq, pos, {track});
        const auto clip = at ? composer_->resolve(at->segment.binId, at->segment.clipId) : std::nullopt;
        const auto g = clip ? model::groupOf(*clip) : std::nullopt;
        if (!g || !model::setGroupAngleAt(seq, track, pos, angle, false, *g)) return false;
        model::BinItem item = loaded->item;
        model::setMultigroupSequence(item, seq);
        storeClip(loaded->binId, item);
        composer_->refreshDisplay();
        return true;
    }
    auto group = model::groupOf(loaded->item);
    if (!group || angle < 0 || angle >= group->angles.size() || model::groupTrackAngle(*group, track) == angle) return false;
    group->trackAngles.insert(track, angle);
    model::BinItem item = loaded->item;
    model::setGroup(item, *group);
    storeClip(loaded->binId, item);
    composer_->refreshDisplay();
    return true;
}

bool MainWindow::chooseAngle(int angle) {
    if (composer_->multiCameraMode()) return timeline_ && timeline_->setGroupAngle(angle, composer_->isPlaying());
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (loaded && model::isMultigroup(loaded->item)) return switchMultigroupAngle(angle, false);
    auto group = loaded ? model::groupOf(loaded->item) : std::nullopt;
    if (!group || angle < 0 || angle >= group->angles.size() || group->selected == angle) return false;
    group->selected = angle;
    model::BinItem item = loaded->item;
    model::setGroup(item, *group);
    storeClip(loaded->binId, item);
    composer_->refreshDisplay();
    return true;
}

// The numeric keypad (p.1131-1132): 1-9 choose the angle at that place in the split display; 0 and . swap banks. Only
// while a split display of a group is shown (MultiCamera mode, or Quad or Nine Split Source view).
bool MainWindow::multiCamKey(int key) {
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    const bool split = composer_->groupSplit() > 1;
    if (!composer_->multiCameraMode() && !(split && loaded && (model::isGroupClip(loaded->item) || model::isMultigroup(loaded->item)))) return false;
    if (key == Qt::Key_0 || key == Qt::Key_Period) {
        swapBank();
        return true;
    }
    if (key < Qt::Key_1 || key > Qt::Key_9) return false;
    const int splitCount = split ? composer_->groupSplit() : 4;
    const auto place = model::keypadSplitPosition(key - Qt::Key_0, splitCount);
    if (!place) return false;
    chooseAngle(composer_->groupBank() * splitCount + *place);
    return true;
}

// Previous In Group and Next In Group (p.1131): "The angle selection switches in either the Source monitor (source
// material) or in the Record monitor (sequence material), whichever is active." In the Source monitor the group's
// selected angle changes, in its bin too ("you also switch the frame representing the group in the bin"); in the
// Record monitor the group segment at the position does.
bool MainWindow::stepInGroup(int step) {
    if (composer_->activeMonitor() == MonitorId::Record) return timeline_ && timeline_->stepGroupAngle(step);
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    if (!loaded) return false;
    if (model::isMultigroup(loaded->item)) return switchMultigroupAngle(step, true);
    auto group = model::groupOf(loaded->item);
    if (!group || group->angles.isEmpty()) return false;
    group->selected = model::stepAngle(group->selected, step, static_cast<int>(group->angles.size()));
    model::BinItem item = loaded->item;
    model::setGroup(item, *group);
    storeClip(loaded->binId, item);
    composer_->refreshDisplay();
    return true;
}

// Clip > Group Clips (p.1124): the bin's selected clips, in the bin's order, synced by the dialog's option; the IN
// and OUT points are the Source monitor's marks of each clip where it has been loaded (ours), else the bin's Mark IN
// and Mark OUT. The group goes into the same bin, selected.
std::optional<QString> MainWindow::groupClips() {
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return std::nullopt;
    const QString binId = view->document().nodeId();
    QList<std::pair<QString, model::BinItem>> clips;
    for (const model::BinItem& item : view->document().bin().items) {
        if (view->selectedIds().contains(item.id)) clips.append({binId, item});
    }
    if (clips.isEmpty()) return std::nullopt;
    model::GroupOptions options;
    options.when = QDateTime::currentDateTimeUtc();
    if (groupClipsHook_) {
        const auto chosen = groupClipsHook_();
        if (!chosen) return std::nullopt;
        options.sync = chosen->first;
        options.auxTc = chosen->second;
    } else {
        const int fps = project_ ? TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps : 30;
        GroupClipsDialog dialog(fps == 24 || fps == 25, this);
        if (dialog.exec() != QDialog::Accepted) return std::nullopt;
        options.sync = dialog.sync();
        options.auxTc = dialog.auxTc();
    }
    // Waveform Analysis: each clip's frame that sounds with the first clip's frame 0.
    const model::BinItem first = clips.first().second;
    options.waveform = [first](const model::BinItem& clip) -> std::optional<std::int64_t> {
        if (clip.id == first.id) return 0;
        return media::waveformOffset(first, clip);
    };
    options.marks = [this](const model::BinItem& clip, bool in) -> std::optional<std::int64_t> {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Source).loaded(clip.id);
        if (!loaded) return std::nullopt;
        return in ? loaded->in : loaded->out;
    };
    QString error;
    const auto group = model::groupClips(view->document().bin(), clips, options, &error);
    if (!group) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("Group Clips: %1").arg(error));
        if (!groupClipsHook_) QMessageBox::warning(this, tr("Group Clips"), error);
        return std::nullopt;
    }
    view->document().addItem(*group);
    view->selectIds({group->id});
    return group->id;
}

QStringList MainWindow::syncClips() {
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return {};
    const QString binId = view->document().nodeId();
    QList<model::BinItem> clips;
    for (const model::BinItem& item : view->document().bin().items) {
        if (view->selectedIds().contains(item.id)) clips << item;
    }
    // "Highlight two or more clips in the bin" (p.497).
    if (clips.size() < 2) {
        const QString why = tr("Select a video clip and an audio clip in a bin to sync them.");
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("Sync Clips: %1").arg(why));
        if (!syncClipsHook_) QMessageBox::information(this, tr("Sync Clips"), why);
        return {};
    }
    model::SyncClipsOptions options;
    if (syncClipsHook_) {
        const auto chosen = syncClipsHook_();
        if (!chosen) return {};
        options = *chosen;
    } else {
        const int fps = project_ ? TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps : 30;
        SyncClipsDialog dialog(fps == 24 || fps == 25, this);
        dialog.setOptions(lastSyncOptions_);
        if (dialog.exec() != QDialog::Accepted) return {};
        options = dialog.options();
        lastSyncOptions_ = options;
    }
    // Waveform Analysis: each clip's frame that sounds with the first video clip's frame 0 (as Group Clips does).
    model::BinItem reference = clips.first();
    for (const model::BinItem& c : clips) {
        if (model::sourceTracksOf(c).contains(QStringLiteral("V1"))) {
            reference = c;
            break;
        }
    }
    options.sync.waveform = [reference](const model::BinItem& clip) -> std::optional<std::int64_t> {
        if (clip.id == reference.id) return 0;
        return media::waveformOffset(reference, clip);
    };
    // In and Out points: the Source monitor's marks on a clip loaded there, else the bin's Mark IN and Mark OUT.
    options.sync.marks = [this](const model::BinItem& clip, bool in) -> std::optional<std::int64_t> {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Source).loaded(clip.id);
        if (!loaded) return std::nullopt;
        return in ? loaded->in : loaded->out;
    };
    const model::SyncClipsResult result = model::syncClips(view->document().bin(), binId, clips, options);
    if (!result.ok()) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("Sync Clips: %1").arg(result.error));
        if (!syncClipsHook_) QMessageBox::warning(this, tr("Sync Clips"), result.error);
        return {};
    }
    QStringList ids;
    for (const model::BinItem& subclip : result.subclips) {
        view->document().addItem(subclip);
        ids << subclip.id;
    }
    view->selectIds(ids);
    return ids;
}

// Clip > Edit Group (p.1125): the selected group clip's clips as a sequence in the group's bin, named "<group>.Edit"
// (ours), in the Record monitor. "Edits are not actually made to the group clip until you specifically choose to
// update the group."
std::optional<QString> MainWindow::editGroup() {
    BinView* view = binContainer_->activeView();
    if (!view || view->document().isReadOnly()) return std::nullopt;
    const model::BinItem* group = nullptr;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* found = view->document().bin().item(id); found && model::isGroupClip(*found)) group = found;
    }
    if (!group) return std::nullopt;
    const QString binId = view->document().nodeId();
    const model::Sequence seq = model::groupEditSequence(binId, *group);
    model::BinItem item =
        model::newSequence(view->document().bin(), group->editRate, QDateTime::currentDateTimeUtc(), group->fields.value(QStringLiteral("Start")));
    QStringList names;
    for (const model::BinItem& i : view->document().bin().items) names << i.name;
    item.name = group->name + QStringLiteral(".Edit");
    for (int n = 2; names.contains(item.name); ++n) item.name = QStringLiteral("%1.Edit.%2").arg(group->name).arg(n);
    seq.storeIn(item);
    view->document().addItem(item);
    composer_->load(MonitorId::Record, binId, item);
    return item.id;
}

namespace {

// A segment's clip as the monitors find it, for model::groupFromSequence and refreshGroupEdits.
model::GroupClipLookup lookupOf(const Composer* composer) {
    return [composer](const QString& bin, const QString& clip) { return composer->resolve(bin, clip); };
}

} // namespace

// Update Group (p.1126): "right click in the Record Monitor and select Update Group. A dialog asks if you want to update
// the group clip or create a new one." The Record monitor's sequence must come from Edit Group. Updating keeps the
// group's name and selected angle (as far as it still has it) and its length at least ("You cannot make the group clip
// shorter"); a new group goes into the same bin.
std::optional<QString> MainWindow::updateGroup() {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList edited = seq.editedGroup();
    BinDocument* doc = edited.size() == 2 ? binContainer_->document(edited[0]) : nullptr;
    const model::BinItem* group = doc ? doc->bin().item(edited[1]) : nullptr;
    if (!group || !model::isGroupClip(*group)) {
        reportFailure(this, tr("Update Group: the sequence in the Record monitor was not made by Edit Group."));
        return std::nullopt;
    }
    int choice = 0;
    if (updateGroupHook_) {
        choice = updateGroupHook_();
    } else {
        QMessageBox box(QMessageBox::Question, tr("Update Group"), tr("Update the group clip \"%1\" or create a new one?").arg(group->name),
                        QMessageBox::Cancel, this);
        QPushButton* update = box.addButton(tr("Update"), QMessageBox::AcceptRole);
        QPushButton* create = box.addButton(tr("Create New"), QMessageBox::AcceptRole);
        box.exec();
        choice = box.clickedButton() == update ? 0 : box.clickedButton() == create ? 1 : -1;
    }
    if (choice < 0 || doc->isReadOnly()) return std::nullopt;
    QString error;
    const auto made = model::groupFromSequence(seq, group->editRate, lookupOf(composer_), choice == 0 ? group->durationFrames : 0, &error);
    if (!made) {
        reportFailure(this, tr("Update Group: %1").arg(error));
        return std::nullopt;
    }
    model::GroupClip angles = made->first;
    if (choice == 0) {
        model::BinItem item = *group;
        // The selected angle stays the same clip where the group still has it.
        const QString selected = model::groupOf(item)->angles.value(model::groupOf(item)->selected).clipId;
        angles.selected = 0;
        for (int i = 0; i < angles.angles.size(); ++i) {
            if (angles.angles[i].clipId == selected) angles.selected = i;
        }
        model::storeGroup(item, angles, made->second);
        item.modified = QDateTime::currentDateTimeUtc();
        storeClip(doc->nodeId(), item);
        return item.id;
    }
    model::BinItem item;
    item.id = model::newItemId();
    item.kind = model::BinItemKind::Group;
    item.name = model::groupName(doc->bin(), angles.angles.first().clip.name);
    item.created = item.modified = QDateTime::currentDateTimeUtc();
    item.editRate = group->editRate;
    model::storeGroup(item, angles, made->second);
    doc->addItem(item);
    return item.id;
}

// Create Group (p.1126): "Load the sequence in the Timeline. Right click in the Record Monitor and choose Create Group.
// A group is created and added to your bin" (the sequence's bin).
std::optional<QString> MainWindow::createGroupFromSequence() {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    BinDocument* doc = binContainer_->document(rec->binId);
    if (!doc || doc->isReadOnly()) return std::nullopt;
    QString error;
    const auto made = model::groupFromSequence(model::Sequence::fromItem(rec->item), rec->item.editRate, lookupOf(composer_), 0, &error);
    if (!made) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("Create Group: %1").arg(error));
        if (!updateGroupHook_) QMessageBox::warning(this, tr("Create Group"), error);
        return std::nullopt;
    }
    model::BinItem item;
    item.id = model::newItemId();
    item.kind = model::BinItemKind::Group;
    item.name = model::groupName(doc->bin(), made->first.angles.first().clip.name);
    item.created = item.modified = QDateTime::currentDateTimeUtc();
    item.editRate = rec->item.editRate;
    model::storeGroup(item, made->first, made->second);
    doc->addItem(item);
    return item.id;
}

// Clip > Refresh Sequence > Group Clips (p.478), on the bin's selected sequences when a bin is active, else on the
// Record monitor's sequence as an undo step, as Motion Adapters/Timewarps does.
int MainWindow::refreshGroupEdits() {
    const model::GroupClipLookup lookup = lookupOf(composer_);
    int changed = 0;
    BinView* view = binContainer_->activeView();
    const QWidget* focus = QApplication::focusWidget();
    if (view && focus && (view == focus || view->isAncestorOf(focus))) {
        for (const QString& id : view->selectedIds()) {
            const model::BinItem* found = view->document().bin().item(id);
            if (!found || found->kind != model::BinItemKind::Sequence) continue;
            model::BinItem item = *found;
            model::Sequence seq = model::Sequence::fromItem(item);
            const int n = model::refreshGroupEdits(seq, lookup);
            if (n == 0) continue;
            seq.storeIn(item);
            item.modified = QDateTime::currentDateTimeUtc();
            storeClip(view->document().nodeId(), item);
            changed += n;
        }
        return changed;
    }
    if (!composer_->state(MonitorId::Record).current()) return 0;
    timeline_->changeRecordSequence(tr("Refresh Sequence"), [&](model::Sequence& seq) {
        changed = model::refreshGroupEdits(seq, lookup);
        return changed > 0;
    });
    return changed;
}

std::optional<std::pair<model::BinItem, std::int64_t>> MainWindow::followMotion(const model::BinItem& clip,
                                                                               std::int64_t frame) const {
    // A rendered motion effect plays its own media; an unrendered one its source clip's frames.
    if (media::isLinked(clip)) return std::nullopt;
    const auto motion = model::motionEffectOf(clip);
    if (!motion) return std::nullopt;
    auto source = composer_->resolve(motion->sourceBin, motion->sourceClip);
    if (!source || !media::isLinked(*source)) return std::nullopt;
    const std::int64_t last = std::max<std::int64_t>(source->durationFrames - 1, 0);
    return std::make_pair(*source, std::clamp<std::int64_t>(motion->sourceFrame(frame), 0, last));
}

// "Render 2-Field Motion Effect Using" (p.563): Duplicated Field, Both Fields, Interpolated Field or VTR-Style,
// for two-field media (a source clip whose Field Motion is Interlaced); the frames at the effect's speed,
// held by its strobe, the output the project's scan.
std::optional<QImage> MainWindow::motionFieldFrame(const model::BinItem& clip, std::int64_t frame, QSize size) const {
    if (media::isLinked(clip)) return std::nullopt;
    const auto motion = model::motionEffectOf(clip);
    if (!motion || !project_) return std::nullopt;
    const bool interlaced = project_->settings.format.scan == ScanMode::Interlaced;
    const auto source = composer_->resolve(motion->sourceBin, motion->sourceClip);
    if (!source || !media::isLinked(*source) || model::clipFieldMotion(*source, interlaced) != model::FieldMotionAttribute::Interlaced) {
        return std::nullopt;
    }
    media::FieldConversion conv;
    conv.source = media::SourceFormat::Interlaced;
    conv.output = interlaced ? media::OutputFormat::Interlaced : media::OutputFormat::Progressive;
    switch (motion->field) {
    case model::FieldMotion::DuplicatedField: conv.rendering = media::FieldRendering::DuplicatedField; break;
    case model::FieldMotion::BothFields: conv.rendering = media::FieldRendering::BothFields; break;
    case model::FieldMotion::InterpolatedField: conv.rendering = media::FieldRendering::InterpolatedField; break;
    case model::FieldMotion::VtrStyle: conv.rendering = media::FieldRendering::VtrStyle; break;
    }
    conv.topFirst = source->link.value(QStringLiteral("fieldOrder")).toString() != QLatin1String("bottom");
    const std::int64_t last = std::max<std::int64_t>(source->durationFrames - 1, 0);
    const media::FrameGetter frameAt = [&](std::int64_t n) {
        return n < 0 || n > last ? QImage() : frames_->frame(*source, n, size);
    };
    const model::MotionEffect& m = *motion;
    const auto positionAt = [&m](double t) {
        // The strobe holds whole pictures, both fields (MotionEffect::sourceFrame).
        const double held = m.strobe > 1 ? std::floor(std::floor(t) / m.strobe) * m.strobe : t;
        return static_cast<double>(m.sourceStart) + held * m.speed;
    };
    const QImage image = media::convertedFrame(frameAt, positionAt, static_cast<double>(frame), conv);
    if (image.isNull()) return std::nullopt;
    return image;
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
    // The Audio Mixer's Master fader, a gain on the whole mix (`scale` for automated pieces), and the Audio Project
    // setting's Output Gain, "the volume of global audio output" (p.920, p.1150).
    const double master = (audioMixer_ ? audioMixer_->masterGain() : 0.0) + outputGainDb();
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
        // The Full Screen Playback setting's S3D View and S3D Overlay (p.1190-1191; 8.6d2).
        view->setPictureSource([this](bool* offline) {
            const QString kind = QStringLiteral("Full Screen Playback");
            s3dOverride_ = std::pair{settings_->text(kind, QStringLiteral("s3dView")), settings_->text(kind, QStringLiteral("s3dOverlay"))};
            const QImage image = composer_->picture(composer_->activeMonitor(), QSize(), offline);
            s3dOverride_.reset();
            return image;
        });
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
        reportFailure(this, tr("Playback: no audio output could be opened; playing without sound."));
    }
}

// A clip's frame through its colour adapter (8.5c; media::applyColorTransformations) and spatial adapter (8.5a;
// media::spatialAdaptedImage): the colours transformed, then the whole frame cut and fit into the project frame at `want`.
// Without a spatial adapter, or a project, the frame as the cache scales it.
// The project's Stereoscopic option (p.65-66; 8.6d1): one eye of a stereo clip, or both eyes frame compatible, every
// 2D clip giving the same picture to both.
QImage MainWindow::adaptedFrame(const model::BinItem& clip, std::int64_t frame, QSize want,
                                const std::optional<model::SpatialAdapter>& spatial,
                                const std::optional<model::ColorEncoding>& color) const {
    // An S3D View composing one eye (stereoPicture()).
    if (eyeOverride_) {
        const model::Eye eye = *eyeOverride_ == model::S3DView::MonoEye::Left    ? model::Eye::Left
                               : *eyeOverride_ == model::S3DView::MonoEye::Right ? model::Eye::Right
                                                                                : model::shownEye(model::StereoMode::LeadingEye, clip);
        return monoFrame(model::eyeClip(clip, eye), frame, want, spatial, color);
    }
    const model::StereoMode mode = project_ ? model::stereoModeOf(project_->settings.stereoscopic) : model::StereoMode::Off;
    if (model::isFrameCompatible(mode)) {
        const QImage left = monoFrame(model::eyeClip(clip, model::Eye::Left), frame, want, spatial, color);
        const QImage right = model::isStereoClip(clip) ? monoFrame(model::eyeClip(clip, model::Eye::Right), frame, want, spatial, color)
                                                       : left;
        return media::frameCompatible(left, right, mode == model::StereoMode::SideBySide, left.size());
    }
    return monoFrame(model::eyeClip(clip, model::shownEye(mode, clip)), frame, want, spatial, color);
}

QImage MainWindow::monoFrame(const model::BinItem& source, std::int64_t frame, QSize want,
                             const std::optional<model::SpatialAdapter>& spatial,
                             const std::optional<model::ColorEncoding>& color) const {
    // The Play button's proxy mode (p.509; 8.7a): the clip's proxy in place of its own media.
    const model::BinItem clip = playedClip(source);
    QStringList transformations = color ? color->applied() : QStringList();
    // In ACES projects the media is ACES 2065 after its adapter, then the working space ACEScct (p.558).
    if (project_ && project_->settings.colorSpace.contains(QLatin1String("ACES"))) {
        transformations = media::withAcesWorkingSpace(transformations);
    }
    if (!spatial || !project_) return media::applyColorTransformations(frames_->frame(clip, frame, want), transformations);
    const QImage full = media::applyColorTransformations(frames_->frame(clip, frame, QSize()), transformations);
    const QSize raster(project_->settings.format.width, project_->settings.format.height);
    return media::spatialAdaptedImage(full, model::withPixelAspect(*spatial, clip), raster, want.isValid() ? want : raster);
}

// Source Settings (8.5b1; p.527-535): the clips selected in the active bin, or the Source monitor's clip, in the dialog;
// Apply sets each one's Reformat attribute and FrameFlex box ("Click Apply to propogate the settings to all clips that you
// selected in the bin", p.540) and updates the monitors.
bool MainWindow::sourceSettings() {
    QList<QPair<QString, QString>> chosen;
    auto isClip = [](const model::BinItem& i) {
        return i.kind == model::BinItemKind::MasterClip || i.kind == model::BinItemKind::Subclip;
    };
    if (BinView* view = binContainer_->activeView()) {
        for (const QString& id : view->selectedIds()) {
            const model::BinItem* item = view->document().bin().item(id);
            if (item && isClip(*item)) chosen.append({view->document().nodeId(), id});
        }
    }
    if (chosen.isEmpty()) {
        const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
        if (!loaded || !isClip(loaded->item)) return false;
        chosen.append({loaded->binId, model::isAdapted(loaded->item) ? model::unadaptedClip(loaded->item).id : loaded->item.id});
    }
    return sourceSettingsFor(chosen);
}

// The Timeline's right-click Source Settings (p.542, p.545, "On the Timeline, or in the bin, select the clip ... right-click and
// choose Source Settings"): the segment's clip.
bool MainWindow::sourceSettingsForSegment(const model::SegmentRef& ref) {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return false;
    const auto seg = model::segmentFor(model::Sequence::fromItem(rec->item), ref);
    if (!seg || seg->isFiller() || seg->clipId.isEmpty()) return false;
    return sourceSettingsFor({{seg->binId, seg->clipId}});
}

bool MainWindow::sourceSettingsFor(const QList<QPair<QString, QString>>& chosen) {
    if (!project_ || chosen.isEmpty()) return false;
    const auto found = composer_->resolve(chosen.first().first, chosen.first().second);
    if (!found || (found->kind != model::BinItemKind::MasterClip && found->kind != model::BinItemKind::Subclip)) return false;
    const model::BinItem first = *found;
    const QImage frame = media::isLinked(first) && !frames_->isOffline(first) ? frames_->frame(first, 0, QSize()) : QImage();
    const QSize raster(project_->settings.format.width, project_->settings.format.height);
    const model::SpatialAdapter adapter = model::spatialAdapterOf(first).value_or(model::SpatialAdapter{});
    SourceSettingsDialog dialog(first.name, frame.isNull() ? QImage(raster, QImage::Format_RGB32) : frame, raster, adapter,
                                static_cast<int>(chosen.size()), this);
    // Playback Rate (p.545): the clip's own rate and the project's, for a clip whose rates differ.
    {
        const Rational projectRate = project_->settings.format.editRate;
        const auto link = media::linkOf(first);
        const bool conformed = media::playsAtProjectRate(first);
        const Rational own = link ? link->rate : first.editRate;
        model::BinItem probe = first;
        QString why;
        const bool differs = link && !(own == projectRate);
        const bool allowed = differs && media::setPlaybackAtProjectRate(probe, !conformed, &why);
        if (!differs) why = tr("The clip plays at the project's rate.");
        dialog.setPlaybackRates(QString::fromStdString(frameRateText(own)) + tr(" fps"),
                                QString::fromStdString(frameRateText(projectRate)) + tr(" fps"), conformed, allowed, why);
    }
    // Color Encoding (p.537-540): the first clip's.
    dialog.setColorEncoding(model::colorEncodingOf(first).value_or(model::ColorEncoding{}));
    // "you can select the CDL option from the Color Transformation list" (p.543): the clip's ASC_SOP and ASC_SAT.
    dialog.setClipCdl(media::cdlTransformationName(first.fields.value(QStringLiteral("ASC_SOP")),
                                                   first.fields.value(QStringLiteral("ASC_SAT"))),
                      settings_ && settings_->flag(QStringLiteral("Color Management"), QStringLiteral("useCdl")));
    dialog.setColorManagementOpener([this] { openColorManagementSetting(); });
    dialog.setAcesProject(project_ && project_->settings.colorSpace.contains(QLatin1String("ACES")));
    dialog.setApplier([this, chosen](const model::SpatialAdapter& a, bool atProjectRate, const model::ColorEncoding& color) {
        for (const auto& [bin, id] : chosen) {
            BinDocument* doc = binContainer_->document(bin);
            const model::BinItem* stored = doc ? doc->bin().item(id) : nullptr;
            if (!stored) continue;
            model::BinItem item = *stored;
            item.fields.insert(QLatin1String(model::kReformatHeading), model::reformatText(a.reformat));
            model::setFrameFlex(item, a.frameFlex);
            // "Click Apply to propogate the settings to all clips that you selected in the bin" (p.540).
            model::setColorEncoding(item, color);
            QString why;
            if (media::playsAtProjectRate(item) != atProjectRate && !media::setPlaybackAtProjectRate(item, atProjectRate, &why)) {
                reportFailure(this, tr("Source Settings: %1").arg(why));
            }
            item.modified = QDateTime::currentDateTimeUtc();
            storeClip(bin, item);
        }
    });
    return sourceSettingsHook_ ? sourceSettingsHook_(dialog) : dialog.exec() == QDialog::Accepted;
}

// The Color Management setting's Select LUT file (p.539, p.541-542; 8.5d2): "Browse for your file, select it and click Open
// to install it", into the project's LUTs folder, every project's, or both, as Installed LUTs says. A file that cannot be
// read or copied says why in a message.
QStringList MainWindow::installLutFile(const QString& chosen) {
    QString path = chosen;
    if (path.isEmpty()) {
        path = lutFileHook_ ? lutFileHook_()
                            : QFileDialog::getOpenFileName(this, tr("Select LUT file"), QString(),
                                                           tr("LUT files (*.cube *.itx *.3dl *.lut *.txt *.clf);;All files (*)"));
    }
    if (path.isEmpty()) return {};
    const QString scope = settings_ ? settings_->text(QStringLiteral("Color Management"), QStringLiteral("lutScope")) : QString();
    QStringList folders;
    if (scope != QLatin1String("shared") && project_) folders << media::projectLutFolder(project_->folder);
    if (scope == QLatin1String("shared") || scope == QLatin1String("both")) folders << media::sharedLutFolder();
    folders.removeAll(QString());
    QStringList names;
    QString error = folders.isEmpty() ? tr("There is no folder to install it in: open a project, or name LUMENIS_SHARED_LUTS.") : QString();
    for (const QString& folder : folders) {
        QString why;
        const QString name = media::installLut(path, folder, &why);
        if (name.isEmpty()) {
            error = why;
            break;
        }
        if (!names.contains(name)) names << name;
    }
    if (!error.isEmpty()) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("LUT not installed: %1: %2").arg(QFileInfo(path).fileName(), error));
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Select LUT file"),
                                    tr("%1 could not be installed: %2").arg(QFileInfo(path).fileName(), error), QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("lutNotInstalled"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return {};
    }
    return names;
}

// The Format tab's Proxy (8.5e2). "You can change the proxy mode at any time, however, any previously rendered media at
// that proxy mode will go offline. Should you switch back to that proxy mode, the rendered proxy media will still be
// available" (p.527): the render keys carry the mode, so the monitors and the Timeline's render dots follow.
bool MainWindow::setProxyMode(int mode) {
    if (!project_ || !model::proxyModes(project_->settings.format).contains(mode)) return false;
    if (project_->settings.proxy == mode) return true;
    model::ProjectSettings changed = project_->settings;
    changed.proxy = mode;
    if (!saveProjectSettings(changed, tr("Proxy"))) return false;
    model::setRenderProxy(mode);
    if (settingsWindow_) settingsWindow_->setProject(project_);
    composer_->refreshDisplay();
    timeline_->setDisplay(timeline_->display()); // the render dots follow the mode
    return true;
}

// The project's raster, rate and scan to the media library: the resolutions offered and XDCAM's fields follow them
// (media::setProjectFormat; p.1240-1272).
void MainWindow::tellMediaTheFormat() const {
    if (!project_) return;
    const ProjectFormat& f = project_->settings.format;
    media::setProjectFormat(QSize(f.width, f.height), f.editRate, f.scan == ScanMode::Interlaced, project_->settings.aspect);
}

bool MainWindow::setProjectFormat(const ProjectFormat& format) {
    if (!project_ || format.editRate != project_->settings.format.editRate) return false;
    if (project_->settings.format == format) return true;
    model::ProjectSettings changed = project_->settings;
    changed.format = format;
    // "Aspect Ratio ... SD: Select either 4:3 or 16:9; HD: Always uses the 16:9 aspect ratio; High-Res: Automatically
    // calculated based on project resolution" (p.1289): an SD project keeps its choice.
    if (format.family != FormatFamily::SD || project_->settings.format.family != FormatFamily::SD) changed.aspect = format.defaultAspect();
    if (!model::proxyModes(format).contains(changed.proxy)) changed.proxy = 0;
    if (!saveProjectSettings(changed, tr("Format"))) return false;
    model::setRenderProxy(changed.proxy);
    tellMediaTheFormat();
    binContainer_->setSequenceFormat(QString::fromStdString(format.label()));
    if (settingsWindow_) settingsWindow_->setProject(project_);
    frames_->clear();
    applySettings();
    composer_->refreshDisplay();
    return true;
}

// The project's Stereoscopic option (p.65-66). Ours: the Format tab shows it; this changes it, kept in the project file,
// and the monitors show the clips again in the new mode.
bool MainWindow::setProjectStereoscopic(const QString& mode) {
    if (!project_) return false;
    const std::vector<std::string> modes = stereoscopicModes();
    if (std::find(modes.begin(), modes.end(), mode.toStdString()) == modes.end()) return false;
    if (project_->settings.format.family == FormatFamily::SD && mode != QLatin1String("Off")) return false;
    if (project_->settings.stereoscopic == mode) return true;
    model::ProjectSettings changed = project_->settings;
    changed.stereoscopic = mode;
    if (!saveProjectSettings(changed, tr("Stereoscopic"))) return false;
    if (settingsWindow_) settingsWindow_->setProject(project_);
    frames_->clear();
    // What follows the option: Full Quality only for interlaced stereoscopic projects (p.403), the S3D Views.
    applySettings();
    composer_->refreshDisplay();
    return true;
}

// The S3D Views and Overlays (8.6d2; p.1162-1163, p.1190-1191, p.1226-1227). In a project whose Stereoscopic option uses
// both eyes (Full, Side by Side, Over / Under; ours: the others have one picture to show), the monitor's picture is made
// once per eye through eyeOverride_ and the two shown as the view says; Mono makes it once with that eye; Frame
// Compatible in a frame-compatible project and Follow Project are the project's own picture (8.6d1). The overlay's
// guides go over the result. The values are the Composer setting's for the monitor unless s3dOverride_ gives others
// (Full Screen Playback, the Video Output tool), where Follow S/R is the monitor's own.
QImage MainWindow::stereoPicture(MonitorId monitor, const std::function<QImage()>& plain) const {
    const model::StereoMode mode = project_ ? model::stereoModeOf(project_->settings.stereoscopic) : model::StereoMode::Off;
    if (eyeOverride_ || !(mode == model::StereoMode::Full || model::isFrameCompatible(mode))) return plain();
    // Groups in the Source monitor (p.1124, 8.6d3): "while viewing stereoscopic clips in the source monitor, the
    // stereoscopic view is turned off. You can choose to display either the left eye image, the right eye image, or the
    // image set as the leading eye. This view mode is set by the Stereoscopic option": Left or Right Eye Only, else
    // (ours) the leading eye.
    const model::LoadedClip* sourceClip = composer_->state(MonitorId::Source).current();
    if (monitor == MonitorId::Source &&
        (composer_->multiCameraMode() || (sourceClip && sourceClip->item.kind == model::BinItemKind::Group))) {
        struct Restore {
            std::optional<model::S3DView::MonoEye>& eye;
            ~Restore() { eye.reset(); }
        } restore{eyeOverride_};
        eyeOverride_ = mode == model::StereoMode::LeftEyeOnly    ? model::S3DView::MonoEye::Left
                       : mode == model::StereoMode::RightEyeOnly ? model::S3DView::MonoEye::Right
                                                                 : model::S3DView::MonoEye::Leading;
        return plain();
    }
    const QString kind = QStringLiteral("Composer");
    const bool source = monitor == MonitorId::Source;
    const QString monitorView = settings_->text(kind, source ? QStringLiteral("sourceS3DView") : QStringLiteral("recordS3DView"));
    const QString monitorOverlay = settings_->text(kind, source ? QStringLiteral("sourceS3DOverlay") : QStringLiteral("recordS3DOverlay"));
    QString viewValue = s3dOverride_ ? s3dOverride_->first : monitorView;
    const QString overlayValue = s3dOverride_ ? s3dOverride_->second : monitorOverlay;
    if (viewValue == QLatin1String("followSR")) viewValue = monitorView;
    const model::S3DView view = model::s3dViewOf(viewValue);
    using K = model::S3DView::Kind;
    QImage result;
    struct Restore {
        std::optional<model::S3DView::MonoEye>& eye;
        ~Restore() { eye.reset(); }
    } restore{eyeOverride_};
    if (view.kind == K::FollowProject || (view.kind == K::FrameCompatible && model::isFrameCompatible(mode))) {
        result = plain();
    } else if (view.kind == K::Mono) {
        eyeOverride_ = view.mono;
        result = plain();
    } else {
        eyeOverride_ = model::S3DView::MonoEye::Left;
        const QImage left = plain();
        eyeOverride_ = model::S3DView::MonoEye::Right;
        const QImage right = plain();
        // Ours: Frame Compatible in a Full project is Side by Side.
        result = media::stereoView(left, right, view, mode != model::StereoMode::OverUnder);
    }
    const model::S3DOverlay overlay = model::s3dOverlayOf(overlayValue);
    if (overlay != model::S3DOverlay::Off && !result.isNull()) {
        const model::S3DSettings s = s3dSettings();
        result = result.convertToFormat(QImage::Format_RGB32);
        media::drawParallaxGuides(result, overlay, model::parallaxNear(s), model::parallaxFar(s), s.rasterWidth);
    }
    return result;
}

model::S3DSettings MainWindow::s3dSettings() const {
    const QString kind = QStringLiteral("S3D");
    model::S3DSettings s;
    s.imperial = settings_->text(kind, QStringLiteral("unitSystem")) == QLatin1String("imperial");
    s.viewingDistance = settings_->value(kind, QStringLiteral("viewingDistance")).toDouble(s.viewingDistance);
    s.screenWidth = settings_->value(kind, QStringLiteral("screenWidth")).toDouble(s.screenWidth);
    s.rasterWidth = settings_->value(kind, QStringLiteral("rasterResolution")).toInt(s.rasterWidth);
    s.eyeSeparation = settings_->value(kind, QStringLiteral("eyeSeparation")).toDouble(s.eyeSeparation);
    s.depthNear = settings_->value(kind, QStringLiteral("depthLimitNear")).toDouble(s.depthNear);
    s.depthFar = settings_->value(kind, QStringLiteral("depthLimitFar")).toDouble(s.depthFar);
    s.manualNear = settings_->text(kind, QStringLiteral("parallaxNearMode")) == QLatin1String("manual");
    s.manualFar = settings_->text(kind, QStringLiteral("parallaxFarMode")) == QLatin1String("manual");
    s.parallaxNearValue = settings_->value(kind, QStringLiteral("parallaxNear")).toDouble();
    s.parallaxFarValue = settings_->value(kind, QStringLiteral("parallaxFar")).toDouble();
    return s;
}

QImage MainWindow::videoOutputPicture(QSize size) {
    s3dOverride_ = std::pair{videoOutput_->s3dView(), videoOutput_->s3dOverlay()};
    const QImage image = composer_->picture(MonitorId::Record, size);
    s3dOverride_.reset();
    return image;
}

// The Format tab's settings are kept in the project file; one that cannot be written says why, and the Format tab shows
// the project as it was.
bool MainWindow::saveProjectSettings(const model::ProjectSettings& changed, const QString& what) {
    if (!project_) return false;
    QString error;
    if (!model::ProjectStore::write(project_->file, changed, &error)) {
        // Console only: the message box below shows it.
        ConsoleLog::instance().append(tr("%1 not changed: %2").arg(what, error));
        auto* box = new QMessageBox(QMessageBox::Warning, what, tr("The project's %1 could not be saved: %2").arg(what, error),
                                    QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("projectNotSaved"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        if (settingsWindow_) settingsWindow_->setProject(project_);
        return false;
    }
    project_->settings = changed;
    return true;
}

// The Target Settings dialog's OK or Apply (p.551; 8.5e3): the project's mask margins, which the monitors show at once.
bool MainWindow::setMaskMargins(const model::MaskMargins& margins) {
    if (!project_) return false;
    if (project_->settings.mask == margins) return true;
    model::ProjectSettings changed = project_->settings;
    changed.mask = margins;
    if (!saveProjectSettings(changed, tr("Mask Margins"))) return false;
    if (settingsWindow_) settingsWindow_->setProject(project_);
    composer_->setMaskMargins(margins);
    if (FullScreenView* view = fullScreen_.data()) view->update();
    return true;
}

// The Format tab's Output Color Space (p.559; 8.5e4b2), saved with the project; the monitors on Project follow it.
bool MainWindow::setOutputColorSpace(const QString& space) {
    if (!project_ || !model::isAcesColorSpace(project_->settings.colorSpace) || !model::acesOutputColorSpaces().contains(space)) {
        return false;
    }
    if (project_->settings.outputColorSpace == space) return true;
    model::ProjectSettings changed = project_->settings;
    changed.outputColorSpace = space;
    if (!saveProjectSettings(changed, tr("Output Color Space"))) return false;
    if (settingsWindow_) settingsWindow_->setProject(project_);
    composer_->setOutputColorSpace(space);
    return true;
}

QImage MainWindow::outputFrame(const QImage& frame) const {
    // "This is a conversion from the ACES Timeline that happens when you export to file so you can deliver a specific
    // format from your ACES project" (p.559-560).
    if (!project_ || !model::isAcesColorSpace(project_->settings.colorSpace)) return frame;
    return media::toDisplayColorSpace(frame, project_->settings.colorSpace, project_->settings.outputColorSpace);
}

// The Color Encoding tab's Color Management Settings button (p.542): the User tab's Color Management setting.
void MainWindow::openColorManagementSetting() {
    SettingsWindow* window = showSettings();
    window->showTab(SettingsWindow::Tab::User);
    const int entry = settings_->file(model::SettingsScope::User).activeIndex(QStringLiteral("Color Management"));
    if (colorManagementHook_) {
        colorManagementHook_(entry);
        return;
    }
    if (entry >= 0) window->list(model::SettingsScope::User)->openEntry(entry);
}

// Without a project, the edit rate stays the LinkOptions default.
media::LinkOptions MainWindow::linkOptions() const {
    media::LinkOptions o;
    if (project_) o.editRate = project_->settings.format.editRate;
    o.when = QDateTime::currentDateTimeUtc();
    o.sdWidescreen = settings_ && settings_->flag(QStringLiteral("Link"), QStringLiteral("sdWidescreen"));
    // The Color Management setting (p.539, p.1157).
    o.insertColorTransformations = settings_ && settings_->flag(QStringLiteral("Color Management"), QStringLiteral("insertAuto"));
    // "If you are linking to clips, then the default is set to Center Keep Size" (p.475), as the Link setting says.
    if (settings_) {
        if (const auto r = model::reformatFromText(settings_->text(QStringLiteral("Link"), QStringLiteral("reformat")))) {
            o.reformat = model::reformatText(*r);
        }
    }
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
        // A file with alpha appears "in your bin as a Matte Key effect" (p.318) unless the Alpha Channel option is
        // Ignore (p.1204: "The imported graphic appears as a single master clip in the bin").
        if (auto key = linkedMatteKeyClip(item, *doc)) {
            ids << key->id;
            continue;
        }
        doc->addItem(item);
        ids << item.id;
    }
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    if (skipped > 0) {
        reportFailure(this, tr("Link: %n file(s) are not media that can be linked.", "", skipped));
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
    connect(&dialog, &LinkedMediaDialog::notRelinked, this, [&dialog](const QString& name, const QString& reason) {
        // "Open the Console window to see the name of the file or files
        // that were not relinked" (p.320); and a message over the Relink dialog (9.7).
        reportFailure(&dialog, tr("Relink: %1: %2").arg(name, reason));
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
    for (const QString& name : notRelinked) reportFailure(this, tr("Relink: %1: no matching media.").arg(name));
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
    std::optional<ProjectFormat> projectFormat;
    if (project_) projectFormat = project_->settings.format;
    ModifyDialog dialog(clips, this, projectFormat);
    const bool ok = modifyHook_ ? modifyHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return 0;
    QStringList errors;
    if (dialog.setsFormat()) {
        // "A message box tells you whether the sequence has been duplicated or modified" (p.1294).
        QStringList said;
        const QList<model::SequenceFormatChange> changes = dialog.applyFormat(clips, &errors);
        for (const model::SequenceFormatChange& c : changes) {
            if (c.duplicated) {
                view->document().addItem(c.item);
                said << tr("%1 was duplicated as %2, with its media unlinked.").arg(c.original, c.item.name);
            } else {
                storeClip(view->document().nodeId(), c.item);
                said << tr("%1 was modified to %2.").arg(c.item.name, c.item.fields.value(QStringLiteral("Format")));
            }
        }
        for (const QString& e : errors) said << e;
        // Console only: the message box below shows it.
        for (const QString& s : said) ConsoleLog::instance().append(tr("Set Format: %1").arg(s));
        if (!said.isEmpty() && !modifyHook_) QMessageBox::information(this, tr("Set Format"), said.join(QLatin1Char('\n')));
        return static_cast<int>(changes.size());
    }
    const QList<model::BinItem> changed = dialog.apply(clips, &errors);
    for (const model::BinItem& item : changed) storeClip(view->document().nodeId(), item);
    // Console only: the message box below shows it.
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

// "Load the sequence into the Record monitor and then, with either the Composer window or the Timeline
// window active, select Clip > Refresh Sequence"; or "select multiple sequences in the bin" (p.478). The
// Record monitor's sequence changes as an undoable edit; a bin's sequences are stored as they are.
void MainWindow::refreshTemplateBins() {
    if (!effectPalette_) return;
    QList<TemplateBin> bins;
    for (const BinDocument* doc : binContainer_->openDocuments()) {
        TemplateBin bin{doc->nodeId(), doc->bin().name, {}};
        for (const model::BinItem& item : doc->bin().items) {
            if (const auto t = model::effectTemplateOf(item)) bin.templates << TemplateBin::Entry{item.id, item.name, t->transition};
            // "Effect icons for open bins also display in the Effect Palette" (FX Guide p.613).
            if (model::correctionTemplateOf(item)) bin.templates << TemplateBin::Entry{item.id, item.name, false};
        }
        bins << bin;
    }
    // "The Effect Palette lists all color-corrected sequences that are loaded in the Record monitor at the bottom of the
    // scrollable list", each with its most recent corrections, "numbered to indicate the order of their creation, with
    // the most recently created template always numbered "1."" (FX Guide p.612).
    if (timeline_) {
        for (const QString& id : timeline_->correctedSequences()) {
            const model::LoadedClip* loaded = composer_->state(MonitorId::Record).loaded(id);
            if (!loaded) continue;
            TemplateBin bin{Timeline::automaticCorrectionBin(id), loaded->item.name, {}};
            const QList<Timeline::AutomaticCorrection> made = timeline_->automaticCorrections(id);
            for (int i = 0; i < made.size(); ++i)
                bin.templates << TemplateBin::Entry{QString::number(i + 1), QStringLiteral("%1  %2").arg(i + 1).arg(made.at(i).name), false};
            bins << bin;
        }
    }
    effectPalette_->setTemplateBins(bins);
}

int MainWindow::refreshMotionAdapters() {
    if (!project_) return 0;
    const bool interlaced = project_->settings.format.scan == ScanMode::Interlaced;
    const bool blending = settings_->flag(QStringLiteral("Render"), QStringLiteral("motionAdapterBlending"));
    const auto resolve = [this](const QString& bin, const QString& clip) { return composer_->resolve(bin, clip); };
    int changed = 0;
    BinView* view = binContainer_->activeView();
    const QWidget* focus = QApplication::focusWidget();
    if (view && focus && (view == focus || view->isAncestorOf(focus))) {
        for (const QString& id : view->selectedIds()) {
            const model::BinItem* found = view->document().bin().item(id);
            if (!found || found->kind != model::BinItemKind::Sequence) continue;
            model::BinItem item = *found;
            model::Sequence seq = model::Sequence::fromItem(item);
            const int n = model::refreshMotionAdapters(seq, resolve, interlaced, blending);
            if (n == 0) continue;
            seq.storeIn(item);
            item.modified = QDateTime::currentDateTimeUtc();
            storeClip(view->document().nodeId(), item);
            changed += n;
        }
        return changed;
    }
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return 0;
    timeline_->changeRecordSequence(tr("Refresh Sequence"), [&](model::Sequence& seq) {
        changed = model::refreshMotionAdapters(seq, resolve, interlaced, blending);
        return changed > 0;
    });
    return changed;
}

// Clip > Refresh Sequence > Color Adapters (p.478) and Remove Color Adapters (p.544; 8.5c): as refreshSpatialAdapters.
int MainWindow::refreshColorAdapters(bool remove) {
    const auto resolve = [this](const QString& bin, const QString& clip) { return composer_->resolve(bin, clip); };
    return refreshSequences([&](model::Sequence& seq) {
        return remove ? model::removeColorAdapters(seq) : model::refreshColorAdapters(seq, resolve);
    });
}

// Clip > Refresh Sequence > Aspect Ratio and Reformatting Options, and All FrameFlex including creative framing (p.478;
// 8.5a): as refreshMotionAdapters, the sequences selected in the active bin, else the Record monitor's.
int MainWindow::refreshSpatialAdapters(bool creative) {
    if (!project_) return 0;
    const auto resolve = [this](const QString& bin, const QString& clip) { return composer_->resolve(bin, clip); };
    return refreshSequences([&](model::Sequence& seq) { return model::refreshSpatialAdapters(seq, resolve, creative); });
}

int MainWindow::refreshSequences(const std::function<int(model::Sequence&)>& refresh) {
    if (!project_) return 0;
    int changed = 0;
    BinView* view = binContainer_->activeView();
    const QWidget* focus = QApplication::focusWidget();
    if (view && focus && (view == focus || view->isAncestorOf(focus))) {
        for (const QString& id : view->selectedIds()) {
            const model::BinItem* found = view->document().bin().item(id);
            if (!found || found->kind != model::BinItemKind::Sequence) continue;
            model::BinItem item = *found;
            model::Sequence seq = model::Sequence::fromItem(item);
            const int n = refresh(seq);
            if (n == 0) continue;
            seq.storeIn(item);
            item.modified = QDateTime::currentDateTimeUtc();
            storeClip(view->document().nodeId(), item);
            changed += n;
        }
        return changed;
    }
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return 0;
    timeline_->changeRecordSequence(tr("Refresh Sequence"), [&](model::Sequence& seq) {
        changed = refresh(seq);
        return changed > 0;
    });
    return changed;
}

int MainWindow::readAudioTimecode() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    auto tell = [this](const QString& text) {
        // Console only: the tell below shows it in a message.
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
        // Console only: the message box below shows it.
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
        // Renders "use this proxy resolution" (p.527); Transcode's "Project Dimensions: Transcodes the media based on the
        // project size and the proxy mode setting" (p.527).
        if (tab == QLatin1String("render") || tab == QLatin1String("transcode")) c.raster = model::proxyRaster(project_->settings);
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
    resolutionCache_.clear(); // the MultiRez display and bin headings look at the media again (8.6b-c)
    coverageCache_.clear();
    // Console only: how many media files the drives hold.
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
            reportFailure(this, tr("Import: %1").arg(error));
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
        // Console only: what was imported.
        ConsoleLog::instance().append(tr("Import: %1: %n item(s), %2 with media", "", static_cast<int>(imported->items.size()))
                                          .arg(QFileInfo(path).fileName())
                                          .arg(online));
        for (const QString& w : imported->warnings) reportFailure(this, tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
    }
    if (display.customColumns != doc->bin().display.customColumns) doc->setDisplay(display);
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    // "When you import an AAF, a sequence report will automatically open" when the Import setting says so (p.238).
    popupAafReports(doc->nodeId(), ids);
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
    if (!bins->saveBin(doc, &saveError)) reportFailure(this, tr("Open Bin: %1").arg(saveError));
    // Console only: what was opened.
    ConsoleLog::instance().append(tr("Open Bin: %1: %n item(s), %2 with media", "", static_cast<int>(imported->items.size()))
                                      .arg(info.fileName())
                                      .arg(online));
    for (const QString& w : imported->warnings) reportFailure(this, tr("Open Bin: %1: %2").arg(info.fileName(), w));
    return id;
}

bool MainWindow::exportMattePict(const QString& path) {
    const QSize raster = project_ ? QSize(project_->settings.format.width, project_->settings.format.height) : QSize(1920, 1080);
    if (effectEditor_->aniMatteImage(QSize(16, 9)).isNull()) {
        // "You must click the Effect Editor before you select File > Export Matte PICT" (p.418). Ours: a message when the
        // Effect Editor shows no AniMatte effect.
        QMessageBox::information(this, tr("Export Matte PICT"),
                                 tr("Show an Animated Matte effect in the Effect Editor, in Effect mode, to export its matte."));
        return false;
    }
    QString file = path;
    if (file.isEmpty()) {
        QSettings remembered;
        const QString folder = remembered.value(QStringLiteral("exportMattePictFolder"), QDir::homePath()).toString();
        file = QFileDialog::getSaveFileName(this, tr("Export PICT As"), QDir(folder).filePath(QStringLiteral("Matte.pct")),
                                            tr("PICT files (*.pct *.pict)"));
        if (file.isEmpty()) return false;
        remembered.setValue(QStringLiteral("exportMattePictFolder"), QFileInfo(file).absolutePath());
    }
    if (!effectEditor_->exportMattePict(file, raster)) {
        QMessageBox::warning(this, tr("Export Matte PICT"), tr("The PICT file could not be written:\n%1").arg(QDir::toNativeSeparators(file)));
        return false;
    }
    return true;
}

// Steps: collect the selection and the clips its sequences use, make sure an Export setting exists,
// ask for the file and setting (Export As dialog), then write the AAF with interop::aaf::exportItems,
// or, when the Export setting's Export As is Media Composer Bin (ours, docs/spec/avb.md "Export"), a
// bin with interop::avb::exportBin. Results and warnings go to the Console. The last folder exported
// to is remembered in QSettings.
QJsonValue MainWindow::exportSetting(const QString& key) const {
    if (exportOverride_.contains(key)) return exportOverride_.value(key);
    return settings_->value(QStringLiteral("Export"), key);
}

std::optional<MainWindow::ExportSelection> MainWindow::exportSelection() {
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty()) {
        reportFailure(this, tr("Export to File: select the clips or sequences to export in a bin."));
        return std::nullopt;
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
    return ExportSelection{selected, clips, binOf, &doc};
}

// The file type of an Export As choice: its extension and its name in the save dialog.
QString MainWindow::exportSuffix(const QString& exportAs) const {
        // A Graphic export's file is its Graphic Format's; an Audio export's WAVE or AIFF-C (7.1b).
        if (exportAs == QLatin1String("graphic")) {
            const QString g = exportValue(QStringLiteral("graphicFormat"));
            return media::stillSuffix(g == QLatin1String("jpeg")   ? media::StillFormat::Jpeg
                                      : g == QLatin1String("tiff") ? media::StillFormat::Tiff
                                      : g == QLatin1String("bmp")  ? media::StillFormat::Bmp
                                      : g == QLatin1String("pict") ? media::StillFormat::Pict
                                                                   : media::StillFormat::Png);
        }
        if (exportAs == QLatin1String("audio"))
            return exportValue(QStringLiteral("audioFileFormat")) == QLatin1String("aiff") ? QStringLiteral("aif")
                                                                                                                         : QStringLiteral("wav");
        if (exportAs == QLatin1String("dpx")) return QStringLiteral("dpx");
        if (exportAs == QLatin1String("openexr")) return QStringLiteral("exr");
        return exportAs == QLatin1String("avb")      ? QStringLiteral("avb")
               : exportAs == QLatin1String("scc")    ? QStringLiteral("scc")
               : exportAs == QLatin1String("dcp")    ? QStringLiteral("dcp")
               : exportAs == QLatin1String("ttml")   ? QStringLiteral("ttml")
               : exportAs == QLatin1String("ale")    ? QStringLiteral("ale")
               : exportAs == QLatin1String("fcpxml") ? QStringLiteral("xml")
               : exportAs == QLatin1String("mp4")    ? QStringLiteral("mp4")
               : exportAs == QLatin1String("mov") || exportAs == QLatin1String("qtMovie") ? QStringLiteral("mov")
               : exportAs == QLatin1String("op1a") || exportAs == QLatin1String("xdcam") || exportAs == QLatin1String("op1aLegacy") ||
                       exportAs == QLatin1String("as11")
                   ? QStringLiteral("mxf")
                                                     : QStringLiteral("aaf");
}

QString MainWindow::exportFilter(const QString& exportAs) {
        return exportAs == QLatin1String("avb")      ? tr("Media Composer bins (*.avb)")
               : exportAs == QLatin1String("scc")    ? tr("Scenarist Closed Caption (*.scc)")
               : exportAs == QLatin1String("dcp")    ? tr("Digital Cinema Package folder (*.dcp)")
               : exportAs == QLatin1String("ttml")   ? tr("Timed Text Markup Language (*.ttml)")
               : exportAs == QLatin1String("ale")    ? tr("Avid Log Exchange (*.ale)")
               : exportAs == QLatin1String("fcpxml") ? tr("Final Cut Pro 7 XML (*.xml)")
               : exportAs == QLatin1String("mp4")    ? tr("MP4 movies (*.mp4)")
               : exportAs == QLatin1String("graphic") || exportAs == QLatin1String("dpx") || exportAs == QLatin1String("openexr")
                   ? tr("Images (*.png *.jpg *.tif *.bmp *.pct *.dpx *.exr)")
               : exportAs == QLatin1String("audio")  ? tr("Audio files (*.wav *.aif)")
               : exportAs == QLatin1String("mov") || exportAs == QLatin1String("qtMovie") ? tr("QuickTime movies (*.mov)")
               : exportAs == QLatin1String("op1a") || exportAs == QLatin1String("xdcam") || exportAs == QLatin1String("op1aLegacy") ||
                       exportAs == QLatin1String("as11")
                   ? tr("MXF files (*.mxf)")
                                                     : tr("AAF files (*.aaf)");
}

// ---------------------------------------------------------------- Export to Device (7.2b; docs/spec/export-device.md)

int MainWindow::exportToDevice(const QString& kind) {
    const QString title = kind == QLatin1String("p2") ? tr("Export to Device > P2") : tr("Export to Device > XDCAM");
    auto say = [&](const QString& text) { reportFailure(this, tr("%1: %2").arg(title, text)); };
    // "If you have a sequence loaded in the Record monitor, the sequence exports when you select Export to Device. You
    // can also right-click the clip or sequence in a bin" (p.883): the Record monitor's sequence, else the selection.
    QList<QPair<model::BinItem, QString>> items; // item, its bin
    if (const auto* loaded = composer_->state(MonitorId::Record).current();
        loaded && loaded->item.kind == model::BinItemKind::Sequence) {
        items << qMakePair(loaded->item, loaded->binId);
    } else if (BinView* view = binContainer_->activeView()) {
        for (const model::BinItem& item : view->document().bin().items) {
            if (view->selectedIds().contains(item.id) && (item.kind == model::BinItemKind::Sequence || item.kind == model::BinItemKind::MasterClip ||
                                                          item.kind == model::BinItemKind::Subclip))
                items << qMakePair(item, view->document().nodeId());
        }
    }
    if (items.isEmpty()) {
        say(tr("load a sequence in the Record monitor, or select clips or sequences in a bin."));
        return 0;
    }
    ExportDeviceDialog dialog(kind, providers_.usable<ext::ExportDeviceProvider>(), exportFlag(QStringLiteral("useMarks")),
                              exportFlag(QStringLiteral("useSelectedTracks")), this);
    const bool ok = exportDeviceHook_ ? exportDeviceHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || !dialog.hasTargets() || !dialog.device()) return 0;
    const ext::ExportDeviceTarget target = dialog.target();
    // Ours: the sound goes as the stereo mix, tracks 1 and 2 ("the system mixes down to audio tracks 1 and 2 during
    // export", p.884); a disk whose clips have another number of tracks takes none.
    constexpr int channels = 2;
    if (target.audioChannels > 0 && target.audioChannels != channels) {
        say(tr("the disk's clips have %1 audio tracks and this clip has %2; reformat the disk or export to another one "
               "(p.884).").arg(target.audioChannels).arg(channels));
        return 0;
    }
    const QString format = dialog.videoFormat();
    const media::MovieCompression* compression = media::findMovieCompression(format);
    if (!compression) {
        say(tr("the device offers no video format this build can write."));
        return 0;
    }
    QJsonObject values{{QStringLiteral("exportAs"), compression->xdcam ? QStringLiteral("xdcam") : QStringLiteral("op1a")},
                       {QStringLiteral("xdcamFormat"), format},
                       {QStringLiteral("mxfCompression"), format},
                       {QStringLiteral("mxfAudioFormat"), QStringLiteral("stereo")},
                       {QStringLiteral("mxfBitDepth"), QString::number(dialog.bitDepth())},
                       {QStringLiteral("useMarks"), dialog.useMarks()},
                       {QStringLiteral("useSelectedTracks"), dialog.useSelectedTracks()}};
    int delivered = 0;
    QTemporaryDir scratch;
    for (int i = 0; i < items.size(); ++i) {
        const QString file = scratch.filePath(QStringLiteral("clip%1.mxf").arg(i));
        exportOverride_ = values;
        QString why;
        const bool written = exportMovieFile(items.at(i).first, items.at(i).second, file, values.value(QStringLiteral("exportAs")).toString(), &why);
        exportOverride_ = {};
        if (!written) {
            say(tr("%1: %2").arg(items.at(i).first.name, why));
            continue;
        }
        std::string name;
        std::string error;
        if (!dialog.device()->deliver(target.id, file.toStdString(), &name, &error)) {
            say(tr("%1: %2").arg(items.at(i).first.name, QString::fromStdString(error)));
            continue;
        }
        ++delivered;
        // Console only: where each item went.
        ConsoleLog::instance().append(tr("%1: %2").arg(title, tr("%1 is %2 on %3").arg(items.at(i).first.name, QString::fromStdString(name),
                                                                                    QString::fromStdString(target.name))));
    }
    return delivered;
}

// ---------------------------------------------------------------- SubCap (7.4a; docs/spec/subcap.md)

bool MainWindow::subcapAction(const QString& action) {
    auto say = [this](const QString& text) {
        reportFailure(this, tr("Subtitles: %1").arg(text));
        return false;
    };
    const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) return say(tr("load a sequence into the Record monitor."));
    const model::Sequence seq = model::Sequence::fromItem(loaded->item);
    const TimecodeFormat format = model::timecodeFormatOf(loaded->item);
    const std::int64_t startFrame = model::startFrameOf(loaded->item);
    const auto effect = timeline_->currentEffect();
    // Labels of one timecode format read as another's (a 25 fps .stl in a 24 fps project): the same hh:mm:ss:ff.
    auto relabel = [](std::int64_t frames, const TimecodeFormat& from, const TimecodeFormat& to) {
        const auto tc = Timecode::parse(Timecode(frames, from).toString(), to);
        return tc ? tc->frameCount() : frames;
    };
    if (action == QLatin1String("import")) {
        if (!effect || !model::subcapSegmentRange(seq, effect->track, effect->frame)) return say(tr("put the position on a Subtitles effect first."));
        const QString path = subcapFileHook_ ? subcapFileHook_(false)
                                             : QFileDialog::getOpenFileName(this, tr("Select a file that contains caption data"), QString(),
                                                                            tr("Caption files (*.stl *.txt);;EBU N19 (*.stl);;Avid DS (*.txt)"));
        if (path.isEmpty()) return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return say(tr("%1 cannot be read.").arg(QDir::toNativeSeparators(path)));
        const QByteArray bytes = file.readAll();
        QString why;
        std::optional<QList<model::Caption>> captions;
        TimecodeFormat fileFormat = format;
        if (path.endsWith(QLatin1String(".stl"), Qt::CaseInsensitive)) {
            int fps = 25;
            captions = model::parseStl(bytes, &fps, nullptr, &why);
            fileFormat = TimecodeFormat::forRate(Rational(fps), false);
        } else {
            captions = model::parseDsCaptions(QString::fromUtf8(bytes), format, &why);
        }
        if (!captions) return say(why);
        // The file's timecode as sequence frames.
        for (model::Caption& c : *captions) {
            c.start = relabel(c.start, fileFormat, format) - startFrame;
            c.end = relabel(c.end, fileFormat, format) - startFrame;
        }
        // "The dialog box ... provides you with information about the timecode ranges of the file and the effect, and
        // reports what the result of the import will be" (p.380): ours, in the Console.
        const auto range = *model::subcapSegmentRange(seq, effect->track, effect->frame);
        auto tc = [&](std::int64_t f) { return QString::fromStdString(Timecode(startFrame + f, format).toString()); };
        if (!captions->isEmpty())
            // Console only: where the file's captions fall against the effect.
            ConsoleLog::instance().append(tr("Subtitles: the file's captions run from %1 to %2, the effect from %3 to %4.")
                                              .arg(tc(captions->first().start), tc(captions->last().end), tc(range.first), tc(range.second)));
        const bool ok = timeline_->changeRecordSequence(tr("Import Caption Data"), [&](model::Sequence& s) {
            return model::importCaptions(s, effect->track, effect->frame, *captions, &why);
        });
        if (!ok) return say(why.isEmpty() ? tr("the captions could not be imported.") : why);
        // Console only: how many captions came in.
        ConsoleLog::instance().append(tr("Subtitles: %1").arg(tr("%n caption(s) imported.", "", static_cast<int>(captions->size()))));
        return true;
    }
    if (action == QLatin1String("export")) {
        QStringList tracks;
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Video) continue;
            if (std::any_of(t.segments.begin(), t.segments.end(), [](const model::Segment& g) { return g.effect && model::isSubCap(*g.effect); }))
                tracks << t.name();
        }
        if (tracks.isEmpty()) return say(tr("the sequence has no Subtitles effects."));
        ExportCaptionsDialog choose(tracks, this);
        if (!(exportCaptionsHook_ ? exportCaptionsHook_(choose) : choose.exec() == QDialog::Accepted)) return false;
        const QString path = subcapFileHook_ ? subcapFileHook_(true)
                                             : QFileDialog::getSaveFileName(this, tr("Export caption data to the specified file"), QString(),
                                                                            tr("EBU N19 (*.stl);;Avid DS (*.txt)"));
        if (path.isEmpty()) return false;
        QList<model::Caption> captions = model::sequenceCaptions(seq, choose.tracks(), choose.topWins());
        QByteArray out;
        if (path.endsWith(QLatin1String(".stl"), Qt::CaseInsensitive)) {
            // EBU N19 is 25 or 30 frames a second: our labels at that rate.
            const int fps = format.nominalFps % 30 == 0 ? 30 : 25;
            const TimecodeFormat stlFormat = TimecodeFormat::forRate(Rational(fps), false);
            for (model::Caption& c : captions) {
                c.start = relabel(startFrame + c.start, format, stlFormat);
                c.end = relabel(startFrame + c.end, format, stlFormat);
            }
            out = model::writeStl(captions, fps, model::subcapStlInfo(seq));
        } else {
            for (model::Caption& c : captions) {
                c.start += startFrame;
                c.end += startFrame;
            }
            out = model::writeDsCaptions(captions, format).toUtf8();
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size() || !file.commit())
            return say(tr("%1 cannot be written.").arg(QDir::toNativeSeparators(path)));
        say(tr("%n caption(s) exported to %1.", "", static_cast<int>(captions.size())).arg(QDir::toNativeSeparators(path)));
        return true;
    }
    if (action == QLatin1String("masterList")) {
        MasterCaptionListDialog dialog(subcapEntries(seq), format, startFrame, this);
        if (!(masterCaptionHook_ ? masterCaptionHook_(dialog) : dialog.exec() == QDialog::Accepted)) return false;
        const QList<MasterCaptionListDialog::Entry> edited = dialog.entries();
        return timeline_->changeRecordSequence(tr("Edit Master Caption List"), [&](model::Sequence& s) {
            for (const MasterCaptionListDialog::Entry& e : edited) {
                model::Track* t = s.track(e.segment.track);
                if (!t) continue;
                std::int64_t start = 0;
                const int i = t->segmentAt(e.segment.start, &start);
                if (i < 0 || start != e.segment.start || !t->segments[i].effect) continue;
                model::setCaptionText(*t->segments[i].effect, e.text);
            }
            return true;
        });
    }
    if (action == QLatin1String("globalProperties")) return editSubcapGlobalProperties();
    return false;
}

QString MainWindow::subcapStylesheetFolder() const {
    // "Caption stylesheets are XML files and are stored in a folder called Caption Effect Stylesheets" (p.387); ours
    // beside the user's settings, as Send_To_Templates.
    const QString userSettings = settings_->path(model::SettingsScope::User);
    return userSettings.isEmpty() ? QDir::home().filePath(QStringLiteral("Caption Effect Stylesheets"))
                                  : QFileInfo(userSettings).absoluteDir().filePath(QStringLiteral("Caption Effect Stylesheets"));
}

bool MainWindow::editSubcapGlobalProperties() {
    const auto ref = timeline_->currentEffect();
    const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
    if (!ref || !loaded) return false;
    const model::Sequence sequence = model::Sequence::fromItem(loaded->item); // effectFor points into it
    const model::Effect* current = model::effectFor(sequence, *ref);
    if (!current || !model::isSubCap(*current)) return false;
    SubcapPropertiesDialog dialog(*current, subcapStylesheetFolder(), this);
    if (!(subcapPropertiesHook_ ? subcapPropertiesHook_(dialog) : dialog.exec() == QDialog::Accepted)) return false;
    const model::Effect edited = dialog.effect();
    return timeline_->changeEffect(*ref, tr("Edit Global Properties"), [&](model::Effect& e, model::Transition*) {
        const QString text = model::captionText(e);
        e.parameters = edited.parameters;
        model::setCaptionText(e, text);
    });
}

// ---------------------------------------------------------------- Video Output tool (7.3b; docs/spec/video-output.md)

void MainWindow::openVideoOutput() {
    videoOutput_->setDevices(providers_.usable<ext::VideoOutputProvider>());
    if (settings_->hasProject()) {
        const model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
        const int active = f.activeIndex(QStringLiteral("Video Output"));
        if (active >= 0) videoOutput_->setValues(f.entries.at(active).values);
    }
    showTool(QStringLiteral("VideoOutput"));
}

void MainWindow::storeVideoOutput(const QJsonObject& values, const QString& name) {
    if (!settings_->hasProject()) return;
    model::SettingsFile& f = settings_->file(model::SettingsScope::Project);
    const QString kind = QStringLiteral("Video Output");
    f.ensure({kind});
    int index = f.activeIndex(kind);
    // Settings > Save As: a version of that name, made active (p.915).
    if (!name.isEmpty()) {
        index = -1;
        for (int i : f.versions(kind)) {
            if (f.entries.at(i).name == name) index = i;
        }
        if (index < 0) index = f.add(model::SettingEntry{kind, name, false, {}});
        f.setActive(index);
    }
    if (index < 0) return;
    f.entries[index].values = values;
    settings_->commit(model::SettingsScope::Project);
}

// ---------------------------------------------------------------- Digital Cut (7.3a; docs/spec/digital-cut.md)

void MainWindow::openDigitalCut() {
    digitalCut_->setDecks(providers_.usable<ext::DeckProvider>());
    QStringList tracks;
    if (const auto* loaded = composer_->state(MonitorId::Record).current(); loaded && loaded->item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(loaded->item);
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video || t.kind == model::TrackKind::Audio) tracks << t.name();
        }
    }
    digitalCut_->setSequenceTracks(tracks);
    // The Output Format menu of the project (p.933, p.1299); "By default, the menus display the timecode format of the
    // sequence you loaded into the Timeline" (p.935).
    if (project_) digitalCut_->setOutputFormats(model::outputFormats(project_->settings.format));
    if (const auto* loaded = composer_->state(MonitorId::Record).current(); loaded && loaded->item.kind == model::BinItemKind::Sequence) {
        DigitalCutTool::Options o = digitalCut_->options();
        o.dropFrame = model::timecodeFormatOf(loaded->item).dropFrame;
        digitalCut_->setOptions(o);
    }
    digitalCut_->setStatus(tracks.isEmpty() ? tr("Load a sequence into the Record monitor.") : QString());
    updateDigitalCutDuration();
    showTool(QStringLiteral("DigitalCut"));
}

namespace {

// The tape's timecode for an output format: NTSC formats' drop or non-drop as the RS-422 Output menu says (p.935).
TimecodeFormat tapeTimecodeFormat(const model::OutputFormat& format, bool dropFrame) {
    return TimecodeFormat::forRate(format.tapeRate, format.ntsc && dropFrame);
}

// "You can determine the frame that ends a sequence by checking the Pullout column ... Type A or B in the Pullin column"
// (p.938): a digital cut starting on a B frame.
bool pullinB(const model::BinItem& item) {
    return item.fields.value(QStringLiteral("Pullin")).trimmed().compare(QLatin1String("B"), Qt::CaseInsensitive) == 0;
}

} // namespace

void MainWindow::updateDigitalCutDuration() {
    const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) {
        digitalCut_->setDuration(QString());
        return;
    }
    const DigitalCutTool::Options o = digitalCut_->options();
    const model::OutputFormat of = digitalCut_->outputFormat();
    if (of.tapeRate.num() == 0) return;
    const model::Sequence seq = model::Sequence::fromItem(loaded->item);
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (!o.entireSequence) {
        if (loaded->in) start = std::clamp<std::int64_t>(*loaded->in, 0, end);
        if (loaded->out) end = std::clamp<std::int64_t>(*loaded->out + 1, start, end);
    }
    const std::int64_t tapeFrames = model::outputFrameCount(of, end - start, o.cadence, pullinB(loaded->item));
    // "If you select 30.00 fps, the sequence duration in the Digital Cut tool matches the sequence duration in the
    // Timeline" (p.936): the tape's length in seconds counted at 30 frames a second.
    if (of.destinationRate && o.destination30) {
        const auto frames30 = static_cast<std::int64_t>(std::llround(static_cast<double>(tapeFrames) * 30.0 / of.tapeRate.toDouble()));
        digitalCut_->setDuration(QString::fromStdString(Timecode(frames30, TimecodeFormat::forRate(Rational(30), false)).toString()));
        return;
    }
    digitalCut_->setDuration(QString::fromStdString(Timecode(tapeFrames, tapeTimecodeFormat(of, o.dropFrame)).toString()));
}

std::int64_t MainWindow::digitalCut(bool preview) {
    auto fail = [this](const QString& text) {
        digitalCut_->setStatus(text);
        // Console only: the Digital Cut tool's status line shows it.
        ConsoleLog::instance().append(tr("Digital Cut: %1").arg(text));
        return std::int64_t{-1};
    };
    // "Load a sequence into the Record monitor. (You cannot access digital cut options without a sequence loaded.)"
    const model::LoadedClip* loaded = composer_->state(MonitorId::Record).current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) return fail(tr("Load a sequence into the Record monitor."));
    const DigitalCutTool::Options o = digitalCut_->options();
    const model::BinItem item = loaded->item;
    const model::Sequence seq = model::Sequence::fromItem(item);
    // The output format (p.933, p.1299): the rate the frames play at, the tape's rate, fields and raster, and its
    // timecode; the project's own format plays frame for frame.
    model::OutputFormat of = digitalCut_->outputFormat();
    if (of.tapeRate.num() == 0) {
        of.playRate = item.editRate;
        of.tapeRate = item.editRate;
    }
    const TimecodeFormat format = of.tapeRate == item.editRate && !of.ntsc ? model::timecodeFormatOf(item) : tapeTimecodeFormat(of, o.dropFrame);
    const bool fromB = pullinB(item);
    // The range: the whole sequence with Entire Sequence, else its marks (p.929).
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (!o.entireSequence) {
        if (loaded->in) start = std::clamp<std::int64_t>(*loaded->in, 0, end);
        if (loaded->out) end = std::clamp<std::int64_t>(*loaded->out + 1, start, end);
    }
    std::int64_t black = 0;
    if (o.addBlack && !o.blackDuration.isEmpty()) {
        const auto tc = Timecode::parse(o.blackDuration.toStdString(), TimecodeFormat::forRate(of.tapeRate, false));
        if (!tc) return fail(tr("Add Black at Tail needs a duration such as 00:00:05:00."));
        black = tc->frameCount();
    }
    if (end <= start) return fail(tr("There is nothing to record."));
    // The sequence's frames as tape frames, with the pulldown the format adds (p.933).
    const std::int64_t tapeFrames = model::outputFrameCount(of, end - start, o.cadence, fromB);

    // The deck: Remote mode cues it; Local mode records where the deck is, as a crash record (ours: the folder deck
    // has no buttons of its own to press).
    std::shared_ptr<ext::DeckProvider> provider = digitalCut_->deckProvider();
    std::unique_ptr<ext::DeckSession> session;
    if (provider) {
        std::string why;
        session = provider->open(digitalCut_->deckId(), &why);
        if (!session) return fail(QString::fromStdString(why));
    } else if (!preview) {
        return fail(tr("No Deck: configure a deck first (p.928)."));
    }
    ext::DeckRecording r;
    r.mode = o.remote ? o.mode : ext::DeckRecordMode::Crash;
    // Where on the tape (p.929-931): the sequence's own timecode, where the deck is, the Mark IN, or (crash record)
    // ignoring time; then the Custom Frame Offset.
    // Sequence Time on another tape rate: the sequence's start timecode's label at the tape's rate, then the marked
    // start's frames as tape frames.
    std::int64_t in = model::sameTimecodeFrames(model::startFrameOf(item), item.editRate, of.tapeRate) +
                      model::outputFrameCount(of, start, o.cadence, fromB);
    if (!o.remote || o.start == DigitalCutTool::Start::RecordDeckTime || o.start == DigitalCutTool::Start::IgnoreTime) {
        in = session ? session->position() : 0;
    } else if (o.start == DigitalCutTool::Start::MarkInTime) {
        const auto tc = Timecode::parse(o.markIn.toStdString(), format);
        if (!tc) return fail(tr("The Mark IN time is not a timecode."));
        // "Deck control for capture and digital cut is limited to 30 fps. A message box warns you if you try to mark an
        // odd timecode value (such as 01:00:00:03)" (p.1296): 60 fps timecode on a 30 fps deck.
        if (format.nominalFps >= 50 && tc->frameCount() % 2 != 0) {
            const QString text = tr("Deck control is limited to 30 fps: %1 is an odd timecode value. Mark an even one.")
                                     .arg(o.markIn);
            if (!quietDigitalCut_) QMessageBox::warning(this, tr("Digital Cut"), text);
            return fail(text);
        }
        in = tc->frameCount();
    }
    if (o.remote && o.customOffset) in += o.frameOffset;
    r.in = std::max<std::int64_t>(0, in);
    // The tape's raster: the project's, or a downconvert's SD raster (p.1299).
    const QSize raster = (of.width > 0 ? QSize(of.width, of.height) : QSize(project_->settings.format.width, project_->settings.format.height))
                             .expandedTo(media::kMinimumRaster);
    r.width = raster.width();
    r.height = raster.height();
    r.rateNum = static_cast<int>(of.tapeRate.num());
    r.rateDen = static_cast<int>(of.tapeRate.den());
    const bool anyVideo = std::any_of(o.sequenceTracks.begin(), o.sequenceTracks.end(), [](const QString& t) { return t.startsWith(QLatin1Char('V')); });
    const bool anyAudio = std::any_of(o.sequenceTracks.begin(), o.sequenceTracks.end(), [](const QString& t) { return t.startsWith(QLatin1Char('A')); });
    // The Enable Track buttons in Remote mode; every track in Local mode. Our mix is stereo: the first two tape tracks
    // enabled take it.
    r.video = anyVideo && (!o.remote || o.tapeVideo);
    r.audio.clear();
    for (int t : o.remote ? o.tapeAudio : QList<int>{1, 2}) {
        if (anyAudio && r.audio.size() < 2) r.audio.push_back(t);
    }
    r.channels = 2;
    const QString range = tr("%1 to %2 on the tape")
                              .arg(QString::fromStdString(Timecode(r.in, format).toString()),
                                   QString::fromStdString(Timecode(r.in + tapeFrames + black, format).toString()));
    // "The Digital Cut tool goes through the motions of an insert edit and shows you how the tape will appear ... but
    // does not actually change the master tape" (p.928): ours cues the deck and says what would be recorded.
    if (preview) {
        if (session && o.remote) session->cue(r.in);
        digitalCut_->setStatus(tr("Preview: %1, %n frame(s).", "", static_cast<int>(tapeFrames + black)).arg(range));
        return 0;
    }
    if (!r.video && r.audio.empty()) return fail(tr("No tracks are enabled to record onto."));
    std::string why;
    if (!session->beginRecord(r, &why)) return fail(QString::fromStdString(why));

    // The frames through the export's picture and sound (the "sink" format), each handed to the deck.
    digitalCutHalted_ = false;
    digitalCut_->setRunning(true);
    digitalCut_->setStatus(tr("Recording %1").arg(range));
    std::int64_t written = 0;
    QString error;
    const float outputGain = static_cast<float>(std::pow(10.0, outputGainDb() / 20.0));
    // The sound plays faster or slower with the picture (p.935: "0.1% slowdown", "4.1% speedup" ...).
    const double speed = model::outputSpeed(of, item.editRate);
    const bool sameSpeed = of.playRate == item.editRate;
    // NTSC SD tapes have their lower field first; the others their upper.
    const bool topFirst = !(of.ntsc && raster.height() == 486);
    movieSink_ = [&](const media::MovieSource& source, const media::MovieExportOptions&, const std::function<bool(double)>& progress,
                     QString* sinkError) {
        std::vector<std::vector<float>> planes;
        std::vector<std::vector<float>> read;
        // The sound of tape samples [from, from + count) at 48 kHz: the sequence's own when the frames play at its rate,
        // else its sound from (from x speed) on, resampled linearly.
        auto soundAt = [&](std::int64_t from, int count) {
            if (sameSpeed) {
                source.sound(from, count, planes);
                return;
            }
            const double first = static_cast<double>(from) * speed;
            const auto base = static_cast<std::int64_t>(std::floor(first));
            const int span = static_cast<int>(std::ceil(static_cast<double>(count) * speed)) + 2;
            source.sound(base, span, read);
            planes.assign(read.size(), std::vector<float>(static_cast<std::size_t>(count), 0.0f));
            for (std::size_t c = 0; c < read.size(); ++c) {
                const std::vector<float>& samples = read[c];
                for (int i = 0; i < count; ++i) {
                    const double at = first + static_cast<double>(i) * speed - static_cast<double>(base);
                    const auto k = static_cast<std::size_t>(at);
                    const float t = static_cast<float>(at - static_cast<double>(k));
                    const float a = k < samples.size() ? samples[k] : 0.0f;
                    const float b = k + 1 < samples.size() ? samples[k + 1] : a;
                    planes[c][static_cast<std::size_t>(i)] = a + (b - a) * t;
                }
            }
        };
        auto sendFrame = [&](const QImage& picture, std::int64_t f, bool silent) {
            QImage frame = picture.size() == raster ? picture.convertToFormat(QImage::Format_RGBA8888)
                                                    : media::fitMoviePicture(picture, raster, media::MovieScaling::Letterbox)
                                                          .convertToFormat(QImage::Format_RGBA8888);
            const std::int64_t from = media::framesToSamples(f, of.tapeRate);
            const int count = static_cast<int>(media::framesToSamples(f + 1, of.tapeRate) - from);
            std::vector<float> interleaved(static_cast<std::size_t>(count) * 2, 0.0f);
            if (!silent && source.sound) {
                soundAt(from, count);
                // Output Gain affects "both the speakers and record devices" (p.920).
                if (outputGain != 1.0f) {
                    for (auto& plane : planes) {
                        for (float& x : plane) x *= outputGain;
                    }
                }
                for (int c = 0; c < 2 && c < static_cast<int>(planes.size()); ++c) {
                    for (int i = 0; i < count && i < static_cast<int>(planes[static_cast<std::size_t>(c)].size()); ++i)
                        interleaved[static_cast<std::size_t>(i * 2 + c)] = planes[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)];
                }
            }
            std::string frameError;
            if (!session->writeFrame(frame.constBits(), static_cast<int>(frame.bytesPerLine()), interleaved.data(), count, &frameError)) {
                if (sinkError) *sinkError = QString::fromStdString(frameError);
                return false;
            }
            ++written;
            return true;
        };
        // The sequence's frames, the last two kept for the fields that show them again.
        QHash<std::int64_t, QImage> kept;
        auto sequenceFrame = [&](std::int64_t n) {
            n = std::clamp<std::int64_t>(n, 0, std::max<std::int64_t>(0, source.frames - 1));
            if (auto it = kept.constFind(n); it != kept.constEnd()) return *it;
            QImage picture = source.picture ? source.picture(n) : QImage();
            if (picture.isNull()) {
                picture = QImage(raster, QImage::Format_RGB32);
                picture.fill(Qt::black);
            }
            picture = media::fitMoviePicture(picture, raster, media::MovieScaling::Letterbox).convertToFormat(QImage::Format_RGB32);
            if (kept.size() >= 2) kept.remove(std::min(kept.keys().first(), kept.keys().last()));
            kept.insert(n, picture);
            return picture;
        };
        const std::int64_t total = tapeFrames + black;
        for (std::int64_t f = 0; f < total; ++f) {
            QImage picture;
            if (f < tapeFrames) {
                // A tape frame's two fields from the frames playing at their times: the same frame, or (pulldown) two
                // frames woven together (p.933).
                if (of.interlaced) {
                    const std::int64_t first = model::outputSourceFrame(of, 2 * f, o.cadence, fromB);
                    const std::int64_t second = model::outputSourceFrame(of, 2 * f + 1, o.cadence, fromB);
                    picture = first == second ? sequenceFrame(first)
                                              : media::weaveFields(sequenceFrame(first), sequenceFrame(second), topFirst);
                } else {
                    picture = sequenceFrame(model::outputSourceFrame(of, f, o.cadence, fromB));
                }
            } else {
                picture = QImage(raster, QImage::Format_RGB32);
                picture.fill(Qt::black); // Add Black at Tail
            }
            if (picture.isNull()) {
                picture = QImage(raster, QImage::Format_RGB32);
                picture.fill(Qt::black);
            }
            if (!sendFrame(picture, f, f >= tapeFrames)) return false;
            // Halt Digital Cut, or the space bar (p.930), between frames.
            if ((progress && !progress(static_cast<double>(f + 1) / static_cast<double>(total))) || digitalCutHalted_) {
                if (sinkError) *sinkError = tr("Halted.");
                return false;
            }
        }
        return true;
    };
    QJsonArray tracks;
    for (const QString& t : o.sequenceTracks) tracks << t;
    exportOverride_ = {{QStringLiteral("useMarks"), !o.entireSequence},
                       {QStringLiteral("useSelectedTracks"), false},
                       {QStringLiteral("movieAudioFormat"), QStringLiteral("stereo")},
                       {QStringLiteral("tracksOnly"), tracks}};
    const bool ok = exportMovieFile(item, loaded->binId, QString(), QStringLiteral("sink"), &error);
    exportOverride_ = {};
    movieSink_ = {};
    digitalCut_->setRunning(false);
    if (!ok) {
        // What was recorded so far stays on the tape.
        session->halt();
        return fail(tr("%1 (%n frame(s) recorded)", "", static_cast<int>(written)).arg(error));
    }
    if (!session->finish(&why)) return fail(QString::fromStdString(why));
    digitalCut_->setStatus(tr("Recorded %1.").arg(range));
    // Console only: what was recorded.
    ConsoleLog::instance().append(tr("Digital Cut: %1 recorded %2").arg(item.name, range));
    return written;
}

// ---------------------------------------------------------------- AS-02 Export Volumes (7.2d; docs/spec/as02.md)

QString MainWindow::newVolumeForExport() {
    if (!project_) {
        reportFailure(this, tr("Volume for Export: open a project first."));
        return {};
    }
    model::ExportVolume volume;
    volume.path = project_->folder;
    volume.name = tr("Export Volume");
    ExportVolumeDialog dialog(volume, this);
    const bool ok = exportVolumeHook_ ? exportVolumeHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return {};
    volume = dialog.volume();
    if (volume.name.isEmpty() || volume.path.isEmpty()) {
        reportFailure(this, tr("Volume for Export: the volume needs a name and a path."));
        return {};
    }
    // "The Export Volume bin opens" (p.903): a bin of the volume's name, with its bar.
    const QString binId = binContainer_->newBin();
    binContainer_->renameNode(binId, volume.name);
    BinDocument* doc = binContainer_->openBin(binId);
    if (!doc) return {};
    doc->setExportVolume(volume.toJson());
    doc->save();
    return binId;
}

bool MainWindow::editExportVolume(const QString& binId) {
    BinDocument* doc = binContainer_->document(binId);
    const auto volume = doc ? model::exportVolumeOf(doc->bin()) : std::nullopt;
    if (!volume) return false;
    ExportVolumeDialog dialog(*volume, this);
    const bool ok = exportVolumeHook_ ? exportVolumeHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return false;
    doc->setExportVolume(dialog.volume().toJson());
    return true;
}

int MainWindow::commitExportVolume(const QString& binId) {
    auto say = [this](const QString& text) { reportFailure(this, tr("Commit: %1").arg(text)); };
    BinDocument* doc = binContainer_->document(binId);
    const auto volume = doc ? model::exportVolumeOf(doc->bin()) : std::nullopt;
    if (!volume) return 0;
    if (media::bmxTranswrapPath().isEmpty()) {
        say(tr("AS-02 needs bmx's bmxtranswrap, which was not found (BUILDING.md, \"AS-11 and AS-02\")."));
        return 0;
    }
    // "Once you commit the assets, the sequence is flattened to the OP1b format, the video is encoded to the J2K codec
    // and audio saved as PCM" (p.904): each sequence not yet committed is written as OP1a with the volume's
    // compression and rewrapped as a bundle in the asset folder; "An AAF copy of the unflattened sequences(s)" goes in
    // the bundle's extra folder.
    QDir().mkpath(volume->folder());
    const QJsonObject values{{QStringLiteral("exportAs"), QStringLiteral("op1a")},
                             {QStringLiteral("mxfCompression"), volume->compression},
                             {QStringLiteral("mxfAudioFormat"), QStringLiteral("stereo")},
                             {QStringLiteral("mxfBitDepth"), QStringLiteral("24")},
                             {QStringLiteral("useMarks"), false},
                             {QStringLiteral("useSelectedTracks"), false}};
    int committed = 0;
    const QList<model::BinItem> items = doc->bin().items;
    for (model::BinItem item : items) {
        if (item.kind != model::BinItemKind::Sequence || item.fields.value(model::kMediaStatusHeading) == model::kCommitted) continue;
        QString name = item.name;
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        const QString bundle = QDir(volume->folder()).filePath(name);
        QTemporaryDir scratch;
        const QString op1a = scratch.filePath(QStringLiteral("version.mxf"));
        exportOverride_ = values;
        QString why;
        const bool written = exportMovieFile(item, binId, op1a, QStringLiteral("op1a"), &why);
        exportOverride_ = {};
        if (!written || !media::writeAs02(op1a, bundle, {volume->shimName, volume->shimId, volume->shimAnnotation}, &why)) {
            say(tr("%1: %2").arg(item.name, why));
            continue;
        }
        QList<model::BinItem> aafItems{item};
        const model::Sequence seq = model::Sequence::fromItem(item);
        QSet<QString> seen;
        for (const model::Track& t : seq.tracks()) {
            for (const model::Segment& seg : t.segments) {
                if (seg.isFiller() || seen.contains(seg.clipId)) continue;
                seen << seg.clipId;
                if (const auto clip = composer_->resolve(seg.binId.isEmpty() ? binId : seg.binId, seg.clipId)) aafItems << *clip;
            }
        }
        QDir(bundle).mkpath(QStringLiteral("extra"));
        const interop::aaf::ExportResult aaf =
            interop::aaf::exportItems(aafItems, QDir(bundle).filePath(QStringLiteral("extra/%1.aaf").arg(name)), {});
        if (!aaf.ok) say(tr("%1: the AAF copy: %2").arg(item.name, aaf.error));
        item.fields.insert(model::kMediaStatusHeading, model::kCommitted);
        doc->replaceItem(item);
        ++committed;
        // Console only: where each item went.
        ConsoleLog::instance().append(tr("Commit: %1").arg(tr("%1 is in %2").arg(item.name, QDir::toNativeSeparators(bundle))));
    }
    doc->save();
    return committed;
}

// ---------------------------------------------------------------- Send To (7.1c2; docs/spec/send-to.md)

QString MainWindow::sendToFolder() const {
    // Ours: a Send_To_Templates folder beside the user's settings, as Title_Templates is (p.360).
    const QString userSettings = settings_->path(model::SettingsScope::User);
    return userSettings.isEmpty() ? QDir::home().filePath(QStringLiteral("Send_To_Templates"))
                                  : QFileInfo(userSettings).dir().filePath(QStringLiteral("Send_To_Templates"));
}

QStringList MainWindow::exportSummary(const QJsonObject& values) const {
    // Each option of the Export setting with its value, its choice's label for menus (ours: every option, the guide
    // shows "all the settings that affect the current export", p.865).
    QStringList lines;
    const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Export"));
    if (!kind) return lines;
    for (const SettingTab& tab : kind->tabs) {
        for (const SettingOption& o : tab.options) {
            if (o.later) continue;
            const QJsonValue v = values.contains(o.key) ? values.value(o.key) : settings_->value(QStringLiteral("Export"), o.key);
            QString shown = v.isBool() ? (v.toBool() ? tr("On") : tr("Off")) : v.isDouble() ? QString::number(v.toDouble()) : v.toString();
            for (const SettingChoice& c : o.choices) {
                if (c.value == v.toString()) shown = c.label;
            }
            const QString label = o.label.isEmpty() ? o.key : o.label;
            lines << QStringLiteral("%1%2: %3").arg(tab.name.isEmpty() ? QString() : tab.name + QStringLiteral(" - "), label, shown);
        }
    }
    return lines;
}

void MainWindow::refreshSendToMenu() {
    auto* menu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:file_output_send_to"));
    if (!menu) return;
    qDeleteAll(sendToActions_);
    sendToActions_.clear();
    QAction* first = menu->actions().value(0);
    // "The following table describes the Send To templates that Avid supplies. If you create custom Send To templates,
    // they also appear as Send To menu commands" (p.865): Pro Tools, then the user's templates.
    auto add = [&](const QString& text, const std::function<void()>& run) {
        auto* a = new QAction(text, menu);
        a->setProperty(kDynamicMenuItem, true);
        connect(a, &QAction::triggered, this, run);
        menu->insertAction(first, a);
        sendToActions_ << a;
    };
    add(tr("Pro Tools"), [this] {
        model::SendToTemplate t;
        t.name = tr("Pro Tools");
        sendTo(t, true);
    });
    for (const model::SendToTemplate& t : model::loadSendToTemplates(sendToFolder())) {
        add(t.name, [this, t] { sendTo(t, false); });
    }
    auto* separator = new QAction(menu);
    separator->setSeparator(true);
    separator->setProperty(kDynamicMenuItem, true);
    menu->insertAction(first, separator);
    sendToActions_ << separator;
}

bool MainWindow::saveSendToTemplateAs(const model::SendToTemplate& t) {
    // "Rename the file. Make sure you leave the .stt extension. Click Save" (p.865).
    const QString suggested = QDir(sendToFolder()).filePath((t.name.isEmpty() ? tr("Untitled") : t.name) + QStringLiteral(".stt"));
    QDir().mkpath(sendToFolder());
    const QString path = sendToSaveHook_ ? sendToSaveHook_(suggested)
                                         : QFileDialog::getSaveFileName(this, tr("Save Send To Template"), suggested,
                                                                        tr("Send To templates (*.stt)"));
    if (path.isEmpty()) return false;
    QString error;
    if (!model::saveSendToTemplate(path, t, &error)) {
        reportFailure(this, tr("Send To: the template could not be saved: %1").arg(error));
        return false;
    }
    refreshSendToMenu();
    return true;
}

bool MainWindow::sendTo(model::SendToTemplate t, bool proTools) {
    const std::optional<ExportSelection> picked = exportSelection();
    if (!picked) return false;
    QSettings remembered;
    // "If you are sending to Avid Pro Tools, a template options dialog box opens" (p.863).
    QString option;
    if (proTools) {
        ProToolsOptionsDialog choose(remembered.value(QStringLiteral("sendTo/proToolsOption")).toString(), this);
        const bool chosen = proToolsHook_ ? proToolsHook_(choose) : choose.exec() == QDialog::Accepted;
        if (!chosen || choose.option().isEmpty()) return false;
        option = choose.option();
        remembered.setValue(QStringLiteral("sendTo/proToolsOption"), option);
        t.folder = remembered.value(QStringLiteral("sendTo/proToolsFolder")).toString();
    }
    // A template's folder is made when it is exported to; only a template without one takes the last export's.
    if (t.folder.isEmpty()) {
        t.folder = remembered.value(QStringLiteral("export/folder")).toString();
        if (t.folder.isEmpty() || !QDir(t.folder).exists()) t.folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    if (t.exportValues.isEmpty()) t.exportValues = settings_->file(model::SettingsScope::User).activeValues(QStringLiteral("Export"));
    // "The Filename text box displays the name of the sequence or clip you chose" (p.864).
    QString name = picked->selected.first().name;
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    SendToDialog dialog(t, name, this);
    auto exportsOf = [&](const model::SendToTemplate& now) {
        return proTools ? model::proToolsExports(option, now.exportValues) : QList<QJsonObject>{now.exportValues};
    };
    auto summarize = [&] {
        QStringList lines;
        for (const QJsonObject& values : exportsOf(dialog.sendToTemplate())) {
            if (!lines.isEmpty()) lines << QString();
            lines << exportSummary(values);
        }
        dialog.setSummary(lines);
    };
    summarize();
    connect(&dialog, &SendToDialog::optionsRequested, this, [&] {
        // "If you need to make any Export settings changes, click the appropriate Options button, make the changes, and
        // click Save" (p.865): the Export setting's dialog with the template's values; the template keeps them.
        const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Export"));
        if (!kind) return;
        model::SettingEntry entry;
        entry.kind = QStringLiteral("Export");
        entry.name = dialog.sendToTemplate().name;
        entry.values = dialog.sendToTemplate().exportValues;
        SettingsDialog options(*kind, entry, &dialog);
        const bool kept = sendToOptionsHook_ ? sendToOptionsHook_(options) : options.exec() == QDialog::Accepted;
        if (!kept) return;
        QJsonObject values = dialog.sendToTemplate().exportValues;
        const QJsonObject changed = options.values();
        for (auto it = changed.begin(); it != changed.end(); ++it) values.insert(it.key(), it.value());
        dialog.setExportValues(values);
        summarize();
    });
    connect(&dialog, &SendToDialog::saveAsTemplateRequested, this, [&] { saveSendToTemplateAs(dialog.sendToTemplate()); });
    const bool ok = sendToHook_ ? sendToHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || dialog.fileName().isEmpty()) return false;
    t = dialog.sendToTemplate();
    QDir().mkpath(t.folder);

    // The export, or a Pro Tools option's exports, each with the template's Export values in place of the setting's.
    QStringList files;
    bool all = true;
    for (const QJsonObject& values : exportsOf(t)) {
        exportOverride_ = values;
        const QString path = QDir(t.folder).filePath(dialog.fileName() + QLatin1Char('.') + exportSuffix(exportValue(QStringLiteral("exportAs"))));
        const qsizetype before = ConsoleLog::instance().lines().size();
        const bool written = writeExport(*picked, path);
        exportOverride_ = {};
        if (!written) {
            // As Export to File: why, in a message (9.7).
            QStringList why = ConsoleLog::instance().lines().mid(before);
            if (why.isEmpty()) why << tr("Nothing was exported.");
            showFailure(this, tr("Send To"), why.join(QLatin1Char('\n')));
            all = false;
            break;
        }
        files << path;
    }
    // "Whenever you return to a Send To dialog box, the destination folder that you set last appears" (p.865).
    if (proTools) {
        remembered.setValue(QStringLiteral("sendTo/proToolsFolder"), t.folder);
    } else {
        const QString file = QDir(sendToFolder()).filePath(t.name + QStringLiteral(".stt"));
        if (QFileInfo::exists(file)) model::saveSendToTemplate(file, t);
    }
    if (!all) return false;
    // Auto Launch with Auto Load Exported File(s), and Reveal file (p.865-866).
    if (t.autoLaunch && !t.application.isEmpty()) {
        const QStringList arguments = t.autoLoad ? files : QStringList();
        if (launchHook_) {
            launchHook_(t.application, arguments);
        } else {
            QProcess::startDetached(t.application, arguments);
        }
    }
    if (t.reveal && !files.isEmpty()) showInFileManager(files.first());
    return true;
}

void MainWindow::makeNewSendTo() {
    // "Select File > Output > Send To > Make New ... The Send To dialog box opens with a default export template",
    // OK, then "Name the new Send To (.stt) template" (p.866-867). The template takes the Export setting's values.
    const std::optional<ExportSelection> picked = exportSelection();
    model::SendToTemplate t;
    t.name = tr("Untitled");
    t.exportValues = settings_->file(model::SettingsScope::User).activeValues(QStringLiteral("Export"));
    QSettings remembered;
    t.folder = remembered.value(QStringLiteral("export/folder")).toString();
    if (t.folder.isEmpty()) t.folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    SendToDialog dialog(t, picked ? picked->selected.first().name : QString(), this);
    dialog.setSummary(exportSummary(t.exportValues));
    connect(&dialog, &SendToDialog::optionsRequested, this, [&] {
        const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Export"));
        if (!kind) return;
        model::SettingEntry entry;
        entry.kind = QStringLiteral("Export");
        entry.values = dialog.sendToTemplate().exportValues;
        SettingsDialog options(*kind, entry, &dialog);
        const bool kept = sendToOptionsHook_ ? sendToOptionsHook_(options) : options.exec() == QDialog::Accepted;
        if (!kept) return;
        QJsonObject values = dialog.sendToTemplate().exportValues;
        const QJsonObject changed = options.values();
        for (auto it = changed.begin(); it != changed.end(); ++it) values.insert(it.key(), it.value());
        dialog.setExportValues(values);
        dialog.setSummary(exportSummary(values));
    });
    const bool ok = sendToHook_ ? sendToHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    saveSendToTemplateAs(dialog.sendToTemplate());
}

void MainWindow::exportToFile() {
    const std::optional<ExportSelection> picked = exportSelection();
    if (!picked) return;
    const QList<model::BinItem>& selected = picked->selected;
    model::SettingsFile& file = settings_->file(model::SettingsScope::User);
    if (file.versions(QStringLiteral("Export")).isEmpty()) {
        file.ensure({QStringLiteral("Export")});
        settings_->commit(model::SettingsScope::User);
    }
    QList<int> versions = file.versions(QStringLiteral("Export"));
    QStringList names;
    int active = 0;
    for (int i = 0; i < versions.size(); ++i) {
        const model::SettingEntry& e = file.entries.at(versions.at(i));
        names << (e.name.isEmpty() ? tr("Untitled") : e.name);
        if (e.active) active = i;
    }
    auto suffixOf = [this](const QString& exportAs) { return exportSuffix(exportAs); };
    auto filterOf = [](const QString& exportAs) { return exportFilter(exportAs); };
    const QString exportAs = exportValue(QStringLiteral("exportAs"));
    QSettings remembered;
    QString folder = remembered.value(QStringLiteral("export/folder")).toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // The first item's name, with characters that are not allowed in file names replaced.
    QString base = selected.first().name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    ExportAsDialog dialog(QDir(folder).filePath(base + QLatin1Char('.') + suffixOf(exportAs)), names, active, this);
    dialog.setFileType(suffixOf(exportAs), filterOf(exportAs));
    // Options (p.869-870): "The Export Settings dialog box opens" over the Export As dialog. Save keeps the changes
    // in the chosen setting; Save As makes a new Export setting with the Setting Name given, chosen in the menu.
    connect(&dialog, &ExportAsDialog::optionsRequested, this, [&](int i) {
        const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Export"));
        if (!kind || i < 0 || i >= versions.size()) return;
        const model::SettingEntry entry = file.entries.at(versions.at(i));
        SettingsDialog options(*kind, entry, &dialog);
        options.setObjectName(QStringLiteral("exportSettings"));
        options.setWindowTitle(tr("Export Settings"));
        options.useSaveButtons();
        if (project_) options.setEditRate(project_->settings.format.editRate);
        const bool kept = exportOptionsHook_ ? exportOptionsHook_(options) : options.exec() == QDialog::Accepted;
        if (!kept) return;
        int chosen = i;
        if (options.saveAsName().isEmpty()) {
            file.entries[versions.at(i)].values = options.values();
        } else {
            model::SettingEntry made = entry;
            made.name = options.saveAsName();
            made.active = false;
            made.values = options.values();
            const int at = file.add(made);
            versions = file.versions(QStringLiteral("Export"));
            names.clear();
            for (int v = 0; v < versions.size(); ++v) {
                const model::SettingEntry& e = file.entries.at(versions.at(v));
                names << (e.name.isEmpty() ? tr("Untitled") : e.name);
                if (e.active) active = v;
                if (versions.at(v) == at) chosen = v;
            }
            dialog.setSettings(names, chosen);
        }
        settings_->commit(model::SettingsScope::User);
        // The file type follows the format chosen.
        const QString as = options.values().value(QStringLiteral("exportAs")).toString(exportAs);
        dialog.setFileType(suffixOf(as), filterOf(as));
    });
    const bool ok = exportHook_ ? exportHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || dialog.path().isEmpty()) return;
    // The setting chosen is the one in use from now on.
    if (const int chosen = dialog.setting(); chosen >= 0 && chosen < versions.size() && chosen != active) {
        file.setActive(versions.at(chosen));
        settings_->commit(model::SettingsScope::User);
    }
    const qsizetype before = ConsoleLog::instance().lines().size();
    if (!writeExport(*picked, dialog.path())) {
        // A failed export says why where it was asked for, not only in the Console (owner's report, 2026-11-08: an
        // export on Windows 11 made no file and said nothing). Not modal, so that tests and scripts go on.
        QStringList why = ConsoleLog::instance().lines().mid(before);
        if (why.isEmpty()) why << tr("Nothing was exported.");
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Export to File"), tr("The export did not complete."),
                                    QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("exportFailed"));
        box->setInformativeText(why.join(QLatin1Char('\n')));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
}

bool MainWindow::writeExport(const ExportSelection& chosen, QString path) {
    const QList<model::BinItem>& selected = chosen.selected;
    const QList<model::BinItem>& clips = chosen.clips;
    const QHash<QString, QString>& binOf = chosen.binOf;
    const BinDocument& doc = *chosen.doc;
    auto suffixOf = [this](const QString& exportAs) { return exportSuffix(exportAs); };
    const QString format = exportValue(QStringLiteral("exportAs"));
    // Exports rendered through the compositor and the mix (7.1a-b).
    const bool movie = format == QLatin1String("mov") || format == QLatin1String("mp4") || format == QLatin1String("qtMovie") ||
                       format == QLatin1String("audio") || format == QLatin1String("graphic") || format == QLatin1String("dpx") ||
                       format == QLatin1String("openexr") || format == QLatin1String("op1a") || format == QLatin1String("xdcam") ||
                       format == QLatin1String("op1aLegacy") || format == QLatin1String("as11") || format == QLatin1String("dcp");
    const bool captionText = format == QLatin1String("scc") || format == QLatin1String("ttml");
    if (format != QLatin1String("aaf") && format != QLatin1String("avb") && format != QLatin1String("ale") &&
        format != QLatin1String("fcpxml") && !movie && !captionText) {
        // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
        ConsoleLog::instance().append(tr("Export to File: only AAF, Avid Log Exchange, Media Composer bins, Final Cut Pro 7 XML, MOV, "
                                         "MP4, Audio, Graphic, DPX and OpenEXR can be exported so far (Export As in the Export setting)."));
        return false;
    }
    const model::BinItem* firstSequence = nullptr;
    for (const model::BinItem& item : selected) {
        if (item.kind == model::BinItemKind::Sequence && !firstSequence) firstSequence = &item;
    }
    if (captionText && !firstSequence) {
        // "Select the sequence containing the Closed Caption text" (p.985).
        // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
        ConsoleLog::instance().append(tr("Export to File: closed caption text comes from a sequence: select one."));
        return false;
    }
    if (format == QLatin1String("fcpxml") && !firstSequence) {
        // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
        ConsoleLog::instance().append(tr("Export to File: Final Cut Pro 7 XML holds a sequence: select one."));
        return false;
    }
    // The file name extension stays the format's (p.869), also when the setting was changed in the
    // dialog.
    const QString suffix = suffixOf(format);
    if (QFileInfo(path).suffix().compare(suffix, Qt::CaseInsensitive) != 0) {
        const QString old = QFileInfo(path).suffix();
        if (old.compare(QLatin1String("aaf"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("avb"), Qt::CaseInsensitive) == 0 ||
            old.compare(QLatin1String("ale"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0 ||
            old.compare(QLatin1String("scc"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("dcp"), Qt::CaseInsensitive) == 0 || old.compare(QLatin1String("ttml"), Qt::CaseInsensitive) == 0 ||
            QStringList{QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("wav"), QStringLiteral("aif"), QStringLiteral("png"),
                        QStringLiteral("jpg"), QStringLiteral("tif"), QStringLiteral("bmp"), QStringLiteral("pct"), QStringLiteral("dpx"),
                        QStringLiteral("exr")}
                .contains(old.toLower())) {
            path.chop(old.size() + 1);
        }
        path += QLatin1Char('.') + suffix;
    }
    QSettings().setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    const bool includeVideo = exportFlag(QStringLiteral("includeVideo"));
    const bool includeAudio = exportFlag(QStringLiteral("includeAudio"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool written = false;
    QString error;
    QStringList warnings;
    qsizetype count = selected.size();
    if (movie) {
        // Each sequence or clip selected as a movie (7.1a): the first at the path chosen, the others beside it
        // under their own names (ours).
        QApplication::restoreOverrideCursor();
        count = 0;
        for (int i = 0; i < selected.size(); ++i) {
            QString to = path;
            if (i > 0) {
                QString name = selected.at(i).name;
                name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
                to = QFileInfo(path).absoluteDir().filePath(name + QLatin1Char('.') + suffix);
            }
            QString why;
            if (exportMovieFile(selected.at(i), doc.nodeId(), to, format, &why)) {
                ++count;
                // Console only: where the export was written.
                ConsoleLog::instance().append(tr("Export to File: %1").arg(QDir::toNativeSeparators(to)));
            } else {
                // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
                ConsoleLog::instance().append(tr("Export to File: %1: %2").arg(selected.at(i).name, why));
            }
        }
        return count > 0;
    }
    if (captionText) {
        // "You can export the Closed Caption text as either TTML (Timed Text Markup Language), or SCC (Scenarist Closed
        // Caption) format" (p.985): the first sequence's D1 data, SCC as its byte pairs at the sequence's timecode.
        const model::Sequence sequence = model::Sequence::fromItem(*firstSequence);
        const QByteArray captionBytes = model::sequenceCaptionData(sequence, [this](const model::Segment& s) { return captionClipData(s); });
        count = 1;
        if (captionBytes.isEmpty() || model::captionLoadingSpans(captionBytes).isEmpty()) {
            error = tr("the sequence has no closed captions on D1");
        } else {
            const bool dropFrame = model::isDropFrame(*firstSequence);
            const TimecodeFormat tcFormat = TimecodeFormat::forRate(firstSequence->editRate, dropFrame);
            const QString text = format == QLatin1String("scc")
                                     ? model::writeScc(captionBytes, model::startFrameOf(*firstSequence), tcFormat)
                                     : model::writeTtml(model::decodeCaptions(captionBytes), firstSequence->editRate);
            QSaveFile out(path);
            written = out.open(QIODevice::WriteOnly) && out.write(text.toUtf8()) >= 0 && out.commit();
            if (!written) error = tr("cannot write %1").arg(QDir::toNativeSeparators(path));
        }
    } else if (format == QLatin1String("ale")) {
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
        // The Export Methods of the Video and Audio Details tabs (7.1c1): the media copied, consolidated or mixed
        // down into the destination folder first, the AAF naming it.
        QList<model::BinItem> items = selected + clips;
        QString why;
        if (!prepareAafMedia(selected, clips, doc.nodeId(), path, items, warnings, &why)) {
            written = false;
            error = why;
        } else {
            const interop::aaf::ExportResult result = interop::aaf::exportItems(items, path, options);
            written = result.ok;
            error = result.error;
            warnings << result.warnings;
            count = items.size();
        }
    }
    QApplication::restoreOverrideCursor();
    if (!written) {
        // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
        ConsoleLog::instance().append(tr("Export to File: %1").arg(error));
        return false;
    }
    // Console only: what was exported.
    ConsoleLog::instance().append(tr("Export to File: %1: %n item(s)", "", static_cast<int>(count)).arg(QDir::toNativeSeparators(path)));
    // Console only: the caller shows the export's Console lines in a message (Export to File, Send To).
    for (const QString& w : warnings) ConsoleLog::instance().append(tr("Export to File: %1").arg(w));
    return true;
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
            reportFailure(this, tr("Import: %1").arg(error));
            continue;
        }
        interop::otio::reuseClips(*imported, doc->bin().items);
        int sequences = 0;
        for (const model::BinItem& item : imported->items) {
            if (item.kind == model::BinItemKind::Sequence) ++sequences;
            doc->addItem(item);
            ids << item.id;
        }
        // Console only: what was imported.
        ConsoleLog::instance().append(tr("Import: %1: %n sequence(s), %2 clip(s)", "", sequences)
                                          .arg(QFileInfo(path).fileName())
                                          .arg(imported->items.size() - sequences));
        for (const QString& w : imported->warnings) reportFailure(this, tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
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
            reportFailure(this, tr("Import: %1").arg(error));
            continue;
        }
        for (const model::BinItem& item : imported->items) {
            doc->addItem(item);
            ids << item.id;
        }
        for (const QString& column : imported->customColumns) {
            if (!display.customColumns.contains(column)) display.customColumns << column;
        }
        // Console only: what was imported.
        ConsoleLog::instance().append(
            tr("Import: %1: %n clip(s) from the shot log", "", static_cast<int>(imported->items.size())).arg(QFileInfo(path).fileName()));
        for (const QString& w : imported->warnings) reportFailure(this, tr("Import: %1: %2").arg(QFileInfo(path).fileName(), w));
    }
    if (display.customColumns != doc->bin().display.customColumns) doc->setDisplay(display);
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    return ids;
}

// ---------------------------------------------------------------- Closed captions (7.4b; docs/spec/closed-captions.md)

QStringList MainWindow::importScc(const QString& binId, const QStringList& paths) {
    if (paths.isEmpty()) return {};
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc) return {};
    const Rational rate = project_ ? project_->settings.format.editRate : Rational(30000, 1001);
    QStringList ids;
    for (const QString& path : paths) {
        auto say = [&](const QString& text) { reportFailure(this, tr("Import: %1: %2").arg(QFileInfo(path).fileName(), text)); };
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            say(tr("cannot be read"));
            continue;
        }
        QString error;
        const auto file = model::parseScc(QString::fromLatin1(f.readAll()), &error);
        if (!file) {
            say(error);
            continue;
        }
        // "The AncData Import dialog box opens. The starting timecode and drop frame checkbox will be automatically
        // populated with the applicable information from the selected .scc file" (p.232).
        AncDataImportDialog dialog(QFileInfo(path).fileName(), file->startFrame(), file->dropFrame, this);
        if (!(ancDataImportHook_ ? ancDataImportHook_(dialog) : dialog.exec() == QDialog::Accepted)) continue;
        const auto start = dialog.startFrame();
        if (!start) {
            say(tr("the starting timecode is not timecode"));
            continue;
        }
        // The file's lines are read in its own timecode; the dialog's start is in the drop frame mode chosen.
        model::SccFile relabelled = *file;
        if (dialog.dropFrame() != file->dropFrame) {
            for (model::SccLine& line : relabelled.lines) {
                const auto fields = Timecode(line.frame, TimecodeFormat{30, file->dropFrame}).fields();
                line.frame = Timecode::fromFields(fields, TimecodeFormat{30, dialog.dropFrame()}).frameCount();
            }
            relabelled.dropFrame = dialog.dropFrame();
        }
        const QByteArray captionBytes = model::sccCaptionData(relabelled, *start, rate);
        if (captionBytes.isEmpty()) {
            say(tr("no caption data after the starting timecode"));
            continue;
        }
        // "It is a D-track only clip that contains only the 608 compatibility bytes from EIA-708" (p.232).
        model::BinItem item;
        item.id = model::newItemId();
        item.mobId = MobId::generate();
        item.kind = model::BinItemKind::MasterClip;
        item.name = QFileInfo(path).completeBaseName();
        item.created = item.modified = QDateTime::currentDateTimeUtc();
        item.editRate = rate;
        model::setClipCaptionData(item, captionBytes);
        const bool sameRate = rate.num() * 1001 == rate.den() * 30000;
        const TimecodeFormat format = TimecodeFormat::forRate(rate, dialog.dropFrame() && sameRate);
        const std::int64_t startAtRate =
            sameRate ? *start : static_cast<std::int64_t>(std::llround(static_cast<double>(*start) * 1001.0 / 30000.0 * static_cast<double>(rate.num()) / static_cast<double>(rate.den())));
        item.fields.insert(QStringLiteral("Tracks"), QStringLiteral("D1"));
        item.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(startAtRate, format).toString()));
        doc->addItem(item);
        ids << item.id;
        // Console only: how many captions came in.
        ConsoleLog::instance().append(tr("Import: %1: %2").arg(QFileInfo(path).fileName(),
                                                               tr("%n caption(s)", "", static_cast<int>(model::decodeCaptions(captionBytes).size()))));
    }
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    return ids;
}

QByteArray MainWindow::captionClipData(const model::Segment& segment) const {
    if (segment.isFiller()) return {};
    const auto clip = composer_->resolveForSegment(segment);
    return clip ? model::clipCaptionData(*clip) : QByteArray();
}

std::optional<model::CaptionScreen> MainWindow::captionOn(MonitorId monitor, std::int64_t frame) const {
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (!loaded) return std::nullopt;
    QByteArray bytes;
    if (loaded->item.kind == model::BinItemKind::Sequence) {
        bytes = model::sequenceCaptionData(model::Sequence::fromItem(loaded->item),
                                          [this](const model::Segment& s) { return captionClipData(s); });
    } else if (model::hasCaptionData(loaded->item)) {
        bytes = model::clipCaptionData(loaded->item);
    }
    if (bytes.isEmpty()) return std::nullopt;
    const int i = monitor == MonitorId::Record ? 1 : 0;
    if (bytes != captionData_[i]) {
        captionData_[i] = bytes;
        captionShown_[i] = model::decodeCaptions(bytes);
    }
    return model::captionAt(captionShown_[i], frame);
}

// The steps of p.951: the file (the system's open dialog first, unless a hook answers), the Frame
// Rate and the bin (New Bin makes one). The reels' sources already in the bin are used again
// (edl::reuseSources), so importing a list twice does not make two sources of one tape.
QStringList MainWindow::importEdl() {
    if (!project_ || !binContainer_->bins()) {
        reportFailure(this, tr("Import EDL: open a project first."));
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
        reportFailure(this, tr("Import EDL: the bin cannot be opened."));
        return {};
    }
    interop::edl::ImportOptions options;
    options.binId = doc->nodeId();
    options.when = QDateTime::currentDateTimeUtc();
    options.rate = dialog.rate();
    QString error;
    auto imported = interop::edl::importFile(path, options, &error);
    if (!imported) {
        reportFailure(this, tr("Import EDL: %1").arg(error));
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
    // Console only: what was imported.
    ConsoleLog::instance().append(tr("Import EDL: %1: a sequence and %n new source(s)", "", static_cast<int>(ids.size()) - 1).arg(name));
    // "A message displays asking you to Decompose the sequence before trying to use it" (p.951):
    // its segments name the tapes' sources, which have no media until Decompose (later).
    reportFailure(this, tr("Import EDL: %1: the sequence uses its tapes' sources; Decompose it before using it.").arg(name));
    for (const QString& w : imported->warnings) reportFailure(this, tr("Import EDL: %1: %2").arg(name, w));
    if (BinView* view = binContainer_->viewFor(doc); view && !sequenceId.isEmpty()) view->selectIds({sequenceId});
    return ids;
}

// The bin's Text view columns and custom columns go out, so the log holds what the bin shows (p.129
// step 1: "click the Text tab to display all clip information").
bool MainWindow::prepareAafMedia(const QList<model::BinItem>& selected, const QList<model::BinItem>& clips, const QString& binId,
                                 const QString& aafPath, QList<model::BinItem>& items, QStringList& warnings, QString* error) {
    auto text = [&](const char* key) { return exportValue(QLatin1String(key)); };
    const QString videoMethod = text("videoExportMethod");
    const QString audioMethod = text("audioExportMethod");
    // The Simplified AAF's options (7.1c3, p.884-885): with the Video Mixdown method the whole picture is one mixdown
    // already, so Mixdown Video/Effects to V1 has nothing to do there.
    const bool simplifyVideo = exportFlag(QStringLiteral("mixdownEffectsToV1")) && videoMethod != QLatin1String("mixdown");
    const bool flattenAudio = exportFlag(QStringLiteral("flattenAudioEffects"));
    const QString trackCount = text("audioTrackCount");
    const int audioLimit = trackCount.isEmpty() || trackCount == QLatin1String("all") ? 0 : trackCount.toInt();
    items = selected + clips;
    if (videoMethod == QLatin1String("link") && audioMethod == QLatin1String("link") && !simplifyVideo && !flattenAudio &&
        audioLimit == 0)
        return true;
    if (!project_) {
        if (error) *error = tr("Open a project first.");
        return false;
    }
    // Media Destinations, Folder: "If you select Folder and then check Use Same Folder as AAF File, Media Composer stores
    // the media in the same folder as the exported AAF files" (p.871); otherwise the folder named (p.1177).
    const QString chosen = text("mediaFolder");
    const QString folder = exportFlag(QStringLiteral("useAafFolder")) || chosen.isEmpty()
                               ? QFileInfo(aafPath).absolutePath()
                               : chosen;
    if (!QDir().mkpath(folder)) {
        if (error) *error = tr("The media folder %1 cannot be made.").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    creation.videoDrive = folder;
    creation.audioDrive = folder;
    creation.raster = QSize(project_->settings.format.width, project_->settings.format.height).expandedTo(media::kMinimumRaster);
    QProgressDialog progress(tr("Preparing the media..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("exportProgress"));
    creation.progress = progressOn(progress);
    struct Done {
        QProgressDialog& p;
        ~Done() { p.reset(); }
    } done{progress};

    // Video Mixdown "Creates a new video mixdown track for the sequence"; without video edits it "replaces all of the
    // video tracks with a single video track named Video Mixdown" (p.871-872, p.1176). Made first, from the sequences
    // as they are, whose clips the bins hold.
    QHash<QString, model::BinItem> mixdownOf; // sequence id -> its mixdown clip
    if (videoMethod == QLatin1String("mixdown")) {
        for (const model::BinItem& item : selected) {
            if (item.kind != model::BinItemKind::Sequence) continue;
            const model::Sequence seq = model::Sequence::fromItem(item);
            QStringList video;
            for (const model::Track& t : seq.tracks()) {
                if (t.kind == model::TrackKind::Video) video << t.name();
            }
            if (video.isEmpty() || seq.duration() <= 0) continue;
            media::Composition c;
            c.sequence = &seq;
            c.tracks = video;
            c.size = creation.raster;
            model::LoadedClip loaded;
            loaded.binId = binId;
            loaded.item = item;
            fillComposition(c, loaded);
            QString why;
            const auto mix = media::renderedVideoClip(tr("%1 Video Mixdown").arg(item.name), item.editRate, seq.duration(),
                                                      [&](std::int64_t f) { return media::composeImage(c, f); }, creation,
                                                      model::BinItemKind::MasterClip, &why);
            if (!mix) {
                if (error) *error = tr("The video mixdown of %1 failed: %2").arg(item.name, why);
                return false;
            }
            mixdownOf.insert(item.id, *mix);
        }
    }

    // The Simplified AAF (p.884): "if a segment of the video is an existing master clip or filler, a reference to that
    // segment is added to the new sequence. If the segment is a transition or effect, a video mixdown occurs which creates
    // a new master clip" (model::simplifiedVideo); "Media Composer can limit the number of tracks included in the
    // exported sequence to the first 2, 4, 8, or 16 tracks. If an audio track contains at least one effect or gain
    // change, the entire track is mixed down to a new master clip." The new clips go into the sequence's bin ("added to
    // the project bin") and the AAF; the bin's sequence stays as it was (ours: the simplified one is only exported).
    QList<model::BinItem> simplified = selected;
    QList<model::BinItem> madeClips; // the Simplified AAF's mixdown clips
    if (simplifyVideo || flattenAudio || audioLimit > 0) {
        BinDocument* doc = binContainer_ ? binContainer_->document(binId) : nullptr;
        auto addClip = [&](const model::BinItem& clip) {
            if (doc) doc->addItem(clip);
            madeClips << clip;
        };
        for (model::BinItem& item : simplified) {
            if (item.kind != model::BinItemKind::Sequence) continue;
            const model::Sequence original = model::Sequence::fromItem(item);
            model::Sequence seq = original;
            if (simplifyVideo) {
                QStringList video;
                for (const model::Track& t : original.tracks()) {
                    if (t.kind == model::TrackKind::Video) video << t.name();
                }
                if (!video.isEmpty()) {
                    media::Composition c;
                    c.sequence = &original;
                    c.tracks = video;
                    c.size = creation.raster;
                    model::LoadedClip loaded;
                    loaded.binId = binId;
                    loaded.item = item;
                    fillComposition(c, loaded);
                    QList<model::Segment> v1;
                    int part = 0;
                    for (const model::SimplifiedSpan& span : model::simplifiedVideo(original)) {
                        const std::int64_t length = span.end - span.start;
                        if (span.kind == model::SimplifiedSpan::Kind::Clip) {
                            v1 << span.segment;
                            continue;
                        }
                        model::Segment segment;
                        segment.length = length;
                        if (span.kind == model::SimplifiedSpan::Kind::Mixdown) {
                            QString why;
                            const auto mix = media::renderedVideoClip(
                                tr("%1 Effects Mixdown %2").arg(item.name).arg(++part), item.editRate, length,
                                [&](std::int64_t f) { return media::composeImage(c, span.start + f); }, creation,
                                model::BinItemKind::MasterClip, &why);
                            if (!mix) {
                                if (error) *error = tr("The video mixdown of %1 failed: %2").arg(item.name, why);
                                return false;
                            }
                            addClip(*mix);
                            segment.binId = binId;
                            segment.clipId = mix->id;
                            segment.sourceMob = mix->mobId;
                            segment.name = mix->name;
                            segment.sourceTrack = QStringLiteral("V1");
                        }
                        v1 << segment;
                    }
                    for (const QString& name : video) seq.removeTrack(name);
                    if (model::Track* t = seq.ensureTrack(QStringLiteral("V1"))) t->segments = v1;
                }
            }
            // The audio tracks in number order; past the limit they are left out.
            QList<const model::Track*> audio;
            for (const model::Track& t : original.tracks()) {
                if (t.kind == model::TrackKind::Audio) audio << &t;
            }
            std::sort(audio.begin(), audio.end(), [](const model::Track* a, const model::Track* b) {
                const auto ra = model::parseTrackName(a->name());
                const auto rb = model::parseTrackName(b->name());
                return (ra ? ra->number : 0) < (rb ? rb->number : 0);
            });
            for (int i = 0; i < audio.size(); ++i) {
                const model::Track& t = *audio.at(i);
                if (audioLimit > 0 && i >= audioLimit) {
                    seq.removeTrack(t.name());
                    continue;
                }
                if (!flattenAudio || !model::audioTrackHasEffects(t) || t.length() <= 0) continue;
                media::MixdownRequest request;
                request.sequence = item;
                request.tracks = {t.name()};
                request.start = 0;
                request.end = t.length();
                request.format = t.format == model::AudioFormat::Stereo       ? media::MixdownFormat::Stereo
                                 : t.format == model::AudioFormat::Surround51 ? media::MixdownFormat::Surround51
                                 : t.format == model::AudioFormat::Surround71 ? media::MixdownFormat::Surround71
                                                                              : media::MixdownFormat::Mono;
                request.alternatingPan =
                    settings_->text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
                request.clips = [this](const QString& bin, const QString& id) { return composer_->resolve(bin, id); };
                QString why;
                const auto mix = media::mixdownClip(request, tr("%1 %2 Mixdown").arg(item.name, t.name()),
                                                    QString::fromStdString(Timecode(model::startFrameOf(item), model::timecodeFormatOf(item)).toString()), creation, &why);
                if (!mix) {
                    if (error) *error = tr("The audio mixdown of %1 %2 failed: %3").arg(item.name, t.name(), why);
                    return false;
                }
                addClip(*mix);
                model::Track* flat = seq.track(t.name());
                if (!flat) continue;
                model::Segment segment;
                segment.binId = binId;
                segment.clipId = mix->id;
                segment.sourceMob = mix->mobId;
                segment.name = mix->name;
                segment.sourceTrack = QStringLiteral("A1");
                segment.length = t.length();
                flat->segments = {segment};
                flat->inserts = {};
            }
            seq.storeIn(item);
        }
        for (model::BinItem& it : items) {
            for (const model::BinItem& changed : simplified) {
                if (changed.id == it.id) it = changed;
            }
        }
    }

    // Consolidate Media: the parts of the clips the sequences use, with the Handle Length (p.1175-1176), and selected
    // master clips whole, as new media in the folder. The new clips keep their old clips' names (ours).
    QList<model::BinItem> sequences;
    QList<model::BinItem> clipItems;
    for (const model::BinItem& item : items) (item.kind == model::BinItemKind::Sequence ? sequences : clipItems) << item;
    clipItems << madeClips;
    if (videoMethod == QLatin1String("consolidate") || audioMethod == QLatin1String("consolidate")) {
        media::ConsolidateOptions co;
        co.mode = media::ConsolidateOptions::Mode::Consolidate;
        co.handles = exportSetting(videoMethod == QLatin1String("consolidate") ? QStringLiteral("videoHandles")
                                                                                                    : QStringLiteral("audioHandles"))
                         .toInt();
        co.keepMastersLinked = true;
        co.creation = creation;
        const media::ConsolidateResult result =
            media::consolidate(simplified, binId, [this](const QString& bin, const QString& id) { return composer_->resolve(bin, id); }, {}, co);
        if (!result.error.isEmpty()) {
            if (error) *error = result.error;
            return false;
        }
        warnings << result.skipped;
        QHash<QString, model::BinItem> made;
        for (model::BinItem m : result.added) made.insert(m.id, m);
        // Sequences now naming the new clips; selected clips replaced by their copies.
        for (const model::BinItem& changed : result.changed) {
            for (model::BinItem& seq : sequences) {
                if (seq.id == changed.id) seq = changed;
            }
        }
        QList<model::BinItem> kept;
        for (const model::BinItem& clip : clipItems) {
            bool replaced = false;
            for (model::BinItem m : result.added) {
                if (!selected.isEmpty() && m.name.startsWith(clip.name + QStringLiteral(".new")) && m.sources.size() == clip.sources.size()) {
                    const bool whole = std::any_of(selected.begin(), selected.end(), [&](const model::BinItem& i) { return i.id == clip.id; });
                    if (!whole) continue;
                    m.name = clip.name;
                    kept << m;
                    made.remove(m.id);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) kept << clip;
        }
        clipItems = kept;
        // The new clips the sequences use, under their old names.
        for (model::BinItem m : made) {
            m.name = m.name.section(QStringLiteral(".new"), 0, 0);
            clipItems << m;
        }
        // Clips no sequence uses any more (their parts were consolidated) are left out, unless selected.
        QSet<QString> used;
        for (const model::BinItem& seqItem : sequences) {
            const model::Sequence seq = model::Sequence::fromItem(seqItem);
            for (const model::Track& t : seq.tracks()) {
                for (const model::Segment& seg : t.segments) {
                    if (!seg.isFiller()) used << seg.clipId;
                }
            }
        }
        QList<model::BinItem> still;
        for (const model::BinItem& clip : clipItems) {
            const bool picked = std::any_of(selected.begin(), selected.end(), [&](const model::BinItem& i) { return i.id == clip.id; });
            if (picked || used.contains(clip.id) || clip.name.isEmpty()) still << clip;
            else if (std::none_of(clips.begin(), clips.end(), [&](const model::BinItem& c) { return c.id == clip.id; })) still << clip;
        }
        clipItems = still;
    }

    // The mixdowns take the place of the sequences' video tracks.
    for (model::BinItem& seqItem : sequences) {
        const auto it = mixdownOf.constFind(seqItem.id);
        if (it == mixdownOf.constEnd()) continue;
        model::Sequence seq = model::Sequence::fromItem(seqItem);
        const model::Sequence original = seq;
        for (const model::Track& t : original.tracks()) {
            if (t.kind == model::TrackKind::Video) seq.removeTrack(t.name());
        }
        model::Track* v1 = seq.ensureTrack(QStringLiteral("V1"));
        if (!v1) continue;
        v1->customName = tr("Video Mixdown");
        model::Segment segment;
        segment.binId = binId;
        segment.clipId = it->id;
        segment.sourceMob = it->mobId;
        segment.name = it->name;
        segment.sourceTrack = QStringLiteral("V1");
        segment.length = it->durationFrames;
        v1->segments = {segment};
        seq.storeIn(seqItem);
        clipItems << *it;
    }

    // Export Media Difference Between Sequences (p.885-886): with Ask to Exclude Media from a Previous Sequence on a
    // Copy All Media tab, "In the Exclude Media Referenced from Sequence pulldown, select the sequence you want to compare
    // with the one you are exporting"; the files that sequence's clips use are not copied (their clips keep linking to
    // them, ours). The choice is among the open bins' other sequences.
    const bool excludeVideo = videoMethod == QLatin1String("copy") && exportFlag(QStringLiteral("videoExcludePrevious"));
    const bool excludeAudio = audioMethod == QLatin1String("copy") && exportFlag(QStringLiteral("audioExcludePrevious"));
    QSet<QString> exclude;
    if (excludeVideo || excludeAudio) {
        QList<model::BinItem> candidates;
        QStringList candidateBins;
        QStringList names;
        for (BinDocument* d : binContainer_ ? binContainer_->openDocuments() : QList<BinDocument*>{}) {
            for (const model::BinItem& i : d->bin().items) {
                if (i.kind != model::BinItemKind::Sequence) continue;
                if (std::any_of(selected.begin(), selected.end(), [&](const model::BinItem& s) { return s.id == i.id; })) continue;
                candidates << i;
                candidateBins << d->nodeId();
                names << i.name;
            }
        }
        if (candidates.isEmpty()) {
            warnings << tr("There is no other sequence to exclude media from; all the media is copied.");
        } else {
            int index = -1;
            if (excludeSequenceHook_) {
                index = excludeSequenceHook_(candidates);
            } else {
                bool ok = false;
                const QString name = QInputDialog::getItem(this, tr("Exclude Media Referenced from Sequence"),
                                                           tr("Exclude Media Referenced from Sequence:"), names, 0, false, &ok);
                if (ok) index = static_cast<int>(names.indexOf(name));
            }
            if (index < 0 || index >= candidates.size()) {
                if (error) *error = tr("The export was cancelled.");
                return false;
            }
            const model::Sequence previous = model::Sequence::fromItem(candidates.at(index));
            for (const model::Track& t : previous.tracks()) {
                for (const model::Segment& seg : t.segments) {
                    if (seg.isFiller()) continue;
                    const auto clip = composer_->resolve(seg.binId.isEmpty() ? candidateBins.at(index) : seg.binId, seg.clipId);
                    if (!clip) continue;
                    for (const model::ClipSource& src : clip->sources) {
                        if (src.kind != model::ClipSource::Kind::File || src.path.isEmpty()) continue;
                        const bool isVideo = src.track.startsWith(QLatin1Char('V'));
                        if ((isVideo && excludeVideo) || (!isVideo && excludeAudio)) exclude << QFileInfo(src.path).absoluteFilePath();
                    }
                }
            }
        }
    }

    // Copy All Media: the clips' files of each kind copied into the folder (p.1175).
    const media::CopiedMedia copied = media::copyClipMedia(clipItems, folder, videoMethod == QLatin1String("copy"),
                                                           audioMethod == QLatin1String("copy"), exclude);
    warnings << copied.warnings;
    items = sequences + copied.items;
    return true;
}

bool MainWindow::exportStills(const QString& path, const QString& format, const model::BinItem& sequenceItem,
                              const media::MovieSource& source, const model::LoadedClip* shown, std::int64_t start, std::int64_t end,
                              const std::function<bool(double)>& progress, QString* error) {
    auto text = [&](const char* key) { return exportValue(QLatin1String(key)); };
    auto flag = [&](const char* key) { return exportFlag(QLatin1String(key)); };
    media::StillOptions o;
    const QString graphic = text("graphicFormat");
    o.format = format == QLatin1String("dpx")       ? media::StillFormat::Dpx
               : format == QLatin1String("openexr") ? media::StillFormat::OpenExr
               : graphic == QLatin1String("jpeg")   ? media::StillFormat::Jpeg
               : graphic == QLatin1String("tiff")   ? media::StillFormat::Tiff
               : graphic == QLatin1String("bmp")    ? media::StillFormat::Bmp
               : graphic == QLatin1String("pict")   ? media::StillFormat::Pict
                                                    : media::StillFormat::Png;
    const QString size = text("graphicSize");
    if (format == QLatin1String("graphic") && size != QLatin1String("project"))
        o.size = QSize(size.section(QLatin1Char('x'), 0, 0).toInt(), size.section(QLatin1Char('x'), 1, 1).toInt());
    // "The Size to Fit suboption sizes to fit the specified width and height. The Crop/Pad suboption instructs Media
    // Composer not to scale or resize the frames" (p.1186).
    o.scaling = text("graphicFit") == QLatin1String("pad") ? media::MovieScaling::CenterKeepSize : media::MovieScaling::Stretch;
    o.legalRange = format == QLatin1String("graphic") && text("graphicLevels") == QLatin1String("legal");
    o.depth = format == QLatin1String("dpx")       ? text("dpxDepth").toInt()
              : format == QLatin1String("openexr") ? text("exrDepth").toInt()
                                                   : text("graphicDepth").toInt();
    o.quality = exportSetting(QStringLiteral("graphicQuality")).toInt();

    // Which frames, each with its file. A Graphic export without Sequential Files is one frame: "Mark an IN point to
    // export the marked frame from a bin or a monitor, or move the position indicator to the frame you want to export"
    // (p.868). Markers only: "images only for those frames that contain markers", named by their comments with Use
    // Comments (p.1186). DPX and OpenEXR are image sequences of the range, or one file for one frame (ours).
    QList<QPair<std::int64_t, QString>> frames;
    const bool sequential = format != QLatin1String("graphic") || flag("sequentialFiles");
    if (format == QLatin1String("graphic") && flag("markersOnly")) {
        QSet<QString> names;
        for (const model::Marker& m : model::markersOf(sequenceItem)) {
            if (m.frame < start || m.frame >= end) continue;
            QString to = media::numberedStillPath(path, m.frame);
            if (flag("useComments") && !m.comment.trimmed().isEmpty()) {
                QString name = m.comment.trimmed();
                name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
                // The same comment twice gets the frame number (ours).
                if (names.contains(name)) name += QStringLiteral(" %1").arg(m.frame);
                names << name;
                to = QFileInfo(path).absoluteDir().filePath(name + QLatin1Char('.') + media::stillSuffix(o.format));
            }
            frames << qMakePair(m.frame, to);
        }
        if (frames.isEmpty()) {
            if (error) *error = tr("There are no markers to export.");
            return false;
        }
    } else if (!sequential) {
        std::int64_t frame = shown ? shown->position : start;
        if (flag("useMarks") && shown && shown->in) frame = *shown->in;
        frames << qMakePair(std::clamp(frame, start, end - 1), path);
    } else if (end - start == 1 && format != QLatin1String("dpx")) {
        frames << qMakePair(start, path);
    } else if (format == QLatin1String("dpx")) {
        // DPX's options (p.891): the files in a subfolder; numbered from the Start Frame, or by the frames' timecode
        // counted in frames ("the sequence timecode is 02:00:10:00 would show the file name as name.173040.dpx"),
        // with the Digits in File Name; one frame is numbered too (ours).
        QString to = path;
        const QString sub = text("dpxSubfolder").trimmed();
        if (!sub.isEmpty()) {
            const QDir folder(QFileInfo(path).absoluteDir().filePath(sub));
            if (!QDir().mkpath(folder.absolutePath())) {
                if (error) *error = tr("The folder %1 cannot be made.").arg(QDir::toNativeSeparators(folder.absolutePath()));
                return false;
            }
            to = folder.filePath(QFileInfo(path).fileName());
        }
        const std::int64_t first = flag("dpxTimecodeFrames") ? model::startFrameOf(sequenceItem) + start
                                                             : static_cast<std::int64_t>(exportSetting(QStringLiteral("dpxStartFrame")).toDouble());
        const int digits = exportSetting(QStringLiteral("dpxDigits")).toInt();
        for (std::int64_t f = start; f < end; ++f) frames << qMakePair(f, media::numberedStillPath(to, first + f - start, digits > 0 ? digits : 7));
    } else {
        for (std::int64_t f = start; f < end; ++f) frames << qMakePair(f, media::numberedStillPath(path, f - start));
    }
    if (format == QLatin1String("dpx")) {
        o.legalRange = text("dpxLevels") == QLatin1String("video"); // "Enabling Video provides 64-940"
        o.dpxInputDevice = text("dpxInputDevice").left(32);
        const QString fps = text("dpxFps");
        o.dpxFrameRate = fps.isEmpty() || fps == QLatin1String("sequence") ? sequenceItem.editRate.toDouble() : fps.toDouble();
    }
    for (qsizetype i = 0; i < frames.size(); ++i) {
        const QImage picture = source.picture(frames.at(i).first - start);
        if (!media::writeStill(frames.at(i).second, picture, o, error)) return false;
        if (progress && !progress(static_cast<double>(i + 1) / static_cast<double>(frames.size()))) {
            if (error) *error = tr("The export was cancelled.");
            return false;
        }
    }
    return true;
}

bool MainWindow::exportMovieFile(const model::BinItem& item, const QString& binId, const QString& path, const QString& format,
                                 QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    if (!project_) return fail(tr("Open a project first."));
    // A sequence as it is; a clip through a sequence of its own (AutoSequence of the one clip).
    model::BinItem sequenceItem;
    model::Sequence seq;
    if (item.kind == model::BinItemKind::Sequence) {
        sequenceItem = item;
        seq = model::Sequence::fromItem(item);
    } else {
        const model::AutoSequenceResult made = model::autoSequence({item}, binId, false);
        if (!made.ok()) return fail(tr("Only sequences and clips can be exported as movies."));
        seq = made.sequence;
        sequenceItem = item;
        sequenceItem.kind = model::BinItemKind::Sequence;
        seq.storeIn(sequenceItem);
    }
    const Settings& s = *settings_;
    auto flag = [&](const char* key) { return exportFlag(QLatin1String(key)); };
    auto text = [&](const char* key) { return exportValue(QLatin1String(key)); };
    // Use Marks: the marks of the monitor the item is shown in ("Mark IN and OUT points to export the marked range from
    // a bin or a monitor. If you mark an IN point and no OUT point, Media Composer exports from the IN point to the end",
    // p.868); the whole item otherwise.
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    const model::LoadedClip* shown = nullptr;
    for (MonitorId m : {MonitorId::Record, MonitorId::Source}) {
        const model::LoadedClip* c = composer_->state(m).current();
        if (c && c->item.id == item.id && !shown) shown = c;
    }
    const bool inRecord = shown && shown == composer_->state(MonitorId::Record).current();
    if (flag("useMarks") && shown) {
        if (shown->in) start = std::clamp<std::int64_t>(*shown->in, 0, end);
        if (shown->out) end = std::clamp<std::int64_t>(*shown->out + 1, start, end);
    }
    if (end <= start) return fail(tr("There is nothing to export."));
    // Use Selected Tracks: "the tracks that are enabled in the Timeline" (p.1175), for the sequence in the Record
    // monitor; every track otherwise. Inactive audio tracks only with Include Inactive Audio Tracks.
    QStringList selected;
    if (flag("useSelectedTracks") && inRecord) selected = timeline_->patch().selectedRecordTracks(seq);
    // The Digital Cut tool's Sequence Track buttons name the tracks themselves.
    if (exportOverride_.contains(QStringLiteral("tracksOnly"))) {
        selected.clear();
        for (const QJsonValue& v : exportOverride_.value(QStringLiteral("tracksOnly")).toArray()) selected << v.toString();
        if (selected.isEmpty()) return fail(tr("There are no tracks to export."));
    }
    QStringList video, audio;
    const QStringList shownVideo = inRecord ? shownVideoTracks(MonitorId::Record, seq) : QStringList();
    for (const model::Track& t : seq.tracks()) {
        if (!selected.isEmpty() && !selected.contains(t.name())) continue;
        if (t.kind == model::TrackKind::Video && (!inRecord || shownVideo.contains(t.name()))) video << t.name();
        if (t.kind == model::TrackKind::Audio && (!t.inactive || flag("includeInactiveAudio"))) audio << t.name();
    }
    media::MovieExportOptions o;
    // MXF OP1a, XDCAM MXF OP1a and MXF OP1a (legacy) (7.2a, p.886-890): picture and sound always, with their own tab's
    // Video Compression or Video Format, Audio Format and Bit Depth.
    // AS-11 (7.2c) is an OP1a file of the MXF OP1a tab's compression, rewrapped by bmxtranswrap below.
    const bool as11 = format == QLatin1String("as11");
    // DCP (7.5, docs/spec/dcp.md): the picture and the mix handed to media::writeDcp below.
    const bool dcp = format == QLatin1String("dcp");
    // "sink": the frames and sound handed to movieSink_ (the Digital Cut, 7.3a) rather than written to a file.
    const bool sink = format == QLatin1String("sink");
    const bool mxf = format == QLatin1String("op1a") || format == QLatin1String("xdcam") || format == QLatin1String("op1aLegacy") || as11;
    o.container = format == QLatin1String("mp4")          ? media::MovieContainer::Mp4
                  : format == QLatin1String("op1a") || as11 ? media::MovieContainer::Mxf
                  : format == QLatin1String("xdcam")      ? media::MovieContainer::Xdcam
                  : format == QLatin1String("op1aLegacy") ? media::MovieContainer::MxfLegacy
                                                          : media::MovieContainer::Mov;
    const QString tracks = mxf ? QStringLiteral("both") : text("movieTracks");
    const bool movie = mxf || sink || dcp || format == QLatin1String("mov") || format == QLatin1String("mp4") || format == QLatin1String("qtMovie");
    const bool sound = format == QLatin1String("audio");
    o.video = movie ? tracks != QLatin1String("audio") && !video.isEmpty() : !sound;
    o.audio = movie ? tracks != QLatin1String("video") && !audio.isEmpty() : sound;
    if (movie && !o.video && !o.audio) return fail(tr("There are no tracks to export."));
    if (sound && audio.isEmpty()) return fail(tr("There are no audio tracks to export."));
    if (!movie && !sound && video.isEmpty()) return fail(tr("There are no video tracks to export."));
    o.compression = format == QLatin1String("op1a") || as11 ? text("mxfCompression")
                    : format == QLatin1String("xdcam")      ? text("xdcamFormat")
                    : format == QLatin1String("op1aLegacy") ? QStringLiteral("j2k")
                                                            : text("movieCompression");
    o.bitRateMbps = exportSetting(QStringLiteral("movieBitRate")).toInt();
    const QString size = mxf ? QStringLiteral("project") : text("movieImageSize");
    if (size != QLatin1String("project")) o.size = QSize(size.section(QLatin1Char('x'), 0, 0).toInt(), size.section(QLatin1Char('x'), 1, 1).toInt());
    const QString scaling = text("movieScaling");
    o.scaling = scaling == QLatin1String("stretch") ? media::MovieScaling::Stretch
                : scaling == QLatin1String("crop")  ? media::MovieScaling::CenterCrop
                : scaling == QLatin1String("keep")  ? media::MovieScaling::CenterKeepSize
                                                    : media::MovieScaling::Letterbox;
    o.fullRange = !mxf && text("movieColorLevels") == QLatin1String("full");
    // MXF's picture is the project's raster, squeezed into an XDCAM-25 or -17 raster (ours).
    if (mxf) o.scaling = media::MovieScaling::Stretch;
    const QString layout = mxf ? text("mxfAudioFormat") : text("movieAudioFormat");
    // The Audio export's Mono or Stereo (p.1185); a movie's Audio Format.
    const media::MixdownFormat mix = sound ? (text("audioFileChannels") == QLatin1String("mono") ? media::MixdownFormat::Mono
                                                                                               : media::MixdownFormat::Stereo)
                                     : layout == QLatin1String("mono")  ? media::MixdownFormat::Mono
                                     : layout == QLatin1String("5.1") ? media::MixdownFormat::Surround51
                                     : layout == QLatin1String("7.1") ? media::MixdownFormat::Surround71
                                                                      : media::MixdownFormat::Stereo;
    o.audioChannels = media::mixdownChannels(mix);
    o.aac = text("movieAudioCodec") == QLatin1String("aac");
    o.sampleRate = mxf ? 48000 : text("movieSampleRate").toInt();
    o.bitDepth = (mxf ? text("mxfBitDepth") : text("movieBitDepth")).toInt();
    // The item's timecode is embedded (p.881).
    o.startTimecode = QString::fromStdString(Timecode(model::startFrameOf(sequenceItem) + start, model::timecodeFormatOf(sequenceItem)).toString());

    // The picture through the compositor at the project's raster, as the Record monitor shows it.
    media::Composition c;
    c.sequence = &seq;
    c.tracks = video;
    c.size = QSize(project_->settings.format.width, project_->settings.format.height).expandedTo(media::kMinimumRaster);
    model::LoadedClip loaded;
    loaded.binId = binId;
    loaded.item = sequenceItem;
    if (shown) {
        loaded.in = shown->in;
        loaded.out = shown->out;
    }
    fillComposition(c, loaded);
    // The sound through the mixdown's mix: "All current Pan and Level settings in the sequence are carried to the
    // exported media" (p.863).
    media::MixdownRequest request;
    request.sequence = sequenceItem;
    request.tracks = audio;
    request.start = start;
    request.end = end;
    request.format = mix;
    request.alternatingPan = s.text(QStringLiteral("Audio"), QStringLiteral("defaultPan")) != QLatin1String("centered");
    request.clips = [this](const QString& bin, const QString& clipId) { return composer_->resolve(bin, clipId); };
    std::optional<media::MixdownRenderer> renderer;
    if (o.audio) renderer.emplace(request);
    media::MovieSource source;
    source.frames = end - start;
    source.frameRate = sequenceItem.editRate;
    source.raster = c.size;
    // Enable Mask Regions: "you can export your sequence with mask margins burned in" (p.1175, p.900).
    const model::MaskMargins burned = flag("enableMaskRegions") ? project_->settings.mask : model::MaskMargins{};
    source.picture = [&](std::int64_t f) { return media::burnMaskMargins(outputFrame(media::composeImage(c, start + f)), burned); };
    source.sound = [&](std::int64_t at, int count, std::vector<std::vector<float>>& out) { renderer->render(at, count, out); };
    QProgressDialog progress(tr("Exporting %1...").arg(item.name), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("exportProgress"));
    const std::function<bool(double)> step = progressOn(progress);
    bool ok = false;
    if (as11) {
        // AS-11 (p.891-893): the programme segments from the spanned markers within the exported range ("This
        // specification requires program segment markers"), the descriptive metadata from the AS-11 tab.
        media::As11Options a;
        a.shim = text("as11Shim");
        auto field = [&](const char* name, const char* key) { return qMakePair(QString::fromLatin1(name), text(key)); };
        a.core = {field("SeriesTitle", "as11SeriesTitle"), field("ProgrammeTitle", "as11ProgrammeTitle"),
                  field("EpisodeTitleNumber", "as11EpisodeTitleNumber"), field("ShimVersion", "as11ShimVersion"),
                  qMakePair(QStringLiteral("AudioTrackLayout"), QString::number(exportSetting(QStringLiteral("as11AudioTrackLayout")).toInt())),
                  field("PrimaryAudioLanguage", "as11PrimaryAudioLanguage"),
                  qMakePair(QStringLiteral("ClosedCaptionsPresent"),
                            exportFlag(QStringLiteral("as11ClosedCaptionsPresent")) ? QStringLiteral("true") : QStringLiteral("false"))};
        a.dpp = {field("ProductionNumber", "as11ProductionNumber"), field("Synopsis", "as11Synopsis"), field("Originator", "as11Originator"),
                 field("CopyrightYear", "as11CopyrightYear"), field("Distributor", "as11Distributor"), field("PictureRatio", "as11PictureRatio"),
                 field("CompletionDate", "as11CompletionDate"), field("ContactEmail", "as11ContactEmail"),
                 field("ContactTelephoneNumber", "as11ContactTelephoneNumber")};
        QList<model::Marker> spans;
        for (const model::Marker& m : model::markersOf(sequenceItem)) {
            if (m.isSpanned() && m.end() > start && m.frame < end) spans << m;
        }
        std::sort(spans.begin(), spans.end(), [](const model::Marker& x, const model::Marker& y) { return x.frame < y.frame; });
        for (const model::Marker& m : spans) {
            const std::int64_t from = std::max(m.frame, start);
            a.segments << media::As11Segment{from - start, std::min(m.end(), end) - from};
        }
        a.startFrame = model::startFrameOf(sequenceItem) + start;
        a.rate = sequenceItem.editRate;
        a.dropFrame = model::timecodeFormatOf(sequenceItem).dropFrame;
        if (a.segments.isEmpty()) {
            if (error) *error = tr("AS-11 needs programme segments: add spanned markers for them (p.414).");
        } else if (media::bmxTranswrapPath().isEmpty()) {
            if (error) *error = tr("AS-11 export needs bmx's bmxtranswrap, which was not found (BUILDING.md, \"AS-11\").");
        } else {
            QTemporaryDir scratch;
            const QString op1a = scratch.filePath(QStringLiteral("programme.mxf"));
            ok = media::exportMovie(op1a, source, o, step, error) && media::writeAs11(op1a, path, a, error);
        }
    } else if (dcp) {
        // A package folder named for the file chosen (ours: the guide only names the output, p.519).
        media::DcpOptions d;
        d.title = text("dcpTitle").isEmpty() ? sequenceItem.name : text("dcpTitle");
        d.kind = text("dcpKind");
        d.container = text("dcpContainer");
        d.fourK = flag("dcpFourK");
        d.channels = text("dcpSound") == QLatin1String("stereo") ? 2 : 6;
        if (!text("dcpIssuer").isEmpty()) d.issuer = text("dcpIssuer");
        if (!o.audio) source.sound = {};
        const QFileInfo chosen(path);
        ok = media::writeDcp(chosen.absoluteDir().filePath(chosen.completeBaseName()), source, d, step, error);
    } else if (sink) {
        ok = movieSink_ && movieSink_(source, o, step, error);
    } else if (movie) {
        ok = media::exportMovie(path, source, o, step, error);
    } else if (sound) {
        // Export Settings: Audio (p.1185).
        media::AudioFileOptions a;
        a.aiff = text("audioFileFormat") == QLatin1String("aiff");
        a.channels = o.audioChannels;
        a.sampleRate = text("audioFileRate").toInt();
        a.bitDepth = text("audioFileDepth").toInt();
        ok = media::exportAudioFile(path, source, a, step, error);
    } else {
        ok = exportStills(path, format, sequenceItem, source, shown, start, end, step, error);
    }
    progress.reset();
    return ok;
}

void MainWindow::exportBin() {
    BinView* view = binContainer_->activeView();
    if (!view) {
        reportFailure(this, tr("Export Bin: open the bin to export first."));
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
        reportFailure(this, tr("Export Bin: %1").arg(result.error));
        return;
    }
    // Console only: what was exported.
    ConsoleLog::instance().append(tr("Export Bin: %1: %n clip(s)", "", result.clips).arg(QDir::toNativeSeparators(path)));
    for (const QString& w : result.warnings) reportFailure(this, tr("Export Bin: %1").arg(w));
}

// "select a sequence in a bin, and when you open a new List Tool window, the old sequence will be
// populated by the selected sequence" (p.948 step 3); without one, the Record monitor's.
void MainWindow::showListTool() {
    showTool(QStringLiteral("ListTool"));
    // The cut lists count feet and frames by the project's film type (9.4a).
    if (project_) {
        listTool_->setFilmType(project_->settings.filmType);
        listTool_->setAspect(project_->settings.aspect.toDouble());
    }
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
QJsonObject MainWindow::importSettingsSnapshot() const {
    // The Import setting's options that importClipMedia() reads (p.1196-1199).
    QJsonObject o;
    for (const char* key : {"frameImportDuration", "convertSampleRate", "skipPullRates", "convertBitDepth", "bwfAlignment",
                            "centerPanMono", "alphaChannel", "dilateFill", "applyGain", "importGain", "cdOnly"}) {
        const QString k = QString::fromLatin1(key);
        o.insert(k, settings_->value(QStringLiteral("Import"), k));
    }
    return o;
}

std::optional<model::BinItem> MainWindow::importClipMedia(model::BinItem clip, const QJsonObject& importSettings,
                                                          media::MediaCreation creation, QString* error) {
    auto flag = [&](const char* key) { return importSettings.value(QString::fromLatin1(key)).toBool(); };
    const Rational editRate = linkOptions().editRate;
    const auto link = media::linkOf(clip);
    // A still picture lasts frameImportDuration seconds.
    if (link && link->videoStream >= 0 && link->channels == 0 && link->frames == 1) {
        const int seconds = std::max(importSettings.value(QStringLiteral("frameImportDuration")).toInt(), 1);
        clip.durationFrames =
            std::max<std::int64_t>(1, seconds * editRate.num() / std::max<std::int64_t>(editRate.den(), 1));
    }
    // The Audio tab (p.1198-1199), clip by clip.
    creation.relabelPullRates = false;
    creation.audioShift = 0;
    if (link && link->channels > 0) {
        const int sourceRate = clip.fields.value(QStringLiteral("Audio SR")).toInt();
        if (flag("convertSampleRate")) {
            // "Do not convert sources with pullup or pulldown rates".
            creation.relabelPullRates = flag("skipPullRates");
        } else if (media::isImportRate(sourceRate)) {
            // "Media Composer imports audio media at the source sample rate. If Media Composer does
            // not support the source sample rate, it autoconverts".
            creation.sampleRate = sourceRate;
        }
        if (!flag("convertBitDepth")) {
            const int bits = clip.fields.value(QStringLiteral("Audio Bit Depth")).section(QLatin1Char(' '), 0, 0).toInt();
            if (bits == 16 || bits == 24) creation.bitDepth = bits;
        }
        // "Subframe Alignment to Broadcast WAV Start Time" (p.1199): on the nearest frame.
        if (flag("bwfAlignment") && link->videoStream < 0 && sourceRate > 0) {
            const auto info = media::probe(link->audioFile(0));
            if (const auto tr = info ? media::bwfTimeReference(*info) : std::nullopt) {
                const media::BwfAlignment a = media::alignToFrame(*tr, sourceRate, clip.editRate);
                creation.audioShift = a.shift;
                clip.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(a.startFrame, TimecodeFormat::forRate(clip.editRate, false)).toString()));
            }
        }
    }
    auto made = media::transcodeClip(clip, 0, -1, creation, error);
    if (!made) return std::nullopt;
    // "Automatically center pan monophonic clips" (p.1198).
    if (flag("centerPanMono") && made->fields.value(QStringLiteral("Tracks")) == QLatin1String("A1")) model::centerClipPan(*made);
    // "Apply attenuation/gain effect on import" (p.215, p.1198): the clip's gain, as Clip > Audio > Apply Gain sets it
    // ("If you later apply gain from the Clip menu ... the pre-import gain is ignored"); with CD only, for files on a disc.
    if (flag("applyGain") && made->fields.value(QStringLiteral("Tracks")).contains(QLatin1Char('A')) &&
        (!flag("cdOnly") || media::isOnDisc(media::lastImportPath(clip)))) {
        model::applyClipGain(*made, importSettings.value(QStringLiteral("importGain")).toDouble());
    }
    made->sourceSettings.insert(QStringLiteral("import"), importSettings);
    return made;
}

QStringList MainWindow::importFiles(const QString& binId, const QStringList& allPaths, const QString& resolution) {
    // AAF files are compositions, not media (p.547); shot logs are clip information (p.131-132);
    // OpenTimelineIO files are timelines (p.969), and so are FCP 7 XML files (ours).
    QStringList paths;
    QStringList aaf;
    QStringList logs;
    QStringList timelines;
    QStringList fcp;
    QStringList captions;
    QStringList picts;
    QStringList cdTracks;
    for (const QString& p : allPaths) {
        (p.endsWith(QLatin1String(".aaf"), Qt::CaseInsensitive)   ? aaf
         : media::isPictFile(p)                                 ? picts
         : media::isCdaFile(p)                                  ? cdTracks
         : p.endsWith(QLatin1String(".scc"), Qt::CaseInsensitive) ? captions
         : interop::ale::isShotLog(p)                           ? logs
         : interop::otio::isOtioFile(p)                         ? timelines
         : interop::fcpxml::isFcpXmlFile(p)                     ? fcp
                                                                : paths)
            << p;
    }
    QStringList fromAaf = importAaf(binId, aaf) + importShotLogs(binId, logs) + importOtio(binId, timelines) + importFcpXml(binId, fcp) +
                          importScc(binId, captions);
    // Multilayer Photoshop files (9.2e, p.218-221): the message box once for all of them; flattened, a file imports as
    // any graphic (FFmpeg reads its merged picture), as does a file of one layer.
    QList<QPair<QString, media::PsdFile>> layered;
    PsdLayersChoice psdChoice = PsdLayersChoice::FlattenedImage;
    for (qsizetype i = paths.size() - 1; i >= 0; --i) {
        if (!media::isPsdFile(paths.at(i))) continue;
        const auto psd = media::readPsdFile(paths.at(i));
        if (psd && media::importableLayers(*psd).size() > 1) layered.prepend({paths.takeAt(i), *psd});
    }
    if (!layered.isEmpty()) {
        QStringList files;
        for (const auto& l : layered) files << l.first;
        psdChoice = askPsdLayersChoice(files);
        if (psdChoice == PsdLayersChoice::FlattenedImage) {
            for (const auto& l : layered) paths << l.first;
            layered.clear();
        } else if (psdChoice == PsdLayersChoice::Cancel) {
            layered.clear();
        }
    }
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || (paths.isEmpty() && picts.isEmpty() && cdTracks.isEmpty() && layered.isEmpty())) return fromAaf;
    // Audio CD tracks Windows shows as ".cda" files (9.2d): read off the disc into WAV files, imported as sound files and
    // named after their tracks.
    QTemporaryDir ripped;
    const QHash<QString, QString> fromCd = readCdTracks(cdTracks, ripped.path());
    for (auto it = fromCd.cbegin(); it != fromCd.cend(); ++it) paths << it.key();
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
    // The tracks read off a CD are imported from the ".cda" file as far as the bin knows.
    for (model::BinItem& clip : clips) {
        const QString wav = QDir::cleanPath(media::lastImportPath(clip));
        if (!fromCd.contains(wav)) continue;
        const QFileInfo cda(fromCd.value(wav));
        clip.fields.insert(QStringLiteral("Source File"), cda.fileName());
        clip.fields.insert(QStringLiteral("Source Path"), cda.absolutePath());
    }
    const QString imp = QStringLiteral("Import");
    // "Autodetect Broadcast Wave Monophonic Groups" (p.1199): name_1.wav, name_2.wav ... as one clip.
    if (settings_->flag(imp, QStringLiteral("autodetectBwfGroups"))) clips = media::groupMonoWavs(clips);
    // The Import settings as they are now, kept with each clip for Batch Import (p.235: "By default, Media Composer
    // imports the file using the Import settings from the last time it imported the file").
    const QJsonObject importSettings = importSettingsSnapshot();
    media::MediaCreation creation = mediaCreation(QStringLiteral("import"));
    if (const media::Resolution* r = media::findResolution(resolution)) creation.resolution = *r;
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("importProgress"));
    QStringList ids;
    // PICT files (p.1231), which FFmpeg does not read (9.2b).
    for (const QString& path : picts) {
        QString error;
        if (const auto made = importPictFile(path, importSettings, creation, *doc, &error)) {
            ids << made->id;
        } else {
            reportFailure(this, tr("Import: %1").arg(error));
        }
    }
    for (const auto& [path, psd] : layered) {
        QList<int> chosen;
        const QList<media::PsdLayer> layers = media::importableLayers(psd);
        if (psdChoice == PsdLayersChoice::SelectLayers) {
            QStringList names;
            for (const media::PsdLayer& l : layers) names << l.name;
            const auto picked = askPsdLayers(path, names);
            if (!picked) continue;
            chosen = *picked;
        } else {
            for (int n = 0; n < layers.size(); ++n) chosen << n;
        }
        progress.setLabelText(tr("Importing %1").arg(QFileInfo(path).fileName()));
        creation.progress = progressOn(progress);
        ids << importPsdLayers(path, psd, chosen, importSettings, creation, *doc);
        if (progress.wasCanceled()) break;
    }
    const media::AlphaHandling alpha = importAlphaHandling(importSettings);
    for (int i = 0; i < clips.size(); ++i) {
        const model::BinItem& clip = clips.at(i);
        progress.setLabelText(tr("Importing %1 (%2 of %3)").arg(clip.name).arg(i + 1).arg(clips.size()));
        const std::function<bool(double)> step = progressOn(progress);
        creation.progress = [&](double done) { return step((i + done) / static_cast<double>(clips.size())); };
        QString error;
        // A picture with an alpha channel is a Matte Key clip, without its sound (p.1196), unless the alpha is ignored.
        if (alpha != media::AlphaHandling::Ignore) {
            const auto link = media::linkOf(clip);
            if (const auto source = link && link->videoStream >= 0 ? media::alphaSourceOf(link->path) : std::nullopt) {
                if (const auto key = importMatteKeyClip(clip, *source, importSettings, creation, *doc, &error)) {
                    ids << key->id;
                } else {
                    reportFailure(this, tr("Import: %1: %2").arg(clip.name, error));
                    if (progress.wasCanceled()) break;
                }
                continue;
            }
        }
        const auto made = importClipMedia(clip, importSettings, creation, &error);
        if (!made) {
            reportFailure(this, tr("Import: %1: %2").arg(clip.name, error));
            if (progress.wasCanceled()) break;
            continue;
        }
        doc->addItem(*made);
        ids << made->id;
    }
    progress.reset();
    ids = fromAaf + ids;
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    if (skipped > 0) reportFailure(this, tr("Import: %n file(s) are not media that can be imported.", "", skipped));
    return ids;
}

// The dialog (from the Record monitor's sequence, its marks and the selected tracks), then the mix
// with a progress dialog, the new clip into its bin (after the premix copy, when asked for) and the
// clip overwritten into the targets, new tracks added when there are none.
void MainWindow::fillComposition(media::Composition& c, const model::LoadedClip& loaded) {
    c.source = [this](const model::Segment& segment, std::int64_t clipFrame, QSize want) -> std::optional<QImage> {
        auto clip = composer_->resolve(segment.binId, segment.clipId);
        if (clip) {
            // A mixed rate clip at the sequence's rate (motion_adapter.h).
            clip = model::clipForSegment(*clip, segment);
            if (auto field = motionFieldFrame(*clip, clipFrame, want)) return *field;
            // A motion effect clip shows its source clip's frames (FX Guide p.165).
            if (auto source = followMotion(*clip, clipFrame)) {
                clip = source->first;
                clipFrame = source->second;
            }
        }
        if (!clip || !media::isLinked(*clip)) return QImage();
        if (frames_->isOffline(*clip)) return std::nullopt;
        if (!segment.spatial) return adaptedFrame(*clip, clipFrame, want, std::nullopt, segment.color);
        if (framingPreview_ && segment == *framingPreview_ && segment.spatial->frameFlex) {
            return adaptedFrame(*clip, clipFrame, want, media::framingPreviewAdapter(*segment.spatial->frameFlex), segment.color);
        }
        // The FrameFlex effect's box at the frame (p.533): frames into the segment, counted from its source IN (ours:
        // a motion effect's speed is not taken into account).
        const double offset = std::clamp(static_cast<double>(clipFrame - segment.sourceIn), 0.0,
                                         static_cast<double>(std::max<std::int64_t>(segment.length - 1, 0)));
        return adaptedFrame(*clip, clipFrame, want, model::adapterAt(*segment.spatial, offset, segment.length), segment.color);
    };
    // For Timecode Burn-In: the sequence, its marks and the source clips (6.3d1).
    c.burnIn.sequence = loaded.item;
    c.burnIn.markIn = loaded.in;
    c.burnIn.markOut = loaded.out;
    c.burnIn.clip = [this](const model::Segment& segment) { return composer_->resolve(segment.binId, segment.clipId); };
    // Timewarps' Source and Output formats (6.5d3a): the project's scan and the media's field order.
    c.interlaced = project_ && project_->settings.format.scan == ScanMode::Interlaced;
    // Classic titles made at another raster show Wrong Format (FX Guide p.376).
    if (project_) c.raster = QSize(project_->settings.format.width, project_->settings.format.height);
    c.topFieldFirst = [this](const model::Segment& segment) {
        const auto clip = composer_->resolve(segment.binId, segment.clipId);
        return !clip || clip->link.value(QStringLiteral("fieldOrder")).toString() != QLatin1String("bottom");
    };
    // Rendered effects play from their precomputes (6.4a); offline ones are computed again.
    c.rendered = [this](const model::EffectRender& render, std::int64_t at, QSize want) -> std::optional<QImage> {
        const auto piece = model::renderedClipAt(render, at);
        if (!piece) return std::nullopt;
        auto clip = model::binItemFromJson(piece->first);
        const std::int64_t frame = piece->second;
        if (!clip) return std::nullopt;
        if (frames_->isOffline(*clip)) {
            // Media moved since: the media database may know where it is now.
            if (!mediaDb_.resolve(*clip) || frames_->isOffline(*clip)) return std::nullopt;
        }
        return frames_->frame(*clip, frame, want);
    };
    fillCompositionQuality(c);
}

void MainWindow::fillCompositionQuality(media::Composition& c) const {
    // The Render setting's Image Interpolation (p.1214).
    const QString interpolation = settings_->text(QStringLiteral("Render"), QStringLiteral("imageInterpolation"));
    c.interpolation = interpolation == QLatin1String("draft")      ? media::ImageInterpolation::Draft
                      : interpolation == QLatin1String("standard") ? media::ImageInterpolation::Standard
                                                                   : media::ImageInterpolation::Advanced;
}

void MainWindow::renderEffects(bool atPosition, bool rerender, bool useLastDrive) {
    auto fail = [this](const QString& text) {
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Render Effect"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("renderFailed"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || !project_) {
        fail(tr("Load a sequence into the Record monitor first."));
        return;
    }
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    // "Make sure the track containing the effect is selected" (p.58).
    const QStringList tracks = timeline_->patch().selectedRecordTracks(seq);
    std::int64_t from = 0;
    std::int64_t to = 0;
    const model::MonitorState& state = composer_->state(MonitorId::Record);
    if (atPosition) {
        from = state.position();
        to = from + 1;
    } else {
        // "Mark an IN point before the start of the first effect ... and then mark an OUT point after
        // the last" (p.59).
        if (!state.in() && !state.out()) {
            fail(tr("Mark IN and OUT points around the effects to render."));
            return;
        }
        from = state.markedStart();
        to = state.markedEnd() + 1;
    }
    QList<model::RenderTarget> targets;
    for (const model::RenderTarget& t : model::renderTargets(seq, tracks, from, to)) {
        if (rerender || !model::isRendered(seq, t)) targets << t;
    }
    if (targets.isEmpty()) {
        fail(tr("There are no effects to render on the selected tracks."));
        return;
    }
    media::MediaCreation creation = mediaCreation(QStringLiteral("render"));
    if (useLastDrive && !lastRenderDrive_.isEmpty()) {
        creation.videoDrive = lastRenderDrive_;
    } else {
        RenderEffectDialog dialog(static_cast<int>(targets.size()), lastRenderDrive_.isEmpty() ? creation.videoDrive : lastRenderDrive_, this);
        const bool ok = renderHook_ ? renderHook_(dialog) : dialog.exec() == QDialog::Accepted;
        if (!ok) return;
        creation.videoDrive = dialog.drive();
        lastRenderDrive_ = creation.videoDrive;
        lastRenderBackground_ = dialog.background();
    }
    renderTargetsNow(*rec, seq, targets, creation, rerender, lastRenderBackground_);
}

// The Render tab's Effects Processing (p.151-152): "Automatic ... Effects render at 16-bit for 10-bit
// media and at 8-bit for 8-bit media" (ours: by the render resolution's depth); 32-bit is 2026.8's.
media::EffectPrecision MainWindow::renderPrecision(const media::MediaCreation& creation) const {
    const QString processing = settings_->text(QStringLiteral("Media Creation"), QStringLiteral("renderEffectsProcessing"));
    if (processing == QLatin1String("8bit")) return media::EffectPrecision::Bits8;
    if (processing == QLatin1String("16bit")) return media::EffectPrecision::Bits16;
    if (processing == QLatin1String("32bit")) return media::EffectPrecision::Bits32;
    return creation.resolution.pixelFormat.contains(QLatin1String("10")) ? media::EffectPrecision::Bits16
                                                                        : media::EffectPrecision::Bits8;
}

void MainWindow::renderTargetsNow(const model::LoadedClip& loaded, const model::Sequence& seq,
                                  const QList<model::RenderTarget>& targets, media::MediaCreation creation, bool rerender,
                                  bool background) {
    // Timewarps take the Render setting's type first; then the render starts again from the sequence
    // as it is now.
    if (applyTimewarpRenderTypes(targets)) {
        if (const model::LoadedClip* now = composer_->state(MonitorId::Record).current()) {
            renderTargetsNow(*now, model::Sequence::fromItem(now->item), targets, std::move(creation), rerender, background);
        }
        return;
    }
    if (background) {
        renderInBackground(loaded, seq, targets, std::move(creation), rerender);
        return;
    }
    const model::LoadedClip* rec = &loaded;
    auto fail = [this](const QString& text) {
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Render Effect"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("renderFailed"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    media::Composition c;
    c.sequence = &seq;
    fillComposition(c, *rec);
    c.precision = renderPrecision(creation);
    // The renders, made one after the other and kept together. A partial render in date is finished:
    // "Media Composer then renders only the part of the effect that has not rendered previously and
    // saves that part as a separate precompute" (p.150); Shift starts again from the first frame.
    // "Press Ctrl+period ... to interrupt the render" (p.150): Cancel and Ctrl+period ask Keep or
    // Discard for the material already rendered.
    QProgressDialog progress(tr("Rendering..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("renderProgress"));
    auto* interrupt = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period), &progress);
    connect(interrupt, &QShortcut::activated, &progress, &QProgressDialog::cancel);
    QList<std::pair<model::RenderTarget, model::EffectRender>> made;
    bool interrupted = false;
    for (qsizetype i = 0; i < targets.size() && !interrupted; ++i) {
        const model::RenderTarget& target = targets.at(i);
        const auto share = static_cast<double>(targets.size());
        const std::function<bool(double)> step = progressOn(progress);
        creation.progress = [step, i, share](double done) { return step((static_cast<double>(i) + done) / share); };
        model::EffectRender earlier;
        std::int64_t start = 0;
        if (!rerender && model::isPartlyRendered(seq, target)) {
            earlier = *model::renderOf(seq, target);
            start = model::renderedFrames(earlier);
        }
        QString error;
        const auto render = media::renderEffect(c, target, rec->item.name, rec->item.editRate, creation, &error, start, true);
        interrupted = progress.wasCanceled();
        if (!render) {
            if (interrupted) break; // nothing of this one was made
            progress.reset();
            fail(error);
            return;
        }
        model::EffectRender whole = *render;
        whole.clips = earlier.clips + render->clips;
        made.append({target, whole});
    }
    progress.reset();
    if (interrupted && !made.isEmpty()) {
        // "Click Keep to save the material already rendered. Click Discard to end the render without
        // saving the material already rendered" (p.150).
        const bool keep = renderInterruptHook_ ? renderInterruptHook_()
                                               : [this] {
                                                     QMessageBox box(QMessageBox::Question, tr("Render Effect"),
                                                                     tr("The render was interrupted."), QMessageBox::NoButton, this);
                                                     QPushButton* k = box.addButton(tr("Keep"), QMessageBox::AcceptRole);
                                                     box.addButton(tr("Discard"), QMessageBox::RejectRole);
                                                     box.exec();
                                                     return box.clickedButton() == k;
                                                 }();
        if (!keep) {
            // The precomputes this render made go (the earlier parts of partial renders stay).
            QStringList files;
            for (const auto& [target, render] : made) {
                const qsizetype before = render.clips.size() - 1;
                const auto item = model::binItemFromJson(render.clips.at(before));
                if (item) files << item->link.value(QStringLiteral("path")).toString();
            }
            media::deleteMediaFiles(files);
            return;
        }
    } else if (interrupted) {
        return;
    }
    QList<model::EffectRender> renders;
    QList<model::EffectRender> replaced;
    for (const auto& [target, render] : made) {
        renders << render;
        // A render of another proxy mode stays with the effect (EffectRender::kept, p.527), its media too.
        if (const auto before = model::renderOf(seq, target); before && before->proxy == render.proxy) replaced << *before;
    }
    timeline_->changeRecordSequence(tr("Render Effect"), [&made](model::Sequence& s) {
        bool any = false;
        for (const auto& [target, render] : made) any = model::setRender(s, target, render) || any;
        return any;
    });
    keepPrecomputes(rec->binId, renders, replaced);
    // Console only: how many effects were rendered (or kept, when the user stopped it).
    ConsoleLog::instance().append(interrupted ? tr("Render Effect: interrupted; %n effect(s) kept.", "", static_cast<int>(made.size()))
                                              : tr("Render Effect: %n effect(s) rendered.", "", static_cast<int>(made.size())));
    renderFinished(!interrupted);
}

bool MainWindow::applyTimewarpRenderTypes(const QList<model::RenderTarget>& targets) {
    const QString renderUsing = settings_->text(QStringLiteral("Render"), QStringLiteral("timewarpsRenderUsing"));
    if (renderUsing.isEmpty() || renderUsing == QLatin1String("original")) return false;
    return timeline_->changeRecordSequence(tr("Timewarp Render Type"), [&](model::Sequence& s) {
        bool any = false;
        for (const model::RenderTarget& target : targets) {
            if (target.transition) continue;
            model::Track* t = s.track(target.track);
            if (!t || target.segment >= t->segments.size() || !t->segments[target.segment].effect) continue;
            model::Effect& e = *t->segments[target.segment].effect;
            if (const auto type = model::renderTypeFor(e, renderUsing)) {
                model::Timewarp tw = model::timewarpOf(e);
                tw.type = *type;
                model::setTimewarp(e, tw);
                any = true;
            }
        }
        return any;
    });
}

void MainWindow::renderFinished(bool completed) {
    // "Save after Render: When this option is selected, Media Composer saves open bins after you
    // perform a render operation" (p.1208).
    if (settings_->flag(QStringLiteral("Media Creation"), QStringLiteral("saveAfterRender"))) {
        QString error;
        if (!binContainer_->saveAll(&error)) reportFailure(this, tr("Save after Render: %1").arg(error));
    }
    if (completed) playRenderCompletionSound();
}

// "Select Background Process ... The render operation is performed in the background. You can continue
// working without having to wait for the background operation to complete. When the effects are
// rendered, the sequence will link to the new pre-computes and the effect icons in the Timeline will
// reflect the rendered state" (FX Guide p.59); the media goes in "drive:AvidMediaFiles/MXF/__bg.1"
// (p.59). The job runs on the Background Queue's thread, so everything it reads is copied first: the
// sequence, its clips (resolved now), and a frame cache of its own. On completion each render goes to
// the effect it was made from, found again by its key, wherever the sequence is now.
void MainWindow::renderInBackground(const model::LoadedClip& loaded, const model::Sequence& seq,
                                    const QList<model::RenderTarget>& targets, media::MediaCreation creation, bool rerender) {
    auto sequence = std::make_shared<const model::Sequence>(seq);
    // The clips the sequence uses, by bin and clip id.
    auto clips = std::make_shared<QHash<QString, model::BinItem>>();
    auto key = [](const QString& binId, const QString& clipId) { return binId + QLatin1Char('\n') + clipId; };
    for (const model::Track& t : seq.tracks()) {
        for (const model::Segment& segment : t.segments) {
            if (segment.isFiller() || clips->contains(key(segment.binId, segment.clipId))) continue;
            if (const auto clip = composer_->resolve(segment.binId, segment.clipId)) {
                clips->insert(key(segment.binId, segment.clipId), *clip);
                // A motion effect clip's source, which it shows.
                if (const auto motion = model::motionEffectOf(*clip)) {
                    if (const auto source = composer_->resolve(motion->sourceBin, motion->sourceClip)) {
                        clips->insert(key(motion->sourceBin, motion->sourceClip), *source);
                    }
                }
            }
        }
    }
    auto cache = std::make_shared<media::FrameCache>();
    media::Composition c;
    c.sequence = sequence.get();
    c.source = [clips, cache, key](const model::Segment& segment, std::int64_t clipFrame, QSize want) -> std::optional<QImage> {
        auto it = clips->constFind(key(segment.binId, segment.clipId));
        if (it == clips->cend()) return QImage();
        if (const auto motion = model::motionEffectOf(*it); motion && !media::isLinked(*it)) {
            it = clips->constFind(key(motion->sourceBin, motion->sourceClip));
            if (it == clips->cend()) return QImage();
            clipFrame = std::clamp<std::int64_t>(motion->sourceFrame(clipFrame), 0, std::max<std::int64_t>(it->durationFrames - 1, 0));
        }
        // A group clip's angle, a mixed rate clip at the sequence's rate (motion_adapter.h).
        const model::BinItem clip = model::clipForSegment(*it, segment);
        if (!media::isLinked(clip)) return QImage();
        if (!cache->isOnline(clip)) return std::nullopt;
        return cache->frame(clip, clipFrame, want);
    };
    c.burnIn.sequence = loaded.item;
    c.burnIn.markIn = loaded.in;
    c.burnIn.markOut = loaded.out;
    c.interlaced = project_ && project_->settings.format.scan == ScanMode::Interlaced;
    c.topFieldFirst = [clips, key](const model::Segment& segment) {
        const auto it = clips->constFind(key(segment.binId, segment.clipId));
        return it == clips->cend() || it->link.value(QStringLiteral("fieldOrder")).toString() != QLatin1String("bottom");
    };
    c.burnIn.clip = [clips, key](const model::Segment& segment) -> std::optional<model::BinItem> {
        const auto it = clips->constFind(key(segment.binId, segment.clipId));
        if (it == clips->cend()) return std::nullopt;
        return *it;
    };
    c.rendered = [cache](const model::EffectRender& render, std::int64_t at, QSize want) -> std::optional<QImage> {
        const auto piece = model::renderedClipAt(render, at);
        if (!piece) return std::nullopt;
        const auto clip = model::binItemFromJson(piece->first);
        if (!clip || !cache->isOnline(*clip)) return std::nullopt;
        return cache->frame(*clip, piece->second, want);
    };
    // The Image Interpolation and Effects Processing of now.
    fillCompositionQuality(c);
    c.precision = renderPrecision(creation);
    creation.folder = QStringLiteral("__bg.1");
    creation.progress = {};

    const QString name = loaded.item.name;
    const Rational rate = loaded.item.editRate;
    // The renders made, each with the target it was made for, written by the worker and read once
    // the job is done (the queue's done flag orders the two).
    auto made = std::make_shared<QList<std::pair<model::RenderTarget, model::EffectRender>>>();
    BackgroundQueue::Work work = [sequence, c, targets, name, rate, creation, rerender, made](const std::function<bool(double)>& progress) {
        media::ConsolidateResult result;
        media::MediaCreation mc = creation;
        for (qsizetype i = 0; i < targets.size(); ++i) {
            const model::RenderTarget& target = targets.at(i);
            const auto share = static_cast<double>(targets.size());
            mc.progress = [&progress, i, share](double done) { return progress((static_cast<double>(i) + done) / share); };
            model::EffectRender earlier;
            std::int64_t start = 0;
            if (!rerender && model::isPartlyRendered(*sequence, target)) {
                earlier = *model::renderOf(*sequence, target);
                start = model::renderedFrames(earlier);
            }
            QString error;
            const auto render = media::renderEffect(c, target, name, rate, mc, &error, start, false);
            if (!render) {
                result.error = error;
                break;
            }
            model::EffectRender whole = *render;
            whole.clips = earlier.clips + render->clips;
            made->append({target, whole});
        }
        return result;
    };
    const QString binId = loaded.binId;
    const QString sequenceId = loaded.item.id;
    backgroundQueue_->add(tr("Render: %1").arg(name), binId, std::move(work), BackgroundQueue::Priority::Normal,
                          [this, binId, sequenceId, made](const media::ConsolidateResult& result) {
                              applyBackgroundRenders(binId, sequenceId, *made);
                              if (!result.error.isEmpty()) reportFailure(this, tr("Render Effect: %1").arg(result.error));
                              renderFinished(result.error.isEmpty());
                          });
    // Console only: how many effects were queued.
    ConsoleLog::instance().append(tr("Render Effect: %n effect(s) queued in the background.", "", static_cast<int>(targets.size())));
}

void MainWindow::applyBackgroundRenders(const QString& binId, const QString& sequenceId,
                                        const QList<std::pair<model::RenderTarget, model::EffectRender>>& made) {
    if (made.isEmpty()) return;
    // The effect each render was made from: the target of the same track whose key is the render's
    // (the sequence may have been edited meanwhile; a render that no longer matches is not linked).
    auto link = [&made](model::Sequence& s, QList<model::EffectRender>* linked, QList<model::EffectRender>* replaced) {
        bool any = false;
        for (const auto& [target, render] : made) {
            for (const model::RenderTarget& t : model::renderTargets(s, {target.track}, 0, s.duration())) {
                if (model::renderKey(s, t) != render.key) continue;
                if (replaced) {
                    if (const auto before = model::renderOf(s, t); before && before->proxy == render.proxy) replaced->append(*before);
                }
                if (model::setRender(s, t, render)) {
                    any = true;
                    if (linked) linked->append(render);
                }
                break;
            }
        }
        return any;
    };
    QList<model::EffectRender> linked;
    QList<model::EffectRender> replaced;
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (rec && rec->item.id == sequenceId) {
        // In the Record monitor: one undo step, as a render in the application is.
        model::Sequence probe = model::Sequence::fromItem(rec->item);
        link(probe, &linked, &replaced);
        timeline_->changeRecordSequence(tr("Render Effect"), [&](model::Sequence& s) { return link(s, nullptr, nullptr); });
    } else if (BinDocument* doc = binContainer_->document(binId)) {
        // In its bin only: the bin item changes.
        if (const model::BinItem* item = doc->bin().item(sequenceId)) {
            model::Sequence s = model::Sequence::fromItem(*item);
            if (link(s, &linked, &replaced)) {
                model::BinItem changed = *item;
                s.storeIn(changed);
                doc->replaceItem(changed);
            }
        }
    }
    // Precomputes of renders that matched nothing still go in the bin, to be found and deleted.
    QList<model::EffectRender> all;
    for (const auto& [target, render] : made) all << render;
    keepPrecomputes(binId, all, replaced);
    // Console only: how many effects were rendered.
    ConsoleLog::instance().append(tr("Render Effect: %n effect(s) rendered in the background.", "", static_cast<int>(linked.size())));
}

void MainWindow::keepPrecomputes(const QString& binId, const QList<model::EffectRender>& renders,
                                 const QList<model::EffectRender>& replaced) {
    BinDocument* doc = binId.isEmpty() ? nullptr : binContainer_->document(binId);
    QSet<QString> kept;
    for (const model::EffectRender& render : renders) {
        for (const QJsonObject& json : render.clips) {
            auto item = model::binItemFromJson(json);
            if (!item) continue;
            kept.insert(item->id);
            if (!doc || doc->bin().item(item->id)) continue; // an earlier part of a partial render
            // Not placed by the user: shown only with Show reference clips (p.155, p.288).
            item->userPlaced = false;
            doc->addItem(*item);
            unsavedPrecomputes_.insert(item->id, binId);
        }
    }
    // Saving the bin ends the time its precomputes may be deleted. Watched once per open bin: the mark
    // goes with the document.
    static const char* const kWatched = "lumenisPrecomputesWatched";
    if (doc && !doc->property(kWatched).toBool()) {
        doc->setProperty(kWatched, true);
        connect(doc, &BinDocument::saved, this, [this, binId] {
            for (auto it = unsavedPrecomputes_.begin(); it != unsavedPrecomputes_.end();) {
                it = it.value() == binId ? unsavedPrecomputes_.erase(it) : std::next(it);
            }
        });
    }
    // "Media Composer overwrites or automatically deletes a media file from your drive only if you:
    // Render an effect two or more times with the Render Effect button ... deletes the earlier,
    // unreferenced versions. This autodeletion applies only to effects rendered since the last save"
    // (FX Guide p.154-155). Ours: when the render is replaced; Undo then shows the effect unrendered.
    // A precompute whose files could not all be deleted (Windows keeps open files) stays in the bin,
    // so its media can still be found and deleted there.
    QHash<QString, QStringList> filesOf; // precompute id -> its files
    QStringList files;
    for (const model::EffectRender& render : replaced) {
        for (const QJsonObject& json : render.clips) {
            const auto item = model::binItemFromJson(json);
            if (!item || kept.contains(item->id) || !unsavedPrecomputes_.contains(item->id)) continue;
            const auto link = media::linkOf(*item);
            filesOf.insert(item->id, link ? link->files() : QStringList{});
            files += filesOf.value(item->id);
            unsavedPrecomputes_.remove(item->id);
        }
    }
    QStringList deleted;
    if (!files.isEmpty()) {
        frames_->clear();
        deleted = media::deleteMediaFiles(files, &mediaDb_);
    }
    QStringList gone;
    for (auto it = filesOf.cbegin(); it != filesOf.cend(); ++it) {
        const QStringList& own = it.value();
        if (std::all_of(own.begin(), own.end(), [&](const QString& f) { return deleted.contains(f); })) gone << it.key();
    }
    if (doc && !gone.isEmpty()) doc->removeItems(gone);
}

// "None: Disables the rendering completion sound. This is the default. System Beep: ... matches the
// sound set for your operating system. Render Sound: ... a customized sound" (p.1215-1216). Ours: the
// Render Sound is two short rising tones through the audio output.
void MainWindow::playRenderCompletionSound() {
    const QString sound = settings_->text(QStringLiteral("Render"), QStringLiteral("renderCompletionSound"));
    if (sound != QLatin1String("systemBeep") && sound != QLatin1String("renderSound")) return;
    if (renderSoundHook_) {
        renderSoundHook_(sound);
        return;
    }
    if (sound == QLatin1String("systemBeep")) {
        QApplication::beep();
        return;
    }
    if (composer_->isPlaying() || playingTone_) return; // not over playback
    std::vector<media::AudioPiece> notes(2);
    const std::int64_t length = media::kPlaybackRate / 6;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i].tone = media::Tone{-14, i == 0 ? 880 : 1320};
        notes[i].at = static_cast<std::int64_t>(i) * length;
        notes[i].length = length;
    }
    audio_->stop();
    if (!audio_->start(std::make_shared<media::AudioMix>(notes), 0, {})) return;
    // The device is let go once the notes are over, unless something else plays by then.
    QTimer::singleShot(500, this, [this] {
        if (!composer_->isPlaying() && !playingTone_ && !scrubbing_) audio_->stop();
    });
}

BinDocument* MainWindow::binForNewClip(const QString& binId) {
    if (BinDocument* doc = binId.isEmpty() ? nullptr : binContainer_->document(binId); doc && !doc->isReadOnly()) return doc;
    QList<BinDocument*> open;
    for (BinDocument* doc : binContainer_->openDocuments()) {
        if (!doc->isReadOnly()) open << doc;
    }
    if (open.isEmpty()) return nullptr;
    if (BinDocument* active = binContainer_->activeDocument(); active && open.contains(active)) {
        open.removeAll(active);
        open.prepend(active);
    }
    if (open.size() == 1) return open.first();
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
    return index >= 0 && index < open.size() ? open.at(index) : nullptr;
}

void MainWindow::placeMotionClip(const QString& sourceBin, const model::BinItem& item) {
    BinDocument* doc = binForNewClip(sourceBin);
    if (!doc) {
        reportFailure(this, tr("Motion Effect: open a bin for the new clip."));
        return;
    }
    doc->addItem(item);
    if (BinView* view = binContainer_->viewFor(doc)) view->selectIds({item.id});
    // "A new clip appears in the Source monitor and in the current bin" (FX Guide p.167).
    composer_->loadDefault(doc->nodeId(), item);
}

// "Cue the clip to the frame that you want to freeze. Select Composer > Freeze Frame ... Select Other,
// type a custom duration in the dialog box ... A new clip appears in the Source monitor and in the
// current bin preceded by a Motion Effect icon. The new clip has the original clip name followed by the
// letters FF" (FX Guide p.166-167). Ours: no drive is asked for, since the clip has no media of its own
// until it is rendered.
std::optional<model::BinItem> MainWindow::freezeFrame() {
    const model::LoadedClip* src = composer_->state(MonitorId::Source).current();
    if (!src || src->item.kind == model::BinItemKind::Sequence || !frameAt(MonitorId::Source, src->position)) {
        reportFailure(this, tr("Freeze Frame: load a clip with picture into the Source monitor."));
        return std::nullopt;
    }
    // Two seconds until changed (ours; the guide gives no default).
    const std::int64_t twoSeconds = std::llround(2.0 * src->item.editRate.toDouble());
    FreezeFrameDialog dialog(src->item.editRate, lastFreezeDuration_ > 0 ? lastFreezeDuration_ : twoSeconds, this);
    const bool ok = freezeHook_ ? freezeHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || !dialog.duration()) return std::nullopt;
    lastFreezeDuration_ = *dialog.duration();
    // A freeze frame of a motion effect clip freezes the source frame it shows.
    model::BinItem source = src->item;
    QString sourceBin = src->binId;
    std::int64_t frame = src->position;
    if (const auto motion = model::motionEffectOf(source); motion && !media::isLinked(source)) {
        if (const auto clip = composer_->resolve(motion->sourceBin, motion->sourceClip)) {
            frame = motion->sourceFrame(frame);
            source = *clip;
            sourceBin = motion->sourceBin;
        }
    }
    model::BinItem item = model::freezeFrameClip(source, sourceBin, frame, *dialog.duration(),
                                                  model::FieldMotion::DuplicatedField, QDateTime::currentDateTimeUtc());
    item.name = src->item.name + QStringLiteral(" FF");
    placeMotionClip(src->binId, item);
    return item;
}

std::optional<model::BinItem> MainWindow::motionEffect() {
    // "If you do not activate the Source monitor, clicking the Motion Effect button does not open the
    // Motion Effect dialog box. If the Record monitor is active, clicking the Motion Effect button opens
    // the Motion Effect Editor for Timewarp effects" (p.168).
    if (composer_->activeMonitor() != MonitorId::Source) {
        showTool(QStringLiteral("MotionEffectEditor"));
        motionEditor_->refresh();
        return std::nullopt;
    }
    const model::LoadedClip* src = composer_->state(MonitorId::Source).current();
    if (!src || src->item.kind == model::BinItemKind::Sequence || !frameAt(MonitorId::Source, 0)) {
        reportFailure(this, tr("Motion Effect: load a clip with picture into the Source monitor."));
        return std::nullopt;
    }
    const model::MonitorState& state = composer_->state(MonitorId::Source);
    // "If you want to use only part of the source clip for the motion effect, mark IN and OUT points in
    // the Source monitor" (p.168).
    const std::int64_t in = state.in() ? *state.in() : 0;
    const std::int64_t out = state.out() ? *state.out() : src->item.durationFrames - 1;
    if (out < in) return std::nullopt;
    std::optional<std::int64_t> fit;
    const model::MonitorState& record = composer_->state(MonitorId::Record);
    if (record.current() && record.in() && record.out() && *record.out() >= *record.in()) fit = *record.out() - *record.in() + 1;
    // The Motion Effects tab's drive; "The resolution options are not available in the Motion Effects
    // tab" (Editing Guide p.1207): ours, the Render tab's.
    media::MediaCreation creation = mediaCreation(QStringLiteral("motionEffects"));
    if (const media::Resolution* r = media::findResolution(settings_->text(QStringLiteral("Media Creation"), QStringLiteral("renderResolution")))) {
        creation.resolution = *r;
    }
    MotionEffectDialog dialog(out - in + 1, src->item.editRate, fit, creation.videoDrive, this);
    if (motionHook_) {
        motionHook_(dialog);
    } else {
        dialog.exec();
    }
    if (dialog.choice() == MotionEffectDialog::Choice::Cancel) return std::nullopt;
    // Made from what the Source monitor shows: a motion effect clip's source and frames when it is one.
    model::BinItem source = src->item;
    QString sourceBin = src->binId;
    std::int64_t from = in;
    std::int64_t to = out;
    if (const auto motion = model::motionEffectOf(source); motion && !media::isLinked(source)) {
        if (const auto clip = composer_->resolve(motion->sourceBin, motion->sourceClip)) {
            from = motion->sourceFrame(in);
            to = motion->sourceFrame(out);
            if (to < from) std::swap(from, to);
            source = *clip;
            sourceBin = motion->sourceBin;
        }
    }
    model::BinItem item = model::motionEffectClip(source, sourceBin, from, to, dialog.speed(), dialog.strobe(),
                                                  model::FieldMotion::DuplicatedField, QDateTime::currentDateTimeUtc());
    if (dialog.choice() == MotionEffectDialog::Choice::CreateAndRender) {
        // "Click Create and Render to render the clip (creating new media files)" (p.169): its frames,
        // written as managed media; the clip then plays its own media.
        creation.videoDrive = dialog.drive();
        creation.raster = creation.raster.expandedTo(media::kMinimumRaster);
        const auto motion = *model::motionEffectOf(item);
        const std::int64_t last = std::max<std::int64_t>(source.durationFrames - 1, 0);
        QProgressDialog progress(tr("Rendering..."), tr("Cancel"), 0, 1000, this);
        progress.setObjectName(QStringLiteral("motionRenderProgress"));
        creation.progress = progressOn(progress);
        QString error;
        const auto rendered = media::renderedVideoClip(
            item.name, item.editRate, item.durationFrames,
            [&](std::int64_t frame) {
                return frames_->frame(source, std::clamp<std::int64_t>(motion.sourceFrame(frame), 0, last), creation.raster);
            },
            creation, model::BinItemKind::MotionEffect, &error);
        progress.reset();
        if (!rendered) {
            reportFailure(this, tr("Motion Effect: %1").arg(error));
            return std::nullopt;
        }
        item.link = rendered->link;
        item.sources = rendered->sources;
        item.mobId = rendered->mobId;
    }
    placeMotionClip(src->binId, item);
    return item;
}

void MainWindow::expertRender() {
    auto fail = [this](const QString& text) {
        auto* box = new QMessageBox(QMessageBox::Information, tr("Smart Render"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("expertRenderMessage"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || !project_) {
        fail(tr("Load a sequence into the Record monitor first."));
        return;
    }
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::MonitorState& state = composer_->state(MonitorId::Record);
    if (!state.in() && !state.out()) {
        fail(tr("Mark IN and OUT points around the effects for Smart Render to analyze."));
        return;
    }
    const std::int64_t from = state.markedStart();
    const std::int64_t to = state.markedEnd() + 1;
    const QStringList tracks = timeline_->patch().selectedRecordTracks(seq);
    model::ExpertRender options;
    // "A real-time effect has a small green dot in the Effect Palette" (p.35).
    options.realTime = [](const model::Effect& e) {
        const EffectInfo* info = EffectCatalog::builtin().find(e.category, e.name);
        return !info || info->realTime;
    };
    // The red indicators of the last playback attempt (p.136, p.143).
    for (const Composer::PlaybackRange& r : timeline_->playbackIndicators()) {
        if (r.dropped) options.recommended.append({r.start, r.start + r.length});
    }
    auto targetsFor = [&](model::ExpertMode mode) {
        model::ExpertRender o = options;
        o.mode = mode;
        return model::expertRender(seq, tracks, from, to, o);
    };
    media::MediaCreation creation = mediaCreation(QStringLiteral("render"));
    ExpertRenderDialog dialog([&](model::ExpertMode mode) { return static_cast<int>(targetsFor(mode).size()); },
                              !options.recommended.isEmpty(), lastRenderDrive_.isEmpty() ? creation.videoDrive : lastRenderDrive_, this);
    // "ExpertRender analyzes the selected effects and highlights the effects that require rendering" (p.142).
    timeline_->setExpertSelection(targetsFor(dialog.mode()), false);
    if (expertHook_) {
        expertHook_(dialog);
    } else {
        dialog.exec();
    }
    const QList<model::RenderTarget> targets = targetsFor(dialog.mode());
    switch (dialog.choice()) {
    case ExpertRenderDialog::Choice::Cancel:
        // "Media Composer removes all ExpertRender highlighting and cancels the ExpertRender process".
        timeline_->setExpertSelection({}, false);
        return;
    case ExpertRenderDialog::Choice::Modify:
        // "The effects identified for rendering remain highlighted, and you can modify the set".
        lastRenderDrive_ = dialog.drive();
        lastRenderBackground_ = dialog.background();
        timeline_->setExpertSelection(targets, true);
        return;
    case ExpertRenderDialog::Choice::Ok:
        break;
    }
    timeline_->setExpertSelection({}, false);
    if (targets.isEmpty()) {
        fail(tr("No effects need rendering."));
        return;
    }
    creation.videoDrive = dialog.drive();
    lastRenderDrive_ = creation.videoDrive;
    lastRenderBackground_ = dialog.background();
    renderTargetsNow(*rec, seq, targets, creation, false, lastRenderBackground_);
}

void MainWindow::renderExpertSelection(bool useLastDrive) {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    const QList<model::RenderTarget> targets = timeline_->expertSelection();
    timeline_->setExpertSelection({}, false);
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || targets.isEmpty()) return;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    media::MediaCreation creation = mediaCreation(QStringLiteral("render"));
    if (useLastDrive && !lastRenderDrive_.isEmpty()) {
        creation.videoDrive = lastRenderDrive_;
    } else {
        RenderEffectDialog dialog(static_cast<int>(targets.size()), lastRenderDrive_.isEmpty() ? creation.videoDrive : lastRenderDrive_, this);
        const bool ok = renderHook_ ? renderHook_(dialog) : dialog.exec() == QDialog::Accepted;
        if (!ok) return;
        creation.videoDrive = dialog.drive();
        lastRenderDrive_ = creation.videoDrive;
        lastRenderBackground_ = dialog.background();
    }
    renderTargetsNow(*rec, seq, targets, creation, false, lastRenderBackground_);
}

void MainWindow::clearRenders(bool atPosition) {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = timeline_->patch().selectedRecordTracks(seq);
    const model::MonitorState& state = composer_->state(MonitorId::Record);
    std::int64_t from = state.position();
    std::int64_t to = from + 1;
    if (!atPosition) {
        // Without marks, the whole sequence (ours).
        from = state.in() || state.out() ? state.markedStart() : 0;
        to = state.in() || state.out() ? state.markedEnd() + 1 : seq.duration();
    }
    const QList<model::RenderTarget> targets = model::renderTargets(seq, tracks, from, to);
    ClearRendersDialog dialog(this);
    const bool ok = clearRendersHook_ ? clearRendersHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return;
    model::ClearRenders options;
    options.skipMotionEffects = dialog.skipMotionEffects();
    options.skipUnknownEffects = dialog.skipUnknownEffects();
    // "Effects that Media Composer does not recognize": those not in the Effect Palette.
    options.known = [](const model::Effect& e) {
        return EffectCatalog::builtin().find(e.category, e.name) != nullptr || model::motionSpeed(e).has_value();
    };
    timeline_->changeRecordSequence(tr("Clear Renders"), [&](model::Sequence& s) { return model::clearRenders(s, targets, options) > 0; });
}

// "Mark an IN point and an OUT point around the area to mix down" (p.228): without marks, ours mixes the
// whole sequence down, as Audio Mixdown does. The clip is named after the sequence (ours).
std::optional<model::BinItem> MainWindow::videoMixdown() {
    auto fail = [this](const QString& text) {
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Video Mixdown"), text, QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("videoMixdownFailed"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return std::optional<model::BinItem>();
    };
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || !project_) return fail(tr("Load a sequence into the Record monitor first."));
    const model::LoadedClip loaded = *rec;
    const model::Sequence seq = model::Sequence::fromItem(loaded.item);
    const QStringList tracks = shownVideoTracks(MonitorId::Record, seq);
    if (tracks.isEmpty()) return fail(tr("There are no video tracks to mix down."));
    const model::MonitorState& marks = composer_->state(MonitorId::Record);
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (marks.in() || marks.out()) {
        start = marks.markedStart();
        end = marks.markedEnd() + 1;
    }
    if (end <= start) return fail(tr("There is nothing to mix down."));
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    VideoMixdownDialog::Setup setup;
    for (const BinDocument* d : binContainer_->openDocuments()) {
        if (!d->isReadOnly()) setup.bins << qMakePair(d->nodeId(), d->name());
    }
    setup.activeBin = loaded.binId;
    setup.drive = creation.videoDrive;
    setup.resolution = creation.resolution.id;
    VideoMixdownDialog dialog(setup, this);
    const bool ok = videoMixdownHook_ ? videoMixdownHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return std::nullopt;
    BinDocument* doc = binContainer_->document(dialog.bin());
    if (!doc || doc->isReadOnly()) return fail(tr("Choose a bin that can be changed."));
    creation.videoDrive = dialog.drive();
    if (const media::Resolution* r = media::findResolution(dialog.resolution())) creation.resolution = *r;
    creation.raster = QSize(project_->settings.format.width, project_->settings.format.height).expandedTo(media::kMinimumRaster);
    media::Composition c;
    c.sequence = &seq;
    c.tracks = tracks;
    c.size = creation.raster;
    fillComposition(c, loaded);
    fillCompositionQuality(c);
    QProgressDialog progress(tr("Mixing down..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("videoMixdownProgress"));
    creation.progress = progressOn(progress);
    QString error;
    const auto clip = media::renderedVideoClip(
        tr("%1 Mixdown").arg(loaded.item.name), loaded.item.editRate, end - start,
        [&](std::int64_t frame) { return media::composeImage(c, start + frame); }, creation, model::BinItemKind::MasterClip, &error);
    progress.reset();
    if (!clip) return fail(tr("The video mixdown failed: %1").arg(error));
    doc->addItem(*clip);
    if (BinView* v = binContainer_->viewFor(doc)) v->selectIds({clip->id});
    return clip;
}

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

double MainWindow::outputGainDb() const {
    return settings_ && settings_->hasProject() ? settings_->value(QStringLiteral("Audio Project"), QStringLiteral("outputGain")).toDouble() : 0.0;
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
    if (!playingTone_) reportFailure(this, tr("Play Calibration Tone: no audio output could be opened."));
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
        // Console only: the message box below shows it.
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
        // Console only: the message box below shows it.
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
        if (error) *error = tr("The clip %1 of the Audio Processing effect is not found.").arg(segment.name);
        return std::nullopt;
    }
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    media::MediaCreation creation = mediaCreation(QStringLiteral("transcode"));
    if (!drive.isEmpty()) {
        creation.videoDrive = drive;
        creation.audioDrive = drive;
    }
    return media::renderAudioSuite(segment, model::clipForSegment(*clip, segment), rec ? rec->item.editRate : clip->editRate, creation, error);
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
    dialog.setDynamicRelink(dynamicRelinkSettings().enabled);
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
    options.acesWorkingSpace = model::isAcesColorSpace(project_->settings.colorSpace);
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
    if (!result.associated.isEmpty()) {
        // Transcode without Create new clips (p.354-355): the clips have media at one more resolution, found in the media
        // database, and Dynamic Relink chooses which they play (8.6a).
        loadMediaDatabase();
        const media::DynamicRelinkSettings relink = dynamicRelinkSettings();
        for (const model::BinItem& item : result.associated) {
            // Console only: that the clip keeps its other resolution too.
            ConsoleLog::instance().append(tr("Consolidate/Transcode: %1 has media at one more resolution.").arg(item.name));
            if (const model::BinItem* now = doc->bin().item(item.id)) {
                model::BinItem clip = *now;
                dynamicRelinkClip(binId, clip, relink);
            }
        }
    }
    if (BinView* view = binContainer_->viewFor(doc); view && !ids.isEmpty()) view->selectIds(ids);
    for (const QString& why : result.skipped) reportFailure(this, tr("Consolidate/Transcode: %1").arg(why));
    if (!result.error.isEmpty()) reportFailure(this, tr("Consolidate/Transcode: %1").arg(result.error));
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
    if (panelId == QLatin1String("ColorCorrection")) size = QSize(760, 330);
    if (panelId == QLatin1String("MotionEffectEditor")) size = QSize(380, 560);
    if (panelId == QLatin1String("AudioMixer")) size = QSize(560, 440);
    if (panelId == QLatin1String("AudioEQ")) size = QSize(620, 300);
    if (panelId == QLatin1String("AudioTrackEffect")) size = QSize(460, 360);
    if (panelId == QLatin1String("AudioSuite")) size = QSize(480, 160);
    if (panelId == QLatin1String("AudioTool")) size = QSize(300, 340);
    if (panelId == QLatin1String("MediaTool")) size = QSize(640, 320);
    if (panelId == QLatin1String("SourceBrowser")) size = QSize(960, 600);
    if (panelId == QLatin1String("ListTool")) size = QSize(980, 560);
    const QPoint at = mapToGlobal(QPoint(80, 90));
    // floatPanel keeps it on the screen and no larger than it (fitOnScreen).
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
    applyDynamicRelinkSetting();
    applyTranscriptSharing();
    // The Render setting's Timewarps Render Using, shown by the Motion Effect Editor's Type (FX p.183).
    if (motionEditor_) motionEditor_->setRenderUsing(s.text(QStringLiteral("Render"), QStringLiteral("timewarpsRenderUsing")));
    // The Paint Effect's Previsualization Marker tool is for 24-fps film projects (FX p.412); Scratch Removal's Frame
    // Processing button is for two-field media (FX p.547).
    if (project_) {
        // OFX plug-ins are told the project's frame rate (kOfxImageEffectPropFrameRate).
        media::setOfxFrameRate(project_->settings.format.editRate.toDouble());
        // And its size, the canonical coordinates of their parameters and overlays (6.11c2).
        media::setOfxProjectSize(QSize(project_->settings.format.width, project_->settings.format.height));
        // And its colour space, the one their clips' images are in unless OpenColorIO converts them (6.11c3).
        media::setOfxColourSpace(project_->settings.colorSpace);
        effectEditor_->setInterlacedProject(project_->settings.format.scan == ScanMode::Interlaced);
        // The YC Waveform's PAL or NTSC scale (FX Guide p.690): PAL for 25 and 50 fps projects (ours).
        if (CorrectionMonitors* m = composer_->correctionMonitors()) {
            const int fps = TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps;
            m->setPal(fps == 25 || fps == 50);
        }
        effectEditor_->setFilmProject(!project_->settings.filmType.isEmpty() &&
                                      TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps == 24);
    }
    // Its Source and Output menus follow the project's format (FX p.183, p.205).
    if (motionEditor_ && project_) {
        motionEditor_->setProjectFormat(TimecodeFormat::forRate(project_->settings.format.editRate, false).nominalFps,
                                        project_->settings.format.scan == ScanMode::Interlaced, !project_->settings.filmType.isEmpty());
    }
    // Video Display (p.1224) and Full Screen Playback (p.1190-1191) settings.
    composer_->setHighQualityScaling(s.flag(QStringLiteral("Video Display"), QStringLiteral("highQualityScaling")));
    // Mixed rate clips load at the project's rate, through Motion Adapters of the Render setting's Type
    // (Editing Guide p.469, p.1215).
    if (project_) {
        composer_->setAdapterOptions(project_->settings.format.editRate, project_->settings.format.scan == ScanMode::Interlaced,
                                     s.flag(QStringLiteral("Render"), QStringLiteral("motionAdapterBlending")));
    }
    // The monitors' Display Color Space converts from the project's colour space (p.544).
    composer_->setProjectColorSpace(project_ ? project_->settings.colorSpace : QString());
    // ACES projects (8.5e4b2): the monitors' Project shows the Output Color Space; the Timeline's blue Video Quality Menu.
    const bool aces = project_ && model::isAcesColorSpace(project_->settings.colorSpace);
    composer_->setOutputColorSpace(aces ? project_->settings.outputColorSpace : QString());
    timeline_->setFloatingPointPrecision(aces);
    // Mask margins (p.550-552; 8.5e3): the project's, shown as each monitor's Target Mask (the Composer setting's Viewer
    // tab, p.1161) and the Full Screen Playback setting's (p.1191).
    composer_->setMaskMargins(project_ ? project_->settings.mask : model::MaskMargins{});
    // The Viewer tab's Source and Record Monitor Color Space (p.1161; 8.5e4a), as Display Color Space.
    composer_->setDisplayColorSpace(MonitorId::Source, media::displayColorSpaceOfValue(s.text(QStringLiteral("Composer"), QStringLiteral("sourceColorSpace"))));
    composer_->setDisplayColorSpace(MonitorId::Record, media::displayColorSpaceOfValue(s.text(QStringLiteral("Composer"), QStringLiteral("recordColorSpace"))));
    composer_->setTargetMask(MonitorId::Source, model::targetMaskFrom(s.text(QStringLiteral("Composer"), QStringLiteral("sourceTargetMask"))));
    composer_->setTargetMask(MonitorId::Record, model::targetMaskFrom(s.text(QStringLiteral("Composer"), QStringLiteral("recordTargetMask"))));
    // "Media Composer can only play back interlaced stereoscopic material using one of the Full Quality options" (p.403;
    // ours: in an interlaced project whose Stereoscopic option is on).
    composer_->setFullQualityOnly(project_ && project_->settings.format.scan == ScanMode::Interlaced &&
                                  model::stereoModeOf(project_->settings.stereoscopic) != model::StereoMode::Off);
    // S3D Settings (p.1216; 8.6d2): Parallax Near and Far are "automatically calculated" unless set to Manual; the
    // calculated values are kept in the setting so that it shows them. The monitors show the S3D Views anew.
    if (project_) {
        const model::S3DSettings s3d = s3dSettings();
        const std::pair<const char*, double> calculated[] = {
            {"parallaxNear", s3d.manualNear ? s3d.parallaxNearValue : model::parallaxPixels(s3d, s3d.depthNear)},
            {"parallaxFar", s3d.manualFar ? s3d.parallaxFarValue : model::parallaxPixels(s3d, s3d.depthFar)}};
        for (const auto& [key, value] : calculated) {
            const QString k = QString::fromLatin1(key);
            if (std::abs(s.value(QStringLiteral("S3D"), k).toDouble() - value) > 0.005) {
                settings_->setValue(QStringLiteral("S3D"), k, std::round(value * 100.0) / 100.0);
            }
        }
        composer_->refreshDisplay();
    }
    if (FullScreenView* view = fullScreen_.data()) {
        view->setReformat(FullScreenView::reformatFrom(s.text(QStringLiteral("Full Screen Playback"), QStringLiteral("reformat"))));
        view->setFullScreenScaling(s.flag(QStringLiteral("Full Screen Playback"), QStringLiteral("fullScreen")));
        view->setTargetMask(model::targetMaskFrom(s.text(QStringLiteral("Full Screen Playback"), QStringLiteral("targetMask"))));
    }
    // Bin settings (p.1152-1154).
    binContainer_->setAutoSaveTiming(s.number(QStringLiteral("Bin"), QStringLiteral("autoSaveMinutes")) * 60 * 1000,
                                     s.number(QStringLiteral("Bin"), QStringLiteral("inactivitySeconds")) * 1000,
                                     s.number(QStringLiteral("Bin"), QStringLiteral("forceAutoSaveMinutes")) * 60 * 1000);
    // The interface's language (ours, 10.4; translation.h): kept for the next start, since the windows are already made
    // in the language Cutthulhu started in. Said once when the user changes it, not when the setting is first read.
    {
        const QString language = s.text(QStringLiteral("Interface"), QStringLiteral("language"));
        if (!language.isEmpty() && language != shownLanguage_) {
            const bool changed = !shownLanguage_.isEmpty();
            shownLanguage_ = language;
            saveLanguage(language);
            if (changed && language != installedLanguage()) {
                auto* box = new QMessageBox(QMessageBox::Information, tr("Language"),
                                            tr("The new language is used when Cutthulhu starts again."), QMessageBox::Ok, this);
                box->setObjectName(QStringLiteral("languageNote"));
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->open();
            }
        }
    }
    // Interface settings, Bins tab (p.1201), and the viewer background (Timeline and Viewers tab).
    {
        const QString ui = QStringLiteral("Interface");
        BinAppearance look;
        look.backgrounds = s.flag(ui, QStringLiteral("customBinBackgrounds"));
        look.tabsOnly = s.flag(ui, QStringLiteral("binBackgroundTabsOnly"));
        if (s.flag(ui, QStringLiteral("overrideBinFonts"))) look.fontFamily = s.text(ui, QStringLiteral("overrideBinFont"));
        if (s.flag(ui, QStringLiteral("overrideBinFontSizes"))) look.fontSize = s.number(ui, QStringLiteral("overrideBinFontSize"));
        // Bin settings (p.1154): the Frame view's borders and icons, and Show local media icons.
        const QString bin = QStringLiteral("Bin");
        look.frameBorders = s.text(bin, QStringLiteral("frameBorders")) == QLatin1String("clip") ? BinAppearance::FrameBorders::ClipColor
                                                                                                 : BinAppearance::FrameBorders::Type;
        look.frameIcons = s.flag(bin, QStringLiteral("frameIcons"));
        look.localMediaIcons = s.flag(bin, QStringLiteral("localMediaIcons"));
        binContainer_->setBinAppearance(look);
        binContainer_->setSidebarAppearance(
            s.flag(ui, QStringLiteral("customSidebarBackground")) ? QColor(s.text(ui, QStringLiteral("sidebarBackground"))) : QColor(),
            s.flag(ui, QStringLiteral("overrideSidebarFont")) ? s.text(ui, QStringLiteral("sidebarFont")) : QString(),
            s.flag(ui, QStringLiteral("overrideSidebarFontSize")) ? s.number(ui, QStringLiteral("sidebarFontSize")) : 0);
        binContainer_->setNewBinDefaults(s.text(ui, QStringLiteral("newBinBackground")), s.text(ui, QStringLiteral("newBinFont")));
        composer_->setViewerBackground(s.flag(ui, QStringLiteral("customViewerBackground"))
                                           ? QColor(s.text(ui, QStringLiteral("viewerBackground")))
                                           : QColor());
    }
    binContainer_->setSkipLockedPrompts(s.flag(QStringLiteral("Bin"), QStringLiteral("skipLockedPrompts")));
    binContainer_->setAtticLimits({s.number(QStringLiteral("Bin"), QStringLiteral("atticMaxFiles")),
                                   s.number(QStringLiteral("Bin"), QStringLiteral("atticMaxVersions")),
                                   s.flag(QStringLiteral("Bin"), QStringLiteral("atticKeepOne"))});
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
    const QString dataDisplay = s.text(composer, QStringLiteral("dataDisplay"));
    d.infoRow = dataDisplay != QLatin1String("off");
    d.twoRows = dataDisplay == QLatin1String("two");
    d.flowData = dataDisplay == QLatin1String("flow");
    d.tickMarks = s.flag(composer, QStringLiteral("tickMarks"));
    d.middlePanel = s.flag(composer, QStringLiteral("middlePanel"));
    composer_->setNameFont(s.value(composer, QStringLiteral("fontFamily")).toString(),
                           s.value(composer, QStringLiteral("fontSize")).toInt());
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
    // "This option is selected by default" (FX Guide p.137).
    highlightRenderAreas_ = s.flag(tl, QStringLiteral("highlightRenderAreas"));
    if (!highlightRenderAreas_) timeline_->setPlaybackIndicators({});
    o.startFillerSeconds = s.value(tl, QStringLiteral("startFiller")).toInt();
    o.flashFrames = s.value(tl, QStringLiteral("flashFrames")).toInt(10);
    // Dynamic Play Acceleration and Slow start speed (Timeline settings, p.1219), for the Composer.
    composer_->setDynamicPlay(s.value(tl, QStringLiteral("dynamicPlayAcceleration")).toInt(3),
                              s.value(tl, QStringLiteral("slowStartSpeed")).toInt(3));
    const QString text = s.value(tl, QStringLiteral("timelineText")).toString();
    o.textColor = text == QLatin1String("black")  ? Timeline::Options::TextColor::Black
                  : text == QLatin1String("both") ? Timeline::Options::TextColor::Both
                                                  : Timeline::Options::TextColor::White;
    o.wireframeDragging = s.value(tl, QStringLiteral("wireframeDragging")).toBool();
    const QString dragDisplay = s.value(tl, QStringLiteral("segmentDragDisplay")).toString();
    o.segmentDragDisplay = dragDisplay == QLatin1String("fourFrame") ? Timeline::Options::DragDisplay::FourFrame
                           : dragDisplay == QLatin1String("none")    ? Timeline::Options::DragDisplay::None
                                                                     : Timeline::Options::DragDisplay::PositionBar;
    o.fastScrub = s.value(tl, QStringLiteral("fastScrub")).toBool();
    o.autoPatching = s.value(tl, QStringLiteral("autoPatching")).toBool();
    o.autoMonitoring = s.value(tl, QStringLiteral("autoMonitoring")).toBool();
    o.segmentDragSyncLocks = s.value(tl, QStringLiteral("segmentDragSyncLocks")).toBool();
    o.positionBarSnap = s.value(tl, QStringLiteral("positionBarSnap")).toBool();
    o.segmentDragSnap = s.value(tl, QStringLiteral("segmentDragSnap")).toBool();
    o.doubleClickNesting = s.value(tl, QStringLiteral("doubleClickNesting")).toBool();
    o.editFromBin = s.flag(QStringLiteral("Bin"), QStringLiteral("editFromBin"));
    // Interface settings, Timeline and Viewers tab (p.1201-1202).
    const QString iface = QStringLiteral("Interface");
    o.background = s.flag(iface, QStringLiteral("timelineBackground")) ? QColor(s.text(iface, QStringLiteral("timelineBackgroundColor"))) : QColor();
    o.blendBackground = s.flag(iface, QStringLiteral("timelineBlend"));
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
    // The Effect Editor setting (p.1171-1172).
    {
        const QString fx = QStringLiteral("Effect Editor");
        EffectEditor::Options e;
        e.indentRows = s.flag(fx, QStringLiteral("indentRows"));
        e.largeText = s.flag(fx, QStringLiteral("largeText"));
        e.setPositionToKeyframe = s.flag(fx, QStringLiteral("setPositionToKeyframe"));
        e.updatePositionWhilePlaying = s.flag(fx, QStringLiteral("updatePositionWhilePlaying"));
        e.showModeMenu = s.flag(fx, QStringLiteral("showAddKeyframeModeMenu"));
        const QString mode = s.text(fx, QStringLiteral("addKeyframeCommand"));
        static const char* const modes[] = {"activeParameter", "activeGroup", "openGroups", "enabledGroups", "openGraphs", "allParameters"};
        for (int i = 0; i < 6; ++i) {
            if (mode == QLatin1String(modes[i])) e.mode = static_cast<EffectEditor::KeyframeMode>(i);
        }
        const QSignalBlocker quiet(effectEditor_);
        effectEditor_->setOptions(e);
    }
    const QString trim = QStringLiteral("Trim");
    o.trimAutoFocus = s.flag(trim, QStringLiteral("autoFocus"));
    o.trimPreroll = s.value(trim, QStringLiteral("preroll")).toInt(2);
    o.trimPostroll = s.value(trim, QStringLiteral("postroll")).toInt(2);
    o.trimIntermission = s.value(trim, QStringLiteral("intermission")).toInt(0);
    o.jklTrim = s.flag(trim, QStringLiteral("jklTrim"));
    const QString small = s.value(trim, QStringLiteral("trimMode")).toString();
    o.smallTrim = small == QLatin1String("always") ? Timeline::Options::SmallTrim::Always
                  : small == QLatin1String("goTo")  ? Timeline::Options::SmallTrim::GoTo
                                                    : Timeline::Options::SmallTrim::Never;
    o.dualImagePlay = s.flag(trim, QStringLiteral("dualImagePlay"));
    o.syncRollers = s.flag(trim, QStringLiteral("syncRollers"));
    // The Correction setting (Editing Guide p.1164-1167; FX Guide p.606-609).
    {
        const QString cc = QStringLiteral("Correction");
        CorrectionTool::Options c;
        const char* const tabs[] = {"tabHsl", "tabChannels", "tabLevels", "tabCurves", "tabSecondary"};
        for (int i = 0; i < 5; ++i) c.groups.at(static_cast<std::size_t>(i)) = s.flag(cc, QLatin1String(tabs[i]));
        c.units.rgb = model::correctionUnitFromKey(s.text(cc, QStringLiteral("unitRgb")));
        c.units.luma = model::correctionUnitFromKey(s.text(cc, QStringLiteral("unitLuma")));
        c.units.composite = model::correctionUnitFromKey(s.text(cc, QStringLiteral("unitComposite")));
        c.solidHistograms = s.flag(cc, QStringLiteral("solidHistograms"));
        c.colorHistograms = s.flag(cc, QStringLiteral("colorHistograms"));
        c.dynamicHistograms = s.flag(cc, QStringLiteral("dynamicHistograms"));
        c.fullRanges = s.flag(cc, QStringLiteral("fullRanges"));
        const QString vectors = s.text(cc, QStringLiteral("secondaryVectors"));
        c.vectors = vectors == QLatin1String("all")       ? CorrectionTool::Options::Vectors::All
                    : vectors == QLatin1String("custom")  ? CorrectionTool::Options::Vectors::Custom
                    : vectors == QLatin1String("enabled") ? CorrectionTool::Options::Vectors::Enabled
                                                          : CorrectionTool::Options::Vectors::Standard;
        c.backgrounds = s.flag(cc, QStringLiteral("showBackgrounds"));
        c.eyedropperInfo = s.flag(cc, QStringLiteral("showEyedropperInfo"));
        c.savedColorLabels = media::savedColorLabelsFromKey(s.text(cc, QStringLiteral("savedColorLabels")));
        c.eyedropperAnywhere = s.flag(cc, QStringLiteral("eyedropperAnywhere"));
        // The Safe Colors project setting (FX Guide ch.23; 6.10e4).
        c.safeColors = safeColorLimits();
        if (correctionTool_) correctionTool_->setOptions(c);
        if (CorrectionMonitors* m = composer_->correctionMonitors()) {
            m->setEyedropperAveraging(s.flag(cc, QStringLiteral("eyedropperAveraging")));
            m->setDualSplitDuringPlayback(s.flag(cc, QStringLiteral("dualSplitDuringPlayback")));
            m->setSafeColors(c.safeColors);
        }
        o.correctionUseMarks = s.flag(cc, QStringLiteral("useMarks"));
    }
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
    // "The four automatic color correction commands ... are also available as buttons in the CC tab of the Command
    // Palette" (FX Guide p.621): the Hue Offsets tab's, in Color Correction mode.
    for (const auto& [id, kind] : {std::pair{"cc.auto_balance", media::AutoCorrection::HslBalance}, std::pair{"cc.auto_black", media::AutoCorrection::HslBlack},
                                   std::pair{"cc.auto_contrast", media::AutoCorrection::HslContrast}, std::pair{"cc.auto_white", media::AutoCorrection::HslWhite}}) {
        commands_.setHandler(QString::fromLatin1(id), [this, k = kind] {
            if (timeline_->isCorrectionMode()) correctionTool_->autoCorrect(k);
        });
    }
    // The Command Palette's twelve Tab buttons (FX Guide p.601-602): one displays the Color Correction tool's tab it is
    // associated with; Alt+click (Option+click) with Active Palette associates the tab the tool shows. Kept as the
    // Correction setting's tabButton1-12 (ours). In Color Correction mode only (p.601's steps start there).
    for (int i = 1; i <= 12; ++i) {
        commands_.setHandler(QStringLiteral("cc.tab_%1").arg(i), [this, i] {
            if (!timeline_->isCorrectionMode()) return;
            const QString key = QStringLiteral("tabButton%1").arg(i);
            if (QApplication::keyboardModifiers().testFlag(Qt::AltModifier)) {
                settings_->setValue(QStringLiteral("Correction"), key, correctionTool_->shownPath());
                return;
            }
            const QString path = settings_->text(QStringLiteral("Correction"), key);
            if (path.isEmpty()) return;
            showTool(QStringLiteral("ColorCorrection"));
            correctionTool_->showPath(path);
        });
    }
    // "With Active Palette selected in the Command palette, click the Save Correction button in the CC tab" (FX Guide
    // p.613), and the buckets as buttons there: "You can map these buttons to any mappable key" (p.613). A bucket button
    // with Alt saves to it, as Alt+click on the tool's bucket does (p.614). In Color Correction mode only.
    commands_.setHandler(QStringLiteral("cc.save_correction"), [this] {
        if (timeline_->isCorrectionMode()) correctionTool_->saveCorrection();
    });
    for (int i = 0; i < CorrectionTool::kBuckets; ++i) {
        commands_.setHandler(QStringLiteral("cc.bucket_%1").arg(i + 1), [this, i] {
            if (timeline_->isCorrectionMode()) correctionTool_->clickBucket(i, QApplication::keyboardModifiers().testFlag(Qt::AltModifier));
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
    // The classic Title Tool (FX Guide p.327).
    handle("clip.new_title", [this] { openTitleTool(); });
    handle("tools.title_tool_application", [this] { openTitleTool(); });
    // "Select Timeline > New > Title" also opens it (FX Guide p.343).
    handle("timeline.new.title", [this] { openTitleTool(); });
    // Title media (FX Guide p.374-375).
    handle("clip.recreate_title_media", [this] { recreateTitleMedia(); });
    handle("timeline.create_unrendered_title_media", [this] { createUnrenderedTitleMedia(); });
    // Tools (ch.5).
    // Tools menu: each opens its panel through showTool().
    handle("tools.command_palette", [this] { showTool(QStringLiteral("CommandPalette")); });
    handle("tools.calculator", [this] { showTool(QStringLiteral("Calculator")); });
    handle("tools.color_info_tool", [this] { showTool(QStringLiteral("ColorInfo")); });
    handle("tools.transcript_tool", [this] {
        showTool(QStringLiteral("Transcript"));
        refreshTranscriptTool();
    });
    handle("tools.console", [this] { showTool(QStringLiteral("Console")); });
    handle("tools.effect_palette", [this] { showTool(QStringLiteral("EffectPalette")); });
    handle("tools.effect_editor", [this] { showTool(QStringLiteral("EffectEditor")); });
    // Edit > Deselect All Keyframes (FX Guide p.112): "All keyframes in the enabled parameter's
    // parameter track are deselected"; with no parameter active, every keyframe of the effect.
    handle("edit.deselect_all_keyframes", [this] {
        const QString active = effectEditor_->activeParameter();
        effectEditor_->deselectAllKeyframes(active.isEmpty() ? QStringList() : QStringList{active});
    });
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
    // File > Input > Link: the Source Browser, linking (p.315-317, p.328).
    handle("file.input.link", [this] {
        showSourceBrowser();
        if (SourceBrowser* browser = sourceBrowser_.data()) browser->setImportMode(false);
    });
    handle("file.media.unmount", [this] { unmountDrives(); });
    handle("file.input.import_media", [this] {
        showSourceBrowser();
        if (SourceBrowser* browser = sourceBrowser_.data()) browser->setImportMode(true);
    });
    handle("clip.consolidate_transcode", [this] { consolidateTranscode(); });
    // Clip > Batch Import (p.1098) and Batch Re-import (p.230, p.234): the same reimport.
    handle("clip.batch_import", [this] { batchImport(); });
    handle("clip.batch_re_import", [this] { batchImport(); });
    handle("timeline.mixdown.audio.to_sequence", [this] { audioMixdown(); });
    handle("timeline.mixdown.video", [this] { videoMixdown(); });
    // Render (FX Guide p.58-61): Shift renders again what is rendered (p.61), Alt skips the dialog
    // with the last drive (p.58).
    auto renderWith = [this](bool atPosition) {
        const Qt::KeyboardModifiers m = QGuiApplication::keyboardModifiers();
        renderEffects(atPosition, m.testFlag(Qt::ShiftModifier), m.testFlag(Qt::AltModifier));
    };
    handle("timeline.render.render_at_position", [renderWith] { renderWith(true); });
    handle("timeline.render.render_in_out", [renderWith] { renderWith(false); });
    handle("timeline.render.clear_renders_at_position", [this] { clearRenders(true); });
    handle("timeline.render.clear_renders_in_out", [this] { clearRenders(false); });
    handle("timeline.render.expertrender_in_out", [this] { expertRender(); });
    // File > Output > Export to File (p.868).
    handle("file.output.export_to_file", [this] { exportToFile(); });
    handle("file.output.send_to.make_new", [this] { makeNewSendTo(); });
    handle("file.output.export_to_device.xdcam", [this] { exportToDevice(QStringLiteral("xdcam")); });
    handle("file.output.export_to_device.p2", [this] { exportToDevice(QStringLiteral("p2")); });
    handle("file.new.volume_for_export", [this] { newVolumeForExport(); });
    handle("file.output.digital_cut", [this] { openDigitalCut(); });
    handle("tools.video_output_tool", [this] { openVideoOutput(); });
    handle("file.export_matte_pict", [this] { exportMattePict(); });
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
    // The check mark is the state; the handler is what enables the button (9.6: it stayed dimmed without one).
    handle("play.digital_audio_scrub", [] {});
    // Composer > Render On-the-Fly (FX Guide p.56-57): on, a stationary frame shows its effects;
    // off, "as if no effects are applied to it". On until turned off (ours: the guide gives no
    // default). Playback is not affected (p.57).
    handle("composer.render_on_the_fly", [] {}); // the action's check mark is the state (below)
    handle("composer.freeze_frame.other", [this] { freezeFrame(); });
    handle("fx.motion_effect", [this] { motionEffect(); });
    if (QAction* fly = commands_.action(QStringLiteral("composer.render_on_the_fly"))) {
        fly->setCheckable(true);
        fly->setChecked(true);
        connect(fly, &QAction::toggled, this, [this](bool on) {
            renderOnTheFly_ = on;
            composer_->refreshDisplay();
        });
    }
    handle("clip.modify.unlink_media", [this] { unlinkSelection(); });
    handle("clip.modify.modify_clip", [this] { modifySelection(); });
    handle("clip.audio.apply_gain", [this] { applyClipGain(); });
    handle("clip.audio.center_pan", [this] { centerPan(); });
    handle("clip.auto_vo", [this] { autoVo(); });
    // MultiCamera (8.1a; p.1123-1131).
    handle("clip.group_clips", [this] { groupClips(); });
    handle("clip.autosync", [this] { syncClips(); });
    handle("clip.multigroup", [this] { multiGroup(); });
    timeline_->setGroupArrowHook([this](int step) {
        const model::LoadedClip* source = composer_->state(MonitorId::Source).current();
        if (composer_->activeMonitor() == MonitorId::Source && !(source && (model::isGroupClip(source->item) || model::isMultigroup(source->item)))) {
            return false;
        }
        return stepInGroup(step);
    });
    handle("clip.edit_group", [this] { editGroup(); });
    handle("composer.update_group", [this] { updateGroup(); });
    handle("composer.create_group", [this] { createGroupFromSequence(); });
    handle("clip.refresh_sequence.group_clips", [this] { refreshGroupEdits(); });
    timeline_->setSoundOffset([](const model::BinItem& a, const model::BinItem& b, QString* why) { return media::waveformOffset(a, b, why); });
    handle("mcam.previous_in_group", [this] { stepInGroup(-1); });
    handle("mcam.next_in_group", [this] { stepInGroup(1); });
    handle("mcam.quad_split", [this] { composer_->setGroupSplit(4); });
    handle("mcam.nine_split", [this] { composer_->setGroupSplit(9); });
    handle("mcam.swap_cam_bank", [this] { swapBank(); });
    // Composer > MultiCamera Mode (p.1129), on and off. The check mark follows the mode; the handler is what enables
    // the menu item (9.6: without it the item stayed dimmed, though the mode worked).
    if (QAction* mode = commands_.action(QStringLiteral("composer.multicamera_mode"))) {
        mode->setCheckable(true);
        connect(mode, &QAction::toggled, this, [this](bool on) { composer_->setMultiCameraMode(on); });
        handle("composer.multicamera_mode", [this, mode] { composer_->setMultiCameraMode(mode->isChecked()); });
    }
    composer_->setPictureClickHandler([this](MonitorId monitor, QPointF fraction, Qt::MouseButton button) {
        return splitClicked(monitor, fraction, button);
    });
    composer_->setKeypadHandler([this](int key) { return multiCamKey(key); });
    // The Group menu (p.1133): the shown group's angles.
    // The Group menu (p.1133): the shown group's tracks, each with the angles that have it and the one it comes from.
    composer_->setGroupMenu([this]() { return groupMenuInfo(); },
                            [this](const QString& track, int angle) { chooseGroupTrackAngle(track, angle); });
    timeline_->setGroupSelectionHook([this](const QString& binId, const QString& groupId, int angle) {
        BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* found = doc ? doc->bin().item(groupId) : nullptr;
        auto group = found ? model::groupOf(*found) : std::nullopt;
        if (!group || angle < 0 || angle >= group->angles.size()) return;
        group->selected = angle;
        model::BinItem item = *found;
        model::setGroup(item, *group);
        storeClip(binId, item);
    });
    handle("bin.commit_multicam_edits", [this] { commitMulticamEdits(); });
    handle("bin.source_settings", [this] { sourceSettings(); });
    // Sequence Report (p.369), from a bin or a monitor.
    handle("bin.sequence_report", [this] { sequenceReport(false); });
    handle("composer.sequence_report", [this] { sequenceReport(true); });
    // Stereoscopic clips (8.6d1; ours, p.213 leaves making them to the Help).
    handle("bin.create_stereoscopic_clip", [this] { createStereoscopicClip(); });
    // Proxy media (8.7a, p.508).
    handle("bin.create_proxies", [this] { createProxies(); });
    // Copy Media (8.7a2, p.512), from the bin's right-click menu and the Sidebar's.
    handle("bin.copy_media", [this] { copyMedia(); });
    connect(binContainer_, &BinContainer::copyMediaRequested, this, [this](const QStringList& ids) { copyMedia(ids); });
    handle("bin.s3d_leading_eye_left", [this] { setStereoLeadingEye(model::Eye::Left); });
    handle("bin.s3d_leading_eye_right", [this] { setStereoLeadingEye(model::Eye::Right); });
    // The Transcript menus (8.2d; p.450-453, 2026.8 guide p.455-456).
    handle("bin.export_transcript", [this] { exportTranscripts(); });
    handle("bin.share_transcripts", [this] { shareTranscripts(); });
    handle("bin.delete_transcript", [this] { deleteTranscripts(); });
    handle("bin.export_subcap", [this] { exportSubCap(); });
    handle("timeline.create_subcap", [this] { createSubCap(); });
    handle("timeline.autocut", [this] { autoCut(); });
    // Scripts (8.3a; p.570-576).
    handle("file.new.new_script", [this] { newScript(); });
    handle("file.save_script", [this] { saveScript(); });
    handle("file.save_a_script_copy_as", [this] { saveScriptCopyAs(); });
    handle("script.add_scene", [this] { scriptWindow_->askScene(); });
    handle("script.add_page", [this] { scriptWindow_->askPage(); });
    handle("script.text_layout", [this] { scriptTextLayout(); });
    handle("other.add_script_mark", [this] { addScriptMark(); });
    handle("tools.background_services", [this] { backgroundServices(); });
    handle("tools.dynamic_media_folders", [this] { showDynamicMediaFolders(); });
    handle("script.scriptsync_ai", [this] { scriptSyncAI(); });
    handle("bin.create_script_from_clip", [this] { createScriptFromClips(); });
    handle("other.find_script", [this] { findScript(); });
    for (const char* id : {"script.show_frames", "script.show_all_takes", "script.hold_slates_onscreen", "script.interpolate_position"})
        handle(id, [this, id] { setScriptSlateOption(QString::fromLatin1(id)); });
    for (const auto& [id, color] : scriptIndicatorColors()) commands_.setHandler(id, [this, cid = id] { setScriptSlateOption(cid); });
    handle("script.manage_transcripts", [this] { manageTranscripts(); });
    connect(binContainer_, &BinContainer::commandRequested, this, [this](const QString& id) {
        if (QAction* a = commands_.action(id)) a->trigger();
    });
    handle("clip.read_audio_timecode", [this] { readAudioTimecode(); });
    // Include/Exclude Bin in Transcription (p.447), from the Bin menu and the Sidebar's right-click menu.
    handle("bin.include_in_transcription", [this] { setBinsTranscribed(true); });
    handle("bin.exclude_from_transcription", [this] { setBinsTranscribed(false); });
    handle("clip.refresh_sequence.motion_adapters_timewarps", [this] { refreshMotionAdapters(); });
    handle("clip.refresh_sequence.aspect_ratio_reformatting", [this] { refreshSpatialAdapters(false); });
    handle("clip.refresh_sequence.all_frameflex", [this] { refreshSpatialAdapters(true); });
    handle("clip.refresh_sequence.color_adapters", [this] { refreshColorAdapters(false); });
    handle("clip.refresh_sequence.remove_color_adapters", [this] { refreshColorAdapters(true); });
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
    // Windows > Center Current and Send Current Home (p.601), for the active panel's floating window:
    // "To center a resized Timeline window ... select Windows > Center Current"; "To restore a resized
    // Timeline window to its default position ... select Windows > Send Current Home". Ours: the default
    // position is the one a floated panel first gets, its preferred size centred on the main window; a
    // docked panel has no window of its own to move.
    handle("windows.center_current", [this] { placeCurrentWindow(false); });
    handle("windows.set_font", [this] { chooseFont(); });
    handle("windows.bin_layout.new_bin_layout", [this] {
        // "The New Bin Layout dialog box opens. Type a name for the bin layout, and then click OK" (p.93).
        std::optional<QString> name;
        if (binLayoutHook_) {
            name = binLayoutHook_({});
        } else {
            bool ok = false;
            const QString n = QInputDialog::getText(this, tr("New Bin Layout"), tr("Bin layout name:"), QLineEdit::Normal, {}, &ok);
            if (ok) name = n;
        }
        if (name) newBinLayout(*name);
    });
    handle("windows.bin_layout.save_current", [this] {
        if (!saveCurrentBinLayout()) {
            QMessageBox::information(this, tr("Save Current"), tr("Open or make a bin layout first."));
        }
    });
    handle("windows.bin_layout.delete_bin_layout", [this] {
        // "The Delete Bin Layout dialog box opens. Click OK" (p.93): the layout to delete is chosen in it (ours).
        const QStringList layouts = binLayouts();
        if (layouts.isEmpty()) return;
        std::optional<QString> name;
        if (binLayoutHook_) {
            name = binLayoutHook_(layouts);
        } else {
            bool ok = false;
            const int current = std::max(0, static_cast<int>(layouts.indexOf(currentBinLayout_)));
            const QString n = QInputDialog::getItem(this, tr("Delete Bin Layout"), tr("Bin layout:"), layouts, current, false, &ok);
            if (ok) name = n;
        }
        if (name) deleteBinLayout(*name);
    });
    // Windows > Bin Layout lists the layouts above its commands (p.92).
    if (auto* menu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_bin_layout"))) {
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            qDeleteAll(binLayoutActions_);
            binLayoutActions_.clear();
            QAction* first = menu->actions().value(0);
            const QStringList layouts = binLayouts();
            for (const QString& name : layouts) {
                auto* a = new QAction(name, menu);
                a->setCheckable(true);
                a->setChecked(name.compare(currentBinLayout_, Qt::CaseInsensitive) == 0);
                a->setProperty(kDynamicMenuItem, true);
                connect(a, &QAction::triggered, this, [this, name] { openBinLayout(name); });
                menu->insertAction(first, a);
                binLayoutActions_ << a;
            }
            if (!layouts.isEmpty()) {
                auto* separator = new QAction(menu);
                separator->setSeparator(true);
                separator->setProperty(kDynamicMenuItem, true);
                menu->insertAction(first, separator);
                binLayoutActions_ << separator;
            }
        });
    }
    // Help > Find Command (ours, 10.3c; find_command.h): every command from the keyboard.
    handle("help.find_command", [this] {
        if (!findCommandWindow_) findCommandWindow_ = new FindCommandWindow(commands_, menuBar(), this);
        findCommandWindow_->showFor(QApplication::focusWidget());
    });
    handle("help.shortcuts", [this] {
        if (!shortcutsWindow_) shortcutsWindow_ = new ShortcutsWindow(commands_, *keyboard_, this);
        shortcutsWindow_->show();
        shortcutsWindow_->raise();
        shortcutsWindow_->activateWindow();
    });
    handle("windows.send_current_home", [this] { placeCurrentWindow(true); });
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
        Panel* p = panels_->activePanel();
        // The Script window closes its script first (a locked, changed one asks, p.590).
        if (p && p->id() == QLatin1String("Script") && !closeScript()) return;
        if (p) panels_->closePanel(p);
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
            connectPhraseFind(*findWindow_);
            // The Script Text tab (8.3e): the Script window's script, and opening a script at a line.
            findWindow_->setScriptSource({[this]() -> std::optional<std::pair<QString, QString>> {
                                              if (!scriptWindow_->isOpen()) return std::nullopt;
                                              return std::pair{scriptWindow_->binId(), scriptWindow_->itemId()};
                                          },
                                          [this](const QString& binId, const QString& itemId, int line) {
                                              if (scriptWindow_->itemId() != itemId || scriptWindow_->binId() != binId) {
                                                  if (!binContainer_->document(binId)) binContainer_->openBin(binId);
                                                  if (!openScript(binId, itemId)) return;
                                              }
                                              scriptWindow_->selectLines(line, line);
                                              if (Panel* p = panels_->panel(QStringLiteral("Script"))) panels_->showPanel(p);
                                          }});
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
    for (const char* what : {"offline_items", "media_relatives", "sources", "unreferenced_clips", "unrendered_titles"}) {
        const QString kind = QString::fromLatin1(what);
        handle(("bin.select." + kind).toLatin1().constData(), [this, kind] {
            // "A message informs you that unreferenced clips highlight in open bins only (items in
            // closed bins do not display)" (p.267).
            if (kind == QLatin1String("unreferenced_clips")) {
                QMessageBox::information(this, tr("Select Unreferenced Clips"),
                                         tr("Unreferenced clips are selected in open bins only; items in closed bins are not shown."));
            }
            binSelect(kind);
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
                // In the Effect Editor they go to its keyframes (FX Guide p.128).
                if (event && active && active->id() == QLatin1String("EffectEditor") &&
                    effectEditor_->goToEvent(id == QLatin1String("move.go_to_next_event"))) {
                    return;
                }
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
                // "Make sure that the Tracking window is active", then Copy or Paste its trackers (FX Guide p.283).
                if (clipboard && effectEditor_->trackingWindowActive()) {
                    if (id == QLatin1String("edit.copy")) effectEditor_->copyTrackers();
                    if (id == QLatin1String("edit.paste")) effectEditor_->pasteTrackers();
                    return;
                }
                // In the Effect Editor, Copy and Paste take its keyframes (FX Guide p.116-119).
                if (const Panel* active = panels_->activePanel(); clipboard && active && active->id() == QLatin1String("EffectEditor")) {
                    if (id == QLatin1String("edit.copy") && effectEditor_->copyKeyframes()) return;
                    if (id == QLatin1String("edit.paste") && effectEditor_->pasteKeyframes()) return;
                }
                // With Enable edit from bin, V and B edit the bin's selected clip (p.624).
                const bool edit = id == QLatin1String("edit.splice_in") || id == QLatin1String("edit.overwrite");
                if (edit && binsActive() && timeline_->options().editFromBin && editFromBin(id == QLatin1String("edit.splice_in"))) return;
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
            auto* a = new QAction(translatedResource("workspaces", name), menu); // built-in names translated (10.4b)
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

// ---------------------------------------------------------------- Titler+ templates (6.8c4a)

void MainWindow::openTitleEditing() {
    showTool(QStringLiteral("EffectEditor"));
    timeline_->setEffectMode(true);
    TitlerDashboard* dashboard = effectEditor_->titlerDashboard();
    dashboard->placeNear(mapToGlobal(QPoint(80, 40)));
    dashboard->show();
    keepOnScreen(dashboard);
    dashboard->raise();
}

bool MainWindow::editTitleTemplate(const QString& binId, const QString& itemId) {
    BinDocument* doc = binContainer_->document(binId);
    const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
    if (!item || !model::isTitleTemplate(*item)) return false;
    const model::EffectTemplate t = *model::effectTemplateOf(*item);
    const Rational rate = project_ ? project_->settings.format.editRate : t.editRate.num() > 0 ? t.editRate : Rational(25);
    const model::BinItem sequence = model::titleTemplateSequence(t, rate, item->name, QDateTime::currentDateTimeUtc());
    // A template edited before goes first (and asks to be saved when it has changes).
    templateEdit_ = TitleTemplateEdit{binId, itemId, sequence, false};
    composer_->load(MonitorId::Record, QLatin1String(kTitleTemplateBin), sequence);
    composer_->moveTo(MonitorId::Record, 0);
    openTitleEditing();
    return true;
}

bool MainWindow::storeTitleTemplate(TitleTemplateEdit& edit, bool saveAs) {
    BinDocument* doc = binContainer_->document(edit.binId);
    if (!doc) doc = binContainer_->openBin(edit.binId);
    const model::BinItem* old = doc ? doc->bin().item(edit.templateId) : nullptr;
    if (!old || doc->isReadOnly()) return false;
    const Rational rate = project_ ? project_->settings.format.editRate : Rational(25);
    const auto t = model::titleTemplateFromSequence(edit.sequence, rate);
    if (!t) return false;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (saveAs) {
        // "Create a duplicate with the new changes" (ours: named "<name> Copy"); the copy is edited from now on.
        const model::BinItem copy = model::effectTemplateItem(*t, tr("%1 Copy").arg(old->name), now);
        doc->addItem(copy);
        edit.templateId = copy.id;
    } else {
        model::BinItem updated = model::effectTemplateItem(*t, old->name, now);
        updated.id = old->id;
        updated.created = old->created;
        if (!doc->replaceItem(updated)) return false;
    }
    edit.dirty = false;
    return true;
}

bool MainWindow::saveTitleTemplate(bool saveAs) {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (templateEdit_ && rec && rec->item.id == templateEdit_->sequence.id) return storeTitleTemplate(*templateEdit_, saveAs);
    // No template open: the shown title becomes a new one in the active bin.
    const auto shown = effectEditor_->shownTitlerEffect();
    BinDocument* doc = binContainer_->activeDocument();
    if (!doc && !binContainer_->openDocuments().isEmpty()) doc = binContainer_->openDocuments().first();
    if (!shown || !doc || doc->isReadOnly()) return false;
    model::EffectTemplate t;
    t.effect = shown->first;
    t.length = shown->second;
    t.editRate = project_ ? project_->settings.format.editRate : Rational(25);
    doc->addItem(model::effectTemplateItem(t, model::effectTemplateName(t), QDateTime::currentDateTimeUtc()));
    return true;
}

void MainWindow::checkTitleTemplate() {
    if (!templateEdit_) return;
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (rec && rec->item.id == templateEdit_->sequence.id) return;
    // The template left the Record monitor; forgotten first, so that what the prompt does comes back to nothing here.
    TitleTemplateEdit edit = *templateEdit_;
    templateEdit_.reset();
    if (!edit.dirty) return;
    const BinDocument* doc = binContainer_->document(edit.binId);
    const model::BinItem* item = doc ? doc->bin().item(edit.templateId) : nullptr;
    const QString name = item ? item->name : QString();
    const bool save = askTemplateSave_ ? askTemplateSave_(name)
                                       : QMessageBox::question(this, tr("Title Designer"), tr("Save the changes to the template \"%1\"?").arg(name),
                                                               QMessageBox::Save | QMessageBox::Discard) == QMessageBox::Save;
    if (save) storeTitleTemplate(edit, false);
}

// ---------------------------------------------------------------- the classic Title Tool (6.8d1)

TitleToolWindow* MainWindow::titleTool() const {
    return titleTool_.data();
}

QString MainWindow::saveClassicTitle(const model::ClassicTitle& title, const TitleToolWindow::SaveRequest& request, const QString& replaceId,
                                     QString* error) {
    // "You can save titles in multiple aspect ratios" (Editing Guide p.989): the first aspect chosen is the title saved
    // (or revised) under its name, each other one a Title Effect clip of its own named "<name> <aspect>" (ours).
    if (request.aspects.size() > 1) {
        TitleToolWindow::SaveRequest first = request;
        first.aspects = {request.aspects.first()};
        const QString id = saveClassicTitle(title, first, replaceId, error);
        if (id.isEmpty()) return id;
        for (qsizetype i = 1; i < request.aspects.size(); ++i) {
            TitleToolWindow::SaveRequest other = request;
            other.aspects = {request.aspects[i]};
            other.name = request.name + QLatin1Char(' ') + request.aspects[i];
            other.placeTrack.clear();
            if (saveClassicTitle(title, other, {}, error).isEmpty()) return {};
        }
        // The first is the one the Title tool goes on with and the Source monitor shows.
        if (BinDocument* doc = binContainer_->document(request.bin); doc && doc->bin().item(id)) {
            composer_->load(MonitorId::Source, doc->nodeId(), *doc->bin().item(id));
        }
        return id;
    }
    if (request.aspects.size() == 1 && request.aspects.first() != title.aspect && !(title.aspect.isEmpty() && request.aspects.first() == QLatin1String("16:9"))) {
        model::ClassicTitle shaped = title;
        shaped.aspect = request.aspects.first() == QLatin1String("16:9") ? QString() : request.aspects.first();
        TitleToolWindow::SaveRequest plain = request;
        plain.aspects.clear();
        return saveClassicTitle(shaped, plain, replaceId, error);
    }
    auto fail = [error](const QString& text) {
        if (error) *error = text;
        return QString();
    };
    if (!project_) return fail(tr("Open a project first."));
    BinDocument* doc = binContainer_->document(request.bin);
    if (!doc) doc = binContainer_->activeDocument();
    if (!doc) return fail(tr("Open a bin to save the title in."));
    if (doc->isReadOnly()) return fail(tr("The bin is locked by another user."));
    const Rational rate = project_->settings.format.editRate;
    const QDateTime when = QDateTime::currentDateTimeUtc();
    // The precomputes, unless fast saved: "Fast Save skips the steps that create anti-aliased images from title
    // objects" (p.357).
    model::TitleMedia media;
    if (!request.fastSave) {
        QString why;
        const auto made = makeTitleMedia(title, request.name, *doc, request.drive, request.resolution, &why);
        if (!made) return fail(why.isEmpty() ? tr("The title's media could not be made.") : why);
        media = *made;
    } else {
        // Kept for Create Unrendered Title Media's default (FX Guide p.375).
        media.resolution = request.resolution.isEmpty() ? mediaCreation(QStringLiteral("titles")).resolution.id : request.resolution;
    }
    const model::BinItem* old = replaceId.isEmpty() ? nullptr : doc->bin().item(replaceId);
    model::BinItem item;
    if (old && old->kind == model::BinItemKind::Title) {
        // "The revised Title Effect clip replaces the previous clip in the bin" (p.360); the old media stays ("The
        // original media files are not changed", p.365).
        item = *old;
        model::reviseClassicTitleClip(item, title, media, when);
        QString base = item.name;
        if (base.startsWith(QLatin1String("unrendered."))) base = base.mid(11);
        item.name = request.fastSave ? model::unrenderedTitleName(base) : base;
        doc->replaceItem(item);
    } else {
        QString name = request.name;
        // Save Title As keeping a title's name: "<name>.01" and on (p.360).
        for (const model::BinItem& i : doc->bin().items) {
            if (i.kind == model::BinItemKind::Title && (i.name == name || i.name == model::unrenderedTitleName(name))) {
                name = model::revisionTitleName(doc->bin(), name);
                break;
            }
        }
        item = model::classicTitleClip(title, request.fastSave ? model::unrenderedTitleName(name) : name, rate, media, when);
        doc->addItem(item);
    }
    // "Media Composer loads titles into the Source monitor as they are saved" (p.359).
    composer_->load(MonitorId::Source, doc->nodeId(), item);
    if (!request.placeTrack.isEmpty()) placeTitle(item, doc->nodeId(), request.placeTrack);
    return item.id;
}

std::optional<model::TitleMedia> MainWindow::makeTitleMedia(const model::ClassicTitle& title, const QString& name, BinDocument& doc,
                                                            const QString& drive, const QString& resolution, QString* error) {
    if (!project_) return std::nullopt;
    media::MediaCreation creation = mediaCreation(QStringLiteral("titles"));
    if (!drive.isEmpty()) creation.videoDrive = drive;
    if (const media::Resolution* r = media::findResolution(resolution)) creation.resolution = *r;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto clips = media::renderClassicTitleMedia(title, name, project_->settings.format.editRate, creation, error);
    QApplication::restoreOverrideCursor();
    if (!clips) return std::nullopt;
    doc.addItem(clips->first);
    doc.addItem(clips->second);
    model::TitleMedia media;
    media.binId = doc.nodeId();
    media.fill = clips->first.id;
    media.matte = clips->second.id;
    media.resolution = creation.resolution.id;
    media.raster = creation.raster;
    return media;
}

void MainWindow::notify(const QString& caption, const QString& text) {
    // Not modal, so that commands run from tests and scripts go on.
    auto* box = new QMessageBox(QMessageBox::Warning, caption, text, QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("titleMediaMessage"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

std::optional<TitleMediaChoice> MainWindow::askTitleMedia(const QString& caption, const TitleMediaChoice& suggestion) {
    if (titleMediaHook_) return titleMediaHook_(caption, suggestion);
    return askTitleMediaChoice(this, caption, suggestion);
}

int MainWindow::recreateTitleMedia() {
    // "Load the sequence containing the titles into the Record monitor. Mark IN and OUT points in the sequence
    // surrounding all the titles that you want to re-create. In the Track Selector panel, select the Record Track
    // button for each track that contains titles you want to re-create" (FX Guide p.374). Ours: without marks, the
    // whole sequence.
    const model::MonitorState& rec = composer_->state(MonitorId::Record);
    const model::LoadedClip* loaded = rec.current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence || !project_) return 0;
    BinDocument* doc = binContainer_->document(loaded->binId);
    if (!doc) doc = binContainer_->activeDocument();
    if (!doc || doc->isReadOnly()) return 0;
    const model::Sequence seq = model::Sequence::fromItem(loaded->item);
    const QStringList tracks = timeline_->patch().selectedRecordTracks(seq);
    const bool marked = rec.in() || rec.out();
    const std::int64_t from = marked ? rec.markedStart() : 0;
    const std::int64_t to = marked ? rec.markedEnd() + 1 : std::numeric_limits<std::int64_t>::max();
    struct Found {
        QString track;
        std::int64_t start;
        model::ClassicTitle title;
        QString name;
    };
    QList<Found> found;
    for (const model::Track& t : seq.tracks()) {
        if (t.kind != model::TrackKind::Video || !tracks.contains(t.name())) continue;
        std::int64_t start = 0;
        for (const model::Segment& s : t.segments) {
            if (start < to && start + s.length > from && s.effect && model::isClassicTitleEffect(*s.effect)) {
                if (const auto title = model::classicTitleOf(*s.effect)) {
                    const model::TitleSource source = model::titleSourceOf(*s.effect);
                    const auto clip = composer_->resolve(source.bin, source.clip);
                    QString name = clip ? clip->name : tr("Title");
                    if (name.startsWith(QLatin1String("unrendered."))) name = name.mid(11);
                    found << Found{t.name(), start, *title, name};
                }
            }
            start += s.length;
        }
    }
    if (found.isEmpty()) {
        notify(tr("Re-create Title Media"), tr("There are no titles on the selected tracks."));
        return 0;
    }
    // "The default drive and resolution are defined in the Titles tab of the Media Creation dialog box" (p.374).
    const media::MediaCreation creation = mediaCreation(QStringLiteral("titles"));
    const auto choice = askTitleMedia(tr("Re-create Title Media"), TitleMediaChoice{creation.videoDrive, creation.resolution.id});
    if (!choice) return 0;
    QList<model::TitleMedia> made;
    for (const Found& f : found) {
        QString error;
        const auto media = makeTitleMedia(f.title, f.name, *doc, choice->drive, choice->resolution, &error);
        if (!media) {
            notify(tr("Re-create Title Media"), error.isEmpty() ? tr("The title's media could not be made.") : error);
            return 0;
        }
        made << *media;
    }
    int done = 0;
    timeline_->changeRecordSequence(tr("Re-create Title Media"), [&](model::Sequence& s) {
        for (qsizetype i = 0; i < found.size(); ++i) {
            model::Track* track = s.track(found.at(i).track);
            if (!track) continue;
            std::int64_t start = 0;
            for (model::Segment& seg : track->segments) {
                if (start == found.at(i).start) {
                    model::setTitleMedia(seg, made.at(i));
                    ++done;
                    break;
                }
                start += seg.length;
            }
        }
        return done > 0;
    });
    return done;
}

int MainWindow::createUnrenderedTitleMedia() {
    // "In the bin, select the titles for which you want to generate media ... Select Timeline > Create Unrendered Title
    // Media" (FX Guide p.375).
    BinDocument* doc = binContainer_->activeDocument();
    BinView* view = doc ? binContainer_->viewFor(doc) : nullptr;
    if (!doc || !view || doc->isReadOnly() || !project_) return 0;
    QList<model::BinItem> titles;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = doc->bin().item(id); item && model::isUnrenderedTitle(*item)) titles << *item;
    }
    if (titles.isEmpty()) return 0;
    // "The default resolution is the resolution specified at the time you originally saved the titles with the Fast
    // Save option" (p.375).
    const media::MediaCreation creation = mediaCreation(QStringLiteral("titles"));
    TitleMediaChoice suggestion{creation.videoDrive, creation.resolution.id};
    if (const auto segment = model::titleSegmentOf(titles.constFirst())) {
        const QString saved = model::titleMediaOf(*segment).resolution;
        if (!saved.isEmpty()) suggestion.resolution = saved;
    }
    const auto choice = askTitleMedia(tr("Create Unrendered Title Media"), suggestion);
    if (!choice) return 0;
    int done = 0;
    for (model::BinItem item : titles) {
        const auto title = model::classicTitleOf(item);
        if (!title) continue;
        const QString name = item.name.startsWith(QLatin1String("unrendered.")) ? item.name.mid(11) : item.name;
        QString error;
        const auto media = makeTitleMedia(*title, name, *doc, choice->drive, choice->resolution, &error);
        if (!media) {
            notify(tr("Create Unrendered Title Media"), error.isEmpty() ? tr("The title's media could not be made.") : error);
            break;
        }
        // "The title clip appears in the bin without the (unrendered) label in the name field" (p.375).
        model::reviseClassicTitleClip(item, *title, *media, QDateTime::currentDateTimeUtc());
        item.name = name;
        doc->replaceItem(item);
        ++done;
    }
    return done;
}

bool MainWindow::placeTitle(const model::BinItem& clip, const QString& bin, const QString& track) {
    // "The title is placed in the Timeline between the In and Out marks" (p.359): overwritten on `track`.
    const model::MonitorState& rec = composer_->state(MonitorId::Record);
    auto segment = model::titleSegmentOf(clip);
    if (!segment || !rec.in() || !rec.out() || *rec.out() < *rec.in()) return false;
    const std::int64_t in = *rec.in();
    segment->length = *rec.out() - in + 1; // marks are inclusive
    if (segment->effect) model::setTitleSource(*segment->effect, {bin, clip.id});
    return timeline_->changeRecordSequence(tr("Place Title"), [&](model::Sequence& seq) {
        if (!seq.ensureTrack(track)) return false;
        seq.overwrite(track, in, {*segment});
        return true;
    });
}

model::FontMap MainWindow::fontReplacements() const {
    const model::SettingsFile& f = settings_->file(model::SettingsScope::Site);
    model::FontMap map;
    for (int i : f.versions(QStringLiteral("Font Replacement"))) {
        const model::FontMap one = model::fontMapFromJson(f.entries.at(i).values);
        for (auto it = one.constBegin(); it != one.constEnd(); ++it) map.insert(it.key(), it.value());
    }
    return map;
}

void MainWindow::saveFontReplacement(const QString& original, const QString& replacement) {
    const QString kind = QStringLiteral("Font Replacement");
    model::SettingsFile& f = settings_->file(model::SettingsScope::Site);
    model::FontMap map = fontReplacements();
    map.insert(original, replacement);
    const QList<int> versions = f.versions(kind);
    int index = versions.isEmpty() ? -1 : versions.first();
    if (index >= 0) f.entries[index].values = model::fontMapToJson(map);
    else index = f.add(model::SettingEntry{kind, QString(), false, model::fontMapToJson(map)});
    f.setActive(index);
    settings_->commit(model::SettingsScope::Site);
}

QList<model::ClassicTitleStyle> MainWindow::titleStyles() const {
    QList<model::ClassicTitleStyle> out;
    const model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    for (int i : f.versions(QStringLiteral("Title Style"))) {
        model::ClassicTitleStyle style = model::ClassicTitleStyle::fromJson(f.entries.at(i).values);
        // The setting's name is the style's ("This name appears in the Style list in the Title tool and in the Settings
        // list", p.363): renamed in the Settings window, it follows.
        style.name = f.entries.at(i).name.trimmed();
        if (!style.name.isEmpty()) out << style;
    }
    return out;
}

void MainWindow::saveTitleStyle(const model::ClassicTitleStyle& style) {
    const QString kind = QStringLiteral("Title Style");
    model::SettingsFile& f = settings_->file(model::SettingsScope::User);
    int index = -1;
    for (int i : f.versions(kind)) {
        if (f.entries.at(i).name.trimmed().compare(style.name, Qt::CaseInsensitive) == 0) index = i;
    }
    if (index >= 0) f.entries[index].values = style.toJson();
    else index = f.add(model::SettingEntry{kind, style.name, false, style.toJson()});
    f.setActive(index);
    settings_->commit(model::SettingsScope::User);
}

bool MainWindow::editTitleInSequence(const model::SegmentRef& ref) {
    const model::MonitorState& rec = composer_->state(MonitorId::Record);
    const model::LoadedClip* loaded = rec.current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) return false;
    const auto segment = model::segmentFor(model::Sequence::fromItem(loaded->item), ref);
    if (!segment || !segment->effect || !model::isClassicTitleEffect(*segment->effect)) return false;
    const auto title = model::classicTitleOf(*segment->effect);
    if (!title) return false;
    // The clip it was edited from, when its bin is open; otherwise Save Title makes a new clip (ours).
    const model::TitleSource source = model::titleSourceOf(*segment->effect);
    const BinDocument* doc = source.isEmpty() ? nullptr : binContainer_->document(source.bin);
    const model::BinItem* clip = doc ? doc->bin().item(source.clip) : nullptr;
    QString name = clip ? clip->name : tr("Title");
    if (name.startsWith(QLatin1String("unrendered."))) name = name.mid(11);
    // "A dialog box opens, asking if you want to promote the title to Marquee" (p.372): ours, without Marquee, it
    // opens in the Title tool directly.
    openTitleTool(false);
    TitleToolWindow* tool = titleTool_;
    if (!tool) return false;
    const QString sequenceId = loaded->item.id;
    tool->openInSequence(*title, clip ? source.bin : QString(), clip ? source.clip : QString(), name,
                         [this, ref, sequenceId](const QString& bin, const QString& clipId) {
        const BinDocument* saved = binContainer_->document(bin);
        if (!saved) saved = binContainer_->activeDocument();
        const model::BinItem* item = saved ? saved->bin().item(clipId) : nullptr;
        const model::LoadedClip* now = composer_->state(MonitorId::Record).current();
        // The sequence may have been closed while the Title tool was open: the clip is saved, the sequence unchanged.
        if (!item || !now || now->item.id != sequenceId) return;
        const QString binId = saved->nodeId();
        // "The revised Title Effect clip replaces the previous clip in the sequence" (p.373).
        timeline_->changeRecordSequence(tr("Revise Title"), [&](model::Sequence& seq) {
            model::Track* track = seq.track(ref.track);
            if (!track) return false;
            std::int64_t start = 0;
            for (model::Segment& s : track->segments) {
                if (start == ref.start) {
                    if (!s.effect || !model::isClassicTitleEffect(*s.effect)) return false;
                    model::reviseTitleSegment(s, *item, binId);
                    return true;
                }
                start += s.length;
            }
            return false;
        });
    });
    tool->updateBackground();
    return true;
}

bool MainWindow::reviseTitle(const QString& binId, const QString& itemId) {
    const BinDocument* doc = binContainer_->document(binId);
    const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
    const auto title = item ? model::classicTitleOf(*item) : std::nullopt;
    if (!title) return false;
    openTitleTool(false);
    TitleToolWindow* tool = titleTool_;
    if (!tool) return false;
    QString name = item->name;
    if (name.startsWith(QLatin1String("unrendered."))) name = name.mid(11);
    tool->openClip(*title, binId, itemId, name, model::isUnrenderedTitle(*item));
    return true;
}

void MainWindow::openTitleTool(bool ask) {
    // "If you know you want to create only Marquee titles or only standard titles for the current session, select
    // Persist" (p.327).
    if (ask && !titleToolPersist_) {
        bool persist = false;
        if (!askNewTitle(this, &persist)) return;
        titleToolPersist_ = persist;
    }
    TitleToolWindow* tool = titleTool_;
    if (!tool) {
        tool = new TitleToolWindow(this);
        titleTool_ = tool;
        // The Record monitor's frame at the position, for the video background (p.333).
        tool->setFrameSource([this](QSize size) { return composer_->picture(MonitorId::Record, size); });
        if (project_) tool->setFrameLines(project_->settings.format.height);
        // Saving (6.8d4a): the open bins, the Media Creation setting's Titles tab, and the save itself.
        TitleToolWindow::Saver saver;
        saver.bins = [this] {
            QList<std::pair<QString, QString>> out;
            for (BinDocument* doc : binContainer_->openDocuments()) out.append({doc->nodeId(), doc->name()});
            return out;
        };
        saver.defaults = [this] {
            TitleToolWindow::SaveRequest r;
            const media::MediaCreation c = mediaCreation(QStringLiteral("titles"));
            r.drive = c.videoDrive;
            r.resolution = c.resolution.id;
            if (BinDocument* doc = binContainer_->activeDocument()) r.bin = doc->nodeId();
            // The project's aspect ratio to start with (p.989).
            if (project_) r.aspects << (project_->settings.aspect.toDouble() > 1.5 ? QStringLiteral("16:9") : QStringLiteral("4:3"));
            return r;
        };
        saver.save = [this](const model::ClassicTitle& title, const TitleToolWindow::SaveRequest& request, const QString& replaceId, QString* error) {
            return saveClassicTitle(title, request, replaceId, error);
        };
        saver.placeTracks = [this] {
            // The record sequence's video tracks and the next one, with both marks set (p.359).
            const model::MonitorState& rec = composer_->state(MonitorId::Record);
            const model::LoadedClip* loaded = rec.current();
            if (!loaded || loaded->item.kind != model::BinItemKind::Sequence || !rec.in() || !rec.out()) return QStringList();
            const model::Sequence seq = model::Sequence::fromItem(loaded->item);
            QStringList tracks;
            int top = 0;
            for (const model::Track& t : seq.tracks()) {
                if (t.kind != model::TrackKind::Video) continue;
                tracks << t.name();
                top = std::max(top, t.number);
            }
            tracks << model::trackName(model::TrackKind::Video, top + 1);
            return tracks;
        };
        tool->setSaver(saver);
        tool->setClipOpener([this](const QString& bin, const QString& itemId) { return reviseTitle(bin, itemId); });
        // Title templates in the user's settings folder ("a Title_Templates folder within the Settings folder", p.360)
        // and title styles as "Title Style" User settings (p.364).
        const QString userSettings = settings_->path(model::SettingsScope::User);
        tool->setTemplateFolder(userSettings.isEmpty() ? QDir::home().filePath(QStringLiteral("Title_Templates"))
                                                       : QFileInfo(userSettings).dir().filePath(QStringLiteral("Title_Templates")));
        TitleToolWindow::StyleStore store;
        store.styles = [this] { return titleStyles(); };
        store.save = [this](const model::ClassicTitleStyle& style) { saveTitleStyle(style); };
        tool->setStyleStore(store);
        // Font mappings as a Site setting ("Media Composer saves font relationships as a site settings file", p.994):
        // one "Font Replacement" entry of original -> replacement families.
        TitleToolWindow::FontStore fonts;
        fonts.map = [this] { return fontReplacements(); };
        fonts.save = [this](const QString& original, const QString& replacement) { saveFontReplacement(original, replacement); };
        if (fontReplacementAsk_) fonts.ask = fontReplacementAsk_;
        if (installedFontsHook_) fonts.installed = installedFontsHook_;
        tool->setFontStore(fonts);
    }
    if (!tool->isVisible()) tool->newTitle();
    tool->updateBackground();
    tool->show();
    tool->raise();
    tool->activateWindow();
}

model::SafeColorLimits MainWindow::safeColorLimits() const {
    QJsonObject values;
    const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Safe Colors"));
    if (kind && settings_) {
        for (const SettingTab& t : kind->tabs) {
            for (const SettingOption& o : t.options) {
                const QJsonValue v = settings_->value(kind->kind, o.key);
                if (!v.isUndefined() && !v.isNull()) values.insert(o.key, v);
            }
        }
    }
    return model::SafeColorLimits::fromValues(values);
}

// Create Stereoscopic Clip (8.6d1): two master clips selected in the active bin become one Stereo Master Clip (p.243).
// Ours: the left eye is the clip whose name ends in "L" or "Left" (after a space, "_", "-" or "."), else the first in
// the bin; the new clip goes in the same bin, selected. "You can only work with stereoscopic material in an HD project"
// (p.66): refused in SD projects.
QString MainWindow::createStereoscopicClip() {
    BinView* view = binContainer_->activeView();
    if (!view || !project_) return {};
    if (project_->settings.format.family == FormatFamily::SD) {
        reportFailure(this, tr("Stereoscopic clips need an HD or high-resolution project."));
        return {};
    }
    BinDocument& doc = view->document();
    QList<model::BinItem> clips;
    for (const model::BinItem& item : doc.bin().items) {
        if (view->selectedIds().contains(item.id)) clips << item;
    }
    if (clips.size() != 2) {
        reportFailure(this, tr("Select the left and right eye master clips to make a stereoscopic clip."));
        return {};
    }
    static const QRegularExpression rightEye(QStringLiteral("(^|[ _.-])(r|right)$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression leftEye(QStringLiteral("(^|[ _.-])(l|left)$"), QRegularExpression::CaseInsensitiveOption);
    if (rightEye.match(clips.at(0).name).hasMatch() || leftEye.match(clips.at(1).name).hasMatch()) clips.swapItemsAt(0, 1);
    QString error;
    const auto stereo = model::makeStereoClip(clips.at(0), clips.at(1), &error);
    if (!stereo) {
        reportFailure(this, tr("Create Stereoscopic Clip: %1").arg(error));
        return {};
    }
    doc.addItem(*stereo);
    view->selectIds({stereo->id});
    return stereo->id;
}

// The S3D Leading Eye of the stereo clips selected in the active bin (p.66). Ours: from the bin's right-click menu.
int MainWindow::setStereoLeadingEye(model::Eye eye) {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    int changed = 0;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = view->document().bin().item(id);
        if (!item || !model::isStereoClip(*item)) continue;
        model::BinItem clip = *item;
        if (model::setLeadingEye(clip, eye)) {
            storeClip(view->document().nodeId(), clip);
            ++changed;
        }
    }
    if (changed > 0) frames_->clear();
    return changed;
}

} // namespace lumenis::ui
