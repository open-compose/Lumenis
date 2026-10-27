#pragma once

#include "lumenis/media/surround.h"
#include "lumenis/media/tone.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/surround_pan.h"
#include "lumenis/ui/surround_panner.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>
#include <optional>

class QAction;
class QButtonGroup;
class QComboBox;
class QDial;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// The Audio Mixer fader: the scale +12 ... -∞ of the picture on p.693,
// marks equally spaced, and positions in between interpolated.
namespace fader {
// The slider's range is 0 (bottom, -∞) to kRange (top, +12 dB): 100 steps between marks.
inline constexpr int kRange = 1100;
// The slider position for a gain in dB; at most +12 dB is shown.
int position(double db);
// The gain at a slider position, rounded to 0.1 dB; model::kGainOff at the bottom.
double gain(int position);
QString text(double db);              // "-6.0", "+12.0", "-∞"
// A typed level: a number in dB, clamped by model::clampGain (so up to +36 dB), or "-∞" / "-inf".
// Nothing if it cannot be read.
std::optional<double> parse(const QString& text);
QString panText(int pan);             // "L57", "MID", "R57"
// A typed pan: "MID", "L<n>", "R<n>" or a signed number, in either case, clamped to
// ±model::kPanMax. Nothing if it cannot be read.
std::optional<int> parsePan(const QString& text);
} // namespace fader

// One track's strip of the Audio Mixer (picture and table on p.693-694).
// It only shows values and reports what the user sets; AudioMixer decides what a change does.
// The Audio Mixer's Set Display Options (p.711; the Fast menu's window options, p.694): what each
// strip shows. Ours: a narrow mixer has smaller pan knobs and value boxes.
struct MixerDisplay {
    bool narrow = false;
    bool faders = true;
    bool legends = true; // the fader scales
    bool effects = true; // the Audio Track Effect insert buttons
    bool soloMute = true;
    bool operator==(const MixerDisplay&) const = default;
};

class MixerStrip : public QWidget {
    Q_OBJECT

public:
    // `track`: "A1" ... or the Master's name. The Master strip has no gang, pan or solo controls,
    // and its track button is not checkable.
    MixerStrip(const QString& track, bool master, QWidget* parent = nullptr);

    const QString& track() const { return track_; }
    QSlider* fader() const { return fader_; }
    QLineEdit* levelBox() const { return level_; }
    QDial* panKnob() const { return pan_; }
    QLineEdit* panBox() const { return panBox_; }
    QToolButton* soloButton() const { return solo_; }
    QToolButton* muteButton() const { return mute_; }
    QToolButton* gangButton() const { return gang_; }
    QToolButton* trackButton() const { return trackButton_; }
    // The Audio Track Effects button (p.790): "+" without inserts, else the letters of the filled
    // ones ("ac"). Its menu is the mixer's to fill.
    QToolButton* insertButton() const { return insert_; }
    void setInserts(const QString& letters);
    void setDisplay(const MixerDisplay& display);
    const MixerDisplay& display() const { return display_; }

    // The values shown: gain in dB (model::kGainOff for -∞), pan from -kPanMax (left) to
    // kPanMax (right).
    double gain() const { return gain_; }
    int pan() const { return panValue_; }
    // Shows values without signalling.
    void show(double gain, int pan);
    // The Volume Level display's text in place of a value ("Clip", "Auto").
    void showText(const QString& text);
    // Surround panning (p.704-706): in a 5.1 or 7.1 sequence a mono or stereo track's strip shows a Pan
    // grid for each channel and the Advanced Panner button in place of the pan knob; an LFE-only track
    // shows "LFE" and no panner (p.710). `channels` 0 shows the pan knob again; -1 shows no pan at
    // all (a 5.1 or 7.1 track in a surround sequence, whose channels have their own speakers).
    void showSurround(model::AudioFormat sequenceFormat, int channels, bool lfeOnly);
    // Shows the channels' pans without signalling.
    void showSurroundPans(const QList<model::SurroundPan>& pans);
    PanGrid* panGrid(int channel) const { return grids_.value(channel); }
    QToolButton* advancedPannerButton() const { return advanced_; }
    // Live mode colours the fader area red (p.725), Auto mode blue (p.717); levels over +12 dB
    // are yellow (p.670).
    void setLive(bool on);
    void setAuto(bool on);
    // The gang letter "A"-"J" shown on the gang button; empty for none.
    void setGang(const QString& letter);
    const QString& gangName() const { return gangName_; }

signals:
    // The fader or Volume Level display was set; `done` once the drag ends.
    // While dragging it fires with `done` false for each move, then once more with true.
    void gainSet(double db, bool done);
    // The pan knob or pan box was set; `done` as for gainSet().
    void panSet(int pan, bool done);
    // A Pan grid's cursor moved (`done` once the drag ends).
    void surroundPanSet(int channel, const model::SurroundPan& pan, bool done);
    void advancedPannerRequested();
    // The right-click menu's "This track is LFE only" (p.710).
    void lfeOnlyRequested(bool on);

protected:
    // Alt-click on the fader or the pan box resets it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Colours the fader and level display for extended levels and Live mode.
    void syncColours();

