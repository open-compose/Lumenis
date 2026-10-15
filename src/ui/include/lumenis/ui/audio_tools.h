#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QWidget>

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
inline constexpr int kRange = 1100;
int position(double db);
double gain(int position);
QString text(double db);              // "-6.0", "+12.0", "-∞"
std::optional<double> parse(const QString& text);
QString panText(int pan);             // "L57", "MID", "R57"
std::optional<int> parsePan(const QString& text);
} // namespace fader

// One track's strip of the Audio Mixer (picture and table on p.693-694).
class MixerStrip : public QWidget {
    Q_OBJECT

public:
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

    double gain() const { return gain_; }
    int pan() const { return panValue_; }
    // Shows values without signalling.
    void show(double gain, int pan);
    // The Volume Level display's text in place of a value ("Clip", "Auto").
    void showText(const QString& text);
    // Live mode colours the fader area red (p.725); levels over +12 dB are
    // yellow (p.670).
    void setLive(bool on);
    void setGang(const QString& letter);
    const QString& gangName() const { return gangName_; }

signals:
    // The fader or Volume Level display was set; `done` once the drag ends.
    void gainSet(double db, bool done);
    void panSet(int pan, bool done);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
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
    QString gangName_;
    double gain_ = 0;
    int panValue_ = 0;
    bool live_ = false;
    bool syncing_ = false;
};

// Tools > Audio Mixer (p.690-726): a strip per audio track of the
// sequence in the Record monitor and the Master strip; the Clip, Auto and
// Live modes; the Fast menu. In Clip mode the faders set the clip gain and
// pan of the segment under the position (p.702). Volume automation (Auto
// mode) and the sound itself come with Phase 5.
class AudioMixer : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Clip, Auto, Live };

    explicit AudioMixer(Timeline& timeline, QWidget* parent = nullptr);

    Mode mode() const { return mode_; }
    void setMode(Mode mode);
    static QString modeName(Mode mode); // "clip", "auto", "live"
    static std::optional<Mode> modeFromName(const QString& name);
    QToolButton* modeButton(Mode mode) const;
    QToolButton* bypassButton() const { return bypass_; }
    QToolButton* mixModeButton() const { return mixMode_; }
    QString mixMode() const { return mixModeValue_; }
    void setMixMode(const QString& value);

    QStringList tracks() const;
    MixerStrip* strip(const QString& track) const { return strips_.value(track); }
    MixerStrip* master() const { return master_; }
    QMenu* fastMenu();
    bool isSoloed(const QString& track) const { return solo_.contains(track); }
    bool isMuted(const QString& track) const { return mute_.contains(track); }
    // Gangs A-J (p.704): ganged faders move together.
    void setGang(const QString& track, const QString& letter);

    void refresh();

signals:
    // Clip or Auto: saved in the Audio Project setting (p.692).
    void modeChanged(Mode mode);
    void mixModeChanged(const QString& value);

private:
    void rebuild(const QStringList& tracks);
    void syncValues();
    void setGain(const QString& track, double db, bool done);
    void setPan(const QString& track, int pan, bool done);
    QStringList selectedTracks() const;

    Timeline& timeline_;
    Mode mode_ = Mode::Clip;
    QToolButton* modeButtons_[3] = {};
    QToolButton* bypass_ = nullptr;
    QToolButton* mixMode_ = nullptr;
    QToolButton* fast_ = nullptr;
    QString mixModeValue_ = QStringLiteral("stereo");
    QWidget* stripArea_ = nullptr;
    QHBoxLayout* stripLayout_ = nullptr;
    QLabel* empty_ = nullptr;
    QHash<QString, MixerStrip*> strips_;
    QStringList order_;
    MixerStrip* master_ = nullptr;
    QSet<QString> solo_;
    QSet<QString> mute_;
    QHash<QString, QString> gangs_;
    QHash<QString, double> liveGain_; // Live mode: not saved (p.725)
    QHash<QString, int> livePan_;
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
    explicit CalibrationToneDialog(const CalibrationTone& tone, QWidget* parent = nullptr);
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
    explicit AudioMeters(int channels, QWidget* parent = nullptr);
    int channels() const { return static_cast<int>(levels_.size()); }
    void setLevel(int channel, double db);
    void setPeak(int channel, double db);
    void resetPeaks();
    double peak(int channel) const { return peaks_.value(channel, -90); }
    int referenceLevel() const { return reference_; }
    // The colour of a level against the reference (p.157).
    static QColor colourFor(double db, int reference);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<double> levels_;
    QList<double> peaks_;
    int reference_ = -20;
};

// Tools > Audio Tool (p.157-159): RP (Reset Peak), an In/Out button per
// channel, the PH (Peak Hold) menu and the meters. Nothing plays yet, so
// the meters stay at the bottom until Phase 5.
class AudioTool : public QWidget {
    Q_OBJECT

public:
    explicit AudioTool(int channels = 8, QWidget* parent = nullptr);

    AudioMeters* meters() const { return meters_; }
    QToolButton* resetPeakButton() const { return reset_; }
    QToolButton* peakHoldButton() const { return peakHold_; }
    QToolButton* ioButton(int channel) const { return io_.value(channel); }
    bool isOutput(int channel) const;
    const CalibrationTone& calibrationTone() const { return tone_; }
    void setCalibrationTone(const CalibrationTone& tone);

    enum class Hold { None, PeakHold, Infinite };
    Hold hold() const { return hold_; }

signals:
    void calibrationToneChanged(const CalibrationTone& tone);

private:
    AudioMeters* meters_ = nullptr;
    QToolButton* reset_ = nullptr;
    QToolButton* peakHold_ = nullptr;
    QList<QToolButton*> io_;
    CalibrationTone tone_;
    Hold hold_ = Hold::PeakHold;
};

} // namespace lumenis::ui
