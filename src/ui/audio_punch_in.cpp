#include "lumenis/ui/audio_punch_in.h"

#include "lumenis/media/audio_input.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/sequence.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

// The Audio Punch-In tool (audio_punch_in.h; Editing Guide p.746-753; docs/spec/punch-in.md). A take
// runs from the first Play In/Out or Record to Stop: the input records all the while (from the
// preroll on, so handles come with it), and each punch-in notes the record frame and the frame of
// the recording heard there. Stop has MainWindow write the recording as one master clip and edits
// the punch-ins into the sequence through Timeline::punchIn. Play itself is the Composer's; looping
// starts it again from the range's start.

namespace lumenis::ui {

namespace {

QToolButton* button(const QString& text, const QString& name, const QString& tip, QWidget* parent, bool checkable = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setMinimumWidth(28);
    return b;
}

QString monitoringText(AudioPunchInTool::Monitoring m) {
    switch (m) {
    case AudioPunchInTool::Monitoring::Automatic: return QObject::tr("Automatic");
    case AudioPunchInTool::Monitoring::On: return QObject::tr("On");
    case AudioPunchInTool::Monitoring::Off: return QObject::tr("Off");
    case AudioPunchInTool::Monitoring::Manual: return QObject::tr("Manual");
    }
    return {};
}

} // namespace

AudioPunchInTool::AudioPunchInTool(Timeline& timeline, QWidget* parent)
    : QWidget(parent), timeline_(timeline), input_(std::make_unique<media::AudioInput>()) {
    setObjectName(QStringLiteral("audioPunchIn"));
    setFocusPolicy(Qt::StrongFocus);
    inputStarter_ = [](media::AudioInput& in, const QString& device, int channels) { return in.start(device, channels); };

    // The transport (1-4 and 5, 6, 9 in the picture on p.748).
    auto* transport = new QHBoxLayout;
    transport->setSpacing(3);
    play_ = button(QStringLiteral("▶"), QStringLiteral("playInOut"), tr("Play In/Out (V)"), this, true);
    record_ = button(QStringLiteral("●"), QStringLiteral("record"), tr("Record (B)"), this, true);
    stop_ = button(QStringLiteral("■"), QStringLiteral("stop"), tr("Stop"), this);
    goToIn_ = button(QStringLiteral("⇤"), QStringLiteral("goToIn"), tr("Go to Mark In"), this);
    for (QToolButton* b : {play_, record_, stop_, goToIn_}) transport->addWidget(b);
    transport->addStretch(1);
    auto* tools = new QHBoxLayout;
    tools->setSpacing(3);
    cancel_ = button(QStringLiteral("✕"), QStringLiteral("cancel"), tr("Cancel"), this);
    audioTool_ = button(QStringLiteral("▮▮"), QStringLiteral("audioTool"), tr("Audio Tool"), this);
    monitor_ = button(QString(), QStringLiteral("monitorMode"), tr("Monitor Mode Input"), this);
    monitor_->setPopupMode(QToolButton::InstantPopup);
    auto* monitorMenu = new QMenu(monitor_);
    for (Monitoring m : {Monitoring::Automatic, Monitoring::On, Monitoring::Off, Monitoring::Manual}) {
        monitorMenu->addAction(monitoringText(m), this, [this, m] { setMonitoring(m); });
    }
    monitor_->setMenu(monitorMenu);
    tools->addWidget(cancel_);
    tools->addWidget(audioTool_);
    tools->addStretch(1);
    tools->addWidget(monitor_);
    connect(play_, &QToolButton::clicked, this, [this] { playInOut(); });
    connect(record_, &QToolButton::clicked, this, [this] { record(); });
    connect(stop_, &QToolButton::clicked, this, [this] { stopTake(); });
    connect(goToIn_, &QToolButton::clicked, this, [this] { goToIn(); });
    connect(cancel_, &QToolButton::clicked, this, [this] { cancelTake(); });
    connect(audioTool_, &QToolButton::clicked, this, &AudioPunchInTool::audioToolRequested);

    // 7, 10-12.
    stopAtEnd_ = new QCheckBox(tr("Stop at end or at mark out"), this);
    stopAtEnd_->setObjectName(QStringLiteral("stopAtEnd"));
    stopAtEnd_->setChecked(true);
    auto seconds = [this](const QString& name) {
        auto* box = new QSpinBox(this);
        box->setObjectName(name);
        box->setRange(0, 60);
        return box;
    };
    preroll_ = seconds(QStringLiteral("preroll"));
    postroll_ = seconds(QStringLiteral("postroll"));
    handles_ = seconds(QStringLiteral("handles"));
    inputSource_ = new QComboBox(this);
    inputSource_->setObjectName(QStringLiteral("inputSource"));
    auto* form = new QFormLayout;
    auto* rolls = new QHBoxLayout;
    rolls->addWidget(preroll_);
    rolls->addWidget(postroll_);
    form->addRow(tr("Pre/Post (sec):"), rolls);
    form->addRow(tr("Handles (sec):"), handles_);
    form->addRow(tr("Input Source:"), inputSource_);

    // 8 and 13: CH1-CH4 and their Timeline Track menus.
    auto* grid = new QGridLayout;
    for (int i = 0; i < kChannels; ++i) {
        QToolButton* ch = button(tr("CH %1").arg(i + 1), QStringLiteral("channel%1").arg(i + 1), tr("Input Channel"), this, true);
        ch->setMinimumWidth(80);
        channels_[i] = ch;
        auto* track = new QComboBox(this);
        track->setObjectName(QStringLiteral("track%1").arg(i + 1));
        tracks_[i] = track;
        grid->addWidget(ch, i, 0);
        grid->addWidget(track, i, 1);
        connect(ch, &QToolButton::toggled, track, &QWidget::setEnabled);
        track->setEnabled(false);
        // Alt+click: another input channel (p.748).
        connect(ch, &QToolButton::clicked, this, [this, i, ch](bool) {
            if (!(QApplication::keyboardModifiers() & Qt::AltModifier)) return;
            ch->toggle(); // the click was for the menu, not the selection
            QMenu menu(ch);
            const int count = std::max(1, inputSource_->currentData().toInt());
            for (int c = 0; c < count; ++c) {
                menu.addAction(tr("Channel %1").arg(c + 1), this, [this, i, c] { setHardwareChannel(i, c); });
            }
            menu.exec(ch->mapToGlobal(QPoint(0, ch->height())));
        });
    }
    channels_[0]->setChecked(true);
    // 14, 15.
    drive_ = new QComboBox(this);
    drive_->setObjectName(QStringLiteral("targetDrive"));
    drive_->setToolTip(tr("Target Drive"));
    bin_ = new QComboBox(this);
    bin_->setObjectName(QStringLiteral("targetBin"));
    bin_->setToolTip(tr("Target Bin"));
    connect(inputSource_, &QComboBox::currentIndexChanged, this, &AudioPunchInTool::dimChannels);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("punchInMessage"));
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(transport);
    layout->addLayout(tools);
    layout->addWidget(stopAtEnd_);
    layout->addLayout(form);
    layout->addLayout(grid);
    layout->addWidget(drive_);
    layout->addWidget(bin_);
    layout->addWidget(message_);
    layout->addStretch(1);

