// Colour corrections (ROADMAP 6.10a; FX Guide p.598-605, p.631-683; docs/spec/color-correction.md): the controls,
// their Enable buttons and resets, the luminance ranges, and the HSL, Channels, Levels, Curves and Secondary groups' pictures.
#include "lumenis/media/color_correction.h"
#include "lumenis/model/color_correction.h"
#include "lumenis/media/compositor.h"
#include "lumenis/model/sequence.h"

#include <QJsonArray>
#include <QTest>

#include <cmath>

using namespace lumenis;
using model::ColorCorrection;

namespace {

double luma(const std::array<double, 3>& c) {
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
}

bool near(double a, double b, double tolerance = 0.01) {
    return std::abs(a - b) <= tolerance;
}

bool sameColor(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return near(a[0], b[0], 1e-6) && near(a[1], b[1], 1e-6) && near(a[2], b[2], 1e-6);
}

} // namespace

class TestColorCorrection : public QObject {
    Q_OBJECT

private slots:
    void controls() {
        // Defaults and ranges (p.633-634, p.638).
        QCOMPARE(model::colorControlDefault(QStringLiteral("hsl/controls/master/saturation")), std::optional<double>(100.0));
        QCOMPARE(model::colorControlDefault(QStringLiteral("hsl/controls/master/gamma")), std::optional<double>(1.0));
        QCOMPARE(model::colorControlDefault(QStringLiteral("hsl/controls/master/clipHigh")), std::optional<double>(235.0));
        QCOMPARE(model::colorControlRange(QStringLiteral("hsl/controls/shadows/setup"))->first, -255.0);
        QVERIFY(model::hslControlPaths().contains(QStringLiteral("hsl/hueOffsets/master/amount")));
        // Adjusting a control turns its button and those above it on (p.604); values are kept within the range.
        ColorCorrection c;
        c.setEnabled(QStringLiteral("hsl"), false);
        c.set(QStringLiteral("hsl/controls/master/hue"), 500);
        QCOMPARE(c.number(QStringLiteral("hsl/controls/master/hue")), 180.0);
        QVERIFY(c.enabled(QStringLiteral("hsl")));
        QVERIFY(c.applies(QStringLiteral("hsl/controls/master/hue")));
        QVERIFY(!c.applies(QStringLiteral("hsl/controls/master/saturation"))); // not set
        // A button off stops what is below it; the values stay (p.605).
        c.setEnabled(QStringLiteral("hsl/controls"), false);
        QVERIFY(!c.applies(QStringLiteral("hsl/controls/master/hue")));
        QCOMPARE(c.number(QStringLiteral("hsl/controls/master/hue")), 180.0);
        c.setEnabled(QStringLiteral("hsl/controls"), true);
        // The Hue Offsets' Gain is the Controls' Master Gain (p.603).
        c.set(QStringLiteral("hsl/hueOffsets/gain"), 150);
        QCOMPARE(c.number(QStringLiteral("hsl/controls/master/gain")), 150.0);
        QVERIFY(c.applies(QStringLiteral("hsl/hueOffsets/gain")));
        // Alt+click resets everything below (p.604).
        c.set(QStringLiteral("hsl/hueOffsets/shadows/amount"), 40);
        c.reset(QStringLiteral("hsl/controls"));
        QVERIFY(!c.applies(QStringLiteral("hsl/controls/master/hue")));
        QVERIFY(!c.applies(QStringLiteral("hsl/controls/master/gain")));
        QVERIFY(c.applies(QStringLiteral("hsl/hueOffsets/shadows/amount")));
        QCOMPARE(ColorCorrection::fromJson(c.toJson()), c);
        // On the Color Correction effect.
        model::Effect effect{QStringLiteral("Image"), QStringLiteral("Color Correction"), {}};
        QVERIFY(model::isColorCorrectionEffect(effect));
        model::setColorCorrection(effect, c);
        QCOMPARE(model::colorCorrectionOf(effect), c);
        model::setColorCorrection(effect, ColorCorrection{});
        QVERIFY(!effect.parameters.contains(model::kColorCorrectionCategory));
    }

