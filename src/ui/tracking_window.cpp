// The Tracking window (FX Guide p.253-255, p.262-269; docs/spec/tracking.md). See tracking_window.h. Its
// controls change the Effect Editor's shown effect (EffectEditor::changeTracking and friends); it shows what the
// Effect Editor holds.
#include "lumenis/ui/tracking_window.h"

#include "lumenis/model/tracking.h"
#include "lumenis/ui/effect_editor.h"

#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

QToolButton* toggle(const QString& name, const QString& text, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setAutoRaise(true);
    return b;
}

} // namespace

// ---------------------------------------------------------------- TrackerTimeline

TrackerTimeline::TrackerTimeline(EffectEditor& editor, int index, QWidget* parent) : QWidget(parent), editor_(editor), index_(index) {
    setObjectName(QStringLiteral("trackertimeline:%1").arg(index));
    setMinimumSize(120, 22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TrackerTimeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const model::TrackingSetup setup = editor_.tracking();
    if (index_ >= setup.trackers.size()) return;
    const model::Tracker& t = setup.trackers.at(index_);
    const std::int64_t length = std::max<std::int64_t>(editor_.span().length, 1);
    auto x = [&](double frame) { return frame / static_cast<double>(length) * width(); };
    // "Black Line at top of timeline: Tracking data exists ... Red Line at bottom of timeline: Tracking data does
    // not exist for this part of the segment" (p.267).
    for (std::int64_t f = 0; f < length; ++f) {
        const QRectF cell(x(static_cast<double>(f)), 0.0, x(static_cast<double>(f + 1)) - x(static_cast<double>(f)) + 0.5, height());
        if (t.pointAt(f)) {
            p.fillRect(QRectF(cell.left(), 2, cell.width(), 3), Qt::black);
        } else {
            p.fillRect(QRectF(cell.left(), height() - 5, cell.width(), 3), QColor(200, 30, 30));
        }
    }
    // "Vertical Bar: Marks the start of a new tracking region"; the reference frame's red diamond (p.267-269).
    p.setPen(QPen(palette().color(QPalette::WindowText), 1));
    for (qsizetype r = 1; r < t.regions.size(); ++r) p.drawLine(QPointF(x(static_cast<double>(t.regions.at(r).start)), 0), QPointF(x(static_cast<double>(t.regions.at(r).start)), height()));
    p.setRenderHint(QPainter::Antialiasing);
    const double rx = x(static_cast<double>(t.reference) + 0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(220, 30, 30));
    p.drawPolygon(QPolygonF{QPointF(rx, height() / 2.0 - 4), QPointF(rx + 4, height() / 2.0), QPointF(rx, height() / 2.0 + 4), QPointF(rx - 4, height() / 2.0)});
    // The position.
    const double px = x(static_cast<double>(editor_.positionFrame()) + 0.5);
    p.fillRect(QRectF(px - 0.5, 0, 1, height()), QColor(80, 140, 255));
}

// ---------------------------------------------------------------- TrackingWindow

TrackingWindow::TrackingWindow(EffectEditor& editor, QWidget* parent) : QWidget(parent, Qt::Tool), editor_(editor) {
    setObjectName(QStringLiteral("trackingWindow"));
    setWindowTitle(tr("Tracking"));
    // Setup Tracking (p.254, p.262-263).
    start_ = new QToolButton(this);
    start_->setObjectName(QStringLiteral("trackingStart"));
    start_->setText(tr("Start Tracking"));
    connect(start_, &QToolButton::clicked, this, [this] { editor_.startTracking(); });
    newTracker_ = new QToolButton(this);
    newTracker_->setObjectName(QStringLiteral("trackingNew"));
    newTracker_->setText(tr("New Tracker"));
    connect(newTracker_, &QToolButton::clicked, this, [this] {
        editor_.changeTracking(tr("New Tracker"), [](model::TrackingSetup& s) {
            if (const auto t = model::newTracker(s)) s.trackers << *t;
        });
    });
    engine_ = new QComboBox(this);
    engine_->setObjectName(QStringLiteral("trackingEngine"));
    for (const char* name : model::kTrackingEngines) engine_->addItem(QLatin1String(name));
    connect(engine_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (!syncing_) editor_.changeTracking(tr("Tracking Engine"), [&](model::TrackingSetup& s) { s.engine = name; });
    });
    track_ = new QComboBox(this);
    track_->setObjectName(QStringLiteral("trackingTrack"));
    track_->addItems({tr("Track Background"), tr("Track Foreground")});
    connect(track_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!syncing_) editor_.changeTracking(tr("Track Foreground"), [&](model::TrackingSetup& s) { s.foreground = i == 1; });
    });
    // Display Tracking Data (p.268-269).
    pointRange_ = new QComboBox(this);
    pointRange_->setObjectName(QStringLiteral("trackingPointRange"));
    pointRange_->addItems({tr("Current"), tr("In to Out"), tr("All")});
    connect(pointRange_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!syncing_) editor_.setPointRange(static_cast<EffectEditor::PointRange>(i));
    });
    display_ = new QComboBox(this);
    display_->setObjectName(QStringLiteral("trackingDisplay"));
    display_->addItems({tr("Tracking Data"), tr("Effect Results")});
    connect(display_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!syncing_) editor_.setShowsTrackingData(i == 0);
    });

    auto* top = new QHBoxLayout;
    top->addWidget(start_);
    top->addWidget(newTracker_);
    top->addWidget(new QLabel(tr("Tracking Engine"), this));
    top->addWidget(engine_);
    top->addWidget(track_);
    top->addStretch(1);
    auto* display = new QHBoxLayout;
    display->addWidget(new QLabel(tr("Point Range"), this));
    display->addWidget(pointRange_);
    display->addWidget(new QLabel(tr("Display"), this));
    display->addWidget(display_);
    display->addStretch(1);
    rows_ = new QVBoxLayout;
    rows_->setSpacing(2);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(display);
    layout->addLayout(rows_);
    layout->addStretch(1);
    resize(640, 220);
    refresh();
}

