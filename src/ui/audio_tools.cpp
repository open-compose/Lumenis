#include "lumenis/ui/audio_tools.h"

#include "lumenis/model/automation.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/timeline.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QDial>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
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

#include <algorithm>
#include <cmath>
#include <memory>

// The Audio Mixer and the Audio Tool (audio_tools.h): the fader scale's maths, the mixer strips, the
// mixer with its Clip, Auto and Live modes and Fast menu, the Set Calibration Tone dialog, and the
// Audio Tool's meters. The mixer reads and changes clip gain and pan through the Timeline
// (Timeline::audioSegmentAt, Timeline::setClipAudio in timeline.*); the gain limits are in
// src/model/sequence.h (kGainOff, kGainFaderMax, clampGain). Sound itself plays from
// src/media/playback.*; these meters are not fed by it yet.

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

// Linear between the two marks around `db`, each step kStep positions high; the top mark is kRange.
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

// A plain small button of the mixer and Audio Tool.
QToolButton* smallButton(const QString& text, const QString& name, QWidget* parent, bool checkable = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setCheckable(checkable);
    b->setAutoRaise(false);
    b->setMinimumWidth(22);
    return b;
}

// A disabled button for a feature that comes later; its tooltip says so.
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

// Lays out the strip top down as on p.693, then wires each control to update the others and emit
// gainSet() or panSet(). Typed values that cannot be read put the old value back.
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

// The fader stops at +12 dB; higher typed levels show in the level display, in yellow.
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

void MixerStrip::setAuto(bool on) {
    auto_ = on;
    syncColours();
}

void MixerStrip::syncColours() {
    // Extended levels (over +12 dB) show yellow (p.670); Live mode red (p.725).
    QString style;
    if (gain_ > model::kGainFaderMax) style = QStringLiteral("background: #b8a020;");
    if (auto_) style = QStringLiteral("background: #2a4a8a;");
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

namespace {

// Adjust Volume/Pan on Track (p.719): "if you type -1 in the Volume Adjustment text box, the
// various audio level settings ... are lowered by exactly 1 dB". Ours: a Pan Adjustment too.
std::optional<std::pair<double, int>> askAdjust(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("adjustVolumePan"));
    dialog.setWindowTitle(QObject::tr("Adjust Volume/Pan on Track"));
    auto* volume = new QDoubleSpinBox(&dialog);
    volume->setRange(-60, 36);
    volume->setDecimals(1);
    volume->setSuffix(QObject::tr(" dB"));
    auto* pan = new QSpinBox(&dialog);
    pan->setRange(-2 * model::kPanMax, 2 * model::kPanMax);
    auto* form = new QFormLayout;
    form->addRow(QObject::tr("Volume Adjustment:"), volume);
    form->addRow(QObject::tr("Pan Adjustment:"), pan);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return std::pair{volume->value(), pan->value()};
}

} // namespace

