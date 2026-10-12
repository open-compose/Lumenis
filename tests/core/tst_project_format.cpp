#include "lumenis/core/project_format.h"

#include <QTest>

using namespace lumenis;

namespace {

int countWhere(auto pred) {
    int n = 0;
    for (const ProjectFormat& f : projectFormatPresets()) {
        n += pred(f) ? 1 : 0;
    }
    return n;
}

} // namespace

class TestProjectFormat : public QObject {
    Q_OBJECT

private slots:
    void frameRatesAreExact() {
        QCOMPARE(*parseFrameRate("23.976"), Rational(24000, 1001));
        QCOMPARE(*parseFrameRate("29.97"), Rational(30000, 1001));
        QCOMPARE(*parseFrameRate("119.88"), Rational(120000, 1001));
        QCOMPARE(*parseFrameRate("25"), Rational(25));
        QVERIFY(!parseFrameRate("26"));
        QCOMPARE(frameRateText(Rational(60000, 1001)), std::string("59.94"));
        QCOMPARE(frameRateText(Rational(48)), std::string("48"));
    }

    void presetsFollowTheGuide() {
        const auto& presets = projectFormatPresets();
        // Spot checks against the guide's tables (ch. 35).
        const auto has = [&](const char* group, int w, int h, Rational rate, ScanMode scan) {
            return countWhere([&](const ProjectFormat& f) {
                       return f.group == group && f.width == w && f.height == h && f.editRate == rate && f.scan == scan;
                   }) == 1;
        };
        QVERIFY(has("HD 1080p", 1920, 1080, Rational(24000, 1001), ScanMode::Progressive));
        QVERIFY(has("HD 1080i", 1920, 1080, Rational(30000, 1001), ScanMode::Interlaced));
        QVERIFY(has("HD 1080i", 1920, 1080, Rational(25), ScanMode::Interlaced));
        QVERIFY(has("UHD", 3840, 2160, Rational(120), ScanMode::Progressive));
        QVERIFY(has("2K DCI Scope", 2048, 858, Rational(24), ScanMode::Progressive));
        QVERIFY(has("PAL", 720, 576, Rational(25), ScanMode::Interlaced));
        QVERIFY(has("NTSC", 720, 486, Rational(24000, 1001), ScanMode::Progressive));
        // No 720p at 24 fps in the guide's HD table.
        QVERIFY(!has("HD 720p", 1280, 720, Rational(24), ScanMode::Progressive));

        // Labels are unique so the Format menu is unambiguous.
        QStringList labels;
        for (const ProjectFormat& f : presets) {
            labels << QString::fromStdString(f.label());
        }
        QCOMPARE(labels.removeDuplicates(), 0);
    }

    void labels() {
        ProjectFormat f{"HD 1080i", FormatFamily::HD, 1920, 1080, Rational(30000, 1001), ScanMode::Interlaced};
        QCOMPARE(f.label(), std::string("1080i/59.94"));
        f = {"HD 1080p", FormatFamily::HD, 1440, 1080, Rational(24000, 1001), ScanMode::Progressive};
        QCOMPARE(f.label(), std::string("1440x1080p/23.976"));
        f = {"PAL", FormatFamily::SD, 720, 576, Rational(25), ScanMode::Interlaced};
        QCOMPARE(f.label(), std::string("PAL 50i"));
        f = {"2K DCI Flat", FormatFamily::HighRes, 1998, 1080, Rational(24), ScanMode::Progressive};
        QCOMPARE(f.label(), std::string("2K DCI Flat 1998x1080 24p"));
        QCOMPARE(f.defaultAspect(), Rational(1998, 1080));
    }

    void customRasterRules() {
        QVERIFY(!validateCustomRaster(1920, 1080, false));
        QVERIFY(!validateCustomRaster(256, 120, false));
        QVERIFY(!validateCustomRaster(8192, 8192, false));
        QVERIFY(validateCustomRaster(1921, 1080, false));
        QVERIFY(validateCustomRaster(254, 1080, false));
        QVERIFY(validateCustomRaster(8194, 1080, false));
        QVERIFY(validateCustomRaster(1924, 1080, true));  // width not divisible by 8
        QVERIFY(!validateCustomRaster(1600, 6000, true)); // the guide's own example
    }

    void nameRules() {
        QVERIFY(!validateName("Episode 1", kMaxProjectNameLength));
        QVERIFY(validateName("", kMaxProjectNameLength));
        QVERIFY(validateName("a/b", kMaxProjectNameLength));
        QVERIFY(validateName("what?", kMaxProjectNameLength));
        QVERIFY(validateName(" lead", kMaxProjectNameLength));
        QVERIFY(validateName("trail ", kMaxProjectNameLength));
        QVERIFY(validateName("dot.", kMaxProjectNameLength));
        QVERIFY(!validateName(std::string(56, 'x'), kMaxProjectNameLength));
        QVERIFY(validateName(std::string(57, 'x'), kMaxProjectNameLength));
        QVERIFY(!validateName(std::string(64, 'x'), kMaxBinNameLength));
        // 56 two-byte characters are still 56 characters.
        std::string accented;
        for (int i = 0; i < 56; ++i) accented += "\xC3\xA9";
        QVERIFY(!validateName(accented, kMaxProjectNameLength));
    }
};

QTEST_GUILESS_MAIN(TestProjectFormat)
#include "tst_project_format.moc"
