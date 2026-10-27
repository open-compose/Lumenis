// The Multi-Mix Tool (multi_mix_dialog.h). The matrix is remade from the mix on every change; it is
// small (at most 24 outputs by the sequence's audio tracks).
#include "lumenis/ui/multi_mix_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEnterEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumenis::ui {

using media::MixdownFormat;
using media::MixOutput;

namespace {

constexpr const char* kTrackProperty = "multiMixTrack";
constexpr const char* kOutputProperty = "multiMixOutput";

// The Edit Mix dialog (p.734): the output's name, its mix (format) and the Master Fader.
std::optional<MixOutput> askEditMix(const MixOutput& output, QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("editMix"));
    dialog.setWindowTitle(QObject::tr("Edit Mix"));
    auto* name = new QLineEdit(output.name, &dialog);
    auto* format = new QComboBox(&dialog);
    for (MixdownFormat f : {MixdownFormat::Mono, MixdownFormat::Stereo, MixdownFormat::DualMono, MixdownFormat::Surround51,
                            MixdownFormat::Surround71}) {
        format->addItem(media::mixdownFormatName(f), static_cast<int>(f));
    }
    format->setCurrentIndex(format->findData(static_cast<int>(output.format)));
    auto* master = new QCheckBox(QObject::tr("Include Master Fader"), &dialog);
    master->setChecked(output.masterFader);
    auto* form = new QFormLayout;
    form->addRow(QObject::tr("Name:"), name);
    form->addRow(QObject::tr("Mix:"), format);
    form->addRow(master);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    MixOutput out = output;
    out.name = name->text().trimmed();
    out.format = static_cast<MixdownFormat>(format->currentData().toInt());
    out.masterFader = master->isChecked();
    return out;
}

} // namespace

