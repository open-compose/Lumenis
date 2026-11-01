// Motion tracking in the Effect Editor (FX Guide ch.10, p.253-269; effect_editor_tracking.cpp, tracking_window.*):
// enabling tracking for Position opens the Tracking window with Point A, the tracking boxes in the Effect Preview
// are moved over the target, Start Tracking follows it through synthetic frames (and stops on the space bar), the
// data drives the Picture-in-Picture, and the Tracking window's rows and menus change the trackers.
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
#include <QRandomGenerator>
#include <QTest>
#include <QToolButton>

#include <cmath>

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
};

QTEST_MAIN(TestTrackingUi)
#include "tst_tracking_ui.moc"
