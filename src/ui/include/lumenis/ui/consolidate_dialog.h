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
class ConsolidateDialog : public QDialog {
    Q_OBJECT

public:
    // `hasSequences`: Create new sequence(s) applies (p.351).
    ConsolidateDialog(const Rational& editRate, bool hasSequences, QWidget* parent = nullptr);

    void setTranscode(bool transcode);
    bool isTranscode() const;
    // The drive chosen, the most spacious one if `drive` is not there.
    void setDrive(const QString& drive);
    void setResolution(const QString& id);
    void setBitDepth(int bits);

    // Everything but the project's raster and name, and the progress.
    media::ConsolidateOptions options() const;

    QListWidget* drives() const { return videoDrives_; }
    QListWidget* audioDrives() const { return audioDrives_; }

    // The Copying Media Files dialog (p.352), after Consolidate with master
    // clips: whether they stay with the media on the original drive.
    // Nothing if it was cancelled.
    static std::optional<bool> askKeepMastersLinked(QWidget* parent);

private:
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
    QPushButton* go_ = nullptr;
};

} // namespace lumenis::ui
