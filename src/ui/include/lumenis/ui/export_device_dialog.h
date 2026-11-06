// The XDCAM and P2 Export Settings dialog boxes (Phase 7.2b; docs/spec/export-device.md; Editing Guide p.883-884,
// p.901, p.1187): Use Marks, Use Selected Tracks, the target disk or card, the Video Format and the audio Bit Depth.
// The dialog only collects the choices; MainWindow::exportToDevice runs the export and the device's deliver().
#pragma once

#include "lumenis/ext/export_device.h"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace lumenis::ui {

class ExportDeviceDialog : public QDialog {
    Q_OBJECT
public:
    // `kind` "xdcam" or "p2"; `devices` the providers of that kind. With no disk or card connected the options are
    // dimmed ("If you do not connect to a P2 device or camera, the options are not available", p.901) and OK too.
    ExportDeviceDialog(const QString& kind, std::vector<std::shared_ptr<ext::ExportDeviceProvider>> devices, bool useMarks,
                       bool useSelectedTracks, QWidget* parent = nullptr);

    bool hasTargets() const { return !entries_.empty(); }
    // The chosen device and target; null and an empty target without any.
    std::shared_ptr<ext::ExportDeviceProvider> device() const;
    ext::ExportDeviceTarget target() const;
    QString videoFormat() const; // a media::movieCompressions() id
    int bitDepth() const;
    bool useMarks() const;
    bool useSelectedTracks() const;

    // For tests: pick the target by its name, the format by its id.
    bool chooseTarget(const QString& name);
    bool chooseFormat(const QString& id);

private:
    void fillFormats();

    struct Entry {
        std::shared_ptr<ext::ExportDeviceProvider> device;
        ext::ExportDeviceTarget target;
    };
    std::vector<Entry> entries_;
    QCheckBox* useMarks_ = nullptr;
    QCheckBox* useSelectedTracks_ = nullptr;
    QComboBox* targets_ = nullptr;
    QComboBox* formats_ = nullptr;
    QComboBox* depths_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace lumenis::ui
