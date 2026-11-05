// Keyframing colour corrections (FX Guide p.683-686; lumenis/model/correction_keyframes.h): which controls take
// keyframes, values between keyframes, held curves, adjustments that add keyframes, merged sides, JSON.
#include "lumenis/model/correction_effect.h"
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

    // The correction as the Effect Editor's effect and back (p.685, 6.10d3b).
    void asEffect() {
        QCOMPARE(correctionParameterCategories().mid(0, 5),
                 (QStringList{QStringLiteral("HSL Master"), QStringLiteral("HSL Highlights"), QStringLiteral("HSL Midtones"), QStringLiteral("HSL Shadows"),
                              QStringLiteral("HSL Hue Offsets")}));
        // The Levels tabs and Curves as keyframe tracks (p.685, 6.10d3c), before the Secondary vectors.
        QCOMPARE(correctionParameterCategories().size(), 5 + 6 + 1 + 12);
        QCOMPARE(correctionParameterCategories().at(5), QStringLiteral("Levels Composite"));
        QCOMPARE(correctionParameterCategories().at(11), QStringLiteral("Curves"));
        QCOMPARE(correctionParameterOf(QStringLiteral("HSL Master"), QStringLiteral("Gain"))->path, QStringLiteral("hsl/controls/master/gain"));
        QCOMPARE(correctionParameterAtPath(QStringLiteral("hsl/hueOffsets/gain"))->category, QStringLiteral("HSL Master")); // the same control
        QCOMPARE(correctionParameterOf(QStringLiteral("HSL Hue Offsets"), QStringLiteral("Shadows Hue"))->min, -360.0);
        QCOMPARE(correctionParameterOf(QStringLiteral("Secondary Yellow"), QStringLiteral("Output Luminance"))->path,
                 QStringLiteral("secondary/vectors/yellow/output/luminance"));
        QVERIFY(correctionParameterAtPath(QStringLiteral("curves/red"))->trackOnly);
        QCOMPARE(correctionParameterAtPath(QStringLiteral("curves/red"))->enablePath, QStringLiteral("curves/red"));
        QCOMPARE(correctionParameterOf(QStringLiteral("Levels Luma"), QStringLiteral("Input White"))->enablePath, QStringLiteral("levels/luma"));
        QVERIFY(!correctionParameterOf(QStringLiteral("Levels Luma"), QStringLiteral("Input Gray"))); // two-point tabs (p.657)
        QVERIFY(!correctionParameterAtPath(QStringLiteral("hsl/controls/master/gain"))->trackOnly);
        QVERIFY(!correctionParameterAtPath(QStringLiteral("channels/red/red")));
        QCOMPARE(toolHue(340), -20.0);
        QCOMPARE(toolHue(-190), 170.0);
        QCOMPARE(toolHue(90), 90.0);

        const EffectSpan span(11);
        ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/shadows/gain"), 120);
        c.set(QStringLiteral("curves/red"), QJsonArray{QJsonArray{0, 0}, QJsonArray{255, 200}});
        c.disabled << QStringLiteral("channels");
        setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/hue"), 0.0, span, 10);
        setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/hue"), 1.0, span, 40);
        const Effect e = correctionEffect(c);
        QVERIFY(isCorrectionModeEffect(e));
        QCOMPARE(e.parameters.value(QStringLiteral("HSL Shadows")).toObject().value(QStringLiteral("Gain")).toDouble(), 120.0);
        QCOMPARE(keyframesOf(e, QStringLiteral("HSL Master"), QStringLiteral("Hue")).keys.size(), 2);
        // Back unchanged; what the editor does not show stays.
        QCOMPARE(correctionFromEffect(e, c), c);
        // An edited value and a new keyframe go into the correction.
        Effect edited = e;
        QJsonObject shadows = edited.parameters.value(QStringLiteral("HSL Shadows")).toObject();
        shadows.insert(QStringLiteral("Gain"), 90);
        edited.parameters.insert(QStringLiteral("HSL Shadows"), shadows);
        setKeyframe(edited, QStringLiteral("HSL Master"), QStringLiteral("Saturation"), 0.5, span, 50);
        const ColorCorrection back = correctionFromEffect(edited, c);
        QCOMPARE(back.number(QStringLiteral("hsl/controls/shadows/gain")), 90.0);
        QCOMPARE(correctionKeyframesOf(back, QStringLiteral("hsl/controls/master/saturation")).keys.size(), 1);
        QCOMPARE(back.values.value(QStringLiteral("curves/red")), c.values.value(QStringLiteral("curves/red")));
        QCOMPARE(back.disabled, c.disabled);
        // A value back at its default is left out.
        shadows.insert(QStringLiteral("Gain"), 100);
        edited.parameters.insert(QStringLiteral("HSL Shadows"), shadows);
        QVERIFY(!correctionFromEffect(edited, c).values.contains(QStringLiteral("hsl/controls/shadows/gain")));
    }

    // Curves and Levels in the Effect Editor: their keyframe tracks change, their values stay the tool's (p.685).
    void trackOnlyAsEffect() {
        const EffectSpan span(11);
        ColorCorrection c;
        const QJsonArray bend{QJsonArray{0, 0}, QJsonArray{128, 100}, QJsonArray{255, 255}};
        setCorrectionKeyframe(c, QStringLiteral("curves/red"), 0.0, span, bend);
        setCorrectionKeyframe(c, QStringLiteral("levels/composite/inWhite"), 0.0, span, 200);
        setCorrectionKeyframe(c, QStringLiteral("levels/composite/inWhite"), 1.0, span, 240);
        Effect e = correctionEffect(c);
        QCOMPARE(keyframesOf(e, QStringLiteral("Curves"), QStringLiteral("Red")).keys.size(), 1);
        QCOMPARE(correctionFromEffect(e, c), c);
        // A value the editor would set is not taken.
        QJsonObject levels = e.parameters.value(QStringLiteral("Levels Composite")).toObject();
        levels.insert(QStringLiteral("Input White"), 10);
        e.parameters.insert(QStringLiteral("Levels Composite"), levels);
        QCOMPARE(correctionFromEffect(e, c).values.value(QStringLiteral("levels/composite/inWhite")), c.values.value(QStringLiteral("levels/composite/inWhite")));
        // A keyframe the track adds (with the held value) goes in, and they keep holding.
        setKeyframe(e, QStringLiteral("Curves"), QStringLiteral("Red"), 0.5, span, bend);
        ColorCorrection back = correctionFromEffect(e, c);
        QCOMPARE(correctionKeyframesOf(back, QStringLiteral("curves/red")).keys.size(), 2);
        QCOMPARE(correctionKeyframesOf(back, QStringLiteral("curves/red")).interpolation, Interpolation::Shelf);
        // Removing the last keyframe leaves the curve.
        setKeyframes(e, QStringLiteral("Curves"), QStringLiteral("Red"), {});
        back = correctionFromEffect(e, c);
        QVERIFY(!back.keyframes.contains(QStringLiteral("curves/red")));
        QCOMPARE(back.values.value(QStringLiteral("curves/red")).toArray(), bend);
    }

    // The keyframe markers below the Current monitor (p.686, 6.10d3c): adjustments go to the selected keyframes, and
    // the selected ones are deleted and moved.
    void markers() {
        const EffectSpan span(21);
        const QString gain = QStringLiteral("hsl/controls/master/gain");
        const QString hue = QStringLiteral("hsl/controls/master/hue");
        ColorCorrection c;
        setCorrectionKeyframe(c, gain, span.timeOf(0), span, 100);
        setCorrectionKeyframe(c, gain, span.timeOf(10), span, 120);
        setCorrectionKeyframe(c, hue, span.timeOf(10), span, 30);
        QCOMPARE(correctionKeyframeFrames(c, span), (QList<double>{0, 10}));
        // With the keyframe at frame 0 selected, an adjustment at frame 15 changes it: no new keyframe.
        ColorCorrection edited = correctionAt(c, span.timeOf(15), span);
        edited.set(gain, 150);
        ColorCorrection out = adjustedAt(c, edited, span.timeOf(15), span, {0.0});
        QCOMPARE(correctionKeyframesOf(out, gain).keys.size(), 2);
        QCOMPARE(correctionAt(out, span.timeOf(0), span).number(gain), 150.0);
        // Deselected (step 3), the same adjustment adds a keyframe at the position (step 5).
        out = adjustedAt(c, edited, span.timeOf(15), span, {});
        QCOMPARE(correctionKeyframeFrames(out, span), (QList<double>{0, 10, 15}));
        // Delete: every control's keyframes at the selected frames.
        ColorCorrection d = c;
        QVERIFY(removeCorrectionKeyframesAt(d, {10.0}, span));
        QCOMPARE(correctionKeyframeFrames(d, span), (QList<double>{0}));
        QVERIFY(!d.keyframes.contains(hue));
        QCOMPARE(d.number(hue), 30.0); // the last one's value stays
        QVERIFY(!removeCorrectionKeyframesAt(d, {5.0}, span));
        // Alt-drag: both controls' keyframes at frame 10 move to 14, the one at 0 stays.
        ColorCorrection m = c;
        QVERIFY(moveCorrectionKeyframes(m, {10.0}, 4.0, span));
        QCOMPARE(correctionKeyframeFrames(m, span), (QList<double>{0, 14}));
        QCOMPARE(correctionAt(m, span.timeOf(14), span).number(hue), 30.0);
        // Not past the segment's end, and not onto a keyframe that stays (a frame short of it).
        QVERIFY(moveCorrectionKeyframes(m, {14.0}, 50.0, span));
        QCOMPARE(correctionKeyframeFrames(m, span), (QList<double>{0, 20}));
        QVERIFY(moveCorrectionKeyframes(m, {20.0}, -30.0, span));
        QCOMPARE(correctionKeyframeFrames(m, span), (QList<double>{0, 1}));
        QVERIFY(!moveCorrectionKeyframes(m, {}, 3.0, span));
    }
};

QTEST_GUILESS_MAIN(TestCorrectionKeyframes)
#include "tst_correction_keyframes.moc"