// The top bar, then the track strips in a sideways-scrolling area with the Master strip to the right.
// The Fast menu is filled each time it opens (fastMenu()).
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
    // The mixes of p.691-692, the surround ones as the Monitor Mix Format button lists them (p.689);
    // Direct Out needs output assignments that come later (lumenis/media/surround.h).
    for (media::MonitorMix m : media::monitorMixes()) {
        const QString id = media::monitorMixId(m);
        QAction* a = mixMenu->addAction(media::monitorMixName(m), this, [this, id] { setMixMode(id); });
        a->setObjectName(QStringLiteral("mixMode:") + id);
        if (m == media::MonitorMix::Quad || m == media::MonitorMix::ProTools51) mixMenu->insertSeparator(a);
    }
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
    // Auto mode's Record and Cancel buttons and preroll and postroll boxes (picture and table on
    // p.717), shown in Auto mode only.
    recordControls_ = new QWidget(this);
    auto* rec = new QHBoxLayout(recordControls_);
    rec->setContentsMargins(0, 0, 0, 0);
    rec->setSpacing(3);
    record_ = smallButton(tr("Rec"), QStringLiteral("recordAutomation"), recordControls_, true);
    record_->setToolTip(tr("Record (B)"));
    cancel_ = smallButton(tr("Cancel"), QStringLiteral("cancelAutomation"), recordControls_);
    preroll_ = new QSpinBox(recordControls_);
    preroll_->setObjectName(QStringLiteral("preroll"));
    preroll_->setRange(0, 60);
    preroll_->setToolTip(tr("Preroll (seconds)"));
    postroll_ = new QSpinBox(recordControls_);
    postroll_->setObjectName(QStringLiteral("postroll"));
    postroll_->setRange(0, 60);
    postroll_->setToolTip(tr("Postroll (seconds)"));
    rec->addWidget(record_);
    rec->addWidget(cancel_);
    rec->addWidget(preroll_);
    rec->addWidget(postroll_);
    recordControls_->setVisible(false);
    connect(record_, &QToolButton::clicked, this, [this] {
        if (recording_) {
            stopRecording(false);
        } else {
            startRecording();
        }
    });
    connect(cancel_, &QToolButton::clicked, this, [this] { stopRecording(true); });
    bar->addWidget(recordControls_);
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
    // The Sequence Mix Format button at the top of the Master strip (p.693): Stereo, 5.1 or 7.1
    // Sequence (p.686), an undoable change to the Record monitor's sequence.
    auto* masterColumn = new QVBoxLayout;
    sequenceMix_ = smallButton(tr("Stereo Sequence"), QStringLiteral("sequenceMixFormat"), this);
    sequenceMix_->setToolTip(tr("Sequence Mix Format"));
    sequenceMix_->setPopupMode(QToolButton::InstantPopup);
    auto* sequenceMenu = new QMenu(sequenceMix_);
    for (model::AudioFormat f : {model::AudioFormat::Stereo, model::AudioFormat::Surround51, model::AudioFormat::Surround71}) {
        QAction* a = sequenceMenu->addAction(sequenceMixName(f), this, [this, f] { setSequenceMixFormat(f); });
        a->setObjectName(QStringLiteral("sequenceMix:") + model::formatName(f));
    }
    sequenceMix_->setMenu(sequenceMenu);
    masterColumn->addWidget(sequenceMix_);
    masterColumn->addWidget(master_, 1);
    // The Master fader sets the level of the whole mix, after the tracks, in every mode.
    connect(master_, &MixerStrip::gainSet, this, [this](double db, bool) { setMasterGain(db); });
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
    connect(&timeline_.composer(), &Composer::changed, this, &AudioMixer::recordTick);
    setFocusPolicy(Qt::StrongFocus);
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
    if (recording_) stopRecording(false);
    mode_ = mode;
    // Bypass is hidden in Live mode (p.692); the recording controls show in Auto mode (p.717).
    bypass_->setVisible(mode != Mode::Live);
    recordControls_->setVisible(mode == Mode::Auto);
    syncValues();
    emit modeChanged(mode);
}

void AudioMixer::setMixMode(const QString& value) {
    const auto mix = media::monitorMixFromId(value);
    if (!mix) return;
    const bool changed = value != mixModeValue_;
    mixModeValue_ = value;
    mixMode_->setText(media::monitorMixShortName(*mix));
    if (changed) emit mixModeChanged(value);
}

media::MonitorMix AudioMixer::monitorMix() const {
    return media::monitorMixFromId(mixModeValue_).value_or(media::MonitorMix::Stereo);
}

QString AudioMixer::sequenceMixName(model::AudioFormat format) {
    switch (format) {
    case model::AudioFormat::Surround51: return tr("5.1 Sequence");
    case model::AudioFormat::Surround71: return tr("7.1 Sequence");
    case model::AudioFormat::Mono:
    case model::AudioFormat::Stereo: break;
    }
    return tr("Stereo Sequence");
}

model::AudioFormat AudioMixer::sequenceMixFormat() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return model::AudioFormat::Stereo;
    return model::Sequence::fromItem(rec->item).mixFormat();
}

bool AudioMixer::setSequenceMixFormat(model::AudioFormat format) {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || sequenceMixFormat() == format) return false;
    return timeline_.changeRecordSequence(tr("Sequence Mix Format"), [format](model::Sequence& s) {
        s.setMixFormat(format);
        return true;
    });
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
    sequenceMix_->setText(sequenceMixName(sequenceMixFormat()));
    sequenceMix_->setEnabled(rec && rec->item.kind == model::BinItemKind::Sequence);
    syncValues();
}

