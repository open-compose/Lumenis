// Motion tracking (FX Guide ch.10, p.248-275; model/tracking.*, media/tracking.*): trackers and their names,
// mapping them to parameters, offset tracking across regions, Smoothing and SteadyGlide, the reference frame, what
// tracking data does to Position, Scaling, Rotation and the Tracking category, and the engines following a moving
// target.
#include "lumenis/media/tracking.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/tracking.h"

#include <QJsonObject>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::model;

namespace {

model::Effect pip() {
    return model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
}

// A tracker with one region and the given points.
Tracker withPoints(const QString& name, const QList<TrackPoint>& points) {
    Tracker t;
    t.name = name;
    TrackerRegion r;
    r.points = points;
    t.regions << r;
    return t;
}

// 160 x 90 frames: dark, with a bright 10 x 10 square whose top-left is at (40 + 3f, 30 + f) at frame f, over a
// faint fixed texture.
QImage squareFrame(std::int64_t f) {
    QImage image(160, 90, QImage::Format_RGB32);
    QRandomGenerator texture(7);
    for (int y = 0; y < 90; ++y) {
        for (int x = 0; x < 160; ++x) {
            const int v = 20 + static_cast<int>(texture.bounded(10));
            image.setPixelColor(x, y, QColor(v, v, v));
        }
    }
    const int left = 40 + 3 * static_cast<int>(f), top = 30 + static_cast<int>(f);
    for (int y = top; y < top + 10; ++y) {
        for (int x = left; x < left + 10; ++x) image.setPixelColor(x, y, QColor(230, 230, 230));
    }
    return image;
}

bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-9) {
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

} // namespace

class TestTracking : public QObject {
    Q_OBJECT

private slots:
    // "The tracker name uses the next letter of the alphabet ... up to the letter D ... 'Untitled Tracker'"; "up to
    // eight trackers for each effect" (p.259). Kept with the effect.
    void trackers() {
        TrackingSetup s;
        for (int i = 0; i < 8; ++i) {
            const auto t = newTracker(s);
            QVERIFY(t);
            s.trackers << *t;
        }
        QCOMPARE(s.trackers.at(0).name, QStringLiteral("Point A"));
        QCOMPARE(s.trackers.at(3).name, QStringLiteral("Point D"));
        QCOMPARE(s.trackers.at(4).name, QStringLiteral("Untitled Tracker"));
        QVERIFY(!newTracker(s));
        s.trackers[0].regions[0].points << TrackPoint{3, 0.25, 0.5};
        s.engine = QStringLiteral("FluidTracker");
        model::Effect e = pip();
        setTracking(e, s);
        QCOMPARE(trackingOf(e), s);
    }

    // Enabling tracking maps (and makes) trackers; the two groups exclude each other (p.251, p.257-259).
    void mapping() {
        model::Effect e = pip();
        QVERIFY(enableTracking(e, TrackedParameter::Position));
        QCOMPARE(trackingOf(e).trackers.size(), 1);
        QCOMPARE(trackerMapping(e, TrackedParameter::Position).selectors.first(), QStringLiteral("Point A"));
        QVERIFY(enableTracking(e, TrackedParameter::Scaling));
        QCOMPARE(trackingOf(e).trackers.size(), 2);
        QCOMPARE(trackerMapping(e, TrackedParameter::Scaling).selectors, (QStringList{QStringLiteral("Point A"), QStringLiteral("Point B")}));
        QVERIFY(enableTracking(e, TrackedParameter::Tracking, 2));
        QVERIFY(!trackerMapping(e, TrackedParameter::Position).on);
        QVERIFY(!trackerMapping(e, TrackedParameter::Scaling).on);
        const TrackerMapping t = trackerMapping(e, TrackedParameter::Tracking);
        QVERIFY(t.on);
        QCOMPARE(t.selectors.at(2), QStringLiteral("Point C"));
        QCOMPARE(t.selectorOn, (QList<bool>{false, false, true, false}));
        // The whole parameter off keeps its selectors (p.260).
        QVERIFY(disableTracking(e, TrackedParameter::Tracking));
        QCOMPARE(trackerMapping(e, TrackedParameter::Tracking).selectors.at(2), QStringLiteral("Point C"));
        QVERIFY(enableTracking(e, TrackedParameter::Position));
        QCOMPARE(trackerMapping(e, TrackedParameter::Position).selectors.first(), QStringLiteral("Point A"));
    }

