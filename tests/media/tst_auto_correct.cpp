// The automatic corrections (lumenis/media/auto_correct.h; FX Guide p.618-622): what each one achieves on a frame
// through the correction it sets.
#include "lumenis/media/auto_correct.h"
#include "lumenis/media/color_correction.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace lumenis;
using media::AutoCorrection;
using model::ColorCorrection;

namespace {

// A ramp of greys from `lo` to `hi` (full range 0-1), tinted by adding `tint` to each channel.
QImage ramp(double lo, double hi, std::array<double, 3> tint = {0, 0, 0}) {
    QImage image(64, 16, QImage::Format_RGB32);
    for (int x = 0; x < image.width(); ++x) {
        const double v = lo + (hi - lo) * x / (image.width() - 1);
        QColor c;
        c.setRgbF(static_cast<float>(std::clamp(v + tint[0], 0.0, 1.0)), static_cast<float>(std::clamp(v + tint[1], 0.0, 1.0)),
                  static_cast<float>(std::clamp(v + tint[2], 0.0, 1.0)));
        for (int y = 0; y < image.height(); ++y) image.setPixelColor(x, y, c);
    }
    return image;
}

std::array<double, 3> through(const ColorCorrection& c, const QColor& color) {
    return media::colorCorrectedPixel(c, {static_cast<double>(color.redF()), static_cast<double>(color.greenF()), static_cast<double>(color.blueF())});
}

// The 8-bit video luma range of the frame through the correction.
std::pair<double, double> lumaRange(const ColorCorrection& c, const QImage& image) {
    double lo = 1e9, hi = -1e9;
    for (int x = 0; x < image.width(); ++x) {
        const auto p = through(c, image.pixelColor(x, 0));
        const double y = 16.0 + 219.0 * (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
        lo = std::min(lo, y);
        hi = std::max(hi, y);
    }
    return {lo, hi};
}

// How far the frame's colours are from neutral, on average: the spread of the channels.
double cast(const ColorCorrection& c, const QImage& image) {
    double total = 0;
    for (int x = 0; x < image.width(); ++x) {
        const auto p = through(c, image.pixelColor(x, 0));
        total += *std::max_element(p.begin(), p.end()) - *std::min_element(p.begin(), p.end());
    }
    return total / image.width();
}

} // namespace

class TestAutoCorrect : public QObject {
    Q_OBJECT

private slots:
    void tonalRange() {
        // "Maximizes the tonal range in the image ... the brightest areas of the image as bright as possible and the
        // darkest areas of the image as dark as possible" (p.621): to legal black and white.
        // Ours: within the Gain slider's 200%, so a ramp over 60% of the range.
        const QImage flat = ramp(0.2, 0.8);
        ColorCorrection c;
        QVERIFY(media::autoCorrect(c, flat, AutoCorrection::HslContrast, false));
        auto [lo, hi] = lumaRange(c, flat);
        QVERIFY2(std::abs(lo - 16) < 3 && std::abs(hi - 235) < 3, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
        QVERIFY(c.enabled(QStringLiteral("hsl/controls/master/gain")) && c.number(QStringLiteral("hsl/controls/master/gain")) > 100);
        // Again changes nothing worth doing (p.619).
        const ColorCorrection once = c;
        media::autoCorrect(c, flat, AutoCorrection::HslContrast, false);
        QVERIFY(std::abs(c.number(QStringLiteral("hsl/controls/master/gain")) - once.number(QStringLiteral("hsl/controls/master/gain"))) < 2);
        // Auto Black moves only Setup; Auto White only Gain.
        ColorCorrection black;
        QVERIFY(media::autoCorrect(black, flat, AutoCorrection::HslBlack, false));
        QVERIFY(!black.values.contains(QStringLiteral("hsl/controls/master/gain")));
        QVERIFY(std::abs(lumaRange(black, flat).first - 16) < 3);
        ColorCorrection white;
        QVERIFY(media::autoCorrect(white, flat, AutoCorrection::HslWhite, false));
        QVERIFY(!white.values.contains(QStringLiteral("hsl/controls/master/setup")));
        QVERIFY(std::abs(lumaRange(white, flat).second - 235) < 3);
        // From the Hue Offsets tab, the same controls with that tab's button on too (p.603).
        ColorCorrection offsets;
        QVERIFY(media::autoCorrect(offsets, flat, AutoCorrection::HslContrast));
        QCOMPARE(offsets.number(QStringLiteral("hsl/controls/master/gain")), c.number(QStringLiteral("hsl/controls/master/gain")));
        QVERIFY(offsets.applies(QStringLiteral("hsl/controls/master/gain")));
        // Nothing to measure.
        QVERIFY(!media::autoCorrect(c, QImage(), AutoCorrection::HslContrast));
    }

    void balance() {
        // "Balances the colors in the image (eliminating any color cast for most images). Adjusts all three ChromaWheel
        // controls" (p.621).
        const QImage blue = ramp(0.2, 0.8, {-0.04, 0.0, 0.08});
        ColorCorrection c;
        const double before = cast(c, blue);
        QVERIFY(media::autoCorrect(c, blue, AutoCorrection::HslBalance));
        QVERIFY2(cast(c, blue) < before / 3, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(cast(c, blue))));
        int wheels = 0;
        for (const char* w : {"shadows", "midtones", "highlights"}) wheels += c.number(QStringLiteral("hsl/hueOffsets/%1/amount").arg(QLatin1String(w))) > 0 ? 1 : 0;
        QCOMPARE(wheels, 3);
        // The Curves tab's: the Red, Green and Blue curves.
        ColorCorrection curves;
        QVERIFY(media::autoCorrect(curves, blue, AutoCorrection::CurvesBalance));
        QVERIFY(curves.values.contains(QStringLiteral("curves/blue")));
        QVERIFY(cast(curves, blue) < before / 2);
    }

