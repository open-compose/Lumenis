// The calibration tone (lumenis/media/tone.h): a sine, white noise or a logarithmic sweep at a peak
// level, worked out from the sample number alone.
#include "lumenis/media/tone.h"

#include <QObject>

#include <algorithm>

#include <cmath>
#include <cstdlib>
#include <numbers>

namespace lumenis::media {

namespace {

// A uniform value in [-1, 1) from a sample number (SplitMix64): the same noise every time.
float noiseAt(std::int64_t n) {
    auto z = static_cast<std::uint64_t>(n) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) / 4503599627370496.0 - 1.0);
}

} // namespace

void renderTone(const Tone& tone, std::int64_t at, int count, int rate, float* out) {
    if (count <= 0 || rate <= 0) return;
    const auto amplitude = static_cast<float>(std::pow(10.0, std::min(tone.level, 0) / 20.0));
    if (tone.frequency == kToneNoise) {
        for (int i = 0; i < count; ++i) out[i] = amplitude * noiseAt(at + i);
        return;
    }
    if (tone.frequency == kToneSweep) {
        // f(t) = f0 * k^(t/T): its phase is 2 pi f0 T (k^(t/T) - 1) / ln k.
        const auto period = static_cast<std::int64_t>(kSweepSeconds * rate);
        const double lnK = std::log(kSweepTo / kSweepFrom);
        for (int i = 0; i < count; ++i) {
            const double t = static_cast<double>((at + i) % period) / rate;
            const double phase = 2.0 * std::numbers::pi * kSweepFrom * kSweepSeconds *
                                 (std::exp(lnK * t / kSweepSeconds) - 1.0) / lnK;
            out[i] = amplitude * static_cast<float>(std::sin(phase));
        }
        return;
    }
    // A sine: the phase from (n * f) mod rate, in whole numbers, stays exact however long it plays.
    const std::int64_t f = std::abs(tone.frequency);
    for (int i = 0; i < count; ++i) {
        const std::int64_t cycle = ((at + i) % rate) * f % rate;
        out[i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(cycle) / rate));
    }
}

QString toneClipName(const Tone& tone, int tracks) {
    QString name;
    if (tone.frequency == kToneNoise) {
        name = QObject::tr("Noise %1 dB").arg(tone.level);
    } else if (tone.frequency == kToneSweep) {
        name = QObject::tr("Sweep %1 dB").arg(tone.level);
    } else {
        name = QObject::tr("Tone %1 Hz %2 dB").arg(tone.frequency).arg(tone.level);
    }
    if (tracks > 1) name += QObject::tr(" %1 Tracks").arg(tracks);
    return name;
}

} // namespace lumenis::media
