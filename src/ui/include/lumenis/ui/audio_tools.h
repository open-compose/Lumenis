#pragma once

#include "lumenis/media/surround.h"
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

class QButtonGroup;
class QDial;
class QHBoxLayout;
class QLabel;
class QLineEdit;
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
    using AdjustAsk = std::function<std::optional<std::pair<double, int>>()>;
    void setAdjustAsk(AdjustAsk ask) { adjustAsk_ = std::move(ask); }

signals:
    // Clip or Auto: saved in the Audio Project setting (p.692).
    // Fires for Live too; MainWindow does not save that one.
    void modeChanged(Mode mode);
    // After setMixMode() changed it.
    void mixModeChanged(const QString& value);
    // The Master fader moved (also while dragging) or setMasterGain() changed it.
    void masterGainChanged(double db);

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

    // While recording: writes the moved strips at a new position, and stops at the end.
    void recordTick();
    // Whether the Record position is between the marks (or there are none).
    bool inRecordRange() const;

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

// The calibration tone (p.920): -20 dB at 1000 Hz unless changed.
struct CalibrationTone {
    int level = -20;
    int frequency = 1000;
};

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
// channel, the PH (Peak Hold) menu and the meters. Nothing plays yet, so
// the meters stay at the bottom until Phase 5.
// A panel's contents ("AudioTool"), made by MainWindow.
class AudioTool : public QWidget {
    Q_OBJECT

public:
    explicit AudioTool(int channels = 8, QWidget* parent = nullptr);

    AudioMeters* meters() const { return meters_; }
    QToolButton* resetPeakButton() const { return reset_; }
    // PH: its menu holds Peak Hold, Infinite Hold and Set Calibration Tone.
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

signals:
    // After setCalibrationTone(), also from the Set Calibration Tone dialog.
    void calibrationToneChanged(const CalibrationTone& tone);

private:
    AudioMeters* meters_ = nullptr;
    QToolButton* reset_ = nullptr;
    QToolButton* peakHold_ = nullptr;
    QList<QToolButton*> io_;
    CalibrationTone tone_;
};

} // namespace lumenis::ui
