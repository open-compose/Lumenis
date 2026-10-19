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
    // std::invalid_argument for a zero denominator.
    Rational(std::int64_t num, std::int64_t den = 1);

    // Numerator (carries the sign) and denominator (always > 0), in lowest terms.
    std::int64_t num() const { return num_; }
    std::int64_t den() const { return den_; }

    // For display and for talking to floating-point APIs only; never feed it back into the model.
    double toDouble() const { return static_cast<double>(num_) / static_cast<double>(den_); }
    std::string toString() const; // "30000/1001"

    // Rounds towards negative infinity.
    std::int64_t floor() const;
    // Rounds half away from zero.
    std::int64_t round() const;

    Rational operator-() const { return {-num_, den_}; }
    // Exact arithmetic on 64-bit integers; intermediate products are not checked for overflow, so
    // keep operands to edit-rate sized values. Division by zero throws std::domain_error.
    friend Rational operator+(const Rational& a, const Rational& b);
    friend Rational operator-(const Rational& a, const Rational& b);
    friend Rational operator*(const Rational& a, const Rational& b);
    friend Rational operator/(const Rational& a, const Rational& b);

    // Member-wise equality is exact because values are always kept in lowest terms.
    friend bool operator==(const Rational& a, const Rational& b) = default;
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b);

private:
    std::int64_t num_ = 0;
    std::int64_t den_ = 1;
};

} // namespace lumenis