    QString track_;
    QSlider* fader_ = nullptr;
    QLineEdit* level_ = nullptr;
    QDial* pan_ = nullptr;
    QLineEdit* panBox_ = nullptr;
    QToolButton* solo_ = nullptr;
    QToolButton* mute_ = nullptr;
    QToolButton* gang_ = nullptr;
    QToolButton* trackButton_ = nullptr;
    QToolButton* insert_ = nullptr;
    QWidget* scale_ = nullptr;
    bool master_ = false;
    MixerDisplay display_;
    QWidget* surroundBox_ = nullptr;
    QList<PanGrid*> grids_;
    QToolButton* advanced_ = nullptr;
    QLabel* lfeLabel_ = nullptr;
    bool lfeOnly_ = false;
    QString gangName_;
    double gain_ = 0;
    int panValue_ = 0;
    bool live_ = false;
    bool auto_ = false;
    bool syncing_ = false; // show() is setting the controls; their signals are ignored
};

// An Audio Grouping group (p.696-698): a named set of audio tracks shown in the Audio Mixer's Groups
// pane. Clicking it selects its tracks. `gang` is the gang letter its tracks share while it is
// ganged (Gang the Group), else empty. Ours: groups live in the mixer, as gangs do, and are not saved.
struct AudioGroup {
    QString name;
    QStringList tracks; // "A1", "A3", in track order
    bool selected = false;
    QString gang;
    bool operator==(const AudioGroup&) const = default;
};

// The Audio Track Grouping window's "Select Tracks in Group" text (picture on p.698): track numbers
// and ranges, "1-3, 5". parseTrackNumbers() reads it (spaces, commas or semicolons between items;
// nothing for text it cannot read); trackNumbersText() writes the shortest such text.
std::optional<QList<int>> parseTrackNumbers(const QString& text);
QString trackNumbersText(QList<int> numbers);

// The Audio Track Grouping window (picture on p.698): Group Name, Select Tracks in Group with Apply
// (the text to the check boxes) and Restore (back to the group as it was), a check box for each
// audio track, OK and Cancel.
class AudioGroupDialog : public QDialog {
    Q_OBJECT

public:
    AudioGroupDialog(const AudioGroup& group, const QStringList& tracks, QWidget* parent = nullptr);
    // The group as the dialog has it: the name, and the checked tracks.
    AudioGroup group() const;
    QLineEdit* nameBox() const { return name_; }
    QLineEdit* tracksBox() const { return text_; }
    QListWidget* trackList() const { return list_; }
    // Apply: checks the tracks the text names; false (nothing changed) when it cannot be read.
    bool apply();
    void restore();

private:
    AudioGroup initial_;
    QLineEdit* name_ = nullptr;
    QLineEdit* text_ = nullptr;
    QListWidget* list_ = nullptr;
};

