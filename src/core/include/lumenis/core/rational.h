#pragma once

#include <cstdint>
#include <compare>
#include <string>

namespace lumenis {

// An exact rational number, always stored in lowest terms with a positive
// denominator. Used for edit rates (30000/1001) and positions in time so that
// no floating-point drift ever enters the edit model.
class Rational {
public:
    constexpr Rational() = default;
    // Reduces num/den to lowest terms and moves the sign to the numerator. Throws
    // std::invalid_argument for a zero denominator, and std::overflow_error when the reduced
    // numerator or denominator would be INT64_MIN (2^63 has no positive int64), so a Rational
    // never holds INT64_MIN and can always be negated.
    Rational(std::int64_t num, std::int64_t den = 1);

    // Numerator (carries the sign) and denominator (always > 0), in lowest terms.
    std::int64_t num() const { return num_; }
    std::int64_t den() const { return den_; }

    // For display and for talking to floating-point APIs only; never feed it back into the model.
    double toDouble() const { return static_cast<double>(num_) / static_cast<double>(den_); }
    std::string toString() const; // "30000/1001"

    // Rounds towards negative infinity. Neither rounding can overflow.
    std::int64_t floor() const;
    // Rounds half away from zero.
    std::int64_t round() const;

    Rational operator-() const { return {-num_, den_}; }
    // Exact arithmetic on 64-bit integers. Products are cross-reduced by gcd first, so a result
    // that fits in lowest terms is always returned; + and - work over the lcm of the denominators,
    // and throw only if that lcm or the numerator over it does not fit. A result that does not fit
    // throws std::overflow_error (never a wrapped value). Division by zero throws
    // std::domain_error.
    friend Rational operator+(const Rational& a, const Rational& b);
    friend Rational operator-(const Rational& a, const Rational& b);
    friend Rational operator*(const Rational& a, const Rational& b);
    friend Rational operator/(const Rational& a, const Rational& b);

    // Member-wise equality is exact because values are always kept in lowest terms.
    friend bool operator==(const Rational& a, const Rational& b) = default;
    // Exact for all values; never overflows or throws.
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b);

private:
    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

} // namespace lumenis