    setInputs({{tr("Default"), 2}});
    setMonitoring(Monitoring::Automatic);
    Composer& composer = timeline_.composer();
    connect(&composer, &Composer::changed, this, &AudioPunchInTool::tick);
    connect(&composer, &Composer::playStopped, this, &AudioPunchInTool::playStopped);
    connect(&timeline_, &Timeline::changed, this, [this] {
        if (!isActive()) refresh();
    });
    refresh();
}

AudioPunchInTool::~AudioPunchInTool() {
    input_->stop();
}

void AudioPunchInTool::setTargets(const QList<QPair<QString, QString>>& drives, const QString& drive,
                                  const QList<QPair<QString, QString>>& bins, const QString& bin) {
    drive_->clear();
    for (const auto& [name, root] : drives) drive_->addItem(name, root);
    if (const int i = drive_->findData(drive); i >= 0) drive_->setCurrentIndex(i);
    bin_->clear();
    for (const auto& [id, name] : bins) bin_->addItem(name, id);
    if (const int i = bin_->findData(bin); i >= 0) bin_->setCurrentIndex(i);
}

// Each input's channel count is kept as the item's data; channel buttons past it are dimmed.
void AudioPunchInTool::setInputs(const QList<QPair<QString, int>>& inputs) {
    inputSource_->clear();
    for (const auto& [name, channels] : inputs) inputSource_->addItem(name, channels);
    dimChannels();
}

