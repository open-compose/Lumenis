// Keyframing colour corrections (FX Guide p.683-686; lumenis/model/correction_keyframes.h): which controls take
// keyframes, values between keyframes, held curves, adjustments that add keyframes, merged sides, JSON.
#include "lumenis/model/correction_keyframes.h"

#include <QJsonArray>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

class TestCorrectionKeyframes : public QObject {
    Q_OBJECT

private slots:
    // p.685's table.
    void keyframing() {
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/controls/master/hue")), CorrectionKeyframing::Graph);
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/controls/shadows/gain")), CorrectionKeyframing::Graph);
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/hueOffsets/midtones/hue")), CorrectionKeyframing::Graph);
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/hueOffsets/highlights/amount")), CorrectionKeyframing::Graph);
        // The Hue Offsets tab's Gain is the Controls tab's Master Gain.
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/hueOffsets/gain")), CorrectionKeyframing::Graph);
        QCOMPARE(correctionKeyframing(QStringLiteral("secondary/vectors/red/output/hue")), CorrectionKeyframing::Graph);
        QCOMPARE(correctionKeyframing(QStringLiteral("curves/red")), CorrectionKeyframing::Track);
        QCOMPARE(correctionKeyframing(QStringLiteral("levels/master/inGray")), CorrectionKeyframing::Track);
        QCOMPARE(correctionKeyframing(QStringLiteral("secondary/isolate")), CorrectionKeyframing::None);
        QCOMPARE(correctionKeyframing(QStringLiteral("secondary/invertLast")), CorrectionKeyframing::None);
        QCOMPARE(correctionKeyframing(QStringLiteral("hsl/lumaRanges/shadows")), CorrectionKeyframing::None);
        QCOMPARE(correctionKeyframing(QStringLiteral("channels/red/2/amount")), CorrectionKeyframing::None);
    }

