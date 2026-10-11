#include "lumenis/core/timecode.h"

#include <QTest>

using lumenis::Rational;
using lumenis::Timecode;
using lumenis::TimecodeFormat;

namespace {

const TimecodeFormat k24{24, false};
const TimecodeFormat k25{25, false};
const TimecodeFormat k30df{30, true};
const TimecodeFormat k30ndf{30, false};
const TimecodeFormat k60df{60, true};

QString str(const Timecode& tc) {
    return QString::fromStdString(tc.toString());
}

std::int64_t frames(const char* text, TimecodeFormat format) {
    const auto tc = Timecode::parse(text, format);
    return tc ? tc->frameCount() : -1;
}

} // namespace

class TestTimecode : public QObject {
    Q_OBJECT

private slots:
    void formatForRate() {
        QCOMPARE(TimecodeFormat::forRate(Rational(24000, 1001), false), k24);
        QCOMPARE(TimecodeFormat::forRate(Rational(30000, 1001), true), k30df);
        QCOMPARE(TimecodeFormat::forRate(Rational(60000, 1001), true), k60df);
        // Drop frame is meaningless at 25 fps and is ignored.
        QCOMPARE(TimecodeFormat::forRate(Rational(25), true), k25);
    }

    void nonDropFrame() {
        QCOMPARE(str(Timecode(0, k25)), QStringLiteral("00:00:00:00"));
        QCOMPARE(str(Timecode(90000, k25)), QStringLiteral("01:00:00:00"));
        QCOMPARE(str(Timecode(86399, k24)), QStringLiteral("00:59:59:23"));
        QCOMPARE(frames("01:00:00:00", k24), 86400);
        QCOMPARE(frames("10:00:00:00", k30ndf), 1080000);
    }

    // Reference values from SMPTE ST 12-1 drop-frame counting.
    void dropFrame30() {
        QCOMPARE(str(Timecode(1799, k30df)), QStringLiteral("00:00:59;29"));
        QCOMPARE(str(Timecode(1800, k30df)), QStringLiteral("00:01:00;02"));
        QCOMPARE(str(Timecode(17982, k30df)), QStringLiteral("00:10:00;00"));
        QCOMPARE(str(Timecode(107892, k30df)), QStringLiteral("01:00:00;00"));
        QCOMPARE(frames("00:01:00;02", k30df), 1800);
        QCOMPARE(frames("01:00:00;00", k30df), 107892);
        QCOMPARE(k30df.framesPerDay(), 2589408);
    }

    void dropFrame60() {
        QCOMPARE(str(Timecode(3599, k60df)), QStringLiteral("00:00:59;59"));
        QCOMPARE(str(Timecode(3600, k60df)), QStringLiteral("00:01:00;04"));
        QCOMPARE(frames("01:00:00;00", k60df), 215784);
    }

    void dropFrameRoundTripsEveryFrameOfAnHour() {
        for (std::int64_t f = 0; f < 107892 * 2; ++f) {
            const Timecode tc(f, k30df);
            const auto parsed = Timecode::parse(tc.toString(), k30df);
            QVERIFY2(parsed, tc.toString().c_str());
            QCOMPARE(parsed->frameCount(), f);
        }
    }

    void rejectsNonexistentDropFrameLabels() {
        QVERIFY(!Timecode::parse("00:01:00;00", k30df));
        QVERIFY(!Timecode::parse("00:01:00;01", k30df));
        QVERIFY(Timecode::parse("00:10:00;00", k30df));
        QVERIFY(!Timecode::parse("00:02:00;03", k60df));
    }

    void rejectsMalformed() {
        QVERIFY(!Timecode::parse("", k25));
        QVERIFY(!Timecode::parse("00:00:00", k25));
        QVERIFY(!Timecode::parse("00:00:00:25", k25));
        QVERIFY(!Timecode::parse("24:00:00:00", k25));
        QVERIFY(!Timecode::parse("00:60:00:00", k25));
        QVERIFY(!Timecode::parse("00:00:00:00x", k25));
        QVERIFY(!Timecode::parse("00-00-00-00", k25));
        QVERIFY(Timecode::parse("00.00.00.00", k25));
    }

    void wrapsPastMidnightForDisplay() {
        QCOMPARE(str(Timecode(25 * 86400, k25)), QStringLiteral("00:00:00:00"));
        QCOMPARE(str(Timecode(-1, k25)), QStringLiteral("23:59:59:24"));
    }
};

QTEST_GUILESS_MAIN(TestTimecode)
#include "tst_timecode.moc"
