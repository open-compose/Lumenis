#include "lumenis/ui/audio_tools.h"

#include "lumenis/model/sequence.h"
#include "lumenis/ui/timeline.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QDial>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace lumenis::ui {

// ---------------------------------------------------------------- fader

namespace fader {

namespace {

// The marks of the fader scale, top down (picture on p.693). The last
// step, -45 to -∞, is drawn to -90 dB.
constexpr double kMarks[] = {12, 9, 6, 3, 0, -3, -7, -11, -15, -30, -45, -90};
constexpr int kSteps = 11;
constexpr int kStep = kRange / kSteps;

} // namespace

int position(double db) {
    if (db <= kMarks[kSteps]) return 0;
    if (db >= kMarks[0]) return kRange;
    for (int i = 0; i < kSteps; ++i) {
        const double hi = kMarks[i];
        const double lo = kMarks[i + 1];
        if (db <= hi && db >= lo) {
            const double frac = (db - lo) / (hi - lo);
            return (kSteps - i - 1) * kStep + static_cast<int>(std::lround(frac * kStep));
        }
    }
    return 0;
}

double gain(int pos) {
    if (pos <= 0) return model::kGainOff;
    if (pos >= kRange) return kMarks[0];
    const int i = kSteps - 1 - pos / kStep; // the mark above
    const double hi = kMarks[i];
    const double lo = kMarks[i + 1];
    const double frac = static_cast<double>(pos % kStep) / kStep;
    return std::round((lo + frac * (hi - lo)) * 10) / 10;
}

QString text(double db) {
    if (db <= model::kGainOff) return QStringLiteral("-∞");
    return (db > 0 ? QStringLiteral("+") : QString()) + QString::number(db, 'f', 1);
}

std::optional<double> parse(const QString& t) {
    const QString s = t.trimmed();
    if (s == QStringLiteral("-∞") || s.compare(QStringLiteral("-inf"), Qt::CaseInsensitive) == 0) {
        return model::kGainOff;
    }
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok) return std::nullopt;
    return model::clampGain(v);
}

QString panText(int pan) {
    if (pan == 0) return QStringLiteral("MID");
    return (pan < 0 ? QStringLiteral("L") : QStringLiteral("R")) + QString::number(std::abs(pan));
}

std::optional<int> parsePan(const QString& t) {
    const QString s = t.trimmed().toUpper();
    if (s == QLatin1String("MID") || s == QLatin1String("0")) return 0;
    bool ok = false;
    int v = 0;
    if (s.startsWith(QLatin1Char('L')) || s.startsWith(QLatin1Char('R'))) {
        v = s.mid(1).toInt(&ok);
        if (s.startsWith(QLatin1Char('L'))) v = -v;
    } else {
        v = s.toInt(&ok);
    }
    if (!ok) return std::nullopt;
    return std::clamp(v, -model::kPanMax, model::kPanMax);
}

} // namespace fader

namespace {

QToolButton* smallButton(const QString& text, const QString& name, QWidget* parent, bool checkable = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setCheckable(checkable);
    b->setAutoRaise(false);
    b->setMinimumWidth(22);
    return b;
}

QToolButton* laterButton(const QString& text, const QString& name, const QString& tip, QWidget* parent) {
    QToolButton* b = smallButton(text, name, parent);
    b->setToolTip(tip + QStringLiteral(" (") + QObject::tr("not available yet") + QLatin1Char(')'));
    b->setEnabled(false);
    return b;
}

// The fader's scale (picture on p.693), in the text colour.
class FaderScale : public QWidget {
public:
    explicit FaderScale(QSlider* fader, QWidget* parent = nullptr) : QWidget(parent), fader_(fader) {
        setFixedWidth(24);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        static const char* const labels[] = {"12", "9", "6", "3", "0", "3", "7", "11", "15", "30", "45", "∞"};
        QPainter p(this);
        QFont f = font();
        f.setPixelSize(8);
        p.setFont(f);
        p.setPen(palette().color(QPalette::WindowText));
        // The groove spans the fader's height less its handle.
        const int margin = 8;
        const int top = fader_->y() - y() + margin;
        const int span = fader_->height() - 2 * margin;
        for (int i = 0; i <= 11; ++i) {
            const int yy = top + span * i / 11;
            p.drawText(QRect(0, yy - 6, width() - 2, 12), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8(labels[i]));
        }
    }

private:
    QSlider* fader_;
};

} // namespace

