// Keyframes on effect parameters (FX Guide ch.4, p.94-134; docs/spec/effect-keyframes.md): the four
// interpolations, Hold and Extrapolate, Elastic and Fixed, Reverse Keyframes, colours, and the Fade
// Effect button's keyframes.
#include "lumenis/model/effect_keyframes.h"

#include <QTest>

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
        reverseKeyframes(e, 101, {}, true);
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
