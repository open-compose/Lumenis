// Keyframes on effect parameters (FX Guide ch.4, p.94-134; docs/spec/effect-keyframes.md): the four
// interpolations, Hold and Extrapolate, Elastic and Fixed, Reverse Keyframes, colours, and the Fade
// Effect button's keyframes.
#include "lumenis/model/effect_keyframes.h"

#include <QTest>

#include <cmath>

using namespace lumenis::model;

namespace {

const QString kFg = QStringLiteral("Foreground");
const QString kLevel = QStringLiteral("Level");

Effect withKeys(Interpolation interpolation, QList<std::pair<double, double>> keys, bool extrapolate = false) {
    Effect e;
    e.name = QStringLiteral("Picture-in-Picture");
    ParameterKeyframes p;
    p.interpolation = interpolation;
    p.extrapolate = extrapolate;
    for (const auto& [at, v] : keys) {
        EffectKeyframe k;
        k.at = at;
        k.value = v;
        p.keys << k;
    }
    setKeyframes(e, kFg, kLevel, p);
    return e;
}

double level(const Effect& e, double t, std::int64_t length = 101) {
    return *numberAt(e, kFg, kLevel, t, length);
}

} // namespace

class TestEffectKeyframes : public QObject {
    Q_OBJECT

private slots:
    void interpolations() {
        // Linear: a straight ramp (p.121).
        const Effect linear = withKeys(Interpolation::Linear, {{0.0, 0.0}, {1.0, 100.0}});
        QCOMPARE(level(linear, 0.25), 25.0);
        // Shelf: "does not change between keyframes and then changes abruptly" (p.120).
        const Effect shelf = withKeys(Interpolation::Shelf, {{0.0, 0.0}, {0.5, 60.0}, {1.0, 100.0}});
        QCOMPARE(level(shelf, 0.49), 0.0);
        QCOMPARE(level(shelf, 0.5), 60.0);
        // Spline: through the keyframes, easing in and out between two (p.121).
        const Effect spline = withKeys(Interpolation::Spline, {{0.0, 0.0}, {1.0, 100.0}});
        QVERIFY(level(spline, 0.25) < 25.0);
        QVERIFY(level(spline, 0.75) > 75.0);
        QVERIFY(std::abs(level(spline, 0.5) - 50.0) < 1e-9);
        QCOMPARE(level(spline, 1.0), 100.0);
        // Three keyframes make an arc through the middle one.
        const Effect arc = withKeys(Interpolation::Spline, {{0.0, 0.0}, {0.5, 100.0}, {1.0, 0.0}});
        QVERIFY(std::abs(level(arc, 0.5) - 100.0) < 1e-9);
        QVERIFY(level(arc, 0.4) > 80.0);
        // Bézier: smooth, through the keyframes; handles tug the curve (p.122).
        Effect bezier = withKeys(Interpolation::Bezier, {{0.0, 0.0}, {1.0, 100.0}});
        QVERIFY(std::abs(level(bezier, 0.5) - 50.0) < 0.5);
        QVERIFY(level(bezier, 0.2) < 20.0);
        ParameterKeyframes p = keyframesOf(bezier, kFg, kLevel);
        p.keys[0].outTime = 0.3;
        p.keys[0].outValue = 90.0; // leaving steeply
        setKeyframes(bezier, kFg, kLevel, p);
        QVERIFY(level(bezier, 0.2) > 30.0);
        QCOMPARE(level(bezier, 1.0), 100.0);
    }

    void holdAndExtrapolate() {
        // "The default condition is Hold" (p.127): the bracketing values outside the keyframes.
        const Effect hold = withKeys(Interpolation::Linear, {{0.25, 20.0}, {0.75, 70.0}});
        QCOMPARE(level(hold, 0.0), 20.0);
        QCOMPARE(level(hold, 1.0), 70.0);
        // Extrapolate carries the change on (p.125).
        const Effect extra = withKeys(Interpolation::Linear, {{0.25, 20.0}, {0.75, 70.0}}, true);
        QCOMPARE(level(extra, 0.0), -5.0);
        QCOMPARE(level(extra, 1.0), 95.0);
        // A single keyframe, or no keyframes: one value.
        QCOMPARE(level(withKeys(Interpolation::Linear, {{0.5, 40.0}}, true), 0.9), 40.0);
        Effect plain;
        plain.parameters = QJsonObject{{kFg, QJsonObject{{kLevel, 30}}}};
        QCOMPARE(level(plain, 0.7), 30.0);
        QVERIFY(!numberAt(Effect{}, kFg, kLevel, 0.5, 10));
    }

