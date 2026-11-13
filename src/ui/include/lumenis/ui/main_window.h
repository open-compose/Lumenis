#pragma once

#include "lumenis/media/link.h"
#include "lumenis/model/sync_clips.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/copy_media.h"
#include "lumenis/media/dynamic_media_folders.h"
#include "lumenis/media/dynamic_relink.h"
#include "lumenis/model/stereo_clip.h"
#include "lumenis/model/stereo_view.h"
#include "lumenis/media/graphic_import.h"
#include "lumenis/media/psd.h"
#include "lumenis/media/sequence_report.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/closed_captions.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/send_to.h"
#include "lumenis/model/safe_colors.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/transcript.h"
#include "lumenis/model/transcript_view.h"
#include "lumenis/model/script.h"
#include "lumenis/model/script_sync.h"
#include "lumenis/ui/batch_import_dialog.h"
#include "lumenis/ui/background_services.h"
#include "lumenis/ui/audio_punch_in.h"
#include "lumenis/ui/audio_suite.h"
#include "lumenis/ui/title_tool.h"
#include "lumenis/ui/proxy_dialogs.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QMainWindow>
#include <QPointer>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>
#include <string>

class QAction;
class QPrinter;
class QTimer;
class QMessageBox;
class QToolButton;

namespace lumenis::media {
class AudioPlayer;
struct Composition;
struct MovieSource;
struct MovieExportOptions;
enum class EffectPrecision;
struct AudioPiece;
}

namespace lumenis::model {
struct LoadedClip;
}

namespace lumenis::ext {
class ProviderRegistry;
class TranscriptionProvider;
class SharedStorageProvider;
}

namespace lumenis::ui {

class BinContainer;
enum class MonitorId;
class BackgroundQueue;
class DmfProfileEditor;
class DynamicMediaFoldersWindow;
class SourceSettingsDialog;
class BackgroundQueueWindow;
class CalculatorWindow;
class ColorInfoTool;
class TranscriptTool;
class SpeakerNamesDialog;
class TranscribeTracksDialog;
class ScriptWindow;
class ScriptSyncDialog;
class ManageTranscriptsDialog;
struct SpeakerNameChange;
class ConsolidateDialog;
class UnmountDialog;
class SetFontDialog;
class ShortcutsWindow;
class FindCommandWindow;

class AncDataImportDialog;
class AudioMixdownDialog;
class VideoMixdownDialog;
class ClearRendersDialog;
class ExpertRenderDialog;
class BinDocument;
class FreezeFrameDialog;
class MotionEffectDialog;
class MotionEffectEditor;
class RenderEffectDialog;
class MultiMixDialog;
class ExportAsDialog;
class SendToDialog;
class ExportDeviceDialog;
class ExportVolumeDialog;
class ProToolsOptionsDialog;
class SettingsDialog;
class ExportBinDialog;
class ImportEdlDialog;
class ListTool;
class AboutInfoDialog;
class AudioEqTool;
class ModifyDialog;
class AudioPunchInTool;
class DigitalCutTool;
class VideoOutputTool;
class ExportCaptionsDialog;
class MasterCaptionListDialog;
class SubcapPropertiesDialog;
class AudioTrackEffectTool;
class AudioMixer;
class AudioTool;
class CreateToneMediaDialog;
class EffectEditor;
class FullScreenView;
class LinkedMediaDialog;
class ManagedRelinkDialog;
class MediaFrames;
class MediaTool;
class MediaToolDisplayDialog;
class EffectPalette;
class CommandPalette;
class ConsoleWindow;
class PrintDocument;
class SequenceReportDialog;
class CommandRegistry;
class Composer;
class KeyboardMap;
class KeyboardPalette;
class MarkersWindow;
class ClipNotesWindow;
class TimecodeWindow;
class FindWindow;
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
    // `commands` and `providers` must outlive the window. The window starts with no project;
    // app/main.cpp calls setProject() when one opens.
    MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // The open project, shown in the title bar. File > Close Project is
    // available only while a project is open.
    void setProject(const model::ProjectEntry& project);

    // Closes the project: bins, monitors, undo history and project settings are cleared and the
    // project-only commands disabled.
    void clearProject();
    const std::optional<model::ProjectEntry>& project() const { return project_; }

    // Favorite Bins and saved bin views as builds before they were User settings kept them
    // (FavoriteBins::legacy, BinViewLibrary::legacy; null for none). Each is written into the active
    // Bin setting the first time a user is active whose setting has none, so updating loses neither.
    // legacyBinListsPending() is true until both are written; app/main.cpp then forgets the old ones.
    void adoptLegacyBinLists(const QJsonValue& favorites, const QJsonValue& views);
    bool legacyBinListsPending() const { return !legacyFavorites_.isNull() || !legacyBinViews_.isNull(); }

    // The main panels and the managers, created in the constructor and alive as long as the window.
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
    // "MediaTool", also "Keyboard", "SourceBrowser", "BackgroundQueue" and "ListTool". An open panel is brought
    // forward; a closed one floats at its first size. Null for an unknown id.
    Panel* showTool(const QString& panelId);
    // The tools' widgets, created in the constructor whether or not their panels are open.
    CommandPalette& commandPalette() const { return *commandPalette_; }
    ToolPalette& toolPalette() const { return *toolPalette_; }
    CalculatorWindow& calculator() const { return *calculator_; }
    ConsoleWindow& console() const { return *console_; }
    EffectPalette& effectPalette() const { return *effectPalette_; }
    EffectEditor& effectEditor() const { return *effectEditor_; }
    AudioMixer& audioMixer() const { return *audioMixer_; }
    // The Timeline Clip Notes window, once Tools > Timeline Clip Notes has opened it (p.484).
    ClipNotesWindow* clipNotesWindow() const { return clipNotesWindow_; }
    // The Timecode window, once Tools > Timecode Window has opened it (p.389).
    TimecodeWindow* timecodeWindow() const { return timecodeWindow_; }
    // Bin > AutoSequence and Clip > AutoSequence (p.499-500): a sequence of the active bin's selected
    // clips by source timecode (`noFiller`, with Alt: one after another), put in that bin as
    // "<tape or source file>.NN" and loaded into the Record monitor. Returns the new item's id, or empty
    // with *error set.
    QString autoSequence(bool noFiller, QString* error = nullptr);
    // Bin > Select (p.266-267; docs/spec/bin-select.md): "offline_items" in the active bin;
    // "media_relatives" and "sources" of the selected objects, and "unreferenced_clips", in all open
    // bins. Selects them and returns how many were selected.
    int binSelect(const QString& what);
    // File > Reveal File (p.360): the media file of the clip selected in the active bin, shown in the
    // system's file browser (Explorer or the Finder select it; elsewhere its folder opens). Returns the
    // file, or empty when the clip has none that exists. The hook replaces the file browser in tests.
    QString revealFile();
    // Shows `file` in the file manager (Explorer, the Finder, the folder elsewhere); the reveal hook in tests.
    void showInFileManager(const QString& file);
    void setRevealHook(std::function<void(const QString& file)> hook) { revealHook_ = std::move(hook); }
    // The Find window, once Edit > Find has opened it (p.417, p.429).
    FindWindow* findWindow() const { return findWindow_; }
    AudioEqTool& audioEq() const { return *audioEq_; }
    AudioPunchInTool& audioPunchIn() const { return *audioPunchIn_; }
    AudioTrackEffectTool& audioTrackEffect() const { return *audioTrackEffect_; }
    AudioSuiteTool& audioSuite() const { return *audioSuite_; }
    // Fills the AudioSuite tool's Target Drive menu (Tools > AudioSuite does too).
    void prepareAudioSuite();
    // Fills the Audio Punch-In tool's input, drive and bin menus (Tools > Audio Punch-In does too).
    void preparePunchIn();
    // Whether a sequence's audio track is heard when it plays: monitored in the Track Selector
    // panel (p.395), not muted in the Audio Mixer, and soloed whenever any track is (p.698).
    bool isHeard(const QString& track) const;
    // The Audio Track Effects a loaded item plays through (media::AudioMix): its inserts, none with the
    // tool's Ctrl+Bypass, and the tool's dragged value in place of the saved one.
    media::TrackInserts insertsOf(const model::LoadedClip& loaded) const;
    // The audio of a loaded item, through the Audio Mixer's Master fader and without EQ while the
    // Audio EQ tool's Bypass RT EQ is on; `monitored`, when set, picks the sequence tracks heard.
    std::vector<media::AudioPiece> soundOf(const model::LoadedClip& loaded,
                                           const std::function<bool(const QString&)>& monitored) const;
    // A mix of a loaded item's pieces for the Audio Mixer's Mix Mode Selection (its speakers) and the
    // sequence's Sequence Mix Format, with insertsOf().
    std::shared_ptr<media::AudioMix> mixOf(const model::LoadedClip& loaded, std::vector<media::AudioPiece> pieces) const;
    AudioTool& audioTool() const { return *audioTool_; }
    MediaTool& mediaTool() const { return *mediaTool_; }
    // Tools > List Tool (p.944-960): EDLs of a sequence.
    ListTool& listTool() const { return *listTool_; }
    // Opens the List Tool with the sequence selected in the active bin, else the Record monitor's
    // (p.948 step 3).
    void showListTool();
    // File > Input > Source Browser (p.312-318).
    SourceBrowser& sourceBrowser() const;

    // Opens it with the project's bins as targets, at the home folder the first time.
    void showSourceBrowser();
    // Links media files into a bin (p.317), returning the new clips' ids.
    // Files that are not media are left out and named in the Console.
    // The bin is opened if needed; empty if it cannot be.
    QStringList linkFiles(const QString& binId, const QStringList& paths);
    // Links files without putting them in a bin and loads them into a
    // monitor, to review (p.315-316).
    void loadFiles(const QStringList& paths, bool recordMonitor = false);
    // "Add volume Bin" (p.318): a new bin named after the folder, with every
    // media file in it and the folders in it. Returns the bin's id.
    // Empty without a project.
    QString linkVolume(const QString& folder);
    // How a sequence's segments find their clips (the bins they name, else the open bins by MobID),
    // for the List Tool.
    std::function<std::optional<model::BinItem>(const MobId&, const QString&)> clipResolver(const model::BinItem& sequence) const;
    // The project's rate and the Link settings.
    media::LinkOptions linkOptions() const;
    // Clip > Relink > Linked Media (p.364-365) and Clip > Modify > Unlink
    // Media (p.368). The hook stands in for showing the dialog (tests).
    void relinkLinkedMedia();
    void setLinkedMediaHook(std::function<void(LinkedMediaDialog&)> hook) { linkedMediaHook_ = std::move(hook); }

