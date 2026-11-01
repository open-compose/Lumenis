// Motion tracking in the Effect Editor (FX Guide ch.10, p.253-269; effect_editor_tracking.cpp, tracking_window.*):
// enabling tracking for Position opens the Tracking window with Point A, the tracking boxes in the Effect Preview
// are moved over the target, Start Tracking follows it through synthetic frames (and stops on the space bar), the
// data drives the Picture-in-Picture, and the Tracking window's rows and menus change the trackers. Then editing
// the data (6.7c3, p.265-274): selecting, moving, deleting and stretching points, regions, the reference frame and
// the Display Markers.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/tracking.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tracking_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QRandomGenerator>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// V1: A (50 frames) then B (50).
BinItem sequence() {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    model::Segment a;
    a.clipId = QStringLiteral("a");
    a.name = QStringLiteral("A");
    a.sourceTrack = QStringLiteral("V1");
    a.length = 50;
    model::Segment b = a;
    b.clipId = QStringLiteral("b");
    b.name = QStringLiteral("B");
    seq.splice(QStringLiteral("V1"), 0, {a, b});
    seq.storeIn(s);
    return s;
}

// 160 x 90: a faint fixed texture with a bright 10 x 10 square whose top-left is at (40 + f, 30) at frame f of the
// effect.
QImage squareFrame(std::int64_t f) {
    QImage image(160, 90, QImage::Format_RGB32);
    QRandomGenerator texture(3);
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 160; ++x) {
            const int v = 20 + static_cast<int>(texture.bounded(10));
            image.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    for (int y = 30; y < 40; ++y) {
        for (int x = 40 + static_cast<int>(f); x < 50 + static_cast<int>(f); ++x) image.setPixelColor(x, y, QColor(230, 230, 230));
    }
    return image;
}

// 160 x 90: a smooth fixed pattern moved right by `shift` pixels, as an unsteady camera moves a shot.
QImage shakenFrame(int shift) {
    QImage image(160, 90, QImage::Format_RGB32);
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 160; ++x) {
            const double u = x - shift;
            const double v = 128.0 + 50.0 * std::sin(0.21 * u + 0.13 * y) + 40.0 * std::cos(0.17 * y - 0.11 * u) + 30.0 * std::sin(0.37 * u);
            const int g = std::clamp(static_cast<int>(v), 0, 255);
            image.setPixelColor(x, y, QColor(g, g, g));
        }
    }
    return image;
}

} // namespace

