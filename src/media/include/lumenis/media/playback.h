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
    // The piece's own gain (clip gain), before the pan: what a mono mixdown uses.
    float gain = 1.0f;
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
// Clips without media, unlinked or offline, are silent. Without
// `alternatingPan` (Audio settings, "Default Pan for mono tracks":
// Centered, p.1147) mono tracks play in the middle.
using ClipLookup = std::function<std::optional<model::BinItem>(const QString& binId, const QString& clipId)>;
std::vector<AudioPiece> audioPieces(const model::BinItem& item, const ClipLookup& clips,
                                    const std::function<bool(const QString& track)>& monitored = {},
                                    bool alternatingPan = true);

// Mixes pieces into interleaved stereo. Reads the files itself, so one mix
// serves one thread at a time.
// Files are opened on first use and kept open for the mix's life.
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
    // The reader of a piece's file, stream and channel; null if it cannot be opened.
    AudioReader* reader(const AudioPiece& piece);

    std::vector<AudioPiece> pieces_;
    std::map<QString, std::unique_ptr<AudioReader>> readers_;
};

// Plays a mix through the system's audio output (miniaudio). A thread
// renders ahead into a buffer the output drains; what the output has taken
// is the clock. Without an output device nothing plays and the clock stays
// at zero: the caller falls back to its own.
// Use it from one thread (the UI's): start() and stop() start and join the feeder thread, and the
// device's callback calls pull() on its own thread.
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    struct PlayOptions {
        // Smooth audio scrub (p.675): up to three times normal speed, either
        // way. Faster speeds take every n-th sample (averaged), so the pitch
        // rises as it did on tape; reverse plays backwards.
        int speed = 1;
        // Digital audio scrub (p.676): this many output samples, then silence.
        std::int64_t limit = -1;
        // Without a device nothing is opened and pull() takes the samples
        // (tests).
        bool device = true;
    };
    // Starts playing from output sample `start`. False if no output could be
    // opened.
    bool start(std::shared_ptr<AudioMix> mix, std::int64_t start, const PlayOptions& options);
    bool start(std::shared_ptr<AudioMix> mix, std::int64_t from) { return start(std::move(mix), from, PlayOptions{}); }
    // The speed playing, -3..3 (negative: backwards).
    int speed() const { return speed_; }
    // Stops the device and the feeder; nothing plays until the next start().
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
    // Renders ahead into the ring on the feeder thread.
    void feed();

    std::unique_ptr<Device> device_;
    // What plays; released by stop().
    std::shared_ptr<AudioMix> mix_;
    std::thread feeder_;
    // Tells the feeder thread to keep going.
    std::atomic<bool> running_{false};
    bool playing_ = false;
    std::atomic<std::int64_t> played_{0};
    std::atomic<std::int64_t> pulledAt_{0}; // steady-clock ns of the last pull
    std::int64_t latency_ = 0;             // samples the device holds
    std::int64_t next_ = 0; // the next sample to render
    int speed_ = 1;
    std::int64_t left_ = -1; // samples still to render before silence; -1: no limit

    // A single-producer, single-consumer ring of stereo samples.
    std::vector<float> ring_;
    std::atomic<std::size_t> readAt_{0};
    std::atomic<std::size_t> writeAt_{0};
};

} // namespace lumenis::media