    void valuesAtTimes() {
        const EffectSpan span(11); // frames 0-10, time 0 to 1
        ColorCorrection c;
        setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/gain"), 0.0, span, 100);
        setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/gain"), 1.0, span, 140);
        QCOMPARE(keyframedControls(c), QStringList{QStringLiteral("hsl/controls/master/gain")});
        QCOMPARE(correctionKeyframesOf(c, QStringLiteral("hsl/controls/master/gain")).keys.size(), 2);
        QCOMPARE(correctionAt(c, 0.0, span).number(QStringLiteral("hsl/controls/master/gain")), 100.0);
        QCOMPARE(correctionAt(c, 0.5, span).number(QStringLiteral("hsl/controls/master/gain")), 120.0);
        QCOMPARE(correctionAt(c, 1.0, span).number(QStringLiteral("hsl/controls/master/gain")), 140.0);
        QVERIFY(correctionAt(c, 0.5, span).keyframes.isEmpty());
        // Clamped to the control's range, as a value is.
        setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/gain"), 1.0, span, 900);
        QCOMPARE(correctionAt(c, 1.0, span).number(QStringLiteral("hsl/controls/master/gain")), 200.0);
        // A keyframe through the Hue Offsets tab's Gain is the Master Gain's.
        setCorrectionKeyframe(c, QStringLiteral("hsl/hueOffsets/gain"), 1.0, span, 150);
        QCOMPARE(correctionAt(c, 1.0, span).number(QStringLiteral("hsl/controls/master/gain")), 150.0);
        // Curves points hold between keyframes.
        const QJsonArray straight{QJsonArray{0, 0}, QJsonArray{255, 255}};
        const QJsonArray bent{QJsonArray{0, 0}, QJsonArray{128, 160}, QJsonArray{255, 255}};
        setCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.0, span, straight);
        setCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.6, span, bent);
        QCOMPARE(correctionAt(c, 0.5, span).values.value(QStringLiteral("curves/red")).toArray(), straight);
        QCOMPARE(correctionAt(c, 0.6, span).values.value(QStringLiteral("curves/red")).toArray(), bent);
        QCOMPARE(correctionAt(c, 1.0, span).values.value(QStringLiteral("curves/red")).toArray(), bent);
        QCOMPARE(correctionKeyframeFrames(c, span), (QList<double>{0.0, 6.0, 10.0}));
        // A control without keyframes gets a value.
        setCorrectionKeyframe(c, QStringLiteral("secondary/isolate"), 0.5, span, 1);
        QVERIFY(!c.keyframes.contains(QStringLiteral("secondary/isolate")));
        QCOMPARE(c.number(QStringLiteral("secondary/isolate")), 1.0);
        // Kept through JSON.
        QCOMPARE(ColorCorrection::fromJson(c.toJson()), c);
        // Removing keyframes; the last leaves its value.
        QVERIFY(removeCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.0, span));
        QVERIFY(!removeCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.3, span));
        QVERIFY(removeCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.6, span));
        QVERIFY(!c.keyframes.contains(QStringLiteral("curves/red")));
        QCOMPARE(c.values.value(QStringLiteral("curves/red")).toArray(), bent);
        // Keyframes alone make a correction.
        ColorCorrection only;
        setCorrectionKeyframe(only, QStringLiteral("hsl/controls/master/hue"), 0.0, span, 10);
        only.values = {};
        QVERIFY(!only.isEmpty());
    }

    // "Make an adjustment ... A new keyframe appears ... at the location of the position indicator" (p.686).
    void adjusting() {
        const EffectSpan span(11);
        ColorCorrection stored;
        setCorrectionKeyframe(stored, QStringLiteral("hsl/controls/master/gain"), 0.0, span, 100);
        stored.set(QStringLiteral("hsl/controls/master/saturation"), 90);
        // At frame 5: Gain changed (keyframed: a keyframe there), Saturation changed (a plain value).
        ColorCorrection edited = correctionAt(stored, 0.5, span);
        edited.set(QStringLiteral("hsl/controls/master/gain"), 130);
        edited.set(QStringLiteral("hsl/controls/master/saturation"), 70);
        const ColorCorrection out = adjustedAt(stored, edited, 0.5, span);
        QCOMPARE(correctionKeyframesOf(out, QStringLiteral("hsl/controls/master/gain")).keys.size(), 2);
        QCOMPARE(correctionAt(out, 0.0, span).number(QStringLiteral("hsl/controls/master/gain")), 100.0);
        QCOMPARE(correctionAt(out, 0.5, span).number(QStringLiteral("hsl/controls/master/gain")), 130.0);
        QVERIFY(!out.keyframes.contains(QStringLiteral("hsl/controls/master/saturation")));
        QCOMPARE(out.number(QStringLiteral("hsl/controls/master/saturation")), 70.0);
        // Nothing changed: nothing changes.
        QCOMPARE(adjustedAt(out, correctionAt(out, 0.3, span), 0.3, span), out);
        // A reset control loses its keyframes; Enable buttons are the tool's.
        ColorCorrection reset = correctionAt(out, 0.5, span);
        reset.values.remove(QStringLiteral("hsl/controls/master/gain"));
        reset.disabled << QStringLiteral("hsl/controls");
        const ColorCorrection gone = adjustedAt(out, reset, 0.5, span);
        QVERIFY(gone.keyframes.isEmpty());
        QVERIFY(!gone.values.contains(QStringLiteral("hsl/controls/master/gain")));
        QCOMPARE(gone.disabled, QStringList{QStringLiteral("hsl/controls")});
    }

    // The two sides merged (p.597): the Program side's keyframed controls are its own; a control it sets plainly stops
    // following the Source side's keyframes.
    void merged() {
        const EffectSpan span(11);
        ColorCorrection source;
        setCorrectionKeyframe(source, QStringLiteral("hsl/controls/master/gain"), 0.0, span, 100);
        setCorrectionKeyframe(source, QStringLiteral("hsl/controls/master/gain"), 1.0, span, 140);
        ColorCorrection program;
        program.set(QStringLiteral("hsl/controls/master/hue"), 20);
        ColorCorrection m = mergedCorrection(source, program);
        QCOMPARE(correctionAt(m, 0.5, span).number(QStringLiteral("hsl/controls/master/gain")), 120.0);
        program.set(QStringLiteral("hsl/controls/master/gain"), 110);
        m = mergedCorrection(source, program);
        QVERIFY(!m.keyframes.contains(QStringLiteral("hsl/controls/master/gain")));
        QCOMPARE(correctionAt(m, 0.5, span).number(QStringLiteral("hsl/controls/master/gain")), 110.0);
    }
};

QTEST_GUILESS_MAIN(TestCorrectionKeyframes)
#include "tst_correction_keyframes.moc"
