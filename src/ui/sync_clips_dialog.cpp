// The Sync Selection dialog (Editing Guide p.497-498; lumenis/ui/sync_clips_dialog.h): the options of the guide's table
// in its order, each track range beside its check box.
#include "lumenis/ui/sync_clips_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {

// "Specify which audio tracks you want to keep from the Start and End range": A1 to A<n>, up to the 99 tracks a clip
// may have (sequence.h kMaxTracks).
QSpinBox* trackBox(const QString& name, int value, QWidget* parent) {
    auto* box = new QSpinBox(parent);
    box->setObjectName(name);
    box->setRange(1, model::kMaxTracks);
    box->setPrefix(QStringLiteral("A"));
    box->setValue(value);
    return box;
}

} // namespace

SyncClipsDialog::SyncClipsDialog(bool filmProject, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Sync Selection"));
    setObjectName(QStringLiteral("syncClipsDialog"));
    auto* layout = new QVBoxLayout(this);
    methods_ = new QButtonGroup(this);
    struct Option {
        model::GroupSync sync;
        const char* text;
        const char* name;
    };
    const Option list[] = {
        {model::GroupSync::FilmTc, QT_TR_NOOP("Film TC/Sound TC"), "filmTc"},
        {model::GroupSync::Inpoints, QT_TR_NOOP("Inpoints"), "inpoints"},
        {model::GroupSync::Outpoints, QT_TR_NOOP("Outpoints"), "outpoints"},
        {model::GroupSync::SourceTimecode, QT_TR_NOOP("Source Timecode"), "sourceTimecode"},
        {model::GroupSync::AuxTimecode, QT_TR_NOOP("Auxiliary TC"), "auxTimecode"},
        {model::GroupSync::Waveform, QT_TR_NOOP("Waveform Analysis"), "waveform"},
    };
    for (const Option& o : list) {
        auto* b = new QRadioButton(tr(o.text), this);
        b->setObjectName(QString::fromLatin1(o.name));
        methods_->addButton(b, static_cast<int>(o.sync));
        // "This option appears dimmed if you are not in a 24p or 25p project."
        if (o.sync == model::GroupSync::FilmTc) b->setEnabled(filmProject);
        if (o.sync == model::GroupSync::AuxTimecode) {
            // "Select an Aux TC, 1 through 5, from the menu."
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
    auto range = [&](QCheckBox* box, QSpinBox* start, QSpinBox* end) {
        auto* row = new QHBoxLayout;
        row->addWidget(box);
        row->addStretch();
        row->addWidget(new QLabel(tr("Start"), this));
        row->addWidget(start);
        row->addWidget(new QLabel(tr("End"), this));
        row->addWidget(end);
        layout->addLayout(row);
        start->setEnabled(false);
        end->setEnabled(false);
        connect(box, &QCheckBox::toggled, start, &QWidget::setEnabled);
        connect(box, &QCheckBox::toggled, end, &QWidget::setEnabled);
    };
    keepVideoAudio_ = new QCheckBox(tr("Keep audio on clip with video"), this);
    keepVideoAudio_->setObjectName(QStringLiteral("keepVideoAudio"));
    keepStart_ = trackBox(QStringLiteral("keepStart"), 1, this);
    keepEnd_ = trackBox(QStringLiteral("keepEnd"), 2, this);
    range(keepVideoAudio_, keepStart_, keepEnd_);
    audioOnly_ = new QCheckBox(tr("Include audio from audio-only clips"), this);
    audioOnly_->setObjectName(QStringLiteral("audioOnlyTracks"));
    audioStart_ = trackBox(QStringLiteral("audioStart"), 1, this);
    audioEnd_ = trackBox(QStringLiteral("audioEnd"), 2, this);
    range(audioOnly_, audioStart_, audioEnd_);
    collapse_ = new QCheckBox(tr("Collapse Audio Tracks"), this);
    collapse_->setObjectName(QStringLiteral("collapseAudio"));
    layout->addWidget(collapse_);
    // Not built yet: a group clip in place of subclips when video tracks overlap (ROADMAP 9.3).
    group_ = new QCheckBox(tr("When video tracks overlap: Create Group Clip instead of Subclip"), this);
    group_->setObjectName(QStringLiteral("createGroupClip"));
    group_->setEnabled(false);
    layout->addWidget(group_);
    // "Source Timecode ... The default option."
    setOptions({});
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

model::SyncClipsOptions SyncClipsDialog::options() const {
    model::SyncClipsOptions o;
    o.sync.sync = static_cast<model::GroupSync>(methods_->checkedId());
    o.sync.auxTc = aux_->currentData().toInt();
    auto pair = [](const QSpinBox* a, const QSpinBox* b) {
        return std::pair<int, int>{std::min(a->value(), b->value()), std::max(a->value(), b->value())};
    };
    if (keepVideoAudio_->isChecked()) o.keepVideoAudio = pair(keepStart_, keepEnd_);
    if (audioOnly_->isChecked()) o.audioOnlyTracks = pair(audioStart_, audioEnd_);
    o.collapseAudio = collapse_->isChecked();
    return o;
}

void SyncClipsDialog::setOptions(const model::SyncClipsOptions& o) {
    if (QAbstractButton* b = methods_->button(static_cast<int>(o.sync.sync)); b && b->isEnabled()) b->setChecked(true);
    else if (QAbstractButton* tc = methods_->button(static_cast<int>(model::GroupSync::SourceTimecode))) tc->setChecked(true);
    aux_->setCurrentIndex(std::clamp(o.sync.auxTc, 1, 5) - 1);
    keepVideoAudio_->setChecked(o.keepVideoAudio.has_value());
    if (o.keepVideoAudio) {
        keepStart_->setValue(o.keepVideoAudio->first);
        keepEnd_->setValue(o.keepVideoAudio->second);
    }
    audioOnly_->setChecked(o.audioOnlyTracks.has_value());
    if (o.audioOnlyTracks) {
        audioStart_->setValue(o.audioOnlyTracks->first);
        audioEnd_->setValue(o.audioOnlyTracks->second);
    }
    collapse_->setChecked(o.collapseAudio);
}

QRadioButton* SyncClipsDialog::button(model::GroupSync sync) const {
    return qobject_cast<QRadioButton*>(methods_->button(static_cast<int>(sync)));
}

} // namespace lumenis::ui
