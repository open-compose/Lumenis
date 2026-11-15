#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/ui/timeline.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QToolButton;

namespace lumenis::media {
class AudioInput;
}

namespace lumenis::ui {

// Tools > Audio Punch-In (Editing Guide p.747-753; docs/spec/punch-in.md): records voice-over from an
// audio input straight into the Record monitor's sequence while it plays. One take, from the first
// play to Stop, is one master clip; each punch-in (Record pressed, then pressed again, or the marks)
// is a segment of it on the chosen tracks, an undo step of its own (Timeline::punchIn).
//
// A panel's contents ("AudioPunchIn"), made by MainWindow, which writes the take as managed media
// (setClipMaker), lists the drives and bins (setTargets), silences the target tracks while a take
// runs (silencedTracks) and plays the input to the speakers when monitoring is on (monitorsInput).
// It follows Composer::changed and Composer::playStopped.
class AudioPunchInTool : public QWidget {
    Q_OBJECT

public:
    // The Monitor Mode Input button (p.748-749, p.753).
    enum class Monitoring { Automatic, On, Off, Manual };
    // Idle; Armed (playing the preroll, recording starts at the IN or the position); Playing (a take
    // runs, not punched in: rehearsing, p.747); Recording (punched in); Finishing (punched out at the
    // OUT, playing on through the postroll and handles).
    enum class State { Idle, Armed, Playing, Recording, Finishing };

    // A finished take for MainWindow to write: each input channel's 48 kHz samples, the sequence's
    // name and edit rate, and the drive and bin chosen.
    struct Take {
        QString sequenceName;
        Rational editRate{25, 1};
        QString drive;
        QString binId;
        std::vector<std::vector<float>> channels;
    };
    // Writes the take as a master clip of channels.size() audio tracks in the bin; nothing, with
    // `error` set, if it could not.
    using ClipMaker = std::function<std::optional<model::BinItem>(const Take& take, QString* error)>;
    // Opens the input (media::AudioInput::start by default; tests simulate one).
    using InputStarter = std::function<bool(media::AudioInput& input, const QString& device, int channels)>;

    // `timeline` must outlive the tool.
    explicit AudioPunchInTool(Timeline& timeline, QWidget* parent = nullptr);
    ~AudioPunchInTool() override;

    void setClipMaker(ClipMaker maker) { clipMaker_ = std::move(maker); }
    void setInputStarter(InputStarter starter) { inputStarter_ = std::move(starter); }
    // The Audio Project setting's Remove Extra Filler After Punchin (p.1149): off, a take recorded past the end of the
    // sequence leaves its target tracks filled to where recording stopped; on, they end with the last punch-in.
    void setRemoveExtraFiller(bool on) { removeExtraFiller_ = on; }
    bool removeExtraFiller() const { return removeExtraFiller_; }
    // The Target Drive and Target Bin menus (p.749): the drives as (name, root) and the bins that can
    // be changed as (id, name), with the root and bin id to choose.
    void setTargets(const QList<QPair<QString, QString>>& drives, const QString& drive,
                    const QList<QPair<QString, QString>>& bins, const QString& bin);
    // The Input Source menu (p.749): the inputs, by name, and how many channels each has. The first is
    // the default.
    void setInputs(const QList<QPair<QString, int>>& inputs);

    // The controls (picture and table on p.748-749).
    QToolButton* playButton() const { return play_; }
    QToolButton* recordButton() const { return record_; }
    QToolButton* stopButton() const { return stop_; }
    QToolButton* goToInButton() const { return goToIn_; }
    QToolButton* cancelButton() const { return cancel_; }
    QToolButton* audioToolButton() const { return audioTool_; }
    QToolButton* monitorButton() const { return monitor_; }
    QCheckBox* stopAtEndBox() const { return stopAtEnd_; }
    QSpinBox* prerollBox() const { return preroll_; }
    QSpinBox* postrollBox() const { return postroll_; }
    QSpinBox* handlesBox() const { return handles_; }
    QComboBox* inputSourceBox() const { return inputSource_; }
    // CH1-CH4 and their Timeline Track menus.
    static constexpr int kChannels = 4;
    QToolButton* channelButton(int i) const { return channels_[i]; }
    QComboBox* trackBox(int i) const { return tracks_[i]; }
    QComboBox* driveBox() const { return drive_; }
    QComboBox* binBox() const { return bin_; }
    QLabel* messageLabel() const { return message_; }

