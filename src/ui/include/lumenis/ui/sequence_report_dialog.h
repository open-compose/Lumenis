#pragma once

#include "lumenis/media/sequence_report.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;

// The Sequence Report dialog box (9.2c; docs/spec/sequence-report.md; Editing Guide p.369-371 and its picture on p.369):
// the bin, the sequence's Name and Starting TC with Apply Changes ("You can modify the sequence name and the starting
// timecode in the Sequence Report dialog box"), Enabled Tracks Only, Use Marks, the Summary Info Options and Generate
// Report. It collects the choices; MainWindow::sequenceReport() writes the report.
namespace lumenis::ui {

class SequenceReportDialog : public QDialog {
    Q_OBJECT

public:
    // `several`: more than one sequence is reported, so Name and Starting TC cannot be changed.
    SequenceReportDialog(const QString& bin, const QString& name, const QString& startTimecode, bool several,
                         QWidget* parent = nullptr);

    media::SequenceReportOptions options() const;
    void setOptions(const media::SequenceReportOptions& options);
    QString name() const;
    QString startTimecode() const;

signals:
    // Apply Changes with the Name and Starting TC as typed.
    void applyChangesRequested(const QString& name, const QString& startTimecode);

private:
    void updateSuboptions();

    QLineEdit* name_ = nullptr;
    QLineEdit* start_ = nullptr;
    QCheckBox* enabledTracks_ = nullptr;
    QCheckBox* useMarks_ = nullptr;
    QCheckBox* effectSummary_ = nullptr;
    QCheckBox* effectLocations_ = nullptr;
    QCheckBox* skipNonRenderable_ = nullptr;
    QCheckBox* skipRelationship_ = nullptr;
    QCheckBox* nestedOnly_ = nullptr;
    QCheckBox* missingOnly_ = nullptr;
    QCheckBox* clipSummary_ = nullptr;
    QCheckBox* sourceSummary_ = nullptr;
    QCheckBox* offlineOnly_ = nullptr;
    QCheckBox* skipGroup_ = nullptr;
    QCheckBox* showUid_ = nullptr;
};

} // namespace lumenis::ui
