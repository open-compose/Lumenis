// The Group Clips dialog (Editing Guide p.1124; lumenis/ui/group_clips_dialog.h): the options in the
// guide's order, the Auxiliary TC menu beside its option.
#include "lumenis/ui/group_clips_dialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

GroupClipsDialog::GroupClipsDialog(bool filmProject, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Group Clips"));
    setObjectName(QStringLiteral("groupClipsDialog"));
    auto* layout = new QVBoxLayout(this);
    options_ = new QButtonGroup(this);
    struct Option {
        model::GroupSync sync;
        const char* text;
        const char* name;
    };
    // The options of p.1124's table.
    const Option list[] = {
        {model::GroupSync::FilmTc, QT_TR_NOOP("Film TC/Sound TC"), "filmTc"},
        {model::GroupSync::Inpoints, QT_TR_NOOP("Inpoints"), "inpoints"},
        {model::GroupSync::Outpoints, QT_TR_NOOP("Outpoints"), "outpoints"},
        {model::GroupSync::SourceTimecode, QT_TR_NOOP("Source Timecode"), "sourceTimecode"},
        {model::GroupSync::AuxTimecode, QT_TR_NOOP("Auxiliary TC"), "auxTimecode"},
        {model::GroupSync::Ink, QT_TR_NOOP("INK Number"), "ink"},
        {model::GroupSync::AuxInk, QT_TR_NOOP("Auxiliary Ink"), "auxInk"},
        {model::GroupSync::Waveform, QT_TR_NOOP("Waveform Analysis"), "waveform"},
    };
    for (const Option& o : list) {
        auto* b = new QRadioButton(tr(o.text), this);
        b->setObjectName(QString::fromLatin1(o.name));
        options_->addButton(b, static_cast<int>(o.sync));
        const bool ink = o.sync == model::GroupSync::Ink || o.sync == model::GroupSync::AuxInk;
        // "This option appears only with 24p and 25p projects" (the ink options).
        if (ink && !filmProject) b->setVisible(false);
        // Not built yet: film timecode and ink numbers (no film projects) and waveform analysis (8.1a2).
        if (ink || o.sync == model::GroupSync::FilmTc || o.sync == model::GroupSync::Waveform) b->setEnabled(false);
        if (o.sync == model::GroupSync::AuxTimecode) {
            // "Select an Auxiliary TC, 1 through 5, from the menu."
            auto* row = new QHBoxLayout;
            row->addWidget(b);
            aux_ = new QComboBox(this);
            aux_->setObjectName(QStringLiteral("auxTcMenu"));
            for (int i = 1; i <= 5; ++i) aux_->addItem(tr("Auxiliary TC%1").arg(i), i);
            row->addWidget(aux_);
            row->addStretch();
            layout->addLayout(row);
        } else {
            layout->addWidget(b);
        }
    }
    // Ours: Inpoints first chosen, the guide's step 1 being marking IN points.
    setSync(model::GroupSync::Inpoints);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

model::GroupSync GroupClipsDialog::sync() const {
    return static_cast<model::GroupSync>(options_->checkedId());
}

int GroupClipsDialog::auxTc() const {
    return aux_->currentData().toInt();
}

void GroupClipsDialog::setSync(model::GroupSync sync, int auxTc) {
    if (QAbstractButton* b = options_->button(static_cast<int>(sync))) b->setChecked(true);
    aux_->setCurrentIndex(std::clamp(auxTc, 1, 5) - 1);
}

QRadioButton* GroupClipsDialog::button(model::GroupSync sync) const {
    return qobject_cast<QRadioButton*>(options_->button(static_cast<int>(sync)));
}

} // namespace lumenis::ui