    // The input hardware channel (0-based) a CH button records; Alt+click chooses another (p.748).
    int hardwareChannel(int i) const { return hardware_[i]; }
    void setHardwareChannel(int i, int channel);

    Monitoring monitoring() const { return monitoring_; }
    void setMonitoring(Monitoring mode);
    // Whether the input is heard now: On, or Automatic while the input's delay is under 100 ms.
    bool monitorsInput() const;

    State state() const { return state_; }
    bool isActive() const { return state_ != State::Idle; }
    media::AudioInput& input() const { return *input_; }
    // The selected channels' Timeline Track choices, in order: a track name, or empty for New Track.
    QStringList targets() const;
    // The existing tracks the take goes to, silenced while it runs (ours: "overwrites the audio on
    // that track and silences that portion during playback", p.749).
    QStringList silencedTracks() const;
    // The punch-ins of the take so far.
    const QList<Timeline::PunchIn>& punches() const { return punches_; }

    // Play In/Out (V, p.748): plays the take's range, IN to OUT (else the position to the end), in a
    // loop until a recording completes.
    void playInOut();
    // Record (B, p.748): see the class comment and docs/spec/punch-in.md.
    void record();
    // Stop (p.748): ends the take and keeps it; Cancel throws it away.
    void stopTake();
    void cancelTake();
    // Go to Mark In (p.748): the IN, else where the take started, else the start.
    void goToIn();

    // Remakes the track menus for the record sequence's mono audio tracks.
    void refresh();

signals:
    // The Audio Tool button (p.748).
    void audioToolRequested();
    // A take started or ended: MainWindow then (un)silences tracks and starts or stops monitoring.
    void activeChanged(bool active);

protected:
    // V and B while the tool has the keyboard (p.750-751).
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Dims the channel buttons the input does not have, and all of them while a take runs.
    void dimChannels();
    bool begin(std::int64_t from, bool loop);
    void punchInAt(std::int64_t at);
    void punchOutAt(std::int64_t end);
    void finish(bool keep);
    void tick();
    void playStopped();
    std::int64_t recordedFrames() const;
    std::int64_t position() const;
    std::int64_t lastFrame() const;
    int fps() const;
    void setState(State state);
    void fail(const QString& text);

    Timeline& timeline_;
    std::unique_ptr<media::AudioInput> input_;
    ClipMaker clipMaker_;
    InputStarter inputStarter_;
    QToolButton* play_ = nullptr;
    QToolButton* record_ = nullptr;
    QToolButton* stop_ = nullptr;
    QToolButton* goToIn_ = nullptr;
    QToolButton* cancel_ = nullptr;
    QToolButton* audioTool_ = nullptr;
    QToolButton* monitor_ = nullptr;
    QCheckBox* stopAtEnd_ = nullptr;
    QSpinBox* preroll_ = nullptr;
    QSpinBox* postroll_ = nullptr;
    QSpinBox* handles_ = nullptr;
    QComboBox* inputSource_ = nullptr;
    QToolButton* channels_[kChannels]{};
    QComboBox* tracks_[kChannels]{};
    int hardware_[kChannels]{0, 1, 2, 3};
    QComboBox* drive_ = nullptr;
    QComboBox* bin_ = nullptr;
    QLabel* message_ = nullptr;
    Monitoring monitoring_ = Monitoring::Automatic;

    State state_ = State::Idle;
    bool loop_ = false;
    bool completed_ = false;  // a recording ended at the OUT: no more looping (p.748)
    bool removeExtraFiller_ = false;
    bool extended_ = false;   // the take records past the end (Stop at End off): the Record monitor's extension is set
    bool finishing_ = false;  // finish() is stopping play; its playStopped is ours
    std::int64_t loopStart_ = 0;
    std::int64_t loopEnd_ = 0; // the last frame of the range
    std::int64_t armAt_ = 0;
    std::int64_t punchStart_ = 0;
    std::int64_t punchSource_ = 0;
    std::optional<std::int64_t> finishAt_;
    std::optional<std::int64_t> startedAt_; // where the take started, for Go to Mark In
    QStringList takeTargets_;
    QList<Timeline::PunchIn> punches_;
};

} // namespace lumenis::ui
