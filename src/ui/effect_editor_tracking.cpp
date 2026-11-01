// The Effect Editor's motion tracking (ROADMAP 6.7c2; FX Guide ch.10, p.250-269; docs/spec/tracking.md): the
// tracking controls in the parameter list (enable buttons, tracker selectors, Axis, Stabilize Tracking), the
// trackers' changes as undo steps, the Tracking window, the tracking boxes and data points in the Effect Preview
// and their drags, and Start Tracking running the engines (media::trackTarget) over frames MainWindow supplies.
#include "lumenis/ui/effect_editor.h"

#include "lumenis/media/compositor.h"
#include "lumenis/media/tracking.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tracking_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>

#include <algorithm>

namespace lumenis::ui {

namespace {

// Stops a tracking run: "Press the space bar. Press Ctrl+period (Windows) or Command+period (Macintosh). Click
// any element of Media Composer's interface" (p.265).
class TrackingStop : public QObject {
public:
    explicit TrackingStop(EffectEditor& editor) : editor_(editor) { qApp->installEventFilter(this); }
    ~TrackingStop() override { qApp->removeEventFilter(this); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Space || (key->key() == Qt::Key_Period && (key->modifiers() & Qt::ControlModifier))) {
                editor_.stopTracking();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            editor_.stopTracking();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    EffectEditor& editor_;
};

QString selectorName(int i) {
    return QStringLiteral("T%1").arg(i + 1);
}

} // namespace

model::TrackingSetup EffectEditor::tracking() const {
    const model::Effect* e = shownEffect();
    return e ? model::trackingOf(*e) : model::TrackingSetup{};
}

model::TrackerMapping EffectEditor::trackerMapping(model::TrackedParameter parameter) const {
    const model::Effect* e = shownEffect();
    return e ? model::trackerMapping(*e, parameter) : model::TrackerMapping{};
}

std::int64_t EffectEditor::positionFrame() const {
    return model::trackingFrame(positionTime(), span_);
}

bool EffectEditor::changeEffectParameters(const QString& description, const std::function<bool(model::Effect&)>& change) {
    if (!shown_ || !current_) return false;
    model::Effect edited = *current_;
    if (!change(edited) || edited.parameters == current_->parameters) return false;
    const QJsonObject parameters = edited.parameters;
    return changeShown(description, [&](model::Effect& e) { e.parameters = parameters; });
}

bool EffectEditor::setParameterTracking(model::TrackedParameter parameter, bool on, int selector) {
    const bool changed = changeEffectParameters(tr("Tracking"), [&](model::Effect& e) {
        if (on) return selector >= 0 ? model::enableTracking(e, parameter, selector) : model::enableTracking(e, parameter);
        if (selector < 0) return model::disableTracking(e, parameter);
        model::TrackerMapping m = model::trackerMapping(e, parameter);
        if (selector >= m.selectorOn.size() || !m.selectorOn.at(selector)) return false;
        m.selectorOn[selector] = false;
        model::setTrackerMapping(e, parameter, m);
        return true;
    });
    // "Once you enable tracking for a parameter, the Tracking window opens" (p.258).
    if (changed && on) showTrackingWindow();
    return changed;
}

bool EffectEditor::mapTracker(model::TrackedParameter parameter, int selector, const QString& tracker) {
    return changeEffectParameters(tr("Tracking"), [&](model::Effect& e) {
        model::TrackerMapping m = model::trackerMapping(e, parameter);
        if (selector < 0 || selector >= m.selectors.size()) return false;
        if (!tracker.isEmpty() && !model::trackingOf(e).tracker(tracker)) return false;
        m.selectors[selector] = tracker;
        m.selectorOn[selector] = !tracker.isEmpty();
        model::setTrackerMapping(e, parameter, m);
        return true;
    });
}

bool EffectEditor::setTrackingAxis(const QString& axis) {
    return changeEffectParameters(tr("Tracking"), [&](model::Effect& e) {
        model::TrackerMapping m = model::trackerMapping(e, model::TrackedParameter::Position);
        m.axis = axis;
        model::setTrackerMapping(e, model::TrackedParameter::Position, m);
        return true;
    });
}

bool EffectEditor::setStabilizeTracking(model::TrackedParameter parameter, bool on) {
    return changeEffectParameters(tr("Tracking"), [&](model::Effect& e) {
        model::TrackerMapping m = model::trackerMapping(e, parameter);
        m.stabilize = on;
        model::setTrackerMapping(e, parameter, m);
        return true;
    });
}

bool EffectEditor::changeTracking(const QString& description, const std::function<void(model::TrackingSetup&)>& change) {
    return changeEffectParameters(description, [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        change(setup);
        model::setTracking(e, setup);
        return true;
    });
}

bool EffectEditor::renameTracker(const QString& from, const QString& to) {
    return changeEffectParameters(tr("Tracker Name"), [&](model::Effect& e) { return model::renameTracker(e, from, to); });
}

bool EffectEditor::deleteTracker(const QString& name) {
    return changeEffectParameters(tr("Close Tracker"), [&](model::Effect& e) { return model::deleteTracker(e, name); });
}

TrackingWindow* EffectEditor::trackingWindow() {
    if (!trackingWindow_) trackingWindow_ = new TrackingWindow(*this, this);
    return trackingWindow_;
}

void EffectEditor::showTrackingWindow() {
    TrackingWindow* w = trackingWindow();
    w->refresh();
    w->show();
    w->raise();
}

void EffectEditor::setPointRange(PointRange range) {
    pointRange_ = range;
    if (trackingWindow_) trackingWindow_->refresh();
    syncPreview();
}

void EffectEditor::setShowsTrackingData(bool show) {
    showTrackingData_ = show;
    if (trackingWindow_) trackingWindow_->refresh();
    syncPreview();
}

// ---------------------------------------------------------------- the controls in the parameter list

QWidget* EffectEditor::trackingControls(model::TrackedParameter parameter, QWidget* parent) {
    const QString key = model::trackedParameterKey(parameter);
    auto* box = new QWidget(parent);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    // The parameter's enable button (p.257-258).
    auto* on = new QCheckBox(tr("On"), box);
    on->setObjectName(QStringLiteral("tracking:") + key);
    connect(on, &QCheckBox::clicked, this, [this, parameter](bool checked) { setParameterTracking(parameter, checked); });
    trackingControls_.insert(on->objectName(), on);
    layout->addWidget(on);
    // Its tracker selectors, T1 ... (the Tracking category's each with its own enable, p.258).
    for (int i = 0; i < model::selectorCount(parameter); ++i) {
        if (parameter == model::TrackedParameter::Tracking) {
            auto* selectorOn = new QCheckBox(selectorName(i), box);
            selectorOn->setObjectName(QStringLiteral("tracking:%1:On%2").arg(key).arg(i + 1));
            connect(selectorOn, &QCheckBox::clicked, this, [this, parameter, i](bool checked) { setParameterTracking(parameter, checked, i); });
            trackingControls_.insert(selectorOn->objectName(), selectorOn);
            layout->addWidget(selectorOn);
        } else {
            layout->addWidget(new QLabel(selectorName(i), box));
        }
        auto* menu = new QComboBox(box);
        menu->setObjectName(QStringLiteral("tracking:%1:%2").arg(key, selectorName(i)));
        connect(menu, &QComboBox::textActivated, this, [this, parameter, i](const QString& t) { mapTracker(parameter, i, t); });
        trackingControls_.insert(menu->objectName(), menu);
        layout->addWidget(menu, 1);
    }
    // Position's Axis menu, here for 2D effects (3D ones have their own row, p.577).
    if (parameter == model::TrackedParameter::Position && !(current_ && media::is3DEffect(*current_))) {
        auto* axis = new QComboBox(box);
        axis->setObjectName(QStringLiteral("tracking:Position:Axis"));
        axis->addItems({QStringLiteral("X and Y"), QStringLiteral("X Tracking Only"), QStringLiteral("Y Tracking Only")});
        connect(axis, &QComboBox::textActivated, this, [this](const QString& t) { setTrackingAxis(t); });
        trackingControls_.insert(axis->objectName(), axis);
        layout->addWidget(axis);
    }
    return box;
}

void EffectEditor::syncTracking(const model::Effect& effect) {
    if (trackingControls_.isEmpty()) return;
    const model::TrackingSetup setup = model::trackingOf(effect);
    QStringList names{QString()};
    for (const model::Tracker& t : setup.trackers) names << t.name;
    for (model::TrackedParameter p : {model::TrackedParameter::Position, model::TrackedParameter::Scaling, model::TrackedParameter::Rotation,
                                      model::TrackedParameter::Tracking}) {
        const QString key = model::trackedParameterKey(p);
        const model::TrackerMapping m = model::trackerMapping(effect, p);
        if (auto* on = qobject_cast<QCheckBox*>(trackingControls_.value(QStringLiteral("tracking:") + key))) {
            const QSignalBlocker block(on);
            on->setChecked(m.on);
        }
        for (int i = 0; i < m.selectors.size(); ++i) {
            if (auto* menu = qobject_cast<QComboBox*>(trackingControls_.value(QStringLiteral("tracking:%1:%2").arg(key, selectorName(i))))) {
                const QSignalBlocker block(menu);
                QStringList items;
                for (int k = 0; k < menu->count(); ++k) items << menu->itemText(k);
                if (items != names) {
                    menu->clear();
                    menu->addItems(names);
                }
                menu->setCurrentText(m.selectors.at(i));
            }
            if (auto* selectorOn = qobject_cast<QCheckBox*>(trackingControls_.value(QStringLiteral("tracking:%1:On%2").arg(key).arg(i + 1)))) {
                const QSignalBlocker block(selectorOn);
                selectorOn->setChecked(m.selectorOn.value(i));
            }
        }
        if (auto* stabilize = qobject_cast<QCheckBox*>(trackingControls_.value(QStringLiteral("tracking:%1:Stabilize").arg(key)))) {
            const QSignalBlocker block(stabilize);
            stabilize->setChecked(m.stabilize);
        }
        if (p == model::TrackedParameter::Position) {
            if (auto* axis = qobject_cast<QComboBox*>(trackingControls_.value(QStringLiteral("tracking:Position:Axis")))) {
                const QSignalBlocker block(axis);
                axis->setCurrentText(m.axis);
            }
        }
    }
}

// ---------------------------------------------------------------- the Effect Preview

void EffectEditor::fillTrackers(const model::Effect& effect, Composer::EffectPreview& preview) const {
    // "The Display menu affects what you see in the Effect Preview monitor once tracking data is available"
    // (p.269): Effect Results shows no tracking data.
    if (!showTrackingData_) return;
    const model::TrackingSetup setup = model::trackingOf(effect);
    const std::int64_t frame = positionFrame();
    for (const model::Tracker& t : setup.trackers) {
        Composer::EffectPreview::TrackerShow show;
        show.color = QColor(t.color);
        // "When this button is active for a tracker, tracking boxes and tracking data points display" (p.254).
        if (t.enabled) {
            if (pointRange_ != PointRange::Current) {
                // All the data points (In to Out's Display Markers come in 6.7c3; ours shows all meanwhile).
                for (qsizetype r = 0; r < t.regions.size(); ++r) {
                    const std::int64_t end = r + 1 < t.regions.size() ? t.regions.at(r + 1).start : std::numeric_limits<std::int64_t>::max();
                    for (const model::TrackPoint& p : t.regions.at(r).points) {
                        if (p.frame >= t.regions.at(r).start && p.frame < end) show.points << QPointF(p.x, p.y);
                    }
                }
            }
            show.current = t.pointAt(frame);
            // "Move the position indicator to the first frame or field of the segment, or to the first frame or
            // field of the tracker region. A tracking box display appears" (p.261); ours also while the region
            // has no data.
            const int r = t.regionAt(frame);
            if (r >= 0 && (frame == t.regions.at(r).start || t.regions.at(r).points.isEmpty())) {
                show.boxes = true;
                show.target = t.regions.at(r).target;
                show.search = t.regions.at(r).search;
            }
        }
        preview.trackers << show;
    }
}

bool EffectEditor::dragTrackerBox(const QString& id, const QPointF& from, const QPointF& to, bool finished) {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (!shown_ || !current_ || parts.size() != 3) {
        handleStart_.reset();
        return false;
    }
    if (!handleStart_) handleStart_ = *current_;
    const model::Effect start = *handleStart_;
    if (finished) handleStart_.reset();
    model::TrackingSetup setup = model::trackingOf(start);
    const int index = parts.at(1).toInt();
    if (index < 0 || index >= setup.trackers.size()) return false;
    model::Tracker& t = setup.trackers[index];
    const int r = t.regionAt(positionFrame());
    if (r < 0) return false;
    model::TrackerRegion& region = t.regions[r];
    const QPointF by = to - from;
    if (parts.at(2) == QLatin1String("move")) {
        // "Click anywhere within the boxes except the adjustment handles, and drag the boxes" (p.261).
        region.target.translate(by);
        region.search.translate(by);
    } else if (parts.at(2) == QLatin1String("target")) {
        // "Drag the adjustment handle for the box" (p.261). Ours: the target stays at least 1% of the frame, and
        // the search area grows to hold it.
        region.target.setBottomRight(region.target.bottomRight() + by);
        region.target.setWidth(std::max(region.target.width(), 0.01));
        region.target.setHeight(std::max(region.target.height(), 0.01));
        region.search = region.search.united(region.target);
    } else {
        region.search.setBottomRight(region.search.bottomRight() + by);
        region.search = region.search.united(region.target);
    }
    model::Effect edited = start;
    model::setTracking(edited, setup);
    if (!finished) {
        preview_ = edited;
        syncPreview();
        return true;
    }
    preview_.reset();
    if (edited.parameters == start.parameters) {
        syncPreview();
        return false;
    }
    const QJsonObject parameters = edited.parameters;
    return changeShown(tr("Tracking Boxes"), [&](model::Effect& e) { e.parameters = parameters; });
}

// ---------------------------------------------------------------- Start Tracking

bool EffectEditor::startTracking() {
    if (!shown_ || !current_ || !trackingFrames_ || tracking_) return false;
    model::Effect edited = *current_;
    model::TrackingSetup setup = model::trackingOf(edited);
    if (setup.trackers.isEmpty()) return false;
    const media::TrackingEngine engine = media::trackingEngine(setup.engine);
    const std::int64_t frame = positionFrame();
    const std::int64_t last = std::max<std::int64_t>(span_.length - 1, 0);
    const model::EffectRef ref = *shown_;
    const std::int64_t first = effectStart_;
    const bool foreground = setup.foreground;
    auto frames = [&](std::int64_t f) { return trackingFrames_(ref, first + f, foreground); };
    tracking_ = true;
    stopTracking_ = false;
    bool any = false;
    {
        const TrackingStop stop(*this);
        for (model::Tracker& t : setup.trackers) {
            if (!t.enabled || stopTracking_) continue;
            const int r = t.regionAt(frame);
            if (r < 0) continue;
            const model::TrackerRegion& region = t.regions.at(r);
            // "When you start tracking from the beginning of a tracking region, Media Composer steps back one
            // frame from the region boundary before it starts tracking" (p.265).
            const std::int64_t from = frame == region.start && r > 0 ? frame - 1 : frame;
            const std::int64_t end = r + 1 < t.regions.size() ? t.regions.at(r + 1).start - 1 : last;
            if (end < from) continue;
            // Resuming within a region: the target where the data has it at the position.
            QRectF target = region.target;
            if (frame != region.start) {
                if (const auto at = t.pointAt(frame)) target.moveCenter(*at);
            }
            const QList<model::TrackPoint> points = media::trackTarget(engine, frames, from, end, target, region.search, [this] {
                QCoreApplication::processEvents();
                return stopTracking_;
            });
            for (const model::TrackPoint& p : points) t.setPoint(p.frame, QPointF(p.x, p.y), r);
            any = any || !points.isEmpty();
        }
    }
    tracking_ = false;
    if (!any) return false;
    model::setTracking(edited, setup);
    const QJsonObject parameters = edited.parameters;
    return changeShown(tr("Tracking"), [&](model::Effect& e) { e.parameters = parameters; });
}

} // namespace lumenis::ui