// ---------------------------------------------------------------- MixerStrip

MixerStrip::MixerStrip(const QString& track, bool master, QWidget* parent) : QWidget(parent), track_(track) {
    setObjectName(QStringLiteral("strip:") + track);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(3);

    // Group/gang button (p.693, 1; p.704), pan (7), insert, S and M (6).
    gang_ = smallButton(QStringLiteral("∞"), QStringLiteral("gang"), this);
    gang_->setToolTip(tr("Gang"));
    gang_->setContextMenuPolicy(Qt::CustomContextMenu);
    pan_ = new QDial(this);
    pan_->setObjectName(QStringLiteral("pan"));
    pan_->setRange(-model::kPanMax, model::kPanMax);
    pan_->setFixedSize(34, 34);
    pan_->setNotchesVisible(false);
    panBox_ = new QLineEdit(fader::panText(0), this);
    panBox_->setObjectName(QStringLiteral("panValue"));
    panBox_->setAlignment(Qt::AlignCenter);
    panBox_->setFixedWidth(44);
    panBox_->installEventFilter(this);
    auto* insert = laterButton(QStringLiteral("+"), QStringLiteral("insert"), tr("Insert Effect"), this);
    solo_ = smallButton(QStringLiteral("S"), QStringLiteral("solo"), this, true);
    solo_->setToolTip(tr("Solo"));
    mute_ = smallButton(QStringLiteral("M"), QStringLiteral("mute"), this, true);
    mute_->setToolTip(tr("Mute"));

    // The fader with its scale (3), the Volume Level display (5) and the
    // track button (4).
    fader_ = new QSlider(Qt::Vertical, this);
    fader_->setObjectName(QStringLiteral("fader"));
    fader_->setRange(0, fader::kRange);
    fader_->setValue(fader::position(0));
    fader_->setPageStep(fader::kRange / 11);
    fader_->setMinimumHeight(180);
    fader_->installEventFilter(this);
    level_ = new QLineEdit(fader::text(0), this);
    level_->setObjectName(QStringLiteral("level"));
    level_->setAlignment(Qt::AlignCenter);
    level_->setFixedWidth(44);
    trackButton_ = smallButton(track, QStringLiteral("trackButton"), this, !master);
    trackButton_->setMinimumWidth(40);

    if (!master) {
        auto* top = new QHBoxLayout;
        top->addStretch(1);
        top->addWidget(gang_);
        top->addStretch(1);
        v->addLayout(top);
        v->addWidget(pan_, 0, Qt::AlignHCenter);
        v->addWidget(panBox_, 0, Qt::AlignHCenter);
    } else {
        gang_->hide();
        pan_->hide();
        panBox_->hide();
        solo_->hide();
    }
    v->addWidget(insert, 0, Qt::AlignHCenter);
    auto* sm = new QHBoxLayout;
    sm->setSpacing(2);
    sm->addWidget(solo_);
    sm->addWidget(mute_);
    v->addLayout(sm);
    auto* faderRow = new QHBoxLayout;
    faderRow->setSpacing(0);
    faderRow->addWidget(new FaderScale(fader_, this));
    faderRow->addWidget(fader_);
    v->addLayout(faderRow, 1);
    v->addWidget(level_, 0, Qt::AlignHCenter);
    v->addWidget(trackButton_, 0, Qt::AlignHCenter);

    connect(fader_, &QSlider::valueChanged, this, [this](int pos) {
        if (syncing_) return;
        gain_ = fader::gain(pos);
        level_->setText(fader::text(gain_));
        syncColours();
        emit gainSet(gain_, !fader_->isSliderDown());
    });
    connect(fader_, &QSlider::sliderReleased, this, [this] { emit gainSet(gain_, true); });
    connect(level_, &QLineEdit::editingFinished, this, [this] {
        if (syncing_ || !level_->isModified()) return;
        level_->setModified(false);
        // Typed values go to +36 dB (p.669-670).
        if (const auto db = fader::parse(level_->text())) {
            show(*db, panValue_);
            emit gainSet(*db, true);
        } else {
            show(gain_, panValue_);
        }
    });
    connect(pan_, &QDial::valueChanged, this, [this](int value) {
        if (syncing_) return;
        panValue_ = value;
        panBox_->setText(fader::panText(value));
        emit panSet(value, !pan_->isSliderDown());
    });
    connect(pan_, &QDial::sliderReleased, this, [this] { emit panSet(panValue_, true); });
    connect(panBox_, &QLineEdit::editingFinished, this, [this] {
        if (syncing_ || !panBox_->isModified()) return;
        panBox_->setModified(false);
        if (const auto p = fader::parsePan(panBox_->text())) {
            show(gain_, *p);
            emit panSet(*p, true);
        } else {
            show(gain_, panValue_);
        }
    });
}

