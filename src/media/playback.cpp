#include "lumenis/media/playback.h"

#include "lumenis/media/audio_input.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/automation.h"
#include "lumenis/model/multichannel.h"
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
    panGains(static_cast<double>(pan), gainDb, left, right);
}

void panGains(double pan, double gainDb, float* left, float* right) {
    // Equal power: the centre is 3 dB down on each side.
    const double p = std::clamp(pan, -100.0, 100.0) / 100.0;
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

namespace {

// The channels of a segment on a multichannel track (lumenis/model/multichannel.h), each a piece of
// its own from the segment's first channel on, placed for stereo speakers as the guide's table has
// it (p.687-688): a stereo track's channels left and right; a surround track's L and R left and
// right, C and LFE in the centre, the surround channels left and right 3 dB down. The segment's pan
// moves them all to one side (ours), its volume automation applies to every channel, and its pan
// automation is not used. Each channel is mixed through its track's inserts on its own
// (channelTrackKey()). AudioSuite effects are not used on multichannel tracks yet.
void multichannelPieces(const model::Track& track, const model::Segment& s, std::int64_t start, const Rational& rate,
                        const ClipLookup& clips, std::vector<AudioPiece>& out) {
    const auto clip = clips(s.binId, s.clipId);
    const auto ref = model::parseTrackName(s.sourceTrack);
    if (!clip || !ref || ref->kind != model::TrackKind::Audio) return;
    // Pans and gains in Pro Tools channel order: L C R Ls Rs Lfe; L C R Lss Rss Lsr Rsr Lfe.
    struct Place {
        double pan;
        double db;
    };
    QList<Place> places;
    switch (track.format) {
    case model::AudioFormat::Stereo: places = {{-100, 0}, {100, 0}}; break;
    case model::AudioFormat::Surround51: places = {{-100, 0}, {0, 0}, {100, 0}, {-100, -3}, {100, -3}, {0, 0}}; break;
    case model::AudioFormat::Surround71:
        places = {{-100, 0}, {0, 0}, {100, 0}, {-100, -3}, {100, -3}, {-100, -3}, {100, -3}, {0, 0}};
        break;
    case model::AudioFormat::Mono: places = {{0, 0}}; break;
    }
    const double perFrame = static_cast<double>(kPlaybackRate) * static_cast<double>(rate.den()) / static_cast<double>(rate.num());
    for (int k = 0; k < places.size(); ++k) {
        auto p = pieceFor(*clip, ref->number - 1 + k);
        if (!p) continue;
        p->sourceStart += framesToSamples(s.sourceIn, clip->editRate);
        p->at = framesToSamples(start, rate);
        p->length = framesToSamples(start + s.length, rate) - p->at;
        const double pan = std::clamp(places.at(k).pan + s.pan, -100.0, 100.0);
        const double gainDb = s.gain <= model::kGainOff ? model::kGainOff : s.gain + places.at(k).db;
        panGains(pan, gainDb, &p->left, &p->right);
        p->gain = gainDb <= model::kGainOff ? 0.0f : static_cast<float>(std::pow(10.0, gainDb / 20.0));
        p->gainDb = gainDb;
        p->pan = pan;
        p->eq = s.eq;
        p->track = channelTrackKey(track.name(), k);
        for (const model::AudioKey& key : s.volumeKeys) {
            p->volumeKeys << model::AudioKey{(key.frame - static_cast<double>(s.sourceIn)) * perFrame, key.value};
        }
        // Volume automation sets the pan from `pan` (AudioPiece::automated()).
        out.push_back(*p);
    }
}

} // namespace

QString channelTrackKey(const QString& track, int channel) {
    return channel == 0 ? track : QStringLiteral("%1#%2").arg(track).arg(channel + 1);
}

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
        const int channels = model::channelCount(track.format);
        for (const model::Segment& s : track.segments) {
            const std::int64_t start = at;
            at += s.length;
            if (s.isFiller() || !clips) continue;
            if (channels > 1) {
                multichannelPieces(track, s, start, rate, clips, out);
                continue;
            }
            std::optional<AudioPiece> p;
            // A rendered AudioSuite effect plays its rendered sound, which starts at the segment's
            // start and already has the clip gain in it (the render order, p.700): the pan, EQ and
            // automation still apply.
            const bool rendered = model::isRendered(s) && QFileInfo::exists(s.audioSuite->rendered);
            double gainDb = s.gain;
            if (rendered) {
                p = AudioPiece{};
                p->path = s.audioSuite->rendered;
                p->stream = s.audioSuite->renderedStream;
                p->channel = s.audioSuite->renderedChannel;
                gainDb = 0.0;
            } else {
                const auto clip = clips(s.binId, s.clipId);
                const auto ref = model::parseTrackName(s.sourceTrack);
                if (!clip || !ref || ref->kind != model::TrackKind::Audio) continue;
                p = pieceFor(*clip, ref->number - 1);
                if (!p) continue;
                p->sourceStart += framesToSamples(s.sourceIn, clip->editRate);
            }
            p->at = framesToSamples(start, rate);
            p->length = framesToSamples(start + s.length, rate) - p->at;
            panGains(s.pan != 0 ? s.pan : side(track.number), gainDb, &p->left, &p->right);
            p->gain = gainDb <= model::kGainOff ? 0.0f : static_cast<float>(std::pow(10.0, gainDb / 20.0));
            p->gainDb = gainDb;
            p->eq = s.eq;
            p->track = track.name();
            p->pan = s.pan != 0 ? s.pan : side(track.number);
            // Keyframes in the clip's frames, as output samples from the segment's start.
            const double perFrame = static_cast<double>(kPlaybackRate) * static_cast<double>(rate.den()) /
                                    static_cast<double>(rate.num());
            for (const auto& [from, into] : {std::pair{&s.volumeKeys, &p->volumeKeys}, std::pair{&s.panKeys, &p->panKeys}}) {
                for (const model::AudioKey& k : *from) {
                    *into << model::AudioKey{(k.frame - static_cast<double>(s.sourceIn)) * perFrame, k.value};
                }
            }
            out.push_back(*p);
        }
    }
    return out;
}

