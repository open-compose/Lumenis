#include "lumenis/media/playback.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sequence.h"

#include <QFileInfo>

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

// Playback's sound (ROADMAP Phase 2.3): audioPieces() turns what a monitor shows (a clip, or a
// sequence's monitored audio tracks) into pieces of files placed in time; AudioMix reads and mixes
// them to 48 kHz stereo through AudioReader (media.cpp); AudioPlayer plays the mix through miniaudio
// (third_party/miniaudio) and gives the clock the Composer's picture follows. The UI side is in
// src/ui/main_window.cpp, which builds the pieces and owns the player; the picture is decoded ahead
// by MediaFrames::prefetch (src/ui/media_frames.cpp).
//
// Threads: a feeder thread (AudioPlayer::feed) renders the mix into a single-producer,
// single-consumer ring; miniaudio's callback thread drains it (pull()); the UI thread reads clock().
// The mix is used only by the feeder while playing.

namespace lumenis::media {

namespace {

constexpr int kChunk = 1024;                         // samples rendered at a time
constexpr std::size_t kRingFrames = kPlaybackRate / 4; // a quarter second ahead

} // namespace

void panGains(int pan, double gainDb, float* left, float* right) {
    // Equal power: the centre is 3 dB down on each side.
    const double p = std::clamp(pan, -100, 100) / 100.0;
    const double angle = (p + 1.0) * std::numbers::pi / 4.0;
    const double gain = std::pow(10.0, gainDb / 20.0);
    *left = static_cast<float>(std::cos(angle) * gain);
    *right = static_cast<float>(std::sin(angle) * gain);
}

// Rounds toward zero; a rate without frames (num <= 0) gives 0.
std::int64_t framesToSamples(std::int64_t frames, const Rational& rate) {
    // Exact in integers: frames * 48000 * den / num.
    if (rate.num() <= 0) return 0;
    return frames * kPlaybackRate * rate.den() / rate.num();
}

// The exact frame position, with fraction, of an output sample.
double samplesToFrames(std::int64_t samples, const Rational& rate) {
    return static_cast<double>(samples) * static_cast<double>(rate.num()) /
           (static_cast<double>(kPlaybackRate) * static_cast<double>(rate.den()));
}

namespace {

// A clip's audio track `track` (0 for A1) as a piece, if it has media.
// The piece's path, stream, channel and start in the file; the caller sets where it plays and for
// how long. Nothing when the clip is unlinked, the track has no channel or the file is missing.
std::optional<AudioPiece> pieceFor(const model::BinItem& clip, int track) {
    const auto link = linkOf(clip);
    if (!link || link->unlinked) return std::nullopt;
    const auto channel = link->channelOf(track);
    if (!channel) return std::nullopt;
    AudioPiece p;
    p.path = link->audioFile(track);
    // Where the clip starts in its media.
    p.sourceStart = framesToSamples(link->first, link->rate);
    if (!QFileInfo::exists(p.path)) return std::nullopt;
    p.stream = channel->first;
    p.channel = channel->second;
    return p;
}

// Odd tracks to the left, even to the right (p.627).
int sidePan(int trackNumber) {
    return trackNumber % 2 == 1 ? -100 : 100;
}

} // namespace

// Clips (not sequences) play every channel from 0 for the clip's duration. For sequences each
// non-filler segment of a monitored audio track plays its source clip's track from the segment's
// source in, at its place in the sequence; the segment's pan, or the track's side when it has none,
// and its gain set the stereo gains.
std::vector<AudioPiece> audioPieces(const model::BinItem& item, const ClipLookup& clips,
                                    const std::function<bool(const QString& track)>& monitored, bool alternatingPan) {
    auto side = [alternatingPan](int trackNumber) { return alternatingPan ? sidePan(trackNumber) : 0; };
    std::vector<AudioPiece> out;
    const Rational rate = item.editRate;
    if (item.kind != model::BinItemKind::Sequence) {
        const auto link = linkOf(item);
        if (!link) return out;
        for (int t = 0; t < link->channels; ++t) {
            if (monitored && !monitored(QStringLiteral("A%1").arg(t + 1))) continue;
            auto p = pieceFor(item, t);
            if (!p) continue;
            p->at = 0;
            p->length = framesToSamples(item.durationFrames, rate);
            // A single channel plays in the middle; more go odd left, even
            // right.
            panGains(link->channels == 1 ? 0 : side(t + 1), 0.0, &p->left, &p->right);
            out.push_back(*p);
        }
        return out;
    }
    const model::Sequence seq = model::Sequence::fromItem(item);
    for (const model::Track& track : seq.tracks()) {
        if (track.kind != model::TrackKind::Audio) continue;
        if (monitored && !monitored(track.name())) continue;
        std::int64_t at = 0;
        for (const model::Segment& s : track.segments) {
            const std::int64_t start = at;
            at += s.length;
            if (s.isFiller() || !clips) continue;
            const auto clip = clips(s.binId, s.clipId);
            const auto ref = model::parseTrackName(s.sourceTrack);
            if (!clip || !ref || ref->kind != model::TrackKind::Audio) continue;
            auto p = pieceFor(*clip, ref->number - 1);
            if (!p) continue;
            p->at = framesToSamples(start, rate);
            p->length = framesToSamples(start + s.length, rate) - p->at;
            p->sourceStart += framesToSamples(s.sourceIn, clip->editRate);
            panGains(s.pan != 0 ? s.pan : side(track.number), s.gain, &p->left, &p->right);
            p->gain = s.gain <= model::kGainOff ? 0.0f : static_cast<float>(std::pow(10.0, s.gain / 20.0));
            out.push_back(*p);
        }
    }
    return out;
}

// ---------------------------------------------------------------- AudioMix

AudioMix::AudioMix(std::vector<AudioPiece> pieces) : pieces_(std::move(pieces)) {}

AudioMix::~AudioMix() = default;

// The reader for a piece, opened on first use and kept for the mix's life; null (kept, so not
// retried) if the file cannot be opened.
AudioReader* AudioMix::reader(const AudioPiece& piece) {
    // One reader for each channel: the channels of a stereo file are read
    // side by side, and a shared reader would seek between them.
    const QString key = piece.path + QLatin1Char('|') + QString::number(piece.stream) + QLatin1Char('|') +
                        QString::number(piece.channel);
    auto it = readers_.find(key);
    if (it == readers_.end()) {
        auto r = std::make_unique<AudioReader>();
        if (!r->open(piece.path, piece.stream)) r.reset();
        it = readers_.emplace(key, std::move(r)).first;
    }
    return it->second.get();
}

// Each piece adds its part of [start, start + count) into the output. Files at 48 kHz are read
// directly; others are read at their own rate and interpolated.
void AudioMix::render(std::int64_t start, int count, float* out) {
    std::fill(out, out + 2 * static_cast<std::ptrdiff_t>(count), 0.0f);
    const std::int64_t end = start + count;
    for (const AudioPiece& piece : pieces_) {
        const std::int64_t from = std::max(start, piece.at);
        const std::int64_t to = std::min(end, piece.at + piece.length);
        if (from >= to) continue;
        AudioReader* r = reader(piece);
        if (!r || piece.channel >= r->channels()) continue;
        const int channels = r->channels();
        const int rate = r->sampleRate();
        const auto n = static_cast<int>(to - from);
        float* dest = out + 2 * (from - start);
        if (rate == kPlaybackRate) {
            const std::vector<float> src = r->samples(piece.sourceStart + (from - piece.at), n);
            const auto got = static_cast<int>(src.size()) / channels;
            for (int i = 0; i < got; ++i) {
                const float s = src[static_cast<std::size_t>(i * channels + piece.channel)];
                dest[2 * i] += s * piece.left;
                dest[2 * i + 1] += s * piece.right;
            }
            continue;
        }
        // Another rate: linear interpolation between the file's samples.
        const double step = static_cast<double>(rate) / kPlaybackRate;
        const double first = static_cast<double>(piece.sourceStart + (from - piece.at)) * step;
        const auto base = static_cast<std::int64_t>(std::floor(first));
        const int need = static_cast<int>(std::ceil(static_cast<double>(n) * step)) + 2;
        const std::vector<float> src = r->samples(base, need);
        const auto got = static_cast<std::int64_t>(src.size()) / channels;
        for (int i = 0; i < n; ++i) {
            const double pos = first + static_cast<double>(i) * step - static_cast<double>(base);
            const auto k = static_cast<std::int64_t>(pos);
            if (k + 1 >= got) break;
            const auto frac = static_cast<float>(pos - static_cast<double>(k));
            const float a = src[static_cast<std::size_t>(k * channels + piece.channel)];
            const float b = src[static_cast<std::size_t>((k + 1) * channels + piece.channel)];
            const float s = a + (b - a) * frac;
            dest[2 * i] += s * piece.left;
            dest[2 * i + 1] += s * piece.right;
        }
    }
}

// ---------------------------------------------------------------- AudioPlayer

// The miniaudio device, when one is open.
struct AudioPlayer::Device {
    ma_device device{};
    bool open = false;
};

namespace {

// miniaudio's callback, on its own audio thread: the output asks for `frames` stereo samples.
void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* player = static_cast<AudioPlayer*>(device->pUserData);
    player->pull(static_cast<float*>(output), static_cast<int>(frames));
}

} // namespace

