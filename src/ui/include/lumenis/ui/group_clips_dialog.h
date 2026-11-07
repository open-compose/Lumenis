#pragma once

#include "lumenis/model/group_clip.h"

#include <QDialog>

#include <optional>

class QButtonGroup;
class QComboBox;
class QRadioButton;

// Clip > Group Clips' dialog (Editing Guide p.1124; docs/spec/multicamera.md): what the clips are synced
// by. MainWindow opens it on the bin's selection and groups the clips with model::groupClips().
namespace lumenis::ui {

class GroupClipsDialog : public QDialog {
    Q_OBJECT

public:
    // `filmProject`: a 24p or 25p project, where INK Number and Auxiliary Ink appear and Film TC/Sound TC
    // could be chosen ("This option appears dimmed if you are not working in a 24p or 25p project").
    // Ours: Film TC/Sound TC and the ink options stay dimmed, not built yet.
    explicit GroupClipsDialog(bool filmProject, QWidget* parent = nullptr);

    model::GroupSync sync() const;
    int auxTc() const; // 1-5
    // Selects an option (for tests and the last choice).
    void setSync(model::GroupSync sync, int auxTc = 1);
    QRadioButton* button(model::GroupSync sync) const;
    QComboBox* auxMenu() const { return aux_; }

private:
    QButtonGroup* options_ = nullptr;
    QComboBox* aux_ = nullptr;
};

} // namespace lumenis::ui