// ---------------------------------------------------------------- AudioMix

bool AudioPiece::equalised() const {
    return eq && eq->in && !model::isFlat(*eq);
}

namespace {

// An automated piece's gains `offset` output samples into it: the volume as model::volumeAt and
// the pan as model::panAt work them out (lumenis/model/automation.h), then the scale.
void automatedGains(const AudioPiece& piece, double offset, float* left, float* right) {
    double db = piece.gainDb;
    if (const auto v = model::automationAt(piece.volumeKeys, offset)) {
        db = (*v <= model::kGainOff || piece.gainDb <= model::kGainOff) ? model::kGainOff
                                                                         : std::min(piece.gainDb + *v, model::kGainMax);
    }
    if (db <= model::kGainOff) {
        *left = 0;
        *right = 0;
        return;
    }
    if (piece.monoGain) {
        *left = static_cast<float>(std::pow(10.0, db / 20.0)) * piece.scale;
        *right = 0;
        return;
    }
    const double pan = model::automationAt(piece.panKeys, offset).value_or(piece.pan);
    panGains(pan, db, left, right);
    *left *= piece.scale;
    *right *= piece.scale;
}

} // namespace

TrackInserts trackInserts(const model::BinItem& item, bool bypassAll) {
    TrackInserts out;
    if (bypassAll || item.kind != model::BinItemKind::Sequence) return out;
    const model::Sequence seq = model::Sequence::fromItem(item);
    for (const model::Track& t : seq.tracks()) {
        if (t.kind != model::TrackKind::Audio) continue;
        QList<model::TrackInsert> list;
        for (const auto& in : t.inserts) {
            if (in && !in->bypass) list << *in;
        }
        if (list.isEmpty()) continue;
        // A multichannel track runs each channel through its own copy of the effects.
        for (int k = 0; k < model::channelCount(t.format); ++k) out[channelTrackKey(t.name(), k)] = list;
    }
    return out;
}