// Tools > Audio Mixer (p.690-726): a strip per audio track of the
// sequence in the Record monitor and the Master strip; the Clip, Auto and
// Live modes; the Fast menu. In Clip mode the faders set the clip gain and
// pan of the segment under the position (p.702). In Auto mode they show and
// set the volume and pan automation there (Timeline::setAutomationValue,
// p.716), and record it while playing (p.717-719).
//
// A panel's contents ("AudioMixer"), made by MainWindow, which stores the mode and mix mode in the
// Audio Project setting. It follows Timeline::changed and Composer::changed. Clip mode changes go
// through Timeline::setClipAudio as undoable edits; Live mode values stay in the mixer.
class AudioMixer : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Clip, Auto, Live };

    // `timeline` must outlive the mixer.
    explicit AudioMixer(Timeline& timeline, QWidget* parent = nullptr);

    Mode mode() const { return mode_; }
    // Checks the mode's button; a new mode updates the strips and emits modeChanged().
    void setMode(Mode mode);
    static QString modeName(Mode mode); // "clip", "auto", "live"
    // Nothing for an unknown name.
    static std::optional<Mode> modeFromName(const QString& name);
    QToolButton* modeButton(Mode mode) const;
    QToolButton* bypassButton() const { return bypass_; }
    // The Mix Mode Selection button (p.691-692), the Monitor Mix Format (p.689): which speakers
    // listen (media::MonitorMix). MainWindow plays through it.
    QToolButton* mixModeButton() const { return mixMode_; }
    // media::monitorMixId(): "stereo", "mono", "5.1-protools", ...
    QString mixMode() const { return mixModeValue_; }
    media::MonitorMix monitorMix() const;
    // Other values are ignored; a change emits mixModeChanged().
    void setMixMode(const QString& value);
    // The Sequence Mix Format button (p.686, p.693): the Record monitor's sequence's
    // model::Sequence::mixFormat(), Stereo when there is none.
    QToolButton* sequenceMixButton() const { return sequenceMix_; }
    model::AudioFormat sequenceMixFormat() const;
    // Sets it as an undoable change ("Sequence Mix Format"); false when nothing changed.
    bool setSequenceMixFormat(model::AudioFormat format);
    // "Stereo Sequence", "5.1 Sequence", "7.1 Sequence".
    static QString sequenceMixName(model::AudioFormat format);

    // The audio tracks of the Record monitor's sequence, a strip each, in order.
    QStringList tracks() const;
    // A track's strip; null for a track not shown. Strips are remade when the tracks change.
    MixerStrip* strip(const QString& track) const { return strips_.value(track); }
    MixerStrip* master() const { return master_; }
    // The Master fader's level in dB (model::kGainOff for -∞), 0 by default: a gain on the whole
    // mix, after each track's. It is kept in the mixer, not in the sequence; MainWindow applies
    // it to what plays.
    double masterGain() const { return masterGain_; }
    // Clamped by model::clampGain; shows it on the Master strip and emits masterGainChanged() if it
    // changed.
    void setMasterGain(double db);
    // Fills the Fast menu for the current mode and selected tracks and returns it (the mixer
    // owns it).
    QMenu* fastMenu();
    bool isSoloed(const QString& track) const { return solo_.contains(track); }
    bool isMuted(const QString& track) const { return mute_.contains(track); }
    // Whether any track is soloed: then only soloed tracks are heard (p.698).
    bool anySoloed() const { return !solo_.isEmpty(); }
    // Solo (`solo`) or Mute on or off for a track, as its strip's button does; also from the
    // Timeline's Track Control panel (p.602). Emits soloMuteChanged().
    void setSoloMute(const QString& track, bool solo, bool on);
    // Gangs A-J (p.704): ganged faders move together.
    // An empty letter takes the track out of its gang.
    void setGang(const QString& track, const QString& letter);

    // Remakes the strips if the sequence's audio tracks changed, then shows the values.
    void refresh();

    // Surround panning (p.704-710). Stereo Link and Stereo Mirror for stereo tracks' two pans (the
    // control at the bottom of the mixer, p.693, and the Advanced Panner's buttons, p.705).
    model::StereoLink stereoLink() const { return stereoLink_; }
    void setStereoLink(model::StereoLink link);
    QToolButton* stereoLinkButton() const { return stereoLinkButton_; }
    // A strip's pan grid moved: in Clip mode the segment under the position takes it when `done`
    // (Timeline::setSurroundPan), with the other channel of a stereo track as the link says.
    void setSurroundPan(const QString& track, int channel, const model::SurroundPan& pan, bool done);
    // The Advanced Panner for a track (one at a time; it follows the position). Null if the track
    // cannot be surround-panned.
    AdvancedPanner* openAdvancedPanner(const QString& track);
    AdvancedPanner* advancedPanner() const { return panner_; }

    // Auto mode's recording (p.717-719): the Record and Cancel buttons and the preroll and
    // postroll in seconds, shown in Auto mode only.
    QToolButton* recordButton() const { return record_; }
    QToolButton* cancelButton() const { return cancel_; }
    QSpinBox* prerollBox() const { return preroll_; }
    QSpinBox* postrollBox() const { return postroll_; }
    bool isRecording() const { return recording_; }
    // Record (or the B key in the mixer): from the IN (else the position), backed up by the
    // preroll, playing with `play` until the OUT plus the postroll or until playback stops. While
    // it records, each fader or pan moved writes its value as a keyframe at every frame the Record
    // position reaches, between the marks (Timeline::writeAutomation). Not in Auto mode: nothing.
    void startRecording(bool play = true);
    // Record again keeps what was recorded as one undo step; Cancel (`cancel`) discards it.
    void stopRecording(bool cancel);
    // Go to Next/Previous Event with the mixer active in Auto mode (p.720): the Record position to
    // the next (1) or previous (-1) keyframe on the selected tracks. False when not in Auto mode or
    // there is none, for the Composer's own event.
    bool goToKeyframe(int direction);
    // Adjust Volume/Pan on Track's dialog (p.719): the volume (dB) and pan to add, or nothing to
    // cancel. Without one the mixer asks with its own dialog (tests set one).
    // Clip mode's Adjust Pan/Vols on Track (p.711) asks the same way.
    using AdjustAsk = std::function<std::optional<std::pair<double, int>>()>;
    void setAdjustAsk(AdjustAsk ask) { adjustAsk_ = std::move(ask); }

    // The top bar's Audio Loop Play and Render Effect buttons (p.692).
    QToolButton* loopButton() const { return loop_; }
    QToolButton* renderButton() const { return render_; }
    // Render Effect (p.692): renders the AudioSuite effects that need it on the selected tracks, in
    // the segments under the position or between the marks (Timeline::renderAudioSuite, with
    // MainWindow's renderer; the AudioSuite tool's target drive is not used). False with a message
    // (lastMessage()) when there is nothing to render. Audio dissolves are not rendered yet (they
    // play as they are).
    using Renderer = std::function<std::optional<model::AudioSuiteEffect>(const model::Segment&, QString* error)>;
    void setRenderer(Renderer renderer) { renderer_ = std::move(renderer); }
    bool renderEffect();
    QString lastMessage() const { return message_; }
    // Disable Track Monitoring (p.711): the selected tracks' monitors are turned off.
    void disableTrackMonitoring();
    // Set Display Options (p.711): what the strips show; the mixer keeps it (ours: not saved).
    const MixerDisplay& display() const { return display_; }
    void setDisplay(const MixerDisplay& display);
    // The Set Display Options dialog; without a hook the mixer asks with its own.
    using DisplayAsk = std::function<std::optional<MixerDisplay>(const MixerDisplay& current)>;
    void setDisplayAsk(DisplayAsk ask) { displayAsk_ = std::move(ask); }
    // Show Track Sidebar (p.691): the Tracks list, whose check boxes choose the strips shown, and the
    // Groups pane (p.696-698). The ">>" button shows it; "<<" hides it.
    QToolButton* sidebarButton() const { return sidebarButton_; }
    bool isSidebarShown() const;
    void setSidebarShown(bool shown);
    QListWidget* sidebarTracks() const { return sidebarTracks_; }
    QListWidget* groupList() const { return groupList_; }
    // Whether a track's strip is shown (all are, until the Tracks list hides some).
    bool isTrackShown(const QString& track) const { return !hidden_.contains(track); }
    void setTrackShown(const QString& track, bool shown);
    // Audio Grouping (p.696-698).
    const QList<AudioGroup>& groups() const { return groups_; }
    // New Group from Selected Tracks: the Audio Track Grouping window, with the selected tracks; OK
    // adds the group. Returns its index, or -1 when cancelled or empty.
    int newGroupFromSelectedTracks();
    // Modify the Group: the window again, for group `index`. False when cancelled.
    bool modifyGroup(int index);
    // Adds a group (a name is made up when it has none); returns its index. Tracks the sequence
    // does not have are kept, so a group outlives a track's removal.
    int addGroup(const AudioGroup& group);
    void duplicateGroup(int index);
    void removeGroup(int index);
    // Gang (or Ungang) the Group: its tracks take the first free gang letter (p.704), or leave it.
    void setGroupGanged(int index, bool ganged);
    // Disable all Groups: clicking a group does nothing until they are enabled again (ours: the
    // guide only names the item).
    bool groupsDisabled() const { return groupsDisabled_; }
    void setGroupsDisabled(bool disabled);
    // A click on a group (p.696): selects its tracks in the Timeline when it was not selected, else
    // deselects those not in another selected group. With `alt` (Alt/Option-click) selecting
    // replaces the track selection with the group, and deselecting deselects every audio track.
    void clickGroup(int index, bool alt);
    // The window's hook; without one the mixer asks with AudioGroupDialog (tests set one).
    using GroupAsk = std::function<std::optional<AudioGroup>(const AudioGroup& initial, const QStringList& tracks)>;
    void setGroupAsk(GroupAsk ask) { groupAsk_ = std::move(ask); }
    // The Groups pane's right-click menu, filled for the group under the click (-1: none).
    QMenu* groupMenu(int index);
    // Live mode (p.725-726). A track's Live Mix value: {dB, pan}.
    std::pair<double, int> liveValue(const QString& track) const;
    // Whether the track's Live Mix pan was moved (or set from automation); MainWindow plays the
    // Live Mix level always and the pan only then.
    bool hasLivePan(const QString& track) const { return livePan_.contains(track); }
    // Set Live Mix to Automation: each track's Live Mix takes the volume and pan automation's
    // values at the Record position (ours: the keyframes stay; see docs/spec/audio-mixer.md).
    void setLiveMixToAutomation();
    // Set Live Mix as Automation: Timeline::setLiveMixAsAutomation with the selected tracks' Live
    // Mix values.
    bool setLiveMixAsAutomation();

