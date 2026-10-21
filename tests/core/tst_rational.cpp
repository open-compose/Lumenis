#include "lumenis/core/rational.h"

#include <QTest>

#include <limits>
#include <stdexcept>

using lumenis::Rational;

class TestRational : public QObject {
    Q_OBJECT

private slots:
    void normalises() {
        const Rational r(60000, 2002);
        QCOMPARE(r.num(), 30000);
        QCOMPARE(r.den(), 1001);
        const Rational negative(1, -2);
        QCOMPARE(negative.num(), -1);
        QCOMPARE(negative.den(), 2);
        QCOMPARE(Rational(0, 5).den(), 1);
    }

    void rejectsZeroDenominator() {
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, Rational(1, 0));
        QVERIFY_THROWS_EXCEPTION(std::domain_error, Rational(1) / Rational(0));
    }

    void arithmetic() {
        QCOMPARE(Rational(1, 2) + Rational(1, 3), Rational(5, 6));
        QCOMPARE(Rational(1, 2) - Rational(1, 3), Rational(1, 6));
        QCOMPARE(Rational(30000, 1001) * Rational(1001), Rational(30000));
        QCOMPARE(Rational(1) / Rational(25), Rational(1, 25));
        QCOMPARE(-Rational(3, 4), Rational(-3, 4));
    }

    void ordering() {
        QVERIFY(Rational(24000, 1001) < Rational(24));
        QVERIFY(Rational(-1, 2) < Rational(0));
        QVERIFY(Rational(2, 4) == Rational(1, 2));
    }

    void rounding() {
        QCOMPARE(Rational(30000, 1001).round(), 30);
        QCOMPARE(Rational(24000, 1001).round(), 24);
        QCOMPARE(Rational(5, 2).round(), 3);
        QCOMPARE(Rational(-5, 2).round(), -3);
        QCOMPARE(Rational(-5, 2).floor(), -3);
        QCOMPARE(Rational(5, 2).floor(), 2);
        QCOMPARE(Rational(30000, 1001).toString(), std::string("30000/1001"));
        // Values whose doubled numerator would overflow.
        constexpr std::int64_t big = std::numeric_limits<std::int64_t>::max();
        QCOMPARE(Rational(big, 2).round(), big / 2 + 1);
        QCOMPARE(Rational(-big, 2).round(), -(big / 2 + 1));
        QCOMPARE(Rational(-big, big - 1).floor(), -2);
    }

    void largeValues() {
        constexpr std::int64_t big = std::numeric_limits<std::int64_t>::max();
        constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
        // Cross-reduction keeps products that fit in lowest terms exact: a frame count near the
        // limit through an edit rate and back.
        const Rational frames(big / 30000 * 30000);
        QCOMPARE(frames * Rational(1001, 30000) * Rational(30000, 1001), frames);
        QCOMPARE(Rational(big, 3) * Rational(3, big), Rational(1));
        QCOMPARE(Rational(big, 1001) / Rational(big, 1001), Rational(1));
        // Sums over the lcm, not the product, of the denominators.
        QCOMPARE(Rational(1, big) + Rational(1, big), Rational(2, big));
        QCOMPARE(Rational(big - 1) + Rational(1), Rational(big));
        // INT64_MIN reduces when it can and is refused when it cannot.
        QCOMPARE(Rational(min, 2), Rational(min / 2));
        QCOMPARE(Rational(2, min), Rational(-1, -(min / 2)));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational{min});
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational{1, min});
        // Results that do not fit throw instead of wrapping.
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(big) + Rational(1));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(-big) - Rational(2));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(big) * Rational(2));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(1, big) * Rational(1, 2));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(1, big) + Rational(1, big - 1));
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, Rational(big) / Rational(1, 2));
        QCOMPARE(-Rational(-big), Rational(big));
    }

    void orderingLargeValues() {
        // Cross-multiplying these would overflow; the comparison must still be exact.
        constexpr std::int64_t big = std::numeric_limits<std::int64_t>::max();
        QVERIFY(Rational(big - 2, big - 1) < Rational(big - 1, big));
        QVERIFY(Rational(-(big - 1), big) < Rational(-(big - 2), big - 1));
        QVERIFY(Rational(big, 2) > Rational(big - 1, 2));
        QVERIFY(Rational(-big, 3) < Rational(big, 7));
        QVERIFY(Rational(1, big) > Rational(0));
        QVERIFY(Rational(1, big) < Rational(1, big - 1));
        QVERIFY((Rational(big, big - 1) <=> Rational(big, big - 1)) == 0);
        QVERIFY(Rational(5) > Rational(9, 2));
        QVERIFY(Rational(-5) < Rational(-9, 2));
    }
};

QTEST_GUILESS_MAIN(TestRational)
#include "tst_rational.moc"
