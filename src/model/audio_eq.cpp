#include "lumenis/model/audio_eq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

// Audio EQ (audio_eq.h). The filter formulas are Robert Bristow-Johnson's "Cookbook formulae for
// audio EQ biquad filter coefficients", which are published for anyone to use.
namespace lumenis::model {

namespace {

template <std::size_t N>
int nearest(const std::array<int, N>& choices, int value) {
    return *std::min_element(choices.begin(), choices.end(),
                             [value](int a, int b) { return std::abs(a - value) < std::abs(b - value); });
}

double clampGainDb(double db) {
    return std::round(std::clamp(db, kEqMin, kEqMax) * 10) / 10;
}

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2) {
    return Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// Shelves with a slope of 1: alpha = sin(w0) / 2 * sqrt(2).
Biquad shelf(bool low, double db, double frequency, double rate) {
    const double a = std::pow(10.0, db / 40.0);
    const double w0 = 2 * std::numbers::pi * frequency / rate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / 2 * std::numbers::sqrt2;
    const double r = 2 * std::sqrt(a) * alpha;
    if (low) {
        return normalised(a * ((a + 1) - (a - 1) * c + r), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - r),
                          (a + 1) + (a - 1) * c + r, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - r);
    }
    return normalised(a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - r),
                      (a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - r);
}

// A peaking filter `octaves` wide.
Biquad peak(double db, double frequency, double octaves, double rate) {
    const double a = std::pow(10.0, db / 40.0);
    const double w0 = 2 * std::numbers::pi * frequency / rate;
    const double alpha = std::sin(w0) * std::sinh(std::numbers::ln2 / 2 * octaves * w0 / std::sin(w0));
    const double c = std::cos(w0);
    return normalised(1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
}

} // namespace

AudioEq clampEq(const AudioEq& eq) {
    AudioEq e = eq;
    e.low = clampGainDb(e.low);
    e.mid = clampGainDb(e.mid);
    e.high = clampGainDb(e.high);
    e.lowTurnover = nearest(kLowTurnovers, e.lowTurnover);
    e.highTurnover = nearest(kHighTurnovers, e.highTurnover);
    e.midFrequency = std::clamp(e.midFrequency, kEqRangeMin, kEqRangeMax);
    return e;
}

bool isFlat(const AudioEq& eq) {
    return eq.low == 0 && eq.mid == 0 && eq.high == 0;
}

std::array<Biquad, 3> eqFilters(const AudioEq& eq, double sampleRate) {
    // A band at 0 dB is no filter at all, so a flat EQ passes the sound unchanged.
    auto band = [](double db, auto make) { return db == 0 ? Biquad{} : make(); };
    return {band(eq.low, [&] { return shelf(true, eq.low, eq.lowTurnover, sampleRate); }),
            band(eq.mid, [&] { return peak(eq.mid, eq.midFrequency, eq.wideMid ? 2.0 : 0.25, sampleRate); }),
            band(eq.high, [&] { return shelf(false, eq.high, std::min<double>(eq.highTurnover, sampleRate * 0.45), sampleRate); })};
}

double eqResponse(const AudioEq& eq, double frequency, double sampleRate) {
    const std::complex<double> z1 = std::polar(1.0, -2 * std::numbers::pi * frequency / sampleRate);
    const std::complex<double> z2 = z1 * z1;
    double db = 0;
    for (const Biquad& f : eqFilters(eq, sampleRate)) {
        const std::complex<double> h = (f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2);
        db += 20 * std::log10(std::max(std::abs(h), 1e-12));
    }
    return db;
}

int setEq(Sequence& sequence, const QString& name, std::int64_t from, std::int64_t to, const std::optional<AudioEq>& eq) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > from && at < to) {
            const std::optional<AudioEq> next = eq ? std::optional(clampEq(*eq)) : std::nullopt;
            if (s.eq != next) {
                s.eq = next;
                ++changed;
            }
        }
        at = end;
    }
    return changed;
}

} // namespace lumenis::model