AudioPlayer::AudioPlayer() : ring_(2 * kRingFrames, 0.0f) {}

AudioPlayer::~AudioPlayer() {
    stop();
}

// Stops any earlier playback, then starts the feeder thread and, unless options.device is false, a
// 48 kHz stereo float device. The device's buffering (periods times period size, converted to the
// output rate) is kept as latency_ for clock(). Speed is clamped to -3..3, 0 meaning 1.
bool AudioPlayer::start(std::shared_ptr<AudioMix> mix, std::int64_t start, const PlayOptions& options) {
    stop();
    mix_ = std::move(mix);
    next_ = start;
    speed_ = std::clamp(options.speed, -3, 3) == 0 ? 1 : std::clamp(options.speed, -3, 3);
    left_ = options.limit;
    const bool device = options.device;
    readAt_ = 0;
    writeAt_ = 0;
    played_ = 0;
    pulledAt_ = 0;
    latency_ = 0;
    running_ = true;
    feeder_ = std::thread([this] { feed(); });
    if (!device) {
        playing_ = true;
        return true;
    }

    auto d = std::make_unique<Device>();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kPlaybackRate;
    config.dataCallback = dataCallback;
    config.pUserData = this;
    if (ma_device_init(nullptr, &config, &d->device) != MA_SUCCESS) {
        stop();
        return false;
    }
    d->open = true;
    // What the device buffers, at the output rate.
    const ma_uint32 internalRate = d->device.playback.internalSampleRate;
    if (internalRate > 0) {
        latency_ = static_cast<std::int64_t>(d->device.playback.internalPeriodSizeInFrames) *
                   static_cast<std::int64_t>(d->device.playback.internalPeriods) * kPlaybackRate / internalRate;
    }
    if (ma_device_start(&d->device) != MA_SUCCESS) {
        ma_device_uninit(&d->device);
        stop();
        return false;
    }
    device_ = std::move(d);
    playing_ = true;
    return true;
}

