#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/consolidate.h"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace lumenis::ui {

// Clip > Consolidate/Transcode (Editing Guide p.350-356).
//
// Only collects the options: MainWindow opens it, takes options(), and runs media::consolidate
// (src/media/consolidate.*) on the selected clips, in the application or as a BackgroundQueue job
// per clip.
// Controls we do not implement yet are shown disabled with the tooltip "Not available yet".
class ConsolidateDialog : public QDialog {
    Q_OBJECT

public:
    // `hasSequences`: Create new sequence(s) applies (p.351).
    // `editRate` sets the default handle length (media::defaultHandles). The drive lists come
    // from media::mediaDrives(), the one with the most space chosen.
    ConsolidateDialog(const Rational& editRate, bool hasSequences, QWidget* parent = nullptr);

    // Consolidate or Transcode mode; the options shown and the Go button's name follow it.
    void setTranscode(bool transcode);
    bool isTranscode() const;
    // The drive chosen, the most spacious one if `drive` is not there.
    // Sets both the video and the audio list.
    void setDrive(const QString& drive);
    // The Target Video Resolution by media::Resolution id; an unknown id leaves it as it is.
    void setResolution(const QString& id);
    // The target audio bit depth: 16 for 16 or fewer, else 24.
    void setBitDepth(int bits);
    // Processing Options: Background Process (p.352, p.356).
    void setBackground(bool background);
    bool isBackground() const;

    // Everything but the project's raster and name, and the progress.
    media::ConsolidateOptions options() const;
    // "In a MediaCentral Production Management environment, with Dynamic Relink enabled, select this option to create
    // new master clips for the transcoded media" (p.354-355): Create new clips is offered while Dynamic Relink is on
    // (MainWindow::dynamicRelinkSettings); otherwise it is dimmed and new clips are made.
    void setDynamicRelink(bool on);

    // The Target Drive(s) lists; each item's Qt::UserRole is the drive's root folder.
    QListWidget* drives() const { return videoDrives_; }
    QListWidget* audioDrives() const { return audioDrives_; }

    // The Copying Media Files dialog (p.352), after Consolidate with master
    // clips: whether they stay with the media on the original drive.
    // Nothing if it was cancelled.
    static std::optional<bool> askKeepMastersLinked(QWidget* parent);

private:
    // Shows the controls of the current mode and drive choice, and enables the Go button only
    // while a target drive is chosen.
    void update();

    QRadioButton* consolidate_ = nullptr;
    QRadioButton* transcode_ = nullptr;
    QListWidget* videoDrives_ = nullptr;
    QListWidget* audioDrives_ = nullptr;
    QLabel* audioLabel_ = nullptr;
    QCheckBox* sameDrive_ = nullptr;
    QSpinBox* handles_ = nullptr;
    QCheckBox* newSequences_ = nullptr;
    QCheckBox* linkedOnly_ = nullptr;
    QCheckBox* skipOnTarget_ = nullptr;
    QCheckBox* deleteOriginals_ = nullptr;
    QCheckBox* convertBitDepth_ = nullptr;
    QComboBox* bitDepth_ = nullptr;
    QWidget* transcodeBox_ = nullptr;
    QWidget* consolidateBox_ = nullptr;
    QComboBox* resolution_ = nullptr;
    QComboBox* raster_ = nullptr;
    QCheckBox* applyColorEncoding_ = nullptr;
    QCheckBox* newClips_ = nullptr; // Create new clips (p.354-355)
    QPushButton* go_ = nullptr;
    QRadioButton* background_ = nullptr;
};

} // namespace lumenis::ui
