#pragma once

#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <memory>
#include <optional>

class QAction;
class QMessageBox;
class QToolButton;

namespace lumenis::media {
class AudioPlayer;
struct AudioPiece;
}

namespace lumenis::model {
struct LoadedClip;
}

namespace lumenis::ext {
class ProviderRegistry;
}

namespace lumenis::ui {

class BinContainer;
enum class MonitorId;
class CalculatorWindow;
class AboutInfoDialog;
class AudioMixer;
class AudioTool;
class EffectEditor;
class FullScreenView;
class LinkedMediaDialog;
class MediaFrames;
class MediaTool;
class MediaToolDisplayDialog;
class EffectPalette;
class CommandPalette;
class ConsoleWindow;
class CommandRegistry;
class Composer;
class KeyboardMap;
class KeyboardPalette;
class MarkersWindow;
class Panel;
class PanelManager;
class Settings;
class SettingsList;
class SettingsWindow;
class SiteSettingsWindow;
class SourceBrowser;
class ToolTipFilter;
class Timeline;
class ToolPalette;
class WorkspaceBar;
class WorkspaceManager;

// Application frame: the menu bar, the paneled interface (Bin Container,
// Composer and Timeline, guide p.74) and the workspace toolbar on the right
// (p.87).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // The open project, shown in the title bar. File > Close Project is
    // available only while a project is open.
    void setProject(const model::ProjectEntry& project);
    void clearProject();
    const std::optional<model::ProjectEntry>& project() const { return project_; }

    PanelManager& panels() const { return *panels_; }
    BinContainer& binContainer() const { return *binContainer_; }
    Composer& composer() const { return *composer_; }
    Timeline& timeline() const { return *timeline_; }
    WorkspaceManager& workspaces() const { return *workspaces_; }
    // The user's, project's and site settings (ch.33); the windows apply
    // them as they change.
    Settings& settings() const { return *settings_; }
    // File > Settings (p.1136) and Windows > Site Settings (p.1141).
    SettingsWindow* showSettings();
    SiteSettingsWindow* showSiteSettings();
    // Tools (ch.5) open as floating panels, which can be docked and tabbed
    // like other panels (p.94): "CommandPalette", "ToolPalette",
    // "Calculator", "Console", "EffectPalette", "EffectEditor", "AudioMixer", "AudioTool",
    // "MediaTool".
    Panel* showTool(const QString& panelId);
    CommandPalette& commandPalette() const { return *commandPalette_; }
    ToolPalette& toolPalette() const { return *toolPalette_; }
    CalculatorWindow& calculator() const { return *calculator_; }
    ConsoleWindow& console() const { return *console_; }
    EffectPalette& effectPalette() const { return *effectPalette_; }
    EffectEditor& effectEditor() const { return *effectEditor_; }
    AudioMixer& audioMixer() const { return *audioMixer_; }
    AudioTool& audioTool() const { return *audioTool_; }
    MediaTool& mediaTool() const { return *mediaTool_; }
    // File > Input > Source Browser (p.312-318).
    SourceBrowser& sourceBrowser() const;
    void showSourceBrowser();
    // Links media files into a bin (p.317), returning the new clips' ids.
    // Files that are not media are left out and named in the Console.
    QStringList linkFiles(const QString& binId, const QStringList& paths);
    // Links files without putting them in a bin and loads them into a
    // monitor, to review (p.315-316).
    void loadFiles(const QStringList& paths, bool recordMonitor = false);
    // "Add volume Bin" (p.318): a new bin named after the folder, with every
    // media file in it and the folders in it. Returns the bin's id.
    QString linkVolume(const QString& folder);
    // The project's rate and the Link settings.
    media::LinkOptions linkOptions() const;
    // Clip > Relink > Linked Media (p.364-365) and Clip > Modify > Unlink
    // Media (p.368). The hook stands in for showing the dialog (tests).
    void relinkLinkedMedia();
    void setLinkedMediaHook(std::function<void(LinkedMediaDialog&)> hook) { linkedMediaHook_ = std::move(hook); }
    int unlinkSelection();
    // Playback's sound (2.3): whether it plays, and its clock: the output
    // sample heard now, counted from where play started.
    bool isPlayingSound() const;
    // Digital audio scrub: whether a burst is playing, and its first output
    // sample.
    bool isScrubbing() const { return scrubbing_; }
    std::int64_t scrubStart() const { return scrubStart_; }
    // Composer > Full Screen Playback (p.391).
    void toggleFullScreen();
    FullScreenView* fullScreenView() const;
    std::int64_t soundClock() const;
    // Tools > Media Tool: the Media Tool Display dialog, then the tool
    // (p.346). Tests answer the dialog through the hook (true: OK).
    void showMediaTool();
    void setMediaToolDisplayHook(std::function<bool(MediaToolDisplayDialog&)> hook) {
        mediaToolDisplay_ = std::move(hook);
    }
    // Help > About's Configuration, Usage and Hardware (p.102-105).
    AboutInfoDialog* createAboutInfo();
    KeyboardPalette& keyboardPalette() const { return *keyboardPalette_; }
    KeyboardMap& keyboard() const { return *keyboard_; }
    // The workspace each of the W1-W12 buttons switches to (p.90-91).
    QStringList workspaceButtons() const;