// Each track's chain is made now; its effects take the inserts' values.
AudioMix::AudioMix(std::vector<AudioPiece> pieces, const TrackInserts& inserts)
    : pieces_(std::move(pieces)), eq_(pieces_.size()) {
    for (const auto& [track, list] : inserts) {
        Chain chain;
        chain.track = track;
        for (const model::TrackInsert& in : list) {
            auto fx = makeAudioEffect(in.effect);
            if (!fx) continue; // "Unavailable Effect" (p.790): the track plays without it
            fx->setAll(in.params);
            chain.tail = std::max(chain.tail, std::min(fx->tail(), 4 * kPlaybackRate));
            chain.effects.push_back(std::move(fx));
        }
        if (!chain.effects.empty()) chains_.push_back(std::move(chain));
    }
}

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
    for (Chain& chain : chains_) renderChain(chain, start, count, out);
    for (std::size_t index = 0; index < pieces_.size(); ++index) {
        const AudioPiece& piece = pieces_[index];
        // Pieces on a track with effects went through renderChain().
        if (!chains_.empty() && std::any_of(chains_.begin(), chains_.end(), [&](const Chain& c) { return c.track == piece.track; })) {
            continue;
        }
        const std::int64_t from = std::max(start, piece.at);
        const std::int64_t to = std::min(end, piece.at + piece.length);
        if (from >= to) continue;
        const auto n = static_cast<int>(to - from);
        std::vector<float> src = samples(piece, from - piece.at, n);
        if (src.empty()) continue;
        if (piece.equalised()) equalise(index, from - piece.at, src);
        float* dest = out + 2 * (from - start);
        if (!piece.automated()) {
            for (int i = 0; i < n; ++i) {
                dest[2 * i] += src[static_cast<std::size_t>(i)] * piece.left;
                dest[2 * i + 1] += src[static_cast<std::size_t>(i)] * piece.right;
            }
            continue;
        }
        for (int i = 0; i < n; ++i) {
            float l = 0;
            float r = 0;
            automatedGains(piece, static_cast<double>(from - piece.at + i), &l, &r);
            dest[2 * i] += src[static_cast<std::size_t>(i)] * l;
            dest[2 * i + 1] += src[static_cast<std::size_t>(i)] * r;
        }
    }
}

// A piece's stereo gains (l, r) are a gain g = sqrt(l^2 + r^2) and a pan (l/g, r/g): the gain goes on
// the mono sum, the pan on what the effects make of it.
void AudioMix::trackInput(Chain& chain, std::int64_t start, int count, std::vector<float>& bus, std::vector<float>& left,
                          std::vector<float>& right) {
    const auto n = static_cast<std::size_t>(count);
    bus.assign(n, 0.0f);
    left.assign(n, -1.0f); // -1: no piece there
    right.assign(n, 0.0f);
    const std::int64_t end = start + count;
    for (std::size_t index = 0; index < pieces_.size(); ++index) {
        const AudioPiece& piece = pieces_[index];
        if (piece.track != chain.track) continue;
        const std::int64_t from = std::max(start, piece.at);
        const std::int64_t to = std::min(end, piece.at + piece.length);
        if (from >= to) continue;
        const auto k = static_cast<int>(to - from);
        std::vector<float> src = samples(piece, from - piece.at, k);
        if (src.empty()) continue;
        if (piece.equalised()) equalise(index, from - piece.at, src);
        for (int i = 0; i < k; ++i) {
            float l = piece.left;
            float r = piece.right;
            if (piece.automated()) automatedGains(piece, static_cast<double>(from - piece.at + i), &l, &r);
            const float g = std::sqrt(l * l + r * r);
            const auto at = static_cast<std::size_t>(from - start + i);
            bus[at] += src[static_cast<std::size_t>(i)] * g;
            if (g > 0) {
                left[at] = l / g;
                right[at] = r / g;
            }
        }
    }
    // Where no piece plays, the pan of the last sample that had one.
    for (std::size_t i = 0; i < n; ++i) {
        if (left[i] < 0) {
            left[i] = chain.left;
            right[i] = chain.right;
        } else {
            chain.left = left[i];
            chain.right = right[i];
        }
    }
}