    void elasticAndFixed() {
        // Elastic keyframes scale with the effect's length; Fixed ones keep their frames (p.124).
        Effect elastic;
        setKeyframe(elastic, kFg, kLevel, 0.0, 11, 0);
        setKeyframe(elastic, kFg, kLevel, 1.0, 11, 100);
        QCOMPARE(level(elastic, 0.5, 11), 50.0);
        QCOMPARE(level(elastic, 0.5, 21), 50.0);
        Effect fixed;
        setKeyframe(fixed, kFg, kLevel, 0.0, 11, 0); // frame 0
        ParameterKeyframes p = keyframesOf(fixed, kFg, kLevel);
        p.fixed = true;
        setKeyframes(fixed, kFg, kLevel, p);
        setKeyframe(fixed, kFg, kLevel, 1.0, 11, 100); // frame 10
        QCOMPARE(keyframesOf(fixed, kFg, kLevel).keys.at(1).at, 10.0);
        QCOMPARE(level(fixed, 0.5, 11), 50.0);
        // Twice as long: the animation still ends at frame 10, halfway.
        QCOMPARE(level(fixed, 0.5, 21), 100.0);
        QCOMPARE(level(fixed, 0.25, 21), 50.0);
        // Setting at an existing keyframe replaces its value; removing takes it away.
        setKeyframe(elastic, kFg, kLevel, 1.0, 11, 80);
        QCOMPARE(keyframesOf(elastic, kFg, kLevel).keys.size(), 2);
        QCOMPARE(level(elastic, 1.0, 11), 80.0);
        QVERIFY(removeKeyframe(elastic, kFg, kLevel, 1.0, 11));
        QVERIFY(!removeKeyframe(elastic, kFg, kLevel, 0.5, 11));
        QCOMPARE(keyframedParameters(elastic), QStringList{QStringLiteral("Foreground/Level")});
        QVERIFY(removeKeyframe(elastic, kFg, kLevel, 0.0, 11));
        QVERIFY(keyframedParameters(elastic).isEmpty());
        QVERIFY(!elastic.parameters.contains(QLatin1String(kKeyframesKey)));
    }

    void reverse() {
        // "Reverse Keyframes does not affect keyframe parameter values. It changes only the position of
        // keyframes in time" (p.124).
        Effect e = withKeys(Interpolation::Linear, {{0.0, 0.0}, {0.25, 100.0}});
        reverseKeyframes(e, 101);
        QCOMPARE(level(e, 0.75), 100.0);
        QCOMPARE(level(e, 1.0), 0.0);
        QVERIFY(!e.parameters.value(kFg).toObject().value(QStringLiteral("Swap Sources")).toBool());
        // The whole of a transition: Swap Sources turns on.
        reverseKeyframes(e, EffectSpan(101, true));
        QVERIFY(e.parameters.value(kFg).toObject().value(QStringLiteral("Swap Sources")).toBool());
        QCOMPARE(level(e, 0.25), 100.0);
        // Only the parameters named.
        Effect two = withKeys(Interpolation::Linear, {{0.0, 0.0}, {1.0, 100.0}});
        setKeyframe(two, QStringLiteral("Position"), QStringLiteral("H Pos"), 0.0, 101, -50);
        setKeyframe(two, QStringLiteral("Position"), QStringLiteral("H Pos"), 1.0, 101, 50);
        reverseKeyframes(two, 101, {QStringLiteral("Position/H Pos")});
        QCOMPARE(*numberAt(two, QStringLiteral("Position"), QStringLiteral("H Pos"), 0.0, 101), 50.0);
        QCOMPARE(level(two, 0.0), 0.0);
    }

    void colours() {
        Effect e;
        setKeyframe(e, QStringLiteral("Background Color"), QStringLiteral("Background Color"), 0.0, 11, QStringLiteral("#000000"));
        setKeyframe(e, QStringLiteral("Background Color"), QStringLiteral("Background Color"), 1.0, 11, QStringLiteral("#ff0000"));
        const auto mid = colorAt(e, QStringLiteral("Background Color"), QStringLiteral("Background Color"), 0.5, 11);
        QVERIFY(mid);
        QCOMPARE(mid->red, 0.5);
        QCOMPARE(mid->green, 0.0);
        Effect plain;
        plain.parameters = QJsonObject{{QStringLiteral("Background Color"), QJsonObject{{QStringLiteral("Background Color"), QStringLiteral("#0000ff")}}}};
        QCOMPARE(colorAt(plain, QStringLiteral("Background Color"), QStringLiteral("Background Color"), 0.3, 11)->blue, 1.0);
    }

    void fadeEffectKeyframes() {
        // "Type the number of frames to fade up and fade down" (p.47): 0 to 100 over the first 10
        // frames and back to 0 over the last 20 of a 101-frame effect.
        Effect e;
        fadeEffect(e, 101, 10, 20);
        QCOMPARE(level(e, 0.0), 0.0);
        QCOMPARE(level(e, 0.05), 50.0);
        QCOMPARE(level(e, 0.1), 100.0);
        QCOMPARE(level(e, 0.5), 100.0);
        QCOMPARE(level(e, 0.9), 50.0);
        QCOMPARE(level(e, 1.0), 0.0);
        // Up only.
        fadeEffect(e, 101, 10, 0);
        QCOMPARE(level(e, 1.0), 100.0);
        // Neither: no keyframes.
        fadeEffect(e, 101, 0, 0);
        QVERIFY(keyframedParameters(e).isEmpty());
    }

