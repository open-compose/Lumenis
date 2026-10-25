#include "lumenis/ui/audio_eq.h"

#include "lumenis/model/audio_eq.h"
#include "lumenis/ui/timeline.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Audio EQ tool (audio_eq.h; Editing Guide p.736-745; docs/spec/audio-eq.md): the response
// graph, the band sliders with their clickable scales, the EQ Range slider, and the tool itself.
// The bands' limits and filters are in src/model/audio_eq.*; the effect is set on segments through
// Timeline::setEq; playback runs the filters in media::AudioMix (src/media/playback.cpp).

namespace lumenis::ui {

namespace {

constexpr double kGraphLow = 20;    // the low shelf works "from 20 Hz" (p.738)
constexpr double kGraphHigh = 20000; // and the high shelf "to 20 kHz"

QToolButton* smallButton(const QString& text, const QString& name, QWidget* parent, bool checkable = false) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setObjectName(name);
    b->setCheckable(checkable);
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

// Sliders count tenths of a dB.
int sliderValue(double db) {
    return static_cast<int>(std::lround(db * 10));
}

// The numbers along a band slider's edge (picture on p.737); clicking one sets it (p.739).
class EqScale : public QWidget {
public:
    EqScale(QSlider* slider, QWidget* parent) : QWidget(parent), slider_(slider) {
        setFixedWidth(26);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        QFont f = font();
        // The theme sets sizes in pixels or points (theme.cpp).
        if (f.pixelSize() > 0) {
            f.setPixelSize(std::max(8, f.pixelSize() * 4 / 5));
        } else {
            f.setPointSizeF(f.pointSizeF() * 0.8);
        }
        p.setFont(f);
        for (int db : kMarks) {
            const QString text = db > 0 ? QStringLiteral("+%1").arg(db) : QString::number(db);
            p.drawText(QRectF(0, yFor(db) - 7, width() - 2, 14), Qt::AlignRight | Qt::AlignVCenter, text);
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        int best = 0;
        for (int db : kMarks) {
            if (std::abs(yFor(db) - event->position().y()) < std::abs(yFor(best) - event->position().y())) best = db;
        }
        slider_->setValue(sliderValue(best));
    }

private:
    static constexpr int kMarks[] = {15, 5, 0, -5, -15};
    // The slider's handle runs between half a handle from each end; ours: about 8 pixels.
    double yFor(int db) const {
        const double top = 8;
        const double bottom = height() - 8.0;
        return top + (model::kEqMax - db) / (model::kEqMax - model::kEqMin) * (bottom - top);
    }
    QSlider* slider_;
};

} // namespace

// ---------------------------------------------------------------- graph

EqGraph::EqGraph(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("eqGraph"));
    setMinimumSize(240, 90);
}

void EqGraph::setEq(const model::AudioEq& eq) {
    eq_ = eq;
    update();
}

double EqGraph::xFor(double hz) const {
    return std::log(hz / kGraphLow) / std::log(kGraphHigh / kGraphLow) * (width() - 1);
}

double EqGraph::yFor(double db) const {
    return (model::kEqMax - db) / (model::kEqMax - model::kEqMin) * (height() - 1);
}

void EqGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setRenderHint(QPainter::Antialiasing);
    const QColor grid = palette().color(QPalette::Mid);
    p.setPen(QPen(grid, 1, Qt::DotLine));
    for (double hz : {50.0, 100.0, 500.0, 3000.0, 12000.0}) p.drawLine(QPointF(xFor(hz), 0), QPointF(xFor(hz), height()));
    // The horizontal centre line is 0 dB (p.738).
    p.setPen(QPen(grid, 1));
    p.drawLine(QPointF(0, yFor(0)), QPointF(width(), yFor(0)));
    QPainterPath curve;
    for (int x = 0; x < width(); ++x) {
        const double hz = kGraphLow * std::pow(kGraphHigh / kGraphLow, x / std::max(1.0, width() - 1.0));
        const double db = std::clamp(model::eqResponse(eq_, hz), model::kEqMin, model::kEqMax);
        if (x == 0) {
            curve.moveTo(x, yFor(db));
        } else {
            curve.lineTo(x, yFor(db));
        }
    }
    p.setPen(QPen(eq_.in ? QColor(0xff4fb0b0) : grid, 2));
    p.drawPath(curve);
}

