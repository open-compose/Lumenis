// The Export Volume dialog box (Phase 7.2d; docs/spec/as02.md; Editing Guide p.902-903): File > New > Volume for Export
// and an Export Volume bin's Properties button. "Select a Volume Type, for example AS-02. Click Set to select the path
// where you want the Export Volume to reside. Enter a name for the volume. Click Set to select the path to the shim
// template you want to use."
#pragma once

#include "lumenis/model/export_volume.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLineEdit;

namespace lumenis::ui {

class ExportVolumeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportVolumeDialog(const model::ExportVolume& volume, QWidget* parent = nullptr);

    // The volume as the dialog has it, the shim template's names read in (model::readShimTemplate).
    model::ExportVolume volume() const;
    // For tests and the Set buttons.
    void setPath(const QString& path);
    void setName(const QString& name);
    void setShimFile(const QString& path);
    void setCompression(const QString& id);
    // Stand in for the folder and file choosers of the Set buttons (tests); they return the path, empty to cancel.
    void setChoosers(std::function<QString()> folder, std::function<QString()> shim) {
        chooseFolder_ = std::move(folder);
        chooseShim_ = std::move(shim);
    }

private:
    QComboBox* type_ = nullptr;
    QLineEdit* path_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* shim_ = nullptr;
    QComboBox* compression_ = nullptr;
    model::ExportVolume original_;
    std::function<QString()> chooseFolder_;
    std::function<QString()> chooseShim_;
};

} // namespace lumenis::ui
