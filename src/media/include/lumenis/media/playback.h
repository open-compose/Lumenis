#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/audio_fx.h"
#include "lumenis/media/surround.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QString>

#include <array>
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

class AudioInput;
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
    // Volume and pan automation (lumenis/model/automation.h), with AudioKey::frame in output
    // samples from `at`: the volume in dB on top of `gainDb`, the pan (-100 to 100) in place of
    // `pan`. When either has keyframes the gains are worked out sample by sample from these and
    // `left`, `right` and `gain` are not used.
    QList<model::AudioKey> volumeKeys;
    QList<model::AudioKey> panKeys;
    double gainDb = 0;     // the clip gain (model::kGainOff for -∞)
    double pan = 0;        // the pan where there is no pan automation
    bool monoGain = false; // the volume alone, on the left, without the pan (a mono mixdown)
    float scale = 1.0f;    // a gain on top of the automated volume (the Master fader in a mixdown)
    bool automated() const { return !volumeKeys.isEmpty() || !panKeys.isEmpty(); }
    // The segment's Audio EQ (lumenis/model/audio_eq.h), filtering the piece's samples before
    // its gains; none, not In, or flat: unfiltered.
    std::optional<model::AudioEq> eq;
    bool equalised() const;
    // The sequence track it plays on ("A1"; "A1#2" for the second channel of a multichannel track,
    // channelTrackKey()); empty for a clip's channel. A track with Audio Track Effect inserts
    // (TrackInserts) is mixed through them.
    QString track;
    // The speaker of the mix's bus (lumenis/media/surround.h) a channel of a multichannel track in a
    // surround sequence plays on, at its volume (`left` holds the gain, `right` is 0) and without a
    // pan; -1 for the others, panned between L and R by `left` and `right`.
    int speaker = -1;
};

// The Audio Track Effect inserts each track plays through (Editing Guide p.790-795;
// lumenis/media/audio_fx.h), a to e, without the bypassed ones.
using TrackInserts = std::map<QString, QList<model::TrackInsert>>;
// A sequence's inserts; empty for a clip, or with `bypassAll` (Ctrl+Bypass, p.794). A multichannel
// track's are listed once for each of its channels, under channelTrackKey().
TrackInserts trackInserts(const model::BinItem& item, bool bypassAll = false);
// What AudioPiece::track and TrackInserts call channel `channel` (0-based) of a track: the track's
// name for the first, "A1#2" and so on for the others of a multichannel track.
QString channelTrackKey(const QString& track, int channel);

// Equal-power gains for a pan from -100 (left) to 100 (right), times a
// gain in dB.
void panGains(int pan, double gainDb, float* left, float* right);
void panGains(double pan, double gainDb, float* left, float* right);

// Frames at an edit rate as output samples, and back.
std::int64_t framesToSamples(std::int64_t frames, const Rational& rate);
double samplesToFrames(std::int64_t samples, const Rational& rate);

// What a monitor plays (Editing Guide p.395, p.627), in output samples from
// the item's start:
// - a clip: each audio channel is a track; mono tracks are panned odd to
//   the left, even to the right (p.627);
// - a sequence: the segments of its monitored audio tracks, each with its
//   clip gain and pan (p.702); a centred pan follows its track's side; and
//   its volume and pan automation (p.714).
// Clips without media, unlinked or offline, are silent. Without
// `alternatingPan` (Audio settings, "Default Pan for mono tracks":
// Centered, p.1147) mono tracks play in the middle.
using ClipLookup = std::function<std::optional<model::BinItem>(const QString& binId, const QString& clipId)>;
std::vector<AudioPiece> audioPieces(const model::BinItem& item, const ClipLookup& clips,
                                    const std::function<bool(const QString& track)>& monitored = {},
                                    bool alternatingPan = true);

// Mixes pieces onto the eight speakers of a bus (lumenis/media/surround.h) and from them into the
// output's channels, interleaved: stereo unless setOutput() says otherwise. Reads the files itself,
// so one mix serves one thread at a time.
// Files are opened on first use and kept open for the mix's life.
class AudioMix {
public:
    // `inserts`: the tracks whose pieces are summed in mono, run through their effects left to right
    // and then panned as their pieces are (p.794). Effects not installed are skipped.
    explicit AudioMix(std::vector<AudioPiece> pieces = {}, const TrackInserts& inserts = {});
    ~AudioMix();
    AudioMix(const AudioMix&) = delete;
    AudioMix& operator=(const AudioMix&) = delete;