// ---------------------------------------------------------------- tool

AudioEqTool::AudioEqTool(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("audioEq"));

    // The top part (picture and table on p.736-737).
    auto* bar = new QHBoxLayout;
    bar->setSpacing(3);
    bar->addWidget(laterButton(tr("EQ"), QStringLiteral("effectIcon"), tr("Effect icon: drag to a bin for a template"), this));
    bar->addWidget(laterButton(tr("Loop"), QStringLiteral("audioLoopPlay"), tr("Audio Loop Play"), this));
    bar->addWidget(laterButton(tr("Render"), QStringLiteral("renderEffect"), tr("Render Effect"), this));
    fast_ = smallButton(QStringLiteral("☰"), QStringLiteral("fastMenu"), this);
    fast_->setToolTip(tr("Fast Menu"));
    fast_->setPopupMode(QToolButton::InstantPopup);
    fast_->setMenu(new QMenu(fast_));
    connect(fast_->menu(), &QMenu::aboutToShow, this, [this] { fastMenu(); });
    bar->addWidget(fast_);
    track_ = smallButton(QString(), QStringLiteral("trackSelection"), this);
    track_->setToolTip(tr("Track Selection Menu"));
    track_->setPopupMode(QToolButton::InstantPopup);
    track_->setMenu(new QMenu(track_));
    // The menu lists the audio tracks; choosing one selects or deselects it in the Timeline
    // (p.737). With several selected, they carry plus signs.
    connect(track_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* menu = track_->menu();
        menu->clear();
        const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        if (!rec || rec->item.kind != model::BinItemKind::Sequence) return;
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        const QStringList selected = tracks();
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Audio) continue;
            const QString name = t.name();
            const bool on = selected.contains(name);
            QAction* a = menu->addAction((on && selected.size() > 1 ? QStringLiteral("+ ") : QString()) + name, this,
                                         [this, name] { timeline_.toggleRecordTrack(name); });
            a->setCheckable(true);
            a->setChecked(on);
        }
    });
    bar->addWidget(track_);
    bar->addStretch(1);
    // In: the current EQ effect is on (inline) when highlighted (p.737-738).
    in_ = smallButton(tr("In"), QStringLiteral("eqIn"), this, true);
    in_->setToolTip(tr("Enable/Disable EQ Effect"));
    connect(in_, &QToolButton::clicked, this, [this](bool on) {
        model::AudioEq e = eq_;
        e.in = on;
        setEq(e);
    });
    bar->addWidget(in_);
    bypass_ = smallButton(tr("Bypass"), QStringLiteral("bypassRtEq"), this, true);
    bypass_->setToolTip(tr("Bypass RT EQ"));
    connect(bypass_, &QToolButton::toggled, this, &AudioEqTool::bypassChanged);
    bar->addWidget(bypass_);

    // The bottom part (picture on p.737): the EQ Parameter display, the three band sliders and
    // their 3-band buttons, the EQ Range slider and the graph.
    auto* bands = new QHBoxLayout;
    bands->setSpacing(6);
    const char* names[] = {"low", "mid", "high"};
    const QString tips[] = {tr("Low shelf"), tr("Parametric midrange"), tr("High shelf")};
    for (int b = 0; b < 3; ++b) {
        auto* column = new QVBoxLayout;
        column->setSpacing(2);
        auto* box = new QLineEdit(QStringLiteral("0.0"), this);
        box->setObjectName(QStringLiteral("eqValue:") + QLatin1String(names[b]));
        box->setToolTip(tips[b]);
        box->setFixedWidth(52);
        box->setAlignment(Qt::AlignCenter);
        boxes_[b] = box;
        auto* slider = new QSlider(Qt::Vertical, this);
        slider->setObjectName(QStringLiteral("eqSlider:") + QLatin1String(names[b]));
        slider->setToolTip(tips[b]);
        slider->setRange(sliderValue(model::kEqMin), sliderValue(model::kEqMax));
        slider->setSingleStep(1);
        slider->setPageStep(10);
        slider->setMinimumHeight(140);
        slider->installEventFilter(this);
        sliders_[b] = slider;
        auto* sliderRow = new QHBoxLayout;
        sliderRow->setSpacing(0);
        sliderRow->addWidget(new EqScale(slider, this));
        sliderRow->addWidget(slider);
        auto* button = smallButton(QString(), QStringLiteral("eqBand:") + QLatin1String(names[b]), this);
        bandButtons_[b] = button;
        column->addWidget(box, 0, Qt::AlignHCenter);
        column->addLayout(sliderRow);
        column->addWidget(button, 0, Qt::AlignHCenter);
        bands->addLayout(column);

        auto valueOf = [this, b](double db) {
            model::AudioEq e = eq_;
            (b == Low ? e.low : b == Mid ? e.mid : e.high) = db;
            return e;
        };
        // Dragging shows the value; releasing it (or a key or click) sets it.
        connect(slider, &QSlider::valueChanged, this, [this, slider, valueOf](int v) {
            if (syncing_) return;
            setEq(valueOf(v / 10.0), !slider->isSliderDown());
        });
        connect(slider, &QSlider::sliderReleased, this, [this, slider, valueOf] {
            if (!syncing_) setEq(valueOf(slider->value() / 10.0));
        });
        connect(box, &QLineEdit::editingFinished, this, [this, box, valueOf] {
            bool ok = false;
            const double db = box->text().trimmed().remove(QStringLiteral("dB")).toDouble(&ok);
            if (ok) {
                setEq(valueOf(db));
            } else {
                showEq(eq_);
            }
        });
    }
    // The low and high shelves' turnovers and the midrange's bandwidth (p.738).
    connect(bandButtons_[Low], &QToolButton::clicked, this, [this] {
        model::AudioEq e = eq_;
        const auto& t = model::kLowTurnovers;
        const auto it = std::find(t.begin(), t.end(), e.lowTurnover);
        e.lowTurnover = it == t.end() || it + 1 == t.end() ? t.front() : *(it + 1);
        setEq(e);
    });
    connect(bandButtons_[Mid], &QToolButton::clicked, this, [this] {
        model::AudioEq e = eq_;
        e.wideMid = !e.wideMid;
        setEq(e);
    });
    connect(bandButtons_[High], &QToolButton::clicked, this, [this] {
        model::AudioEq e = eq_;
        const auto& t = model::kHighTurnovers;
        const auto it = std::find(t.begin(), t.end(), e.highTurnover);
        e.highTurnover = it == t.end() || it + 1 == t.end() ? t.front() : *(it + 1);
        setEq(e);
    });

    range_ = new QSlider(Qt::Horizontal, this);
    range_->setObjectName(QStringLiteral("eqRange"));
    range_->setToolTip(tr("EQ Range: the centre of the parametric midrange"));
    range_->setRange(0, kRangeSteps);
    range_->setPageStep(kRangeSteps / 20);
    connect(range_, &QSlider::valueChanged, this, [this](int v) {
        if (syncing_) return;
        model::AudioEq e = eq_;
        e.midFrequency = rangeFrequency(v);
        setEq(e, !range_->isSliderDown());
    });
    connect(range_, &QSlider::sliderReleased, this, [this] {
        if (syncing_) return;
        model::AudioEq e = eq_;
        e.midFrequency = rangeFrequency(range_->value());
        setEq(e);
    });
    readout_ = new QLabel(this);
    readout_->setObjectName(QStringLiteral("eqRangeReadout"));
    readout_->setMinimumWidth(60);
    auto* rangeRow = new QHBoxLayout;
    auto* lowEnd = new QLabel(frequencyText(model::kEqRangeMin), this);
    auto* highEnd = new QLabel(frequencyText(model::kEqRangeMax), this);
    rangeRow->addWidget(lowEnd);
    rangeRow->addWidget(range_, 1);
    rangeRow->addWidget(highEnd);
    rangeRow->addWidget(readout_);

    graph_ = new EqGraph(this);
    auto* right = new QVBoxLayout;
    right->addWidget(graph_, 1);
    right->addLayout(rangeRow);
    auto* body = new QHBoxLayout;
    body->addLayout(bands);
    body->addLayout(right, 1);

    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("eqMessage"));
    message_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(bar);
    layout->addLayout(body, 1);
    layout->addWidget(message_);

    connect(&timeline_, &Timeline::changed, this, &AudioEqTool::refresh);
    connect(&timeline_.composer(), &Composer::changed, this, &AudioEqTool::refresh);
    refresh();
}