void AudioPunchInTool::dimChannels() {
    const int count = std::max(inputSource_->currentData().toInt(), 1);
    for (int i = 0; i < kChannels; ++i) {
        const bool ok = i < count;
        channels_[i]->setEnabled(ok && !isActive());
        if (!ok) channels_[i]->setChecked(false);
    }
}

void AudioPunchInTool::setHardwareChannel(int i, int channel) {
    if (i < 0 || i >= kChannels) return;
    hardware_[i] = std::max(channel, 0);
    channels_[i]->setText(hardware_[i] == i ? tr("CH %1").arg(i + 1) : tr("CH %1 (%2)").arg(i + 1).arg(hardware_[i] + 1));
}

void AudioPunchInTool::setMonitoring(Monitoring mode) {
    monitoring_ = mode;
    monitor_->setText(monitoringText(mode));
    if (isActive()) emit activeChanged(true); // MainWindow looks at monitorsInput() again
}

// "Monitoring is not disabled if latency <100ms" (p.749); Manual leaves it to the hardware.
bool AudioPunchInTool::monitorsInput() const {
    if (monitoring_ == Monitoring::On) return true;
    if (monitoring_ == Monitoring::Automatic) return input_->latency() < 0.1;
    return false;
}

QStringList AudioPunchInTool::targets() const {
    QStringList out;
    for (int i = 0; i < kChannels; ++i) {
        if (!channels_[i]->isChecked()) continue;
        out << tracks_[i]->currentData().toString();
    }
    return out;
}

QStringList AudioPunchInTool::silencedTracks() const {
    if (!isActive()) return {};
    QStringList out;
    for (const QString& t : takeTargets_) {
        if (!t.isEmpty()) out << t;
    }
    return out;
}

// New Track, then the sequence's mono audio tracks that are not locked (p.749).
void AudioPunchInTool::refresh() {
    QList<std::pair<QString, QString>> items{{tr("New Track"), QString()}};
    if (const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        rec && rec->item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Audio && t.format == model::AudioFormat::Mono && !t.locked) {
                items.append({t.name(), t.name()});
            }
        }
    }
    for (QComboBox* box : tracks_) {
        const QString was = box->currentData().toString();
        box->clear();
        for (const auto& [label, value] : items) box->addItem(label, value);
        if (const int i = box->findData(was); i >= 0) box->setCurrentIndex(i);
    }
}

int AudioPunchInTool::fps() const {
    return std::max(timeline_.format().nominalFps, 1);
}

std::int64_t AudioPunchInTool::position() const {
    return timeline_.composer().state(MonitorId::Record).position();
}

std::int64_t AudioPunchInTool::lastFrame() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec) return 0;
    return std::max<std::int64_t>(model::Sequence::fromItem(rec->item).duration() - 1, 0);
}

// The recording's length so far, in the sequence's frames.
std::int64_t AudioPunchInTool::recordedFrames() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    const Rational rate = rec ? rec->item.editRate : Rational(25);
    return static_cast<std::int64_t>(std::floor(media::samplesToFrames(input_->recorded(), rate)));
}

