#pragma once

#include <QDialog>
#include <QStringList>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QRadioButton;

// The dialogs of the Clip menu's audio commands (Editing Guide p.216, p.754; docs/spec/clip-audio.md):
// Apply Clip Gain and Auto VO. MainWindow opens them on the bin's selection and does the work with
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

} // namespace lumenis::ui