signals:
    // A track's Solo or Mute changed.
    void soloMuteChanged();
    // Clip or Auto: saved in the Audio Project setting (p.692).
    // Fires for Live too; MainWindow does not save that one.
    void modeChanged(Mode mode);
    // After setMixMode() changed it.
    void mixModeChanged(const QString& value);
    // The Master fader moved (also while dragging) or setMasterGain() changed it.
    void masterGainChanged(double db);
    // A Live Mix value changed (MainWindow plays it at once).
    void liveMixChanged();
    // A strip's Audio Track Effects menu > Audio Track Effect Tool (p.793).
    void trackEffectToolRequested(const QString& track);

private:
    // Replaces the strips with one per track, keeping each track's gang, solo and mute.
    void rebuild(const QStringList& tracks);
    // Shows each strip's values for the mode: the segment under the position (Clip), nothing
    // yet (Auto), or the Live mix.
    void syncValues();
    // A strip's fader or pan was set: moves the ganged tracks too, and stores it as the mode says.
    void setGain(const QString& track, double db, bool done);
    void setPan(const QString& track, int pan, bool done);
    // The tracks whose Timeline record track buttons are selected, in strip order.
    QStringList selectedTracks() const;
    // Where a segment without a pan of its own plays on the track: the Audio setting's Default Pan
    // for mono tracks (model::sidePan), else the middle.
    int defaultPan(const QString& track) const;

    Timeline& timeline_;
    Mode mode_ = Mode::Clip;
    QToolButton* modeButtons_[3] = {};
    QToolButton* bypass_ = nullptr;
    QToolButton* mixMode_ = nullptr;
    QToolButton* sequenceMix_ = nullptr;
    QToolButton* stereoLinkButton_ = nullptr;
    model::StereoLink stereoLink_ = model::StereoLink::Unlinked;
    // The open Advanced Panner, a child that deletes itself on close; QPointer clears it then, also
    // while the mixer itself is being destroyed.
    QPointer<AdvancedPanner> panner_;
    QToolButton* fast_ = nullptr;
    QString mixModeValue_ = QStringLiteral("stereo");
    QWidget* stripArea_ = nullptr;
    QHBoxLayout* stripLayout_ = nullptr;
    QLabel* empty_ = nullptr;
    QHash<QString, MixerStrip*> strips_; // track -> strip
    QStringList order_;                  // tracks(), the strips' order
    MixerStrip* master_ = nullptr;
    double masterGain_ = 0;
    QSet<QString> solo_;                 // by track name; kept when strips are remade
    QSet<QString> mute_;
    QHash<QString, QString> gangs_;      // track -> gang letter
    QHash<QString, double> liveGain_; // Live mode: not saved (p.725)
    QHash<QString, int> livePan_;
    // Auto mode recording.
    QToolButton* record_ = nullptr;
    QToolButton* cancel_ = nullptr;
    QSpinBox* preroll_ = nullptr;
    QSpinBox* postroll_ = nullptr;
    QWidget* recordControls_ = nullptr;
    bool recording_ = false;
    bool recordPlaying_ = false;           // playback was started for it
    std::optional<std::int64_t> recordEnd_; // the OUT plus the postroll
    std::int64_t lastWritten_ = -1;         // the Record position last written
    QSet<QString> touchedGain_;             // strips moved while recording
    QSet<QString> touchedPan_;
    AdjustAsk adjustAsk_;
    QToolButton* loop_ = nullptr;
    QToolButton* render_ = nullptr;
    Renderer renderer_;
    QString message_;
    MixerDisplay display_;
    DisplayAsk displayAsk_;
    // The sidebar (audio_groups.cpp).
    QWidget* sidebar_ = nullptr;
    QToolButton* sidebarButton_ = nullptr;
    QListWidget* sidebarTracks_ = nullptr;
    QListWidget* groupList_ = nullptr;
    QMenu* groupMenu_ = nullptr;
    QSet<QString> hidden_;
    QList<AudioGroup> groups_;
    bool groupsDisabled_ = false;
    GroupAsk groupAsk_;
    // Builds the sidebar; called once by the constructor.
    QWidget* makeSidebar();
    // Refills the Tracks list and the Groups pane from order_ and groups_.
    void syncSidebar();
    // Selects or deselects audio tracks in the Timeline.
    void selectTracks(const QSet<QString>& tracks, bool replace);

    // While recording: writes the moved strips at a new position, and stops at the end.
    void recordTick();
    // Whether the Record position is between the marks (or there are none).
    bool inRecordRange() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