void AudioMix::renderChain(Chain& chain, std::int64_t start, int count, float* out) {
    std::vector<float> bus, left, right;
    if (start != chain.next) {
        for (auto& fx : chain.effects) fx->reset();
        const std::int64_t warm = std::min<std::int64_t>(chain.tail, std::max<std::int64_t>(start, 0));
        for (std::int64_t at = start - warm; at < start;) {
            const auto k = static_cast<int>(std::min<std::int64_t>(4096, start - at));
            trackInput(chain, at, k, bus, left, right);
            for (auto& fx : chain.effects) fx->process(bus.data(), k);
            at += k;
        }
    }
    trackInput(chain, start, count, bus, left, right);
    for (auto& fx : chain.effects) fx->process(bus.data(), count);
    for (int i = 0; i < count; ++i) {
        const auto k = static_cast<std::size_t>(i);
        out[2 * i] += bus[k] * left[k];
        out[2 * i + 1] += bus[k] * right[k];
    }
    chain.next = start + count;
}

// Files at 48 kHz are read directly; others are read at their own rate and interpolated linearly.
// Empty when the file cannot be read.
std::vector<float> AudioMix::samples(const AudioPiece& piece, std::int64_t offset, int count) {
    AudioReader* r = reader(piece);
    if (!r || piece.channel >= r->channels() || count <= 0) return {};
    std::vector<float> out(static_cast<std::size_t>(count), 0.0f);
    const int channels = r->channels();
    const int rate = r->sampleRate();
    if (rate == kPlaybackRate) {
        const std::vector<float> src = r->samples(piece.sourceStart + offset, count);
        const auto got = std::min(static_cast<int>(src.size()) / channels, count);
        for (int i = 0; i < got; ++i) out[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i * channels + piece.channel)];
        return out;
    }
    const double step = static_cast<double>(rate) / kPlaybackRate;
    const double first = static_cast<double>(piece.sourceStart + offset) * step;
    const auto base = static_cast<std::int64_t>(std::floor(first));
    const int need = static_cast<int>(std::ceil(static_cast<double>(count) * step)) + 2;
    const std::vector<float> src = r->samples(base, need);
    const auto got = static_cast<std::int64_t>(src.size()) / channels;
    for (int i = 0; i < count; ++i) {
        const double pos = first + static_cast<double>(i) * step - static_cast<double>(base);
        const auto k = static_cast<std::int64_t>(pos);
        if (k + 1 >= got) break;
        const auto frac = static_cast<float>(pos - static_cast<double>(k));
        const float a = src[static_cast<std::size_t>(k * channels + piece.channel)];
        const float b = src[static_cast<std::size_t>((k + 1) * channels + piece.channel)];
        out[static_cast<std::size_t>(i)] = a + (b - a) * frac;
    }
    return out;
}

void AudioMix::equalise(std::size_t index, std::int64_t offset, std::vector<float>& samples) {
    if (eq_.size() != pieces_.size()) eq_.resize(pieces_.size());
    EqState& eq = eq_[index];
    if (!eq.ready) {
        eq.filters = model::eqFilters(*pieces_[index].eq, kPlaybackRate);
        eq.ready = true;
    }
    auto run = [&eq](std::vector<float>& buffer) {
        for (float& sample : buffer) {
            double x = static_cast<double>(sample);
            for (std::size_t k = 0; k < eq.filters.size(); ++k) {
                const model::Biquad& f = eq.filters[k];
                std::array<double, 2>& z = eq.state[k];
                const double y = f.b0 * x + z[0];
                z[0] = f.b1 * x - f.a1 * y + z[1];
                z[1] = f.b2 * x - f.a2 * y;
                x = y;
            }
            sample = static_cast<float>(x);
        }
    };
    if (offset != eq.next) {
        // Not following on: start again, settled on what comes before (none at the piece's start,
        // where the filters start from silence).
        eq.state = {};
        const auto warm = static_cast<int>(std::min<std::int64_t>(offset, kPlaybackRate / 10));
        if (warm > 0) {
            std::vector<float> before = this->samples(pieces_[index], offset - warm, warm);
            run(before);
        }
    }
    run(samples);
    eq.next = offset + static_cast<std::int64_t>(samples.size());
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
    if (AudioInput* input = monitor_.load()) input->addMonitor(out, count);
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
