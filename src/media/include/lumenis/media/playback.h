#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Playback (ROADMAP Phase 2.3): mixing the audio of what a monitor shows,
// and playing it through the system's audio output, whose progress is the
// clock the picture follows.
namespace lumenis::media {

class AudioReader;

// The output's sample rate. Files at other rates are resampled.
inline constexpr int kPlaybackRate = 48000;

// One channel of one file, placed in what plays.
struct AudioPiece {
    QString path;
    int stream = -1;               // the file's audio stream
    int channel = 0;               // within that stream
    std::int64_t sourceStart = 0;  // where in the file it starts, in output samples
    std::int64_t at = 0;           // where it plays, in output samples
    std::int64_t length = 0;       // in output samples
    float left = 1.0f;             // gains into the stereo output
    float right = 1.0f;
};

// Equal-power gains for a pan from -100 (left) to 100 (right), times a
// gain in dB.
void panGains(int pan, double gainDb, float* left, float* right);

// Frames at an edit rate as output samples, and back.
std::int64_t framesToSamples(std::int64_t frames, const Rational& rate);
double samplesToFrames(std::int64_t samples, const Rational& rate);

// What a monitor plays (Editing Guide p.395, p.627), in output samples from
// the item's start:
// - a clip: each audio channel is a track; mono tracks are panned odd to
//   the left, even to the right (p.627);
// - a sequence: the segments of its monitored audio tracks, each with its
//   clip gain and pan (p.702); a centred pan follows its track's side.
// Clips without media, unlinked or offline, are silent.
using ClipLookup = std::function<std::optional<model::BinItem>(const QString& binId, const QString& clipId)>;
std::vector<AudioPiece> audioPieces(const model::BinItem& item, const ClipLookup& clips,
                                    const std::function<bool(const QString& track)>& monitored = {});

// Mixes pieces into interleaved stereo. Reads the files itself, so one mix
// serves one thread at a time.
class AudioMix {
public:
    explicit AudioMix(std::vector<AudioPiece> pieces = {});
    ~AudioMix();
    AudioMix(const AudioMix&) = delete;
    AudioMix& operator=(const AudioMix&) = delete;

    const std::vector<AudioPiece>& pieces() const { return pieces_; }
    // `count` stereo samples from output sample `start` into `out`
    // (2 * count floats), silence where nothing plays.
    void render(std::int64_t start, int count, float* out);

private:
    AudioReader* reader(const AudioPiece& piece);

    std::vector<AudioPiece> pieces_;
    std::map<QString, std::unique_ptr<AudioReader>> readers_;
};

// Plays a mix through the system's audio output (miniaudio). A thread
// renders ahead into a buffer the output drains; what the output has taken
// is the clock. Without an output device nothing plays and the clock stays
// at zero: the caller falls back to its own.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Starts playing from output sample `start`. False if no output could be
    // opened. Without `device` nothing is opened and pull() takes the
    // samples (tests).
    bool start(std::shared_ptr<AudioMix> mix, std::int64_t start, bool device = true);
    void stop();
    bool isPlaying() const { return playing_; }
    // Output samples the device has taken since start.
    std::int64_t played() const { return played_.load(); }
    // The audio clock: the sample being heard now. Devices take samples in
    // bursts and hold them a while (Core Audio especially); the clock runs
    // smoothly between bursts and leaves out what the device still holds.
    std::int64_t clock() const;
    // The output in use ("Null" when there is no sound card), for Help >
    // About and the Console.
    QString deviceName() const;

    // Fills `out` (2 * count floats) from the buffer, as the device does
    // (tests).
    int pull(float* out, int count);

private:
    struct Device;
    void feed();

    std::unique_ptr<Device> device_;
    std::shared_ptr<AudioMix> mix_;
    std::thread feeder_;
    std::atomic<bool> running_{false};
    bool playing_ = false;
    std::atomic<std::int64_t> played_{0};
    std::atomic<std::int64_t> pulledAt_{0}; // steady-clock ns of the last pull
    std::int64_t latency_ = 0;             // samples the device holds
    std::int64_t next_ = 0; // the next sample to render

    // A single-producer, single-consumer ring of stereo samples.
    std::vector<float> ring_;
    std::atomic<std::size_t> readAt_{0};
    std::atomic<std::size_t> writeAt_{0};
};

} // namespace lumenis::media