    // Returns the number of clips unlinked.
    int unlinkSelection();
    // Clip > Relink > Managed Media (p.361-364): the selection, or with
    // "Selected items in ALL open bins" every open bin's. The hook stands in
    // for the dialog (tests; true: OK).
    void relinkManagedMedia();
    // File > Media > Unmount (p.343-344): the Unmount Disk or Drives dialog; the hook answers it in
    // tests (false for Cancel).
    void unmountDrives();
    // Windows > Center Current (home false) and Send Current Home (p.601): the active panel's floating
    // window centred on its screen, or put back at its preferred size centred on the main window.
    // False when the active panel is not floating.
    bool placeCurrentWindow(bool home);
    // Windows > Set Font (p.81-82): the Set Font dialog for the active Bin, Composer or Timeline; the
    // font goes into the bin, the Composer setting or the Timeline's display (its Timeline View).
    // False when no such window is active or the dialog is cancelled. The hook answers the dialog.
    bool chooseFont();
    // Windows > Bin Layout (p.92-93; docs/spec/bin-layout.md): named Bin Layout Project settings, each
    // holding BinContainer::binLayout(). New saves the current arrangement under a name (replacing one of
    // that name), Save Current saves it into the layout last opened or made, Delete removes one, Open
    // applies one. Each returns false when there is no such layout (or no name).
    QStringList binLayouts() const;
    bool newBinLayout(const QString& name);
    bool saveCurrentBinLayout();
    bool deleteBinLayout(const QString& name);
    bool openBinLayout(const QString& name);
    QString currentBinLayout() const { return currentBinLayout_; }
    // Called when a workspace becomes active (public for tests). Its Workspace View's Bin Layout opens
    // (p.91); with Link to Named settings (p.89-90) the linked workspace's settings versions become
    // active.
    void linkSettings(const QString& workspace);
    // Answers the New Bin Layout (no choices: a name) and Delete Bin Layout (a choice) dialogs in tests;
    // nullopt is Cancel.
    using BinLayoutAsk = std::function<std::optional<QString>(const QStringList& choices)>;
    void setBinLayoutHook(BinLayoutAsk hook) { binLayoutHook_ = std::move(hook); }
    // Help > Shortcuts (p.97), once opened.
    ShortcutsWindow* shortcutsWindow() const { return shortcutsWindow_; }
    FindCommandWindow* findCommandWindow() const { return findCommandWindow_; }
    void setFontHook(std::function<bool(SetFontDialog&)> hook) { fontHook_ = std::move(hook); }
    void setUnmountHook(std::function<bool(UnmountDialog&)> hook) { unmountHook_ = std::move(hook); }
    // Deleting managed clips from a bin (p.359): the clips, their media
    // files, or both. The hook answers the Delete dialog (tests; nothing:
    // Cancel).
    struct BinDeleteChoice {
        bool clips = true;
        bool media = false;
    };
    void setBinDeleteHook(std::function<std::optional<BinDeleteChoice>(int clips, int files)> hook) {
        binDeleteHook_ = std::move(hook);
    }
    void setManagedRelinkHook(std::function<bool(ManagedRelinkDialog&)> hook) { managedRelinkHook_ = std::move(hook); }
    // Managed media (Phase 2.4, p.349-359). File > Input > Import Media:
    // the files as new media in a bin (p.213), at the Media Creation
    // setting's import resolution unless `resolution` names another.
    // AAF files among the paths go to importAaf(), shot logs to importShotLogs(), OpenTimelineIO
    // files to importOtio(), FCP 7 XML files to importFcpXml(). Returns the new items' ids.
    QStringList importFiles(const QString& binId, const QStringList& paths, const QString& resolution = {});
    // The Import setting's options that shape an import (p.1196-1199), as importFiles() keeps them with each clip
    // (BinItem::sourceSettings "import") for Batch Import.
    QJsonObject importSettingsSnapshot() const;
    // A linked clip imported as new managed media with `importSettings` (a snapshot as above): still pictures' length,
    // the Import setting's Audio tab, center pan. The result carries the snapshot. Nothing, with `error`, when it fails.
    std::optional<model::BinItem> importClipMedia(model::BinItem clip, const QJsonObject& importSettings,
                                                  media::MediaCreation creation, QString* error = nullptr);
    // Graphics on import (9.2b, main_window_graphics.cpp, docs/spec/graphics-import.md; p.217-218, p.1196, p.1231). The
    // Alpha Channel option of an Import settings snapshot.
    media::AlphaHandling importAlphaHandling(const QJsonObject& importSettings) const;
    // A picture with alpha imported as a Matte Key clip (model/matte_key_clip.h): fill and matte precomputes and the clip
    // into `doc`, named and placed as `clip` says (its Source File and Path). Returns the Matte Key clip.
    std::optional<model::BinItem> importMatteKeyClip(const model::BinItem& clip, const media::AlphaSource& source,
                                                     const QJsonObject& importSettings, media::MediaCreation creation,
                                                     BinDocument& doc, QString* error = nullptr, double level = 100.0);
    // Multilayer Photoshop files (9.2e, main_window_graphics.cpp, docs/spec/graphics-import.md; p.218-221). The message
    // box's choice, asked once for all the files of an import ("This selection applies to all files you select for
    // import", p.221), and the Select Layers dialog's layers (indexes into media::importableLayers; nothing for Cancel).
    // Tests set hooks in place of the dialogs.
    enum class PsdLayersChoice { SequenceOfLayers, FlattenedImage, SelectLayers, Cancel };
    void setPsdLayersChoiceHook(std::function<PsdLayersChoice(const QStringList& files)> hook) { psdChoiceHook_ = std::move(hook); }
    void setPsdSelectLayersHook(std::function<std::optional<QList<int>>(const QString& file, const QStringList& layers)> hook) {
        psdSelectHook_ = std::move(hook);
    }
    PsdLayersChoice askPsdLayersChoice(const QStringList& files);
    std::optional<QList<int>> askPsdLayers(const QString& file, const QStringList& layers);
    // The `chosen` layers of a Photoshop file into `doc`, bottom to top: a master clip for a background layer (no
    // transparency), a Matte Key clip with the layer's Opacity as its Foreground Level for the others, each named after
    // its layer; then a sequence named after the file with a video track a layer, at most kMaxPsdTracks. Returns the
    // items' ids, the sequence's last.
    static constexpr int kMaxPsdTracks = 24;
    QStringList importPsdLayers(const QString& path, const media::PsdFile& file, const QList<int>& chosen,
                                const QJsonObject& importSettings, media::MediaCreation creation, BinDocument& doc);
    // A linked clip whose file has alpha (9.2e2, main_window_graphics.cpp; p.318, p.1204), when the Link setting's Alpha
    // Channel is not Ignore: the linked clip kept in `doc` as "<name>.link", hidden like a precompute, and a Matte Key
    // clip named after it that keys its frames as they play. Nothing (and nothing added) for other clips.
    std::optional<model::BinItem> linkedMatteKeyClip(const model::BinItem& linked, BinDocument& doc);
    // Audio CD tracks (9.2d, main_window_graphics.cpp, docs/spec/audio-cd.md): each ".cda" file read off its disc by
    // an audio CD provider (ext::AudioCdProvider) into a WAV file in `folder`, named after the ".cda" file. Returns WAV
    // path -> ".cda" path; tracks no provider reads are left out with a Console line.
    QHash<QString, QString> readCdTracks(const QStringList& cdaPaths, const QString& folder);
    // A PICT file imported into `doc`: a Matte Key clip when it has alpha and the alpha is not ignored, else a master
    // clip lasting the Frame Import Duration.
    std::optional<model::BinItem> importPictFile(const QString& path, const QJsonObject& importSettings,
                                                 media::MediaCreation creation, BinDocument& doc, QString* error = nullptr);
    // The Sequence Report (9.2c, main_window_report.cpp, docs/spec/sequence-report.md; p.368-371): the bin's selected
    // sequences, or with `fromMonitor` the sequence in the active monitor; the Sequence Report dialog; each report written
    // to a text file (the first where the Save Summary Output File As dialog says, the others beside it) and opened.
    // Returns how many were written.
    int sequenceReport(bool fromMonitor = false);
    struct ReportTarget {
        QString binId;
        model::BinItem sequence;
        std::optional<MonitorId> monitor; // loaded there: its marks and, in the Record monitor, the selected tracks count
    };
    QList<ReportTarget> sequenceReportTargets(bool fromMonitor) const;
    media::SequenceReportInput sequenceReportInput(const ReportTarget& target) const;
    // Apply Changes: the sequence's name and Starting TC (its Start column), stored in its bin. False for a timecode
    // that cannot be read or a bin that cannot be changed.
    bool applySequenceReportChanges(const ReportTarget& target, const QString& name, const QString& startTimecode);
    // "Popup Report on AAF Import" (p.237-238): with the Import setting's OMFI/AAF option on, a report of each imported
    // sequence among `ids` with the options there, as text or CSV, written to the project's Reports folder (ours) and
    // opened; with Add Markers at Effect Locations, a marker at each effect the report lists. Returns the reports.
    int popupAafReports(const QString& binId, const QStringList& ids);
    // Writes a report to `path` and opens it; the path, or empty when it cannot be written.
    QString writeSequenceReport(const media::SequenceReportInput& input, const media::SequenceReportOptions& options,
                                const QString& path);
    // Tests: replace the dialog's exec() (true generates), the Save Summary Output File As dialog (the path, empty for
    // Cancel) and opening the report.
    void setSequenceReportHook(std::function<bool(SequenceReportDialog&)> hook) { sequenceReportHook_ = std::move(hook); }
    void setReportFileHook(std::function<QString(const QString& suggested)> hook) { reportFileHook_ = std::move(hook); }
    void setOpenReportHook(std::function<void(const QString& path)> hook) { openReportHook_ = std::move(hook); }
    // Clip > Batch Import and Batch Re-import (9.2a, main_window_batch_import.cpp, docs/spec/batch-import.md; p.233-237,
    // p.1097-1098): the message box (Offline only / All clips / Cancel), then the Batch Import dialog on the active
    // bin's selected imported master clips and the clips its selected sequences use, then runBatchImport(). Returns the
    // clips imported again.
    int batchImport();
    // The clips the Batch Import dialog lists for the active bin's selection: only those missing their media with
    // `offlineOnly`. Mixed-rate sequences are left out with a message, clips not at the project's rate with a Console
    // line.
    QList<BatchImportDialog::Entry> batchImportClips(bool offlineOnly);
    // Imports the dialog's found clips again at its resolution and drives, with each clip's last Import settings or the
    // current ones (Override clip settings with current settings). The new media take the clip's MobID; the clip
    // links to them. Returns how many.
    int runBatchImport(const BatchImportDialog& dialog);
    enum class BatchImportChoice { OfflineOnly, AllClips, Cancel };
    // Tests: replace the message box and the dialog's exec() (true imports).
    void setBatchImportChoiceHook(std::function<BatchImportChoice()> hook) { batchImportChoiceHook_ = std::move(hook); }
    void setBatchImportHook(std::function<bool(BatchImportDialog&)> hook) { batchImportHook_ = std::move(hook); }
    // AAF files imported into a bin (p.547): their clips and sequences,
    // master clips with media on the drives online by MobID.
    // Returns the new items' ids; failures and warnings go to the Console.
    QStringList importAaf(const QString& binId, const QStringList& paths);
    // Shot logs (Avid Log Exchange, p.131-132) imported into a bin: a master clip for each logged
    // event. Returns the new items' ids; warnings go to the Console.
    QStringList importShotLogs(const QString& binId, const QStringList& paths);
    // Scenarist Closed Caption files (7.4b; docs/spec/closed-captions.md; p.232): each through the AncData Import
    // dialog box (the hook stands in for it in tests) into a D-track only master clip of its caption data.
    QStringList importScc(const QString& binId, const QStringList& paths);
    void setAncDataImportHook(std::function<bool(AncDataImportDialog&)> hook) { ancDataImportHook_ = std::move(hook); }
    // The caption data of the clip a segment comes from (empty without one): what the Timeline and Show Captions read.
    QByteArray captionClipData(const model::Segment& segment) const;
    // The caption on a monitor's frame: the Record monitor's sequence's D1, or a caption clip's own data.
    std::optional<model::CaptionScreen> captionOn(MonitorId monitor, std::int64_t frame) const;
    // OpenTimelineIO files imported into a bin (p.969): a sequence for each timeline and an offline
    // master clip for each piece of media it names, unless the bin has that clip already (Relink
    // brings the media online). Also what dropping .otio files into a bin does. Returns the new
    // items' ids; failures and warnings go to the Console.
    QStringList importOtio(const QString& binId, const QStringList& paths);
    // FCP 7 XML files (ours: Media Composer has no FCP XML, docs/spec/fcp-xml.md) imported as
    // importOtio() imports OpenTimelineIO, through the same rules.
    QStringList importFcpXml(const QString& binId, const QStringList& paths);
    // File > Input > Import EDL (p.951): the Import EDL dialog (the file, the Frame Rate and the bin),
    // then the sequence and a source for each reel go into the bin. The hook stands in for the dialog
    // (tests; true: OK). Returns the new items' ids.
    QStringList importEdl();
    void setImportEdlHook(std::function<bool(ImportEdlDialog&)> hook) { importEdlHook_ = std::move(hook); }
    // File > Output > Export Bin (p.130): the active bin's master clips as a shot log. The hook
    // stands in for the Export Bin As dialog (tests; true: OK).
    void exportBin();
    void setExportBinHook(std::function<bool(ExportBinDialog&)> hook) { exportBinHook_ = std::move(hook); }
    // File > Open Bin (p.296): a bin from another project, listed under
    // Other Bins and opened. A Media Composer bin (.avb) is read into a bin
    // of ours first (interop::avb::importFile), its master clips online by
    // MobID. Returns the bin's Sidebar id; empty, with `error` set, when it
    // cannot be read. Warnings go to the Console.
    QString openBinFile(const QString& path, QString* error = nullptr);
    // File > Output > Export to File (p.868-869) on the active bin's
    // selection, with the sequences' clips: the Export As dialog, then the
    // chosen Export setting (p.1173-1177). The hook stands in for the
    // dialog (tests; true: Save).
    void exportToFile();
    // What Export to File and Send To export (7.1c2): the items selected in the active bin, the clips their sequences
    // use, and those clips' bins. Nothing (with the Console told) when nothing is selected.
    struct ExportSelection {
        QList<model::BinItem> selected;
        QList<model::BinItem> clips;
        QHash<QString, QString> binOf;
        const BinDocument* doc = nullptr;
    };
    std::optional<ExportSelection> exportSelection();
    // Writes `chosen` to `path` as the Export setting (or exportOverride_, Send To's template) says; the path's
    // extension is made the format's. False, with the Console told, when nothing was written.
    bool writeExport(const ExportSelection& chosen, QString path);
    // An Export As choice's file extension ("aaf", "mov" ...) and its file dialog filter.
    QString exportSuffix(const QString& exportAs) const;
    // The Send To dialog's Export Setting Summary (p.865) of these Export values over the setting's.
    QStringList exportSummary(const QJsonObject& values) const;
    // Save As Template: the save dialog in the templates' folder; the menu then lists it.
    bool saveSendToTemplateAs(const model::SendToTemplate& t);
    static QString exportFilter(const QString& exportAs);
    // The Export setting's value of `key`: Send To's template values when one is being exported (exportOverride_),
    // else the active Export setting's.
    QJsonValue exportSetting(const QString& key) const;
    QString exportValue(const QString& key) const { return exportSetting(key).toString(); }
    bool exportFlag(const QString& key) const { return exportSetting(key).toBool(); }
    // One sequence or clip as a MOV or MP4 movie (7.1a, docs/spec/movie-export.md) with the Export setting's options:
    // the picture through the compositor, the sound through the mixdown's mix. `format` is the Export As value ("mov",
    // "mp4", "qtMovie"). False with the reason in `error`.
    bool exportMovieFile(const model::BinItem& item, const QString& binId, const QString& path, const QString& format,
                         QString* error);
    // AAF export's Export Methods (7.1c1, Editing Guide p.870-872, p.1175-1177): Copy All Media, Consolidate Media and
    // Video Mixdown into the Media Destinations folder; the Simplified AAF's Mixdown Video/Effects to V1, Flatten Audio
    // Tracks that Contain Effects and the number of audio tracks (7.1c3, p.884-885), and Export Media Difference Between
    // Sequences (p.885-886). `items` is what the AAF then holds: the sequences and the clips, naming the new media.
    // False with the reason in `error`.
    bool prepareAafMedia(const QList<model::BinItem>& selected, const QList<model::BinItem>& clips, const QString& binId,
                         const QString& aafPath, QList<model::BinItem>& items, QStringList& warnings, QString* error);
    // File > Output > Export to Device > XDCAM or P2 (7.2b; docs/spec/export-device.md; p.883-884, p.901): the Record
    // monitor's sequence, or else the bin's selected clips and sequences, as MXF OP1a clips on a disk or card of an
    // export device (ext::ExportDeviceProvider, the built-in media::XdcamDiskProvider among them). `kind` "xdcam" or
    // "p2". The hook stands in for the dialog (tests). Returns how many clips were delivered.
    int exportToDevice(const QString& kind);
    void setExportDeviceHook(std::function<bool(ExportDeviceDialog&)> hook) { exportDeviceHook_ = std::move(hook); }
    // File > Output > Digital Cut (7.3a; docs/spec/digital-cut.md; p.926-938): openDigitalCut fills the tool with the
    // decks and the Record monitor's sequence's tracks and shows it; digitalCut previews (cues the deck and says what
    // would be recorded; 0) or records the sequence onto the chosen deck with the tool's options (the frames recorded,
    // or -1 with the reason in the tool and the Console).
    void openDigitalCut();
    // Tools > Video Output Tool (7.3b; docs/spec/video-output.md): the tool with the video output hardware and the
    // project's Video Output setting; storeVideoOutput keeps its values in the active version, or (with a name) in a
    // version of that name made active.
    // The SubCap effect's buttons (7.4a; docs/spec/subcap.md; FX Guide p.378-388): "import" (Import Caption Data into
    // the SubCap segment under the position, subdividing it), "export" (Export Caption Data of the sequence's captions),
    // "masterList" (Edit Master Caption List) and "globalProperties". The hooks stand in for the file dialogs (they
    // return the path; `save` for Export) and the dialogs (tests).
    bool subcapAction(const QString& action);
    bool editSubcapGlobalProperties();
    // Where the caption stylesheets are kept: "Caption Effect Stylesheets" beside the user's settings.
    QString subcapStylesheetFolder() const;
    void setSubcapHooks(std::function<QString(bool save)> file, std::function<bool(ExportCaptionsDialog&)> exportChoices,
                        std::function<bool(MasterCaptionListDialog&)> masterList, std::function<bool(SubcapPropertiesDialog&)> properties = {}) {
        subcapFileHook_ = std::move(file);
        exportCaptionsHook_ = std::move(exportChoices);
        masterCaptionHook_ = std::move(masterList);
        subcapPropertiesHook_ = std::move(properties);
    }
    void openVideoOutput();
    void storeVideoOutput(const QJsonObject& values, const QString& name);
    VideoOutputTool& videoOutputTool() { return *videoOutput_; }
    // The Audio Project setting's Output Gain, dB (p.1150).
    double outputGainDb() const;
    std::int64_t digitalCut(bool preview);
    // The Digital Cut tool's Timecode Duration (p.936): the cut's length on the tape at the chosen output format; with 24
    // (NTSC) and a Dest. TC Rate of 30.00 fps, counted at 30 fps.
    void updateDigitalCutDuration();
    // Tests: true stops the Digital Cut's message boxes (the odd 60 fps Mark IN, p.1296); the text still goes to the
    // tool's status line and the Console.
    void setQuietDigitalCut(bool quiet) { quietDigitalCut_ = quiet; }
    DigitalCutTool& digitalCutTool() { return *digitalCut_; }
    // AS-02 Export Volumes (7.2d; docs/spec/as02.md; p.901-904): File > New > Volume for Export makes an Export Volume
    // bin (its id; empty when cancelled); the bin's Properties button edits its volume; Commit writes its sequences not
    // yet committed as AS-02 bundles through bmxtranswrap and returns how many. The hook stands in for the dialog.
    QString newVolumeForExport();
    bool editExportVolume(const QString& binId);
    int commitExportVolume(const QString& binId);
    void setExportVolumeHook(std::function<bool(ExportVolumeDialog&)> hook) { exportVolumeHook_ = std::move(hook); }
    // Stands in for the Exclude Media Referenced from Sequence pulldown (p.886): it gets the sequences to choose from
    // and returns the chosen one's index, or -1 to cancel (tests).
    void setExcludeSequenceHook(std::function<int(const QList<model::BinItem>&)> hook) { excludeSequenceHook_ = std::move(hook); }
    // The Graphic, DPX and OpenEXR exports' stills (7.1b) of `source` (frames `start` to `end` of the item): one frame,
    // an image sequence, or the frames with markers, as the Export setting says.
    bool exportStills(const QString& path, const QString& format, const model::BinItem& sequenceItem, const media::MovieSource& source,
                      const model::LoadedClip* shown, std::int64_t start, std::int64_t end, const std::function<bool(double)>& progress,
                      QString* error);
    // File > Export Matte PICT (FX Guide p.418): the Effect Editor's AniMatte matte at the position, at the project's
    // raster, through the Export PICT As dialog (`path` given: no dialog). False when nothing was written.
    bool exportMattePict(const QString& path = QString());
    void setExportHook(std::function<bool(ExportAsDialog&)> hook) { exportHook_ = std::move(hook); }
    // Tests: called with the Export Settings dialog box that Options opens (in place of exec()); true for Save (or Save
    // As, after SettingsDialog::saveAs), false for Cancel.
    void setExportOptionsHook(std::function<bool(SettingsDialog&)> hook) { exportOptionsHook_ = std::move(hook); }
    // File > Output > Send To (7.1c2; docs/spec/send-to.md, p.863-867). sendTo exports the active bin's selection with
    // a template (`proTools`: the built-in Pro Tools template and its options); makeNewSendTo makes a template. The hooks
    // stand in for the dialogs, the save dialog (it returns the path, empty to cancel) and Auto Launch (tests).
    bool sendTo(model::SendToTemplate t, bool proTools);
    void makeNewSendTo();
    void refreshSendToMenu();
    QString sendToFolder() const;
    void setSendToHooks(std::function<bool(SendToDialog&)> dialog, std::function<bool(ProToolsOptionsDialog&)> proTools,
                        std::function<QString(const QString&)> save, std::function<bool(SettingsDialog&)> options = {},
                        std::function<void(const QString&, const QStringList&)> launch = {}) {
        sendToHook_ = std::move(dialog);
        proToolsHook_ = std::move(proTools);
        sendToSaveHook_ = std::move(save);
        sendToOptionsHook_ = std::move(options);
        launchHook_ = std::move(launch);
    }
    // Clip > Consolidate/Transcode on the active bin's selection
    // (p.350-356). The hooks stand in for the dialogs (tests): the first
    // answers Consolidate/Transcode (true: OK), the second Copying Media
    // Files (keep master clips linked; nothing: cancel).
    void consolidateTranscode();
    void setConsolidateHook(std::function<bool(ConsolidateDialog&)> hook) { consolidateHook_ = std::move(hook); }
    // Timeline > Mixdown > Audio > To Sequence (p.731-732): the selected audio tracks of the Record
    // monitor's sequence, between its marks (or all of it), mixed down into a new audio clip in the
    // chosen bin and overwritten into the target tracks, as one undo step. The hook stands in for the
    // dialog (tests; true: OK).
    void audioMixdown();
    void setMixdownHook(std::function<bool(AudioMixdownDialog&)> hook) { mixdownHook_ = std::move(hook); }
    // Timeline > Mixdown > Video (FX Guide p.227-228): the Record monitor's sequence as the monitor shows it
    // ("collapses the selected track and all tracks under it"), between its marks, rendered into a new master
    // clip in the chosen bin, on the chosen drive and resolution. The sequence is left as it is. Returns the
    // clip; the hook stands in for the dialog (tests; true: OK).
    std::optional<model::BinItem> videoMixdown();
    void setVideoMixdownHook(std::function<bool(VideoMixdownDialog&)> hook) { videoMixdownHook_ = std::move(hook); }
    // Rendering video effects (FX Guide p.57-61; docs/spec/rendering.md). Render at Position renders
    // the effects at the Record monitor's position on the selected video tracks, Render In/Out those
    // between its marks; effects already rendered are left unless `rerender` (Shift, p.61). The Render
    // Effect dialog chooses the drive unless `useLastDrive` (Alt, p.58), which takes the last one
    // chosen. Each render is written as a precompute and kept with its effect, all in one undo step;
    // Cancel during the render keeps nothing. The hook stands in for the dialog (tests; true: OK).
    void renderEffects(bool atPosition, bool rerender = false, bool useLastDrive = false);
    void setRenderHook(std::function<bool(RenderEffectDialog&)> hook) { renderHook_ = std::move(hook); }
    // Partial render (p.150): an interrupted render asks whether to Keep or Discard what was rendered;
    // tests answer here (true: Keep).
    void setRenderInterruptHook(std::function<bool()> hook) { renderInterruptHook_ = std::move(hook); }
    // The Render setting's Render Completion Sound (p.1215-1216) when a render finishes: the hook is
    // given "systemBeep" or "renderSound" in place of the sound (tests); nothing for None.
    void setRenderSoundHook(std::function<void(const QString&)> hook) { renderSoundHook_ = std::move(hook); }
    // Timeline > Render > ExpertRender In/Out (FX Guide p.140-147): the effects needing rendering on the
    // selected video tracks between the Record monitor's marks (model::expertRender), highlighted in the
    // Timeline; the Expert Render dialog renders them (OK), leaves them highlighted for Modify Selection
    // (then the Render Effect button renders the set) or cancels. The hook answers the dialog (tests).
    void expertRender();
    void setExpertRenderHook(std::function<void(ExpertRenderDialog&)> hook) { expertHook_ = std::move(hook); }
    // Composer > Freeze Frame > Other (FX Guide p.166-167): a Freeze Frame clip of the Source monitor's
    // frame, the duration from the dialog (the hook answers it in tests; true: OK), put in the source
    // clip's bin (or a bin chosen) and loaded into the Source monitor. Returns the new clip.
    std::optional<model::BinItem> freezeFrame();
    void setFreezeFrameHook(std::function<bool(FreezeFrameDialog&)> hook) { freezeHook_ = std::move(hook); }
    // The Motion Effect button with the Source monitor active (FX Guide p.168-172): the Motion Effect
    // dialog for the Source monitor's clip between its marks (all of it without), Fit To Fill from the
    // Record monitor's marks; Create makes the Variable Speed and/or Strobe Motion clip, Create and
    // Render also renders its media to the Target Drive. The new clip goes to a bin as Freeze Frame's
    // does and into the Source monitor. The hook answers the dialog (tests). Returns the new clip.
    std::optional<model::BinItem> motionEffect();
    void setMotionEffectHook(std::function<void(MotionEffectDialog&)> hook) { motionHook_ = std::move(hook); }
    // Tools' Motion Effect Editor (FX Guide p.178-189), the "MotionEffectEditor" panel.
    MotionEffectEditor& motionEffectEditor() const { return *motionEditor_; }
    // Modify Selection's end: "click the Render Effect button in the Effect Editor or in the Tool
    // palette" (p.145) renders the highlighted effects, through the Render Effect dialog unless
    // `useLastDrive` (Alt).
    void renderExpertSelection(bool useLastDrive = false);
    // Clear Renders at Position and In/Out (p.60-61): the renders of the effects there on the selected
    // video tracks forgotten (the precomputes stay), as one undo step ("Undo Clear Renders"). The hook
    // answers the Clear Renders dialog (tests; true: OK).
    void clearRenders(bool atPosition);
    void setClearRendersHook(std::function<bool(ClearRendersDialog&)> hook) { clearRendersHook_ = std::move(hook); }
    // Create Sequence Based on Selection's Select a Bin dialog (p.614): tests answer it here with the
    // index of the chosen bin name, or -1 for Cancel.
    void setBinChoiceHook(std::function<int(const QStringList& bins)> hook) { binChoiceHook_ = std::move(hook); }
    // Clip > Modify > Modify Clip (p.279-281) on the active bin's selection: the Modify dialog, and
    // the clips it changes stored. `hook` stands in for the dialog's exec() in tests. Returns how many
    // clips changed.
    int modifySelection();
    void setModifyHook(std::function<bool(ModifyDialog&)> hook) { modifyHook_ = std::move(hook); }
    // The Clip menu's audio commands on the active bin's selection (docs/spec/clip-audio.md):
    // Apply Gain (p.216) and Center Pan (p.714) on master clips and subclips, Auto VO (p.754) on one
    // sequence, which adds "<name>.vo" to the bin. Each returns how many items it changed or made.
    // The hooks stand in for the dialogs in tests: the gain (nothing: Cancel), the Center Pan
    // confirmation, and Auto VO's track and Keep segments.
    int applyClipGain();
    int centerPan();
    int autoVo();
    // Clip > Group Clips (8.1a; p.1124): the new group's id, nothing when none was made. The hook stands in for the
    // dialog in tests: the option and Auxiliary TC chosen, or nothing for Cancel.
    std::optional<QString> groupClips();
    void setGroupClipsHook(std::function<std::optional<std::pair<model::GroupSync, int>>()> hook) { groupClipsHook_ = std::move(hook); }
    // Clip > Sync Clips (9.3, the guide's AutoSync; p.496-498, docs/spec/sync-clips.md): the bin's selected clips synced
    // as the Sync Selection dialog says into synced subclips, added to the bin and selected; their ids, empty when none
    // was made (the reason in a message). The hook stands in for the dialog in tests: the options, or nothing for Cancel.
    QStringList syncClips();
    void setSyncClipsHook(std::function<std::optional<model::SyncClipsOptions>()> hook) { syncClipsHook_ = std::move(hook); }
    // Clip > Edit Group, Update Group, Create Group and Refresh Sequence > Group Clips (8.1a2; p.478, p.1125-1126): the
    // new sequence's or group's id, nothing when none was made; the segments refreshed. The hook stands in for Update
    // Group's question in tests: 0 to update the group, 1 to create a new one, -1 to cancel.
    std::optional<QString> editGroup();
    std::optional<QString> updateGroup();
    std::optional<QString> createGroupFromSequence();
    int refreshGroupEdits();
    void setUpdateGroupHook(std::function<int()> hook) { updateGroupHook_ = std::move(hook); }
    // Previous In Group (-1) and Next In Group (1) on the active monitor (p.1131); false when nothing changed.
    bool stepInGroup(int step);
    // Choosing an angle in a split display: by place (a click at `fraction` of the Source monitor's picture, or a
    // numeric keypad key) or by number; false when nothing changed (8.1b; p.1131-1132).
    bool splitClicked(MonitorId monitor, QPointF fraction, Qt::MouseButton button);
    bool multiCamKey(int key);
    // Transcripts (8.2a; Editing Guide p.442-450). The engine: `provider` (tests) in place of the best usable
    // ext::TranscriptionProvider in the registry (whisper.cpp's when installed); none means no transcription.
    void setTranscriber(std::shared_ptr<ext::TranscriptionProvider> provider);
    std::shared_ptr<ext::TranscriptionProvider> transcriber() const;
    // "By default, Media Composer will create a transcription for all media in a project" (p.446): queues a Background
    // Queue job for each master clip with sound in the bins included in transcription that has no transcript yet and is
    // not being transcribed. Returns how many were queued (0 without a project or an engine).
    int queueTranscriptions();
    // Include/Exclude Bin in Transcription (p.447) on the Sidebar's selected bins, else the active bin; included bins'
    // clips are queued.
    void setBinsTranscribed(bool included);
    bool isBinTranscribed(const QString& binId) const;
    // "Transcribe" and Transcribe Remaining Clips (p.443): the project's master clips with these MobIDs, transcribed at
    // once (High priority on the Background Queue, ahead of background transcription). Returns how many were queued.
    int transcribeNow(const QStringList& mobs);
    // A change to a clip's transcript in the store (the Transcript Tool's edits, p.444-446); false when it changes
    // nothing or cannot be saved.
    // The edit holds the entry's lock (p.452); when another station holds it, the user is asked whether to take it over.
    bool editTranscript(const QString& mob, const std::function<bool(model::Transcript&)>& edit);
    // Peer-to-peer transcript sharing (p.451-452; 8.7b): the Transcript setting's Transcript Sharing (Off, Host Shared
    // Transcripts, Use Shared Transcripts) and Shared Transcripts Folder, applied by applySettings. Hosting copies this
    // workstation's transcripts into the folder first (model::hostTranscripts). The folder must be on shared storage
    // (model::isSharedLocation); otherwise sharing stays off and the Console says why.
    void applyTranscriptSharing();
    // Writes a transcript to the store; when another station holds its lock, asks whether to "abort on the other station
    // and obtain the lock" (p.452) and writes on yes.
    bool saveTranscript(const model::Transcript& transcript);
    // The question asked then (tests answer it): the clip's name and the lock's holder; true takes the lock over.
    using TranscriptTakeOver = std::function<bool(const QString& clipName, const model::TranscriptLock& heldBy)>;
    void setTranscriptTakeOverHook(TranscriptTakeOver hook) { transcriptTakeOver_ = std::move(hook); }
    TranscriptTool& transcriptTool() const { return *transcriptTool_; }
    // Manage Speaker Names (p.445, the Transcript setting's Manage Speakers): opens the window over the active one (window
    // modal, without blocking) and returns it; nullptr without a project. Its changes apply at once to the project's
    // speaker list (<project>/Speakers.json) and to the transcripts of the project's master clips.
    SpeakerNamesDialog* manageSpeakers();
    // PhraseFind AI indexing (the Find window's Settings > Stop/Start PhraseFind AI Indexing, p.441): while off, no
    // clip is queued for transcription in the background (Transcribe still works). On at start.
    bool transcriptionIndexing() const { return transcriptionIndexing_; }
    void setTranscriptionIndexing(bool on) { transcriptionIndexing_ = on; }
    // The PhraseFind AI index's state: the master clips with sound in the bins included in transcription, and how many
    // of them have a transcript.
    std::pair<int, int> phraseIndexStatus() const;
    // Transcripts out (8.2d; Editing Guide p.450-456). Each acts on the active bin's selection, as the bin's Transcript
    // menu does. Export Transcript: a text file per clip or sequence (a sequence's tracks from the Transcribe window);
    // returns how many were written. Share Transcripts: the selection's transcripts in one shared transcripts file.
    // Delete Transcript: the selected clips' transcripts; returns how many were deleted. Export SubCap: the selected
    // sequence's dialogue as a caption file. Create SubCap: captions on a new track of the Record monitor's sequence.
    int exportTranscripts();
    bool shareTranscripts();
    int deleteTranscripts(const QStringList& mobs = {});
    bool exportSubCap();
    bool createSubCap();
    // Manage Transcripts (2026.8 guide p.455-456; Script > Manage Transcripts, the Find window's Settings, the Transcript
    // setting's button): opened window modal without blocking, returned; nullptr without a project.
    ManageTranscriptsDialog* manageTranscripts();
    // AutoCut (8.2e; 2026.8 guide p.451-452). assignSpeakerAngle: the Source monitor's group gives the speaker named
    // `speaker` the angle `angle` (-1: none), kept in the group (GroupClip::speakerAngles). autoCut: the camera switches
    // on the selected group segments of the Record monitor's sequence from the groups' transcripts, one undo step;
    // returns how many switches were made.
    bool assignSpeakerAngle(const QString& speaker, int angle);
    int autoCut();
    // Scripts (8.3a; Editing Guide p.570-576, docs/spec/scriptsync.md). newScript: File > New > New Script, "Open the bin
    // to which you want to import the script ... Navigate to the script you want to import": a text file into a script
    // item in the active bin, opened; `path` in place of the file dialog (tests). openScript: a script item in the Script
    // window (saving the one shown, if changed). saveScript: File > Save Script, the window's script into its item.
    // saveScriptCopyAs: File > Save a Script Copy As, a new item named `name` (asked when empty) in the same bin, opened.
    // scriptTextLayout: Script > Text Layout's dialog (or `layout`).
    bool newScript(const QString& path = {});
    bool openScript(const QString& binId, const QString& itemId);
    bool saveScript();
    bool saveScriptCopyAs(const QString& name = {});
    bool scriptTextLayout(std::optional<model::ScriptLayout> layout = std::nullopt);
    ScriptWindow& scriptWindow() const { return *scriptWindow_; }
    // Slates and takes (8.3b, p.576-582). loadScriptTake: a take's clip into the Source monitor, at its representative
    // frame, or with Interpolate Position at the double-clicked line (`line` >= 0, `fraction` down it); false when the
    // clip is gone. setScriptSlateOption: the Script menu's Show Frames, Show All Takes, Hold Slates Onscreen, Interpolate
    // Position (toggled) and Color > colour (chosen), by command id. scriptIndicatorColors: Color's commands and colours.
    bool loadScriptTake(const model::ScriptTake& take, int line = -1, double fraction = 0.0);
    void setScriptSlateOption(const QString& commandId);
    // Script marks (8.3c, p.582-588). addScriptMark: Add Script Mark at the Source monitor's position. findScript: Find
    // Script. findScriptBins: Find Bin, the takes' clips selected in their opened bins; how many. spliceScriptRange: the
    // take's marked frames over script lines [first, last] spliced into the sequence.
    bool addScriptMark();
    bool findScript();
    int findScriptBins(const QList<model::ScriptTake>& takes);
    bool spliceScriptRange(const model::ScriptTake& take, int first, int last);
    // ScriptSync AI and Create Script from Clip (8.3d, p.569-570, p.584-585). scriptSyncAI: Script > ScriptSync AI's
    // dialog, then marks on the Script window's chosen takes from their clips' transcripts; clips without one are queued
    // for transcription. Returns the marks placed, -1 when nothing could run. createScriptFromClips: the bin's Transcript >
    // Create Script from Clip on the active bin's selected clips, a script item in that bin, opened; false without
    // transcripts (those clips are queued). setScriptSyncHook: tests answer the dialog (true: OK).
    int scriptSyncAI();
    bool createScriptFromClips();
    // Shared scripts (8.3e, p.589-590). closeScript: closes the Script window, saving a changed script, or for one someone
    // else holds asking Save Script As / Don't Save / Cancel; false when cancelled. scriptLockClicked: the script lock
    // icon. setCloseScriptHook: tests answer that question (0 Save Script As, 1 Don't Save, 2 Cancel).
    bool closeScript();
    void scriptLockClicked(bool alt);
    void setCloseScriptHook(std::function<int()> ask) { closeScriptHook_ = std::move(ask); }
    using ScriptSyncAsk = std::function<bool(ScriptSyncDialog&)>;
    void setScriptSyncHook(ScriptSyncAsk ask) { scriptSyncHook_ = std::move(ask); }
    static QList<std::pair<QString, QString>> scriptIndicatorColors();
    // Imports a shared transcripts file into the store; the count, or -1 with `error` (Language Hints that differ: "need
    // to match when exporting and importing", p.451).
    int importSharedTranscripts(const QString& path, QString* error = nullptr);
    // Test hooks: the file or folder chosen (`kind`: "save", "folder" or "open"; `suggested` a file name) and the
    // Transcribe window's answer, in place of the dialogs.
    using TranscriptPathAsk = std::function<QString(const QString& kind, const QString& suggested)>;
    using TranscribeTracksAsk = std::function<bool(TranscribeTracksDialog&)>;
    void setTranscriptHooks(TranscriptPathAsk path, TranscribeTracksAsk tracks) {
        transcriptPathHook_ = std::move(path);
        transcribeTracksHook_ = std::move(tracks);
    }
    void applySpeakerNameChange(const SpeakerNameChange& change);
    // The project's speaker names and those in its transcripts, and those in its transcripts only; the names Identify
    // Speaker offers ("a list of previously identified speakers in this project", p.444).
    QStringList projectSpeakerNames(QStringList* used = nullptr) const;
    bool chooseAngle(int angle);
    // Clip > MultiGroup (8.1b3; p.1125): the multigroup's id, nothing when none was made (the Group Clips hook
    // stands in for its dialog in tests).
    std::optional<QString> multiGroup();
    // The group the Source monitor shows at frame `at` (its group clip, or a multigroup's group segment there).
    std::optional<model::BinItem> sourceGroup(std::int64_t at, model::GroupSegmentAt* segment = nullptr) const;
    bool switchMultigroupAngle(int angle, bool step);
    // The Multi-angle View menu of split display place `place` (p.1134); null when no group is shown.
    QMenu* multiAngleMenu(int place);
    // Commit Multicam Edits (p.1135) on the bin's selected sequences: the duplicates' ids.
    QStringList commitMulticamEdits();
    // Read Audio Timecode (p.668-669): the selected master clips' and subclips' LTC on the chosen
    // track, into the chosen Auxiliary TC column (media::ltc::readClipTimecode); other items are
    // ignored with a message.
    int readAudioTimecode();
    // Clip > Refresh Sequence > Motion Adapters/Timewarps (Editing Guide p.478): the sequences selected in
    // the active bin when it has the focus, else the Record monitor's sequence, take their clips' current
    // Field Motion (model::refreshMotionAdapters). Returns how many adapters changed.
    int refreshMotionAdapters();
    // Refresh Sequence > Aspect Ratio and Reformatting Options (`creative` false) and All FrameFlex including creative
    // framing (8.5a, p.478). Returns how many segments changed.
    int refreshSpatialAdapters(bool creative);
    // Clip > Refresh Sequence > Color Adapters, or with `remove` Remove Color Adapters (p.478, p.544; 8.5c): video segments
    // take their clip's Color Adapter, or lose theirs. Returns how many segments changed.
    int refreshColorAdapters(bool remove);
    // The Color Management setting's Select LUT file (8.5d2, p.539, p.541): `path` (asked for when empty) installed where
    // Installed LUTs says. Returns the names it is listed under; empty when nothing was installed (a message says why).
    QStringList installLutFile(const QString& path = {});
    // The Format tab's Proxy (p.526-527, p.549-550; 8.5e2): the project's proxy mode (0, 4 or 16) saved with the project.
    // The monitors' pictures are made at most at the proxy raster (model::proxyRaster), renders and Transcode's Project
    // Dimensions at it; renders made at another mode go offline and come back with it (model::setRenderProxy). False
    // when no project is open, its format does not offer the mode or the project file cannot be written (a message
    // says why).
    bool setProxyMode(int mode);
    // Changing the Project Format (p.1287-1293; 8.7c1b): the Format tab's Project Type to another format of the same edit
    // rate. "The available resolutions and ... raster dimensions change to those of the new project. Any new sequences
    // you create use the format of the new project." The Proxy mode falls back to Off when the new raster has no such
    // mode; existing sequences keep theirs (Set Format changes them, p.1294). False for another edit rate.
    bool setProjectFormat(const ProjectFormat& format);
    // The Format tab's Mask Margins (p.550-551; 8.5e3): the project's, saved with it and shown by the monitors' Target
    // Mask. False without a project or when the project file cannot be written (a message says why).
    bool setMaskMargins(const model::MaskMargins& margins);
    // ACES projects' Output Color Space (p.559-560; 8.5e4b2), saved with the project. False outside ACES projects, for an
    // unknown space or when the project file cannot be written.
    bool setOutputColorSpace(const QString& space);
    // An exported frame: in ACES projects the working space converted to the Output Color Space; otherwise as it is.
    QImage outputFrame(const QImage& frame) const;
    void setLutFileHook(std::function<QString()> hook) { lutFileHook_ = std::move(hook); }
    // The Color Encoding tab's Color Management Settings button (p.542): the setting opened from the User tab; for tests
    // the hook gets the setting's entry instead of the dialog opening.
    void openColorManagementSetting();
    void setColorManagementHook(std::function<void(int entry)> hook) { colorManagementHook_ = std::move(hook); }
    // Source Settings (8.5b1, p.527-535) for the clips selected in the active bin, or the Source monitor's clip; the hook
    // answers for tests (true for OK). False when there is no clip or it was cancelled.
    bool sourceSettings();
    // Stereoscopic clips (8.6d1, docs/spec/stereoscopic.md): Create Stereoscopic Clip from the two master clips selected
    // in the active bin (the new clip's id, empty when refused), and the selected stereo clips' S3D Leading Eye (how many
    // changed).
    QString createStereoscopicClip();
    int setStereoLeadingEye(model::Eye eye);
    // Sets the project's Stereoscopic option to one of stereoscopicModes() (p.65-66; not in SD projects but Off).
    bool setProjectStereoscopic(const QString& mode);
    // Proxy media (8.7a, main_window_proxy.cpp, docs/spec/proxy-media.md). Bin > Create Proxies (p.508): the Create
    // Proxies window, then makeProxies() with its drive and processing option. makeProxies() makes the proxies of the
    // active bin's selected items' master clips on `drive` (a media drive's root), at once or as Background Queue jobs,
    // and returns how many clips it made or queued.
    int createProxies();
    int makeProxies(const QString& drive, bool background);
    // Distributed Processing (p.510): a proxy job per master clip sent to the shared storage module that lists
    // `workspaceId` (ext::SharedStorageProvider); the clips take their proxies when pollDistributedJobs() finds the jobs
    // done (every two seconds while some are out). Returns the jobs sent.
    int submitDistributedProxies(const QString& workspaceId);
    void pollDistributedJobs();
    int distributedJobsOut() const { return int(distributedJobs_.size()); }
    // The drives media can go to: the media drives, then the storage modules' workspaces.
    QList<MediaTarget> mediaTargets() const;
    // Copy Media (p.512-513; 8.7a2): copyMediaRequest() gathers the files of the active bin's selected items or, given
    // Sidebar ids, of every item in those bins and folders (closed bins are opened: "If you select bins that are closed,
    // they will be opened at the time the copy function begins"). copyMedia() opens the Copy Media window on them and
    // runs it; runCopyMedia() copies a request as the window left it and returns the files copied.
    media::CopyMediaRequest copyMediaRequest(const QStringList& nodeIds = {});
    int copyMedia(const QStringList& nodeIds = {});
    int runCopyMedia(const media::CopyMediaRequest& request);
    // The clip as the monitors play it: its proxy's link in place of its own when the Play button's proxy mode and the
    // Timeline's video quality call for it (model::playedProxyLink).
    model::BinItem playedClip(const model::BinItem& clip) const;
    // A clip's cell under a proxy heading (model::proxyCellText at the Timeline's video quality).
    QString proxyCell(const model::BinItem& clip, const QString& heading) const;
    // The project's S3D Settings (p.1216; 8.6d2), whose parallax the S3D Overlay's guides show.
    model::S3DSettings s3dSettings() const;
    // The Record monitor's picture as the Video Output tool's S3D View and S3D Overlay show it (p.1226-1227), for the
    // output modules (7.3c).
    QImage videoOutputPicture(QSize size);
    // The same for a Record monitor segment's clip (the Timeline's right-click Source Settings, 8.5b2), and for given
    // clips (bin, item id).
    bool sourceSettingsForSegment(const model::SegmentRef& ref);
    bool sourceSettingsFor(const QList<QPair<QString, QString>>& clips);
    void setSourceSettingsHook(std::function<bool(SourceSettingsDialog&)> hook) { sourceSettingsHook_ = std::move(hook); }
    // The Effect Palette's template bins (FX Guide p.37-38): every open bin's video effect templates
    // (model::effectTemplateOf), in the order the bins were opened.
    void refreshTemplateBins();
    // Change Sample Rate (p.731): new media for the selected audio clips, and the clips the selected
    // sequences' audio segments use, at the chosen rate (media::resampleClipAudio).
    int changeSampleRate();
    // Timeline > Mixdown > Audio > Multiple Mixes (p.732-734): the Multi-Mix Tool on the Record
    // monitor's sequence. Mix makes a master clip in the chosen bin for each output, and with Create
    // New Sequence a new sequence with a track for each mix, the video and data tracks copied unless
    // excluded. Returns the clips made. With `canMix` false (opened from the Settings window) the
    // tool only edits and saves settings (p.734). The hook stands in for exec() in tests.
    int multipleMixes(bool canMix = true);
    void setMultiMixHook(std::function<bool(MultiMixDialog&)> hook) { multiMixHook_ = std::move(hook); }
    // The Audio Tool's PH menu (p.157-159, p.920; docs/spec/tone.md). Play Calibration Tone plays the
    // calibration tone on every output channel until Reset Peak (p.157) or playback; false if no
    // output could be opened (`device` false: no device, the player's pull() takes the samples, for
    // tests). Create Tone Media makes a master clip of tone in the chosen bin, its media on the chosen
    // drive; nothing when cancelled or on failure (with a message). The hook stands in for exec().
    bool playCalibrationTone(bool device = true);
    void stopCalibrationTone();
    bool isPlayingTone() const { return playingTone_; }
    std::optional<model::BinItem> createToneMedia();
    void setToneMediaHook(std::function<bool(CreateToneMediaDialog&)> hook) { toneMediaHook_ = std::move(hook); }
    // Site_EQs_Bin (p.745): our native bin "Site_Effects/Site_EQs_Bin" in the site settings folder,
    // made empty if it is missing so that File > Open Bin finds it; empty without a site folder or
    // when it cannot be made. Its EQ templates (the open bin's, if it is open) follow the Audio EQ
    // tool's predefined ones in its Fast menu.
    QString siteEqBinPath() const;
    // The Audio Project setting's Sample Rate (p.1148): 44100, 48000 or 96000.
    int projectSampleRate() const;
    QList<model::BinItem> siteEqTemplates() const;
    // Gives the Audio Tool's meters what the output played since the last call (a timer calls it
    // while sound plays); channels switched to I (Input) show nothing, as nothing is recorded here.
    void updateMeters();
    media::AudioPlayer& audioPlayer() const { return *audio_; }
    // The saved Audio Multi-Mix settings (Project settings) by name, and saving one.
    QMap<QString, media::MultiMix> savedMultiMixes() const;
    void saveMultiMix(const QString& name, const media::MultiMix& mix);
    struct ReadTimecodeChoice {
        bool userBits = false;
        int track = 0;
        QString destination = QStringLiteral("Auxiliary TC1");
        bool fill = true;
    };
    struct SampleRateChoice {
        int rate = 48000;
        int filterSize = 32;
        bool deleteOriginal = false;
        QString drive; // a drive root; empty: the Media Creation setting's
    };
    struct ClipAudioHooks {
        std::function<std::optional<double>(double current)> gain;
        std::function<bool()> confirmCenterPan;
        std::function<std::optional<std::pair<QString, bool>>(const QStringList& tracks)> autoVo;
        std::function<std::optional<ReadTimecodeChoice>(int tracks)> readTimecode;
        std::function<std::optional<SampleRateChoice>()> sampleRate;
        // Messages the commands would show in a box (ignored items, failures).
        std::function<void(const QString&)> message;
    };
    void setClipAudioHooks(ClipAudioHooks hooks) { clipAudioHooks_ = std::move(hooks); }
    // Writes an Audio Punch-In take as managed media on its drive and adds the clip to its bin
    // (AudioPunchInTool::ClipMaker).
    std::optional<model::BinItem> punchInClip(const AudioPunchInTool::Take& take, QString* error);
    // An AudioSuite render of `segment` of the Record monitor's sequence, its media on `drive` (empty:
    // the Media Creation setting's audio drive); media::renderAudioSuite.
    std::optional<model::AudioSuiteEffect> renderAudioSuite(const model::Segment& segment, const QString& drive,
                                                            QString* error);
    // A new master clip made by the AudioSuite tool in master clip mode, with its template, in the
    // target bin (p.803-804); media::audioSuiteClip.
    bool makeAudioSuiteClip(const AudioSuiteTool::MasterClipRender& render, QString* error);
    void setCopyingMediaHook(std::function<std::optional<bool>()> hook) { copyingMediaHook_ = std::move(hook); }
    // How media is made, from the Media Creation setting's tab ("import",
    // "transcode") and the project.
    media::MediaCreation mediaCreation(const QString& tab) const;
    // Background Consolidate and Transcode (p.356-357).
    BackgroundQueue& backgroundQueue() const { return *backgroundQueue_; }
    // The background services (8.4a, p.375-377). backgroundServices: Tools > Background Services' dialog (or the hook's
    // answer), its options kept on OK; false when cancelled. The options as stored.
    bool backgroundServices();
    BackgroundServicesDialog::Options backgroundServiceOptions() const;
    void setBackgroundServicesHook(std::function<bool(BackgroundServicesDialog&)> hook) { backgroundServicesHook_ = std::move(hook); }
    // Dynamic Media Folders (8.4b, p.371-376; main_window_dmf.cpp). The folders and profiles, kept in
    // dynamicMediaFoldersFile() (empty without a user: then they are not saved). setDynamicMediaFolders keeps new ones
    // and looks in them at once; scanDynamicMediaFolders is what the timer runs: an enabled folder's entry is queued
    // on the Background Queue once it looks the same on two scans.
    const media::DynamicMediaFolders& dynamicMediaFolders() const { return dmf_; }
    void setDynamicMediaFolders(const media::DynamicMediaFolders& folders);
    QString dynamicMediaFoldersFile() const;
    void scanDynamicMediaFolders();
    // An entry being processed or processed: `ready` once its job has ended, with what it made (the linked clips and
    // Consolidate's and Transcode's ".new" ones) or its error. Kept until acquired into a bin (8.4c).
    struct DmfAsset {
        QString folder;
        QString entry;
        bool ready = false;
        QList<model::BinItem> clips;
        QString error;
        // Acquired while still being processed: the offline clips standing in for it, and their bin (8.4c).
        QString placeholderBin;
        QStringList placeholders;
    };
    const QList<DmfAsset>& dmfAssets() const { return dmfAssets_; }
    // Tools > Dynamic Media Folders (8.4c, p.372-376): the window; editDmfProfile, its Profile Editor for a folder's
    // profile (the hook answers for tests), true when saved; acquireDmfAssets, its Acquire column: the folder's
    // assets into the profile's bin, returning the bin's id (empty when nothing went in, or there is no project).
    void showDynamicMediaFolders();
    DynamicMediaFoldersWindow* dynamicMediaFoldersWindow() const { return dmfWindow_; }
    bool editDmfProfile(const QString& folder, const QString& profile);
    void setDmfProfileEditorHook(std::function<bool(DmfProfileEditor&)> hook) { dmfProfileEditorHook_ = std::move(hook); }
    QString acquireDmfAssets(const QString& folder);
    // The media database (p.358): read when a project opens, again with
    // File > Media > Load Media Database and Refresh Media Directories.
    // Managed clips in bins opened after are pointed at their files.
    void loadMediaDatabase();
    const media::MediaDatabase& mediaDatabase() const { return mediaDb_; }
    // Playback's sound (2.3): whether it plays, and its clock: the output
    // sample heard now, counted from where play started.
    bool isPlayingSound() const;
    // Digital audio scrub: whether a burst is playing, and its first output
    // sample.
    bool isScrubbing() const { return scrubbing_; }
    std::int64_t scrubStart() const { return scrubStart_; }
    // Composer > Full Screen Playback (p.391).
    void toggleFullScreen();