// The calibration tone (p.920): -20 dB at 1000 Hz unless changed.
using CalibrationTone = media::Tone;

// Set Calibration Tone (p.920): tone level and frequency.
class CalibrationToneDialog : public QDialog {
    Q_OBJECT

public:
    // Starts at `tone`. The level is -90 to 0 dB, the frequency 1 to 20000 Hz.
    explicit CalibrationToneDialog(const CalibrationTone& tone, QWidget* parent = nullptr);
    // The values entered.
    CalibrationTone tone() const;

private:
    QSpinBox* level_ = nullptr;
    QSpinBox* frequency_ = nullptr;
};

// Create Tone Media (p.158-159): the tone's level and frequency (0 for random noise, -777 for a
// sweep), how many tracks (1 to 8), the target bin and the target drive. The length is ours (the
// guide gives none): 60 seconds unless changed.
struct ToneMediaChoice {
    CalibrationTone tone;
    int tracks = 1;
    int seconds = 60;
    QString bin;   // a bin's node id
    QString drive; // a media drive's root
};

class CreateToneMediaDialog : public QDialog {
    Q_OBJECT

public:
    // Starts at the calibration tone; `bins` and `drives` are (id or root, name) pairs, with
    // `bin` and `drive` chosen first.
    CreateToneMediaDialog(const CalibrationTone& tone, const QList<QPair<QString, QString>>& bins, const QString& bin,
                          const QList<QPair<QString, QString>>& drives, const QString& drive, QWidget* parent = nullptr);
    ToneMediaChoice choice() const;
    // OK is dimmed for a frequency other than 1-20000 Hz, 0 or -777, or without a bin or drive.
    bool isValid() const;

private:
    void update();
    QSpinBox* level_ = nullptr;
    QSpinBox* frequency_ = nullptr;
    QSpinBox* tracks_ = nullptr;
    QSpinBox* seconds_ = nullptr;
    QComboBox* bins_ = nullptr;
    QComboBox* drives_ = nullptr;
    QPushButton* ok_ = nullptr;
};