MultiMixDialog::MultiMixDialog(const Setup& setup, QWidget* parent) : QDialog(parent), setup_(setup) {
    setObjectName(QStringLiteral("multiMixTool"));
    setWindowTitle(tr("Multi-Mix Tool"));

    setting_ = new QComboBox(this);
    setting_->setObjectName(QStringLiteral("selectSetting"));
    setting_->addItem(tr("Untitled"), QString());
    for (auto it = setup_.settings.cbegin(); it != setup_.settings.cend(); ++it) setting_->addItem(it.key(), it.key());
    auto* top = new QFormLayout;
    top->addRow(tr("Select Setting:"), setting_);

    bins_ = new QComboBox(this);
    bins_->setObjectName(QStringLiteral("bin"));
    for (const auto& [id, name] : setup_.bins) bins_->addItem(name, id);
    bins_->setCurrentIndex(std::max(0, bins_->findData(setup_.activeBin)));
    drives_ = new QComboBox(this);
    drives_->setObjectName(QStringLiteral("drive"));
    for (const auto& [name, root] : setup_.drives) drives_->addItem(name, root);
    drives_->setCurrentIndex(std::max(0, drives_->findData(setup_.drive)));
    useMarks_ = new QCheckBox(tr("Use Marks"), this);
    newSequence_ = new QCheckBox(tr("Create New Sequence, named:"), this);
    sequenceName_ = new QLineEdit(this);
    sequenceName_->setObjectName(QStringLiteral("sequenceName"));
    exclude_ = new QCheckBox(tr("Exclude Video and Data Tracks"), this);
    auto* where = new QGroupBox(this);
    auto* whereGrid = new QGridLayout(where);
    whereGrid->addWidget(new QLabel(tr("Bin:"), where), 0, 0);
    whereGrid->addWidget(bins_, 0, 1);
    whereGrid->addWidget(new QLabel(tr("Drive:"), where), 1, 0);
    whereGrid->addWidget(drives_, 1, 1);
    whereGrid->addWidget(useMarks_, 0, 2);
    whereGrid->addWidget(newSequence_, 1, 2);
    whereGrid->addWidget(sequenceName_, 2, 2);
    whereGrid->addWidget(exclude_, 3, 2);
    // Processing Options (picture on p.733): the mixes are made here; Distributed Processing needs
    // render services we do not have.
    auto* processing = new QGroupBox(tr("Processing Options:"), this);
    auto* p = new QVBoxLayout(processing);
    auto* application = new QRadioButton(tr("Application"), processing);
    application->setChecked(true);
    auto* distributed = new QRadioButton(tr("Distributed Processing"), processing);
    distributed->setEnabled(false);
    p->addWidget(application);
    p->addWidget(distributed);

    matrix_ = new QWidget;
    matrix_->setObjectName(QStringLiteral("mixMatrix"));
    grid_ = new QGridLayout(matrix_);
    grid_->setSpacing(2);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(matrix_);
    problem_ = new QLabel(this);
    problem_->setObjectName(QStringLiteral("mixProblem"));

    auto* saveButton = new QPushButton(tr("Save Setting"), this);
    auto* saveAsButton = new QPushButton(tr("Save Setting As..."), this);
    mixButton_ = new QPushButton(tr("Mix"), this);
    mixButton_->setObjectName(QStringLiteral("mix"));
    auto* close = new QPushButton(tr("Close"), this);
    connect(saveButton, &QPushButton::clicked, this, [this] { saveSetting(); });
    connect(saveAsButton, &QPushButton::clicked, this, [this] { saveSettingAs(); });
    connect(mixButton_, &QPushButton::clicked, this, [this] {
        takeOptions();
        if (media::multiMixProblem(mix_).isEmpty()) accept();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(saveButton);
    bottom->addWidget(saveAsButton);
    bottom->addStretch(1);
    bottom->addWidget(mixButton_);
    bottom->addWidget(close);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(where);
    layout->addWidget(processing);
    layout->addWidget(scroll, 1);
    layout->addWidget(problem_);
    layout->addLayout(bottom);

    connect(setting_, &QComboBox::currentIndexChanged, this, [this] {
        const QString name = setting_->currentData().toString();
        if (setup_.settings.contains(name)) setMix(setup_.settings.value(name));
    });
    for (QCheckBox* box : {useMarks_, newSequence_, exclude_}) {
        connect(box, &QCheckBox::toggled, this, [this] { takeOptions(); });
    }
    connect(sequenceName_, &QLineEdit::textEdited, this, [this] { takeOptions(); });

    const int chosen = setting_->findData(setup_.setting);
    if (chosen > 0) {
        setting_->setCurrentIndex(chosen); // loads it
    } else {
        // A new mix: one stereo output of every track (ours).
        media::MultiMix m;
        MixOutput out;
        out.inputs = setup_.tracks;
        m.outputs << out;
        setMix(m);
    }
    resize(760, 560);
}

void MultiMixDialog::later(const std::function<void()>& f) {
    QMetaObject::invokeMethod(this, f, Qt::QueuedConnection);
}

void MultiMixDialog::setMix(const media::MultiMix& mix) {
    mix_ = mix;
    const QSignalBlocker b1(useMarks_), b2(newSequence_), b3(exclude_);
    useMarks_->setChecked(mix_.useMarks);
    newSequence_->setChecked(mix_.newSequence);
    exclude_->setChecked(mix_.excludeVideoAndData);
    sequenceName_->setText(mix_.sequenceName);
    rebuild();
}

void MultiMixDialog::takeOptions() {
    mix_.useMarks = useMarks_->isChecked();
    mix_.newSequence = newSequence_->isChecked();
    mix_.excludeVideoAndData = exclude_->isChecked();
    mix_.sequenceName = sequenceName_->text().trimmed();
    sequenceName_->setEnabled(mix_.newSequence);
    exclude_->setEnabled(mix_.newSequence);
}

QString MultiMixDialog::bin() const {
    return bins_->currentData().toString();
}

QString MultiMixDialog::drive() const {
    return drives_->currentData().toString();
}

QString MultiMixDialog::settingName() const {
    return setting_->currentData().toString();
}

QToolButton* MultiMixDialog::inputButton(int output, const QString& track) const {
    return inputs_.value({output, track});
}

void MultiMixDialog::addOutput() {
    MixOutput out;
    out.format = MixdownFormat::Stereo;
    mix_.outputs << out;
    rebuild();
}

void MultiMixDialog::removeOutput(int output) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    mix_.outputs.removeAt(output);
    rebuild();
}

void MultiMixDialog::clearMix(int output) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    mix_.outputs[output].inputs.clear();
    rebuild();
}

void MultiMixDialog::setFormat(int output, MixdownFormat format) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    mix_.outputs[output].format = format;
    rebuild();
}