QStringList AudioEqTool::tracks() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QStringList out;
    for (const QString& t : timeline_.patch().selectedRecordTracks(seq)) {
        const model::Track* track = seq.track(t);
        if (track && track->kind == model::TrackKind::Audio) out << t;
    }
    return out;
}

QString AudioEqTool::track() const {
    const QStringList t = tracks();
    return t.isEmpty() ? QString() : t.first();
}

bool AudioEqTool::bypassed() const {
    return bypass_->isChecked();
}

void AudioEqTool::setBypassed(bool on) {
    bypass_->setChecked(on);
}

int AudioEqTool::rangePosition(double hz) {
    const double f = std::clamp(hz, model::kEqRangeMin, model::kEqRangeMax);
    return static_cast<int>(std::lround(std::log(f / model::kEqRangeMin) / std::log(model::kEqRangeMax / model::kEqRangeMin) * kRangeSteps));
}

// Rounded to whole hertz, and to 10 Hz from 1 kHz up.
double AudioEqTool::rangeFrequency(int position) {
    const double f = model::kEqRangeMin * std::pow(model::kEqRangeMax / model::kEqRangeMin,
                                                   std::clamp(position, 0, kRangeSteps) / static_cast<double>(kRangeSteps));
    return f < 1000 ? std::round(f) : std::round(f / 10) * 10;
}