// New strips go after the "empty" label (index 0) and before the stretch.
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
        s->setAuto(mode_ == Mode::Auto);
        const auto segment = timeline_.audioSegmentAt(t);
        switch (mode_) {
        case Mode::Clip:
            s->fader()->setEnabled(segment.has_value());
            s->panKnob()->setEnabled(segment.has_value());
            s->show(segment ? segment->gain : 0, segment ? segment->pan : 0);
            // "In Clip Volume mode, if the track has an volume and pan automation value associated
            // with it, the word Auto appears" (p.694).
            if (segment && segment->hasAutomation()) s->showText(tr("Auto"));
            break;
        case Mode::Auto: {
            // The automation at the position (p.716); a strip being recorded keeps what the user
            // is doing with it.
            s->fader()->setEnabled(segment.has_value());
            s->panKnob()->setEnabled(segment.has_value());
            if (recording_ && (touchedGain_.contains(t) || touchedPan_.contains(t))) break;
            const double gain = timeline_.automationAt(t, model::AutomationParam::Volume).value_or(0);
            const double pan = timeline_.automationAt(t, model::AutomationParam::Pan).value_or(0);
            s->show(gain, static_cast<int>(std::lround(pan)));
            // "... if the track has a system clip volume value associated with it, the word Clip
            // appears" (p.694).
            if (segment && segment->gain != 0) s->showText(tr("Clip"));
            break;
        }
        case Mode::Live:
            s->fader()->setEnabled(true);
            s->panKnob()->setEnabled(true);
            s->show(liveGain_.value(t, 0), livePan_.value(t, 0));
            break;
        }
    }
    master_->setLive(mode_ == Mode::Live);
    master_->show(masterGain_, 0);
}

void AudioMixer::setMasterGain(double db) {
    db = model::clampGain(db);
    const bool changed = db != masterGain_;
    masterGain_ = db;
    master_->show(db, 0);
    if (changed) emit masterGainChanged(db);
}

// Live mode keeps the values in the mixer and follows every move. Clip mode stores only finished
// changes (done), one undo step per ganged track with a segment under the position. Auto mode sets
// the automation there the same way (Timeline::setAutomationValue); while recording, every move is
// written at the position and the track keeps being written as the position moves (recordTick).
void AudioMixer::setGain(const QString& track, double db, bool done) {
    // Ganged faders move by the same amount (p.704).
    QStringList moved{track};
    const QString gang = gangs_.value(track);
    const double was = mode_ == Mode::Live ? liveGain_.value(track, 0)
                       : mode_ == Mode::Auto
                           ? (recording_ && touchedGain_.contains(track) && strips_.value(track)
                                  ? strips_.value(track)->gain()
                                  : timeline_.automationAt(track, model::AutomationParam::Volume).value_or(0))
                           : timeline_.audioSegmentAt(track).value_or(model::Segment{}).gain;
    const double delta = db - was;
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
    if (mode_ == Mode::Auto) {
        for (const QString& t : moved) {
            const double base = t == track ? was : timeline_.automationAt(t, model::AutomationParam::Volume).value_or(0);
            const double value = t == track ? db : model::clampGain(base + delta);
            if (recording_) {
                // The strip holds what it records, for the frames that follow (recordTick).
                touchedGain_.insert(t);
                if (MixerStrip* s = strips_.value(t)) s->show(value, s->pan());
                if (inRecordRange()) timeline_.writeAutomation(t, model::AutomationParam::Volume, value);
            } else if (done) {
                timeline_.setAutomationValue(t, model::AutomationParam::Volume, value);
            }
        }
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
    if (mode_ == Mode::Auto) {
        if (recording_) {
            touchedPan_.insert(track);
            if (MixerStrip* s = strips_.value(track)) s->show(s->gain(), pan);
            if (inRecordRange()) timeline_.writeAutomation(track, model::AutomationParam::Pan, pan);
        } else if (done) {
            timeline_.setAutomationValue(track, model::AutomationParam::Pan, pan);
        }
    }
}

bool AudioMixer::inRecordRange() const {
    const model::MonitorState& record = timeline_.composer().state(MonitorId::Record);
    const std::int64_t at = record.position();
    return (!record.in() || at >= *record.in()) && (!record.out() || at <= *record.out());
}

void AudioMixer::startRecording(bool play) {
    if (mode_ != Mode::Auto || recording_) {
        record_->setChecked(recording_);
        return;
    }
    if (!timeline_.beginAutomationRecording()) {
        record_->setChecked(false);
        return;
    }
    recording_ = true;
    record_->setChecked(true);
    touchedGain_.clear();
    touchedPan_.clear();
    Composer& composer = timeline_.composer();
    const model::MonitorState& record = composer.state(MonitorId::Record);
    const std::int64_t fps = std::max(timeline_.format().nominalFps, 1);
    const std::int64_t start = record.in().value_or(record.position());
    recordEnd_.reset();
    if (record.out()) recordEnd_ = *record.out() + postroll_->value() * fps;
    lastWritten_ = -1;
    recordPlaying_ = false;
    if (play) {
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, std::max<std::int64_t>(start - preroll_->value() * fps, 0));
        composer.play(1);
        recordPlaying_ = composer.isPlaying();
    }
    lastWritten_ = record.position();
}