void MultiMixDialog::toggleInput(int output, const QString& track) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    QStringList& in = mix_.outputs[output].inputs;
    if (in.contains(track)) {
        in.removeAll(track);
    } else {
        // In the rows' order.
        QStringList sorted;
        for (const QString& t : std::as_const(setup_.tracks)) {
            if (t == track || in.contains(t)) sorted << t;
        }
        in = sorted;
    }
    rebuild();
}

void MultiMixDialog::toggleColumn(int output) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    QStringList& in = mix_.outputs[output].inputs;
    in = in.size() == setup_.tracks.size() ? QStringList{} : setup_.tracks;
    rebuild();
}

void MultiMixDialog::setMasterFader(int output, bool on) {
    if (output < 0 || output >= mix_.outputs.size()) return;
    mix_.outputs[output].masterFader = on;
    rebuild();
}

bool MultiMixDialog::editMix(int output) {
    if (output < 0 || output >= mix_.outputs.size()) return false;
    const auto edited = editMixAsk ? editMixAsk(mix_.outputs.at(output)) : askEditMix(mix_.outputs.at(output), this);
    if (!edited) return false;
    MixOutput out = *edited;
    out.inputs = mix_.outputs.at(output).inputs;
    mix_.outputs[output] = out;
    rebuild();
    return true;
}

bool MultiMixDialog::saveSetting() {
    const QString name = settingName();
    if (name.isEmpty()) return saveSettingAs();
    takeOptions();
    setup_.settings.insert(name, mix_);
    if (save) save(name, mix_);
    return true;
}

bool MultiMixDialog::saveSettingAs() {
    std::optional<QString> name;
    if (nameAsk) {
        name = nameAsk();
    } else {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Save Setting As"), tr("Setting name:"), QLineEdit::Normal, {}, &ok);
        if (ok) name = text.trimmed();
    }
    if (!name || name->isEmpty()) return false;
    takeOptions();
    setup_.settings.insert(*name, mix_);
    if (setting_->findData(*name) < 0) {
        const QSignalBlocker block(setting_);
        setting_->addItem(*name, *name);
    }
    {
        const QSignalBlocker block(setting_);
        setting_->setCurrentIndex(setting_->findData(*name));
    }
    if (save) save(*name, mix_);
    return true;
}

void MultiMixDialog::fillOutputMenu(QMenu* menu, int output) {
    menu->clear();
    menu->addAction(tr("Edit Mix"), this, [this, output] { editMix(output); });
    menu->addSeparator();
    const std::pair<MixdownFormat, QString> formats[] = {{MixdownFormat::Mono, tr("Set to Mono")},
                                                         {MixdownFormat::Stereo, tr("Set to Stereo")},
                                                         {MixdownFormat::DualMono, tr("Set to Dual Mono")},
                                                         {MixdownFormat::Surround51, tr("Set to Surround 5.1")},
                                                         {MixdownFormat::Surround71, tr("Set to Surround 7.1")}};
    for (const auto& [format, text] : formats) {
        QAction* a = menu->addAction(text, this, [this, output, f = format] { setFormat(output, f); });
        a->setCheckable(true);
        a->setChecked(output < mix_.outputs.size() && mix_.outputs.at(output).format == format);
        // "Only odd-numbered tracks can be set to Dual Mono" (p.734).
        if (format == MixdownFormat::DualMono) a->setEnabled(media::multiMixTrackNumber(mix_, output) % 2 == 1);
    }
    menu->addSeparator();
    menu->addAction(tr("Add Output"), this, [this] { addOutput(); });
    menu->addAction(tr("Remove Output"), this, [this, output] { removeOutput(output); });
    menu->addAction(tr("Clear Mix"), this, [this, output] { clearMix(output); });
}

bool MultiMixDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Enter && (QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
        const QVariant output = watched->property(kOutputProperty);
        const QVariant track = watched->property(kTrackProperty);
        if (output.isValid() && track.isValid()) {
            // After this event: rebuild() deletes the button.
            later([this, o = output.toInt(), t = track.toString()] { toggleInput(o, t); });
        }
    }
    return QDialog::eventFilter(watched, event);
}

