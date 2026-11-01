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
#include <QMessageBox>
#include <QPushButton>

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

// The trackers Copy took (p.283), for any effect of the session.
QList<model::Tracker>& copiedTrackers() {
    static QList<model::Tracker> copied;
    return copied;
}

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

void EffectEditor::moveToFrame(std::int64_t frame) {
    if (!shown_) return;
    const std::int64_t last = std::max<std::int64_t>(span_.length - 1, 0);
    timeline_.composer().moveTo(MonitorId::Record, effectStart_ + std::clamp<std::int64_t>(frame, 0, last));
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
    // Stabilize's Auto-Zoom (p.279): "In the Tracking subcategory of the Position parameter category, click Auto-Zoom".
    if (parameter == model::TrackedParameter::Position && current_ && model::isStabilizeEffect(*current_)) {
        auto* zoom = new QPushButton(tr("Auto-Zoom"), box);
        zoom->setObjectName(QStringLiteral("tracking:Position:AutoZoom"));
        connect(zoom, &QPushButton::clicked, this, [this] { autoZoom(); });
        trackingControls_.insert(zoom->objectName(), zoom);
        layout->addWidget(zoom);
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
    for (int index = 0; index < setup.trackers.size(); ++index) {
        const model::Tracker& t = setup.trackers.at(index);
        Composer::EffectPreview::TrackerShow show;
        show.color = QColor(t.color);
        // "When this button is active for a tracker, tracking boxes and tracking data points display" (p.254).
        if (t.enabled) {
            const QList<std::int64_t> selected = pointSelection_.value(index);
            for (std::int64_t f : shownPointFrames(t)) {
                show.points << *t.pointAt(f);
                show.frames << f;
                show.selected << selected.contains(f);
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
    if (!runTracking(edited)) return false;
    const QJsonObject parameters = edited.parameters;
    return changeShown(tr("Tracking"), [&](model::Effect& e) { e.parameters = parameters; });
}

bool EffectEditor::runTracking(model::Effect& edited) {
    if (!shown_ || !trackingFrames_ || tracking_) return false;
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
    if (any) model::setTracking(edited, setup);
    return any;
}

// ---------------------------------------------------------------- stabilizing

double EffectEditor::frameAspect() const {
    const QSizeF size = previewSize();
    return size.height() > 0 ? size.width() / size.height() : 16.0 / 9.0;
}

void EffectEditor::keepAutoZoom(const model::Effect& before, model::Effect& after) {
    if (autoZooming_ || !model::isStabilizeEffect(after) || !model::trackingOf(before).autoZoom || !model::trackingOf(after).autoZoom) return;
    // Position and Scaling, with their keyframes.
    auto placed = [](const model::Effect& e) {
        QJsonObject o;
        for (const char* group : {"Position", "Scaling"}) {
            o.insert(QLatin1String(group), e.parameters.value(QLatin1String(group)));
            for (const char* name : {"X", "Y"}) {
                o.insert(QStringLiteral("%1/%2").arg(QLatin1String(group), QLatin1String(name)),
                         model::keyframesOf(e, QLatin1String(group), QLatin1String(name)).toJson());
            }
        }
        return o;
    };
    if (placed(before) != placed(after)) {
        model::TrackingSetup setup = model::trackingOf(after);
        setup.autoZoom = false;
        model::setTracking(after, setup);
        autoZoomOff_ = true;
        return;
    }
    // "If you retrack, turn SteadyGlide or Smoothing on or off, or change the smoothing values ... Auto-Zoom
    // recalculates Position and Scaling values so that black edges continue to be removed" (p.279).
    if (before.parameters.value(QStringLiteral("Trackers")) != after.parameters.value(QStringLiteral("Trackers"))) {
        model::applyAutoZoom(after, span_.length, frameAspect());
    }
}

bool EffectEditor::autoZoom() {
    if (!shown_ || !current_ || !model::isStabilizeEffect(*current_)) return false;
    model::Effect edited = *current_;
    const model::AutoZoomResult result = model::applyAutoZoom(edited, span_.length, frameAspect());
    if (result != model::AutoZoomResult::Applied) {
        // "A warning message appears if no tracking data exists. A warning message also appears if the stabilized
        // image moves too far offscreen to allow Auto-Zoom to calculate usable values" (p.279).
        auto* box = new QMessageBox(QMessageBox::Warning, tr("Auto-Zoom"),
                                    result == model::AutoZoomResult::NoData
                                        ? tr("There is no tracking data for Auto-Zoom.")
                                        : tr("The stabilized image moves too far offscreen for Auto-Zoom."),
                                    QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("autoZoomWarning"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return false;
    }
    if (edited.parameters == current_->parameters) return false;
    const QJsonObject parameters = edited.parameters;
    autoZooming_ = true;
    const bool done = changeShown(tr("Auto-Zoom"), [&](model::Effect& e) { e.parameters = parameters; });
    autoZooming_ = false;
    return done;
}

bool EffectEditor::autoStabilize(const QString& track, std::int64_t frame) {
    // The segment's first frame, so that the Effect Editor shows its Stabilize effect.
    timeline_.composer().moveTo(MonitorId::Record, frame);
    refresh();
    if (!shown_ || !current_ || shown_->track != track || !model::isStabilizeEffect(*current_) || level_ != 0) return false;
    // "Creates a tracker and enables the tracker in the Position parameter for the effect. Tracks the image using
    // the FluidStabilizer tracking engine. Processes the tracking data using the SteadyGlide option and applies the
    // data to the image. Uses the Auto-Zoom option to adjust the Scaling and Position parameters" (p.277).
    model::Effect edited = *current_;
    model::enableTracking(edited, model::TrackedParameter::Position);
    model::TrackingSetup setup = model::trackingOf(edited);
    setup.engine = QStringLiteral("FluidStabilizer");
    const model::TrackerMapping m = model::trackerMapping(edited, model::TrackedParameter::Position);
    if (model::Tracker* t = m.selectors.isEmpty() ? nullptr : setup.tracker(m.selectors.first())) t->process = model::TrackProcess::SteadyGlide;
    model::setTracking(edited, setup);
    // "Opens the Effect Editor (if necessary) and the Tracking window."
    showTrackingWindow();
    // "You can stop the tracking process at any time ... This lets you set different tracking options, and then
    // track and stabilize manually": what was set up stays.
    if (runTracking(edited) && !stopTracking_) model::applyAutoZoom(edited, span_.length, frameAspect());
    const QJsonObject parameters = edited.parameters;
    autoZooming_ = true;
    const bool done = changeShown(tr("AutoStabilize"), [&](model::Effect& e) { e.parameters = parameters; });
    autoZooming_ = false;
    return done;
}

// ---------------------------------------------------------------- editing tracking data

QList<std::int64_t> EffectEditor::shownPointFrames(const model::Tracker& tracker) const {
    // "Current: Displays the data point for the current frame only. In to Out: Displays the data points between the
    // Display Markers ... All: Displays all data points for the segment" (p.268).
    QList<std::int64_t> out;
    if (pointRange_ == PointRange::Current) return out;
    const auto [in, last] = displayRange();
    for (std::int64_t f : model::pointFrames(tracker)) {
        if (pointRange_ == PointRange::All || (f >= in && f <= last)) out << f;
    }
    return out;
}

void EffectEditor::selectionChanged() {
    if (trackingWindow_) trackingWindow_->refresh();
    syncPreview();
}

void EffectEditor::selectPoints(int index, const QList<std::int64_t>& frames, bool extend) {
    if (!extend) pointSelection_.clear();
    QList<std::int64_t>& selected = pointSelection_[index];
    for (std::int64_t f : frames) {
        if (!selected.contains(f)) selected << f;
    }
    std::sort(selected.begin(), selected.end());
    if (selected.isEmpty()) pointSelection_.remove(index);
    if (!frames.isEmpty()) lastPoint_ = std::pair{index, frames.last()};
    selectionChanged();
}

void EffectEditor::clearPointSelection() {
    if (pointSelection_.isEmpty()) return;
    pointSelection_.clear();
    lastPoint_.reset();
    selectionChanged();
}

void EffectEditor::lassoPoints(const QRectF& area, bool extend) {
    // "Lasso the data points" (p.271); Shift+lasso adds to the selection (ours, as Shift+click does).
    if (!extend) pointSelection_.clear();
    const model::TrackingSetup setup = tracking();
    for (int i = 0; i < setup.trackers.size(); ++i) {
        const model::Tracker& t = setup.trackers.at(i);
        if (!t.enabled || !showTrackingData_) continue;
        QList<std::int64_t> inside;
        for (std::int64_t f : shownPointFrames(t)) {
            if (area.contains(*t.pointAt(f))) inside << f;
        }
        if (!inside.isEmpty()) selectPoints(i, inside, true);
    }
    selectionChanged();
}

bool EffectEditor::dragTrackPoint(const QString& id, const QPointF& from, const QPointF& to, bool finished, bool shift) {
    const QStringList parts = id.split(QLatin1Char(':'));
    if (!shown_ || !current_ || parts.size() != 3) {
        handleStart_.reset();
        return false;
    }
    const int index = parts.at(1).toInt();
    const std::int64_t frame = parts.at(2).toLongLong();
    if (!handleStart_) {
        handleStart_ = *current_;
        const model::TrackingSetup setup = model::trackingOf(*current_);
        if (index < 0 || index >= setup.trackers.size()) {
            handleStart_.reset();
            return false;
        }
        if (shift && lastPoint_ && lastPoint_->first == index) {
            // "Shift+click the first and last data points in the range" (p.271).
            const auto [lo, hi] = std::minmax(lastPoint_->second, frame);
            QList<std::int64_t> range;
            for (std::int64_t f : model::pointFrames(setup.trackers.at(index))) {
                if (f >= lo && f <= hi) range << f;
            }
            const std::pair<int, std::int64_t> anchor = *lastPoint_;
            selectPoints(index, range, true);
            lastPoint_ = anchor;
        } else if (shift) {
            selectPoints(index, {frame}, true);
        } else if (!pointSelection_.value(index).contains(frame)) {
            // "Click the data point" (p.271); a selected one is dragged with the rest of the selection.
            selectPoints(index, {frame});
        }
    }
    const model::Effect start = *handleStart_;
    if (finished) handleStart_.reset();
    const QPointF by = to - from;
    if (finished && by.isNull()) {
        // A click: the point clicked without Shift becomes the selection, even if it was part of a larger one.
        const bool alone = pointSelection_.size() == 1 && pointSelection_.value(index) == QList<std::int64_t>{frame};
        if (!shift && !alone) selectPoints(index, {frame});
        preview_.reset();
        syncPreview();
        return true;
    }
    model::TrackingSetup setup = model::trackingOf(start);
    if (index < 0 || index >= setup.trackers.size()) return false;
    const QList<std::int64_t> frames = pointSelection_.value(index);
    model::Tracker& t = setup.trackers[index];
    // "drag the data point to a new position. All the data points move along with the one you drag" (p.272), or
    // in Stretch Points Mode stretch between the anchors (p.273).
    const bool changed = stretchMode_ ? model::stretchPoints(t, frames, frame, by) : model::movePoints(t, frames, by);
    model::Effect edited = start;
    model::setTracking(edited, setup);
    if (!finished) {
        preview_ = edited;
        syncPreview();
        return changed;
    }
    preview_.reset();
    if (!changed || edited.parameters == start.parameters) {
        syncPreview();
        return false;
    }
    const QJsonObject parameters = edited.parameters;
    return changeShown(stretchMode_ ? tr("Stretch Points") : tr("Move Points"), [&](model::Effect& e) { e.parameters = parameters; });
}

bool EffectEditor::deleteSelectedPoints() {
    if (pointSelection_.isEmpty() || !shown_ || !current_) return false;
    const QHash<int, QList<std::int64_t>> selection = pointSelection_;
    return changeEffectParameters(tr("Delete Points"), [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        bool any = false;
        for (auto it = selection.cbegin(); it != selection.cend(); ++it) {
            if (it.key() < 0 || it.key() >= setup.trackers.size()) continue;
            any = model::deletePoints(setup.trackers[it.key()], it.value()) || any;
        }
        if (any) model::setTracking(e, setup);
        return any;
    });
}

void EffectEditor::setStretchMode(bool on) {
    stretchMode_ = on;
    if (trackingWindow_) trackingWindow_->refresh();
}

bool EffectEditor::addTrackerRegion(int index) {
    const std::int64_t frame = positionFrame();
    return changeEffectParameters(tr("Add New Tracker Region"), [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        if (index < 0 || index >= setup.trackers.size() || !model::addRegion(setup.trackers[index], frame)) return false;
        model::setTracking(e, setup);
        return true;
    });
}

bool EffectEditor::deleteTrackerRegion(int index) {
    const std::int64_t frame = positionFrame();
    const bool done = changeEffectParameters(tr("Delete Tracker Region"), [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        if (index < 0 || index >= setup.trackers.size() || !model::deleteRegion(setup.trackers[index], frame)) return false;
        model::setTracking(e, setup);
        return true;
    });
    if (done) {
        // Its points may be gone.
        pointSelection_.remove(index);
        selectionChanged();
    }
    return done;
}

bool EffectEditor::setReferenceFrame(int index) {
    const std::int64_t frame = positionFrame();
    return changeEffectParameters(tr("Set Reference Frame"), [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        if (index < 0 || index >= setup.trackers.size() || setup.trackers.at(index).reference == frame) return false;
        setup.trackers[index].reference = frame;
        model::setTracking(e, setup);
        return true;
    });
}

bool EffectEditor::goToRegion(bool next) {
    // "Go to Previous Region / Go to Next Region": the nearest region start of any tracker (ours, as all trackers'
    // regions are shown together).
    const std::int64_t frame = positionFrame();
    std::optional<std::int64_t> target;
    for (const model::Tracker& t : tracking().trackers) {
        for (const model::TrackerRegion& r : t.regions) {
            if (next ? r.start > frame && (!target || r.start < *target) : r.start < frame && (!target || r.start > *target)) {
                target = r.start;
            }
        }
    }
    if (!target) return false;
    moveToFrame(*target);
    return true;
}

bool EffectEditor::copyTrackers() {
    // "Enable the trackers you want to copy in the Tracking window" (p.283).
    QList<model::Tracker> enabled;
    for (const model::Tracker& t : tracking().trackers) {
        if (t.enabled) enabled << t;
    }
    if (enabled.isEmpty()) return false;
    copiedTrackers() = enabled;
    return true;
}

bool EffectEditor::hasCopiedTrackers() const {
    return !copiedTrackers().isEmpty();
}

bool EffectEditor::pasteTrackers() {
    if (copiedTrackers().isEmpty() || !shown_ || !current_) return false;
    // "The trackers appear in the Tracking window below any existing trackers" (p.283).
    const QList<model::Tracker> copied = copiedTrackers();
    return changeEffectParameters(tr("Paste Trackers"), [&](model::Effect& e) {
        model::TrackingSetup setup = model::trackingOf(e);
        if (model::pasteTrackers(setup, copied) == 0) return false;
        model::setTracking(e, setup);
        return true;
    });
}

bool EffectEditor::trackingWindowActive() const {
    return trackingWindow_ && trackingWindow_->isVisible() && trackingWindow_->isActiveWindow();
}

std::pair<std::int64_t, std::int64_t> EffectEditor::displayRange() const {
    const std::int64_t last = std::max<std::int64_t>(span_.length - 1, 0);
    if (!displayRange_) return {0, last};
    return {std::clamp<std::int64_t>(displayRange_->first, 0, last), std::clamp<std::int64_t>(displayRange_->second, 0, last)};
}

void EffectEditor::setDisplayRange(std::int64_t in, std::int64_t out) {
    if (out < in) std::swap(in, out);
    displayRange_ = std::pair{in, out};
    if (trackingWindow_) trackingWindow_->refresh();
    syncPreview();
}

} // namespace lumenis::ui