void AudioMixer::stopRecording(bool cancel) {
    if (!recording_) return;
    recording_ = false;
    record_->setChecked(false);
    if (recordPlaying_) timeline_.composer().stop();
    recordPlaying_ = false;
    touchedGain_.clear();
    touchedPan_.clear();
    timeline_.endAutomationRecording(cancel);
    syncValues();
}

// A new Record position while recording writes each moved strip there (frame-accurate keyframes,
// p.721), between the marks; past the end, or when the playback it started stops, it stops.
void AudioMixer::recordTick() {
    if (!recording_) return;
    Composer& composer = timeline_.composer();
    const std::int64_t at = composer.state(MonitorId::Record).position();
    if (at != lastWritten_) {
        lastWritten_ = at;
        if (inRecordRange()) {
            for (const QString& t : std::as_const(touchedGain_)) {
                if (const MixerStrip* s = strips_.value(t)) timeline_.writeAutomation(t, model::AutomationParam::Volume, s->gain());
            }
            for (const QString& t : std::as_const(touchedPan_)) {
                if (const MixerStrip* s = strips_.value(t)) timeline_.writeAutomation(t, model::AutomationParam::Pan, s->pan());
            }
        }
    }
    if ((recordEnd_ && at > *recordEnd_) || (recordPlaying_ && !composer.isPlaying())) stopRecording(false);
}

bool AudioMixer::goToKeyframe(int direction) {
    if (mode_ != Mode::Auto) return false;
    const auto frame = timeline_.nextKeyframe(direction);
    if (!frame) return false;
    timeline_.composer().moveTo(MonitorId::Record, *frame);
    return true;
}