    // Null until Full Screen Playback is first opened.
    FullScreenView* fullScreenView() const;

    // The audio clock in output samples (media::kPlaybackRate per second); 0 without a player.
    std::int64_t soundClock() const;
    // Tools > Media Tool: the Media Tool Display dialog, then the tool
    // (p.346). Tests answer the dialog through the hook (true: OK).
    void showMediaTool();
    void setMediaToolDisplayHook(std::function<bool(MediaToolDisplayDialog&)> hook) {
        mediaToolDisplay_ = std::move(hook);
    }
    // Help > About's Configuration, Usage and Hardware (p.102-105).
    // A new dialog parented to this window; the caller shows it and decides when it goes.
    AboutInfoDialog* createAboutInfo();
    KeyboardPalette& keyboardPalette() const { return *keyboardPalette_; }
    KeyboardMap& keyboard() const { return *keyboard_; }
    // The workspace each of the W1-W12 buttons switches to (p.90-91).
    // Index i is button W(i+1); an empty name leaves it unassigned.
    QStringList workspaceButtons() const;
    // Timeline views (p.604-605): the names of the Timeline View User settings, sorted ("Media Composer
    // sorts the Timeline views alphabetically", p.605), and the view each TV button switches to
    // (index 0 is TV1). An unassigned button takes the view in its place in that order (p.605).
    QStringList timelineViews() const;
    QStringList timelineViewButtons() const;
    // Makes view `name` the active Timeline View setting, which applies it.
    void chooseTimelineView(const QString& name);
    // Keeps the Timeline's Fast menu settings and Clip Color choices as view `name`: a new view, or
    // with `replace` (or a name already used) in place of that view's; it becomes the one in use.
    void saveTimelineView(const QString& name, bool replace);