void AudioPunchInTool::setState(State state) {
    const bool was = isActive();
    state_ = state;
    play_->setChecked(state != State::Idle);
    record_->setChecked(state == State::Recording || state == State::Armed);
    dimChannels();
    for (int i = 0; i < kChannels; ++i) tracks_[i]->setDisabled(isActive() || !channels_[i]->isChecked());
    inputSource_->setDisabled(isActive());
    if (was != isActive()) emit activeChanged(isActive());
}

void AudioPunchInTool::fail(const QString& text) {
    message_->setText(text);
}

// Starts a take: the input, then play from `from` backed up by the preroll ("you can hear the audio
// during preroll", p.749).
bool AudioPunchInTool::begin(std::int64_t from, bool loop) {
    message_->clear();
    Composer& composer = timeline_.composer();
    const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Load a sequence into the Record monitor first."));
        return false;
    }
    takeTargets_ = targets();
    if (takeTargets_.isEmpty()) {
        fail(tr("Select an input channel."));
        return false;
    }
    int open = 0;
    for (int i = 0; i < kChannels; ++i) {
        if (channels_[i]->isChecked()) open = std::max(open, hardware_[i] + 1);
    }
    if (!inputStarter_(*input_, inputSource_->currentIndex() == 0 ? QString() : inputSource_->currentText(), open)) {
        fail(tr("The input could not be opened."));
        return false;
    }
    const model::MonitorState& s = composer.state(MonitorId::Record);
    loop_ = loop;
    completed_ = false;
    finishAt_.reset();
    punches_.clear();
    startedAt_ = s.position();
    loopStart_ = from;
    loopEnd_ = s.out() ? *s.out() : lastFrame();
    setState(State::Playing);
    composer.setActiveMonitor(MonitorId::Record);
    composer.moveTo(MonitorId::Record, std::max<std::int64_t>(from - preroll_->value() * fps(), 0));
    composer.play(1);
    return true;
}

void AudioPunchInTool::playInOut() {
    if (isActive()) return;
    const model::MonitorState& s = timeline_.composer().state(MonitorId::Record);
    // "Set only an Out point. The position indicator is used as the In point" (p.749).
    begin(s.in().value_or(s.position()), true);
}

void AudioPunchInTool::record() {
    switch (state_) {
    case State::Idle: {
        const model::MonitorState& s = timeline_.composer().state(MonitorId::Record);
        // "If you set an In point and Out point, recording automatically starts at the In point"
        // (p.748); else where the position is.
        const std::int64_t at = s.in().value_or(s.position());
        if (!begin(at, false)) return;
        armAt_ = at;
        if (position() >= armAt_) {
            punchInAt(position());
        } else {
            setState(State::Armed);
        }
        return;
    }
    case State::Armed:
        // "The Record button takes precedence over preroll" (p.749).
        punchInAt(position());
        return;
    case State::Playing:
        punchInAt(position());
        return;
    case State::Recording:
        punchOutAt(position());
        return;
    case State::Finishing:
        return;
    }
}

// The frame of the recording heard at `at`: what has come in, less the frames played since `at`.
void AudioPunchInTool::punchInAt(std::int64_t at) {
    punchStart_ = at;
    punchSource_ = std::max<std::int64_t>(recordedFrames() - (position() - at), 0);
    setState(State::Recording);
}

void AudioPunchInTool::punchOutAt(std::int64_t end) {
    if (end > punchStart_) punches_.append({punchStart_, end - punchStart_, punchSource_});
    setState(State::Playing);
}

void AudioPunchInTool::stopTake() {
    if (isActive()) finish(true);
}

void AudioPunchInTool::cancelTake() {
    if (isActive()) finish(false);
}

void AudioPunchInTool::goToIn() {
    const model::MonitorState& s = timeline_.composer().state(MonitorId::Record);
    if (s.isEmpty()) return;
    timeline_.composer().moveTo(MonitorId::Record, s.in().value_or(startedAt_.value_or(0)));
}

