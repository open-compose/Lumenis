#include "lumenis/media/time_stretch.h"

#include "lumenis/media/media.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

// The WSOLA time stretch, transient-cut stretching and varispeed (time_stretch.h; docs/spec/time-stretch.md).
namespace lumenis::media {

namespace {

std::size_t lengthOf(const std::vector<std::vector<float>>& channels) {
    return channels.empty() ? 0 : channels.front().size();
}

// The channels' sum, which the joins are found on (a stereo pair moves together).
std::vector<float> mono(const std::vector<std::vector<float>>& channels) {
    std::vector<float> sum(lengthOf(channels), 0.0f);
    for (const auto& c : channels) {
        for (std::size_t i = 0; i < sum.size() && i < c.size(); ++i) sum[i] += c[i];
    }
    return sum;
}

// Auto's crossfade: 30 ms for small changes in length down to 10 ms at four times or a quarter (ours), as the guide
// says: "small narrow-range time (length) changes require longer crossfades while larger changes in length require
// shorter crossfades" (p.857).
double crossfadeMs(const StretchOptions& o, double ratio) {
    if (o.crossfadeMs > 0) return std::clamp(o.crossfadeMs, 1.0, 200.0);
    return std::clamp(30.0 - 10.0 * std::abs(std::log2(ratio)), 10.0, 30.0);
}

// The normalised cross-correlation of `a` and `b` over `n` samples, every other one (enough to tell the best join).
double similarity(const float* a, const float* b, std::size_t n) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t i = 0; i < n; i += 2) {
        const double x = static_cast<double>(a[i]);
        const double y = static_cast<double>(b[i]);
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }
    const double norm = std::sqrt(aa * bb);
    return norm > 1e-12 ? ab / norm : 0.0;
}

} // namespace

std::vector<std::vector<float>> timeStretch(const std::vector<std::vector<float>>& channels, double ratio,
                                            const StretchOptions& o) {
    ratio = std::clamp(ratio, 0.25, 4.0);
    const std::size_t n = lengthOf(channels);
    if (n == 0 || std::abs(ratio - 1.0) < 1e-9) return channels;
    const auto outLength = static_cast<std::size_t>(std::llround(static_cast<double>(n) * ratio));
    const int rate = std::max(o.sampleRate, 1);
    // The window is two crossfades long; the next one starts a crossfade later in the output (the synthesis hop).
    const auto overlap = static_cast<std::size_t>(std::max(16.0, crossfadeMs(o, ratio) * rate / 1000.0));
    const std::size_t window = 2 * overlap;
    const std::size_t hop = overlap;
    // How far a window may move from its place: one period of the lowest pitch, less towards rhythm.
    const double period = rate / std::clamp(o.minPitchHz, 40.0, 1000.0);
    const auto reach = static_cast<std::ptrdiff_t>(
        std::max(1.0, std::round(period * (0.25 + 0.75 * std::clamp(o.accuracy, 0.0, 1.0)))));
    // The input with silence after it, so every window can be read whole.
    const std::size_t padded = n + window + static_cast<std::size_t>(reach) + 1;
    std::vector<std::vector<float>> in;
    in.reserve(channels.size());
    for (const auto& c : channels) {
        std::vector<float> p(padded, 0.0f);
        std::copy(c.begin(), c.end(), p.begin());
        in.push_back(std::move(p));
    }
    std::vector<float> guide = mono(channels);
    guide.resize(padded, 0.0f);
    // A periodic Hann window: windows a hop apart add up to one.
    std::vector<float> hann(window);
    for (std::size_t i = 0; i < window; ++i) {
        hann[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(window)));
    }
    std::vector<std::vector<float>> out(channels.size(), std::vector<float>(outLength + window, 0.0f));
    std::vector<float> weight(outLength + window, 0.0f);
    const auto last = static_cast<std::ptrdiff_t>(n);
    std::ptrdiff_t previous = 0; // where the last window was read from
    for (std::size_t at = 0; at < outLength; at += hop) {
        std::ptrdiff_t from = 0;
        if (at > 0) {
            // Where the stretch puts this window, and where the last one carries on to: the window is read from the
            // place near the first whose start sounds most like the second.
            const auto nominal = static_cast<std::ptrdiff_t>(std::llround(static_cast<double>(at) / ratio));
            const std::ptrdiff_t natural = previous + static_cast<std::ptrdiff_t>(hop);
            const std::ptrdiff_t lo = std::max<std::ptrdiff_t>(0, nominal - reach);
            const std::ptrdiff_t hi = std::min<std::ptrdiff_t>(last, nominal + reach);
            double best = -2.0;
            from = std::clamp<std::ptrdiff_t>(nominal, 0, last);
            if (natural >= 0 && natural < last) {
                for (std::ptrdiff_t c = lo; c <= hi; ++c) {
                    const double s = similarity(guide.data() + c, guide.data() + natural, overlap);
                    if (s > best) {
                        best = s;
                        from = c;
                    }
                }
            }
        }
        for (std::size_t ch = 0; ch < channels.size(); ++ch) {
            const float* src = in[ch].data() + from;
            float* dst = out[ch].data() + at;
            for (std::size_t i = 0; i < window; ++i) dst[i] += hann[i] * src[i];
        }
        for (std::size_t i = 0; i < window; ++i) weight[at + i] += hann[i];
        previous = from;
    }
    // Divided by the windows' sum, which is one but at the very start (the first half window alone).
    for (std::size_t i = 0; i < outLength; ++i) {
        const float w = weight[i];
        if (w > 1e-3f) {
            for (auto& c : out) c[i] /= w;
        }
    }
    for (auto& c : out) c.resize(outLength);
    return out;
}

