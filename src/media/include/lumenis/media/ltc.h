#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/bin.h"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

// Longitudinal timecode (LTC, SMPTE ST 12-1) in audio: our own decoder for Clip > Read Audio
// Timecode (Editing Guide p.668-669; docs/spec/clip-audio.md), and an encoder for tests and test
// media. Each frame is 80 bits sent least significant first in biphase mark code: every bit starts
// with a transition, and a 1 has another in its middle. Bits 64-79 are the sync word; the 32 user
// bits are in eight groups of four between the time fields.
namespace lumenis::media::ltc {

struct Frame {
    std::int64_t sample = 0; // where its first bit starts, in the samples fed
    TimecodeFields time;
    bool dropFrame = false;
    std::uint32_t userBits = 0; // group 1 in the low four bits ... group 8 in the high four
    // The user bits read as timecode (groups 1-8: frame units and tens, seconds, minutes, hours, as
    // the time fields are; ours, since the guide does not say how they hold it).
    TimecodeFields userTime() const;
};

// Decodes LTC from mono samples fed in order, at `sampleRate`, for `fps` frames a second (the
// nominal rate: 24, 25, 30). The signal may have either polarity and any level; frames whose
// sync word is not found are skipped. A frame is read when the transition after its last bit comes,
// so the last frame of a recording is not.
class Decoder {
public:
    Decoder(int sampleRate, double fps);
    void feed(const float* samples, std::size_t count);
    const std::vector<Frame>& frames() const { return frames_; }

private:
    void transition(double at);
    void bit(int value, double start);

    double bitLength_;
    std::int64_t fed_ = 0;
    int sign_ = 0;
    float peak_ = 0;
    double last_ = -1; // the previous transition
    double pendingStart_ = -1; // the first half of a 1, waiting for its second
    std::uint64_t bits_ = 0; // the last 64 data bits, newest highest
    std::uint32_t sync_ = 0; // the last 16 bits, newest lowest
    std::vector<double> starts_; // the last 80 bits' starts
    std::vector<Frame> frames_;
};

// `count` frames of LTC from `start` at `sampleRate`, level ±0.5, each frame's user bits `userBits`.
std::vector<float> encode(const Timecode& start, int count, int sampleRate, std::uint32_t userBits = 0);
// The user bits that hold `time` as userTime() reads them.
std::uint32_t userBitsFor(const TimecodeFields& time);

// Clip > Read Audio Timecode for one clip (p.668-669): the timecode of the clip's first frame, read
// from the LTC on its audio track `track` (0 for A1). With `userBits` the user bits' timecode. With
// `fill` ("Fill Undecodeable Frames") the first frame that decodes is worked back to the clip's
// start; without it the clip's first frame must decode. Nothing, with the reason in *error, for a
// clip without that track's media or without LTC there.
std::optional<Timecode> readClipTimecode(const model::BinItem& clip, int track, bool userBits, bool fill,
                                         QString* error = nullptr);

} // namespace lumenis::media::ltc