void MixerStrip::show(double gain, int pan) {
    syncing_ = true;
    gain_ = gain;
    panValue_ = pan;
    fader_->setValue(fader::position(std::min(gain, model::kGainFaderMax)));
    level_->setText(fader::text(gain));
    pan_->setValue(pan);
    panBox_->setText(fader::panText(pan));
    syncColours();
    syncing_ = false;
}

void MixerStrip::showText(const QString& text) {
    syncing_ = true;
    level_->setText(text);
    syncing_ = false;
}

void MixerStrip::setLive(bool on) {
    live_ = on;
    syncColours();
}

void MixerStrip::syncColours() {
    // Extended levels (over +12 dB) show yellow (p.670); Live mode red (p.725).
    QString style;
    if (gain_ > model::kGainFaderMax) style = QStringLiteral("background: #b8a020;");
    if (live_) style = QStringLiteral("background: #8a2a2a;");
    fader_->setStyleSheet(style.isEmpty() ? QString() : QStringLiteral("QSlider { %1 }").arg(style));
    level_->setStyleSheet(gain_ > model::kGainFaderMax ? QStringLiteral("QLineEdit { background: #b8a020; }") : QString());
}

void MixerStrip::setGang(const QString& letter) {
    gangName_ = letter;
    gang_->setText(letter.isEmpty() ? QStringLiteral("∞") : letter);
    gang_->setToolTip(letter.isEmpty() ? tr("Gang") : tr("Gang %1").arg(letter));
}

