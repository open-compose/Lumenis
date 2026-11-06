// The Digital Cut tool (lumenis/ui/digital_cut_tool.h).
#include "lumenis/ui/digital_cut_tool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

// The Deck Selection menu's commands after the decks (p.927); their data marks them.
constexpr const char* kAdjustDeck = "adjust";
constexpr const char* kAutoConfigure = "autoconfigure";
constexpr const char* kCheckDecks = "check";

QPushButton* trackButton(const QString& name, const QString& objectName, QWidget* parent) {
    auto* b = new QPushButton(name, parent);
    b->setObjectName(objectName);
    b->setCheckable(true);
    b->setChecked(true);
    b->setFixedWidth(b->fontMetrics().horizontalAdvance(name) + 16);
    return b;
}

} // namespace

DigitalCutTool::DigitalCutTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("digitalCutTool"));
    auto* layout = new QVBoxLayout(this);

    // Output Mode and Bit Depth (p.932): ours has one output path, at the project's raster.
    auto* output = new QFormLayout;
    outputMode_ = new QComboBox(this);
    outputMode_->setObjectName(QStringLiteral("dcOutputMode"));
    outputMode_->addItem(tr("Full Quality"));
    bitDepth_ = new QComboBox(this);
    bitDepth_->setObjectName(QStringLiteral("dcBitDepth"));
    bitDepth_->addItem(tr("8-bit"), 8);
    bitDepth_->addItem(tr("10-bit"), 10);
    output->addRow(tr("Output Mode:"), outputMode_);
    output->addRow(tr("Bit Depth:"), bitDepth_);
    layout->addLayout(output);

    auto check = [this](const QString& text, const char* name, bool on) {
        auto* c = new QCheckBox(text, this);
        c->setObjectName(QString::fromLatin1(name));
        c->setChecked(on);
        return c;
    };
    entire_ = check(tr("Entire Sequence"), "dcEntireSequence", true);
    safe_ = check(tr("Digital Cut Safe Mode"), "dcSafeMode", true);
    stopOnDropped_ = check(tr("Stop on Dropped Frames"), "dcStopOnDropped", false);
    ltcPreroll_ = check(tr("LTC out during preroll"), "dcLtcPreroll", false);
    layout->addWidget(entire_);
    layout->addWidget(safe_);
    layout->addWidget(stopOnDropped_);
    auto* black = new QHBoxLayout;
    addBlack_ = check(tr("Add Black at Tail"), "dcAddBlack", false);
    blackDuration_ = new QLineEdit(QStringLiteral("00:00:00:00"), this);
    blackDuration_->setObjectName(QStringLiteral("dcBlackDuration"));
    black->addWidget(addBlack_);
    black->addWidget(blackDuration_);
    layout->addLayout(black);
    layout->addWidget(ltcPreroll_);

    // Deck Control: Remote or Local, the deck, where to start and the kind of edit (p.927-931).
    auto* deck = new QGroupBox(tr("Deck Control"), this);
    auto* deckLayout = new QFormLayout(deck);
    remote_ = new QRadioButton(tr("Remote"), deck);
    remote_->setObjectName(QStringLiteral("dcRemote"));
    remote_->setChecked(true);
    local_ = new QRadioButton(tr("Local"), deck);
    local_->setObjectName(QStringLiteral("dcLocal"));
    auto* modes = new QHBoxLayout;
    modes->addWidget(remote_);
    modes->addWidget(local_);
    deckLayout->addRow(modes);
    deckMenu_ = new QComboBox(deck);
    deckMenu_->setObjectName(QStringLiteral("dcDeckMenu"));
    deckLayout->addRow(tr("Deck:"), deckMenu_);
    editMenu_ = new QComboBox(deck);
    editMenu_->setObjectName(QStringLiteral("dcEditMenu"));
    editMenu_->addItem(tr("Insert Edit"), static_cast<int>(ext::DeckRecordMode::Insert));
    editMenu_->addItem(tr("Assemble Edit"), static_cast<int>(ext::DeckRecordMode::Assemble));
    editMenu_->addItem(tr("Crash Record"), static_cast<int>(ext::DeckRecordMode::Crash));
    startMenu_ = new QComboBox(deck);
    startMenu_->setObjectName(QStringLiteral("dcStartMenu"));
    auto* where = new QHBoxLayout;
    where->addWidget(startMenu_);
    where->addWidget(editMenu_);
    deckLayout->addRow(where);
    markIn_ = new QLineEdit(QStringLiteral("00:00:00:00"), deck);
    markIn_->setObjectName(QStringLiteral("dcMarkIn"));
    deckLayout->addRow(tr("Mark IN:"), markIn_);
    auto* prerollRow = new QHBoxLayout;
    customPreroll_ = new QCheckBox(tr("Custom Preroll"), deck);
    customPreroll_->setObjectName(QStringLiteral("dcCustomPreroll"));
    preroll_ = new QSpinBox(deck);
    preroll_->setObjectName(QStringLiteral("dcPrerollSeconds"));
    preroll_->setRange(0, 10);
    preroll_->setValue(5);
    preroll_->setSuffix(tr(" s"));
    prerollRow->addWidget(customPreroll_);
    prerollRow->addWidget(preroll_);
    deckLayout->addRow(prerollRow);
    auto* offsetRow = new QHBoxLayout;
    customOffset_ = new QCheckBox(tr("Custom Frame Offset"), deck);
    customOffset_->setObjectName(QStringLiteral("dcCustomOffset"));
    offset_ = new QSpinBox(deck);
    offset_->setObjectName(QStringLiteral("dcFrameOffset"));
    offset_->setRange(-5, 5);
    offsetRow->addWidget(customOffset_);
    offsetRow->addWidget(offset_);
    deckLayout->addRow(offsetRow);
    layout->addWidget(deck);

    // The Sequence Track buttons and the Enable Track buttons (p.930).
    auto* tracks = new QFormLayout;
    auto* sequence = new QWidget(this);
    sequenceRow_ = new QHBoxLayout(sequence);
    sequenceRow_->setContentsMargins(0, 0, 0, 0);
    sequenceRow_->setSpacing(2);
    tracks->addRow(tr("Sequence:"), sequence);
    auto* tape = new QWidget(this);
    auto* tapeRow = new QHBoxLayout(tape);
    tapeRow->setContentsMargins(0, 0, 0, 0);
    tapeRow->setSpacing(2);
    for (const QString& t : {QStringLiteral("V"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A4")}) {
        QPushButton* b = trackButton(t, QStringLiteral("dcTape_") + t, tape);
        if (t == QLatin1String("A3") || t == QLatin1String("A4")) b->setChecked(false);
        tapeButtons_ << b;
        tapeRow->addWidget(b);
    }
    tapeRow->addStretch(1);
    tracks->addRow(tr("Record to:"), tape);
    layout->addLayout(tracks);

    // Preview Digital Cut (yellow), Play Digital Cut and Halt Digital Cut (p.928-930).
    auto* buttons = new QHBoxLayout;
    preview_ = new QPushButton(tr("Preview Digital Cut"), this);
    preview_->setObjectName(QStringLiteral("dcPreview"));
    play_ = new QPushButton(tr("Play Digital Cut"), this);
    play_->setObjectName(QStringLiteral("dcPlay"));
    halt_ = new QPushButton(tr("Halt Digital Cut"), this);
    halt_->setObjectName(QStringLiteral("dcHalt"));
    buttons->addWidget(preview_);
    buttons->addWidget(play_);
    buttons->addWidget(halt_);
    layout->addLayout(buttons);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("dcStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addStretch(1);

    connect(preview_, &QPushButton::clicked, this, &DigitalCutTool::previewRequested);
    connect(play_, &QPushButton::clicked, this, &DigitalCutTool::playRequested);
    connect(halt_, &QPushButton::clicked, this, &DigitalCutTool::haltRequested);
    connect(remote_, &QRadioButton::toggled, this, [this] { updateEnabled(); });
    connect(editMenu_, &QComboBox::currentIndexChanged, this, [this] { fillStartMenu(); });
    connect(startMenu_, &QComboBox::currentIndexChanged, this, [this] { updateEnabled(); });
    connect(deckMenu_, &QComboBox::activated, this, [this](int index) {
        const QString command = deckMenu_->itemData(index, Qt::UserRole + 1).toString();
        if (command == QLatin1String(kCheckDecks)) {
            emit checkDecksRequested();
        } else if (!command.isEmpty()) {
            setStatus(tr("The built-in decks have no settings to adjust."));
        }
        if (!command.isEmpty() && !decks_.empty()) deckMenu_->setCurrentIndex(0);
    });
    fillStartMenu();
    setSequenceTracks({});
}

void DigitalCutTool::setDecks(std::vector<std::shared_ptr<ext::DeckProvider>> providers) {
    providers_ = std::move(providers);
    decks_.clear();
    deckMenu_->clear();
    for (const auto& p : providers_) {
        if (!p) continue;
        for (const ext::DeckInfo& d : p->decks()) decks_.push_back({p, d});
    }
    for (const DeckEntry& e : decks_) {
        deckMenu_->addItem(QString::fromStdString(e.info.name));
        // An offline deck in italics (p.928).
        if (!e.info.online) {
            QFont f = deckMenu_->font();
            f.setItalic(true);
            deckMenu_->setItemData(deckMenu_->count() - 1, f, Qt::FontRole);
        }
    }
    if (decks_.empty()) deckMenu_->addItem(tr("No Deck"));
    deckMenu_->insertSeparator(deckMenu_->count());
    const QList<QPair<QString, const char*>> commands{{tr("Adjust Deck"), kAdjustDeck},
                                                      {tr("Auto-configure"), kAutoConfigure},
                                                      {tr("Check Decks"), kCheckDecks}};
    for (const auto& [label, command] : commands) {
        deckMenu_->addItem(label);
        deckMenu_->setItemData(deckMenu_->count() - 1, QString::fromLatin1(command), Qt::UserRole + 1);
    }
    deckMenu_->setCurrentIndex(0);
    updateEnabled();
}

std::shared_ptr<ext::DeckProvider> DigitalCutTool::deckProvider() const {
    const int i = deckMenu_->currentIndex();
    return i >= 0 && i < static_cast<int>(decks_.size()) ? decks_[static_cast<std::size_t>(i)].provider : nullptr;
}

std::string DigitalCutTool::deckId() const {
    const int i = deckMenu_->currentIndex();
    return i >= 0 && i < static_cast<int>(decks_.size()) ? decks_[static_cast<std::size_t>(i)].info.id : std::string();
}

bool DigitalCutTool::chooseDeck(const QString& name) {
    for (std::size_t i = 0; i < decks_.size(); ++i) {
        if (QString::fromStdString(decks_[i].info.name) == name) {
            deckMenu_->setCurrentIndex(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

void DigitalCutTool::setSequenceTracks(const QStringList& tracks) {
    qDeleteAll(sequenceButtons_);
    sequenceButtons_.clear();
    while (QLayoutItem* item = sequenceRow_->takeAt(0)) delete item;
    for (const QString& t : tracks) {
        QPushButton* b = trackButton(t, QStringLiteral("dcTrack_") + t, sequenceRow_->parentWidget());
        sequenceButtons_ << b;
        sequenceRow_->addWidget(b);
    }
    sequenceRow_->addStretch(1);
    updateEnabled();
}

void DigitalCutTool::fillStartMenu() {
    // With Crash Record the left menu also offers Ignore Time: "To ignore the starting timecode and start recording at
    // the current location, select Ignore Time" (p.931).
    const auto keep = startMenu_->currentData();
    startMenu_->clear();
    startMenu_->addItem(tr("Sequence Time"), static_cast<int>(Start::SequenceTime));
    startMenu_->addItem(tr("Record Deck Time"), static_cast<int>(Start::RecordDeckTime));
    startMenu_->addItem(tr("Mark In Time"), static_cast<int>(Start::MarkInTime));
    if (editMenu_->currentData().toInt() == static_cast<int>(ext::DeckRecordMode::Crash))
        startMenu_->addItem(tr("Ignore Time"), static_cast<int>(Start::IgnoreTime));
    const int i = startMenu_->findData(keep);
    startMenu_->setCurrentIndex(i >= 0 ? i : 0);
    updateEnabled();
}

void DigitalCutTool::updateEnabled() {
    const bool remote = remote_->isChecked();
    const bool any = !sequenceButtons_.isEmpty();
    for (QWidget* w : {static_cast<QWidget*>(startMenu_), static_cast<QWidget*>(editMenu_), static_cast<QWidget*>(customPreroll_),
                       static_cast<QWidget*>(preroll_), static_cast<QWidget*>(customOffset_), static_cast<QWidget*>(offset_)})
        w->setEnabled(remote && !running_);
    markIn_->setEnabled(remote && !running_ && startMenu_->currentData().toInt() == static_cast<int>(Start::MarkInTime));
    // "Select the video and audio tracks to record to on the tape by using the Enable Track buttons" (Remote only).
    for (QPushButton* b : tapeButtons_) b->setEnabled(remote && !running_);
    preview_->setEnabled(any && !running_);
    play_->setEnabled(any && !running_ && !decks_.empty());
    halt_->setEnabled(running_);
}

DigitalCutTool::Options DigitalCutTool::options() const {
    Options o;
    o.entireSequence = entire_->isChecked();
    o.safeMode = safe_->isChecked();
    o.stopOnDroppedFrames = stopOnDropped_->isChecked();
    o.addBlack = addBlack_->isChecked();
    o.blackDuration = blackDuration_->text().trimmed();
    o.ltcPreroll = ltcPreroll_->isChecked();
    o.remote = remote_->isChecked();
    o.mode = static_cast<ext::DeckRecordMode>(editMenu_->currentData().toInt());
    o.start = static_cast<Start>(startMenu_->currentData().toInt());
    o.markIn = markIn_->text().trimmed();
    o.customPreroll = customPreroll_->isChecked();
    o.prerollSeconds = preroll_->value();
    o.customOffset = customOffset_->isChecked();
    o.frameOffset = offset_->value();
    o.outputMode = outputMode_->currentText();
    o.bitDepth = bitDepth_->currentData().toInt();
    for (QPushButton* b : sequenceButtons_) {
        if (b->isChecked()) o.sequenceTracks << b->text();
    }
    o.tapeVideo = tapeButtons_.first()->isChecked();
    o.tapeAudio.clear();
    for (int i = 1; i < tapeButtons_.size(); ++i) {
        if (tapeButtons_.at(i)->isChecked()) o.tapeAudio << i;
    }
    return o;
}

void DigitalCutTool::setOptions(const Options& o) {
    entire_->setChecked(o.entireSequence);
    safe_->setChecked(o.safeMode);
    stopOnDropped_->setChecked(o.stopOnDroppedFrames);
    addBlack_->setChecked(o.addBlack);
    blackDuration_->setText(o.blackDuration);
    ltcPreroll_->setChecked(o.ltcPreroll);
    (o.remote ? remote_ : local_)->setChecked(true);
    editMenu_->setCurrentIndex(editMenu_->findData(static_cast<int>(o.mode)));
    fillStartMenu();
    const int start = startMenu_->findData(static_cast<int>(o.start));
    if (start >= 0) startMenu_->setCurrentIndex(start);
    markIn_->setText(o.markIn);
    customPreroll_->setChecked(o.customPreroll);
    preroll_->setValue(o.prerollSeconds);
    customOffset_->setChecked(o.customOffset);
    offset_->setValue(o.frameOffset);
    for (QPushButton* b : sequenceButtons_) b->setChecked(o.sequenceTracks.contains(b->text()));
    tapeButtons_.first()->setChecked(o.tapeVideo);
    for (int i = 1; i < tapeButtons_.size(); ++i) tapeButtons_.at(i)->setChecked(o.tapeAudio.contains(i));
    updateEnabled();
}

void DigitalCutTool::setStatus(const QString& text) { status_->setText(text); }

void DigitalCutTool::setRunning(bool running) {
    running_ = running;
    updateEnabled();
}

} // namespace lumenis::ui