    void spans() {
        // A segment effect's frames run 0 to 1; a transition's at its progress (ours, as the compositor).
        const EffectSpan seg(11);
        QCOMPARE(seg.timeOf(0), 0.0);
        QCOMPARE(seg.timeOf(10), 1.0);
        QCOMPARE(seg.frameOf(0.5), 5.0);
        const EffectSpan tr(4, true);
        QCOMPARE(tr.timeOf(0), 0.2);
        QCOMPARE(tr.timeOf(3), 0.8);
        QCOMPARE(tr.frameOf(0.4), 1.0);
        // Fixed keyframes on a transition are in its frames.
        Effect e;
        setKeyframe(e, kFg, kLevel, tr.timeOf(1), tr, 0);
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        setFixed(p, true, tr);
        QCOMPARE(p.keys.at(0).at, 1.0);
        QCOMPARE(keyframeTime(p, p.keys.at(0), tr), 0.4);
        setFixed(p, false, tr);
        QCOMPARE(p.keys.at(0).at, 0.4);
    }

    void fixedThroughTrims() {
        // "Fixed keyframes let you 'pin' a parameter change to a particular timecode" (p.124): a
        // segment of 11 frames with Level 0 at frame 0 and 100 at frame 10; adding 5 frames at the
        // head leaves the change on the same material, now at frames 5 to 15.
        Effect e;
        setKeyframe(e, kFg, kLevel, 0.0, 11, 0);
        setKeyframe(e, kFg, kLevel, 1.0, 11, 100);
        Effect elastic = e;
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        setFixed(p, true, 11);
        setKeyframes(e, kFg, kLevel, p);
        shiftFixedKeyframes(e, 5);
        QCOMPARE(level(e, 0.0, 16), 0.0);
        QCOMPARE(level(e, 5.0 / 15, 16), 0.0);
        QCOMPARE(level(e, 10.0 / 15, 16), 50.0);
        QCOMPARE(level(e, 1.0, 16), 100.0);
        // Elastic keyframes scale with the new length (p.124) and do not move.
        shiftFixedKeyframes(elastic, 5);
        QCOMPARE(level(elastic, 0.5, 16), 50.0);
    }

    void moving() {
        // "You cannot move a keyframe past another keyframe" (p.114): a frame short.
        Effect e = withKeys(Interpolation::Linear, {{0.0, 0.0}, {0.5, 50.0}, {1.0, 100.0}});
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        QVERIFY(moveKeyframe(p, 1, 0.95, std::nullopt, 11));
        QCOMPARE(p.keys.at(1).at, 0.9);
        QVERIFY(moveKeyframe(p, 1, std::nullopt, 70.0, 11));
        QCOMPARE(p.keys.at(1).value.toDouble(), 70.0);
        QVERIFY(!moveKeyframe(p, 1, 0.9, 70.0, 11));
        // The last keyframe can go outside the effect (p.113).
        QVERIFY(moveKeyframe(p, 2, 1.3, std::nullopt, 11));
        QCOMPARE(p.keys.at(2).at, 1.3);
        QVERIFY(!moveKeyframe(p, 5, 0.2, std::nullopt, 11));
    }

    void alignAndSlip() {
        // Keyframes at frames 0, 2, 6 and 10 of an 11-frame effect; the position at frame 4.
        const EffectSpan span(11);
        auto keysAt = [&](QList<double> frames) {
            ParameterKeyframes p;
            for (double f : frames) {
                EffectKeyframe k;
                k.at = span.timeOf(f);
                k.value = f;
                p.keys << k;
            }
            return p;
        };
        auto frames = [&](const ParameterKeyframes& p) {
            QList<double> out;
            for (const EffectKeyframe& k : p.keys) out << std::round(span.frameOf(k.at) * 1000) / 1000;
            return out;
        };
        // Align: the reference keyframe to the position; the others stay (p.114).
        ParameterKeyframes p = keysAt({0, 2, 6, 10});
        QVERIFY(alignKeyframes(p, {1}, 1, span.timeOf(4), span, false));
        QCOMPARE(frames(p), (QList<double>{0, 4, 6, 10}));
        // An intervening keyframe stops it a frame short (p.115).
        p = keysAt({0, 2, 6, 10});
        QVERIFY(alignKeyframes(p, {1}, 1, span.timeOf(8), span, false));
        QCOMPARE(frames(p), (QList<double>{0, 5, 6, 10}));
        // Selected keyframes move together; deselected ones stay (p.115-116).
        p = keysAt({0, 2, 6, 10});
        QVERIFY(alignKeyframes(p, {0, 1}, 1, span.timeOf(3), span, false));
        QCOMPARE(frames(p), (QList<double>{1, 3, 6, 10}));
        // All selected is a slip; Slip moves every keyframe, even out of the effect (p.115-116).
        p = keysAt({0, 2, 6, 10});
        QVERIFY(alignKeyframes(p, {0, 1, 2, 3}, 1, span.timeOf(4), span, false));
        QCOMPARE(frames(p), (QList<double>{2, 4, 8, 12}));
        p = keysAt({0, 2, 6, 10});
        QVERIFY(alignKeyframes(p, {}, 2, span.timeOf(4), span, true));
        QCOMPARE(frames(p), (QList<double>{-2, 0, 4, 8}));
    }