    // The layout of a new user (p.74): Bin Container top left, Composer to
    // its right, Timeline along the bottom.
    static QJsonObject defaultLayout();

    // Help > About: the product, its licence and the application picture.
    // The caller owns the box.
    QMessageBox* createAboutBox();
    static constexpr int kAboutPictureSize = 240; // logical pixels
    // Tools > Project's first size; the guide gives none.
    static constexpr int kProjectWidth = 300;
    static constexpr int kProjectHeight = 460;

signals:
    // File > Close Project: the application returns to Select Project.
    void closeProjectRequested();
    // The user closed the window (or chose File > Exit).
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installHandlers();
    void installPanelHandlers();
    void installProjectHandlers(bool projectOpen);
    bool binsActive() const;
    void createPanels();
    void syncWorkspaceUi();
    void placeWorkspaceButton();
    void newWorkspace();
    void deleteWorkspace();
    void restoreCurrentToDefault();
    void showAbout();
    void applySettings();
    void createTools();
    // Workspaces are User settings, one Workspace View each (p.86,
    // p.89-90): written when they change, read when a user's settings load.
    void writeWorkspaces();
    void readWorkspaces();
    // Link to Named settings (p.89-90): the linked workspace's settings
    // versions become active.
    void linkSettings(const QString& workspace);
    void updateWorkspaceButtons();
    // File > Get Position Info (p.101): the Timeline at the position, in the
    // Console.
    void positionInfo();
    SettingsList* activeSettingsList() const;

    CommandRegistry& commands_;
    const ext::ProviderRegistry& providers_;
    std::optional<model::ProjectEntry> project_;
    PanelManager* panels_ = nullptr;
    BinContainer* binContainer_ = nullptr;
    Composer* composer_ = nullptr;
    Timeline* timeline_ = nullptr;
    MarkersWindow* markersWindow_ = nullptr;
    CommandPalette* commandPalette_ = nullptr;
    ToolPalette* toolPalette_ = nullptr;
    CalculatorWindow* calculator_ = nullptr;
    EffectPalette* effectPalette_ = nullptr;
    EffectEditor* effectEditor_ = nullptr;
    AudioMixer* audioMixer_ = nullptr;
    AudioTool* audioTool_ = nullptr;
    MediaTool* mediaTool_ = nullptr;
    MediaFrames* frames_ = nullptr;
    // Guarded: bins save (and signal) as the window is taken down.
    QPointer<SourceBrowser> sourceBrowser_;
    bool sourceBrowserOpen_ = false;
    std::unique_ptr<media::AudioPlayer> audio_;
    QPointer<FullScreenView> fullScreen_;
    // Digital audio scrub (p.676): the last position seen, and the burst.
    QString scrubItem_;
    std::int64_t scrubPosition_ = -1;
    std::int64_t scrubStart_ = 0;
    int scrubToken_ = 0;
    bool scrubbing_ = false;
    void scrubIfMoved();
    std::vector<media::AudioPiece> soundOf(const model::LoadedClip& loaded,
                                           const std::function<bool(const QString&)>& monitored) const;
    // What a monitor shows at a position: a linked clip and its frame.
    std::optional<std::pair<model::BinItem, std::int64_t>> frameAt(MonitorId monitor, std::int64_t position) const;
    void startSound(MonitorId monitor, int speed);
    std::function<void(LinkedMediaDialog&)> linkedMediaHook_;
    // Puts a changed clip back in its bin and the monitors.
    void storeClip(const QString& binId, const model::BinItem& item);
    void refreshSourceBins();
    std::function<bool(MediaToolDisplayDialog&)> mediaToolDisplay_;
    QString appliedMixerMode_;
    QDateTime projectOpened_;
    QList<model::BinItem> projectItems(int* binCount = nullptr) const;
    ConsoleWindow* console_ = nullptr;
    KeyboardPalette* keyboardPalette_ = nullptr;
    Settings* settings_ = nullptr;
    SettingsWindow* settingsWindow_ = nullptr;
    SiteSettingsWindow* siteSettingsWindow_ = nullptr;
    ToolTipFilter* toolTips_ = nullptr;
    bool syncingWorkspaces_ = false;
    QString linkedWorkspace_;
    std::unique_ptr<KeyboardMap> keyboard_;
    WorkspaceManager* workspaces_ = nullptr;
    WorkspaceBar* bar_ = nullptr;
    QToolButton* workspaceButton_ = nullptr;
    QList<QAction*> workspaceActions_;
};

} // namespace lumenis::ui
