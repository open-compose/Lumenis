// The Update, Merge, Flatten and Remove Color Correction dialogs (FX Guide p.700-706). See
// lumenis/ui/correction_manage_dialog.h.

#include "lumenis/ui/correction_manage_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

// The boxes' names in the dialogs (p.701, p.706).
QString boxText(model::CorrectionRelationship r) {
    switch (r) {
    case model::CorrectionRelationship::SourceTape: return CorrectionManageDialog::tr("Tape");
    case model::CorrectionRelationship::SourceClipName: return CorrectionManageDialog::tr("Clip Name");
    case model::CorrectionRelationship::MasterClip: return CorrectionManageDialog::tr("Master Clip");
    case model::CorrectionRelationship::SubClip: return CorrectionManageDialog::tr("Sub Clip");
    case model::CorrectionRelationship::ProgramTrack: return CorrectionManageDialog::tr("Track");
    case model::CorrectionRelationship::SourceSegment:
    case model::CorrectionRelationship::ProgramSegment: return CorrectionManageDialog::tr("Segment");
    }
    return {};
}

} // namespace

QString CorrectionManageDialog::title(Kind kind) {
    switch (kind) {
    case Kind::Update: return tr("Update Color Correction");
    case Kind::Merge: return tr("Merge Color Correction");
    case Kind::Flatten: return tr("Flatten Color Correction");
    case Kind::Remove: return tr("Remove Color Correction");
    }
    return {};
}

CorrectionManageDialog::CorrectionManageDialog(Kind kind, const QMap<model::CorrectionRelationship, int>& counts, QWidget* parent)
    : QDialog(parent), kind_(kind) {
    setObjectName(QStringLiteral("correctionManageDialog"));
    setWindowTitle(title(kind));
    const bool showCounts = kind == Kind::Flatten || kind == Kind::Remove;
    auto* outer = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    // A column: its heading (with the total), a line in the side's colour (ours: green Source, blue Program, as the
    // pictures), and a box per relationship.
    const auto column = [&](const QString& heading, const QList<model::CorrectionRelationship>& rs, const QColor& colour) {
        auto* grid = new QGridLayout;
        int total = 0;
        for (model::CorrectionRelationship r : rs) total += counts.value(r);
        grid->addWidget(new QLabel(heading, this), 0, 0);
        if (showCounts) grid->addWidget(new QLabel(QString::number(total), this), 0, 1, Qt::AlignRight);
        auto* line = new QFrame(this);
        line->setFixedHeight(2);
        line->setAutoFillBackground(true);
        QPalette p = line->palette();
        p.setColor(QPalette::Window, colour);
        line->setPalette(p);
        grid->addWidget(line, 1, 0, 1, 2);
        int row = 2;
        for (model::CorrectionRelationship r : rs) {
            auto* box = new QCheckBox(boxText(r), this);
            box->setObjectName(QStringLiteral("relationship:") + model::relationshipKey(r));
            const bool segment = model::isSegmentOnly(r);
            if (segment && (kind == Kind::Update || kind == Kind::Flatten)) box->setEnabled(false);
            box->setChecked(!segment);
            boxes_.insert(r, box);
            grid->addWidget(box, row, 0);
            if (showCounts) grid->addWidget(new QLabel(QString::number(counts.value(r)), this), row, 1, Qt::AlignRight);
            ++row;
        }
        grid->setRowStretch(row, 1);
        columns->addLayout(grid, 1);
    };
    column(tr("Source"), model::sourceRelationships(), QColor(0x3c, 0xc8, 0x3c));
    columns->addSpacing(24);
    column(tr("Program"), model::programRelationships(), QColor(0x3c, 0x64, 0xe6));
    outer->addLayout(columns);
    if (kind == Kind::Merge) {
        // "Click the 'In case of conflict' menu, and select an option" (p.703).
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("In case of conflict:"), this));
        conflict_ = new QComboBox(this);
        conflict_->setObjectName(QStringLiteral("conflict"));
        conflict_->addItem(tr("Prefer Source Sequence"), static_cast<int>(model::MergeConflict::PreferSource));
        conflict_->addItem(tr("Prefer Record Sequence"), static_cast<int>(model::MergeConflict::PreferRecord));
        conflict_->addItem(tr("Prefer Latest"), static_cast<int>(model::MergeConflict::PreferLatest));
        conflict_->setCurrentIndex(2); // "This is the default setting"
        row->addWidget(conflict_, 1);
        outer->addLayout(row);
    }
    auto* bottom = new QHBoxLayout;
    newSequence_ = new QCheckBox(tr("Create New Sequence"), this);
    newSequence_->setObjectName(QStringLiteral("createNewSequence"));
    bottom->addWidget(newSequence_);
    bottom->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottom->addWidget(buttons);
    outer->addLayout(bottom);
}

QList<model::CorrectionRelationship> CorrectionManageDialog::relationships() const {
    QList<model::CorrectionRelationship> out;
    for (auto it = boxes_.cbegin(); it != boxes_.cend(); ++it) {
        if (it.value()->isEnabled() && it.value()->isChecked()) out << it.key();
    }
    return out;
}

void CorrectionManageDialog::setRelationship(model::CorrectionRelationship r, bool on) {
    if (QCheckBox* b = boxes_.value(r)) b->setChecked(on);
}

bool CorrectionManageDialog::createNewSequence() const {
    return newSequence_->isChecked();
}

void CorrectionManageDialog::setCreateNewSequence(bool on) {
    newSequence_->setChecked(on);
}

model::MergeConflict CorrectionManageDialog::conflict() const {
    return conflict_ ? static_cast<model::MergeConflict>(conflict_->currentData().toInt()) : model::MergeConflict::PreferLatest;
}

void CorrectionManageDialog::setConflict(model::MergeConflict conflict) {
    if (!conflict_) return;
    conflict_->setCurrentIndex(conflict_->findData(static_cast<int>(conflict)));
}

} // namespace lumenis::ui