    // The classic Title Tool (FX Guide p.327, ROADMAP 6.8d1): Clip > New Title and Tools > Title Tool Application open
    // the New Title dialog (unless Persist was chosen this session), then the Title Tool over the Record monitor's
    // frame; with `ask` false the dialog is skipped. The window, made when first opened, or null.
    void openTitleTool(bool ask = true);
    TitleToolWindow* titleTool() const;
    // Saving a classic title (6.8d4a; FX Guide p.357-360): its fill and matte precomputes (unless fast saved) and its
    // Title Effect clip in the request's bin, or with `replaceId` that clip revised; loaded into the Source monitor.
    // Each aspect ratio the request names (9.5, p.989) is saved: the first under the request's name, the others as
    // "<name> <aspect>". The (first) clip's id, or empty with the reason.
    QString saveClassicTitle(const model::ClassicTitle& title, const TitleToolWindow::SaveRequest& request, const QString& replaceId,
                             QString* error = nullptr);
    // Revising a title in a bin (p.359): the Title Tool opens with Title Effect clip `itemId`; false for other items.
    bool reviseTitle(const QString& binId, const QString& itemId);
    // Save Title's Place in Timeline (In -> Out) (FX Guide p.359): the Title Effect clip's segment overwritten on
    // `track` of the record sequence between its marks, one undo step; false without both marks. `bin` is the clip's
    // bin, kept as the segment's source for Edit Title.
    bool placeTitle(const model::BinItem& clip, const QString& bin, const QString& track);
    // A title's fill and matte precomputes made in `doc` at the drive and resolution (empty: the Media Creation
    // setting's Titles tab); nothing, with the reason, on failure.
    std::optional<model::TitleMedia> makeTitleMedia(const model::ClassicTitle& title, const QString& name, BinDocument& doc,
                                                    const QString& drive, const QString& resolution, QString* error);
    std::optional<TitleMediaChoice> askTitleMedia(const QString& caption, const TitleMediaChoice& suggestion);
    // A warning that does not block (object name "titleMediaMessage").
    void notify(const QString& caption, const QString& text);
    // Revising a title in a sequence (FX Guide p.372-373): the Effect Editor's Edit Title button and the Timeline's
    // Edit Title open the record sequence's title segment at `segment` in the Title Tool; saving revises (Save Title)
    // or adds (Save Title As) the Title Effect clip and puts it in the segment's place, one undo step. False if the
    // segment is not a classic title.
    bool editTitleInSequence(const model::SegmentRef& segment);
    // Clip > Re-create Title Media (FX Guide p.374): new precomputes, at the drive and resolution asked (the Media
    // Creation setting's Titles tab first), for the titles on the selected record tracks between the marks (ours: the
    // whole sequence without marks), put in the sequence's bin; one undo step. The number re-created.
    int recreateTitleMedia();
    // Timeline > Create Unrendered Title Media (p.375): media for the fast-saved Title Effect clips selected in the active
    // bin, which lose "unrendered." from their names. The number made.
    int createUnrenderedTitleMedia();
    // For tests: what asks for the title media's drive and resolution (default: askTitleMediaChoice).
    void setTitleMediaDialog(std::function<std::optional<TitleMediaChoice>(const QString&, const TitleMediaChoice&)> ask) {
        titleMediaHook_ = std::move(ask);
    }
    // Title styles (FX Guide p.361-364) kept as "Title Style" User settings, one a style, named as it; saving one of an
    // existing name replaces it.
    QList<model::ClassicTitleStyle> titleStyles() const;
    void saveTitleStyle(const model::ClassicTitleStyle& style);
    // Font replacement for classic titles (Editing Guide p.993-994; 8.7d1): the mapping kept as the "Font Replacement"
    // Site setting ("Once you have defined a font replacement, Media Composer uses that definition across all projects
    // and all users of the system", p.994), and adding one original -> replacement to it.
    model::FontMap fontReplacements() const;
    void saveFontReplacement(const QString& original, const QString& replacement);
    // Tests: what answers the Title Tool's Font Replacement dialog and which font families are installed; set before the
    // Title Tool first opens.
    void setFontReplacementHooks(TitleToolWindow::FontStore::Ask ask, std::function<QStringList()> installed) {
        fontReplacementAsk_ = std::move(ask);
        installedFontsHook_ = std::move(installed);
    }

