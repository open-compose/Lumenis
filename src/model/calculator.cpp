#include "lumenis/model/calculator.h"

#include "lumenis/core/timecode.h"

#include <algorithm>
#include <cstdlib>

// The Calculator tool's arithmetic (calculator.h), kept out of the UI so it can be tested. The
// window itself is in src/ui/tool_windows.*.
namespace lumenis::model {

namespace {

// HHMMSSFF, or six digits of feet and two of frames.
constexpr int kMaxDigits = 8;

TimecodeFormat timecodeFormat(const CalculatorFormat& f) {
    return TimecodeFormat{f.nominalFps, f.dropFrame};
}

} // namespace

const QList<CalculatorFormat>& calculatorFormats() {
    static const QList<CalculatorFormat> formats{
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "23.976 Timecode")), Rational(24000, 1001), 24, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "24 Timecode")), Rational(24), 24, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "25 Timecode")), Rational(25), 25, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "29.97 NDF Timecode")), Rational(30000, 1001), 30, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "29.97 DF Timecode")), Rational(30000, 1001), 30, true, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "30 Timecode")), Rational(30), 30, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "50 Timecode")), Rational(50), 50, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "59.94 NDF Timecode")), Rational(60000, 1001), 60, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "59.94 DF Timecode")), Rational(60000, 1001), 60, true, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "60 Timecode")), Rational(60), 60, false, 0},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "35mm, 4 perf (16 fr/ft)")), Rational(24), 0, false, 16},
        {QStringLiteral(QT_TRANSLATE_NOOP("calculator", "16mm (40 fr/ft)")), Rational(24), 0, false, 40},
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

// The last eight digits, padded on the left with zeros, are split into two-digit fields.
std::int64_t Calculator::parse(const QString& digits) const {
    const CalculatorFormat& f = calculatorFormats().at(format_);
    const QString d = digits.right(kMaxDigits).rightJustified(kMaxDigits, QLatin1Char('0'));
    auto part = [&](int from) { return static_cast<std::int64_t>(d.mid(from, 2).toInt()); };
    if (f.isFilm()) {
        // Feet, then two digits of frames.
        return d.left(kMaxDigits - 2).toLongLong() * f.framesPerFoot + part(kMaxDigits - 2);
    }
    // Fields past their limits (90 seconds, 40 frames) still add up: they are carried into the
    // next field first. A label drop-frame skips (00:01:00;00) counts as the next label that
    // exists (00:01:00;02), so the count follows drop-frame like any other label.
    const std::int64_t fps = f.nominalFps;
    std::int64_t frames = part(6);
    std::int64_t seconds = part(4) + frames / fps;
    frames %= fps;
    std::int64_t minutes = part(2) + seconds / 60;
    seconds %= 60;
    const std::int64_t hours = part(0) + minutes / 60;
    minutes %= 60;
    const TimecodeFormat format = timecodeFormat(f);
    if (seconds == 0 && minutes % 10 != 0) frames = std::max<std::int64_t>(frames, format.framesDroppedPerMinute());
    const TimecodeFields fields{static_cast<int>(hours), static_cast<int>(minutes), static_cast<int>(seconds),
                                static_cast<int>(frames)};
    return Timecode::fromFields(fields, format).frameCount();
}

// Film is "feet+frames"; timecode uses Timecode's text (so it wraps after 24 hours). Negative
// values get a leading minus sign.
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
