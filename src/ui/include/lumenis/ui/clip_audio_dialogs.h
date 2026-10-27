#pragma once

#include <QDialog>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QRadioButton;

// The dialogs of the Clip menu's audio commands (Editing Guide p.216, p.668, p.731, p.754;
// docs/spec/clip-audio.md): Apply Clip Gain, Auto VO, Read Audio Timecode and Change Sample Rate. MainWindow opens them on the bin's selection and does the work with
// lumenis/model/clip_audio.h.
namespace lumenis::ui {

// Clip > Audio > Apply Gain: "Type a decibel level from 12 to -96 to adjust the volume, or use the
// Up and Down arrows on the keyboard" (p.216). The box starts at the clip's gain.
class ApplyGainDialog : public QDialog {
    Q_OBJECT

public:
    explicit ApplyGainDialog(double current, QWidget* parent = nullptr);
    double gain() const;
    QDoubleSpinBox* gainBox() const { return box_; }

private:
    QDoubleSpinBox* box_ = nullptr;
};

// Clip > Auto VO (p.754): the Using track menu (the sequence's audio tracks) and Extract segments or
// Keep segments.
class AutoVoDialog : public QDialog {
    Q_OBJECT

public:
    explicit AutoVoDialog(const QStringList& audioTracks, QWidget* parent = nullptr);
    QString track() const;
    bool keep() const;
    QComboBox* trackMenu() const { return track_; }
    QRadioButton* extractButton() const { return extract_; }
    QRadioButton* keepButton() const { return keep_; }

private:
    QComboBox* track_ = nullptr;
    QRadioButton* extract_ = nullptr;
    QRadioButton* keep_ = nullptr;
};

// Clip > Read Audio Timecode (p.668-669): User Bit Timecode, the Audio Timecode Source (the audio
// tracks; A1 the default), the Destination Track (Auxiliary TC1-5; TC1 the default) and Fill
// Undecodeable Frames (on by default).
class ReadAudioTimecodeDialog : public QDialog {
    Q_OBJECT

public:
    explicit ReadAudioTimecodeDialog(int audioTracks, QWidget* parent = nullptr);
    bool userBits() const;
    int sourceTrack() const;   // 0 for A1
    QString destination() const; // "Auxiliary TC1" ...
    bool fill() const;
    QCheckBox* userBitsBox() const { return userBits_; }
    QComboBox* sourceMenu() const { return source_; }
    QComboBox* destinationMenu() const { return destination_; }
    QCheckBox* fillBox() const { return fill_; }

private:
    QCheckBox* userBits_ = nullptr;
    QComboBox* source_ = nullptr;
    QComboBox* destination_ = nullptr;
    QCheckBox* fill_ = nullptr;
};

// Clip > Audio > Change Sample Rate (p.731): Sample Rate (32, 44.1 or 48 kHz), Quality (High,
// Medium, Low), Delete Original Media, and the Target Drive. `drives` are {name, root} pairs.
class ChangeSampleRateDialog : public QDialog {
    Q_OBJECT

public:
    ChangeSampleRateDialog(const QList<std::pair<QString, QString>>& drives, QWidget* parent = nullptr);
    int sampleRate() const;
    // The resampler's filter length for the quality (media::Resampler): 64, 32 or 8.
    int filterSize() const;
    bool deleteOriginal() const;
    QString drive() const; // a drive root
    QComboBox* rateMenu() const { return rate_; }
    QComboBox* qualityMenu() const { return quality_; }
    QCheckBox* deleteBox() const { return delete_; }
    QComboBox* driveMenu() const { return drive_; }

private:
    QComboBox* rate_ = nullptr;
    QComboBox* quality_ = nullptr;
    QCheckBox* delete_ = nullptr;
    QComboBox* drive_ = nullptr;
};

} // namespace lumenis::ui
