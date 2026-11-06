// Safe colour limits (FX Guide ch.23; 6.10e4a): the setting's values, units, and which limits a colour exceeds.
#include "lumenis/model/safe_colors.h"
#include "lumenis/model/safe_color_limiter.h"

#include <QTest>

#include <array>
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

    void limiterParameters() {
        // "When you first apply a Safe Color Limiter effect, its parameters default to the current limits in the Safe
        // Color settings" (FX Guide p.728): seeded, read back as limits.
        Effect e{QStringLiteral("Image"), QStringLiteral("Safe Color Limiter"), {}};
        QVERIFY(isSafeColorLimiter(e));
        // Unset: the json's defaults, the setting's own; both Enable buttons on, 422 Safe off, analysis on.
        SafeLimiting d = safeColorLimiting(e, 0.0, EffectSpan(10));
        QCOMPARE(d.limits.composite, SafeColorLimits{}.composite);
        QVERIFY(d.compositeLuma && d.rgb && !d.exact && d.analysis);
        SafeColorLimits set;
        set.composite = {-20.0, 110.0, SafeUnit::Ire};
        set.luma = {7.5, 100.0, SafeUnit::Ire};
        set.rgb = {5.0, 95.0, SafeUnit::Percent};
        seedSafeColorLimiter(e, set);
        const SafeLimiting s = safeColorLimiting(e, 0.0, EffectSpan(10));
        QCOMPARE(s.limits.composite, set.composite);
        QCOMPARE(s.limits.luma, set.luma);
        QCOMPARE(s.limits.rgb, set.rgb);
        QCOMPARE(e.parameters.value(kSafeColorLimiterCategory).toObject().value(QStringLiteral("RGB Gamut units")).toString(), QStringLiteral("%"));
        // The menus' units, both ways.
        for (SafeUnit u : {SafeUnit::Bit8, SafeUnit::Percent, SafeUnit::Ire, SafeUnit::Millivolts}) QCOMPARE(safeUnitFromMenu(safeUnitMenuText(u)), u);
        // The Enable buttons, 422 Safe and Source Monitoring – Off.
        QJsonObject p = e.parameters.value(kSafeColorLimiterCategory).toObject();
        p.insert(QStringLiteral("RGB Levels Parameters: Enable"), false);
        p.insert(QStringLiteral("422 Safe"), true);
        p.insert(QStringLiteral("Source Monitor Analysis"), QStringLiteral("Source Monitoring – Off"));
        e.parameters.insert(kSafeColorLimiterCategory, p);
        const SafeLimiting off = safeColorLimiting(e, 0.0, EffectSpan(10));
        QVERIFY(off.compositeLuma && !off.rgb && off.exact && !off.analysis);
    }

    void limiting() {
        // Within every limit: unchanged.
        SafeLimiting l;
        l.exact = true;
        const std::array<double, 3> grey{126.0, 126.0, 126.0};
        QCOMPARE(limitSafeLevels(grey, l), grey);
        // RGB Gamut H 200: "any pixels with a higher red, green, or blue level are modified so that their red, green,
        // and blue levels are no higher than" it (p.546).
        l.limits.rgb = {16.0, 200.0, SafeUnit::Bit8};
        const auto white = limitSafeLevels({235.0, 235.0, 235.0}, l);
        for (double v : white) QVERIFY(v <= 200.0 + 1e-6);
        // Luma H 90 IRE: white comes down to it, a grey stays grey.
        SafeLimiting luma;
        luma.limits.luma = {0.0, 90.0, SafeUnit::Ire};
        luma.exact = true;
        const auto down = limitSafeLevels({235.0, 235.0, 235.0}, luma);
        QVERIFY(std::abs(down[0] - fromSafeUnit(90.0, SafeUnit::Ire)) < 0.05);
        QVERIFY(std::abs(down[0] - down[2]) < 1e-6);
        // Composite: vivid yellow (about 133 IRE) within the default 120 IRE, its hue kept (red equal to green, blue
        // below them).
        SafeLimiting composite;
        composite.exact = true;
        const auto yellow = limitSafeLevels({235.0, 235.0, 16.0}, composite);
        QVERIFY(!safeViolations(static_cast<int>(std::floor(yellow[0])), static_cast<int>(std::floor(yellow[1])), static_cast<int>(std::lround(yellow[2])),
                                composite.limits)
                     .compositeHigh);
        QVERIFY(std::abs(yellow[0] - yellow[1]) < 1e-6 && yellow[2] < yellow[0]);
        // With Composite/Luma Levels off, nothing changes for yellow.
        composite.compositeLuma = false;
        QCOMPARE(limitSafeLevels({235.0, 235.0, 16.0}, composite), (std::array<double, 3>{235.0, 235.0, 16.0}));
        // 422 Safe: every enabled limit holds for many colours; one pass may leave some just outside, never more.
        SafeLimiting tight;
        tight.limits.composite = {0.0, 100.0, SafeUnit::Ire};
        tight.limits.luma = {5.0, 95.0, SafeUnit::Ire};
        tight.limits.rgb = {20.0, 230.0, SafeUnit::Bit8};
        tight.exact = true;
        for (int r = 16; r <= 235; r += 31) {
            for (int g = 16; g <= 235; g += 31) {
                for (int b = 16; b <= 235; b += 31) {
                    const auto c = limitSafeLevels({double(r), double(g), double(b)}, tight);
                    for (double x : c) QVERIFY(x >= 20.0 - 1e-6 && x <= 230.0 + 1e-6);
                    const double y = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
                    QVERIFY2(y >= fromSafeUnit(5.0, SafeUnit::Ire) - 0.6 && y <= fromSafeUnit(95.0, SafeUnit::Ire) + 0.6, qPrintable(QString::number(y)));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestSafeColors)
#include "tst_safe_colors.moc"