std::vector<std::vector<float>> rhythmicStretch(const std::vector<std::vector<float>>& channels, double ratio,
                                                double thresholdDb, double decayMs, const StretchOptions& o) {
    ratio = std::clamp(ratio, 0.25, 4.0);
    const std::size_t n = lengthOf(channels);
    if (n == 0 || std::abs(ratio - 1.0) < 1e-9) return channels;
    const int rate = std::max(o.sampleRate, 1);
    // The level in 5 ms blocks; a transient is a block 6 dB or more above the one before, loud enough to count.
    const auto block = static_cast<std::size_t>(std::max(1, rate / 200));
    const std::vector<float> sum = mono(channels);
    std::vector<double> level;
    for (std::size_t at = 0; at < n; at += block) {
        double e = 0;
        const std::size_t end = std::min(n, at + block);
        for (std::size_t i = at; i < end; ++i) e += static_cast<double>(sum[i]) * static_cast<double>(sum[i]);
        level.push_back(10.0 * std::log10(e / static_cast<double>(end - at) + 1e-12));
    }
    const double loudest = level.empty() ? -120.0 : *std::max_element(level.begin(), level.end());
    const double floor = loudest + std::clamp(thresholdDb, -40.0, 0.0);
    std::vector<std::size_t> cuts{0};
    const std::size_t spacing = static_cast<std::size_t>(rate) * 30 / 1000; // ours: transients at least 30 ms apart
    for (std::size_t b = 1; b < level.size(); ++b) {
        if (level[b] >= floor && level[b] - level[b - 1] >= 6.0 && b * block - cuts.back() >= spacing) cuts.push_back(b * block);
    }
    if (cuts.size() < 2) return timeStretch(channels, ratio, o);
    cuts.push_back(n);
    const auto outLength = static_cast<std::size_t>(std::llround(static_cast<double>(n) * ratio));
    std::vector<std::vector<float>> out(channels.size(), std::vector<float>(outLength, 0.0f));
    const auto decay = static_cast<std::size_t>(std::max(1.0, std::clamp(decayMs, 1.0, 1000.0) * rate / 1000.0));
    for (std::size_t p = 0; p + 1 < cuts.size(); ++p) {
        const auto start = static_cast<std::size_t>(std::llround(static_cast<double>(cuts[p]) * ratio));
        const auto next = std::min(outLength, static_cast<std::size_t>(std::llround(static_cast<double>(cuts[p + 1]) * ratio)));
        if (start >= next) continue;
        const std::size_t room = next - start;
        const std::size_t piece = cuts[p + 1] - cuts[p];
        const std::size_t count = std::min(room, piece);
        // A piece cut short fades out over the decay at the end of its place.
        const std::size_t fade = piece > room ? std::min(decay, count) : 0;
        for (std::size_t ch = 0; ch < channels.size(); ++ch) {
            for (std::size_t i = 0; i < count; ++i) {
                float g = 1.0f;
                if (fade > 0 && i >= count - fade) g = static_cast<float>(count - i) / static_cast<float>(fade);
                out[ch][start + i] = channels[ch][cuts[p] + i] * g;
            }
        }
    }
    return out;
}

std::vector<std::vector<float>> varispeed(const std::vector<std::vector<float>>& channels, double factor, int sampleRate) {
    const std::size_t n = lengthOf(channels);
    if (n == 0 || factor <= 0 || std::abs(factor - 1.0) < 1e-9) return channels;
    // Read as if recorded `factor` times faster, written at the rate: shorter by the factor, higher by it.
    const auto from = static_cast<int>(std::lround(static_cast<double>(sampleRate) * factor));
    const auto outLength = static_cast<std::size_t>(std::llround(static_cast<double>(n) / factor));
    std::vector<std::vector<float>> out;
    for (const auto& c : channels) {
        Resampler r(1, from, sampleRate, 64);
        std::vector<float> made;
        if (r.isValid()) {
            made = r.push(c.data(), static_cast<int>(c.size()));
            const std::vector<float> rest = r.flush();
            made.insert(made.end(), rest.begin(), rest.end());
        }
        made.resize(outLength, 0.0f);
        out.push_back(std::move(made));
    }
    return out;
}

std::vector<std::vector<float>> pitchShift(const std::vector<std::vector<float>>& channels, double semitones,
                                           bool timeCorrection, const StretchOptions& o, double lengthRatio) {
    const double factor = std::pow(2.0, std::clamp(semitones, -24.0, 24.0) / 12.0);
    if (!timeCorrection) return varispeed(channels, factor, o.sampleRate);
    if (std::abs(factor - 1.0) < 1e-9) return timeStretch(channels, lengthRatio, o);
    // Longer by the factor with the pitch kept, then played that much faster: the pitch moves, the length is back. The
    // stretch may go past timeStretch's 4.0 (two octaves up with Time Shift's own length): done in two steps then.
    double stretch = factor * lengthRatio;
    std::vector<std::vector<float>> s = channels;
    while (stretch > 4.0 + 1e-9 || stretch < 0.25 - 1e-9) {
        const double step = stretch > 1.0 ? 4.0 : 0.25;
        s = timeStretch(s, step, o);
        stretch /= step;
    }
    s = timeStretch(s, stretch, o);
    return varispeed(s, factor, o.sampleRate);
}

} // namespace lumenis::media
