#pragma once

#include "lumenis/model/bin.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

// Syncing clips by their sound (Editing Guide p.1124, Group Clips' Waveform Analysis; p.1126, Waveform Sync;
// docs/spec/multicamera.md). Ours: the loudness of each sound in 1 ms steps, less its average, is cross-correlated
// (through an FFT) and the best match is the lag. Only the clips' first audio track is used ("only the first one is
// used for syncing", p.1126). MainWindow gives the results to model::groupClips and Timeline::waveformSync.
namespace lumenis::media {

// How much later `b` is than `a`, in seconds: what sounds at a's time t sounds at b's time t + lag. Both are mono
// samples at `rateA` and `rateB`. Nothing when either is silent or no lag of at most `maxLag` seconds (0: any) matches.
std::optional<double> soundLag(const std::vector<float>& a, int rateA, const std::vector<float>& b, int rateB,
                               double maxLag = 0.0);

// A clip's first audio track from its first frame as mono samples, at most `seconds` of it; empty samples when the
// clip has no sound that can be read (with the reason).
struct ClipSound {
    std::vector<float> samples;
    int rate = 0;
};
ClipSound clipSound(const model::BinItem& clip, double seconds = 3600.0, QString* error = nullptr);

// The frames between two clips' sounds: frame f of `a` sounds with frame f + offset of `b`, at a's edit rate.
// The first 20 minutes of each are compared. Nothing (with the reason) when either has no sound or they do not match.
std::optional<std::int64_t> waveformOffset(const model::BinItem& a, const model::BinItem& b, QString* error = nullptr);

} // namespace lumenis::media