    // Titler+ templates (FX Guide p.319-320, ROADMAP 6.8c4a). Edit Template, or a template dragged to the Record
    // monitor, "opens the template in the Record monitor" (model::titleTemplateSequence, under the bin id
    // kTitleTemplateBin, which keeps no items). False when the item is not a Titler+ template.
    bool editTitleTemplate(const QString& binId, const QString& itemId);
    // The Dashboard's Save ("Update the current template with new changes") and Save As ("Create a duplicate with the
    // new changes"); with no template being edited both keep the shown Titler+ effect as a new template in the active
    // bin (ours). False when there is nothing to save.
    bool saveTitleTemplate(bool saveAs);
    // "Open another sequence in the Record monitor, which prompts you to save": asked with the template's name when
    // an edited template with changes leaves the Record monitor; true saves. A message box until set (tests set it).
    void setTemplateSavePrompt(std::function<bool(const QString&)> ask) { askTemplateSave_ = std::move(ask); }
    static constexpr const char* kTitleTemplateBin = "titleTemplate";

    // The layout of a new user (p.74): Bin Container top left, Composer to
    // its right, Timeline along the bottom.
    static QJsonObject defaultLayout();

    // Help > About: the product, its licence and the application picture.
    // The caller owns the box.
    QMessageBox* createAboutBox();
    static constexpr int kAboutPictureSize = 240; // logical pixels
    // Printing (9.1, main_window_print.cpp, docs/spec/printing.md; Editing Guide p.292, p.424, p.645). One printer for
    // the session, set up by File > Page Setup ("The Page Setup dialog box opens, reflecting the specific options for
    // your printer", p.292) and used by every print command.
    QPrinter& printer();
    void pageSetup();
    // File > Print Bin (p.292): the active bin as its view shows it. While the Markers window or the Console is the
    // window last used the command reads "Print" and prints that window instead (p.424): the Markers window's current
    // view, the Console's log. The Media Tool prints its table as the Text view does (p.345).
    bool printActive();
    // File > Print Frame (p.292): "the frame currently displayed in the active monitor", with the clip's name and
    // timecode under it. Beeps with nothing loaded.
    bool printFrame();
    // File > Print Timeline (p.645): "the current view of the Timeline", Heads and Heads Tails views too. Beeps with no
    // sequence.
    bool printTimeline();
    // What printActive() prints: the bin, the Markers window, the Console or the Media Tool.
    enum class PrintTarget { Bin, Markers, Console, MediaTool };
    PrintTarget printTarget() const { return printTarget_; }
    // Tests and scripted runs: replaces the Print dialog (`pageSetup` false) and the Page Setup dialog (true). The hook
    // may change the printer (its output file, page layout) and says whether to go on.
    using PrintDialogHook = std::function<bool(QPrinter& printer, bool pageSetup)>;
    void setPrintDialogHook(PrintDialogHook hook) { printDialogHook_ = std::move(hook); }
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

public:
    // The Safe Colors project setting's limits and Actions (FX Guide ch.23; 6.10e4).
    model::SafeColorLimits safeColorLimits() const;
    // The Dynamic Relink Project setting (Editing Guide p.1106-1110, p.1169-1171; 8.6a, main_window_relink.cpp).
    media::DynamicRelinkSettings dynamicRelinkSettings() const;
    // An item loading into a monitor or the Timeline (the Composer's load filter): with Dynamic Relink on, a clip, or
    // each clip a sequence uses, relinked to the media the setting chooses and stored in its bin (p.1107). Returns the
    // item as it loads.
    model::BinItem dynamicRelinkItem(const QString& binId, const model::BinItem& item);
    // Relinks the clips loaded in the monitors and those of the loaded sequences (p.1170); off, puts them back on their
    // affinity media. How many changed.
    int dynamicRelinkLoaded();
    // What the Timeline's MultiRez display says of a clip (8.6b, p.1111-1115): in the Show Mismatches display whether
    // its media match the settings in use (Offline when it has none), with `target` whether media match the Target
    // settings. Worked out once and kept until the media or the setting change.
    model::ClipFacts::Resolution clipResolution(const QString& binId, const QString& clipId, bool target);
    // Whether a rendered effect's media are at another resolution than the Media Creation setting's Render tab gives
    // (Show Mismatched Render Ranges, p.1115).
    bool renderAtOtherResolution(const model::EffectRender& render) const;
    // A clip's MultiRez bin cell under a resolution heading (p.1116; 8.6c), kept until the media change.
    QString multiRezCell(const model::BinItem& clip, const QString& heading);

private:
    // The Titler+ template open in the Record monitor (editTitleTemplate): where it is, its sequence as edited, and
    // whether that has changes not yet saved.
    struct TitleTemplateEdit {
        QString binId;
        QString templateId;
        model::BinItem sequence;
        bool dirty = false;
    };
    std::optional<TitleTemplateEdit> templateEdit_;
    QPointer<TitleToolWindow> titleTool_;
    bool titleToolPersist_ = false; // the New Title dialog's Persist (p.327)
    std::function<std::optional<TitleMediaChoice>(const QString&, const TitleMediaChoice&)> titleMediaHook_;
    std::function<bool(const QString&)> askTemplateSave_;
    // Saves an edited template (Save, or Save As a copy, which then is the one edited); false when it cannot.
    bool storeTitleTemplate(TitleTemplateEdit& edit, bool saveAs);
    // After the Record monitor changed: an edited template that left it is forgotten, saved first if asked.
    void checkTitleTemplate();
    // The Effect Editor in Effect mode with the Dashboard, as after applying Titler+ (p.303).
    void openTitleEditing();
    // Command handlers: application-wide (installHandlers), needing the panels (installPanelHandlers),
    // and needing a project, cleared again with false (installProjectHandlers).
    void installHandlers();
    void installPanelHandlers();
    void installProjectHandlers(bool projectOpen);
    // An imported master or audio clip whose MobID the media database knows
    // is linked to that media (importAaf, openBinFile). True when it was.
    bool linkByMobId(model::BinItem& item) const;
    // importOtio() and importFcpXml(): each file read with the OTIO or the FCP XML importer.
    QStringList importTimelines(const QString& binId, const QStringList& paths, bool fcp);