QString AudioEqTool::frequencyText(double hz) {
    if (hz < 1000) return QStringLiteral("%1 Hz").arg(std::lround(hz));
    return QStringLiteral("%1 kHz").arg(QString::number(hz / 1000, 'g', 3));
}

void AudioEqTool::showEq(const model::AudioEq& e) {
    syncing_ = true;
    const double gains[] = {e.low, e.mid, e.high};
    for (int b = 0; b < 3; ++b) {
        if (typed_.isEmpty()) boxes_[b]->setText(QString::number(gains[b], 'f', 1));
        if (!sliders_[b]->isSliderDown()) sliders_[b]->setValue(sliderValue(gains[b]));
    }
    bandButtons_[Low]->setText(frequencyText(e.lowTurnover));
    bandButtons_[Mid]->setText(e.wideMid ? tr("2 Oct") : tr("1/4 Oct"));
    bandButtons_[High]->setText(frequencyText(e.highTurnover));
    if (!range_->isSliderDown()) range_->setValue(rangePosition(e.midFrequency));
    readout_->setText(frequencyText(e.midFrequency));
    in_->setChecked(e.in);
    graph_->setEq(e);
    syncing_ = false;
}

void AudioEqTool::fail(const QString& text) {
    message_->setText(text);
}

void AudioEqTool::refresh() {
    const QStringList t = tracks();
    track_->setText(t.isEmpty() ? tr("No track") : t.first() + (t.size() > 1 ? QStringLiteral(" +") : QString()));
    current_.reset();
    if (!t.isEmpty()) {
        if (const auto s = timeline_.audioSegmentAt(t.first())) current_ = s->eq;
    }
    // A segment without an EQ shows the last values set, so they can be applied (ours).
    if (current_) eq_ = *current_;
    showEq(eq_);
}

