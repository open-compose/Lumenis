// ACES encodings and the published camera curves (8.5e4b1); see the header. The constants are the published ones: the
// Academy's specifications for ACEScct, ACEScc and ACESproxy, ARRI's LogC3 (EI 800), Sony's S-Log3 and Panasonic's V-Log
// formulas, and each gamut's chromaticities.
#include "lumenis/media/aces.h"

#include <algorithm>
#include <cmath>

namespace lumenis::media::aces {

namespace {

using Vec = std::array<double, 3>;

Vec mul(const Matrix& m, const Vec& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Matrix mul(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
        }
    }
    return r;
}

Matrix inverse(const Matrix& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double k = std::abs(det) > 1e-15 ? 1.0 / det : 0.0;
    return {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k,
            (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k,
            (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k};
}

Vec xyz(double x, double y) { return {x / y, 1.0, (1.0 - x - y) / y}; }

Matrix rgbToXyz(const Primaries& p) {
    const Vec r = xyz(p[0], p[1]), g = xyz(p[2], p[3]), b = xyz(p[4], p[5]), w = xyz(p[6], p[7]);
    const Matrix m{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const Vec s = mul(inverse(m), w);
    return {m[0] * s[0], m[1] * s[1], m[2] * s[2], m[3] * s[0], m[4] * s[1], m[5] * s[2], m[6] * s[0], m[7] * s[1], m[8] * s[2]};
}

Matrix bradford(const Vec& from, const Vec& to) {
    const Matrix m{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const Vec a = mul(m, from), b = mul(m, to);
    const Matrix scale{b[0] / a[0], 0, 0, 0, b[1] / a[1], 0, 0, 0, b[2] / a[2]};
    return mul(inverse(m), mul(scale, m));
}

constexpr double kD65x = 0.3127, kD65y = 0.3290;
constexpr double kAcesX = 0.32168, kAcesY = 0.33767;

} // namespace

Primaries ap0() { return {0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.0770, kAcesX, kAcesY}; }
Primaries ap1() { return {0.713, 0.293, 0.165, 0.830, 0.128, 0.044, kAcesX, kAcesY}; }
Primaries rec709() { return {0.640, 0.330, 0.300, 0.600, 0.150, 0.060, kD65x, kD65y}; }
Primaries rec2020() { return {0.708, 0.292, 0.170, 0.797, 0.131, 0.046, kD65x, kD65y}; }
Primaries p3d65() { return {0.680, 0.320, 0.265, 0.690, 0.150, 0.060, kD65x, kD65y}; }
Primaries alexaWideGamut() { return {0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020, kD65x, kD65y}; }
Primaries sGamut3() { return {0.730, 0.280, 0.140, 0.855, 0.100, -0.050, kD65x, kD65y}; }
Primaries sGamut3Cine() { return {0.766, 0.275, 0.225, 0.800, 0.089, -0.087, kD65x, kD65y}; }
Primaries vGamut() { return {0.730, 0.280, 0.165, 0.840, 0.100, -0.030, kD65x, kD65y}; }

Matrix conversion(const Primaries& from, const Primaries& to) {
    const Vec wf = xyz(from[6], from[7]), wt = xyz(to[6], to[7]);
    const bool sameWhite = std::abs(from[6] - to[6]) < 1e-6 && std::abs(from[7] - to[7]) < 1e-6;
    const Matrix adapt = sameWhite ? Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1} : bradford(wf, wt);
    return mul(inverse(rgbToXyz(to)), mul(adapt, rgbToXyz(from)));
}

std::array<double, 3> apply(const Matrix& m, const std::array<double, 3>& rgb) { return mul(m, rgb); }

double linearToAcescct(double linear) {
    if (linear <= 0.0078125) return 10.5402377416545 * linear + 0.0729055341958355;
    return (std::log2(linear) + 9.72) / 17.52;
}

double acescctToLinear(double cct) {
    if (cct <= 0.155251141552511) return (cct - 0.0729055341958355) / 10.5402377416545;
    return std::min(std::exp2(cct * 17.52 - 9.72), 65504.0);
}

double linearToAcescc(double linear) {
    if (linear <= 0.0) return (std::log2(std::exp2(-16.0)) + 9.72) / 17.52;
    if (linear < std::exp2(-15.0)) return (std::log2(std::exp2(-16.0) + linear * 0.5) + 9.72) / 17.52;
    return (std::log2(linear) + 9.72) / 17.52;
}

double acesccToLinear(double cc) {
    if (cc < (9.72 - 15.0) / 17.52) return (std::exp2(cc * 17.52 - 9.72) - std::exp2(-16.0)) * 2.0;
    return std::min(std::exp2(cc * 17.52 - 9.72), 65504.0);
}

double linearToAcesproxy(double linear) {
    if (linear <= std::exp2(-9.72)) return 64.0 / 1023.0;
    const double code = (std::log2(linear) + 2.5) * 50.0 + 425.0;
    return std::clamp(std::round(code), 64.0, 940.0) / 1023.0;
}

double logC3ToLinear(double t) {
    // ALEXA LogC3, EI 800 (ARRI's published constants).
    const double cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190, d = 0.385537, e = 5.367655, f = 0.092809;
    return t > e * cut + f ? (std::pow(10.0, (t - d) / c) - b) / a : (t - f) / e;
}

double sLog3ToLinear(double v) {
    const double x = v * 1023.0;
    if (x >= 171.2102946929) return std::pow(10.0, (x - 420.0) / 261.5) * (0.18 + 0.01) - 0.01;
    return (x - 95.0) * 0.01125 / (171.2102946929 - 95.0);
}

double vLogToLinear(double v) {
    const double b = 0.00873, c = 0.241514, d = 0.598206;
    return v < 0.181 ? (v - 0.125) / 5.6 : std::pow(10.0, (v - d) / c) - b;
}

std::array<double, 3> outputToneMap(const std::array<double, 3>& linearAp1) {
    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const double x = std::max(0.0, linearAp1[i]) * 0.6;
        out[i] = std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }
    return out;
}

} // namespace lumenis::media::aces