    // Whether the Bin Container (docked, floating, or the Project panel) is the active panel.
    // Splice-in (or Overwrite) of the active bin's one selected clip, with Enable edit from bin (p.624):
    // true when the bin had one to edit, whether or not the edit was made.
    bool editFromBin(bool splice);
    bool binsActive() const;
    void createPanels();

    // Workspaces: the menus and button kept in step with WorkspaceManager, and the commands that change
    // them.
    void syncWorkspaceUi();
    void placeWorkspaceButton();
    void newWorkspace();
    void deleteWorkspace();
    void restoreCurrentToDefault();
    void showAbout();

    // Pushes the active settings into every window; runs on each Settings::changed.
    // Writes the lists adoptLegacyBinLists() was given into the Bin setting; true when it wrote one.
    bool writeLegacyBinLists();
    void applySettings();
    void createTools();
    // Workspaces are User settings, one Workspace View each (p.86,
    // p.89-90): written when they change, read when a user's settings load.
    void writeWorkspaces();
    void readWorkspaces();
    void updateWorkspaceButtons();
    // Gives the Timeline, the Command Palette and the TV buttons the views as they are now.
    void updateTimelineViews();
    // File > Get Position Info (p.101): the Timeline at the position, in the
    // Console.
    void positionInfo();
    SettingsList* activeSettingsList() const;