bool MixerStrip::eventFilter(QObject* watched, QEvent* event) {
    // Alt-click (Option-click) resets the fader to 0 dB and the pan to MID
    // (p.703).
    if (event->type() == QEvent::MouseButtonPress) {
        auto* m = static_cast<QMouseEvent*>(event);
        if (m->modifiers() & Qt::AltModifier) {
            if (watched == fader_) {
                show(0, panValue_);
                emit gainSet(0, true);
                return true;
            }
            if (watched == panBox_) {
                show(gain_, 0);
                emit panSet(0, true);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- AudioMixer

AudioMixer::AudioMixer(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("audioMixer"));

    // The top bar (picture and table on p.691-693).
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    bar->addWidget(laterButton(QStringLiteral(">>"), QStringLiteral("showTrackSidebar"), tr("Show Track Sidebar"), this));
    mixMode_ = smallButton(tr("Stereo"), QStringLiteral("mixMode"), this);
    mixMode_->setToolTip(tr("Mix Mode Selection"));
    mixMode_->setPopupMode(QToolButton::InstantPopup);
    auto* mixMenu = new QMenu(mixMode_);
    // Stereo and Mono; the surround and Direct Out mixes need output
    // hardware (p.691-692, p.1150).
    mixMenu->addAction(tr("Stereo"), this, [this] { setMixMode(QStringLiteral("stereo")); });
    mixMenu->addAction(tr("Mono"), this, [this] { setMixMode(QStringLiteral("mono")); });
    mixMode_->setMenu(mixMenu);
    bar->addWidget(mixMode_);
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    const std::pair<Mode, QString> names[] = {{Mode::Clip, tr("Clip")}, {Mode::Auto, tr("Auto")}, {Mode::Live, tr("Live")}};
    for (const auto& [m, text] : names) {
        QToolButton* b = smallButton(text, QStringLiteral("mode:") + modeName(m), this, true);
        modes->addButton(b);
        modeButtons_[static_cast<int>(m)] = b;
        connect(b, &QToolButton::clicked, this, [this, mode = m] { setMode(mode); });
        bar->addWidget(b);
    }
    modeButtons_[0]->setChecked(true);
    bypass_ = smallButton(tr("Bypass"), QStringLiteral("bypass"), this, true);
    bypass_->setToolTip(tr("Bypass clip volume and volume automation"));
    bar->addWidget(bypass_);
    bar->addWidget(laterButton(tr("Loop"), QStringLiteral("audioLoopPlay"), tr("Audio Loop Play"), this));
    bar->addWidget(laterButton(tr("Render"), QStringLiteral("renderEffect"), tr("Render Effect"), this));
    bar->addStretch(1);
    fast_ = smallButton(QStringLiteral("☰"), QStringLiteral("fastMenu"), this);
    fast_->setToolTip(tr("Fast Menu"));
    fast_->setPopupMode(QToolButton::InstantPopup);
    fast_->setMenu(new QMenu(fast_));
    connect(fast_->menu(), &QMenu::aboutToShow, this, [this] { fastMenu(); });
    bar->addWidget(fast_);

    // The strips scroll sideways; the Master strip stays on the right
    // (p.694-696).
    stripArea_ = new QWidget;
    stripLayout_ = new QHBoxLayout(stripArea_);
    stripLayout_->setContentsMargins(0, 0, 0, 0);
    stripLayout_->setSpacing(4);
    empty_ = new QLabel(tr("Load a sequence with audio tracks into the Record monitor."), stripArea_);
    empty_->setWordWrap(true);
    stripLayout_->addWidget(empty_);
    stripLayout_->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("mixerStrips"));
    scroll->setWidgetResizable(true);
    scroll->setWidget(stripArea_);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    master_ = new MixerStrip(tr("Master"), true, this);
    master_->setObjectName(QStringLiteral("strip:Master"));
    // The Sequence Mix Format button at the top of the Master strip (p.693).
    auto* masterColumn = new QVBoxLayout;
    masterColumn->addWidget(laterButton(tr("Stereo Sequence"), QStringLiteral("sequenceMixFormat"),
                                        tr("Sequence Mix Format"), this));
    masterColumn->addWidget(master_, 1);
    auto* body = new QHBoxLayout;
    body->addWidget(scroll, 1);
    body->addLayout(masterColumn);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(bar);
    layout->addLayout(body, 1);

    connect(&timeline_, &Timeline::changed, this, &AudioMixer::refresh);
    connect(&timeline_.composer(), &Composer::changed, this, &AudioMixer::refresh);
    refresh();
}

QString AudioMixer::modeName(Mode mode) {
    switch (mode) {
    case Mode::Clip: return QStringLiteral("clip");
    case Mode::Auto: return QStringLiteral("auto");
    case Mode::Live: return QStringLiteral("live");
    }
    return QStringLiteral("clip");
}

std::optional<AudioMixer::Mode> AudioMixer::modeFromName(const QString& name) {
    for (Mode m : {Mode::Clip, Mode::Auto, Mode::Live}) {
        if (modeName(m) == name) return m;
    }
    return std::nullopt;
}

QToolButton* AudioMixer::modeButton(Mode mode) const {
    return modeButtons_[static_cast<int>(mode)];
}

void AudioMixer::setMode(Mode mode) {
    modeButtons_[static_cast<int>(mode)]->setChecked(true);
    if (mode == mode_) return;
    mode_ = mode;
    // Bypass is hidden in Live mode (p.692).
    bypass_->setVisible(mode != Mode::Live);
    syncValues();
    emit modeChanged(mode);
}

void AudioMixer::setMixMode(const QString& value) {
    if (value != QLatin1String("stereo") && value != QLatin1String("mono")) return;
    const bool changed = value != mixModeValue_;
    mixModeValue_ = value;
    mixMode_->setText(value == QLatin1String("mono") ? tr("Mono") : tr("Stereo"));
    if (changed) emit mixModeChanged(value);
}

QStringList AudioMixer::tracks() const {
    return order_;
}

void AudioMixer::refresh() {
    QStringList tracks;
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (rec && rec->item.kind == model::BinItemKind::Sequence) {
        for (const QString& t : model::Sequence::fromItem(rec->item).trackNames()) {
            if (t.startsWith(QLatin1Char('A'))) tracks << t;
        }
    }
    if (tracks != order_) rebuild(tracks);
    syncValues();
}

void AudioMixer::rebuild(const QStringList& tracks) {
    for (MixerStrip* s : std::as_const(strips_)) delete s;
    strips_.clear();
    order_ = tracks;
    empty_->setVisible(tracks.isEmpty());
    int at = 1;
    for (const QString& t : tracks) {
        auto* strip = new MixerStrip(t, false, stripArea_);
        strip->setGang(gangs_.value(t));
        strip->soloButton()->setChecked(solo_.contains(t));
        strip->muteButton()->setChecked(mute_.contains(t));
        connect(strip, &MixerStrip::gainSet, this, [this, t](double db, bool done) { setGain(t, db, done); });
        connect(strip, &MixerStrip::panSet, this, [this, t](int pan, bool done) { setPan(t, pan, done); });
        // Solo and mute stay through mode changes and closing the mixer
        // (p.694, p.699).
        connect(strip->soloButton(), &QToolButton::toggled, this, [this, t](bool on) {
            if (on) {
                solo_.insert(t);
            } else {
                solo_.remove(t);
            }
        });
        connect(strip->muteButton(), &QToolButton::toggled, this, [this, t](bool on) {
            if (on) {
                mute_.insert(t);
            } else {
                mute_.remove(t);
            }
        });
        // The track button selects the Timeline track and back (p.698).
        connect(strip->trackButton(), &QToolButton::clicked, this, [this, t] { timeline_.toggleRecordTrack(t); });
        // Right-click the gang button for gangs A-J (p.704).
        connect(strip->gangButton(), &QToolButton::customContextMenuRequested, this, [this, strip, t](const QPoint& pos) {
            QMenu menu(strip);
            menu.addAction(tr("None"), this, [this, t] { setGang(t, QString()); });
            for (char c = 'A'; c <= 'J'; ++c) {
                const QString letter(QLatin1Char{c});
                menu.addAction(tr("Gang %1").arg(letter), this, [this, t, letter] { setGang(t, letter); });
            }
            menu.exec(strip->gangButton()->mapToGlobal(pos));
        });
        stripLayout_->insertWidget(at++, strip);
        strips_.insert(t, strip);
    }
}

void AudioMixer::setGang(const QString& track, const QString& letter) {
    if (letter.isEmpty()) {
        gangs_.remove(track);
    } else {
        gangs_.insert(track, letter);
    }
    if (MixerStrip* s = strips_.value(track)) s->setGang(letter);
}

void AudioMixer::syncValues() {
    const model::Sequence seq = [this] {
        const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        return rec ? model::Sequence::fromItem(rec->item) : model::Sequence();
    }();
    for (const QString& t : order_) {
        MixerStrip* s = strips_.value(t);
        if (!s) continue;
        s->trackButton()->setChecked(timeline_.patch().isRecordSelected(t));
        s->setLive(mode_ == Mode::Live);
        const auto segment = timeline_.audioSegmentAt(t);
        switch (mode_) {
        case Mode::Clip:
            s->fader()->setEnabled(segment.has_value());
            s->panKnob()->setEnabled(segment.has_value());
            s->show(segment ? segment->gain : 0, segment ? segment->pan : 0);
            break;
        case Mode::Auto:
            // Volume automation comes with Phase 5; the display says when
            // the track has clip volume (p.694).
            s->fader()->setEnabled(false);
            s->panKnob()->setEnabled(false);
            s->show(0, 0);
            if (segment && segment->gain != 0) s->showText(tr("Clip"));
            break;
        case Mode::Live:
            s->fader()->setEnabled(true);
            s->panKnob()->setEnabled(true);
            s->show(liveGain_.value(t, 0), livePan_.value(t, 0));
            break;
        }
    }
    master_->setLive(mode_ == Mode::Live);
}

void AudioMixer::setGain(const QString& track, double db, bool done) {
    // Ganged faders move by the same amount (p.704).
    QStringList moved{track};
    const QString gang = gangs_.value(track);
    const double delta = db - (mode_ == Mode::Live ? liveGain_.value(track, 0)
                                                   : timeline_.audioSegmentAt(track).value_or(model::Segment{}).gain);
    if (!gang.isEmpty()) {
        for (const QString& t : order_) {
            if (t != track && gangs_.value(t) == gang) moved << t;
        }
    }
    if (mode_ == Mode::Live) {
        for (const QString& t : moved) liveGain_.insert(t, t == track ? db : model::clampGain(liveGain_.value(t, 0) + delta));
        syncValues();
        return;
    }
    if (mode_ != Mode::Clip || !done) return;
    for (const QString& t : moved) {
        const auto segment = timeline_.audioSegmentAt(t);
        if (!segment) continue;
        const double value = t == track ? db : model::clampGain(segment->gain + delta);
        timeline_.setClipAudio(t, value, std::nullopt, false, tr("Clip Gain"));
    }
}

void AudioMixer::setPan(const QString& track, int pan, bool done) {
    if (mode_ == Mode::Live) {
        livePan_.insert(track, pan);
        return;
    }
    if (mode_ == Mode::Clip && done) timeline_.setClipAudio(track, std::nullopt, pan, false, tr("Clip Pan"));
}

QStringList AudioMixer::selectedTracks() const {
    QStringList out;
    for (const QString& t : order_) {
        if (timeline_.patch().isRecordSelected(t)) out << t;
    }
    return out;
}

QMenu* AudioMixer::fastMenu() {
    // The Fast menu's items depend on the mode (p.711, p.719-720, p.725-726)
    // and are dimmed until a track is selected (p.711). They act between
    // the marks, or on whole tracks without them.
    QMenu* m = fast_->menu();
    m->clear();
    const QStringList tracks = selectedTracks();
    auto later = [m](const QString& text) { m->addAction(text)->setEnabled(false); };
    auto onTracks = [this, m, tracks](const QString& text, std::optional<bool> gain, std::optional<bool> pan,
                                      bool remove) {
        QAction* a = m->addAction(text);
        a->setEnabled(!tracks.isEmpty());
        connect(a, &QAction::triggered, this, [this, tracks, gain, pan, remove, text] {
            for (const QString& t : tracks) {
                const MixerStrip* s = strips_.value(t);
                if (!s) continue;
                std::optional<double> g;
                std::optional<int> p;
                if (gain) g = remove ? 0.0 : s->gain();
                if (pan) p = remove ? 0 : s->pan();
                timeline_.setClipAudio(t, g, p, true, text);
            }
        });
    };
    switch (mode_) {
    case Mode::Clip:
        onTracks(tr("Set Level on Track"), true, std::nullopt, false);
        onTracks(tr("Set Pan on Track"), std::nullopt, true, false);
        later(tr("Adjust Pan/Vols on Track..."));
        later(tr("Remove Default Clip Pan on Track"));
        later(tr("Commit Default Clip Pan on Track"));
        onTracks(tr("Remove Clip Gain on Track"), true, std::nullopt, true);
        onTracks(tr("Remove Pan on Track"), std::nullopt, true, true);
        onTracks(tr("Remove Pan/Vols on Track"), true, true, true);
        later(tr("Disable Track Monitoring"));
        later(tr("Set Display Options..."));
        break;
    case Mode::Auto:
        for (const QString& t : {tr("Filter volume automation on Track"), tr("Filter Pan on Track"),
                                 tr("Adjust Volume/Pan on Track..."), tr("Remove volume automation on Track"),
                                 tr("Remove Pan on Track"), tr("Remove Volume/Pan on Track"),
                                 tr("Remove Hidden Volume Automation on Track - Global"),
                                 tr("Remove Hidden Pan on Track - Global"),
                                 tr("Remove Hidden Pan/Volume on Track - Global"), tr("Calibrate Hardware Sliders"),
                                 tr("Set Display Options...")}) {
            later(t);
        }
        break;
    case Mode::Live: {
        QAction* reset = m->addAction(tr("Set Live Mix to Default"));
        connect(reset, &QAction::triggered, this, [this] {
            liveGain_.clear();
            livePan_.clear();
            syncValues();
        });
        later(tr("Set Live Mix to Automation"));
        later(tr("Set Live Mix as Automation"));
        break;
    }
    }
    return m;
}

// ---------------------------------------------------------------- CalibrationToneDialog

CalibrationToneDialog::CalibrationToneDialog(const CalibrationTone& tone, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("calibrationToneDialog"));
    setWindowTitle(tr("Set Calibration Tone"));
    level_ = new QSpinBox(this);
    level_->setObjectName(QStringLiteral("toneLevel"));
    level_->setRange(-90, 0);
    level_->setSuffix(tr(" dB"));
    level_->setValue(tone.level);
    frequency_ = new QSpinBox(this);
    frequency_->setObjectName(QStringLiteral("toneFrequency"));
    frequency_->setRange(1, 20000);
    frequency_->setSuffix(tr(" Hz"));
    frequency_->setValue(tone.frequency);
    auto* form = new QFormLayout;
    form->addRow(tr("Tone Level:"), level_);
    form->addRow(tr("Frequency:"), frequency_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

CalibrationTone CalibrationToneDialog::tone() const {
    return CalibrationTone{level_->value(), frequency_->value()};
}

// ---------------------------------------------------------------- AudioMeters

namespace {

// The digital scale (picture on p.158).
constexpr int kDigital[] = {0, -4, -8, -14, -20, -26, -30, -35, -40, -65, -90};

// Where a level sits: the scale's marks equally spaced, as pictured.
double fraction(double db) {
    constexpr int n = 10;
    if (db >= 0) return 0;
    if (db <= -90) return 1;
    for (int i = 0; i < n; ++i) {
        if (db <= kDigital[i] && db >= kDigital[i + 1]) {
            return (i + (kDigital[i] - db) / (kDigital[i] - kDigital[i + 1])) / n;
        }
    }
    return 1;
}

} // namespace

AudioMeters::AudioMeters(int channels, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("audioMeters"));
    for (int i = 0; i < channels; ++i) {
        levels_ << -90;
        peaks_ << -90;
    }
    setMinimumHeight(220);
}

QSize AudioMeters::sizeHint() const {
    return QSize(60 + channels() * 14, 260);
}

void AudioMeters::setLevel(int channel, double db) {
    if (channel < 0 || channel >= levels_.size()) return;
    levels_[channel] = db;
    peaks_[channel] = std::max(peaks_.at(channel), db);
    update();
}

void AudioMeters::setPeak(int channel, double db) {
    if (channel < 0 || channel >= peaks_.size()) return;
    peaks_[channel] = db;
    update();
}

void AudioMeters::resetPeaks() {
    for (double& p : peaks_) p = -90;
    update();
}

QColor AudioMeters::colourFor(double db, int reference) {
    // Green below the reference level, yellow to about -3 dB, red to 0 dB
    // (p.157).
    if (db < reference) return QColor(0xff3cc85a);
    if (db < -3) return QColor(0xffd8c040);
    return QColor(0xffd84040);
}

void AudioMeters::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor text = palette().color(QPalette::WindowText);
    const int scaleWidth = 26;
    const int top = 8;
    const int bottom = height() - 8;
    const int span = bottom - top;
    QFont f = font();
    f.setPixelSize(8);
    p.setFont(f);
    p.setPen(text);
    // Digital scale on the left; VU on the right, 0 VU at the reference
    // (p.157-158).
    for (int d : kDigital) {
        const int yy = top + static_cast<int>(fraction(d) * span);
        p.drawText(QRect(0, yy - 6, scaleWidth - 4, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(d));
        const int vu = d - reference_;
        p.drawText(QRect(width() - scaleWidth + 4, yy - 6, scaleWidth - 4, 12), Qt::AlignLeft | Qt::AlignVCenter,
                   (vu > 0 ? QStringLiteral("+") : QString()) + QString::number(vu));
    }
    const int area = width() - 2 * scaleWidth;
    const int n = std::max(channels(), 1);
    const int barWidth = std::max(area / n - 3, 3);
    for (int c = 0; c < channels(); ++c) {
        const int x = scaleWidth + c * (area / n) + 1;
        p.fillRect(QRect(x, top, barWidth, span), palette().color(QPalette::Base));
        const int levelY = top + static_cast<int>(fraction(levels_.at(c)) * span);
        // The bar, coloured by band.
        for (int yy = levelY; yy < bottom; ++yy) {
            const double db = -90 + 90.0 * (1 - static_cast<double>(yy - top) / span);
            p.setPen(colourFor(db, reference_));
            p.drawLine(x, yy, x + barWidth - 1, yy);
        }
        const int peakY = top + static_cast<int>(fraction(peaks_.at(c)) * span);
        if (peaks_.at(c) > -90) {
            p.setPen(colourFor(peaks_.at(c), reference_));
            p.drawLine(x, peakY, x + barWidth - 1, peakY);
        }
    }
}

// ---------------------------------------------------------------- AudioTool

AudioTool::AudioTool(int channels, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("audioTool"));
    // The top row (picture on p.158): RP, an In/Out button per channel, PH.
    reset_ = smallButton(QStringLiteral("RP"), QStringLiteral("resetPeak"), this);
    reset_->setToolTip(tr("Reset Peak"));
    peakHold_ = smallButton(QStringLiteral("PH"), QStringLiteral("peakHold"), this);
    peakHold_->setToolTip(tr("Peak Hold"));
    peakHold_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(peakHold_);
    auto* holds = new QActionGroup(menu);
    QAction* peak = menu->addAction(tr("Peak Hold"));
    QAction* infinite = menu->addAction(tr("Infinite Hold"));
    for (QAction* a : {peak, infinite}) {
        a->setCheckable(true);
        holds->addAction(a);
    }
    peak->setChecked(true);
    connect(peak, &QAction::triggered, this, [this] { hold_ = Hold::PeakHold; });
    connect(infinite, &QAction::triggered, this, [this] { hold_ = Hold::Infinite; });
    menu->addSeparator();
    menu->addAction(tr("Set Calibration Tone..."), this, [this] {
        CalibrationToneDialog dialog(tone_, this);
        if (dialog.exec() == QDialog::Accepted) setCalibrationTone(dialog.tone());
    });
    // Tone needs playback, tone media needs media files (Phase 2 and 5).
    menu->addAction(tr("Play Calibration Tone"))->setEnabled(false);
    menu->addAction(tr("Create Tone Media..."))->setEnabled(false);
    peakHold_->setMenu(menu);

    auto* top = new QHBoxLayout;
    top->setSpacing(2);
    top->addWidget(reset_);
    for (int c = 0; c < channels; ++c) {
        // I = Input, O = Output (p.157).
        QToolButton* b = smallButton(QStringLiteral("O"), QStringLiteral("io%1").arg(c + 1), this, true);
        b->setChecked(true);
        b->setToolTip(tr("Channel %1: Input or Output").arg(c + 1));
        connect(b, &QToolButton::toggled, b, [b](bool out) { b->setText(out ? QStringLiteral("O") : QStringLiteral("I")); });
        io_ << b;
        top->addWidget(b);
    }
    top->addWidget(peakHold_);
    top->addStretch(1);

    meters_ = new AudioMeters(channels, this);
    // Reset Peak resets the peaks and stops the tone (p.157).
    connect(reset_, &QToolButton::clicked, meters_, &AudioMeters::resetPeaks);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(top);
    layout->addWidget(meters_, 1);
}

bool AudioTool::isOutput(int channel) const {
    const QToolButton* b = io_.value(channel);
    return b && b->isChecked();
}

void AudioTool::setCalibrationTone(const CalibrationTone& tone) {
    tone_ = tone;
    emit calibrationToneChanged(tone);
}

} // namespace lumenis::ui
