// The Sequence Report dialog box (sequence_report_dialog.h; Editing Guide p.369-371).
#include "lumenis/ui/sequence_report_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

SequenceReportDialog::SequenceReportDialog(const QString& bin, const QString& name, const QString& startTimecode, bool several,
                                           QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Sequence Report"));
    setObjectName(QStringLiteral("sequenceReportDialog"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Bin: %1").arg(bin), this));
    auto* form = new QFormLayout;
    name_ = new QLineEdit(name, this);
    name_->setObjectName(QStringLiteral("reportName"));
    start_ = new QLineEdit(startTimecode, this);
    start_->setObjectName(QStringLiteral("reportStartingTc"));
    form->addRow(tr("Name:"), name_);
    auto* startRow = new QHBoxLayout;
    startRow->addWidget(start_);
    auto* apply = new QPushButton(tr("Apply Changes"), this);
    apply->setObjectName(QStringLiteral("reportApplyChanges"));
    startRow->addWidget(apply);
    form->addRow(tr("Starting TC:"), startRow);
    layout->addLayout(form);
    if (several) {
        name_->setEnabled(false);
        start_->setEnabled(false);
        apply->setEnabled(false);
    }
    connect(apply, &QPushButton::clicked, this, [this] { emit applyChangesRequested(name_->text(), start_->text()); });

    auto* scope = new QGroupBox(this);
    auto* scopeLayout = new QVBoxLayout(scope);
    enabledTracks_ = new QCheckBox(tr("Enabled Tracks Only"), scope);
    useMarks_ = new QCheckBox(tr("Use Marks"), scope);
    scopeLayout->addWidget(enabledTracks_);
    scopeLayout->addWidget(useMarks_);
    layout->addWidget(scope);

    auto* summary = new QGroupBox(tr("Summary Info Options"), this);
    auto* s = new QVBoxLayout(summary);
    auto add = [&](const QString& text, const char* object, bool indented) {
        auto* box = new QCheckBox(text, summary);
        box->setObjectName(QLatin1String(object));
        if (indented) {
            auto* row = new QHBoxLayout;
            row->addSpacing(20);
            row->addWidget(box);
            s->addLayout(row);
        } else {
            s->addWidget(box);
        }
        return box;
    };
    effectSummary_ = add(tr("Create Effect Summary"), "reportEffectSummary", false);
    effectLocations_ = add(tr("Create Effect Location List"), "reportEffectLocations", false);
    skipNonRenderable_ = add(tr("Skip Non-Renderable Effects"), "reportSkipNonRenderable", true);
    skipRelationship_ = add(tr("Skip Relationship-Only Color Correction"), "reportSkipRelationship", true);
    nestedOnly_ = add(tr("Show Nested Effects Only"), "reportNestedOnly", true);
    missingOnly_ = add(tr("Show Missing Effects Only"), "reportMissingOnly", true);
    clipSummary_ = add(tr("Create Clip Summary"), "reportClipSummary", false);
    sourceSummary_ = add(tr("Create Source Summary"), "reportSourceSummary", false);
    offlineOnly_ = add(tr("Offline Only"), "reportOfflineOnly", true);
    skipGroup_ = add(tr("Skip Non-Selected Clips In Group Clips"), "reportSkipGroup", true);
    showUid_ = add(tr("Show Globally Unique Identifier (UID)"), "reportShowUid", true);
    layout->addWidget(summary);
    // The picture on p.369 starts with the four summaries on.
    setOptions(media::SequenceReportOptions{});
    for (QCheckBox* parentBox : {effectSummary_, effectLocations_, clipSummary_, sourceSummary_}) {
        connect(parentBox, &QCheckBox::toggled, this, &SequenceReportDialog::updateSuboptions);
    }

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* generate = buttons->addButton(tr("Generate Report"), QDialogButtonBox::AcceptRole);
    generate->setObjectName(QStringLiteral("generateReport"));
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateSuboptions();
}

void SequenceReportDialog::updateSuboptions() {
    // The Effect Location List's options and the summaries' (p.370), offered while their summary is.
    const bool effects = effectSummary_->isChecked() || effectLocations_->isChecked();
    for (QCheckBox* b : {skipNonRenderable_, skipRelationship_, nestedOnly_, missingOnly_}) b->setEnabled(effects);
    const bool clips = clipSummary_->isChecked() || sourceSummary_->isChecked();
    for (QCheckBox* b : {offlineOnly_, skipGroup_, showUid_}) b->setEnabled(clips);
}

media::SequenceReportOptions SequenceReportDialog::options() const {
    media::SequenceReportOptions o;
    o.enabledTracksOnly = enabledTracks_->isChecked();
    o.useMarks = useMarks_->isChecked();
    o.effectSummary = effectSummary_->isChecked();
    o.effectLocations = effectLocations_->isChecked();
    o.skipNonRenderable = skipNonRenderable_->isChecked();
    o.skipRelationshipOnly = skipRelationship_->isChecked();
    o.nestedOnly = nestedOnly_->isChecked();
    o.missingOnly = missingOnly_->isChecked();
    o.clipSummary = clipSummary_->isChecked();
    o.sourceSummary = sourceSummary_->isChecked();
    o.offlineOnly = offlineOnly_->isChecked();
    o.skipNonSelectedGroupClips = skipGroup_->isChecked();
    o.showUid = showUid_->isChecked();
    return o;
}

void SequenceReportDialog::setOptions(const media::SequenceReportOptions& o) {
    enabledTracks_->setChecked(o.enabledTracksOnly);
    useMarks_->setChecked(o.useMarks);
    effectSummary_->setChecked(o.effectSummary);
    effectLocations_->setChecked(o.effectLocations);
    skipNonRenderable_->setChecked(o.skipNonRenderable);
    skipRelationship_->setChecked(o.skipRelationshipOnly);
    nestedOnly_->setChecked(o.nestedOnly);
    missingOnly_->setChecked(o.missingOnly);
    clipSummary_->setChecked(o.clipSummary);
    sourceSummary_->setChecked(o.sourceSummary);
    offlineOnly_->setChecked(o.offlineOnly);
    skipGroup_->setChecked(o.skipNonSelectedGroupClips);
    showUid_->setChecked(o.showUid);
}

QString SequenceReportDialog::name() const { return name_->text(); }
QString SequenceReportDialog::startTimecode() const { return start_->text(); }

} // namespace lumenis::ui