// B records in Auto mode (p.718).
void AudioMixer::keyPressEvent(QKeyEvent* event) {
    if (mode_ == Mode::Auto && event->key() == Qt::Key_B && event->modifiers() == Qt::NoModifier) {
        if (recording_) {
            stopRecording(false);
        } else {
            startRecording();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

QStringList AudioMixer::selectedTracks() const {
    QStringList out;
    for (const QString& t : order_) {
        if (timeline_.patch().isRecordSelected(t)) out << t;
    }
    return out;
}

// Each working item applies the selected strips' shown values (or removes them) through
// Timeline::setClipAudio with `onTrack`; the rest are listed disabled.
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
    case Mode::Auto: {
        // Each acts between the marks on the selected tracks (Timeline::editAutomation, p.719-720).
        using Edit = Timeline::AutomationEdit;
        using model::AutomationParam;
        auto onAuto = [this, m, tracks](const QString& text, Edit edit, std::optional<AutomationParam> param) {
            QAction* a = m->addAction(text);
            a->setEnabled(!tracks.isEmpty());
            connect(a, &QAction::triggered, this, [this, edit, param] {
                // Alt with Filter keeps only the peaks (p.719).
                const bool alt = QApplication::keyboardModifiers() & Qt::AltModifier;
                timeline_.editAutomation(edit, param, 0, alt);
            });
        };
        onAuto(tr("Filter volume automation on Track"), Edit::Filter, AutomationParam::Volume);
        onAuto(tr("Filter Pan on Track"), Edit::Filter, AutomationParam::Pan);
        QAction* adjust = m->addAction(tr("Adjust Volume/Pan on Track..."));
        adjust->setEnabled(!tracks.isEmpty());
        connect(adjust, &QAction::triggered, this, [this] {
            const auto by = adjustAsk_ ? adjustAsk_() : askAdjust(this);
            if (!by) return;
            if (by->first != 0) timeline_.editAutomation(Edit::Adjust, AutomationParam::Volume, by->first);
            if (by->second != 0) timeline_.editAutomation(Edit::Adjust, AutomationParam::Pan, by->second);
        });
        onAuto(tr("Remove volume automation on Track"), Edit::Remove, AutomationParam::Volume);
        onAuto(tr("Remove Pan on Track"), Edit::Remove, AutomationParam::Pan);
        onAuto(tr("Remove Volume/Pan on Track"), Edit::Remove, std::nullopt);
        onAuto(tr("Remove Hidden Volume Automation on Track - Global"), Edit::RemoveHidden, AutomationParam::Volume);
        onAuto(tr("Remove Hidden Pan on Track - Global"), Edit::RemoveHidden, AutomationParam::Pan);
        onAuto(tr("Remove Hidden Pan/Volume on Track - Global"), Edit::RemoveHidden, std::nullopt);
        later(tr("Calibrate Hardware Sliders"));
        later(tr("Set Display Options..."));
        break;
    }
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

constexpr int kMarks = 10; // intervals between the marks of kDigital

} // namespace

// The scale's marks are equally spaced, as pictured; levels between two marks are interpolated.
double AudioMeters::scalePosition(double db) {
    if (db >= 0) return 0;
    if (db <= -90) return 1;
    for (int i = 0; i < kMarks; ++i) {
        if (db <= kDigital[i] && db >= kDigital[i + 1]) {
            return (i + (kDigital[i] - db) / (kDigital[i] - kDigital[i + 1])) / kMarks;
        }
    }
    return 1;
}

// The inverse of scalePosition(): which interval the position falls in, then where in it.
double AudioMeters::levelAt(double position) {
    if (position <= 0) return 0;
    if (position >= 1) return -90;
    const double at = position * kMarks;
    const int i = std::min(static_cast<int>(at), kMarks - 1);
    return kDigital[i] - (at - i) * (kDigital[i] - kDigital[i + 1]);
}

// Starts with every level and peak at the bottom of the scale. The default clock is a monotonic
// timer started here.
AudioMeters::AudioMeters(int channels, QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("audioMeters"));
    for (int i = 0; i < channels; ++i) {
        levels_ << -90;
        peaks_ << -90;
        peakTimes_ << 0;
    }
    auto started = std::make_shared<QElapsedTimer>();
    started->start();
    clock_ = [started] { return started->elapsed(); };
    setMinimumHeight(220);
}

void AudioMeters::setHold(Hold hold) {
    hold_ = hold;
}

void AudioMeters::setClock(Clock clock) {
    if (clock) clock_ = std::move(clock);
}

QSize AudioMeters::sizeHint() const {
    return QSize(60 + channels() * 14, 260);
}

// A higher level always raises the peak and restarts its hold time. With Peak Hold, a peak older
// than kPeakHoldMs falls to the current level; with Infinite Hold it stays until resetPeaks().
void AudioMeters::setLevel(int channel, double db) {
    if (channel < 0 || channel >= levels_.size()) return;
    levels_[channel] = db;
    const qint64 now = clock_();
    if (db >= peaks_.at(channel) || (hold_ == Hold::PeakHold && now - peakTimes_.at(channel) >= kPeakHoldMs)) {
        peaks_[channel] = db;
        peakTimes_[channel] = now;
    }
    update();
}

void AudioMeters::setPeak(int channel, double db) {
    if (channel < 0 || channel >= peaks_.size()) return;
    peaks_[channel] = db;
    peakTimes_[channel] = clock_();
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

// A bar per channel between the scales, filled from the bottom up to its level and coloured line by
// line, and a line at its peak.
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
        const int yy = top + static_cast<int>(scalePosition(d) * span);
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
        const int levelY = top + static_cast<int>(scalePosition(levels_.at(c)) * span);
        // The bar, coloured by band: each line's level read back through the scale's own mapping,
        // so the bands change colour at the marks they belong to.
        for (int yy = levelY; yy < bottom; ++yy) {
            const double db = levelAt(static_cast<double>(yy - top) / span);
            p.setPen(colourFor(db, reference_));
            p.drawLine(x, yy, x + barWidth - 1, yy);
        }
        const int peakY = top + static_cast<int>(scalePosition(peaks_.at(c)) * span);
        if (peaks_.at(c) > -90) {
            p.setPen(colourFor(peaks_.at(c), reference_));
            p.drawLine(x, peakY, x + barWidth - 1, peakY);
        }
    }
}

// ---------------------------------------------------------------- AudioTool

// RP, the In/Out buttons and PH with its menu above the meters.
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
    connect(peak, &QAction::triggered, this, [this] { meters_->setHold(Hold::PeakHold); });
    connect(infinite, &QAction::triggered, this, [this] { meters_->setHold(Hold::Infinite); });
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
