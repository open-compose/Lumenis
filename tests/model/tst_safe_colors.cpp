// Safe colour limits (FX Guide ch.23; 6.10e4a): the setting's values, units, and which limits a colour exceeds.
#include "lumenis/model/safe_colors.h"

#include <QTest>

#include <cmath>

using namespace lumenis::model;

class TestSafeColors : public QObject {
    Q_OBJECT

private slots:
    void defaultsAndValues() {
        // The defaults of the guide's picture (p.723): Composite 0-120 IRE, Luminance 10-108.45 IRE, RGB 16-235 8 Bit, Ignore.
        const SafeColorLimits d;
        QCOMPARE(d.composite, (SafeRange{0.0, 120.0, SafeUnit::Ire}));
        QCOMPARE(d.luma, (SafeRange{10.0, 108.45, SafeUnit::Ire}));
        QCOMPARE(d.rgb, (SafeRange{16.0, 235.0, SafeUnit::Bit8}));
        QVERIFY(!d.warns());
        QCOMPARE(SafeColorLimits::fromValues({}), d);
        SafeColorLimits l;
        l.luma = {16.0, 235.0, SafeUnit::Bit8};
        l.warnRgb = true;
        QCOMPARE(SafeColorLimits::fromValues(l.toValues()), l);
        QVERIFY(l.warns());
        QCOMPARE(l.toValues().value(QStringLiteral("rgbAction")).toString(), QStringLiteral("warn"));
    }

    void units() {
        // Reference black 16 and white 235 (p.722): 0 and 100 IRE or percent, 0 and 700 mV.
        QCOMPARE(toSafeUnit(16, SafeUnit::Ire), 0.0);
        QCOMPARE(toSafeUnit(235, SafeUnit::Percent), 100.0);
        QCOMPARE(toSafeUnit(235, SafeUnit::Millivolts), 700.0);
        QCOMPARE(toSafeUnit(200, SafeUnit::Bit8), 200.0);
        for (SafeUnit u : {SafeUnit::Bit8, SafeUnit::Percent, SafeUnit::Ire, SafeUnit::Millivolts}) {
            QVERIFY(std::abs(fromSafeUnit(toSafeUnit(123.0, u), u) - 123.0) < 1e-9);
            QCOMPARE(safeUnitFrom(safeUnitKey(u)), u);
        }
    }

    void levelsAndViolations() {
        // Reference white: luma 235 (100 IRE), no chroma.
        SafeLevels white = safeLevels(235, 235, 235);
        QVERIFY(std::abs(white.luma - 235.0) < 1e-9);
        QVERIFY(std::abs(white.compositeHigh - white.compositeLow) < 1e-9);
        // 100% yellow reaches about 133 IRE on the composite signal ("vivid yellow ... can produce composite levels that
        // are too high", p.727).
        const SafeLevels yellow = safeLevels(235, 235, 16);
        QVERIFY2(std::abs(toSafeUnit(yellow.compositeHigh, SafeUnit::Ire) - 133.0) < 1.0, qPrintable(QString::number(toSafeUnit(yellow.compositeHigh, SafeUnit::Ire))));
        // Within the defaults: nothing; yellow past the composite high; full-range white and black past RGB and luma.
        const SafeColorLimits d;
        QVERIFY(!safeViolations(128, 128, 128, d).any());
        const SafeViolations y = safeViolations(235, 235, 16, d);
        QVERIFY(y.compositeHigh && !y.compositeLow && !y.lumaHigh);
        // Full-range white: past RGB Gamut's 235 and past 108.45 IRE (level 253.5); level 252 is within luma.
        const SafeViolations hot = safeViolations(255, 255, 255, d);
        QVERIFY(hot.rgbHigh[0] && hot.rgbHigh[1] && hot.rgbHigh[2]);
        QVERIFY(hot.lumaHigh);
        QVERIFY(!safeViolations(252, 252, 252, d).lumaHigh);
        const SafeViolations black = safeViolations(0, 0, 0, d);
        QVERIFY(black.lumaLow && black.rgbLow[0] && black.compositeLow);
        // A tighter luma limit catches what the default lets through.
        SafeColorLimits tight;
        tight.luma = {16.0, 235.0, SafeUnit::Bit8};
        QVERIFY(safeViolations(245, 245, 245, tight).lumaHigh);
        SafeViolations joined;
        joined.add(y);
        joined.add(black);
        QVERIFY(joined.compositeHigh && joined.compositeLow && joined.lumaLow);
    }

    void picturesAndWarnings() {
        // A picture's 0 and 255 stand for reference black and white (6.10e4b).
        QCOMPARE(pictureLevel(0), 16);
        QCOMPARE(pictureLevel(255), 235);
        QCOMPARE(pictureLevel(128), 126);
        // Only the types set to Warn count (p.724): the top Actions button for Composite and Luminance, the bottom one
        // for RGB Gamut (p.722).
        SafeViolations all;
        all.compositeHigh = true;
        all.lumaLow = true;
        all.rgbHigh = {true, false, false};
        SafeColorLimits l;
        QVERIFY(!warnedViolations(all, l).any());
        l.warnRgb = true;
        SafeViolations w = warnedViolations(all, l);
        QVERIFY(w.rgbHigh[0] && !w.compositeHigh && !w.lumaLow);
        l.warnRgb = false;
        l.warnCompositeLuma = true;
        w = warnedViolations(all, l);
        QVERIFY(w.compositeHigh && w.lumaLow && !w.rgbHigh[0]);
    }
};

QTEST_GUILESS_MAIN(TestSafeColors)
#include "tst_safe_colors.moc"