// Ends the take: play stops, the input stops, and with `keep` the recording becomes a clip and the
// punch-ins are edited in. A punch-in still open ends at the position.
void AudioPunchInTool::finish(bool keep) {
    if (state_ == State::Recording) punchOutAt(position());
    finishing_ = true;
    timeline_.composer().stop();
    finishing_ = false;
    if (keep) {
        // The input runs behind play by its latency (and a busy machine's delays): before it stops,
        // let it bring in the sound through the end of the last punch-in, so the take is not cut
        // short. Bounded, in case the device has stopped delivering.
        std::int64_t needed = 0;
        for (const Timeline::PunchIn& p : punches_) needed = std::max(needed, p.source + p.length);
        QElapsedTimer waited;
        waited.start();
        const auto limit = static_cast<qint64>(1000 + input_->latency() * 1000);
        while (recordedFrames() < needed && waited.elapsed() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    input_->stop();
    const QList<Timeline::PunchIn> punches = punches_;
    const QStringList targets = takeTargets_;
    setState(State::Idle);
    if (!keep || punches.isEmpty()) return;
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || !clipMaker_) return;
    Take take;
    take.sequenceName = rec->item.name;
    take.editRate = rec->item.editRate;
    take.drive = drive_->currentData().toString();
    take.binId = bin_->currentData().toString();
    for (int i = 0; i < kChannels; ++i) {
        if (channels_[i]->isChecked()) take.channels.push_back(input_->samples(hardware_[i]));
    }
    QString error;
    const auto clip = clipMaker_(take, &error);
    if (!clip) {
        fail(error.isEmpty() ? tr("The recording could not be saved.") : error);
        return;
    }
    // Punch-ins past the end of what was recorded are cut to it.
    const std::int64_t length = clip->durationFrames;
    QList<Timeline::PunchIn> fitted;
    for (Timeline::PunchIn p : punches) {
        p.length = std::min(p.length, length - p.source);
        if (p.length > 0) fitted << p;
    }
    if (!timeline_.punchIn(take.binId, *clip, targets, fitted)) fail(timeline_.lastMessage());
    refresh();
}

// On each Composer change while a take runs: start an armed recording at its frame, end one at the OUT
// (or the end) when Stop at End is on, loop, and end the take after the postroll and handles.
void AudioPunchInTool::tick() {
    if (!isActive() || finishing_) return;
    const std::int64_t at = position();
    if (state_ == State::Armed && at >= armAt_) punchInAt(armAt_);
    if (state_ == State::Recording && stopAtEnd_->isChecked() && at > loopEnd_) {
        punchOutAt(loopEnd_ + 1);
        completed_ = true;
        // "Instructs Media Composer to record audio at the beginning and end of the clip" (p.749):
        // play and the input go on for the postroll or the handles, whichever is longer.
        finishAt_ = loopEnd_ + std::max(postroll_->value(), handles_->value()) * fps();
        setState(State::Finishing);
    }
    if (finishAt_ && at >= *finishAt_) {
        finish(true);
        return;
    }
    // "The play loops from the In point to the Out point but stops looping once recording
    // completes" (p.748).
    if (loop_ && !completed_ && state_ == State::Playing && at >= loopEnd_ && loopEnd_ < lastFrame()) {
        timeline_.composer().moveTo(MonitorId::Record, loopStart_);
        timeline_.composer().play(1);
    }
}

// Play stopped by itself (the end of the sequence) or by the space bar (p.751).
void AudioPunchInTool::playStopped() {
    if (!isActive() || finishing_) return;
    if (loop_ && !completed_ && state_ == State::Playing && position() >= loopEnd_) {
        Composer& composer = timeline_.composer();
        composer.moveTo(MonitorId::Record, loopStart_);
        composer.play(1);
        return;
    }
    // Recording ends where play stopped: past the last frame when it ran to the end.
    if (state_ == State::Recording) punchOutAt(position() >= lastFrame() ? lastFrame() + 1 : position());
    finish(true);
}

void AudioPunchInTool::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_V) {
            playInOut();
            return;
        }
        if (event->key() == Qt::Key_B) {
            record();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui
