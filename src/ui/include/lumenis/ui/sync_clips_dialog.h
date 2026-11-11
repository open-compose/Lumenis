#pragma once

#include "lumenis/model/sync_clips.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QSpinBox;

// The Sync Selection dialog of Sync Clips (Editing Guide p.497-498; docs/spec/sync-clips.md): what the clips are synced
// by and which audio tracks the synced subclip keeps. MainWindow opens it on the bin's selection and syncs the clips
// with model::syncClips().
namespace lumenis::ui {

class SyncClipsDialog : public QDialog {
    Q_OBJECT

public:
    // `filmProject`: a 24p or 25p project; otherwise Film TC/Sound TC "appears dimmed".
    explicit SyncClipsDialog(bool filmProject, QWidget* parent = nullptr);

    // The options as chosen (the marks and the waveform analysis are MainWindow's to add).
    model::SyncClipsOptions options() const;
    // Shows `options` (for tests and the last choice).
    void setOptions(const model::SyncClipsOptions& options);
    QRadioButton* button(model::GroupSync sync) const;

private:
    QButtonGroup* methods_ = nullptr;
    QComboBox* aux_ = nullptr;
    QCheckBox* keepVideoAudio_ = nullptr;
    QSpinBox* keepStart_ = nullptr;
    QSpinBox* keepEnd_ = nullptr;
    QCheckBox* audioOnly_ = nullptr;
    QSpinBox* audioStart_ = nullptr;
    QSpinBox* audioEnd_ = nullptr;
    QCheckBox* collapse_ = nullptr;
    QCheckBox* group_ = nullptr;
};

} // namespace lumenis::ui