    void lumaRanges() {
        // "brightness level 175 is defined as 50% highlight, 50% midtone, and 0% shadow. Brightness level 215 is defined
        // as 95% highlight, 5% midtone, and 0% shadow" (p.642).
        const ColorCorrection c;
        auto w = model::lumaRangeWeights(c, 175);
        QVERIFY2(near(w[2], 0.5, 0.03) && near(w[1], 0.5, 0.03) && near(w[0], 0.0, 0.01), qPrintable(QStringLiteral("%1 %2 %3").arg(w[0]).arg(w[1]).arg(w[2])));
        w = model::lumaRangeWeights(c, 215);
        QVERIFY(near(w[2], 0.95, 0.03) && near(w[1], 0.05, 0.03));
        w = model::lumaRangeWeights(c, 40);
        QVERIFY(near(w[0], 0.95, 0.03));
        w = model::lumaRangeWeights(c, 128);
        QVERIFY(w[1] > 0.8);
        QVERIFY(near(w[0] + w[1] + w[2], 1.0, 1e-9));
        // A curve of its own: highlights at 100% everywhere share the darks evenly with the shadows (ours: the curves'
        // values made to add up to one).
        ColorCorrection own;
        own.set(QStringLiteral("hsl/lumaRanges/highlights"), QJsonArray{QJsonArray{0, 100}, QJsonArray{255, 100}});
        QVERIFY(near(model::lumaRangeWeights(own, 10)[2], 0.5, 0.02));
        QVERIFY(model::lumaRangeWeights(own, 128)[2] > 0.5);
    }

