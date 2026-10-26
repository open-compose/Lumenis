#include "lumenis/ui/mixdown_dialog.h"

#include "lumenis/media/managed.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// The Audio Mixdown dialog box (mixdown_dialog.h). Formats are ids in a button group: the
// media::MixdownFormat values.
namespace lumenis::ui {

AudioMixdownDialog::AudioMixdownDialog(const Setup& setup, QWidget* parent) : QDialog(parent), setup_(setup) {
    setObjectName(QStringLiteral("audioMixdownDialog"));
    setWindowTitle(tr("Audio Mixdown"));

    // "The Source Tracks area lists the source audio tracks and the Range area lists the start and
    // end timecodes" (p.732).
    auto* source = new QGroupBox(tr("Source Tracks"), this);
    auto* sourceLabel = new QLabel(setup.sourceTracks.join(QStringLiteral(", ")), source);
    sourceLabel->setObjectName(QStringLiteral("mixdownSourceTracks"));
    (new QVBoxLayout(source))->addWidget(sourceLabel);
    auto* range = new QGroupBox(tr("Range"), this);
    auto* rangeLabel = new QLabel(tr("%1 to %2").arg(setup.rangeStart, setup.rangeEnd), range);
    rangeLabel->setObjectName(QStringLiteral("mixdownRange"));
    (new QVBoxLayout(range))->addWidget(rangeLabel);

    // "Select Mono, Dual Mono, Stereo, 5.1, or 7.1 and select the target track" (p.732).
    auto* output = new QGroupBox(tr("Mixdown"), this);
    auto* outputLayout = new QVBoxLayout(output);
    auto* buttons = new QHBoxLayout;
    formats_ = new QButtonGroup(this);
    const QList<QPair<QString, int>> kinds{{tr("Mono"), static_cast<int>(media::MixdownFormat::Mono)},
                                           {tr("Dual Mono"), static_cast<int>(media::MixdownFormat::DualMono)},
                                           {tr("Stereo"), static_cast<int>(media::MixdownFormat::Stereo)},
                                           {tr("5.1"), static_cast<int>(media::MixdownFormat::Surround51)},
                                           {tr("7.1"), static_cast<int>(media::MixdownFormat::Surround71)}};
    for (const auto& [text, id] : kinds) {
        auto* b = new QRadioButton(text, output);
        b->setObjectName(QStringLiteral("mixdownFormat:") + text);
        formats_->addButton(b, id);
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    outputLayout->addLayout(buttons);
    target_ = new QComboBox(output);
    target_->setObjectName(QStringLiteral("mixdownTarget"));
    auto* targetRow = new QFormLayout;
    targetRow->addRow(tr("Target Track"), target_);
    outputLayout->addLayout(targetRow);
    master_ = new QCheckBox(tr("Include Master Fader"), output);
    master_->setObjectName(QStringLiteral("mixdownIncludeMaster"));
    outputLayout->addWidget(master_);
    connect(formats_, &QButtonGroup::idClicked, this, [this] { fillTargets(); });

    // "Select a bin and a drive" (p.732).
    auto* where = new QFormLayout;
    bins_ = new QComboBox(this);
    bins_->setObjectName(QStringLiteral("mixdownBin"));
    for (const auto& [id, name] : setup.bins) bins_->addItem(name, id);
    if (const int i = bins_->findData(setup.activeBin); i >= 0) bins_->setCurrentIndex(i);
    drives_ = new QComboBox(this);
    drives_->setObjectName(QStringLiteral("mixdownDrive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drives_->addItem(d.name, d.root);
    if (const int i = drives_->findData(setup.drive); i >= 0) drives_->setCurrentIndex(i);
    where->addRow(tr("Bin"), bins_);
    where->addRow(tr("Drive"), drives_);
    premix_ = new QCheckBox(tr("Save Premix Sequence"), this);
    premix_->setObjectName(QStringLiteral("mixdownSavePremix"));

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    box->button(QDialogButtonBox::Ok)->setEnabled(!setup.sourceTracks.isEmpty() && bins_->count() > 0 && drives_->count() > 0);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(source);
    layout->addWidget(range);
    layout->addWidget(output);
    layout->addLayout(where);
    layout->addWidget(premix_);
    layout->addWidget(box);

    setFormat(media::MixdownFormat::DualMono);
}

media::MixdownFormat AudioMixdownDialog::format() const {
    const int id = formats_->checkedId();
    return id < 0 ? media::MixdownFormat::DualMono : static_cast<media::MixdownFormat>(id);
}

void AudioMixdownDialog::setFormat(media::MixdownFormat format) {
    if (QAbstractButton* b = formats_->button(static_cast<int>(format))) b->setChecked(true);
    fillTargets();
}

// "A mono mixdown goes to the next available mono track ... If there are no appropriate tracks in
// the Timeline, the mixdown operation creates them" (p.732): the first empty track (pair) is chosen,
// else New Track(s). "A stereo or surround sound mixdown goes to the next available stereo or surround
// sound track": an empty track of the format. Ours: Dual Mono's pairs are two empty mono tracks one
// after the other.
void AudioMixdownDialog::fillTargets() {
    target_->clear();
    const model::AudioFormat want = media::mixdownTrackFormat(format());
    QStringList empty;
    for (const QString& t : setup_.emptyTracks) {
        if (setup_.formats.value(t, model::AudioFormat::Mono) == want) empty << t;
    }
    if (want != model::AudioFormat::Mono) {
        for (const QString& t : empty) target_->addItem(t, QStringList{t});
        target_->addItem(tr("New Track"), QStringList{});
    } else if (format() == media::MixdownFormat::Mono) {
        for (const QString& t : empty) target_->addItem(t, QStringList{t});
        target_->addItem(tr("New Track"), QStringList{});
    } else {
        for (qsizetype i = 0; i + 1 < empty.size(); ++i) {
            target_->addItem(tr("%1 and %2").arg(empty.at(i), empty.at(i + 1)), QStringList{empty.at(i), empty.at(i + 1)});
        }
        target_->addItem(tr("New Tracks"), QStringList{});
    }
    target_->setCurrentIndex(0);
}

QStringList AudioMixdownDialog::targetTracks() const {
    return target_->currentData().toStringList();
}

bool AudioMixdownDialog::includeMasterFader() const {
    return master_->isChecked();
}

void AudioMixdownDialog::setIncludeMasterFader(bool on) {
    master_->setChecked(on);
}

QString AudioMixdownDialog::bin() const {
    return bins_->currentData().toString();
}

QString AudioMixdownDialog::drive() const {
    return drives_->currentData().toString();
}

bool AudioMixdownDialog::savePremixSequence() const {
    return premix_->isChecked();
}

void AudioMixdownDialog::setSavePremixSequence(bool on) {
    premix_->setChecked(on);
}

} // namespace lumenis::ui
