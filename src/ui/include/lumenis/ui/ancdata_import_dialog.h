// The AncData Import dialog box (Phase 7.4b; docs/spec/closed-captions.md; Editing Guide p.232): importing a Scenarist
// Closed Caption file asks for its starting timecode and whether it is drop frame, "automatically populated with the
// applicable information from the selected .scc file". MainWindow::importScc opens it.
#pragma once

#include <QDialog>

#include <cstdint>
#include <optional>

class QCheckBox;
class QLineEdit;

namespace lumenis::ui {

class AncDataImportDialog : public QDialog {
    Q_OBJECT

public:
    // `startFrame` in the file's timecode (30 frames a second).
    AncDataImportDialog(const QString& fileName, std::int64_t startFrame, bool dropFrame, QWidget* parent = nullptr);

    void setStartTimecode(const QString& text);
    void setDropFrame(bool on);
    bool dropFrame() const;
    // The starting timecode as a frame count at 30 frames a second; nothing when it does not read as timecode.
    std::optional<std::int64_t> startFrame() const;

private:
    QLineEdit* start_ = nullptr;
    QCheckBox* dropFrame_ = nullptr;
};

} // namespace lumenis::ui