    void redundant() {
        // "If three or more keyframes lie on a straight line segment ... only the first and last
        // keyframes are necessary" (p.106).
        Effect e = withKeys(Interpolation::Linear, {{0.0, 0.0}, {0.25, 25.0}, {0.5, 50.0}, {0.75, 20.0}, {1.0, 20.0}});
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        QCOMPARE(removeRedundantKeyframes(p), 1);
        QCOMPARE(p.keys.size(), 4);
        QCOMPARE(p.keys.at(1).at, 0.5);
        // Shelf: a keyframe holding its predecessor's value.
        p = keyframesOf(withKeys(Interpolation::Shelf, {{0.0, 10.0}, {0.5, 10.0}, {1.0, 30.0}}), kFg, kLevel);
        QCOMPARE(removeRedundantKeyframes(p), 1);
        // Spline: only a flat run (p.107, conservative).
        p = keyframesOf(withKeys(Interpolation::Spline, {{0.0, 0.0}, {0.5, 50.0}, {1.0, 100.0}}), kFg, kLevel);
        QCOMPARE(removeRedundantKeyframes(p), 0);
        p = keyframesOf(withKeys(Interpolation::Spline, {{0.0, 5.0}, {0.5, 5.0}, {1.0, 5.0}}), kFg, kLevel);
        QCOMPARE(removeRedundantKeyframes(p), 1);
        // Colours channel by channel.
        Effect c;
        for (const auto& [t, v] : QList<std::pair<double, QString>>{{0.0, "#000000"}, {0.5, "#808080"}, {1.0, "#ffffff"}}) {
            setKeyframe(c, QStringLiteral("Background Color"), QStringLiteral("Background Color"), t, 11, v);
        }
        p = keyframesOf(c, QStringLiteral("Background Color"), QStringLiteral("Background Color"));
        QCOMPARE(removeRedundantKeyframes(p), 0); // 0x80 is not exactly halfway
    }

    void handles() {
        // Bézier handles (p.122-123): the defaults are the Spline's tangents; set ones bend the curve,
        // and are kept through Fixed (in frames) without moving the curve.
        Effect e = withKeys(Interpolation::Bezier, {{0.0, 0.0}, {0.5, 50.0}, {1.0, 0.0}});
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        const auto [in, out] = effectiveHandles(p, 1, 11);
        QCOMPARE(in.time, -0.5 / 3);
        QCOMPARE(in.value, 0.0); // flat at the top of the arc
        QCOMPARE(out.time, 0.5 / 3);
        const double before = level(e, 0.25, 11);
        setHandles(p, 0, {}, {0.1, 40.0}, 11);
        setKeyframes(e, kFg, kLevel, p);
        const double bent = level(e, 0.25, 11);
        QVERIFY(bent > before);
        setFixed(p, true, 11);
        setKeyframes(e, kFg, kLevel, p);
        QVERIFY(std::abs(level(e, 0.25, 11) - bent) < 1e-6);
        QCOMPARE(effectiveHandles(p, 0, 11).second.time, 0.1);
        // A way in after its keyframe is pulled back to it.
        setHandles(p, 1, {0.2, 5.0}, {0.1, 0.0}, 11);
        QCOMPARE(p.keys.at(1).inTime, 0.0);
    }

    void json() {
        Effect e = withKeys(Interpolation::Bezier, {{0.0, 0.0}, {1.0, 100.0}}, true);
        ParameterKeyframes p = keyframesOf(e, kFg, kLevel);
        p.keys[1].inTime = -0.2;
        p.keys[1].inValue = 5.0;
        p.fixed = true;
        setKeyframes(e, kFg, kLevel, p);
        const Effect back = Effect::fromJson(e.toJson());
        QCOMPARE(keyframesOf(back, kFg, kLevel), p);
    }
};

QTEST_GUILESS_MAIN(TestEffectKeyframes)
#include "tst_effect_keyframes.moc"