    // Offset tracking joins a second target into one path (p.265-267); without it the path jumps.
    void offsetTracking() {
        Tracker t = withPoints(QStringLiteral("Point A"), {{0, 0.1, 0.5}, {1, 0.2, 0.5}, {2, 0.3, 0.5}, {3, 0.4, 0.5}});
        TrackerRegion second;
        second.start = 4;
        // Tracked from the frame before the region (p.265): a different target, moving the same way.
        second.points = {{3, 0.7, 0.2}, {4, 0.8, 0.2}, {5, 0.9, 0.2}};
        t.regions << second;
        QVERIFY(near(*trackerPath(t, 3), QPointF(0.4, 0.5)));
        QVERIFY(near(*trackerPath(t, 5), QPointF(0.6, 0.5)));
        QCOMPARE(t.regionAt(4), 1);
        QVERIFY(near(*t.pointAt(4), QPointF(0.8, 0.2)));
        t.offset = false;
        QVERIFY(near(*trackerPath(t, 5), QPointF(0.9, 0.2)));
        // Between points and beyond the ends: the nearest data.
        Tracker gaps = withPoints(QStringLiteral("Point A"), {{0, 0.0, 0.0}, {4, 0.4, 0.8}});
        QVERIFY(near(*trackerPath(gaps, 1), QPointF(0.1, 0.2)));
        QVERIFY(near(*trackerPath(gaps, 9), QPointF(0.4, 0.8)));
        QVERIFY(!trackerPath(Tracker{}, 0));
    }

    // Smoothing and SteadyGlide (p.269-270); the reference frame (p.269).
    void processing() {
        QList<TrackPoint> zigzag;
        for (int f = 0; f < 40; ++f) zigzag << TrackPoint{f, 0.01 * f + (f % 2 ? 0.02 : -0.02), 0.5};
        Tracker t = withPoints(QStringLiteral("Point A"), zigzag);
        auto roughness = [](const Tracker& tr) {
            double sum = 0.0;
            for (int f = 5; f < 35; ++f) {
                const double a = trackerPath(tr, f - 1)->x(), b = trackerPath(tr, f)->x(), c = trackerPath(tr, f + 1)->x();
                sum += std::abs(a - 2 * b + c);
            }
            return sum;
        };
        const double raw = roughness(t);
        t.process = TrackProcess::Smoothing;
        t.smoothing = 50;
        QVERIFY(roughness(t) < raw / 10.0);
        QVERIFY(std::abs(trackerJitter(t, 20)->x()) < 1e-12);
        t.process = TrackProcess::SteadyGlide;
        QVERIFY(std::abs(trackerJitter(t, 20)->x()) > 0.01);
        // Movement from the reference frame: none there.
        Tracker line = withPoints(QStringLiteral("Point A"), {{0, 0.1, 0.1}, {10, 0.3, 0.6}});
        QVERIFY(near(*trackerMovement(line, 10), QPointF(0.2, 0.5)));
        line.reference = 10;
        QVERIFY(near(*trackerMovement(line, 10), QPointF()));
        QVERIFY(near(*trackerMovement(line, 0), QPointF(-0.2, -0.5)));
    }