// The Audio Tool's meters (picture on p.158): the digital scale on the
// left, 0 to -90 dB, the VU scale on the right with 0 VU at the reference
// level; green below the reference, yellow to -3 dB, red above (p.157).
class AudioMeters : public QWidget {
    Q_OBJECT

public:
    // The PH menu's peak hold (p.157 names Peak Hold and Infinite Hold; the guide does not say how
    // long Peak Hold holds, so this follows common peak meters): PeakHold keeps a peak for
    // kPeakHoldMs, then lets it fall to the current level; Infinite keeps it until resetPeaks().
    enum class Hold { PeakHold, Infinite };
    static constexpr int kPeakHoldMs = 2000;
    // Milliseconds from any fixed start; peak hold times are measured with it.
    using Clock = std::function<qint64()>;

    // `channels` bars, all at -90 dB.
    explicit AudioMeters(int channels, QWidget* parent = nullptr);
    int channels() const { return static_cast<int>(levels_.size()); }
    // A channel's level in dB (0-based channel; others are ignored); raises its peak if higher,
    // and lets an expired peak fall as hold() says.
    void setLevel(int channel, double db);
    Hold hold() const { return hold_; }
    void setHold(Hold hold);
    // Replaces the clock (tests); an empty function is ignored.
    void setClock(Clock clock);
    // Where a level sits on the digital scale, 0 at the top (0 dB) to 1 at the bottom (-90 dB):
    // the scale's marks equally spaced, as pictured on p.158. levelAt() is its inverse.
    static double scalePosition(double db);
    static double levelAt(double position);
    // Sets a channel's peak mark directly.
    void setPeak(int channel, double db);
    // RP: every peak back to -90 dB.
    void resetPeaks();
    // -90 for a channel that does not exist.
    double peak(int channel) const { return peaks_.value(channel, -90); }
    // The level shown as 0 VU, in dB (-20).
    int referenceLevel() const { return reference_; }
    // The colour of a level against the reference (p.157).
    static QColor colourFor(double db, int reference);
    QSize sizeHint() const override;

protected:
    // The two scales, then each channel's bar and peak line.
    void paintEvent(QPaintEvent* event) override;

private:
    QList<double> levels_;
    QList<double> peaks_;
    QList<qint64> peakTimes_; // clock_() when each peak was set
    Hold hold_ = Hold::PeakHold;
    Clock clock_;
    int reference_ = -20;
};

