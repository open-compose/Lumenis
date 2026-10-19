#include "lumenis/core/rational.h"

#include <numeric>
#include <stdexcept>

// Rational: exact fractions for edit rates and times (rational.h). Every arithmetic result goes
// back through the constructor, so it is reduced to lowest terms with a positive denominator.
namespace lumenis {

Rational::Rational(std::int64_t num, std::int64_t den) {
    if (den == 0) {
        throw std::invalid_argument("Rational: zero denominator");
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const std::int64_t g = std::gcd(num, den);
    num_ = g ? num / g : 0;
    den_ = g ? den / g : 1;
}

std::string Rational::toString() const {
    return std::to_string(num_) + "/" + std::to_string(den_);
}

// C++ division truncates towards zero, so negative values with a remainder step down by one.
std::int64_t Rational::floor() const {
    std::int64_t q = num_ / den_;
    if ((num_ % den_ != 0) && (num_ < 0)) {
        --q;
    }
    return q;
}

// (2n + d) / 2d is n/d + 1/2 truncated; for negatives the same is done on the magnitude.
std::int64_t Rational::round() const {
    const std::int64_t twice = 2 * num_;
    if (num_ >= 0) {
        return (twice + den_) / (2 * den_);
    }
    return -((-twice + den_) / (2 * den_));
}

Rational operator+(const Rational& a, const Rational& b) {
    return {a.num_ * b.den_ + b.num_ * a.den_, a.den_ * b.den_};
}

Rational operator-(const Rational& a, const Rational& b) {
    return {a.num_ * b.den_ - b.num_ * a.den_, a.den_ * b.den_};
}

Rational operator*(const Rational& a, const Rational& b) {
    return {a.num_ * b.num_, a.den_ * b.den_};
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.num_ == 0) {
        throw std::domain_error("Rational: division by zero");
    }
    return {a.num_ * b.den_, a.den_ * b.num_};
}

// Cross-multiplication; valid because both denominators are positive.
std::strong_ordering operator<=>(const Rational& a, const Rational& b) {
    return a.num_ * b.den_ <=> b.num_ * a.den_;
}

} // namespace lumenis