    void curvesContrast() {
        // "Makes an adjustment to the Master curve to maximize the tonal range" (p.621).
        const QImage flat = ramp(0.3, 0.7);
        ColorCorrection c;
        QVERIFY(media::autoCorrect(c, flat, AutoCorrection::CurvesContrast));
        QCOMPARE(model::colorCurve(c, QStringLiteral("master")).size(), 2);
        auto [lo, hi] = lumaRange(c, flat);
        QVERIFY2(lo < 20 && hi > 231, qPrintable(QStringLiteral("%1 %2").arg(lo).arg(hi)));
    }

    void removeColorCast() {
        // The midtones wheel makes a mid grey with a cast neutral, and leaves the shadows nearly alone (p.622).
        const QColor tinted = QColor::fromRgbF(0.5f, 0.5f, 0.6f);
        ColorCorrection c;
        QVERIFY(media::removeColorCast(c, tinted, media::CastRemoval::Midtones));
        const auto p = through(c, tinted);
        QVERIFY2(std::abs(p[2] - p[0]) < 0.03, qPrintable(QStringLiteral("%1 %2 %3").arg(p[0]).arg(p[1]).arg(p[2])));
        QVERIFY(c.values.contains(QStringLiteral("hsl/hueOffsets/midtones/amount")));
        QVERIFY(!c.values.contains(QStringLiteral("hsl/hueOffsets/shadows/amount")));
        // A highlight colour is out of the Shadows wheel's range.
        ColorCorrection none;
        QVERIFY(!media::removeColorCast(none, QColor::fromRgbF(0.95f, 0.95f, 1.0f), media::CastRemoval::Shadows));
        // The Curves button adds a point to each colour curve; again adds another (p.622).
        ColorCorrection curves;
        QVERIFY(media::removeColorCast(curves, tinted, media::CastRemoval::Curves));
        const auto q = through(curves, tinted);
        QVERIFY2(std::abs(q[2] - q[0]) < 0.02 && std::abs(q[1] - q[0]) < 0.02, qPrintable(QStringLiteral("%1 %2 %3").arg(q[0]).arg(q[1]).arg(q[2])));
        QCOMPARE(model::colorCurve(curves, QStringLiteral("red")).size(), 3);
        QVERIFY(media::removeColorCast(curves, QColor::fromRgbF(0.2f, 0.25f, 0.2f), media::CastRemoval::Curves));
        QCOMPARE(model::colorCurve(curves, QStringLiteral("green")).size(), 4);
    }
};

QTEST_MAIN(TestAutoCorrect)
#include "tst_auto_correct.moc"
