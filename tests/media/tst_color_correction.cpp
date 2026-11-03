// Colour corrections (ROADMAP 6.10a1; FX Guide p.598-605, p.631-644; docs/spec/color-correction.md): the controls,
// their Enable buttons and resets, the luminance ranges, and the HSL group's pictures.
#include "lumenis/media/color_correction.h"
#include "lumenis/model/color_correction.h"
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
};

QTEST_MAIN(TestColorCorrection)
#include "tst_color_correction.moc"