void AudioEqTool::setEq(const model::AudioEq& eq, bool done) {
    eq_ = model::clampEq(eq);
    showEq(eq_);
    if (!done) return;
    message_->clear();
    const QStringList t = tracks();
    if (t.isEmpty()) return fail(tr("Select an audio track in the Timeline."));
    if (!timeline_.setEq(t, eq_, false, tr("Audio EQ"))) fail(timeline_.lastMessage());
}

bool AudioEqTool::setEqInOut() {
    message_->clear();
    const QString t = track();
    if (t.isEmpty()) {
        fail(tr("Select an audio track in the Timeline."));
        return false;
    }
    // "Set EQ In/Out applies only to the audio track currently selected by the Audio EQ tool"
    // (p.741), with the current clip's EQ, or none.
    const auto s = timeline_.audioSegmentAt(t);
    if (!s) {
        fail(tr("There is no audio segment at the position on %1.").arg(t));
        return false;
    }
    if (!timeline_.setEq({t}, s->eq, true, tr("Set EQ"))) {
        fail(timeline_.lastMessage());
        return false;
    }
    return true;
}

bool AudioEqTool::removeEqs(bool enabledTracks) {
    message_->clear();
    const QStringList t = enabledTracks ? tracks() : QStringList{track()};
    if (t.isEmpty() || t.first().isEmpty()) {
        fail(tr("Select an audio track in the Timeline."));
        return false;
    }
    if (!timeline_.setEq(t, std::nullopt, true, tr("Remove EQs"))) {
        fail(timeline_.lastMessage());
        return false;
    }
    return true;
}

// The Fast menu (picture on p.744).
QMenu* AudioEqTool::fastMenu() {
    QMenu* menu = fast_->menu();
    menu->clear();
    const bool any = !track().isEmpty();
    menu->addAction(tr("Set EQ - In/Out"), this, [this] { setEqInOut(); })->setEnabled(any);
    menu->addAction(tr("Remove EQs On Track - In/Out"), this, [this] { removeEqs(false); })->setEnabled(any);
    menu->addAction(tr("Remove EQs On Enabled Tracks - In/Out"), this, [this] { removeEqs(true); })->setEnabled(any);
    menu->addSeparator();
    // The predefined templates (p.743-744). Their values are not documented; they come later.
    const char* templates[] = {"Female Voice with Presence",
                               "Male Voice with Presence",
                               "Music High Boost",
                               "Music High Boost with Mid-Rollout",
                               "Music Low Boost",
                               "Music Low Boost with Mid-Rollout",
                               "NTSC Hum Buster",
                               "PAL Hum Buster",
                               "Tape Hiss Filter",
                               "Tape Hiss Filter with High-End Boost",
                               "Telephone EQ \"A\"",
                               "Telephone EQ \"B\""};
    for (const char* name : templates) {
        QAction* a = menu->addAction(tr(name));
        a->setEnabled(false);
        a->setToolTip(tr("Not available yet"));
    }
    return menu;
}

bool AudioEqTool::eventFilter(QObject* watched, QEvent* event) {
    for (int b = 0; b < 3; ++b) {
        if (watched != sliders_[b] || event->type() != QEvent::KeyPress) continue;
        auto* key = static_cast<QKeyEvent*>(event);
        const QString text = key->text();
        // "Values are cumulative until you press Enter" (p.739).
        if (text.size() == 1 && (text.at(0).isDigit() || text == QLatin1String("-") || text == QLatin1String("."))) {
            typed_ += text;
            boxes_[b]->setText(typed_);
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !typed_.isEmpty()) {
            bool ok = false;
            const double db = typed_.toDouble(&ok);
            typed_.clear();
            if (ok) {
                model::AudioEq e = eq_;
                (b == Low ? e.low : b == Mid ? e.mid : e.high) = db;
                setEq(e);
            } else {
                showEq(eq_);
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape && !typed_.isEmpty()) {
            typed_.clear();
            showEq(eq_);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lumenis::ui
