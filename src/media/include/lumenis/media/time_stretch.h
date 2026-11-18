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

// Formant-correct pitch shifting (Time Shift's Monophonic mode, p.859-860: "In Monophonic mode, pitch shift can also be
// formant-correct"): pitch-synchronous overlap-add. The sound's period is found every 5 ms (autocorrelation, from
// `options.minPitchHz` up to 1000 Hz; unvoiced sound takes 10 ms); grains two periods long, Hann-windowed, are cut a
// period apart and laid down closer together to raise the pitch or further apart to lower it, each from where
// `lengthRatio` puts it in the input. A grain keeps its own shape, so the formants, the resonances that make a voice
// sound like itself, stay where they were. `formantSemitones` (-24 to 24, the Formant control) squeezes or widens each
// grain in time, which moves the formants by that much without moving the pitch.
std::vector<std::vector<float>> formantPitchShift(const std::vector<std::vector<float>>& channels, double semitones,
                                                  double formantSemitones, double lengthRatio,
                                                  const StretchOptions& options = {});

// Time Shift's Follow (Polyphonic mode, p.860: "an envelope follower that simulates the original acoustics of the audio
// being stretched"): `stretched`, made from `original` at `ratio` times its length, is given the original's level over
// time, stretched with it. Levels are measured over 10 ms every 5 ms on the channels' sum; the gain at each point is the
// original's level there over the stretched one's (at most 4 times, 12 dB), smoothed between the points, so the decays
// and attacks keep their shape instead of the overlapping windows' smear.
std::vector<std::vector<float>> followEnvelope(const std::vector<std::vector<float>>& original,
                                               std::vector<std::vector<float>> stretched, double ratio,
                                               int sampleRate = 48000);

} // namespace lumenis::media
