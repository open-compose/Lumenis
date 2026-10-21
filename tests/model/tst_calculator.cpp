#include "lumenis/model/calculator.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

int formatIndex(const QString& name) {
    const auto& formats = calculatorFormats();
    for (int i = 0; i < formats.size(); ++i) {
        if (formats.at(i).name == name) return i;
    }
    return -1;
}

void type(Calculator& c, const char* digits) {
    for (const char* p = digits; *p; ++p) c.digit(*p - '0');
}

} // namespace

class TestCalculator : public QObject {
    Q_OBJECT

private slots:
    void typesWithoutColonsOrLeadingZeros() {
        // p.100: no leading zeros, colons or semicolons.
        Calculator c;
        c.setFormat(formatIndex(QStringLiteral("25 Timecode")));
        QCOMPARE(c.display(), QStringLiteral("00:00:00:00"));
        type(c, "1");
        QCOMPARE(c.display(), QStringLiteral("00:00:00:01"));
        type(c, "000");
        QCOMPARE(c.display(), QStringLiteral("00:00:10:00"));
        QCOMPARE(c.value(), 250);
        c.backspace();
        QCOMPARE(c.display(), QStringLiteral("00:00:01:00"));
        c.doubleZero();
        QCOMPARE(c.display(), QStringLiteral("00:01:00:00"));
    }

    void addsAndSubtracts() {
        Calculator c;
        c.setFormat(formatIndex(QStringLiteral("25 Timecode")));
        type(c, "1000"); // 10 s
        c.add();
        type(c, "520"); // 5 s 20 fr
        c.equals();
        QCOMPARE(c.display(), QStringLiteral("00:00:15:20"));
        c.subtract();
        type(c, "2000");
        c.equals();
        QCOMPARE(c.display(), QStringLiteral("-00:00:04:05"));
        c.clear();
        QCOMPARE(c.value(), 0);
        // Fields past their limits still add up: 90 seconds.
        type(c, "9000");
        QCOMPARE(c.display(), QStringLiteral("00:01:30:00"));
    }

    void convertsBetweenFormats() {
        Calculator c;
        // Drop-frame and non-drop-frame at 29.97 keep the frame count.
        c.setFormat(formatIndex(QStringLiteral("29.97 NDF Timecode")));
        type(c, "100000"); // 10 minutes NDF
        c.equals();
        QCOMPARE(c.value(), 18000);
        c.setFormat(formatIndex(QStringLiteral("29.97 DF Timecode")));
        QCOMPARE(c.value(), 18000);
        QCOMPARE(c.display(), QStringLiteral("00:10:00;18"));
        // 30 to 25 fps keeps the duration in time (p.100).
        c.clear();
        c.setFormat(formatIndex(QStringLiteral("30 Timecode")));
        type(c, "10000"); // 1 minute
        c.setFormat(formatIndex(QStringLiteral("25 Timecode")));
        QCOMPARE(c.display(), QStringLiteral("00:01:00:00"));
        // A minute of 24 fps video is 90 feet of 35mm film.
        c.setFormat(formatIndex(QStringLiteral("24 Timecode")));
        c.setFormat(formatIndex(QStringLiteral("35mm, 4 perf (16 fr/ft)")));
        QCOMPARE(c.display(), QStringLiteral("90+00"));
        c.clear();
        type(c, "1215");
        QCOMPARE(c.value(), 12 * 16 + 15);
        QCOMPARE(c.display(), QStringLiteral("12+15"));
    }

    void dropFrameLabelsThatDoNotExist() {
        // Digits Timecode::parse rejects in drop-frame still count in drop-frame: a skipped
        // label is the next one that exists, and fields past their limits are carried first.
        Calculator c;
        c.setFormat(formatIndex(QStringLiteral("29.97 DF Timecode")));
        type(c, "110000"); // 00:11:00;00 is skipped: 00:11:00;02
        QCOMPARE(c.value(), c.parse(QStringLiteral("110002")));
        QCOMPARE(c.display(), QStringLiteral("00:11:00;02"));
        c.clear();
        type(c, "9000"); // 90 seconds: 00:01:30;00
        QCOMPARE(c.value(), c.parse(QStringLiteral("13000")));
        QCOMPARE(c.display(), QStringLiteral("00:01:30;00"));
        c.clear();
        type(c, "5940"); // 59 seconds and 40 frames: 00:01:00;10
        QCOMPARE(c.display(), QStringLiteral("00:01:00;10"));
        // Non-drop-frame is unchanged.
        c.setFormat(formatIndex(QStringLiteral("29.97 NDF Timecode")));
        c.clear();
        type(c, "9000");
        QCOMPARE(c.value(), 90 * 30);
    }

    void convertKeepsTime() {
        const auto& f = calculatorFormats();
        const CalculatorFormat& ntsc = f.at(formatIndex(QStringLiteral("29.97 NDF Timecode")));
        const CalculatorFormat& film = f.at(formatIndex(QStringLiteral("23.976 Timecode")));
        QCOMPARE(Calculator::convert(30, ntsc, film), 24);
        QCOMPARE(Calculator::convert(1001, film, film), 1001);
    }
};

QTEST_GUILESS_MAIN(TestCalculator)
#include "tst_calculator.moc"
