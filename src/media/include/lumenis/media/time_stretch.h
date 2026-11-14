#pragma once

#include <vector>

// The time-stretch engine behind AudioSuite's Pitch Shift, Time Compression Expansion and Time Shift (Editing Guide
// p.850-860; ROADMAP 11.2, docs/spec/time-stretch.md). Our own, the owner's choice of method: WSOLA, waveform-similarity
// overlap-add. The sound is cut into overlapping windows; each output window is taken from near where the stretch puts
// it in the input, moved within a search range to where it best continues the window before it (the highest
// cross-correlation), and the windows are added up with a Hann crossfade. The waveform's periods line up across every
// join, so the pitch stays and the length changes. Pitch changes are a stretch and a resample.
//
// Every function takes and returns channels of the same length at `sampleRate`. Channels are stretched together, the
// joins found on their sum, so a stereo pair keeps its image ("processing the two sides of the stereo signal relative to
// each other", p.856).
namespace lumenis::media {

struct StretchOptions {
    int sampleRate = 48000;
    // The crossfade, half the window, in milliseconds; 0 for Auto, set from the change in length ("crossfade times are
    // set automatically according to the percentage of change in length", p.857): longest for small changes.
    double crossfadeMs = 0;
    // The lowest pitch the joins are found for (40 to 1000 Hz, p.857): the search reaches one of its periods each way.
    double minPitchHz = 40;
    // Sound (1) against rhythm (0), p.857: how much of the search range is used. Towards rhythm each window stays
    // nearer its place, keeping the timing; towards sound it moves further for a smoother join.
    double accuracy = 0.5;
};

// The channels at `ratio` times their length (0.25 to 4.0, p.857), their pitch kept. A ratio of 1 copies them.
std::vector<std::vector<float>> timeStretch(const std::vector<std::vector<float>>& channels, double ratio,
                                            const StretchOptions& options = {});

// Rhythmic material (Time Shift's Rhythmic mode, p.858-860): the sound cut at its transients (a rise in level over
// `thresholdDb`, 0 to -40 dB below the loudest), each piece moved to its stretched start and kept as it is, so drum hits
// are neither smeared nor doubled. Where a piece is longer than its place it fades out over `decayMs` before the next
// begins; where it is shorter, silence follows. Without transients it is timeStretch().
std::vector<std::vector<float>> rhythmicStretch(const std::vector<std::vector<float>>& channels, double ratio,
                                                double thresholdDb, double decayMs, const StretchOptions& options = {});

// The channels played `factor` times as fast, as tape is varispeeded: pitch up by `factor`, length 1/`factor` (p.851).
std::vector<std::vector<float>> varispeed(const std::vector<std::vector<float>>& channels, double factor, int sampleRate = 48000);

// Pitch moved by `semitones` (an octave up or down, p.850; two in Time Shift, p.860). With `timeCorrection` the length
// stays (a stretch by the pitch factor, then varispeed); without, it is varispeed (p.851). `lengthRatio` stretches the
// length besides (Time Shift's Time and Pitch together).
std::vector<std::vector<float>> pitchShift(const std::vector<std::vector<float>>& channels, double semitones,
                                           bool timeCorrection, const StretchOptions& options = {},
                                           double lengthRatio = 1.0);

} // namespace lumenis::media
