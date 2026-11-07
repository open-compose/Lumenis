// Syncing clips by their sound (lumenis/media/waveform_sync.h): loudness envelopes cross-correlated with a radix-2 FFT.
#include "lumenis/media/waveform_sync.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace lumenis::media {

namespace {

constexpr int kEnvelopeRate = 1000; // envelope steps per second
// How much of each clip is compared (ours): 20 minutes keeps the FFT within 4M points (about 130 MB).
constexpr double kSyncSeconds = 1200.0;

// The mean absolute level of each 1 ms of `samples`, less the envelope's average.
std::vector<double> envelope(const std::vector<float>& samples, int rate) {
    std::vector<double> out;
    if (rate <= 0) return out;
    const double step = static_cast<double>(rate) / kEnvelopeRate;
    const auto n = static_cast<std::size_t>(static_cast<double>(samples.size()) / step);
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto from = static_cast<std::size_t>(static_cast<double>(i) * step);
        const auto to = std::min(samples.size(), static_cast<std::size_t>(static_cast<double>(i + 1) * step));
        double sum = 0.0;
        for (std::size_t k = from; k < to; ++k) sum += std::abs(static_cast<double>(samples[k]));
        out[i] = to > from ? sum / static_cast<double>(to - from) : 0.0;
    }
    double mean = 0.0;
    for (const double v : out) mean += v;
    if (!out.empty()) mean /= static_cast<double>(out.size());
    for (double& v : out) v -= mean;
    return out;
}

// In-place iterative radix-2 FFT; `inverse` without the 1/n scale.
void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * std::numbers::pi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> wn(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * wn;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                wn *= w;
            }
        }
    }
}

double energy(const std::vector<double>& e) {
    double sum = 0.0;
    for (const double v : e) sum += v * v;
    return sum;
}

} // namespace

std::optional<double> soundLag(const std::vector<float>& a, int rateA, const std::vector<float>& b, int rateB, double maxLag) {
    const std::vector<double> ea = envelope(a, rateA);
    const std::vector<double> eb = envelope(b, rateB);
    const double na = energy(ea);
    const double nb = energy(eb);
    if (ea.empty() || eb.empty() || na <= 1e-12 || nb <= 1e-12) return std::nullopt;
    std::size_t n = 1;
    while (n < ea.size() + eb.size()) n <<= 1;
    std::vector<std::complex<double>> fa(n);
    std::vector<std::complex<double>> fb(n);
    for (std::size_t i = 0; i < ea.size(); ++i) fa[i] = ea[i];
    for (std::size_t i = 0; i < eb.size(); ++i) fb[i] = eb[i];
    fft(fa, false);
    fft(fb, false);
    // c[k] = sum over i of a[i] b[i + k]: the inverse of conj(A) B.
    for (std::size_t i = 0; i < n; ++i) fa[i] = std::conj(fa[i]) * fb[i];
    fft(fa, true);
    const auto limit = static_cast<std::int64_t>(maxLag > 0.0 ? maxLag * kEnvelopeRate : static_cast<double>(n));
    double best = 0.0;
    std::optional<std::int64_t> bestLag;
    for (std::size_t k = 0; k < n; ++k) {
        // Lags from -(|a| - 1) to |b| - 1; the rest of the circle is empty.
        const auto lag = k < eb.size() ? static_cast<std::int64_t>(k) : static_cast<std::int64_t>(k) - static_cast<std::int64_t>(n);
        if (lag < -static_cast<std::int64_t>(ea.size()) + 1 || std::llabs(lag) > limit) continue;
        const double v = fa[k].real() / static_cast<double>(n);
        if (v > best) {
            best = v;
            bestLag = lag;
        }
    }
    // A match must explain a fair part of both sounds' loudness changes (ours: a correlation of at least 0.2).
    if (!bestLag || best / std::sqrt(na * nb) < 0.2) return std::nullopt;
    return static_cast<double>(*bestLag) / kEnvelopeRate;
}

ClipSound clipSound(const model::BinItem& clip, double seconds, QString* error) {
    ClipSound out;
    const auto link = linkOf(clip);
    const auto channel = link ? link->channelOf(0) : std::nullopt;
    if (!link || link->unlinked || !channel) {
        if (error) *error = QStringLiteral("\"%1\" has no sound.").arg(clip.name);
        return out;
    }
    AudioReader reader;
    if (!reader.open(link->audioFile(0), channel->first, error)) return out;
    out.rate = reader.sampleRate();
    if (out.rate <= 0 || channel->second >= reader.channels()) return out;
    // Where the clip's first frame is in the file.
    const double fileRate = link->rate.toDouble();
    const auto start = fileRate > 0.0 ? static_cast<std::int64_t>(std::llround(static_cast<double>(link->first) / fileRate * out.rate)) : 0;
    const auto total = static_cast<std::int64_t>(seconds * out.rate);
    const int chunk = out.rate; // a second at a time
    for (std::int64_t at = std::max<std::int64_t>(start, 0); static_cast<std::int64_t>(out.samples.size()) < total;) {
        const std::vector<float> got = reader.samples(at, chunk);
        const int frames = static_cast<int>(got.size()) / reader.channels();
        if (frames <= 0) break;
        for (int i = 0; i < frames; ++i) out.samples.push_back(got[static_cast<std::size_t>(i * reader.channels() + channel->second)]);
        at += frames;
    }
    return out;
}

std::optional<std::int64_t> waveformOffset(const model::BinItem& a, const model::BinItem& b, QString* error) {
    const ClipSound sa = clipSound(a, kSyncSeconds, error);
    if (sa.samples.empty()) return std::nullopt;
    const ClipSound sb = clipSound(b, kSyncSeconds, error);
    if (sb.samples.empty()) return std::nullopt;
    const auto lag = soundLag(sa.samples, sa.rate, sb.samples, sb.rate);
    if (!lag) {
        if (error) *error = QStringLiteral("The sounds of \"%1\" and \"%2\" do not match.").arg(a.name, b.name);
        return std::nullopt;
    }
    return static_cast<std::int64_t>(std::llround(*lag * a.editRate.toDouble()));
}

} // namespace lumenis::media
