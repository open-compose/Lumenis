#pragma once

#include "lumenis/model/sequence.h"

#include <array>
#include <optional>

// Audio EQ (Editing Guide p.736-745; docs/spec/audio-eq.md): the limits of the three bands, the
// filters that play them, their frequency response for the Audio EQ tool's graph, and setting
// and removing the effect on segments. Playback and Audio Mixdown run the filters
// (media::AudioMix, src/media/playback.cpp); the tool is src/ui/audio_eq.cpp.
namespace lumenis::model {

inline constexpr double kEqMin = -20.0; // "The range is from +15 dB to -20 dB" (p.739)
inline constexpr double kEqMax = 15.0;
inline constexpr std::array<int, 4> kLowTurnovers{50, 80, 120, 240};       // p.738
inline constexpr std::array<int, 4> kHighTurnovers{6000, 8000, 12000, 15000};
inline constexpr double kEqRangeMin = 50.0; // the EQ Range slider's ends (picture on p.737)
inline constexpr double kEqRangeMax = 15000.0;

// The EQ with each value put in range: gains to kEqMin..kEqMax, rounded to 0.1 dB; turnovers to
// the nearest of their four; the midrange to kEqRangeMin..kEqRangeMax.
AudioEq clampEq(const AudioEq& eq);
// An EQ that changes nothing: every band at 0 dB.
bool isFlat(const AudioEq& eq);

// One second-order filter section, normalised (a0 = 1): y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
// The EQ's three sections at `sampleRate`: a low shelf turning over at lowTurnover, a peaking
// filter at midFrequency (1/4 or 2 octaves wide) and a high shelf at highTurnover. They are the
// widely published "Audio EQ Cookbook" second-order sections (shelf slope 1; ours: the guide
// gives the controls, not the filter shapes).
std::array<Biquad, 3> eqFilters(const AudioEq& eq, double sampleRate);
// The EQ's gain in dB at `frequency` Hz (the Audio EQ tool's graph, p.738).
double eqResponse(const AudioEq& eq, double frequency, double sampleRate = 48000);

// Set EQ - In/Out (p.739-741) on audio track `track`: `eq` on every whole segment that
// overlaps record frames [from, to); without one, their EQ is removed. "Apply Audio EQ to entire
// segments only" (p.738). Filler and a locked track are left alone. Returns how many segments
// changed.
int setEq(Sequence& sequence, const QString& track, std::int64_t from, std::int64_t to,
          const std::optional<AudioEq>& eq);

} // namespace lumenis::model