    void pictures() {
        const std::array<double, 3> orange{0.9, 0.5, 0.1};
        // No correction: unchanged.
        QCOMPARE(media::colorCorrectedPixel(ColorCorrection{}, orange), orange);
        // Saturation 0: "complete desaturation (monochrome image)" (p.633), the luma kept.
        ColorCorrection grey;
        grey.set(QStringLiteral("hsl/controls/master/saturation"), 0);
        auto out = media::colorCorrectedPixel(grey, orange);
        QVERIFY(near(out[0], out[1]) && near(out[1], out[2]));
        QVERIFY(near(luma(out), luma(orange)));
        // Hue 180 is Invert Chroma (p.634).
        ColorCorrection hue;
        hue.set(QStringLiteral("hsl/controls/master/hue"), 180);
        ColorCorrection invert;
        invert.set(QStringLiteral("hsl/controls/master/invertChroma"), 1);
        const auto a = media::colorCorrectedPixel(hue, orange), b = media::colorCorrectedPixel(invert, orange);
        for (int i = 0; i < 3; ++i) QVERIFY(near(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)]));
        QVERIFY(a[2] > a[0]); // orange turns blue
        // Contrast -100: "all pixels mapped to neutral gray".
        ColorCorrection flat;
        flat.set(QStringLiteral("hsl/controls/master/contrast"), -100);
        out = media::colorCorrectedPixel(flat, orange);
        QVERIFY(near(out[0], out[2]) && near(luma(out), (125.5 - 16) / 219.0, 0.02));
        // Gain 50 halves the luminance above black; Gamma 2 lightens the midtones; Brightness adds.
        const std::array<double, 3> mid{0.5, 0.5, 0.5};
        ColorCorrection gain;
        gain.set(QStringLiteral("hsl/controls/master/gain"), 50);
        QVERIFY(near(luma(media::colorCorrectedPixel(gain, mid)), 0.25));
        ColorCorrection gamma;
        gamma.set(QStringLiteral("hsl/controls/master/gamma"), 2);
        QVERIFY(luma(media::colorCorrectedPixel(gamma, mid)) > 0.6);
        ColorCorrection bright;
        bright.set(QStringLiteral("hsl/controls/master/brightness"), 50);
        QVERIFY(near(luma(media::colorCorrectedPixel(bright, mid)), 0.5 + 50.0 / 219.0));
        // A range's adjustment acts on its own range: Highlights gain leaves a dark pixel alone.
        ColorCorrection highs;
        highs.set(QStringLiteral("hsl/controls/highlights/gain"), 50);
        const std::array<double, 3> dark{0.08, 0.08, 0.08}, light{0.95, 0.95, 0.95};
        QVERIFY(near(luma(media::colorCorrectedPixel(highs, dark)), 0.08, 0.005));
        QVERIFY(luma(media::colorCorrectedPixel(highs, light)) < 0.6);
        // ChromaWheels: the Master wheel towards blue (-120, p.636) adds blue to grey; the Shadows wheel only darks.
        ColorCorrection wheel;
        wheel.set(QStringLiteral("hsl/hueOffsets/master/hue"), -120);
        wheel.set(QStringLiteral("hsl/hueOffsets/master/amount"), 50);
        out = media::colorCorrectedPixel(wheel, mid);
        QVERIFY(out[2] > out[0] + 0.1 && out[2] > out[1] + 0.1);
        ColorCorrection shadows;
        shadows.set(QStringLiteral("hsl/hueOffsets/shadows/hue"), 0);
        shadows.set(QStringLiteral("hsl/hueOffsets/shadows/amount"), 50);
        const auto darkOut = media::colorCorrectedPixel(shadows, dark), lightOut = media::colorCorrectedPixel(shadows, light);
        QVERIFY(darkOut[0] > darkOut[2] + 0.05);  // red into the darks
        QVERIFY(near(lightOut[0], lightOut[2], 0.01));
        // Clip Low: pixels at or under it go to black (p.634).
        ColorCorrection clip;
        clip.set(QStringLiteral("hsl/controls/master/clipLow"), 64);
        QVERIFY(near(luma(media::colorCorrectedPixel(clip, {0.15, 0.15, 0.15})), 0.0));
        QVERIFY(near(luma(media::colorCorrectedPixel(clip, mid)), 0.5));
        // The HSL tab off: nothing.
        grey.setEnabled(QStringLiteral("hsl"), false);
        QCOMPARE(media::colorCorrectedPixel(grey, orange), orange);
        // A picture, alpha kept.
        const media::Picture p = media::Picture::solid(4, 2, QColor(230, 128, 26));
        const media::Picture corrected = media::colorCorrectedPicture(invert, p);
        const QColor px = corrected.toImage().pixelColor(1, 1);
        QVERIFY(px.blue() > px.red());
        QCOMPARE(px.alpha(), 255);
    }

    void channels() {
        // The default row of each channel: itself at 100%, then 0% of itself twice and 0% Offset (p.645).
        ColorCorrection c;
        QCOMPARE(c.number(model::channelComponentPath(QStringLiteral("green"), 1, QStringLiteral("amount"))), 100.0);
        QCOMPARE(c.number(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("amount"))), 0.0);
        QCOMPARE(c.text(model::channelComponentPath(QStringLiteral("green"), 3, QStringLiteral("type"))), QStringLiteral("green"));
        QCOMPARE(c.text(model::channelComponentPath(QStringLiteral("green"), 4, QStringLiteral("type"))), QStringLiteral("offset"));
        QCOMPARE(model::colorControlRange(model::channelComponentPath(QStringLiteral("red"), 2, QStringLiteral("amount")))->first, -200.0);
        // "You might redefine the green channel as 80% green and 20% red" (p.644).
        c.set(model::channelComponentPath(QStringLiteral("green"), 1, QStringLiteral("amount")), 80);
        c.set(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("type")), QStringLiteral("red"));
        c.set(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("amount")), 20);
        QVERIFY(c.adjusted(QStringLiteral("channels/green")) && !c.adjusted(QStringLiteral("channels/red")));
        const std::array<double, 3> px{0.6, 1.0, 0.2};
        auto out = media::colorCorrectedPixel(c, px);
        QVERIFY(near(out[0], 0.6) && near(out[1], 0.8 + 0.12) && near(out[2], 0.2));
        // "20% Green – is the same as –20% Green" (p.647).
        ColorCorrection inverted, negative;
        inverted.set(model::channelComponentPath(QStringLiteral("blue"), 2, QStringLiteral("type")), QStringLiteral("green"));
        inverted.set(model::channelComponentPath(QStringLiteral("blue"), 2, QStringLiteral("amount")), 20);
        inverted.set(model::channelComponentPath(QStringLiteral("blue"), 2, QStringLiteral("invert")), 1);
        negative.set(model::channelComponentPath(QStringLiteral("blue"), 2, QStringLiteral("type")), QStringLiteral("green"));
        negative.set(model::channelComponentPath(QStringLiteral("blue"), 2, QStringLiteral("amount")), -20);
        QCOMPARE(media::colorCorrectedPixel(inverted, px), media::colorCorrectedPixel(negative, px));
        QVERIFY(near(media::colorCorrectedPixel(negative, px)[2], 0.0));
        // "redefine the red channel as 100% Cb" (p.647): neutral grey has no chroma, Cb at mid grey (ours).
        ColorCorrection cb;
        cb.set(model::channelComponentPath(QStringLiteral("red"), 1, QStringLiteral("type")), QStringLiteral("cb"));
        QVERIFY(near(media::colorCorrectedPixel(cb, {0.3, 0.3, 0.3})[0], 0.5));
        // Offset adds a constant.
        ColorCorrection offset;
        offset.set(model::channelComponentPath(QStringLiteral("red"), 4, QStringLiteral("amount")), 10);
        QVERIFY(near(media::colorCorrectedPixel(offset, px)[0], 0.7));
        // The group's button off: nothing.
        c.setEnabled(QStringLiteral("channels"), false);
        QCOMPARE(media::colorCorrectedPixel(c, px), px);
    }

    void levels() {
        const ColorCorrection none;
        QCOMPARE(model::levelsPoints(QStringLiteral("luma")).size(), 4); // black and white points only (p.657)
        // Defaults 16, 128, 235 (p.649): the identity between them.
        QVERIFY(near(model::levelsValue(none, QStringLiteral("master"), 100), 100, 1e-9));
        // Input black point 60: "all values of 60 or less to the output value of 16 ... between 60 and 235 are now
        // mapped to the full output range" (p.649-650).
        ColorCorrection black;
        black.set(QStringLiteral("levels/master/inBlack"), 60);
        QCOMPARE(model::levelsValue(black, QStringLiteral("master"), 40), 16.0);
        QVERIFY(near(model::levelsValue(black, QStringLiteral("master"), 235), 235));
        QVERIFY(model::levelsValue(black, QStringLiteral("master"), 100) < 100); // darker below the gray point, which stays (p.650)
        // Output black point 60: 16 becomes 60, the range shrinks.
        ColorCorrection outBlack;
        outBlack.set(QStringLiteral("levels/master/outBlack"), 60);
        QCOMPARE(model::levelsValue(outBlack, QStringLiteral("master"), 16), 60.0);
        QVERIFY(near(model::levelsValue(outBlack, QStringLiteral("master"), 128), 128, 1e-6)); // the gray points stay
        // Input gray point 100 maps 100 to 128 and brightens; output gray 100 maps 128 to 100 and darkens (p.651-652).
        ColorCorrection grayIn, grayOut;
        grayIn.set(QStringLiteral("levels/master/inGray"), 100);
        grayOut.set(QStringLiteral("levels/master/outGray"), 100);
        QVERIFY(near(model::levelsValue(grayIn, QStringLiteral("master"), 100), 128, 1e-6));
        QVERIFY(model::levelsValue(grayIn, QStringLiteral("master"), 180) > 180);
        QVERIFY(near(model::levelsValue(grayOut, QStringLiteral("master"), 128), 100, 1e-6));
        // "steeper near the black point and somewhat shallower near the white point" (p.656).
        const double low = model::levelsValue(grayIn, QStringLiteral("master"), 30) - model::levelsValue(grayIn, QStringLiteral("master"), 20);
        const double high = model::levelsValue(grayIn, QStringLiteral("master"), 230) - model::levelsValue(grayIn, QStringLiteral("master"), 220);
        QVERIFY(low > high);
        // A flat line at 128 makes "a uniform, mid-gray image" (p.655).
        ColorCorrection flat;
        for (const char* point : {"outBlack", "outGray", "outWhite"}) flat.set(QStringLiteral("levels/master/") + QLatin1String(point), 128);
        auto out = media::colorCorrectedPixel(flat, {0.9, 0.2, 0.4});
        for (double v : out) QVERIFY(near(v, (128.0 - 16) / 219.0));
        // The Red tab acts on red only; Luma on Y' only, keeping the chroma's balance.
        ColorCorrection red;
        red.set(QStringLiteral("levels/red/outWhite"), 180);
        out = media::colorCorrectedPixel(red, {1.0, 1.0, 1.0});
        QVERIFY(near(out[0], (180.0 - 16) / 219.0) && near(out[1], 1.0) && near(out[2], 1.0));
        ColorCorrection lumaTab;
        lumaTab.set(QStringLiteral("levels/luma/outWhite"), 200);
        out = media::colorCorrectedPixel(lumaTab, {1.0, 1.0, 1.0});
        QVERIFY(near(out[0], (200.0 - 16) / 219.0) && near(out[0], out[2]));
        // Composite: a saturated colour's swing brought within the white point keeps its hue (ours).
        ColorCorrection composite;
        composite.set(QStringLiteral("levels/composite/inWhite"), 255);
        composite.set(QStringLiteral("levels/composite/outWhite"), 200);
        const std::array<double, 3> yellow{0.95, 0.95, 0.1};
        out = media::colorCorrectedPixel(composite, yellow);
        QVERIFY(luma(out) < luma(yellow) && out[0] > out[2] + 0.3);
    }

    void curves() {
        ColorCorrection c;
        // The default curve, the 45 degree line from 0 to 255 (p.660).
        QCOMPARE(model::colorCurve(c, QStringLiteral("red")), (QList<QPointF>{QPointF(0, 0), QPointF(255, 255)}));
        QCOMPARE(model::curveValue(model::colorCurve(c, QStringLiteral("red")), 77.0), 77.0);
        // "up to sixteen control points ... at least two" (p.662).
        QList<QPointF> many;
        for (int i = 0; i < 20; ++i) many << QPointF(i * 12, i * 12);
        model::setColorCurve(c, QStringLiteral("blue"), many);
        QCOMPARE(model::colorCurve(c, QStringLiteral("blue")).size(), model::kMaxCurvePoints);
        model::setColorCurve(c, QStringLiteral("blue"), {QPointF(10, 10)});
        QVERIFY(!c.values.contains(QStringLiteral("curves/blue")));
        // A point through which the curve passes: "at 100 on the input (horizontal) axis and at 69 on the output" (p.663);
        // no overshoot beyond the points' outputs.
        model::setColorCurve(c, QStringLiteral("red"), {QPointF(0, 0), QPointF(100, 69), QPointF(255, 255)});
        const QList<QPointF> red = model::colorCurve(c, QStringLiteral("red"));
        QVERIFY(near(model::curveValue(red, 100), 69, 1e-9));
        for (int x = 0; x <= 100; x += 5) QVERIFY(model::curveValue(red, x) <= 69.0 + 1e-9);
        auto out = media::colorCorrectedPixel(c, {100 / 255.0, 100 / 255.0, 100 / 255.0});
        QVERIFY(near(out[0], 69 / 255.0) && near(out[1], 100 / 255.0));
        // Anchoring: close points keep the part of the curve between the first two still (p.663).
        const QList<QPointF> anchored{QPointF(0, 0), QPointF(100, 100), QPointF(105, 105), QPointF(200, 240), QPointF(255, 255)};
        for (int x = 0; x <= 100; x += 10) QVERIFY(near(model::curveValue(anchored, x), x, 1e-9));
        // A flat line at 128 on every graph: "a uniform, midgray image" (p.660), through Master.
        ColorCorrection flat;
        model::setColorCurve(flat, QStringLiteral("master"), {QPointF(0, 128), QPointF(255, 128)});
        for (double v : media::colorCorrectedPixel(flat, {0.1, 0.5, 0.9})) QVERIFY(near(v, 128 / 255.0));
        // The Master sliders are the HSL group's (p.659): adjusting one turns on the HSL hierarchy and the Curves tab.
        ColorCorrection sliders;
        sliders.setEnabled(QStringLiteral("hsl"), false);
        sliders.setEnabled(QStringLiteral("curves"), false);
        sliders.set(QStringLiteral("curves/masterSaturation"), 0);
        QCOMPARE(sliders.number(QStringLiteral("hsl/controls/master/saturation")), 0.0);
        QVERIFY(sliders.enabled(QStringLiteral("hsl")) && sliders.enabled(QStringLiteral("curves")));
        out = media::colorCorrectedPixel(sliders, {0.9, 0.5, 0.1});
        QVERIFY(near(out[0], out[2]));
        // Groups add up in order: a Channels change is seen by Curves.
        ColorCorrection both;
        both.set(model::channelComponentPath(QStringLiteral("red"), 1, QStringLiteral("amount")), 50);
        model::setColorCurve(both, QStringLiteral("red"), {QPointF(0, 0), QPointF(127.5, 255), QPointF(255, 255)});
        QVERIFY(near(media::colorCorrectedPixel(both, {1.0, 0.0, 0.0})[0], 1.0));
        QCOMPARE(ColorCorrection::fromJson(both.toJson()), both);
    }

    void secondaryVectors() {
        // Twelve vectors, six standard then six custom (p.670); standard Red 30 degrees wide, custom 3 saturation 35 and
        // width 40 (p.671), Softness 30 (p.676).
        const QStringList vectors = model::secondaryVectors();
        QCOMPARE(vectors.size(), 12);
        QCOMPARE(vectors.mid(0, 6), (QStringList{QStringLiteral("green"), QStringLiteral("yellow"), QStringLiteral("red"),
                                                 QStringLiteral("magenta"), QStringLiteral("blue"), QStringLiteral("cyan")}));
        ColorCorrection c;
        const model::SecondaryVector red = model::secondaryVector(c, QStringLiteral("red"));
        QVERIFY(red.standard);
        QCOMPARE(red.hue, 0.0);
        QCOMPARE(red.hueWidth, 30.0);
        QCOMPARE(red.softness, 30.0);
        const model::SecondaryVector three = model::secondaryVector(c, QStringLiteral("3"));
        QVERIFY(!three.standard);
        QCOMPARE(three.saturation, 35.0);
        QCOMPARE(three.satWidth, 40.0);
        QCOMPARE(model::colorControlRange(model::vectorPath(QStringLiteral("3"), QStringLiteral("input"), QStringLiteral("hueWidth")))->second, 100.0);
        QCOMPARE(model::colorControlRange(model::vectorPath(QStringLiteral("red"), QStringLiteral("input"), QStringLiteral("hueWidth")))->second, 180.0);
        // The output follows the input until changed.
        QCOMPARE(red.outHue, red.hue);
        // Membership: a wedge for standard vectors, "15 degrees either side of pure red" (p.671), with no upper
        // saturation limit (p.670).
        model::SecondaryVector hard = red;
        hard.softness = 0;
        QCOMPARE(model::vectorMembership(hard, 14, 90), 1.0);
        QCOMPARE(model::vectorMembership(hard, 16, 90), 0.0);
        QCOMPARE(model::vectorMembership(hard, 0, 5), 0.0); // under the saturation minimum
        // Softness: positive blends beyond the edge, negative within it (p.676).
        QVERIFY(model::vectorMembership(red, 20, 90) > 0.0 && model::vectorMembership(red, 20, 90) < 1.0);
        model::SecondaryVector inner = red;
        inner.softness = -128;
        QVERIFY(model::vectorMembership(inner, 12, 90) < 1.0);
        QCOMPARE(model::vectorMembership(inner, 20, 90), 0.0);
        // A custom vector is an ellipse: inside at its centre, outside at a saturation beyond its width.
        model::SecondaryVector ellipse = three;
        ellipse.softness = 0;
        QCOMPARE(model::vectorMembership(ellipse, three.hue, 35), 1.0);
        QCOMPARE(model::vectorMembership(ellipse, three.hue, 60), 0.0);
        QCOMPARE(model::vectorMembership(ellipse, three.hue + 60, 35), 0.0);
        // Lock Vectors: input hue changes carry the output with them (p.676).
        model::setVectorInput(c, QStringLiteral("red"), QStringLiteral("hue"), 10, false);
        c.set(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("hue")), 40);
        model::setVectorInput(c, QStringLiteral("red"), QStringLiteral("hue"), 20, true);
        QCOMPARE(c.number(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("hue"))), 50.0);
        QCOMPARE(model::enabledVectors(c), QStringList{QStringLiteral("red")});
        // Alt+click resets the vector (p.675).
        c.reset(QStringLiteral("secondary/vectors/red"));
        QVERIFY(model::enabledVectors(c).isEmpty());
    }

    void secondaryPictures() {
        const std::array<double, 3> green{0.1, 0.8, 0.1}, red{0.85, 0.1, 0.1}, grey{0.5, 0.5, 0.5};
        // Wheel places: red at 0, green near +120 (p.636).
        QVERIFY(near(media::wheelHueSaturation(red).x(), 0.0, 1.0));
        QVERIFY(near(media::wheelHueSaturation(green).x(), 120.0, 12.0));
        QVERIFY(near(media::wheelHueSaturation(grey).y(), 0.0));
        // The Green vector's hue shifted by 180 turns green magenta and leaves red alone (p.677).
        ColorCorrection c;
        const QString hue = model::vectorPath(QStringLiteral("green"), QStringLiteral("output"), QStringLiteral("hue"));
        c.set(hue, -60);
        auto out = media::colorCorrectedPixel(c, green);
        QVERIFY(out[0] > out[1] && out[2] > out[1]);
        const std::array<double, 3> mild{0.35, 0.55, 0.35};
        QVERIFY(near(luma(media::colorCorrectedPixel(c, mild)), luma(mild), 0.01)); // the luminance kept within the gamut
        QVERIFY(sameColor(media::colorCorrectedPixel(c, red), red));
        // Output Luminance +10 adds 10% of the range.
        ColorCorrection lum;
        lum.set(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("luminance")), 10);
        QVERIFY(near(luma(media::colorCorrectedPixel(lum, red)), luma(red) + 0.1, 0.01));
        // Vectors apply in order: green made yellow, then yellow made red (p.683).
        ColorCorrection chain;
        chain.set(model::vectorPath(QStringLiteral("green"), QStringLiteral("output"), QStringLiteral("hue")), 60);
        chain.set(model::vectorPath(QStringLiteral("yellow"), QStringLiteral("output"), QStringLiteral("hue")), 0);
        out = media::colorCorrectedPixel(chain, green);
        QVERIFY2(near(media::wheelHueSaturation(out).x(), 0.0, 15.0), qPrintable(QString::number(media::wheelHueSaturation(out).x())));
        // Invert Last Vector: everything but the vector is corrected (p.682); desaturating the rest leaves red red.
        ColorCorrection invert;
        invert.set(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("saturation")), 0);
        invert.set(QStringLiteral("secondary/invertLast"), 1);
        QVERIFY(sameColor(media::colorCorrectedPixel(invert, red), red));
        out = media::colorCorrectedPixel(invert, green);
        QVERIFY(media::wheelHueSaturation(out).y() < media::wheelHueSaturation(green).y() - 40.0);
        // Isolate: outside the selected vector in grey (p.679).
        ColorCorrection isolate;
        isolate.set(QStringLiteral("secondary/isolate"), 1);
        isolate.set(QStringLiteral("secondary/selected"), QStringLiteral("red"));
        QVERIFY(sameColor(media::colorCorrectedPixel(isolate, red), red));
        out = media::colorCorrectedPixel(isolate, green);
        QVERIFY(near(out[0], out[1]) && near(out[1], out[2]));
        // The syringe: samples of a green pick the Green vector, set its output to its input and select Isolate (p.678).
        ColorCorrection syringe;
        const QList<QPointF> samples{media::wheelHueSaturation({0.15, 0.7, 0.15}), media::wheelHueSaturation({0.2, 0.75, 0.1}),
                                     media::wheelHueSaturation({0.1, 0.65, 0.2})};
        QCOMPARE(model::syringeVector(syringe, samples), QStringLiteral("green"));
        QCOMPARE(syringe.text(QStringLiteral("secondary/selected")), QStringLiteral("green"));
        QCOMPARE(syringe.number(QStringLiteral("secondary/isolate")), 1.0);
        const model::SecondaryVector v = model::secondaryVector(syringe, QStringLiteral("green"));
        QCOMPARE(v.outHue, v.hue);
        for (const QPointF& p : samples) QCOMPARE(model::vectorMembership(v, p.x(), p.y()), 1.0);
        // Add to and Subtract from Selection (p.679).
        const QPointF yellowish = media::wheelHueSaturation({0.6, 0.75, 0.1});
        QVERIFY(model::vectorMembership(v, yellowish.x(), yellowish.y()) < 1.0);
        model::addToVector(syringe, QStringLiteral("green"), yellowish);
        QCOMPARE(model::vectorMembership(model::secondaryVector(syringe, QStringLiteral("green")), yellowish.x(), yellowish.y()), 1.0);
        model::subtractFromVector(syringe, QStringLiteral("green"), yellowish);
        model::SecondaryVector after = model::secondaryVector(syringe, QStringLiteral("green"));
        after.softness = 0;
        QCOMPARE(model::vectorMembership(after, yellowish.x(), yellowish.y()), 0.0);
        // A custom vector grows and shrinks too.
        ColorCorrection custom;
        model::addToVector(custom, QStringLiteral("3"), QPointF(-150, 80));
        QCOMPARE(model::vectorMembership(model::secondaryVector(custom, QStringLiteral("3")), -150, 80), 1.0);
        model::subtractFromVector(custom, QStringLiteral("3"), QPointF(-150, 70));
        after = model::secondaryVector(custom, QStringLiteral("3"));
        after.softness = 0;
        QCOMPARE(model::vectorMembership(after, -150, 70), 0.0);
        QCOMPARE(ColorCorrection::fromJson(syringe.toJson()), syringe);
    }

    void segmentCorrections() {
        // A segment's Source and Program sides (p.596-597), kept with the sequence.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.binId = QStringLiteral("bin");
        a.name = QStringLiteral("a");
        a.sourceTrack = QStringLiteral("V1");
        a.length = 10;
        model::Segment b = a;
        b.clipId = QStringLiteral("b");
        model::Segment c = a;
        c.clipId = QStringLiteral("c");
        seq.splice(QStringLiteral("V1"), 0, {a, model::Segment::filler(5), b, c});
        model::Track* v1 = seq.track(QStringLiteral("V1"));
        model::correctionOf(v1->segments[0], model::CorrectionSide::Source).set(QStringLiteral("hsl/controls/master/saturation"), 0);
        model::correctionOf(v1->segments[3], model::CorrectionSide::Program).set(QStringLiteral("hsl/controls/master/gain"), 50);
        QVERIFY(v1->segments.at(0).hasCorrection());
        const model::Sequence back = model::Sequence::fromJson(seq.toJson());
        QCOMPARE(back.track(QStringLiteral("V1"))->segments, seq.track(QStringLiteral("V1"))->segments);
        // "a single adjustment using the merged values from the Source and Program sides" (p.597; ours: the Program
        // side's set controls in place of the Source side's).
        ColorCorrection source, program;
        source.set(QStringLiteral("hsl/controls/master/gain"), 150);
        source.set(QStringLiteral("hsl/controls/master/hue"), 20);
        source.setEnabled(QStringLiteral("hsl/controls"), false);
        program.set(QStringLiteral("hsl/controls/master/gain"), 80);
        const ColorCorrection merged = model::mergedCorrection(source, program);
        QCOMPARE(merged.number(QStringLiteral("hsl/controls/master/gain")), 80.0);
        QCOMPARE(merged.number(QStringLiteral("hsl/controls/master/hue")), 20.0);
        QVERIFY(merged.applies(QStringLiteral("hsl/controls/master/gain")));
        QCOMPARE(model::mergedCorrection(source, ColorCorrection{}), source);
        // The monitors' shots (p.590-591): filler is not a shot. a 0-10 | filler 10-15 | b 15-25 | c 25-35.
        QCOMPARE(model::correctionShotStart(*v1, 17, 0), std::optional<std::int64_t>(15));
        QCOMPARE(model::correctionShotStart(*v1, 17, -1), std::optional<std::int64_t>(0));
        QCOMPARE(model::correctionShotStart(*v1, 17, 1), std::optional<std::int64_t>(25));
        QCOMPARE(model::correctionShotStart(*v1, 3, 2), std::optional<std::int64_t>(25));
        QCOMPARE(model::correctionShotStart(*v1, 3, -1), std::nullopt);
        QCOMPARE(model::correctionShotStart(*v1, 12, 0), std::nullopt);
        QCOMPARE(model::correctionShotStart(*v1, 12, -1), std::optional<std::int64_t>(0));
        QCOMPARE(model::correctionShotStart(*v1, 12, 1), std::optional<std::int64_t>(15));
        // Go to Previous/Next Uncorrected Shot (p.592): a and c are corrected.
        QCOMPARE(model::uncorrectedShotStart(*v1, 2, 1), std::optional<std::int64_t>(15));
        QCOMPARE(model::uncorrectedShotStart(*v1, 17, 1), std::nullopt);
        QCOMPARE(model::uncorrectedShotStart(*v1, 30, -1), std::optional<std::int64_t>(15));
        QCOMPARE(model::uncorrectedShotStart(*v1, 17, -1), std::nullopt);
        // Pictures: the correction applies before effects, and not when corrections are left out (Dual Split).
        media::Composition comp;
        comp.sequence = &seq;
        comp.tracks = {QStringLiteral("V1")};
        comp.size = QSize(8, 6);
        comp.precision = media::EffectPrecision::Bits32;
        comp.source = [](const model::Segment&, std::int64_t, QSize size) -> std::optional<QImage> {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(230, 128, 26));
            return image;
        };
        QColor px = media::composeImage(comp, 3).pixelColor(4, 3);
        QVERIFY(std::abs(px.red() - px.blue()) <= 2); // desaturated
        px = media::composeImage(comp, 17).pixelColor(4, 3);
        QVERIFY(px.red() > px.blue() + 100);          // b: no correction
        comp.corrections = false;
        px = media::composeImage(comp, 3).pixelColor(4, 3);
        QVERIFY(px.red() > px.blue() + 100);
    }
};

QTEST_MAIN(TestColorCorrection)
#include "tst_color_correction.moc"