// The device goes first, then the feeder thread is joined; safe to call when nothing plays.
void AudioPlayer::stop() {
    if (device_ && device_->open) {
        // Stops the callbacks before the buffer and mix go.
        ma_device_uninit(&device_->device);
    }
    device_.reset();
    running_ = false;
    if (feeder_.joinable()) feeder_.join();
    playing_ = false;
    mix_.reset();
}

// Empty when no device is open (not playing, or tests without one).
QString AudioPlayer::deviceName() const {
    if (!device_ || !device_->open) return {};
    return QString::fromUtf8(device_->device.playback.name);
}

// The feeder thread: whenever the ring has room for a chunk, renders the next kChunk output samples
// (silence once a digital scrub's limit is used up) and publishes them by advancing writeAt_.
// readAt_ and writeAt_ count samples without wrapping; the ring index is their remainder.
void AudioPlayer::feed() {
    std::vector<float> chunk(2 * kChunk);
    const int step = std::abs(speed_);
    std::vector<float> wide(2 * static_cast<std::size_t>(kChunk * step));
    while (running_) {
        const std::size_t used = writeAt_.load() - readAt_.load();
        if (kRingFrames - used < kChunk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        if (left_ == 0) {
            std::fill(chunk.begin(), chunk.end(), 0.0f);
        } else if (step == 1 && speed_ > 0) {
            mix_->render(next_, kChunk, chunk.data());
            next_ += kChunk;
        } else {
            // kChunk output samples cover kChunk * step of the program,
            // forward or backward from where play is.
            const int span = kChunk * step;
            const std::int64_t from = speed_ > 0 ? next_ : next_ - span;
            mix_->render(from, span, wide.data());
            for (int i = 0; i < kChunk; ++i) {
                float l = 0.0f;
                float r = 0.0f;
                for (int k = 0; k < step; ++k) {
                    const int src = speed_ > 0 ? i * step + k : span - 1 - (i * step + k);
                    l += wide[static_cast<std::size_t>(2 * src)];
                    r += wide[static_cast<std::size_t>(2 * src + 1)];
                }
                chunk[static_cast<std::size_t>(2 * i)] = l / static_cast<float>(step);
                chunk[static_cast<std::size_t>(2 * i + 1)] = r / static_cast<float>(step);
            }
            next_ += speed_ > 0 ? span : -span;
        }
        if (left_ > 0) {
            // A digital scrub's samples, then silence.
            const std::int64_t keep = std::min<std::int64_t>(left_, kChunk);
            std::fill(chunk.begin() + 2 * keep, chunk.end(), 0.0f);
            left_ -= keep;
        }
        std::size_t w = writeAt_.load();
        for (int i = 0; i < kChunk; ++i, ++w) {
            const std::size_t at = 2 * (w % kRingFrames);
            ring_[at] = chunk[static_cast<std::size_t>(2 * i)];
            ring_[at + 1] = chunk[static_cast<std::size_t>(2 * i + 1)];
        }
        writeAt_.store(w);
    }
}

// Called by the device's thread (or by tests): copies what the ring holds, up to `count`, pads the
// rest with silence, and notes how much and when for clock().
int AudioPlayer::pull(float* out, int count) {
    std::size_t r = readAt_.load();
    const std::size_t available = writeAt_.load() - r;
    const auto n = static_cast<int>(std::min<std::size_t>(available, static_cast<std::size_t>(std::max(count, 0))));
    for (int i = 0; i < n; ++i, ++r) {
        const std::size_t at = 2 * (r % kRingFrames);
        out[2 * i] = ring_[at];
        out[2 * i + 1] = ring_[at + 1];
    }
    // Not ready yet: silence, and the clock waits.
    std::fill(out + 2 * n, out + 2 * std::max(count, 0), 0.0f);
    readAt_.store(r);
    played_ += n;
    pulledAt_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
    return n;
}

// 0 until the device has taken samples.
std::int64_t AudioPlayer::clock() const {
    const std::int64_t played = played_.load();
    const std::int64_t at = pulledAt_.load();
    if (played <= 0 || at == 0) return 0;
    // When the device last took samples it held `latency_` of them unheard;
    // since then it has played on, at the output rate, through those.
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    const std::int64_t since = (now - at) * kPlaybackRate / 1'000'000'000;
    return std::clamp<std::int64_t>(played - latency_ + since, 0, played);
}

} // namespace lumenis::media