    // Editing the data points (p.266, p.271-274).
    void editing() {
        Tracker t = withPoints(QStringLiteral("Point A"), {{0, 0.0, 0.0}, {1, 0.5, 0.9}, {2, 0.1, 0.9}, {3, 0.3, 0.3}, {4, 0.4, 0.4}});
        QCOMPARE(pointFrames(t), (QList<std::int64_t>{0, 1, 2, 3, 4}));
        // Delete: onto the straight line between the unselected points around them.
        QVERIFY(deletePoints(t, {1, 2}));
        QVERIFY(near(*t.pointAt(1), QPointF(0.1, 0.1)));
        QVERIFY(near(*t.pointAt(2), QPointF(0.2, 0.2)));
        QVERIFY(!deletePoints(t, {1, 2}));
        // Move: all by the same amount.
        QVERIFY(movePoints(t, {3, 4}, QPointF(0.1, 0.0)));
        QVERIFY(near(*t.pointAt(4), QPointF(0.5, 0.4)));
        QVERIFY(near(*t.pointAt(0), QPointF(0.0, 0.0)));
        // Stretch from an end: the other end stays.
        Tracker line = withPoints(QStringLiteral("Point A"), {{0, 0.0, 0.0}, {1, 0.1, 0.0}, {2, 0.2, 0.0}, {3, 0.3, 0.0}, {4, 0.4, 0.0}});
        Tracker end = line;
        QVERIFY(stretchPoints(end, {0, 1, 2, 3, 4}, 4, QPointF(0.0, 0.4)));
        QVERIFY(near(*end.pointAt(0), QPointF(0.0, 0.0)));
        QVERIFY(near(*end.pointAt(2), QPointF(0.2, 0.2)));
        QVERIFY(near(*end.pointAt(4), QPointF(0.4, 0.4)));
        // From the middle: both ends stay.
        Tracker middle = line;
        QVERIFY(stretchPoints(middle, {0, 1, 2, 3, 4}, 2, QPointF(0.0, 0.2)));
        QVERIFY(near(*middle.pointAt(0), QPointF(0.0, 0.0)));
        QVERIFY(near(*middle.pointAt(1), QPointF(0.1, 0.1)));
        QVERIFY(near(*middle.pointAt(2), QPointF(0.2, 0.2)));
        QVERIFY(near(*middle.pointAt(4), QPointF(0.4, 0.0)));
        // Regions: a new one from the position, its boxes on the tracker there; deleting it takes its data.
        Tracker regions = line;
        regions.regions[0].target = QRectF(0.0, 0.0, 0.1, 0.1);
        QVERIFY(addRegion(regions, 3));
        QCOMPARE(regions.regions.size(), 2);
        QVERIFY(near(regions.regions.at(1).target.center(), QPointF(0.3, 0.0)));
        QVERIFY(!regions.pointAt(3)); // to be tracked
        QVERIFY(!addRegion(regions, 3));
        regions.setPoint(3, QPointF(0.9, 0.9));
        QVERIFY(deleteRegion(regions, 3));
        QCOMPARE(regions.regions.size(), 1);
        QVERIFY(regions.pointAt(3)); // the first region's own point shows again
        QVERIFY(deleteRegion(regions, 0));
        QVERIFY(!regions.hasData());
    }

    // Tracking data through segment edits (p.281-282): trims keep the data with the material, both parts of a
    // split keep theirs; and pasting trackers (p.283).
    void throughEdits() {
        QList<TrackPoint> points;
        for (std::int64_t f = 0; f < 40; ++f) points << TrackPoint{f, 0.01 * static_cast<double>(f), 0.5};
        auto tracked = [&] {
            model::Effect e = pip();
            TrackingSetup setup;
            setup.trackers << withPoints(QStringLiteral("Point A"), points);
            setup.trackers[0].reference = 5;
            setTracking(e, setup);
            return e;
        };
        // shiftTrackingData itself: the head trimmed in by 10 frames.
        model::Effect e = tracked();
        shiftTrackingData(e, -10);
        Tracker t = trackingOf(e).trackers.first();
        QVERIFY(near(*t.pointAt(0), QPointF(0.10, 0.5)));
        QVERIFY(near(*t.pointAt(-10), QPointF(0.0, 0.5))); // the trimmed material keeps its data
        QCOMPARE(t.reference, std::int64_t(-5));
        // Trimmed back out: as before.
        shiftTrackingData(e, 10);
        QCOMPARE(trackingOf(e), trackingOf(tracked()));
        // Trimmed out beyond the data: the first region still starts at the effect's first frame.
        shiftTrackingData(e, 4);
        t = trackingOf(e).trackers.first();
        QCOMPARE(t.regions.first().start, std::int64_t(0));
        QVERIFY(!t.pointAt(3));
        QVERIFY(near(*t.pointAt(4), QPointF(0.0, 0.5)));

        // In a sequence: a segment with the tracked effect, split by an Add Edit and trimmed at its head.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = QStringLiteral("V1");
        a.sourceIn = 20;
        a.length = 40;
        a.effect = tracked();
        seq.splice(QStringLiteral("V1"), 0, {a});
        QVERIFY(seq.addEdit(QStringLiteral("V1"), 15));
        const model::Track* v1 = seq.track(QStringLiteral("V1"));
        QCOMPARE(v1->segments.size(), 2);
        // "tracking data is preserved on all parts of the segment for the range of frames in each part".
        QVERIFY(near(*trackingOf(*v1->segments.at(0).effect).trackers.first().pointAt(14), QPointF(0.14, 0.5)));
        QVERIFY(near(*trackingOf(*v1->segments.at(1).effect).trackers.first().pointAt(0), QPointF(0.15, 0.5)));
        // Trimming the second part's head in by 5 (the roller's B side): its first frame is material frame 20.
        model::trim(seq, {model::TrimRoller{QStringLiteral("V1"), 15, model::TrimSide::B}}, model::TrimKind::Overwrite, 5);
        v1 = seq.track(QStringLiteral("V1"));
        const model::Segment& second = v1->segments.at(v1->segments.size() - 1);
        QCOMPARE(second.sourceIn, std::int64_t(40));
        QVERIFY(near(*trackingOf(*second.effect).trackers.first().pointAt(0), QPointF(0.20, 0.5)));

        // Pasting: below the others; a name in use gets a number; no more than eight.
        TrackingSetup setup = trackingOf(tracked());
        const QList<Tracker> copied = setup.trackers;
        QCOMPARE(pasteTrackers(setup, copied), 1);
        QCOMPARE(setup.trackers.size(), 2);
        QCOMPARE(setup.trackers.last().name, QStringLiteral("Point A 2"));
        QCOMPARE(setup.trackers.last().regions, setup.trackers.first().regions);
        for (int i = 0; i < 10; ++i) pasteTrackers(setup, copied);
        QCOMPARE(setup.trackers.size(), kMaxTrackers);
    }