QWidget* TrackingWindow::control(const QString& kind, int index) const {
    return findChild<QWidget*>(QStringLiteral("tracker%1:%2").arg(kind).arg(index));
}

void TrackingWindow::rebuildRows() {
    while (QLayoutItem* item = rows_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    const model::TrackingSetup setup = editor_.tracking();
    for (int i = 0; i < setup.trackers.size(); ++i) {
        auto* row = new QWidget(this);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);
        // "The button is purple when active and gray when inactive"; Alt+click enables or disables them all
        // (p.254, p.260).
        auto* enable = toggle(QStringLiteral("trackerenable:%1").arg(i), tr("En"), tr("Enable Tracker"), row);
        connect(enable, &QToolButton::clicked, this, [this, i](bool on) {
            const bool all = QApplication::keyboardModifiers() & Qt::AltModifier;
            editor_.changeTracking(tr("Enable Tracker"), [&](model::TrackingSetup& s) {
                for (int k = 0; k < s.trackers.size(); ++k) {
                    if (all || k == i) s.trackers[k].enabled = on;
                }
            });
        });
        auto* offset = toggle(QStringLiteral("trackeroffset:%1").arg(i), tr("Of"), tr("Offset Tracking"), row);
        connect(offset, &QToolButton::clicked, this, [this, i](bool on) {
            editor_.changeTracking(tr("Offset Tracking"), [&](model::TrackingSetup& s) { s.trackers[i].offset = on; });
        });
        // "SteadyGlide and Smoothing are mutually exclusive" (p.270).
        auto* steady = toggle(QStringLiteral("trackersteadyglide:%1").arg(i), tr("SG"), tr("SteadyGlide"), row);
        auto* smooth = toggle(QStringLiteral("trackersmoothing:%1").arg(i), tr("Sm"), tr("Smoothing"), row);
        auto process = [this, i](model::TrackProcess which, bool on) {
            editor_.changeTracking(tr("Smoothing"), [&](model::TrackingSetup& s) { s.trackers[i].process = on ? which : model::TrackProcess::None; });
        };
        connect(steady, &QToolButton::clicked, this, [process](bool on) { process(model::TrackProcess::SteadyGlide, on); });
        connect(smooth, &QToolButton::clicked, this, [process](bool on) { process(model::TrackProcess::Smoothing, on); });
        auto* value = new QSpinBox(row);
        value->setObjectName(QStringLiteral("trackervalue:%1").arg(i));
        value->setRange(1, 100);
        value->setToolTip(tr("Smoothing Value"));
        connect(value, &QSpinBox::editingFinished, this, [this, i, value] {
            editor_.changeTracking(tr("Smoothing Value"), [&](model::TrackingSetup& s) { s.trackers[i].smoothing = value->value(); });
        });
        auto* color = new QToolButton(row);
        color->setObjectName(QStringLiteral("trackercolor:%1").arg(i));
        color->setToolTip(tr("Tracker Color"));
        color->setFixedSize(18, 14);
        connect(color, &QToolButton::clicked, this, [this, i] {
            const QColor c = QColorDialog::getColor(QColor(editor_.tracking().trackers.value(i).color), this);
            if (c.isValid()) editor_.changeTracking(tr("Tracker Color"), [&](model::TrackingSetup& s) { s.trackers[i].color = c.name(); });
        });
        auto* name = new QLineEdit(row);
        name->setObjectName(QStringLiteral("trackername:%1").arg(i));
        name->setFixedWidth(110);
        connect(name, &QLineEdit::editingFinished, this, [this, i, name] {
            const QString was = editor_.tracking().trackers.value(i).name;
            if (name->text() != was && !editor_.renameTracker(was, name->text())) name->setText(was);
        });
        auto* timeline = new TrackerTimeline(editor_, i, row);
        auto* close = new QToolButton(row);
        close->setObjectName(QStringLiteral("trackerclose:%1").arg(i));
        close->setText(QStringLiteral("x"));
        close->setToolTip(tr("Close Tracker"));
        close->setAutoRaise(true);
        connect(close, &QToolButton::clicked, this, [this, i] { editor_.deleteTracker(editor_.tracking().trackers.value(i).name); });
        for (QWidget* w : {static_cast<QWidget*>(enable), static_cast<QWidget*>(offset), static_cast<QWidget*>(steady), static_cast<QWidget*>(smooth),
                           static_cast<QWidget*>(value), static_cast<QWidget*>(color), static_cast<QWidget*>(name)}) {
            h->addWidget(w);
        }
        h->addWidget(timeline, 1);
        h->addWidget(close);
        rows_->addWidget(row);
    }
}