    CommandRegistry& commands_;
    const ext::ProviderRegistry& providers_;
    std::optional<model::ProjectEntry> project_;
    // The Dynamic Relink setting as last applied: a change relinks what is loaded (applyDynamicRelinkSetting()).
    std::optional<media::DynamicRelinkSettings> appliedRelink_;
    void applyDynamicRelinkSetting();
    // The Timeline's MultiRez button wired to Dynamic Relink (main_window_relink.cpp).
    void connectMultiRez();
    // File > Page Setup and the print commands (main_window_print.cpp).
    void connectPrinting();
    QHash<QString, model::ClipFacts::Resolution> resolutionCache_;
    bool dynamicRelinkClip(const QString& binId, model::BinItem& clip, const media::DynamicRelinkSettings& settings,
                           bool crossRate = true);
    QHash<QString, QHash<QString, media::MediaCoverage>> coverageCache_; // multiRezCell()
    PanelManager* panels_ = nullptr;
    BinContainer* binContainer_ = nullptr;
    Composer* composer_ = nullptr;
    Timeline* timeline_ = nullptr;
    MarkersWindow* markersWindow_ = nullptr;
    ClipNotesWindow* clipNotesWindow_ = nullptr; // Tools > Timeline Clip Notes
    TimecodeWindow* timecodeWindow_ = nullptr;   // Tools > Timecode Window
    FindWindow* findWindow_ = nullptr;           // Edit > Find
    CommandPalette* commandPalette_ = nullptr;
    ToolPalette* toolPalette_ = nullptr;
    CalculatorWindow* calculator_ = nullptr;
    ColorInfoTool* colorInfo_ = nullptr; // Tools > Color Info Tool (FX Guide p.696-697)
    EffectPalette* effectPalette_ = nullptr;
    EffectEditor* effectEditor_ = nullptr;
    class CorrectionTool* correctionTool_ = nullptr;
    AudioMixer* audioMixer_ = nullptr;
    AudioEqTool* audioEq_ = nullptr;
    AudioPunchInTool* audioPunchIn_ = nullptr;
    DigitalCutTool* digitalCut_ = nullptr;
    VideoOutputTool* videoOutput_ = nullptr;
    bool digitalCutHalted_ = false;
    TitleToolWindow::FontStore::Ask fontReplacementAsk_;
    std::function<QStringList()> installedFontsHook_;
    bool quietDigitalCut_ = false;
    // The Digital Cut's receiver of the export's frames and sound (exportMovieFile's "sink" format).
    std::function<bool(const media::MovieSource&, const media::MovieExportOptions&, const std::function<bool(double)>&, QString*)> movieSink_;
    AudioTrackEffectTool* audioTrackEffect_ = nullptr;
    AudioSuiteTool* audioSuite_ = nullptr;
    AudioTool* audioTool_ = nullptr;
    MediaTool* mediaTool_ = nullptr;
    ListTool* listTool_ = nullptr;
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
    // Counts bursts, so that only the latest one's timer lets the device go.
    int scrubToken_ = 0;
    bool scrubbing_ = false;
    bool playingTone_ = false;
    bool metersLive_ = false; // the meters show something; updateMeters() clears them once sound stops
    std::function<bool(CreateToneMediaDialog&)> toneMediaHook_;

    // Plays a scrub burst when the position moved while stopped (runs on Composer::changed).
    void scrubIfMoved();

    // What a monitor shows at a position: a linked clip and its frame.
    // The video tracks of `seq` the monitor shows, bottom to top (Track Monitor buttons, p.627).
    QStringList shownVideoTracks(MonitorId monitor, const model::Sequence& seq) const;
    // Where a sequence loaded in a monitor gets its pictures from: its clips' frames, Timecode
    // Burn-In's sequence, marks and source clips, and its rendered effects' precomputes.
    void fillComposition(media::Composition& c, const model::LoadedClip& loaded);
    // Renders `targets` of the Record monitor's sequence `seq` (loaded as `loaded`) with `creation`'s
    // drive and resolution: the progress, partial render and Keep or Discard, one undo step
    // (renderEffects() and the ExpertRender carry on here). With `background`, renderInBackground().
    void renderTargetsNow(const model::LoadedClip& loaded, const model::Sequence& seq, const QList<model::RenderTarget>& targets,
                          media::MediaCreation creation, bool rerender, bool background = false);
    // Background Process (FX Guide p.59): the same renders as a Background Queue job, into the drive's
    // "MXF/__bg.1" folder; applyBackgroundRenders() links them when the job is done.
    void renderInBackground(const model::LoadedClip& loaded, const model::Sequence& seq, const QList<model::RenderTarget>& targets,
                            media::MediaCreation creation, bool rerender);
    // Links background renders to the effects they were made from in the sequence `sequenceId` of bin
    // `binId` (through the Timeline when it is in the Record monitor, else in the bin), matching each by
    // its key, and keeps their precomputes in the bin.
    void applyBackgroundRenders(const QString& binId, const QString& sequenceId,
                                const QList<std::pair<model::RenderTarget, model::EffectRender>>& made);
    // After a render: Save after Render, and the completion sound when it was not interrupted.
    void renderFinished(bool completed);
    // The Render tab's Effects Processing for `creation`.
    media::EffectPrecision renderPrecision(const media::MediaCreation& creation) const;
    // The Render setting's Image Interpolation into `c`.
    void fillCompositionQuality(media::Composition& c) const;
    std::optional<std::pair<model::BinItem, std::int64_t>> frameAt(MonitorId monitor, std::int64_t position) const;
    // An unrendered motion effect clip's frame as its source clip and that clip's frame
    // (model::motionEffectOf); nothing for other clips.
    std::optional<std::pair<model::BinItem, std::int64_t>> followMotion(const model::BinItem& clip, std::int64_t frame) const;
    // The Source monitor's Quad Split or Nine Split picture of a group clip at group frame `at` (p.1127-1128).
    // `selected` is the angle outlined (the group's own when unset); `offsets` stand in for the group's (a segment's).
    QImage groupSplitImage(const model::BinItem& group, std::int64_t at, QSize size, bool* offline, std::optional<int> selected = {},
                           const QList<std::int64_t>& offsets = {}) const;
    // MultiCamera mode (8.1b; p.1129-1132): the group segment it follows, the Source monitor's picture of it.
    std::optional<model::GroupSegmentAt> multiCamSegment() const;

    // Transcription (8.2a; docs/spec/transcripts.md, main_window_transcripts.cpp). Which bins are transcribed, kept in
    // the project folder; the clips being transcribed by their MobIDs; the time of the last key or click, for "Pause
    // background transcription while editing" (p.447).
    std::optional<model::TranscriptionBins> transcriptionBins_;
    std::shared_ptr<ext::TranscriptionProvider> transcriber_;
    QSet<QString> transcribing_;
    QTimer* transcriptionTimer_ = nullptr;
    QObject* activityWatcher_ = nullptr;
    // Sets up (project open) or clears (closed) transcription: the Sidebar's status, the timer.
    void setUpTranscription(bool projectOpen);
    // The timer's tick: queues transcriptions once the user has been idle ten seconds (or always, with the pause off).
    void transcriptionTick();
    // The Transcript Tool (8.2b; Tools > Transcript Tool): its panel and what it follows.
    TranscriptTool* transcriptTool_ = nullptr;
    QString transcriptKey_;           // what it shows now, so that position changes do not rebuild it
    QStringList transcriptTracks_;    // the track button's choice; empty: all audio tracks
    bool transcriptSourceSide_ = true;
    QString lastSourceLoaded_;
    QString lastRecordLoaded_;
    int transcriptGeneration_ = 0;    // bumped when a transcript is made or changed
    // The transcript sharing last applied (applyTranscriptSharing), and the take-over question.
    model::TranscriptSharing appliedSharing_ = model::TranscriptSharing::Off;
    QString appliedSharedFolder_;
    TranscriptTakeOver transcriptTakeOver_;
    bool takeOverTranscriptLock(const model::TranscriptStore& store, const QString& mob, const QString& clipName);
    void setUpTranscriptTool();
    bool transcriptionIndexing_ = true;
    TranscriptPathAsk transcriptPathHook_;
    ScriptWindow* scriptWindow_ = nullptr;
    const model::BinItem* scriptTakeItem(const model::ScriptTake& take) const;
    void applyScriptSettings();
    std::int64_t scriptPreRollFrames() const;
    void followScriptRecording();
    QList<model::ScriptTake> scriptRecordTakes_; // the Record button's takes while screening
    model::ScriptSyncOptions scriptSyncOptions_; // the ScriptSync AI dialog's last choices (ours: kept for the session)
    ScriptSyncAsk scriptSyncHook_;
    std::function<int()> closeScriptHook_;
    QMetaObject::Connection scriptLockConnection_; // the open script's bin's lockChanged
    void updateScriptLock();
    void refreshScriptMenu();
    void setUpScriptWindow();
    TranscribeTracksAsk transcribeTracksHook_;
    // The file or folder for transcripts out, through the hook or a file dialog; empty when cancelled.
    QString transcriptPath(const QString& kind, const QString& suggested, const QString& filter);
    // What an item shows, its sequence's or multigroup's on `tracks`.
    model::ShownTranscript shownTranscriptOf(const model::BinItem& item, const QStringList& tracks) const;
    // The Transcribe window for a sequence's audio tracks; false when cancelled.
    bool chooseTranscribeTracks(TranscribeTracksDialog& dialog);
    // Gives the Find window the transcripts and the index (8.2c).
    void connectPhraseFind(FindWindow& window);
    // Queues one clip's transcription; `now` for the user's Transcribe (High priority). False when it is already queued
    // or there is no engine.
    bool queueTranscription(const QString& binId, const model::BinItem& item, bool now);
    // Shows what the chosen side's monitor holds, its position and marks (Composer::changed).
    void refreshTranscriptTool();
    // The Group menu (p.1133): its contents, and a choice of an angle for one of the group's tracks.
    std::optional<Composer::GroupMenuInfo> groupMenuInfo() const;
    bool chooseGroupTrackAngle(const QString& track, int angle);
    QImage multiCamPicture(QSize size, bool* offline) const;
    void swapBank();
    // An unrendered motion effect clip's frame `frame` made from its interlaced source clip's fields with the
    // clip's Render 2-Field Motion Effect Using choice (FX Guide p.563, 6.5d3a); nothing for other clips and
    // for progressive sources, which followMotion() shows.
    std::optional<QImage> motionFieldFrame(const model::BinItem& clip, std::int64_t frame, QSize size) const;
    // A proxy job out on Distributed Processing (submitDistributedProxies).
    struct DistributedJob {
        std::shared_ptr<ext::SharedStorageProvider> storage;
        std::string id;
        QString binId;
        QString clipId;
        QString clipName;
    };
    QList<DistributedJob> distributedJobs_;
    QTimer* distributedTimer_ = nullptr;
    // The master clips the items give for Create Proxies, with their bins (main_window_proxy.cpp).
    QList<std::pair<QString, model::BinItem>> proxyClips(const QList<std::pair<QString, model::BinItem>>& items) const;
    // The monitor's picture from `plain` as the S3D View and Overlay show it (8.6d2; main_window.cpp says how).
    QImage stereoPicture(MonitorId monitor, const std::function<QImage()>& plain) const;
    // While set, adaptedFrame() plays this eye of stereo clips (Leading: each clip's leading eye): how stereoPicture()
    // makes each eye's picture.
    mutable std::optional<model::S3DView::MonoEye> eyeOverride_;
    // The S3D View and Overlay values the next picture takes instead of the Composer setting's (Full Screen Playback, the
    // Video Output tool).
    mutable std::optional<std::pair<QString, QString>> s3dOverride_;
    // adaptedFrame() for one eye: the clip's frame with its colour and spatial adapters.
    QImage monoFrame(const model::BinItem& clip, std::int64_t frame, QSize want, const std::optional<model::SpatialAdapter>& spatial,
                     const std::optional<model::ColorEncoding>& color) const;
    QImage adaptedFrame(const model::BinItem& clip, std::int64_t frame, QSize want, const std::optional<model::SpatialAdapter>& spatial,
                        const std::optional<model::ColorEncoding>& color = std::nullopt) const;
    // The sequences selected in the active bin (when it has the focus), else the Record monitor's, through `refresh`, each
    // changed one stored (the Record monitor's as an undo step). Returns the changes `refresh` counted.
    int refreshSequences(const std::function<int(model::Sequence&)>& refresh);
    // While the Record monitor's Effect Preview is composed: the segment whose FrameFlex effect the Effect Editor shows,
    // drawn whole under its box (EffectEditor::framingSegment; 8.5b3).
    std::optional<model::Segment> framingPreview_;
    std::function<QString()> lutFileHook_;
    // Writes the project file with `changed` and keeps it; false, with a message naming `what`, when it cannot be written.
    bool saveProjectSettings(const model::ProjectSettings& changed, const QString& what);
    std::function<void(int)> colorManagementHook_;