// Row 0: the output buttons (their number, and the format under it); row 1: the Master Fader
// buttons; then a row per track with its name and a check button per output; the + button last.
void MultiMixDialog::rebuild() {
    while (QLayoutItem* item = grid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    outputButtons_.clear();
    masterButtons_.clear();
    inputs_.clear();
    for (int o = 0; o < mix_.outputs.size(); ++o) {
        const MixOutput& out = mix_.outputs.at(o);
        auto* head = new QToolButton(matrix_);
        head->setObjectName(QStringLiteral("output:%1").arg(o));
        const int number = media::multiMixTrackNumber(mix_, o);
        const QString numbers = out.format == MixdownFormat::DualMono ? QStringLiteral("%1-%2").arg(number).arg(number + 1)
                                                                      : QString::number(number);
        head->setText(QStringLiteral("%1\n%2").arg(numbers, media::mixdownFormatName(out.format)));
        head->setToolTip(out.name.isEmpty() ? tr("Output %1").arg(number) : out.name);
        head->setMinimumWidth(56);
        // The menus belong to the dialog and every edit from a button waits for the button's own
        // signal to finish: rebuild() deletes the buttons.
        connect(head, &QToolButton::clicked, this, [this, o, head] {
            const bool alt = QApplication::keyboardModifiers() & Qt::AltModifier;
            const QPoint at = head->mapToGlobal(QPoint(0, head->height()));
            later([this, o, alt, at] {
                if (alt) {
                    toggleColumn(o);
                    return;
                }
                QMenu menu(this);
                fillOutputMenu(&menu, o);
                menu.exec(at);
            });
        });
        head->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(head, &QToolButton::customContextMenuRequested, this, [this, o, head](const QPoint& pos) {
            const QPoint at = head->mapToGlobal(pos);
            later([this, o, at] {
                QMenu menu(this);
                fillOutputMenu(&menu, o);
                menu.exec(at);
            });
        });
        grid_->addWidget(head, 0, o + 1);
        outputButtons_ << head;
        auto* master = new QToolButton(matrix_);
        master->setObjectName(QStringLiteral("master:%1").arg(o));
        master->setText(QStringLiteral("M"));
        master->setToolTip(tr("Master Fader"));
        master->setCheckable(true);
        master->setChecked(out.masterFader);
        connect(master, &QToolButton::toggled, this, [this, o](bool on) { later([this, o, on] { setMasterFader(o, on); }); });
        grid_->addWidget(master, 1, o + 1, Qt::AlignHCenter);
        masterButtons_ << master;
    }
    for (int r = 0; r < setup_.tracks.size(); ++r) {
        const QString& track = setup_.tracks.at(r);
        grid_->addWidget(new QLabel(track, matrix_), r + 2, 0);
        for (int o = 0; o < mix_.outputs.size(); ++o) {
            auto* cell = new QToolButton(matrix_);
            cell->setObjectName(QStringLiteral("input:%1:%2").arg(o).arg(track));
            cell->setCheckable(true);
            cell->setChecked(mix_.outputs.at(o).inputs.contains(track));
            cell->setFixedSize(22, 18);
            cell->setProperty(kOutputProperty, o);
            cell->setProperty(kTrackProperty, track);
            cell->installEventFilter(this);
            connect(cell, &QToolButton::clicked, this, [this, o, track] { later([this, o, track] { toggleInput(o, track); }); });
            grid_->addWidget(cell, r + 2, o + 1, Qt::AlignHCenter);
            inputs_.insert({o, track}, cell);
        }
    }
    auto* plus = new QToolButton(matrix_);
    plus->setObjectName(QStringLiteral("addOutput"));
    plus->setText(QStringLiteral("+"));
    plus->setToolTip(tr("Add Output"));
    plus->setEnabled(media::multiMixTracks(mix_) < media::kMultiMixTracks);
    connect(plus, &QToolButton::clicked, this, [this] { later([this] { addOutput(); }); });
    grid_->addWidget(plus, static_cast<int>(setup_.tracks.size()) + 2, std::max<int>(1, static_cast<int>(mix_.outputs.size())));
    grid_->setRowStretch(static_cast<int>(setup_.tracks.size()) + 3, 1);
    grid_->setColumnStretch(static_cast<int>(mix_.outputs.size()) + 1, 1);
    const QString problem = media::multiMixProblem(mix_);
    problem_->setText(problem);
    mixButton_->setEnabled(setup_.canMix && problem.isEmpty());
}

} // namespace lumenis::ui
