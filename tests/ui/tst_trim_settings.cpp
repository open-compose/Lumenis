// The Trim settings (Editing Guide p.1223; docs/spec/trim-settings.md) and what they drive: the Play
// Loop of Trim mode with Preroll, Postroll and Intermission, trimming during the loop (p.657,
// p.659-660), Edit Review (p.657-658), J-K-L Trim (p.658-659), the trim displays' outgoing and
// incoming frames, Small Trim display (p.648-649), Dual Image Play (p.659) and Sync Rollers (p.656).
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QSignalSpy>
#include <QWidget>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

Segment seg(const QString& clipId, const QString& track, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = track;
    s.length = length;
    return s;
}

// A Timeline over V1 a (100) b (100) and A1 x (100) y (100), at 25 frames a second: one transition,
// at 100, on both tracks.
struct Fixture {
    Composer composer;
    Timeline timeline{composer};

    explicit Fixture(bool syncLocks = false) {
        timeline.resize(1000, 360);
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 100), seg(QStringLiteral("b"), QStringLiteral("V1"), 100)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 100), seg(QStringLiteral("y"), QStringLiteral("A1"), 100)});
        if (syncLocks) {
            for (const QString& t : seq.trackNames()) seq.track(t)->syncLocked = true;
        }
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        composer.setActiveMonitor(MonitorId::Record);
        set([](Timeline::Options& o) {
            o.trimPreroll = 2;
            o.trimPostroll = 1;
        });
    }
    void set(const std::function<void(Timeline::Options&)>& change) {
        Timeline::Options o = timeline.options();
        change(o);
        timeline.setOptions(o);
    }
    std::int64_t position() const { return composer.state(MonitorId::Record).position(); }
    std::int64_t firstLength() const {
        return Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.first().length;
    }
    // Into Trim mode at the transition (the position moves there).
    void trimAtCut() {
        composer.moveTo(MonitorId::Record, 95);
        QVERIFY(timeline.enterTrimMode(0));
        QCOMPARE(timeline.rollers().size(), 2);
    }
};

} // namespace

class TestTrimSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // "This loop begins at a preroll point before the transition and ends at a postroll point"
    // (p.657); a trim during the loop is seen in the next pass (p.660).
    void playLoop() {
        Fixture f;
        f.trimAtCut();
        QVERIFY(f.timeline.perform(QStringLiteral("trim.play_loop")));
        QVERIFY(f.timeline.isTrimLooping());
        QVERIFY(f.composer.isLooping());
        QCOMPARE(f.position(), 50); // 2 seconds before 100
        // The Play Loop button of the trim display is lit.
        auto* button = f.composer.findChild<QToolButton*>(QStringLiteral("trim.play_loop"));
        QVERIFY(button && button->isChecked());
        f.composer.advance(2000); // 50 frames: to 100
        QCOMPARE(f.position(), 100);
        // Trim forward 5 while it plays: the position stays with the loop.
        QVERIFY(f.timeline.perform(QStringLiteral("trim.trim_forward_1_frame")));
        QVERIFY(f.timeline.trimBy(4));
        QCOMPARE(f.firstLength(), 105);
        QCOMPARE(f.position(), 100);
        // To the postroll's end (1 second after the old cut, 124), then the next pass starts 2 seconds
        // before the new cut.
        f.composer.advance(960);
        QCOMPARE(f.position(), 55);
        // The pass now ends at 129.
        f.composer.advance(2920);
        QCOMPARE(f.position(), 128);
        f.composer.advance(40);
        QCOMPARE(f.position(), 55);
        // Play Loop again stops it; Trim mode stays.
        QVERIFY(f.timeline.perform(QStringLiteral("trim.play_loop")));
        QVERIFY(!f.composer.isLooping());
        QVERIFY(!f.timeline.isTrimLooping());
        QVERIFY(f.timeline.isTrimMode());
        QVERIFY(!button->isChecked());
        // So does the space bar (Play), p.660.
        QVERIFY(f.timeline.perform(QStringLiteral("trim.play_loop")));
        f.composer.perform(QStringLiteral("play.play"));
        QVERIFY(!f.timeline.isTrimLooping());
    }

    // The Intermission waits at the end of each pass (ours: a pause, docs/spec/trim-settings.md).
    void intermission() {
        Fixture f;
        f.set([](Timeline::Options& o) { o.trimIntermission = 1; });
        f.trimAtCut();
        QVERIFY(f.timeline.playTrimLoop());
        f.composer.advance(2960); // to 124
        QCOMPARE(f.position(), 124);
        QVERIFY(f.composer.isLoopPaused());
        QVERIFY(f.composer.isLooping());
        QTRY_VERIFY_WITH_TIMEOUT(!f.composer.isLoopPaused(), 3000);
        QVERIFY(f.position() < 100); // round again from 50
        f.composer.stop();
        QVERIFY(!f.composer.isLooping());
    }

    // "Click the Play Loop button ... twice ... The action takes you to the last trimmed transition"
    // (p.656): from Source/Record mode it enters Trim mode first.
    void playLoopFromSourceRecord() {
        Fixture f;
        f.trimAtCut();
        f.timeline.exitTrimMode();
        f.composer.moveTo(MonitorId::Record, 10);
        QVERIFY(f.timeline.playTrimLoop());
        QVERIFY(f.timeline.isTrimMode());
        QCOMPARE(f.timeline.rollers().first().frame, 100);
        QCOMPARE(f.position(), 50);
        f.composer.stop();
    }

    // Edit Review (p.657-658): plays from the Preroll before the previous transition; in Trim mode it
    // deselects the rollers.
    void editReview() {
        Fixture f;
        f.composer.moveTo(MonitorId::Record, 150);
        QVERIFY(f.timeline.perform(QStringLiteral("play.edit_review")));
        QCOMPARE(f.position(), 50);
        QVERIFY(f.composer.isPlaying());
        f.composer.stop();
        f.trimAtCut();
        QVERIFY(f.timeline.editReview());
        QVERIFY(!f.timeline.isTrimMode());
        QCOMPARE(f.position(), 50);
        f.composer.stop();
        // Before the first transition there is nothing to review.
        f.composer.moveTo(MonitorId::Record, 20);
        QVERIFY(!f.timeline.editReview());
    }

    // J-K-L Trim (p.658-659): L plays the trim and K completes it; K held with L tapped trims a frame;
    // off, the keys only play.
    void jklTrim() {
        Fixture f;
        f.trimAtCut();
        f.composer.perform(QStringLiteral("play.play_forward"));
        f.composer.advance(400); // 10 frames
        f.composer.perform(QStringLiteral("play.pause"));
        QCOMPARE(f.firstLength(), 110);
        QCOMPARE(f.timeline.rollers().first().frame, 110);
        QCOMPARE(f.position(), 110);
        // K and L: one frame.
        f.composer.setFocus();
        QTest::keyPress(&f.composer, Qt::Key_K);
        QTest::keyPress(&f.composer, Qt::Key_L);
        QTest::keyRelease(&f.composer, Qt::Key_L);
        QTest::keyRelease(&f.composer, Qt::Key_K);
        QCOMPARE(f.firstLength(), 111);
        QVERIFY(f.timeline.isTrimMode());
        // J-K-L Trim off: play moves only the position.
        f.set([](Timeline::Options& o) { o.jklTrim = false; });
        f.composer.perform(QStringLiteral("play.play_forward"));
        f.composer.advance(400);
        f.composer.perform(QStringLiteral("play.pause"));
        QCOMPARE(f.firstLength(), 111);
    }

    // The trim monitors show the sequence's last outgoing and first incoming frames (p.648, p.655).
    // While the Record monitor plays, the side last clicked follows; with Dual Image Play both do
    // (p.659).
    void trimFrames() {
        Fixture f;
        // Each frame a different red.
        f.composer.setPictureResolver([](MonitorId, std::int64_t frame, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(static_cast<int>(frame % 256), 0, 0));
            return image;
        });
        f.composer.resize(900, 420);
        f.composer.show();
        f.set([](Timeline::Options& o) { o.jklTrim = false; });
        f.trimAtCut();
        QCOMPARE(f.composer.trimDisplay().aFrame, 99);
        QCOMPARE(f.composer.trimDisplay().bFrame, 100);
        // Big Trim: the Source monitor shows frame 99 of the sequence, the Record monitor frame 100.
        auto red = [](QWidget* w) {
            const QImage shot = w->grab().toImage();
            return qRed(shot.pixel(shot.width() / 2, shot.height() / 2));
        };
        QCOMPARE(red(f.composer.picture(MonitorId::Source)), 99);
        QCOMPARE(red(f.composer.picture(MonitorId::Record)), 100);
        f.composer.perform(QStringLiteral("play.play_forward"));
        f.composer.advance(400);
        QCOMPARE(f.composer.trimFrame(TrimSide::B), 110);
        QCOMPARE(f.composer.trimFrame(TrimSide::A), 99);
        f.composer.stop();
        // Clicking the outgoing picture makes that side follow play, in the Record monitor.
        QTest::mouseClick(f.composer.picture(MonitorId::Source), Qt::LeftButton);
        QCOMPARE(f.composer.trimPlaySide(), TrimSide::A);
        QCOMPARE(f.composer.activeMonitor(), MonitorId::Record);
        f.composer.perform(QStringLiteral("play.play_forward"));
        f.composer.advance(400);
        QCOMPARE(f.composer.trimFrame(TrimSide::A), f.position() - 1);
        QCOMPARE(f.composer.trimFrame(TrimSide::B), 100);
        f.composer.stop();
        // Dual Image Play.
        f.timeline.exitTrimMode();
        f.set([](Timeline::Options& o) { o.dualImagePlay = true; });
        f.trimAtCut();
        f.composer.perform(QStringLiteral("play.play_forward"));
        f.composer.advance(400);
        QCOMPARE(f.composer.trimFrame(TrimSide::B), 110);
        QCOMPARE(f.composer.trimFrame(TrimSide::A), 109);
        f.composer.stop();
    }

    // Small Trim display (p.648-649, p.1223): the Source monitor keeps its controls; the Record
    // monitor gives way to the two trim pictures with their counters.
    void smallTrim() {
        Fixture f;
        f.composer.resize(900, 420);
        f.composer.show();
        auto* sourceControls = f.composer.findChild<QWidget*>(QStringLiteral("sourceControls"));
        auto* recordControls = f.composer.findChild<QWidget*>(QStringLiteral("recordControls"));
        QVERIFY(sourceControls && recordControls);
        // Never: Big Trim.
        f.trimAtCut();
        QVERIFY(!f.composer.trimDisplay().small);
        QVERIFY(!f.composer.smallTrim()->isVisible());
        QVERIFY(!sourceControls->isVisible());
        f.timeline.exitTrimMode();
        // Always.
        f.set([](Timeline::Options& o) { o.smallTrim = Timeline::Options::SmallTrim::Always; });
        f.trimAtCut();
        QVERIFY(f.composer.trimDisplay().small);
        QVERIFY(f.composer.smallTrim()->isVisible());
        QVERIFY(!f.composer.picture(MonitorId::Record)->isVisible());
        QVERIFY(f.composer.picture(MonitorId::Source)->isVisible());
        QVERIFY(sourceControls->isVisible());
        QVERIFY(!recordControls->isVisible());
        QVERIFY(f.composer.trimBar()->isVisible());
        auto* counterA = f.composer.findChild<QToolButton*>(QStringLiteral("smallTrimCounterA"));
        QVERIFY(counterA && counterA->isVisible());
        QVERIFY(f.timeline.trimBy(3));
        QCOMPARE(counterA->text(), QStringLiteral("3"));
        // A counter selects its side (p.651).
        counterA->click();
        QCOMPARE(f.timeline.rollers().first().side, TrimSide::A);
        f.timeline.exitTrimMode();
        QVERIFY(!f.composer.smallTrim()->isVisible());
        QVERIFY(f.composer.picture(MonitorId::Record)->isVisible());
        QVERIFY(recordControls->isVisible());
        // "Go to Transition": only after Go to Previous/Next Event.
        f.set([](Timeline::Options& o) { o.smallTrim = Timeline::Options::SmallTrim::GoTo; });
        f.trimAtCut();
        QVERIFY(!f.composer.trimDisplay().small);
        f.composer.perform(QStringLiteral("move.go_to_previous_event"));
        QVERIFY(f.timeline.isTrimMode());
        QVERIFY(f.composer.trimDisplay().small);
        f.timeline.exitTrimMode();
        f.trimAtCut();
        QVERIFY(!f.composer.trimDisplay().small);
    }

    // Sync Rollers for Sync Locked Tracks (p.656): gray rollers on the sync-locked tracks without one,
    // for a single-roller trim; Shift-clicking a transition on such a track moves the trim there.
    void syncRollers() {
        Fixture f(true);
        const QList<TrimRoller> a{{QStringLiteral("V1"), 100, TrimSide::A}};
        f.timeline.setRollers(a, TrimKind::Ripple);
        QVERIFY(f.timeline.syncRollers().isEmpty()); // the setting is off
        f.set([](Timeline::Options& o) { o.syncRollers = true; });
        QStringList tracks;
        for (const TrimRoller& r : f.timeline.syncRollers()) {
            tracks << r.track;
            QCOMPARE(r.frame, 100);
            QCOMPARE(r.side, TrimSide::A);
        }
        QVERIFY(tracks.contains(QStringLiteral("A1")));
        QVERIFY(!tracks.contains(QStringLiteral("V1")));
        // Dual rollers and Overwrite trims have none.
        f.timeline.setRollers({{QStringLiteral("V1"), 100, TrimSide::Both}}, TrimKind::Ripple);
        QVERIFY(f.timeline.syncRollers().isEmpty());
        f.timeline.setRollers(a, TrimKind::Overwrite);
        QVERIFY(f.timeline.syncRollers().isEmpty());
        // Shift-click on A1, which has a sync roller: the trim moves to it.
        f.timeline.setRollers(a, TrimKind::Ripple);
        f.timeline.clickTransition({QStringLiteral("A1"), 100, TrimSide::A}, TrimKind::Ripple, true);
        QCOMPARE(f.timeline.rollers().size(), 1);
        QCOMPARE(f.timeline.rollers().first().track, QStringLiteral("A1"));
        // Without sync locks there are none.
        Fixture plain;
        plain.set([](Timeline::Options& o) { o.syncRollers = true; });
        plain.timeline.setRollers(a, TrimKind::Ripple);
        QVERIFY(plain.timeline.syncRollers().isEmpty());
    }
};

QTEST_MAIN(TestTrimSettings)
#include "tst_trim_settings.moc"