void TrackingWindow::refresh() {
    const model::TrackingSetup setup = editor_.tracking();
    QStringList names;
    for (const model::Tracker& t : setup.trackers) names << t.name;
    if (names.size() != shown_.size()) {
        shown_ = names;
        rebuildRows();
    }
    shown_ = names;
    syncing_ = true;
    engine_->setCurrentText(setup.engine);
    track_->setCurrentIndex(setup.foreground ? 1 : 0);
    pointRange_->setCurrentIndex(static_cast<int>(editor_.pointRange()));
    display_->setCurrentIndex(editor_.showsTrackingData() ? 0 : 1);
    for (int i = 0; i < setup.trackers.size(); ++i) {
        const model::Tracker& t = setup.trackers.at(i);
        if (auto* b = qobject_cast<QToolButton*>(control(QStringLiteral("enable"), i))) b->setChecked(t.enabled);
        if (auto* b = qobject_cast<QToolButton*>(control(QStringLiteral("offset"), i))) b->setChecked(t.offset);
        if (auto* b = qobject_cast<QToolButton*>(control(QStringLiteral("steadyglide"), i))) b->setChecked(t.process == model::TrackProcess::SteadyGlide);
        if (auto* b = qobject_cast<QToolButton*>(control(QStringLiteral("smoothing"), i))) b->setChecked(t.process == model::TrackProcess::Smoothing);
        if (auto* v = qobject_cast<QSpinBox*>(control(QStringLiteral("value"), i))) v->setValue(t.smoothing);
        if (auto* n = qobject_cast<QLineEdit*>(control(QStringLiteral("name"), i)); n && !n->hasFocus()) n->setText(t.name);
        if (auto* c = qobject_cast<QToolButton*>(control(QStringLiteral("color"), i))) {
            c->setStyleSheet(QStringLiteral("background-color: %1;").arg(t.color));
        }
        if (QWidget* tl = control(QStringLiteral("timeline"), i)) tl->update();
    }
    start_->setEnabled(!setup.trackers.isEmpty());
    newTracker_->setEnabled(setup.trackers.size() < model::kMaxTrackers && !editor_.isBlank());
    syncing_ = false;
}

} // namespace lumenis::ui
