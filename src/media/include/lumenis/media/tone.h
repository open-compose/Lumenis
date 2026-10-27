#pragma once

#include <QString>

#include <cstdint>

// The Audio Tool's calibration tone (Editing Guide p.157-159, p.920; docs/spec/tone.md): what Play
// Calibration Tone plays (an AudioPiece with a tone, lumenis/media/playback.h) and what Create Tone
// Media writes as managed media.
namespace lumenis::media {

// A frequency of 0 makes random noise, -777 a sweep (p.159).
inline constexpr int kToneNoise = 0;
inline constexpr int kToneSweep = -777;
// The sweep (ours: the guide gives no range or length): 20 Hz to 20 kHz, rising logarithmically over
// ten seconds, then again from 20 Hz.
inline constexpr double kSweepFrom = 20.0;
inline constexpr double kSweepTo = 20000.0;
inline constexpr double kSweepSeconds = 10.0;

struct Tone {
    int level = -20;       // dB on the digital scale: the peak of each sample, 0 dB = full scale
    int frequency = 1000;  // Hz, or kToneNoise or kToneSweep
    bool operator==(const Tone&) const = default;
};

// Fills `out` with `count` mono samples at `rate`, from sample `at` of an endless tone. The same
// sample number always gives the same value, noise included, so pieces can be rendered in any order.
void renderTone(const Tone& tone, std::int64_t at, int count, int rate, float* out);

// The default name of a tone master clip, from its options (p.159: "The default name reflects the
// options you selected"; the wording is ours): "Tone 1000 Hz -20 dB", "Noise -20 dB", "Sweep -20 dB",
// with the number of tracks after it when there are more than one ("Tone 1000 Hz -20 dB 2 Tracks").
QString toneClipName(const Tone& tone, int tracks);

} // namespace lumenis::media
