#pragma once

// The dialogs of the Color Correction tool's Update, Merge, Flatten and Remove Correction commands (FX Guide ch.21,
// p.700-706; lumenis/model/correction_manage.h, 6.10d4): a Source column (Tape, Clip Name, Master Clip, Sub Clip,
// Segment) and a Program column (Track, Segment) of relationship check boxes, Create New Sequence, and OK and Cancel
// (p.701, p.706). Merge adds the "In case of conflict" menu (p.703). Flatten and Remove give each relationship's number
// of segments ("The dialog box indicates the number of segments ... for each relationship category", p.705), and each
// column's total.
//
// Which boxes start checked: Update "By default, all available relationships are selected", its Segment boxes dimmed
// (p.701); Merge's Segment boxes "are not selected by default" (p.703); Flatten selects "all available relationships",
// the segment ones being segment-only already (dimmed, ours); Remove as its picture shows (p.706: everything but the
// Segment boxes; the text says "all"). Create New Sequence starts off, as in the pictures.

#include "lumenis/model/correction_manage.h"

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;

namespace lumenis::ui {

class CorrectionManageDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind { Update, Merge, Flatten, Remove };
    // `counts` are model::correctionCounts of the sequence (shown for Flatten and Remove).
    CorrectionManageDialog(Kind kind, const QMap<model::CorrectionRelationship, int>& counts, QWidget* parent = nullptr);

    Kind kind() const { return kind_; }
    // "Update Color Correction", "Merge Color Correction", ... (p.701-706).
    static QString title(Kind kind);
    // The checked relationships.
    QList<model::CorrectionRelationship> relationships() const;
    void setRelationship(model::CorrectionRelationship r, bool on);
    QCheckBox* relationshipBox(model::CorrectionRelationship r) const { return boxes_.value(r); }
    bool createNewSequence() const;
    void setCreateNewSequence(bool on);
    model::MergeConflict conflict() const;
    void setConflict(model::MergeConflict conflict);

private:
    Kind kind_;
    QMap<model::CorrectionRelationship, QCheckBox*> boxes_;
    QCheckBox* newSequence_ = nullptr;
    QComboBox* conflict_ = nullptr;
};

} // namespace lumenis::ui