    // What the data does: Position (with the Axis menu, p.275), Scaling and Rotation from two trackers, the
    // Tracking category's map (p.259).
    void changes() {
        model::Effect e = pip();
        QVERIFY(enableTracking(e, TrackedParameter::Position));
        TrackingSetup s = trackingOf(e);
        s.trackers[0].regions[0].points = {{0, 0.5, 0.5}, {10, 0.6, 0.4}};
        setTracking(e, s);
        const double aspect = 16.0 / 9.0;
        QVERIFY(near(trackedChange(e, 10, aspect).position, QPointF(0.1, -0.1)));
        TrackerMapping m = trackerMapping(e, TrackedParameter::Position);
        m.axis = QStringLiteral("X Tracking Only");
        setTrackerMapping(e, TrackedParameter::Position, m);
        QVERIFY(near(trackedChange(e, 10, aspect).position, QPointF(0.1, 0.0)));
        // The Position change in the effect the compositor draws: pixels of a 1920-wide frame.
        const model::Effect drawn = media::trackedEffect(e, 1.0, model::EffectSpan(11), aspect);
        QVERIFY(std::abs(drawn.parameters.value(QStringLiteral("Position")).toObject().value(QStringLiteral("X")).toDouble() - 192.0) < 1e-9);

        // Two trackers twice as far apart and turned a quarter turn clockwise.
        model::Effect warp{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
        QVERIFY(enableTracking(warp, TrackedParameter::Scaling));
        QVERIFY(enableTracking(warp, TrackedParameter::Rotation));
        TrackingSetup two = trackingOf(warp);
        two.trackers[0].regions[0].points = {{0, 0.5, 0.5}, {4, 0.5, 0.5}};
        two.trackers[1].regions[0].points = {{0, 0.5 + 0.1 / aspect, 0.5}, {4, 0.5, 0.7}};
        setTracking(warp, two);
        const TrackedChange c = trackedChange(warp, 4, aspect);
        QVERIFY(std::abs(c.scale - 2.0) < 1e-9);
        QVERIFY(std::abs(c.rotation - 90.0) < 1e-9);
        const model::Effect drawnWarp = media::trackedEffect(warp, 1.0, model::EffectSpan(5), aspect);
        QVERIFY(std::abs(drawnWarp.parameters.value(QStringLiteral("Rotation")).toObject().value(QStringLiteral("Z")).toDouble() - 90.0) < 1e-9);
        QVERIFY(std::abs(drawnWarp.parameters.value(QStringLiteral("Scaling (3D)")).toObject().value(QStringLiteral("X")).toDouble() - 200.0) < 1e-9);

        // Four trackers: the map takes the reference corners to where they are now.
        model::Effect corners{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
        for (int i = 0; i < 4; ++i) QVERIFY(enableTracking(corners, TrackedParameter::Tracking, i));
        TrackingSetup four = trackingOf(corners);
        const QList<QPointF> from{{0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}};
        const QList<QPointF> to{{0.3, 0.25}, {0.75, 0.2}, {0.85, 0.9}, {0.25, 0.7}};
        for (int i = 0; i < 4; ++i) {
            four.trackers[i].regions[0].points = {{0, from.at(i).x(), from.at(i).y()}, {6, to.at(i).x(), to.at(i).y()}};
        }
        setTracking(corners, four);
        const auto map = trackedChange(corners, 6, aspect).transform;
        QVERIFY(map);
        for (int i = 0; i < 4; ++i) {
            const auto& h = *map;
            const double z = h[6] * from.at(i).x() + h[7] * from.at(i).y() + h[8];
            const QPointF got((h[0] * from.at(i).x() + h[1] * from.at(i).y() + h[2]) / z, (h[3] * from.at(i).x() + h[4] * from.at(i).y() + h[5]) / z);
            QVERIFY(near(got, to.at(i), 1e-9));
        }
        // And the picture moved by a map.
        media::Picture dot(20, 10);
        float* p = dot.pixel(5, 5);
        p[0] = p[3] = 1.0f;
        const media::Picture moved = media::transformed(dot, {1, 0, 0.5, 0, 1, 0, 0, 0, 1});
        QVERIFY(moved.pixel(15, 5)[0] > 0.9f);
        QVERIFY(moved.pixel(5, 5)[0] < 0.1f);
    }

    // The engines follow the square (p.263-264).
    void engines() {
        const QRectF target(45.0 / 160, 35.0 / 90, 14.0 / 160, 14.0 / 90);
        const QRectF search(0, 0, 40.0 / 160, 30.0 / 90);
        int seen = 0;
        const QList<TrackPoint> correlation = media::trackTarget(media::TrackingEngine::Correlation, squareFrame, 0, 8, target, search, {},
                                                                 [&](const TrackPoint&) { ++seen; });
        QCOMPARE(correlation.size(), 9);
        QCOMPARE(seen, 9);
        const QPointF start(correlation.first().x * 160, correlation.first().y * 90);
        const QPointF end(correlation.last().x * 160, correlation.last().y * 90);
        QVERIFY2(near(end - start, QPointF(24, 8), 0.5), qPrintable(QStringLiteral("%1 %2").arg((end - start).x()).arg((end - start).y())));
        // Backwards too.
        const QList<TrackPoint> back = media::trackTarget(media::TrackingEngine::Correlation, squareFrame, 8, 5, QRectF(69.0 / 160, 43.0 / 90, 14.0 / 160, 14.0 / 90), search);
        QCOMPARE(back.last().frame, 5);
        QVERIFY(near(QPointF(back.last().x * 160, back.last().y * 90) - QPointF(back.first().x * 160, back.first().y * 90), QPointF(-9, -3), 0.5));
        // "press the space bar" (p.265).
        int asked = 0;
        const QList<TrackPoint> stopped = media::trackTarget(media::TrackingEngine::Correlation, squareFrame, 0, 8, target, search, [&] { return ++asked > 3; });
        QCOMPARE(stopped.size(), 4);
        // FluidTracker: the flow at a larger textured patch moving 4 pixels a frame across a 480 x 270 frame.
        auto patchFrame = [](std::int64_t f) {
            QImage image(480, 270, QImage::Format_RGB32);
            image.fill(QColor(30, 30, 30));
            QRandomGenerator texture(11);
            const int left = 150 + 4 * static_cast<int>(f);
            for (int y = 100; y < 160; ++y) {
                for (int x = 0; x < 60; ++x) {
                    const int v = 80 + static_cast<int>(texture.bounded(170));
                    image.setPixelColor(left + x, y, QColor(v, v, v));
                }
            }
            return image;
        };
        const QList<TrackPoint> fluid = media::trackTarget(media::TrackingEngine::Fluid, patchFrame, 0, 4, QRectF(170.0 / 480, 120.0 / 270, 20.0 / 480, 20.0 / 270), search);
        QCOMPARE(fluid.size(), 5);
        const double moved = (fluid.last().x - fluid.first().x) * 480;
        QVERIFY2(std::abs(moved - 16.0) < 2.0, qPrintable(QString::number(moved)));
        QCOMPARE(media::trackingEngine(QStringLiteral("FluidStabilizer")), media::TrackingEngine::FluidStabilizer);
    }
};

QTEST_MAIN(TestTracking)
#include "tst_tracking.moc"
