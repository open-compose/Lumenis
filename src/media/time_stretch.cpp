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

namespace {

// The sound's period in samples about `centre`: the lag with the highest normalised autocorrelation between the lags of
// 1000 Hz and `minPitchHz`, over a window of twice the longest; 0 where none reaches 0.5 (unvoiced, or silence).
double periodAt(const std::vector<float>& m, std::size_t centre, int rate, double minPitchHz) {
    const auto shortest = static_cast<std::size_t>(std::max(2, rate / 1000));
    const auto longest = static_cast<std::size_t>(std::max<double>(static_cast<double>(shortest) + 1, rate / std::max(minPitchHz, 20.0)));
    const std::size_t span = 2 * longest;
    if (m.size() < span + longest) return 0;
    const std::size_t start = std::min(centre > longest ? centre - longest : 0, m.size() - span - longest);
    const float* a = m.data() + start;
    double best = 0.5;
    std::size_t lag = 0;
    for (std::size_t l = shortest; l <= longest; ++l) {
        const double c = similarity(a, a + l, span);
        if (c > best) {
            best = c;
            lag = l;
        }
    }
    // The shortest lag near the best one, so a period is not taken for two (the correlation repeats at multiples).
    if (lag != 0) {
        for (std::size_t d = 2; d <= 4; ++d) {
            const std::size_t l = lag / d;
            if (l >= shortest && similarity(a, a + l, span) > 0.9 * best) return static_cast<double>(l);
        }
    }
    return static_cast<double>(lag);
}

} // namespace

std::vector<std::vector<float>> formantPitchShift(const std::vector<std::vector<float>>& channels, double semitones,
                                                  double formantSemitones, double lengthRatio, const StretchOptions& o) {
    const double alpha = std::pow(2.0, std::clamp(semitones, -24.0, 24.0) / 12.0);       // the pitch's factor
    const double beta = std::pow(2.0, std::clamp(formantSemitones, -24.0, 24.0) / 12.0); // the formants'
    lengthRatio = std::clamp(lengthRatio, 0.25, 4.0);
    const std::size_t n = lengthOf(channels);
    if (n == 0) return channels;
    if (std::abs(alpha - 1.0) < 1e-9 && std::abs(beta - 1.0) < 1e-9) return timeStretch(channels, lengthRatio, o);
    const int rate = std::max(o.sampleRate, 1);
    const std::vector<float> m = mono(channels);
    // The period every 5 ms, unvoiced stretches taking 10 ms.
    const auto hop = static_cast<std::size_t>(std::max(1, rate / 200));
    const double unvoiced = rate / 100.0;
    std::vector<double> periods;
    for (std::size_t at = 0; at < n; at += hop) {
        const double p = periodAt(m, at, rate, o.minPitchHz);
        periods.push_back(p > 0 ? p : unvoiced);
    }
    auto periodOf = [&](double t) {
        const auto i = static_cast<std::size_t>(std::clamp(t / static_cast<double>(hop), 0.0, static_cast<double>(periods.size() - 1)));
        return periods[i];
    };
    // The analysis marks, a period apart.
    std::vector<double> marks;
    for (double t = 0; t < static_cast<double>(n); t += periodOf(t)) marks.push_back(t);
    const auto outLength = static_cast<std::size_t>(std::llround(static_cast<double>(n) * lengthRatio));
    std::vector<std::vector<float>> out(channels.size(), std::vector<float>(outLength, 0.0f));
    std::vector<float> weight(outLength, 0.0f);
    std::size_t k = 0;
    for (double ts = 0; ts < static_cast<double>(outLength);) {
        // The mark nearest where this point of the output comes from.
        const double ti = ts / lengthRatio;
        while (k + 1 < marks.size() && std::abs(marks[k + 1] - ti) <= std::abs(marks[k] - ti)) ++k;
        const double mark = marks[k];
        const double period = periodOf(mark);
        // The grain, two periods long in the input, `beta` times shorter in the output.
        const double half = period / beta;
        const auto from = static_cast<std::int64_t>(std::ceil(ts - half));
        const auto to = static_cast<std::int64_t>(std::floor(ts + half));
        for (std::int64_t i = std::max<std::int64_t>(from, 0); i <= to && i < static_cast<std::int64_t>(outLength); ++i) {
            const double u = (static_cast<double>(i) - ts) * beta; // input samples from the mark
            const double w = 0.5 * (1.0 + std::cos(std::numbers::pi * u / period));
            const double x = mark + u;
            if (x < 0 || x >= static_cast<double>(n - 1)) continue;
            const auto x0 = static_cast<std::size_t>(x);
            const auto f = static_cast<float>(x - static_cast<double>(x0));
            const auto wf = static_cast<float>(w);
            for (std::size_t c = 0; c < channels.size(); ++c) {
                const std::vector<float>& in = channels[c];
                out[c][static_cast<std::size_t>(i)] += wf * (in[x0] + f * (in[x0 + 1] - in[x0]));
            }
            weight[static_cast<std::size_t>(i)] += wf;
        }
        // The next grain a period of the new pitch later.
        ts += period / alpha;
    }
    // The grains' windows add up to about the pitch factor where they overlap; dividing by their sum keeps the level.
    // Thin spots (grains further apart than they are long) are not raised more than twice.
    for (std::size_t i = 0; i < outLength; ++i) {
        const float g = 1.0f / std::max(weight[i], 0.5f);
        for (auto& c : out) c[i] *= g;
    }
    return out;
}

std::vector<std::vector<float>> followEnvelope(const std::vector<std::vector<float>>& original,
                                               std::vector<std::vector<float>> stretched, double ratio, int sampleRate) {
    const std::size_t n = lengthOf(original);
    const std::size_t out = lengthOf(stretched);
    if (n == 0 || out == 0 || ratio <= 0) return stretched;
    const auto hop = static_cast<std::size_t>(std::max(1, sampleRate / 200));
    // The level (RMS of the channels' sum) over two hops about each point every `hop`.
    auto levels = [&](const std::vector<float>& m) {
        std::vector<double> l;
        for (std::size_t at = 0; at < m.size(); at += hop) {
            const std::size_t a = at > hop ? at - hop : 0;
            const std::size_t b = std::min(m.size(), at + hop);
            double sum = 0;
            for (std::size_t i = a; i < b; ++i) sum += static_cast<double>(m[i]) * static_cast<double>(m[i]);
            l.push_back(std::sqrt(sum / static_cast<double>(std::max<std::size_t>(b - a, 1))));
        }
        return l;
    };
    const std::vector<double> was = levels(mono(original));
    const std::vector<double> now = levels(mono(stretched));
    auto at = [](const std::vector<double>& l, double index) {
        index = std::clamp(index, 0.0, static_cast<double>(l.size() - 1));
        const auto i = static_cast<std::size_t>(index);
        const double f = index - static_cast<double>(i);
        return i + 1 < l.size() ? l[i] + f * (l[i + 1] - l[i]) : l[i];
    };
    // The gain at each of the stretched sound's points: the original's level where it came from over its own.
    std::vector<double> gains(now.size(), 1.0);
    for (std::size_t p = 0; p < now.size(); ++p) {
        const double target = at(was, static_cast<double>(p) / ratio);
        gains[p] = now[p] > 1e-6 ? std::clamp(target / now[p], 0.0, 4.0) : 1.0;
    }
    for (std::size_t i = 0; i < out; ++i) {
        const auto g = static_cast<float>(at(gains, static_cast<double>(i) / static_cast<double>(hop)));
        for (auto& c : stretched) c[i] *= g;
    }
    return stretched;
}

} // namespace lumenis::media
