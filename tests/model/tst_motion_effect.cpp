// Traditional motion effects' model (FX Guide p.165-173, p.561-563; docs/spec/motion-effects.md): which
// source frame each frame of a Freeze Frame, Variable Speed or Strobe Motion clip shows, the clips'
// lengths and names, keeping them in a bin item, and a segment's Motion Effect speed.
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem source() {
    BinItem item;
    item.id = QStringLiteral("clip");
    item.name = QStringLiteral("Turbocharge");
    item.editRate = Rational(30);
    item.durationFrames = 100;
    return item;
}

// A motion effect from frame `start` at `speed` with `strobe`.
MotionEffect motion(std::int64_t start, double speed, int strobe = 1) {
    MotionEffect m;
    m.sourceBin = QStringLiteral("bin");
    m.sourceClip = QStringLiteral("clip");
    m.sourceStart = start;
    m.speed = speed;
    m.strobe = strobe;
    return m;
}

} // namespace

class TestMotionEffect : public QObject {
    Q_OBJECT

private slots:
    void frames() {
        // Half speed holds each frame twice ("slow-motion effects by duplicating frames", p.168).
        const MotionEffect half = motion(10, 0.5);
        QCOMPARE(half.sourceFrame(0), 10);
        QCOMPARE(half.sourceFrame(1), 10);
        QCOMPARE(half.sourceFrame(2), 11);
        QCOMPARE(half.sourceFrame(5), 12);
        // Double speed leaves every other frame out.
        const MotionEffect fast = motion(10, 2.0);
        QCOMPARE(fast.sourceFrame(3), 16);
        // A third: three frames each, without falling short through rounding.
        const MotionEffect third = motion(0, 1.0 / 3.0);
        QCOMPARE(third.sourceFrame(2), 0);
        QCOMPARE(third.sourceFrame(3), 1);
        QCOMPARE(third.sourceFrame(6), 2);
        // Reverse from the start frame back.
        const MotionEffect reverse = motion(50, -1.0);
        QCOMPARE(reverse.sourceFrame(0), 50);
        QCOMPARE(reverse.sourceFrame(7), 43);
        const MotionEffect slowReverse = motion(50, -0.5);
        QCOMPARE(slowReverse.sourceFrame(1), 50);
        QCOMPARE(slowReverse.sourceFrame(2), 49);
        // Strobe: "a rate of 5 causes every fifth frame to be held for five frames" (p.171).
        const MotionEffect strobe = motion(0, 1.0, 5);
        QCOMPARE(strobe.sourceFrame(4), 0);
        QCOMPARE(strobe.sourceFrame(5), 5);
        QCOMPARE(strobe.sourceFrame(9), 5);
        // A freeze frame never moves.
        const MotionEffect freeze = motion(33, 0.0);
        QCOMPARE(freeze.sourceFrame(1000), 33);
        // Real time (p.172): freeze and forward speeds; not reverse or strobe.
        QVERIFY(freeze.isRealTime());
        QVERIFY(half.isRealTime());
        QVERIFY(!reverse.isRealTime());
        QVERIFY(!strobe.isRealTime());
    }

    void clips() {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        // "The new clip has the original clip name followed by the letters FF" (p.167).
        const BinItem ff = freezeFrameClip(source(), QStringLiteral("bin"), 12, 60, FieldMotion::DuplicatedField, now);
        QCOMPARE(ff.kind, BinItemKind::MotionEffect);
        QCOMPARE(ff.name, QStringLiteral("Turbocharge FF"));
        QCOMPARE(ff.durationFrames, 60);
        QCOMPARE(ff.editRate, Rational(30));
        QVERIFY(motionEffectOf(ff)->isFreeze());
        QCOMPARE(motionEffectOf(ff)->sourceStart, 12);
        // 3 seconds at 50% become 6 (p.165); "the original clip name followed by the frame rate in
        // parentheses" (p.170).
        const BinItem slow = motionEffectClip(source(), QStringLiteral("bin"), 0, 89, 0.5, 1, FieldMotion::DuplicatedField, now);
        QCOMPARE(slow.durationFrames, 180);
        QCOMPARE(slow.name, QStringLiteral("Turbocharge (15FPS)"));
        // Strobe alone, and the two together as "Turbocharge (15FPS) (Strobe: 5)" (p.178).
        QCOMPARE(motionEffectClip(source(), QStringLiteral("bin"), 0, 9, 1.0, 5, FieldMotion::DuplicatedField, now).name,
                 QStringLiteral("Turbocharge Strobe (5)"));
        QCOMPARE(motionEffectClip(source(), QStringLiteral("bin"), 0, 9, 0.5, 5, FieldMotion::DuplicatedField, now).name,
                 QStringLiteral("Turbocharge (15FPS) (Strobe: 5)"));
        // Reverse starts at the OUT and runs back to the IN.
        const BinItem back = motionEffectClip(source(), QStringLiteral("bin"), 20, 29, -1.0, 1, FieldMotion::DuplicatedField, now);
        QCOMPARE(back.durationFrames, 10);
        QCOMPARE(motionEffectOf(back)->sourceFrame(0), 29);
        QCOMPARE(motionEffectOf(back)->sourceFrame(9), 20);
        QCOMPARE(back.name, QStringLiteral("Turbocharge (-30FPS)"));
        // Kept through the bin file.
        const auto again = binItemFromJson(binItemToJson(slow));
        QVERIFY(again);
        QCOMPARE(motionEffectOf(*again), motionEffectOf(slow));
        QVERIFY(!motionEffectOf(source()));
    }

    void segmentSpeed() {
        // A segment's Motion Effect (Fit to Fill's, an import's) plays at its speed from sourceIn.
        Segment s;
        s.clipId = QStringLiteral("clip");
        s.sourceIn = 100;
        s.length = 40;
        QCOMPARE(segmentSourceFrame(s, 7), 107);
        s.effect = motionEffect(0.5);
        QCOMPARE(segmentSourceFrame(s, 7), 103);
        s.effect = motionEffect(0.0);
        QCOMPARE(segmentSourceFrame(s, 7), 100);
        s.effect = motionEffect(-1.0);
        QCOMPARE(segmentSourceFrame(s, 7), 93);
    }
};

QTEST_GUILESS_MAIN(TestMotionEffect)
#include "tst_motion_effect.moc"
