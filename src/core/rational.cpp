#include "lumenis/core/rational.h"

#include <limits>
#include <numeric>
#include <stdexcept>

// Rational: exact fractions for edit rates and times (rational.h). Every arithmetic result goes
// back through the constructor, so it is reduced to lowest terms with a positive denominator.
// Operands are cross-reduced by their gcds before multiplying, and every step that could leave 64
// bits is checked portably (no __int128 or compiler built-ins, which MSVC lacks); one that would
// throws std::overflow_error. Comparison and rounding never overflow.
namespace lumenis {

namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

// |v| as unsigned, also for INT64_MIN (whose magnitude has no signed type).
std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

[[noreturn]] void overflow() {
    throw std::overflow_error("Rational: result does not fit in 64 bits");
}

// a * b, or overflow_error. The operands come from Rationals, which never hold INT64_MIN, so the
// product's magnitude must fit INT64_MAX.
std::int64_t checkedMul(std::int64_t a, std::int64_t b) {
    if (a == 0 || b == 0) return 0;
    const std::uint64_t ua = magnitude(a);
    const std::uint64_t ub = magnitude(b);
    if (ua > static_cast<std::uint64_t>(kMax) / ub) overflow();
    const auto product = static_cast<std::int64_t>(ua * ub);
    return (a < 0) != (b < 0) ? -product : product;
}

// a + b, or overflow_error. The sum may be INT64_MIN; the constructor reduces or refuses it.
std::int64_t checkedAdd(std::int64_t a, std::int64_t b) {
    if ((b > 0 && a > kMax - b) || (b < 0 && a < kMin - b)) overflow();
    return a + b;
}

// gcd(|a|, b) for b > 0; std::gcd needs |a| representable, which it is (no INT64_MIN here).
std::int64_t gcdWith(std::int64_t a, std::int64_t b) {
    return std::gcd(a, b);
}

// Floor division with a remainder in [0, d), for d > 0, without forming q * d (which can overflow).
void floorDivMod(std::int64_t n, std::int64_t d, std::int64_t& q, std::int64_t& r) {
    q = n / d;
    r = n % d;
    if (r < 0) {
        r += d;
        --q;
    }
}

} // namespace

// The reduction is done on unsigned magnitudes so that INT64_MIN in either argument is handled:
// a result whose reduced numerator or denominator still needs 2^63 is refused.
Rational::Rational(std::int64_t num, std::int64_t den) {
    if (den == 0) {
        throw std::invalid_argument("Rational: zero denominator");
    }
    std::uint64_t n = magnitude(num);
    std::uint64_t d = magnitude(den);
    const std::uint64_t g = std::gcd(n, d);
    n /= g;
    d /= g;
    if (n > static_cast<std::uint64_t>(kMax) || d > static_cast<std::uint64_t>(kMax)) overflow();
    const auto signedN = static_cast<std::int64_t>(n);
    num_ = (num < 0) != (den < 0) ? -signedN : signedN;
    den_ = static_cast<std::int64_t>(d);
}

std::string Rational::toString() const {
    return std::to_string(num_) + "/" + std::to_string(den_);
}

// C++ division truncates towards zero, so negative values with a remainder step down by one.
std::int64_t Rational::floor() const {
    std::int64_t q = 0;
    std::int64_t r = 0;
    floorDivMod(num_, den_, q, r);
    return q;
}

// On the magnitude m/d: the quotient, plus one when the remainder r is at least half of d
// (r >= d - r, which cannot overflow as 2 * r could); the sign is put back afterwards.
std::int64_t Rational::round() const {
    const std::int64_t m = num_ < 0 ? -num_ : num_;
    const std::int64_t r = m % den_;
    const std::int64_t rounded = m / den_ + (r >= den_ - r ? 1 : 0);
    return num_ < 0 ? -rounded : rounded;
}

// Over the lcm of the denominators: a.den / g * b.den, with g their gcd.
Rational operator+(const Rational& a, const Rational& b) {
    const std::int64_t g = gcdWith(a.den_, b.den_);
    const std::int64_t num = checkedAdd(checkedMul(a.num_, b.den_ / g), checkedMul(b.num_, a.den_ / g));
    return {num, checkedMul(a.den_ / g, b.den_)};
}

Rational operator-(const Rational& a, const Rational& b) {
    return a + (-b);
}

// Each numerator is reduced against the other's denominator first, so the result is already in
// lowest terms and overflows only when it really does not fit.
Rational operator*(const Rational& a, const Rational& b) {
    const std::int64_t g1 = gcdWith(a.num_, b.den_);
    const std::int64_t g2 = gcdWith(b.num_, a.den_);
    return {checkedMul(a.num_ / g1, b.num_ / g2), checkedMul(a.den_ / g2, b.den_ / g1)};
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.num_ == 0) {
        throw std::domain_error("Rational: division by zero");
    }
    return a * Rational(b.den_, b.num_);
}

// Without cross-multiplying (which can overflow): compare the integer parts, then the fractional
// parts r1/d1 and r2/d2, which are ordered as d2/r2 and d1/r1 are. That is a continued-fraction
// expansion of both values; it ends because the denominators keep shrinking.
std::strong_ordering operator<=>(const Rational& a, const Rational& b) {
    std::int64_t n1 = a.num_;
    std::int64_t d1 = a.den_;
    std::int64_t n2 = b.num_;
    std::int64_t d2 = b.den_;
    for (;;) {
        std::int64_t q1 = 0;
        std::int64_t r1 = 0;
        std::int64_t q2 = 0;
        std::int64_t r2 = 0;
        floorDivMod(n1, d1, q1, r1);
        floorDivMod(n2, d2, q2, r2);
        if (q1 != q2) return q1 <=> q2;
        // A zero fraction is the smaller one.
        if (r1 == 0 || r2 == 0) return (r1 != 0) <=> (r2 != 0);
        // r1/d1 <=> r2/d2 is d2/r2 <=> d1/r1.
        const std::int64_t nextN1 = d2;
        const std::int64_t nextD1 = r2;
        n2 = d1;
        d2 = r1;
        n1 = nextN1;
        d1 = nextD1;
    }
}

} // namespace lumenis
