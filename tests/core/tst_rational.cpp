#include "lumenis/core/rational.h"

#include <QTest>

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
    }
};

QTEST_GUILESS_MAIN(TestRational)
#include "tst_rational.moc"