class TestTrackingUi : public QObject {
    Q_OBJECT

private slots:
    void tracking() {
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        const EffectInfo* pip = EffectCatalog::builtin().find(QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"));
        QVERIFY(pip);
        timeline.dropEffect(QStringLiteral("V1"), 70, *pip);
        composer.moveTo(MonitorId::Record, 50);
        editor.resize(700, 900);
        editor.show();
        settle();
        // The frames tracked: the square, counted from the effect's first frame (Record frame 50); the space bar
        // pressed when frame `stopAt` is asked for.
        std::int64_t stopAt = -1;
        editor.setTrackingFrames([&](const model::EffectRef& ref, std::int64_t frame, bool foreground) {
            Q_UNUSED(foreground);
            if (ref.track != QStringLiteral("V1")) return QImage();
            if (frame - 50 == stopAt) QCoreApplication::postEvent(&editor, new QKeyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier));
            return squareFrame(frame - 50);
        });
        auto effect = [&] {
            return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(1).effect;
        };

        // "Open the Position or Scaling category, and then click the enable button for the Tracking subcategory"
        // (p.257): Point A, mapped; the Tracking window opens.
        auto* on = editor.findChild<QCheckBox*>(QStringLiteral("tracking:Position"));
        QVERIFY(on);
        on->click();
        settle();
        QVERIFY(model::trackerMapping(effect(), model::TrackedParameter::Position).on);
        QCOMPARE(model::trackingOf(effect()).trackers.size(), 1);
        QVERIFY(editor.trackingWindow()->isVisible());
        QVERIFY(on->isChecked());
        QCOMPARE(editor.findChild<QComboBox*>(QStringLiteral("tracking:Position:T1"))->currentText(), QStringLiteral("Point A"));
        TrackingWindow* window = editor.trackingWindow();
        QVERIFY(window->control(QStringLiteral("timeline"), 0));

        // The tracking boxes in the Effect Preview at the effect's first frame (p.261), moved over the square.
        timeline.setEffectMode(true);
        settle();
        QCOMPARE(composer.effectPreview().trackers.size(), 1);
        QVERIFY(composer.effectPreview().trackers.first().boxes);
        const QPointF centre = composer.effectPreview().trackers.first().target.center();
        const QPointF square(45.0 / 160, 35.0 / 90);
        QVERIFY(editor.dragHandle(QStringLiteral("track:0:move"), centre, square, true));
        settle();
        QVERIFY(std::abs(model::trackingOf(effect()).trackers.first().regions.first().target.center().x() - square.x()) < 1e-9);
        // The corner handle makes the target smaller (and the search area keeps holding it).
        const QRectF target = model::trackingOf(effect()).trackers.first().regions.first().target;
        QVERIFY(editor.dragHandle(QStringLiteral("track:0:target"), target.bottomRight(), target.bottomRight() - QPointF(0.01, 0.0), true));
        settle();

        // Start Tracking (p.264): the square through the effect, one undo step.
        QVERIFY(editor.startTracking());
        settle();
        const model::Tracker tracked = model::trackingOf(effect()).trackers.first();
        QVERIFY(tracked.pointAt(49));
        const QPointF moved = *tracked.pointAt(20) - *tracked.pointAt(0);
        QVERIFY2(std::abs(moved.x() * 160 - 20.0) < 0.5 && std::abs(moved.y() * 90) < 0.5, qPrintable(QStringLiteral("%1 %2").arg(moved.x() * 160).arg(moved.y() * 90)));
        // The data moves the Picture-in-Picture (Position, p.498).
        QVERIFY(std::abs(model::trackedChange(effect(), 20, 16.0 / 9.0).position.x() * 160 - 20.0) < 0.5);
        // The Effect Preview: every data point, the position's as a cross; Current only that one (p.268).
        QCOMPARE(composer.effectPreview().trackers.first().points.size(), 50);
        QVERIFY(composer.effectPreview().trackers.first().current);
        window->pointRangeMenu()->setCurrentIndex(0);
        settle();
        QVERIFY(composer.effectPreview().trackers.first().points.isEmpty());
        window->displayMenu()->setCurrentIndex(1); // Effect Results
        settle();
        QVERIFY(composer.effectPreview().trackers.isEmpty());
        window->displayMenu()->setCurrentIndex(0);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(!model::trackingOf(effect()).trackers.first().hasData());

        // "To stop a tracking operation ... Press the space bar" (p.265): what was tracked so far is kept.
        stopAt = 10;
        QVERIFY(editor.startTracking());
        settle();
        const model::Tracker partial = model::trackingOf(effect()).trackers.first();
        QVERIFY(partial.pointAt(9));
        QVERIFY(!partial.pointAt(30));

        // The Tracking window's rows: the name renames the tracker and its selector (p.255, p.259-260).
        auto* name = qobject_cast<QLineEdit*>(window->control(QStringLiteral("name"), 0));
        QVERIFY(name);
        name->setText(QStringLiteral("Car"));
        emit name->editingFinished();
        settle();
        QCOMPARE(model::trackerMapping(effect(), model::TrackedParameter::Position).selectors.first(), QStringLiteral("Car"));
        // Smoothing on, then SteadyGlide instead ("mutually exclusive", p.270).
        qobject_cast<QToolButton*>(window->control(QStringLiteral("smoothing"), 0))->click();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().process, model::TrackProcess::Smoothing);
        qobject_cast<QToolButton*>(window->control(QStringLiteral("steadyglide"), 0))->click();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().process, model::TrackProcess::SteadyGlide);
        // New Tracker: Point B (the next letter not in use, p.259); the Tracking Engine menu.
        window->newTrackerButton()->click();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.last().name, QStringLiteral("Point A"));
        window->engineMenu()->setCurrentText(QStringLiteral("FluidTracker"));
        settle();
        QCOMPARE(model::trackingOf(effect()).engine, QStringLiteral("FluidTracker"));
        // The Close button: gone, and from the selector (p.255).
        qobject_cast<QToolButton*>(window->control(QStringLiteral("close"), 0))->click();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.size(), 1);
        QVERIFY(model::trackerMapping(effect(), model::TrackedParameter::Position).selectors.first().isEmpty());
        timeline.setEffectMode(false);
    }

    void editing() {
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        const EffectInfo* pip = EffectCatalog::builtin().find(QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"));
        QVERIFY(pip);
        timeline.dropEffect(QStringLiteral("V1"), 70, *pip);
        composer.moveTo(MonitorId::Record, 50);
        editor.resize(700, 900);
        editor.show();
        settle();
        auto effect = [&] {
            return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(1).effect;
        };
        auto point = [&](std::int64_t f) { return *model::trackingOf(effect()).trackers.first().pointAt(f); };
        editor.findChild<QCheckBox*>(QStringLiteral("tracking:Position"))->click();
        settle();
        // Data on every frame: x = 0.2 + f / 100, y = 0.5.
        QVERIFY(editor.changeTracking(QStringLiteral("data"), [](model::TrackingSetup& s) {
            for (std::int64_t f = 0; f < 50; ++f) s.trackers[0].setPoint(f, QPointF(0.2 + static_cast<double>(f) / 100.0, 0.5));
        }));
        timeline.setEffectMode(true);
        settle();
        QCOMPARE(composer.effectPreview().trackers.first().points.size(), 50);
        QCOMPARE(composer.effectPreview().trackers.first().frames.at(7), std::int64_t(7));

        // "Click the data point"; "Shift+click the first and last data points in the range" (p.271).
        QVERIFY(editor.dragHandle(QStringLiteral("trackpoint:0:10"), point(10), point(10), true));
        QCOMPARE(editor.selectedPoints(0), (QList<std::int64_t>{10}));
        QVERIFY(composer.effectPreview().trackers.first().selected.at(10));
        QVERIFY(!composer.effectPreview().trackers.first().selected.at(11));
        QVERIFY(editor.dragHandle(QStringLiteral("trackpoint:0:14"), point(14), point(14), true, true));
        QCOMPARE(editor.selectedPoints(0), (QList<std::int64_t>{10, 11, 12, 13, 14}));

        // Dragging one moves them all (p.272), one undo step.
        QVERIFY(editor.dragHandle(QStringLiteral("trackpoint:0:12"), point(12), point(12) + QPointF(0, 0.05), false));
        QVERIFY(editor.dragHandle(QStringLiteral("trackpoint:0:12"), point(12), point(12) + QPointF(0, 0.1), true));
        settle();
        QVERIFY(std::abs(point(10).y() - 0.6) < 1e-9 && std::abs(point(14).y() - 0.6) < 1e-9);
        QVERIFY(std::abs(point(15).y() - 0.5) < 1e-9);
        QCOMPARE(editor.selectedPoints(0).size(), 5);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(std::abs(point(12).y() - 0.5) < 1e-9);

        // Stretch Points Mode: the middle point dragged, the ends anchored (p.273).
        TrackingWindow* window = editor.trackingWindow();
        window->stretchButton()->click();
        QVERIFY(editor.stretchMode());
        QVERIFY(editor.dragHandle(QStringLiteral("trackpoint:0:12"), point(12), point(12) + QPointF(0, 0.1), true));
        settle();
        QVERIFY(std::abs(point(12).y() - 0.6) < 1e-9);
        QVERIFY(point(11).y() > 0.5 && point(11).y() < 0.6);
        window->stretchButton()->click();
        QVERIFY(!editor.stretchMode());

        // Delete: onto the straight line between the points around the selection (p.272).
        QVERIFY(timeline.perform(QStringLiteral("edit.delete")));
        settle();
        QVERIFY(std::abs(point(12).y() - 0.5) < 1e-9);
        QVERIFY(std::abs(point(12).x() - 0.32) < 1e-9);
        // The Effect Editor's own effect is still there (Delete did not remove it).
        QVERIFY(model::trackingOf(effect()).trackers.size() == 1);

        // A lasso selects the points inside it; Shift adds (p.271).
        editor.lassoPoints(QRectF(QPointF(0.195, 0.45), QPointF(0.225, 0.55)), false);
        QCOMPARE(editor.selectedPoints(0), (QList<std::int64_t>{0, 1, 2}));
        editor.lassoPoints(QRectF(QPointF(0.475, 0.45), QPointF(0.485, 0.55)), true);
        QCOMPARE(editor.selectedPoints(0), (QList<std::int64_t>{0, 1, 2, 28}));
        editor.lassoPoints(QRectF(QPointF(0.9, 0.9), QPointF(0.95, 0.95)), false);
        QVERIFY(editor.selectedPoints(0).isEmpty());
        // Double-click: selected, and the position on its frame.
        emit composer.effectPointDoubleClicked(QStringLiteral("trackpoint:0:20"));
        settle();
        QCOMPARE(editor.selectedPoints(0), (QList<std::int64_t>{20}));
        QCOMPARE(editor.positionFrame(), std::int64_t(20));

        // The tracker timeline: clicking moves the position; its menu adds a region there (p.266).
        auto* track = dynamic_cast<TrackerTimeline*>(window->control(QStringLiteral("timeline"), 0));
        QVERIFY(track);
        window->show();
        settle();
        QTest::mouseClick(track, Qt::LeftButton, {}, QPoint(static_cast<int>(track->xOf(30)), track->height() / 2));
        settle();
        QCOMPARE(editor.positionFrame(), std::int64_t(30));
        std::unique_ptr<QMenu> menu(track->makeMenu());
        menu->findChild<QAction*>(QStringLiteral("addRegion"))->trigger();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().regions.size(), 2);
        QCOMPARE(model::trackingOf(effect()).trackers.first().regions.at(1).start, std::int64_t(30));
        // Set Reference Frame (p.269).
        menu->findChild<QAction*>(QStringLiteral("setReference"))->trigger();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().reference, std::int64_t(30));
        // Go to Previous/Next Region (p.266).
        window->previousRegionButton()->click();
        settle();
        QCOMPARE(editor.positionFrame(), std::int64_t(0));
        window->nextRegionButton()->click();
        settle();
        QCOMPARE(editor.positionFrame(), std::int64_t(30));
        QVERIFY(!editor.goToRegion(true));
        // Delete Tracker Region: the region and its data go.
        menu->findChild<QAction*>(QStringLiteral("deleteRegion"))->trigger();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().regions.size(), 1);
        menu.reset(); // a child of the row's timeline, which goes when the rows are rebuilt

        // The Display Markers and In to Out (p.268): only the points in the range.
        editor.setDisplayRange(5, 9);
        QCOMPARE(editor.displayRange(), (std::pair<std::int64_t, std::int64_t>{5, 9}));
        window->pointRangeMenu()->setCurrentIndex(1);
        settle();
        QCOMPARE(composer.effectPreview().trackers.first().frames, (QList<std::int64_t>{5, 6, 7, 8, 9}));
        // Dragging the end marker in the timeline.
        const QPoint end(static_cast<int>(static_cast<double>(10) / 50.0 * track->width()), track->height() / 2);
        QTest::mousePress(track, Qt::LeftButton, {}, end);
        QTest::mouseMove(track, QPoint(static_cast<int>(track->xOf(14)), track->height() / 2));
        QMouseEvent move(QEvent::MouseMove, QPointF(track->xOf(14), track->height() / 2.0), track->mapToGlobal(QPointF(track->xOf(14), track->height() / 2.0)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(track, &move);
        QTest::mouseRelease(track, Qt::LeftButton, {}, QPoint(static_cast<int>(track->xOf(14)), track->height() / 2));
        QCOMPARE(editor.displayRange(), (std::pair<std::int64_t, std::int64_t>{5, 14}));

        track = nullptr; // Paste rebuilds the rows
        // Copy and Paste in the Tracking window (p.283-284): the enabled trackers, below the others, renamed when
        // the name is taken; one undo step.
        QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
        QVERIFY(editor.hasCopiedTrackers());
        QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier);
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.size(), 2);
        QCOMPARE(model::trackingOf(effect()).trackers.last().name, QStringLiteral("Point A 2"));
        QCOMPARE(model::trackingOf(effect()).trackers.last().regions, model::trackingOf(effect()).trackers.first().regions);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.size(), 1);
        timeline.setEffectMode(false);
    }

    // The Stabilize effect (p.275-279): applied to one segment, AutoStabilize tracks it with FluidStabilizer and
    // SteadyGlide and zooms; Auto-Zoom follows the data until Position or Scaling is changed by hand.
    void stabilize() {
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        editor.resize(700, 900);
        editor.show();
        settle();
        // The shot shakes: 0, 3, 0, 3 ... pixels.
        editor.setTrackingFrames([](const model::EffectRef& ref, std::int64_t frame, bool) {
            if (ref.track != QStringLiteral("V1")) return QImage();
            return shakenFrame((frame - 50) % 2 == 0 ? 0 : 3);
        });
        QString requestedTrack;
        std::int64_t requestedFrame = -1;
        connect(&timeline, &Timeline::autoStabilizeRequested, &editor, [&](const QString& track, std::int64_t frame) {
            requestedTrack = track;
            requestedFrame = frame;
            editor.autoStabilize(track, frame);
        });
        auto effect = [&] {
            return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(1).effect;
        };
        const EffectInfo* stabilize = EffectCatalog::builtin().find(QStringLiteral("Image"), QStringLiteral("Stabilize"));
        QVERIFY(stabilize);
        QVERIFY(timeline.dropEffect(QStringLiteral("V1"), 70, *stabilize));
        settle();
        QCOMPARE(requestedTrack, QStringLiteral("V1"));
        QCOMPARE(requestedFrame, std::int64_t(50));
        // "Creates a tracker and enables the tracker in the Position parameter ... FluidStabilizer ... SteadyGlide ...
        // Auto-Zoom" (p.277); the Tracking window open.
        model::TrackingSetup setup = model::trackingOf(effect());
        QCOMPARE(setup.trackers.size(), 1);
        QCOMPARE(setup.engine, QStringLiteral("FluidStabilizer"));
        QCOMPARE(setup.trackers.first().process, model::TrackProcess::SteadyGlide);
        QVERIFY(setup.trackers.first().pointAt(49));
        QVERIFY(setup.autoZoom);
        QVERIFY(model::trackerMapping(effect(), model::TrackedParameter::Position).on);
        QVERIFY(editor.trackingWindow()->isVisible());
        // The shake undone: odd frames moved back by the 3 pixels (frame 0 is the reference).
        QVERIFY2(std::abs(model::trackedChange(effect(), 1, 16.0 / 9.0).position.x() * 160.0 + 3.0) < 1.0,
                 qPrintable(QString::number(model::trackedChange(effect(), 1, 16.0 / 9.0).position.x() * 160.0)));
        const double scale = effect().parameters.value(QStringLiteral("Scaling")).toObject().value(QStringLiteral("X")).toDouble();
        QVERIFY2(scale > 101.0 && scale < 106.0, qPrintable(QString::number(scale)));
        // One undo step takes it all back.
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(!effect().parameters.contains(QStringLiteral("Trackers")));
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));
        settle();
        QVERIFY(model::trackingOf(effect()).autoZoom);

        // Turning SteadyGlide off recalculates Auto-Zoom (p.279): the full shake is undone now, a little more zoom.
        composer.moveTo(MonitorId::Record, 50);
        settle();
        TrackingWindow* window = editor.trackingWindow();
        qobject_cast<QToolButton*>(window->control(QStringLiteral("steadyglide"), 0))->click();
        settle();
        QCOMPARE(model::trackingOf(effect()).trackers.first().process, model::TrackProcess::None);
        const double unsmoothed = effect().parameters.value(QStringLiteral("Scaling")).toObject().value(QStringLiteral("X")).toDouble();
        QVERIFY2(unsmoothed > 101.0, qPrintable(QString::number(unsmoothed)));
        QVERIFY(model::trackingOf(effect()).autoZoom);

        // A Scaling slider moved by hand: Auto-Zoom off, with the message (p.279); the Auto-Zoom button again.
        QSlider* x = editor.slider(QStringLiteral("Scaling"), QStringLiteral("X"));
        QVERIFY(x);
        x->setValue(150);
        settle();
        QVERIFY(!model::trackingOf(effect()).autoZoom);
        QMessageBox* off = editor.findChild<QMessageBox*>(QStringLiteral("autoZoomOff"));
        QVERIFY(off);
        off->close();
        auto* zoom = editor.findChild<QPushButton*>(QStringLiteral("tracking:Position:AutoZoom"));
        QVERIFY(zoom);
        zoom->click();
        settle();
        QVERIFY(model::trackingOf(effect()).autoZoom);
        QVERIFY(std::abs(effect().parameters.value(QStringLiteral("Scaling")).toObject().value(QStringLiteral("X")).toDouble() - unsmoothed) < 1e-6);

        // Two segments selected: no AutoStabilize (p.276); Region Stabilize never runs it.
        requestedFrame = -1;
        const EffectInfo* region = EffectCatalog::builtin().find(QStringLiteral("Image"), QStringLiteral("Region Stabilize"));
        QVERIFY(region);
        QVERIFY(timeline.dropEffect(QStringLiteral("V1"), 10, *region));
        settle();
        QCOMPARE(requestedFrame, std::int64_t(-1));
        // Its wire frame in the Effect Preview (p.280), dragged; the Model Fast Menu.
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        QCOMPARE(editor.effectName(), QStringLiteral("Region Stabilize"));
        QCOMPARE(composer.effectPreview().outline.size(), 4);
        const QPointF centre = (composer.effectPreview().outline.at(0) + composer.effectPreview().outline.at(2)) / 2.0;
        QVERIFY(editor.dragHandle(QStringLiteral("move"), centre, centre + QPointF(0.1, 0.0), true));
        settle();
        const model::Effect regionEffect =
            *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0).effect;
        QVERIFY(std::abs(regionEffect.parameters.value(QStringLiteral("Region of Interest")).toObject().value(QStringLiteral("X")).toDouble() - 10.0) < 0.5);
        auto* model = editor.findChild<QComboBox*>(QStringLiteral("param:Model/Model"));
        QVERIFY(model);
        QCOMPARE(model->currentText(), QStringLiteral("Translational"));
        timeline.setEffectMode(false);

        requestedFrame = -1;
        requestedFrame = -1;
        timeline.setSelection({model::SegmentRef{QStringLiteral("V1"), 0}, model::SegmentRef{QStringLiteral("V1"), 50}});
        QVERIFY(timeline.applyEffectToSelection(*stabilize));
        settle();
        QCOMPARE(requestedFrame, std::int64_t(-1));
    }
};

QTEST_MAIN(TestTrackingUi)
#include "tst_tracking_ui.moc"
