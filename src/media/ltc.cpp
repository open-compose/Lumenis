// LTC encoding and decoding (ltc.h), and reading a clip's audio timecode.
#include "lumenis/media/ltc.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace lumenis::media::ltc {

namespace {

// The sync word, bits 64-79 in the order sent: 0011 1111 1111 1101.
constexpr std::uint32_t kSync = 0b0011111111111101;

// Where each field sits in the 64 data bits: {first bit, width}.
struct Field {
    int bit;
    int width;
};
constexpr Field kFrameUnits{0, 4}, kFrameTens{8, 2}, kSecondUnits{16, 4}, kSecondTens{24, 3}, kMinuteUnits{32, 4},
    kMinuteTens{40, 3}, kHourUnits{48, 4}, kHourTens{56, 2};
constexpr int kDropFrameBit = 10;
// The user bit groups 1-8.
constexpr int kUserGroups[8] = {4, 12, 20, 28, 36, 44, 52, 60};

int field(std::uint64_t bits, Field f) {
    return static_cast<int>((bits >> f.bit) & ((1u << f.width) - 1u));
}

void put(std::uint64_t& bits, Field f, int value) {
    bits |= (static_cast<std::uint64_t>(value) & ((1u << f.width) - 1u)) << f.bit;
}

TimecodeFields bcd(int fu, int ft, int su, int st, int mu, int mt, int hu, int ht) {
    return {ht * 10 + hu, mt * 10 + mu, st * 10 + su, ft * 10 + fu};
}

} // namespace

TimecodeFields Frame::userTime() const {
    auto g = [this](int i) { return static_cast<int>((userBits >> (4 * i)) & 0xF); };
    return bcd(g(0), g(1), g(2), g(3), g(4), g(5), g(6), g(7));
}

std::uint32_t userBitsFor(const TimecodeFields& time) {
    const int digits[8] = {time.frames % 10, time.frames / 10, time.seconds % 10, time.seconds / 10,
                           time.minutes % 10, time.minutes / 10, time.hours % 10, time.hours / 10};
    std::uint32_t out = 0;
    for (int i = 0; i < 8; ++i) out |= static_cast<std::uint32_t>(digits[i] & 0xF) << (4 * i);
    return out;
}

Decoder::Decoder(int sampleRate, double fps) : bitLength_(sampleRate / (std::max(fps, 1.0) * 80.0)) {}

// Transitions are found by sign changes with hysteresis at a quarter of the loudest sample so far,
// so noise near zero is not taken for one.
void Decoder::feed(const float* samples, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i, ++fed_) {
        const float v = samples[i];
        peak_ = std::max(peak_ * 0.99999f, std::abs(v));
        const float threshold = peak_ * 0.25f;
        const int s = v > threshold ? 1 : v < -threshold ? -1 : 0;
        if (s == 0 || s == sign_) continue;
        // The first edge counts too: a recording that starts with LTC starts with a bit.
        transition(static_cast<double>(fed_));
        sign_ = s;
    }
}

// A transition after about a whole bit ends a 0; two after about half a bit each make a 1. Anything
// else loses the place, found again at the next sync word.
void Decoder::transition(double at) {
    const double previous = last_;
    last_ = at;
    if (previous < 0) return;
    const double interval = at - previous;
    if (interval < 0.25 * bitLength_ || interval > 1.5 * bitLength_) {
        pendingStart_ = -1;
        return;
    }
    if (interval > 0.75 * bitLength_) {
        pendingStart_ = -1;
        bit(0, previous);
    } else if (pendingStart_ < 0) {
        pendingStart_ = previous;
    } else {
        bit(1, pendingStart_);
        pendingStart_ = -1;
    }
}

void Decoder::bit(int value, double start) {
    // The bit 16 places back leaves the sync window for the data.
    const std::uint64_t leaving = (sync_ >> 15) & 1u;
    bits_ = (bits_ >> 1) | (leaving << 63);
    sync_ = ((sync_ << 1) | static_cast<std::uint32_t>(value)) & 0xFFFFu;
    starts_.push_back(start);
    if (starts_.size() > 80) starts_.erase(starts_.begin());
    if (sync_ != kSync || starts_.size() < 80) return;
    Frame f;
    f.sample = static_cast<std::int64_t>(std::llround(starts_.front()));
    f.time = bcd(field(bits_, kFrameUnits), field(bits_, kFrameTens), field(bits_, kSecondUnits), field(bits_, kSecondTens),
                 field(bits_, kMinuteUnits), field(bits_, kMinuteTens), field(bits_, kHourUnits), field(bits_, kHourTens));
    f.dropFrame = (bits_ >> kDropFrameBit) & 1u;
    for (int g = 0; g < 8; ++g) f.userBits |= static_cast<std::uint32_t>((bits_ >> kUserGroups[g]) & 0xFu) << (4 * g);
    if (f.time.hours < 24 && f.time.minutes < 60 && f.time.seconds < 60) frames_.push_back(f);
}