// Tools > Audio Tool (p.157-159): RP (Reset Peak), an In/Out button per
// channel, the PH (Peak Hold) menu and the meters. MainWindow plays the
// tone and feeds the meters what the output plays (AudioPlayer::takePeaks).
// A panel's contents ("AudioTool"), made by MainWindow.
class AudioTool : public QWidget {
    Q_OBJECT

public:
    explicit AudioTool(int channels = 8, QWidget* parent = nullptr);

    AudioMeters* meters() const { return meters_; }
    QToolButton* resetPeakButton() const { return reset_; }
    // PH: its menu holds Peak Hold, Infinite Hold, Set Calibration Tone, Play Calibration Tone and
    // Create Tone Media.
    QToolButton* peakHoldButton() const { return peakHold_; }
    // A channel's In/Out button (0-based); null for one that does not exist.
    QToolButton* ioButton(int channel) const { return io_.value(channel); }
    // Whether a channel's button says O (Output, checked, the default); false for one that does not
    // exist.
    bool isOutput(int channel) const;
    const CalibrationTone& calibrationTone() const { return tone_; }
    // Stores the tone and emits calibrationToneChanged(), even if it is the same.
    void setCalibrationTone(const CalibrationTone& tone);

    // The PH menu's hold choice, which the meters follow.
    using Hold = AudioMeters::Hold;
    Hold hold() const { return meters_->hold(); }

    // The PH menu's Play Calibration Tone and Create Tone Media... items.
    QAction* playToneAction() const { return playTone_; }
    QAction* createToneMediaAction() const { return createTone_; }

signals:
    // After setCalibrationTone(), also from the Set Calibration Tone dialog.
    void calibrationToneChanged(const CalibrationTone& tone);
    // PH > Play Calibration Tone (p.920); MainWindow plays it until Reset Peak (p.157).
    void playToneRequested();
    // PH > Create Tone Media... (p.158); MainWindow asks for the options and makes the clip.
    void createToneMediaRequested();
    // RP was clicked: the peaks are reset; the tone stops (p.157).
    void resetPeakClicked();

private:
    AudioMeters* meters_ = nullptr;
    QToolButton* reset_ = nullptr;
    QToolButton* peakHold_ = nullptr;
    QList<QToolButton*> io_;
    CalibrationTone tone_;
    QAction* playTone_ = nullptr;
    QAction* createTone_ = nullptr;
};

} // namespace lumenis::ui