    const std::vector<AudioPiece>& pieces() const { return pieces_; }
    // The output: a row of gains from the bus for each channel (outputMatrix(), trackMatrix()). By
    // default a stereo sequence's on stereo speakers, which has the pieces' left and right gains.
    void setOutput(std::vector<SpeakerGains> matrix);
    // A monitor's output: a sequence mixed in `sequenceFormat` heard through `monitor`.
    void setOutput(model::AudioFormat sequenceFormat, MonitorMix monitor);
    int channels() const { return static_cast<int>(matrix_.size()); }
    // The monitor setOutput() was given; Stereo for a matrix.
    MonitorMix monitor() const { return monitor_; }
    // The output channels the bus's L and R speakers reach most: where a stereo signal from outside
    // the mix goes (Audio Punch-In's monitoring).
    int leftChannel() const { return leftChannel_; }
    int rightChannel() const { return rightChannel_; }
    // `count` samples of each output channel from output sample `start` into `out`
    // (channels() * count floats, interleaved), silence where nothing plays.
    void render(std::int64_t start, int count, float* out);

private:
    // The reader of a piece's file, stream and channel; null if it cannot be opened.
    AudioReader* reader(const AudioPiece& piece);
    // `count` samples of a piece's channel from `offset` output samples into the piece, at the
    // output rate; zeros where the file has none.
    std::vector<float> samples(const AudioPiece& piece, std::int64_t offset, int count);
    // Runs piece `index`'s EQ over `samples`, which start `offset` samples into the piece. The
    // filters keep their state from one render to the next; when a render does not follow on from
    // the last one (a seek, reverse play), they start again, run first over up to 0.1 s of the
    // samples before so they have settled.
    void equalise(std::size_t index, std::int64_t offset, std::vector<float>& samples);

    // One piece's filters and where its last render ended.
    struct EqState {
        bool ready = false;
        std::array<model::Biquad, 3> filters{};
        std::array<std::array<double, 2>, 3> state{}; // transposed direct form II
        std::int64_t next = -1;
    };

    // One track's effects and where its last render ended. `left` and `right` are the pan the
    // effects' output takes where no piece plays (a reverb's tail): the last piece's. A channel of a
    // multichannel track plays on its pieces' `speaker` instead.
    struct Chain {
        QString track;
        int speaker = -1;
        std::vector<std::unique_ptr<AudioEffect>> effects;
        int tail = 0; // the longest tail, in samples (at most 4 s)
        std::int64_t next = -1;
        float left = 0.70710678f;
        float right = 0.70710678f;
    };
    // The track's pieces from `start`, summed in mono with their gains, and the pan of each sample,
    // into `bus`, `left` and `right` (resized to `count`).
    void trackInput(Chain& chain, std::int64_t start, int count, std::vector<float>& bus, std::vector<float>& left,
                    std::vector<float>& right);
    // A track's effects over [start, start + count), added to the bus (kSpeakers * count floats,
    // interleaved); after a seek, the effects first run over the tail's length before `start` so
    // they are settled.
    void renderChain(Chain& chain, std::int64_t start, int count, float* bus);

    std::vector<AudioPiece> pieces_;
    std::vector<EqState> eq_;
    std::vector<Chain> chains_;
    std::map<QString, std::unique_ptr<AudioReader>> readers_;
    std::vector<SpeakerGains> matrix_;
    MonitorMix monitor_ = MonitorMix::Stereo;
    int leftChannel_ = 0;
    int rightChannel_ = 1;
    std::vector<float> bus_; // render()'s, kept to save allocating it each time
};

// Plays a mix through the system's audio output (miniaudio). A thread
// renders ahead into a buffer the output drains; what the output has taken
// is the clock. Without an output device nothing plays and the clock stays
// at zero: the caller falls back to its own. The device has the mix's channels, in the order of its
// monitor (AudioMix::monitor()); the system mixes them down if it has fewer speakers.
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

    // The output's channels: the mix's (AudioMix::channels()), 2 when nothing plays.
    int channels() const { return channels_; }
    // Fills `out` (channels() * count floats) from the buffer, as the device does
    // (tests), plus the monitored input if there is one.
    int pull(float* out, int count);
    // Audio Punch-In's monitoring (p.748-749, p.753): what `input` records is added to the output
    // until this is called again with null. The input must outlive that call.
    void setMonitor(AudioInput* input) { monitor_.store(input); }
    AudioInput* monitor() const { return monitor_.load(); }

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
    std::atomic<AudioInput*> monitor_{nullptr};
    std::atomic<std::int64_t> played_{0};
    std::atomic<std::int64_t> pulledAt_{0}; // steady-clock ns of the last pull
    std::int64_t latency_ = 0;             // samples the device holds
    std::int64_t next_ = 0; // the next sample to render
    int speed_ = 1;
    std::int64_t left_ = -1; // samples still to render before silence; -1: no limit

    // A single-producer, single-consumer ring of samples of channels_ channels.
    std::vector<float> ring_;
    int channels_ = 2;
    int monitorLeft_ = 0; // where the monitored input goes (AudioMix::leftChannel())
    int monitorRight_ = 1;
    std::vector<float> monitorBuffer_; // the monitored input, in stereo, when channels_ is not 2
    std::atomic<std::size_t> readAt_{0};
    std::atomic<std::size_t> writeAt_{0};
};

} // namespace lumenis::media
