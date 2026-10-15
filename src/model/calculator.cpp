#include "lumenis/model/calculator.h"

#include "lumenis/core/timecode.h"

#include <cstdlib>

namespace lumenis::model {

namespace {

constexpr int kMaxDigits = 8;

TimecodeFormat timecodeFormat(const CalculatorFormat& f) {
    return TimecodeFormat{f.nominalFps, f.dropFrame};
}

} // namespace

const QList<CalculatorFormat>& calculatorFormats() {
    static const QList<CalculatorFormat> formats{
        {QStringLiteral("23.976 Timecode"), Rational(24000, 1001), 24, false, 0},
        {QStringLiteral("24 Timecode"), Rational(24), 24, false, 0},
        {QStringLiteral("25 Timecode"), Rational(25), 25, false, 0},
        {QStringLiteral("29.97 NDF Timecode"), Rational(30000, 1001), 30, false, 0},
        {QStringLiteral("29.97 DF Timecode"), Rational(30000, 1001), 30, true, 0},
        {QStringLiteral("30 Timecode"), Rational(30), 30, false, 0},
        {QStringLiteral("50 Timecode"), Rational(50), 50, false, 0},
        {QStringLiteral("59.94 NDF Timecode"), Rational(60000, 1001), 60, false, 0},
        {QStringLiteral("59.94 DF Timecode"), Rational(60000, 1001), 60, true, 0},
        {QStringLiteral("60 Timecode"), Rational(60), 60, false, 0},
        {QStringLiteral("35mm, 4 perf (16 fr/ft)"), Rational(24), 0, false, 16},
        {QStringLiteral("16mm (40 fr/ft)"), Rational(24), 0, false, 40},
    };
    return formats;
}

Calculator::Calculator() {
    // 29.97 non-drop-frame: the NTSC default.
    format_ = 3;
}

std::int64_t Calculator::convert(std::int64_t frames, const CalculatorFormat& from, const CalculatorFormat& to) {
    if (from.rate == to.rate) return frames;
    // Same duration in time: frames / from.rate * to.rate, rounded to the
    // nearest frame, in integers (no floating point: a long double is only
    // 64 bits on some platforms). Rates are at most 60000/1001, so this does
    // not overflow for any duration the calculator shows.
    const std::int64_t num = from.rate.den() * to.rate.num();
    const std::int64_t den = from.rate.num() * to.rate.den();
    const std::int64_t n = std::llabs(frames) * num;
    const std::int64_t rounded = (n + den / 2) / den;
    return frames < 0 ? -rounded : rounded;
}

void Calculator::setFormat(int index) {
    if (index < 0 || index >= calculatorFormats().size() || index == format_) return;
    if (!entry_.isEmpty() || op_) equals();
    total_ = convert(total_, calculatorFormats().at(format_), calculatorFormats().at(index));
    format_ = index;
}

std::int64_t Calculator::parse(const QString& digits) const {
    const CalculatorFormat& f = calculatorFormats().at(format_);
    const QString d = digits.right(kMaxDigits).rightJustified(kMaxDigits, QLatin1Char('0'));
    auto part = [&](int from) { return static_cast<std::int64_t>(d.mid(from, 2).toInt()); };
    if (f.isFilm()) {
        // Feet, then two digits of frames.
        return d.left(kMaxDigits - 2).toLongLong() * f.framesPerFoot + part(kMaxDigits - 2);
    }
    const TimecodeFields fields{static_cast<int>(part(0)), static_cast<int>(part(2)), static_cast<int>(part(4)),
                                static_cast<int>(part(6))};
    if (const auto tc = Timecode::parse(
            QStringLiteral("%1:%2:%3%4%5")
                .arg(fields.hours, 2, 10, QLatin1Char('0'))
                .arg(fields.minutes, 2, 10, QLatin1Char('0'))
                .arg(fields.seconds, 2, 10, QLatin1Char('0'))
                .arg(f.dropFrame ? QLatin1Char(';') : QLatin1Char(':'))
                .arg(fields.frames, 2, 10, QLatin1Char('0'))
                .toStdString(),
            timecodeFormat(f))) {
        return tc->frameCount();
    }
    // Fields past their limits (90 seconds, 40 frames) still add up.
    return ((part(0) * 60 + part(2)) * 60 + part(4)) * f.nominalFps + part(6);
}

QString Calculator::text(std::int64_t frames) const {
    const CalculatorFormat& f = calculatorFormats().at(format_);
    const QString sign = frames < 0 ? QStringLiteral("-") : QString();
    const std::int64_t n = std::llabs(frames);
    if (f.isFilm()) {
        return sign + QStringLiteral("%1+%2").arg(n / f.framesPerFoot).arg(n % f.framesPerFoot, 2, 10, QLatin1Char('0'));
    }
    return sign + QString::fromStdString(Timecode(n, timecodeFormat(f)).toString());
}

void Calculator::digit(int d) {
    if (d < 0 || d > 9 || entry_.size() >= kMaxDigits) return;
    entry_ += QChar(u'0' + d);
}

void Calculator::doubleZero() {
    digit(0);
    digit(0);
}

void Calculator::backspace() {
    entry_.chop(1);
}

void Calculator::commit() {
    if (entry_.isEmpty()) return;
    const std::int64_t v = parse(entry_);
    entry_.clear();
    if (op_ == '+') {
        total_ += v;
    } else if (op_ == '-') {
        total_ -= v;
    } else {
        total_ = v;
    }
}

void Calculator::add() {
    commit();
    op_ = '+';
}

void Calculator::subtract() {
    commit();
    op_ = '-';
}

void Calculator::equals() {
    commit();
    op_ = 0;
}

void Calculator::clear() {
    total_ = 0;
    entry_.clear();
    op_ = 0;
}

std::int64_t Calculator::value() const {
    return entry_.isEmpty() ? total_ : parse(entry_);
}

QString Calculator::display() const {
    return text(value());
}

} // namespace lumenis::model