    // Starts the monitor's sound at `speed` for playback, or none above three times normal.
    void startSound(MonitorId monitor, int speed);
    std::function<void(LinkedMediaDialog&)> linkedMediaHook_;
    std::function<bool(ConsolidateDialog&)> consolidateHook_;
    std::function<bool(AudioMixdownDialog&)> mixdownHook_;
    std::function<bool(VideoMixdownDialog&)> videoMixdownHook_;
    std::function<bool(RenderEffectDialog&)> renderHook_;
    std::function<bool()> renderInterruptHook_;
    std::function<void(const QString&)> renderSoundHook_;
    // Precomputes rendered since their bin was last saved, by id, with that bin's id: "Media Composer
    // automatically saves the last-rendered effect and deletes the earlier, unreferenced versions. This
    // autodeletion applies only to effects rendered since the last save" (FX Guide p.155).
    QHash<QString, QString> unsavedPrecomputes_;
    // Puts the precomputes of `renders` in the bin `binId` as reference clips (FX Guide p.155: shown
    // with Set Bin Display's Effects and Show reference clips), and deletes those of `replaced`
    // that were rendered since that bin was last saved.
    void keepPrecomputes(const QString& binId, const QList<model::EffectRender>& renders,
                         const QList<model::EffectRender>& replaced);
    // The Render setting's Render Completion Sound (p.1215-1216).
    void playRenderCompletionSound();
    std::function<void(ExpertRenderDialog&)> expertHook_;
    std::function<bool(FreezeFrameDialog&)> freezeHook_;
    std::int64_t lastFreezeDuration_ = 0; // the last Freeze Frame duration, offered next time
    std::function<void(MotionEffectDialog&)> motionHook_;
    MotionEffectEditor* motionEditor_ = nullptr;
    // The Render setting's Timewarps Render Using (FX Guide p.173): the Timewarps among `targets` take
    // its Type before they render, as one undo step of their own ("Changes to motion effect types are
    // permanent", p.174). True when any changed.
    bool applyTimewarpRenderTypes(const QList<model::RenderTarget>& targets);
    // The bin a new clip made from `binId`'s clip goes in: that bin when it is open and writable, else
    // the open bin the user picks (the active one when it is the only one). Null when none is open.
    BinDocument* binForNewClip(const QString& binId);
    // Puts a new motion effect clip in its bin, selects it there and loads it into the Source monitor.
    void placeMotionClip(const QString& sourceBin, const model::BinItem& item);
    std::function<bool(ClearRendersDialog&)> clearRendersHook_;
    QString shownLanguage_; // the Interface setting's language last read (10.4)
    QString lastRenderDrive_; // the drive the last Render Effect dialog chose (Alt-click renders there)
    bool lastRenderBackground_ = false; // and whether it chose Background Process
    bool renderOnTheFly_ = true; // Composer > Render On-the-Fly
    bool highlightRenderAreas_ = true; // the Timeline setting Highlight Suggested Render Areas After Playback
    std::function<int(const QStringList&)> binChoiceHook_;
    std::function<void(const QString&)> revealHook_;
    std::function<bool(UnmountDialog&)> unmountHook_;
    std::function<bool(SetFontDialog&)> fontHook_;
    ShortcutsWindow* shortcutsWindow_ = nullptr;
    FindCommandWindow* findCommandWindow_ = nullptr; // Help > Find Command (10.3c)
    BinLayoutAsk binLayoutHook_;
    QString currentBinLayout_;
    QList<QAction*> binLayoutActions_;
    std::function<bool(ModifyDialog&)> modifyHook_;
    std::function<std::optional<std::pair<model::GroupSync, int>>()> groupClipsHook_;
    std::function<std::optional<model::SyncClipsOptions>()> syncClipsHook_;
    model::SyncClipsOptions lastSyncOptions_; // the Sync Selection dialog opens as it was last left
    std::function<int()> updateGroupHook_;
    ClipAudioHooks clipAudioHooks_;
    std::function<bool(MultiMixDialog&)> multiMixHook_;
    std::function<bool(ExportAsDialog&)> exportHook_;
    std::function<bool(SettingsDialog&)> exportOptionsHook_;
    std::function<int(const QList<model::BinItem>&)> excludeSequenceHook_;
    std::function<bool(ExportDeviceDialog&)> exportDeviceHook_;
    std::function<bool(ExportVolumeDialog&)> exportVolumeHook_;
    std::function<QString(bool)> subcapFileHook_;
    std::function<bool(ExportCaptionsDialog&)> exportCaptionsHook_;
    std::function<bool(MasterCaptionListDialog&)> masterCaptionHook_;
    std::function<bool(SubcapPropertiesDialog&)> subcapPropertiesHook_;
    std::function<bool(AncDataImportDialog&)> ancDataImportHook_;
    // The last captions decoded for each monitor, by their data (Show Captions asks for every frame painted).
    mutable QByteArray captionData_[2];
    mutable QList<model::ShownCaption> captionShown_[2];
    // Send To's template values while it exports (7.1c2); empty otherwise.
    QJsonObject exportOverride_;
    std::function<bool(SendToDialog&)> sendToHook_;
    std::function<bool(ProToolsOptionsDialog&)> proToolsHook_;
    std::function<QString(const QString&)> sendToSaveHook_;
    std::function<bool(SettingsDialog&)> sendToOptionsHook_;
    std::function<void(const QString&, const QStringList&)> launchHook_;
    QList<QAction*> sendToActions_;
    std::function<bool(ImportEdlDialog&)> importEdlHook_;
    std::function<bool(ExportBinDialog&)> exportBinHook_;
    std::function<bool(ManagedRelinkDialog&)> managedRelinkHook_;
    std::function<std::optional<BinDeleteChoice>(int, int)> binDeleteHook_;
    std::function<std::optional<bool>()> copyingMediaHook_;
    media::MediaDatabase mediaDb_;
    BackgroundQueue* backgroundQueue_ = nullptr;
    std::function<bool(BackgroundServicesDialog&)> backgroundServicesHook_;
    bool dmfAssetsWaiting_ = false; // Dynamic Media Folder assets to acquire: the indicator's green dot (8.4b-c)
    media::DynamicMediaFolders dmf_;
    QList<DmfAsset> dmfAssets_;
    QHash<QString, QString> dmfSeen_; // entry path -> how it looked on the last scan
    QTimer* dmfTimer_ = nullptr;
    DynamicMediaFoldersWindow* dmfWindow_ = nullptr;
    std::function<bool(SourceSettingsDialog&)> sourceSettingsHook_;
    std::function<bool(DmfProfileEditor&)> dmfProfileEditorHook_;
    void refreshDmfWindow();
    void setUpDynamicMediaFolders();
    void saveDynamicMediaFolders();
    void queueDmfEntry(const QString& folder, const QString& entry, const media::DmfProfile& profile);
    void refreshDmfAssets();
    void setUpBackgroundServices();
    void updateBackgroundIndicator();
    void stopBackgroundServicesOnExit();
    // Puts a finished job's clips and sequences in its bin.
    void applyConsolidate(const QString& binId, const media::ConsolidateResult& result);
    // Puts a changed clip back in its bin and the monitors.
    void storeClip(const QString& binId, const model::BinItem& item);

    // Gives the Source Browser the project's bins.
    void refreshSourceBins();
    std::function<bool(MediaToolDisplayDialog&)> mediaToolDisplay_;
    // The Audio Mixer mode last taken from the settings, so the mixer is switched only when it changes.
    QString appliedMixerMode_;
    QJsonValue legacyFavorites_;
    QJsonValue legacyBinViews_;
    // When the project opened (Help > About, Usage).
    QDateTime projectOpened_;

    // Every item in the project's bins, open ones as they are now; *binCount gets the number of bins.
    QList<model::BinItem> projectItems(int* binCount = nullptr) const;
    ConsoleWindow* console_ = nullptr;
    KeyboardPalette* keyboardPalette_ = nullptr;
    Settings* settings_ = nullptr;
    SettingsWindow* settingsWindow_ = nullptr;
    SiteSettingsWindow* siteSettingsWindow_ = nullptr;
    ToolTipFilter* toolTips_ = nullptr;
    // Set while workspaces are written to or read from the settings, so the two do not feed back.
    bool syncingWorkspaces_ = false;
    // The workspace whose Link to Named settings were last applied.
    QString linkedWorkspace_;
    // The Timeline view last applied, so applySettings() applies a view only when another is chosen.
    QString appliedTimelineView_;
    // Printing (main_window_print.cpp).
    std::unique_ptr<QPrinter> printer_;
    PrintDialogHook printDialogHook_;
    std::function<BatchImportChoice()> batchImportChoiceHook_;
    std::function<PsdLayersChoice(const QStringList&)> psdChoiceHook_;
    std::function<std::optional<QList<int>>(const QString&, const QStringList&)> psdSelectHook_;
    std::function<bool(SequenceReportDialog&)> sequenceReportHook_;
    std::function<QString(const QString&)> reportFileHook_;
    std::function<void(const QString&)> openReportHook_;
    std::function<bool(BatchImportDialog&)> batchImportHook_;
    PrintTarget printTarget_ = PrintTarget::Bin;
    // Follows the window last used (printTarget_) and names File > Print Bin after it.
    void followPrintTarget(QWidget* focused);
    bool printDocument(const PrintDocument& document);
    std::unique_ptr<KeyboardMap> keyboard_;
    WorkspaceManager* workspaces_ = nullptr;
    WorkspaceBar* bar_ = nullptr;
    QToolButton* workspaceButton_ = nullptr;
    // The workspace entries put in Windows > Workspaces, rebuilt by syncWorkspaceUi().
    QList<QAction*> workspaceActions_;
};

} // namespace lumenis::ui