std::vector<float> encode(const Timecode& start, int count, int sampleRate, std::uint32_t userBits) {
    const TimecodeFormat format = start.format();
    const double bitLength = sampleRate / (format.nominalFps * 80.0);
    std::vector<float> out;
    float level = 0.5f;
    double at = 0;
    auto run = [&](double length) {
        const auto end = static_cast<std::size_t>(std::llround(at + length));
        while (out.size() < end) out.push_back(level);
        at += length;
    };
    for (int k = 0; k < count; ++k) {
        const TimecodeFields t = Timecode(start.frameCount() + k, format).fields();
        std::uint64_t bits = 0;
        put(bits, kFrameUnits, t.frames % 10);
        put(bits, kFrameTens, t.frames / 10);
        put(bits, kSecondUnits, t.seconds % 10);
        put(bits, kSecondTens, t.seconds / 10);
        put(bits, kMinuteUnits, t.minutes % 10);
        put(bits, kMinuteTens, t.minutes / 10);
        put(bits, kHourUnits, t.hours % 10);
        put(bits, kHourTens, t.hours / 10);
        if (format.dropFrame) bits |= std::uint64_t{1} << kDropFrameBit;
        for (int g = 0; g < 8; ++g) bits |= static_cast<std::uint64_t>((userBits >> (4 * g)) & 0xFu) << kUserGroups[g];
        for (int b = 0; b < 80; ++b) {
            const int value = b < 64 ? static_cast<int>((bits >> b) & 1u) : static_cast<int>((kSync >> (79 - b)) & 1u);
            level = -level;
            if (value) {
                run(bitLength / 2);
                level = -level;
                run(bitLength / 2);
            } else {
                run(bitLength);
            }
        }
    }
    return out;
}

std::optional<Timecode> readClipTimecode(const model::BinItem& clip, int track, bool userBits, bool fill, QString* error) {
    auto fail = [error](const QString& text) -> std::optional<Timecode> {
        if (error) *error = text;
        return std::nullopt;
    };
    const auto link = linkOf(clip);
    if (!link || link->unlinked) return fail(QStringLiteral("the clip has no media"));
    const auto channel = link->channelOf(track);
    const QString path = link->audioFile(track);
    if (!channel || !QFileInfo::exists(path)) return fail(QStringLiteral("A%1 has no media").arg(track + 1));
    AudioReader reader;
    QString why;
    if (!reader.open(path, channel->first, &why)) return fail(why);
    const int rate = reader.sampleRate();
    const int channels = reader.channels();
    const TimecodeFormat format = TimecodeFormat::forRate(clip.editRate, false);
    const double samplesPerFrame = rate * static_cast<double>(clip.editRate.den()) / static_cast<double>(clip.editRate.num());
    const auto first = static_cast<std::int64_t>(std::llround(
        static_cast<double>(link->first) * rate * static_cast<double>(link->rate.den()) / static_cast<double>(link->rate.num())));
    const auto total = static_cast<std::int64_t>(std::llround(samplesPerFrame * static_cast<double>(clip.durationFrames)));
    Decoder decoder(rate, format.nominalFps);
    std::vector<float> mono;
    // A second at a time, until a frame decodes.
    for (std::int64_t at = 0; at < total && decoder.frames().empty(); at += rate) {
        const int count = static_cast<int>(std::min<std::int64_t>(rate, total - at));
        const std::vector<float> block = reader.samples(first + at, count);
        mono.resize(block.size() / static_cast<std::size_t>(std::max(channels, 1)));
        for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = block[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel->second)];
        decoder.feed(mono.data(), mono.size());
        if (block.empty()) break;
    }
    if (decoder.frames().empty()) return fail(QStringLiteral("no audio timecode was found on A%1").arg(track + 1));
    const Frame& f = decoder.frames().front();
    const auto clipFrame = static_cast<std::int64_t>(std::llround(static_cast<double>(f.sample) / samplesPerFrame));
    if (clipFrame != 0 && !fill) return fail(QStringLiteral("the clip's first frame has no audio timecode"));
    const TimecodeFormat labelled = TimecodeFormat::forRate(clip.editRate, f.dropFrame && format.nominalFps % 30 == 0);
    const Timecode at = Timecode::fromFields(userBits ? f.userTime() : f.time, labelled);
    return Timecode(at.frameCount() - clipFrame, labelled);
}

} // namespace lumenis::media::ltc
