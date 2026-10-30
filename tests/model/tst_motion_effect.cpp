// Traditional motion effects' model (FX Guide p.165-173, p.561-563; docs/spec/motion-effects.md): which
// source frame each frame of a Freeze Frame, Variable Speed or Strobe Motion clip shows, the clips'
// lengths and names, keeping them in a bin item, and a segment's Motion Effect speed; the Timewarp
// effect's graphs, templates and Promote to Timewarp (FX Guide p.174-196, p.484-488).
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"

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

    void timewarpTemplates() {
        // A Timewarp applied to a 41-frame segment gets its template's graphs (p.203, p.484-488).
        const auto applied = [](const QString& name) {
            return timewarpForSegment(Effect{QLatin1String(kTimewarpCategory), name, {}}, 41);
        };
        // Plain Timewarp: 100%.
        const Effect plain = applied(QStringLiteral("Timewarp"));
        QVERIFY(isTimewarp(plain));
        QCOMPARE(timewarpPosition(plain, 17, 41), 17.0);
        // Reverse Motion through the Position graph: the last frame first.
        const Effect reverse = applied(QStringLiteral("Reverse Motion"));
        QVERIFY(timewarpOf(reverse).positionGraph);
        QCOMPARE(timewarpPosition(reverse, 0, 41), 40.0);
        QCOMPARE(timewarpPosition(reverse, 40, 41), 0.0);
        QCOMPARE(timewarpPosition(reverse, 10, 41), 30.0);
        // 0% To 100%: frozen at first, then speeding up; 100% to 0% ends on the segment's last frame.
        const Effect ramp = applied(QStringLiteral("0% To 100%"));
        QCOMPARE(timewarpPosition(ramp, 0, 41), 0.0);
        QVERIFY(timewarpPosition(ramp, 1, 41) < 0.1);
        QVERIFY(timewarpPosition(ramp, 40, 41) > 15.0 && timewarpPosition(ramp, 40, 41) < 25.0);
        const Effect down = applied(QStringLiteral("100% to 0%"));
        QCOMPARE(timewarpPosition(down, 40, 41), 40.0);
        QVERIFY(timewarpPosition(down, 0, 41) > 15.0 && timewarpPosition(down, 0, 41) < 25.0);
        // Speed Bump is slower in the middle; Speed Boost faster.
        const Effect bump = applied(QStringLiteral("Speed Bump"));
        QVERIFY(timewarpPosition(bump, 40, 41) < 39.0);
        const Effect boost = applied(QStringLiteral("Speed Boost"));
        QVERIFY(timewarpPosition(boost, 40, 41) > 41.0);
        // Trim to Fill: the same source frames over whatever length the segment is trimmed to.
        const Effect fill = applied(QStringLiteral("Trim to Fill"));
        QCOMPARE(timewarpPosition(fill, 20, 21), 40.0);
        QCOMPARE(timewarpPosition(fill, 10, 21), 20.0);
        // FluidFilm renders with FluidMotion (p.204), which blends until 6.5d.
        QVERIFY(blendsFrames(applied(QStringLiteral("FluidFilm Progressive"))));
        QVERIFY(!blendsFrames(plain));
        // Applying it to a segment through the model does the same.
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = QStringLiteral("clip");
        s.binId = QStringLiteral("bin");
        s.sourceIn = 100;
        s.length = 41;
        seq.track(QStringLiteral("V1"))->segments = {s};
        QVERIFY(applySegmentEffect(seq, QStringLiteral("V1"), 5, Effect{QLatin1String(kTimewarpCategory), QStringLiteral("Reverse Motion"), {}}));
        const Segment& warped = seq.track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(segmentSourceFrame(warped, 0), 140);
        QCOMPARE(segmentSourceFrame(warped, 40), 100);
        QCOMPARE(Sequence::fromJson(seq.toJson()).toJson(), seq.toJson());
    }

    void timewarpGraphs() {
        // The Speed graph adds up speeds from the Source Anchor Frame (p.181, p.187).
        Effect e = promotedTimewarp(0.5);
        QCOMPARE(timewarpPosition(e, 10, 100), 5.0);
        Timewarp t = timewarpOf(e);
        t.anchor = 20.0; // source frame 20 ...
        t.anchorAt = 0.5; // ... at the middle, frame 49.5 -> 50
        setTimewarp(e, t);
        QCOMPARE(timewarpPosition(e, 50, 100), 20.0);
        QCOMPARE(timewarpPosition(e, 40, 100), 15.0);
        // A keyframed freeze in the middle (the rolling clip that freezes, p.190-192): Shelf at 0%.
        Effect freeze = promotedTimewarp(1.0);
        const EffectSpan span(31);
        setKeyframe(freeze, QLatin1String(kSpeedCategory), QLatin1String(kSpeedParameter), 0.0, span, 100.0);
        setKeyframe(freeze, QLatin1String(kSpeedCategory), QLatin1String(kSpeedParameter), span.timeOf(10), span, 0.0);
        setKeyframe(freeze, QLatin1String(kSpeedCategory), QLatin1String(kSpeedParameter), span.timeOf(20), span, 100.0);
        ParameterKeyframes keys = keyframesOf(freeze, QLatin1String(kSpeedCategory), QLatin1String(kSpeedParameter));
        keys.interpolation = Interpolation::Shelf;
        setKeyframes(freeze, QLatin1String(kSpeedCategory), QLatin1String(kSpeedParameter), keys);
        QCOMPARE(timewarpPosition(freeze, 10, 31), 10.0);
        QCOMPARE(timewarpPosition(freeze, 15, 31), 10.0);
        QCOMPARE(timewarpPosition(freeze, 20, 31), 10.0);
        QCOMPARE(timewarpPosition(freeze, 25, 31), 15.0);
        // Strobe holds every nth output frame.
        t = timewarpOf(e);
        t.anchor = 0.0;
        t.anchorAt = 0.0;
        t.strobe = 4;
        setTimewarp(e, t);
        QCOMPARE(timewarpPosition(e, 7, 100), 2.0);
        QCOMPARE(timewarpPosition(e, 8, 100), 4.0);
        // Blended types mix the frames around a fractional position.
        t.strobe = 1;
        t.type = TimewarpType::BlendedInterpolated;
        setTimewarp(e, t);
        QVERIFY(blendsFrames(e));
        Segment s;
        s.clipId = QStringLiteral("clip");
        s.sourceIn = 10;
        s.length = 100;
        s.effect = e;
        QCOMPARE(segmentSourceFrame(s, 3), 11);
        QCOMPARE(segmentSourcePosition(s, 3), 11.5);
        QCOMPARE(timewarpOf(Effect::fromJson(e.toJson())), t);
    }

    void promote() {
        // Promote to Timewarp (p.177): a segment's Motion Effect becomes a Timewarp at its speed ...
        Segment s;
        s.clipId = QStringLiteral("clip");
        s.binId = QStringLiteral("bin");
        s.sourceIn = 10;
        s.length = 20;
        QVERIFY(!promoteToTimewarp(s, {}));
        s.effect = motionEffect(0.5);
        QVERIFY(promoteToTimewarp(s, {}));
        QVERIFY(isTimewarp(*s.effect));
        QCOMPARE(segmentSourceFrame(s, 6), 13);
        // ... and a motion effect clip's segment shows its source clip, from the same frame on, without
        // its strobe ("Strobe motion information does not carry over").
        BinItem clip = motionEffectClip(source(), QStringLiteral("bin"), 30, 59, 0.5, 3, FieldMotion::DuplicatedField,
                                        QDateTime::currentDateTimeUtc());
        Segment m;
        m.binId = QStringLiteral("bin");
        m.clipId = clip.id;
        m.sourceIn = 8; // strobe 3 holds the clip's frame 6 there: source frame 30 + 6 x 0.5
        m.length = 10;
        const auto resolve = [&](const QString&, const QString& id) -> std::optional<BinItem> {
            return id == clip.id ? std::optional<BinItem>(clip) : std::nullopt;
        };
        QVERIFY(promoteToTimewarp(m, resolve));
        QCOMPARE(m.clipId, QStringLiteral("clip"));
        QCOMPARE(m.sourceIn, 33);
        QCOMPARE(segmentSourceFrame(m, 4), 35);
        QCOMPARE(timewarpOf(*m.effect).strobe, 1);
    }
};

QTEST_GUILESS_MAIN(TestMotionEffect)
#include "tst_motion_effect.moc"
