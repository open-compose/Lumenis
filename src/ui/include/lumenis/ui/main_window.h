#pragma once

#include "lumenis/media/link.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/audio_punch_in.h"
#include "lumenis/ui/audio_suite.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
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
struct Composition;
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
class BackgroundQueue;
class BackgroundQueueWindow;
class CalculatorWindow;
class ConsolidateDialog;
class UnmountDialog;
class SetFontDialog;
class ShortcutsWindow;
class AudioMixdownDialog;
class ClearRendersDialog;
class ExpertRenderDialog;
class RenderEffectDialog;
class MultiMixDialog;
class ExportAsDialog;
class ExportBinDialog;
class ImportEdlDialog;
class ListTool;
class AboutInfoDialog;
class AudioEqTool;
class ModifyDialog;
class AudioPunchInTool;
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
    // AAF files imported into a bin (p.547): their clips and sequences,
    // master clips with media on the drives online by MobID.
    // Returns the new items' ids; failures and warnings go to the Console.
    QStringList importAaf(const QString& binId, const QStringList& paths);
    // Shot logs (Avid Log Exchange, p.131-132) imported into a bin: a master clip for each logged
    // event. Returns the new items' ids; warnings go to the Console.
    QStringList importShotLogs(const QString& binId, const QStringList& paths);
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
    void setExportHook(std::function<bool(ExportAsDialog&)> hook) { exportHook_ = std::move(hook); }
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
    // Read Audio Timecode (p.668-669): the selected master clips' and subclips' LTC on the chosen
    // track, into the chosen Auxiliary TC column (media::ltc::readClipTimecode); other items are
    // ignored with a message.
    int readAudioTimecode();
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
    EffectPalette* effectPalette_ = nullptr;
    EffectEditor* effectEditor_ = nullptr;
    AudioMixer* audioMixer_ = nullptr;
    AudioEqTool* audioEq_ = nullptr;
    AudioPunchInTool* audioPunchIn_ = nullptr;
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
    // (renderEffects() and the ExpertRender carry on here).
    void renderTargetsNow(const model::LoadedClip& loaded, const model::Sequence& seq, const QList<model::RenderTarget>& targets,
                          media::MediaCreation creation, bool rerender);
    std::optional<std::pair<model::BinItem, std::int64_t>> frameAt(MonitorId monitor, std::int64_t position) const;

    // Starts the monitor's sound at `speed` for playback, or none above three times normal.
    void startSound(MonitorId monitor, int speed);
    std::function<void(LinkedMediaDialog&)> linkedMediaHook_;
    std::function<bool(ConsolidateDialog&)> consolidateHook_;
    std::function<bool(AudioMixdownDialog&)> mixdownHook_;
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
    std::function<bool(ClearRendersDialog&)> clearRendersHook_;
    QString lastRenderDrive_; // the drive the last Render Effect dialog chose (Alt-click renders there)
    bool renderOnTheFly_ = true; // Composer > Render On-the-Fly
    bool highlightRenderAreas_ = true; // the Timeline setting Highlight Suggested Render Areas After Playback
    std::function<int(const QStringList&)> binChoiceHook_;
    std::function<void(const QString&)> revealHook_;
    std::function<bool(UnmountDialog&)> unmountHook_;
    std::function<bool(SetFontDialog&)> fontHook_;
    ShortcutsWindow* shortcutsWindow_ = nullptr;
    BinLayoutAsk binLayoutHook_;
    QString currentBinLayout_;
    QList<QAction*> binLayoutActions_;
    std::function<bool(ModifyDialog&)> modifyHook_;
    ClipAudioHooks clipAudioHooks_;
    std::function<bool(MultiMixDialog&)> multiMixHook_;
    std::function<bool(ExportAsDialog&)> exportHook_;
    std::function<bool(ImportEdlDialog&)> importEdlHook_;
    std::function<bool(ExportBinDialog&)> exportBinHook_;
    std::function<bool(ManagedRelinkDialog&)> managedRelinkHook_;
    std::function<std::optional<BinDeleteChoice>(int, int)> binDeleteHook_;
    std::function<std::optional<bool>()> copyingMediaHook_;
    media::MediaDatabase mediaDb_;
    BackgroundQueue* backgroundQueue_ = nullptr;
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
    std::unique_ptr<KeyboardMap> keyboard_;
    WorkspaceManager* workspaces_ = nullptr;
    WorkspaceBar* bar_ = nullptr;
    QToolButton* workspaceButton_ = nullptr;
    // The workspace entries put in Windows > Workspaces, rebuilt by syncWorkspaceUi().
    QList<QAction*> workspaceActions_;
};

} // namespace lumenis::ui
