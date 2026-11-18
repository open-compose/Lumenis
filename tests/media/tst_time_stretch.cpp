// The time-stretch engine (media/time_stretch.h; Editing Guide p.850-860; docs/spec/time-stretch.md): WSOLA keeps a
// tone's pitch and makes no clicks at its joins, stretches a stereo pair together, cuts rhythmic material at its
// transients, pitch shifts with and without Time Correction, and the AudioSuite processes on it (Pitch Shift, Time
// Compression Expansion, Time Shift) change the length as they should.
#include "lumenis/media/audio_suite.h"
#include "lumenis/media/time_stretch.h"

#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kRate = 48000;

std::vector<float> sine(std::size_t count, double frequency, double amplitude = 0.5) {
    std::vector<float> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(i) / kRate));
    }
    return v;
}

// The frequency from the upward zero crossings of the middle half (away from the ends' half windows).
double frequencyOf(const std::vector<float>& v) {
    const std::size_t from = v.size() / 4;
    const std::size_t to = 3 * v.size() / 4;
    std::size_t first = 0, last = 0, count = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        if (v[i - 1] < 0 && v[i] >= 0) {
            if (count == 0) first = i;
            last = i;
            ++count;
        }
    }
    return count < 2 ? 0.0 : static_cast<double>(count - 1) * kRate / static_cast<double>(last - first);
}

// The share of the middle half's energy at `frequency` (a windowed Goertzel), 1 for a pure tone.
double purity(const std::vector<float>& v, double frequency) {
    const std::size_t from = v.size() / 4;
    const std::size_t n = v.size() / 2;
    double re = 0, im = 0, energy = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
        const double x = static_cast<double>(v[from + i]) * w;
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / kRate;
        re += x * std::cos(phase);
        im -= x * std::sin(phase);
        energy += x * x;
    }
    // A Hann-windowed tone A sin: its bin is A n / 4, its energy 3 A^2 n / 16, so |bin|^2 / energy = n / 3.
    return energy > 0 ? (re * re + im * im) / (energy * static_cast<double>(n) / 3.0) : 0.0;
}

// The largest step from one sample to the next: a click at a join would be far above the tone's own.
// A voice-like sound: a pulse train at `pitch` Hz through two resonances (formants) at `f1` and 2.5 times it, each
// pulse ringing as a decaying sine.
std::vector<float> voice(std::size_t count, double pitch, double f1) {
    std::vector<float> v(count, 0.0f);
    const auto period = static_cast<std::size_t>(std::lround(kRate / pitch));
    for (std::size_t at = 0; at < count; at += period) {
        for (std::size_t i = 0; i < 960 && at + i < count; ++i) {
            const double t = static_cast<double>(i) / kRate;
            const double ring = std::exp(-t / 0.002);
            v[at + i] += static_cast<float>(0.3 * ring * (std::sin(2.0 * std::numbers::pi * f1 * t) +
                                                          0.5 * std::sin(2.0 * std::numbers::pi * 2.5 * f1 * t)));
        }
    }
    return v;
}

// The pitch by autocorrelation over the middle 8192 samples: the shortest lag within 5% of the best between 60 and
// 500 Hz.
double pitchOf(const std::vector<float>& v) {
    const std::size_t n = 8192;
    const std::size_t from = v.size() / 2 - n / 2;
    auto corr = [&](std::size_t lag) {
        double ab = 0, aa = 0, bb = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double a = static_cast<double>(v[from + i]);
            const double b = static_cast<double>(v[from + i + lag]);
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        return ab / std::sqrt(aa * bb + 1e-30);
    };
    const auto shortest = static_cast<std::size_t>(kRate / 500);
    const auto longest = static_cast<std::size_t>(kRate / 60);
    std::vector<double> c(longest + 1, 0.0);
    double best = 0;
    for (std::size_t l = shortest; l <= longest; ++l) best = std::max(best, c[l] = corr(l));
    for (std::size_t l = shortest + 1; l < longest; ++l) {
        if (c[l] >= 0.95 * best && c[l] >= c[l - 1] && c[l] >= c[l + 1]) return kRate / static_cast<double>(l);
    }
    return 0;
}

// The strongest frequency between 300 Hz and 1.8 kHz (the first formant), from a Hann-windowed DFT of the middle 8192
// samples in 10 Hz steps.
double formantOf(const std::vector<float>& v) {
    const std::size_t n = 8192;
    const std::size_t from = v.size() / 2 - n / 2;
    double best = 0, at = 0;
    for (double f = 300; f <= 1800; f += 10) {
        double re = 0, im = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
            const double ph = 2.0 * std::numbers::pi * f * static_cast<double>(i) / kRate;
            re += w * static_cast<double>(v[from + i]) * std::cos(ph);
            im += w * static_cast<double>(v[from + i]) * std::sin(ph);
        }
        const double m = re * re + im * im;
        if (m > best) {
            best = m;
            at = f;
        }
    }
    return at;
}

// The level (RMS) over each 10 ms of the sound.
std::vector<double> envelopeOf(const std::vector<float>& v) {
    const std::size_t block = kRate / 100;
    std::vector<double> e;
    for (std::size_t at = 0; at + block <= v.size(); at += block) {
        double sum = 0;
        for (std::size_t i = 0; i < block; ++i) sum += static_cast<double>(v[at + i]) * static_cast<double>(v[at + i]);
        e.push_back(std::sqrt(sum / static_cast<double>(block)));
    }
    return e;
}

double largestStep(const std::vector<float>& v) {
    double m = 0;
    for (std::size_t i = v.size() / 8 + 1; i < 7 * v.size() / 8; ++i) m = std::max(m, std::abs(static_cast<double>(v[i] - v[i - 1])));
    return m;
}

} // namespace

class TestTimeStretch : public QObject {
    Q_OBJECT

private slots:
    // Longer and shorter, the pitch kept, the joins smooth (p.856: "without changing pitch").
    void stretchKeepsPitch() {
        const std::vector<float> in = sine(kRate, 440.0);
        const double ownStep = largestStep(in);
        for (const double ratio : {1.5, 0.6, 2.0, 0.25, 4.0}) {
            const auto out = timeStretch({in}, ratio);
            QCOMPARE(out.size(), std::size_t{1});
            QCOMPARE(static_cast<double>(out[0].size()), std::round(kRate * ratio));
            QVERIFY2(std::abs(frequencyOf(out[0]) - 440.0) < 2.0, qPrintable(QString::number(frequencyOf(out[0]))));
            QVERIFY2(purity(out[0], 440.0) > 0.9, qPrintable(QStringLiteral("%1: %2").arg(ratio).arg(purity(out[0], 440.0))));
            QVERIFY2(largestStep(out[0]) < 1.3 * ownStep, qPrintable(QString::number(largestStep(out[0]) / ownStep)));
            qInfo("ratio %.2f: purity %.4f, largest step %.3f of the tone's", ratio, purity(out[0], 440.0),
                  largestStep(out[0]) / ownStep);
        }
        // Without room to find the joins (one 1000 Hz period, towards rhythm) a 440 Hz tone's periods do not line up:
        // the measures above tell the difference.
        StretchOptions blind;
        blind.minPitchHz = 1000;
        blind.accuracy = 0;
        const auto rough = timeStretch({in}, 1.5, blind);
        qInfo("without the search: purity %.4f, largest step %.3f", purity(rough[0], 440.0), largestStep(rough[0]) / ownStep);
        QVERIFY(purity(rough[0], 440.0) < purity(timeStretch({in}, 1.5)[0], 440.0));
        // Out of the guide's range it is held to it (0.25 to 4.0, p.857); 1 copies.
        QCOMPARE(timeStretch({in}, 10.0)[0].size(), std::size_t{4 * kRate});
        QCOMPARE(timeStretch({in}, 1.0)[0], in);
    }

    // A stereo pair moves together: the sides keep their relation sample for sample (p.856).
    void stereoTogether() {
        const std::vector<float> left = sine(kRate / 2, 330.0);
        std::vector<float> right = left;
        for (float& s : right) s *= 0.5f;
        const auto out = timeStretch({left, right}, 1.7);
        QCOMPARE(out.size(), std::size_t{2});
        for (std::size_t i = 0; i < out[0].size(); ++i) QVERIFY(std::abs(out[1][i] - 0.5f * out[0][i]) < 1e-5f);
    }

    // Pitch Shift: with Time Correction the length stays and the pitch moves; without, tape varispeed (p.850-851).
    void pitchShifting() {
        const std::vector<float> in = sine(kRate, 440.0);
        auto up = pitchShift({in}, 12.0, true);
        QVERIFY(std::abs(static_cast<double>(up[0].size()) - kRate) <= 1.0);
        QVERIFY2(std::abs(frequencyOf(up[0]) - 880.0) < 4.0, qPrintable(QString::number(frequencyOf(up[0]))));
        QVERIFY(purity(up[0], 880.0) > 0.85);
        auto down = pitchShift({in}, -5.0, true);
        const double fourth = 440.0 * std::pow(2.0, -5.0 / 12.0);
        QVERIFY2(std::abs(frequencyOf(down[0]) - fourth) < 2.0, qPrintable(QString::number(frequencyOf(down[0]))));
        auto tape = pitchShift({in}, 12.0, false);
        QCOMPARE(tape[0].size(), std::size_t{kRate / 2});
        QVERIFY(std::abs(frequencyOf(tape[0]) - 880.0) < 2.0);
        // Two octaves up with the length doubled besides (Time Shift's Time and Pitch together): over 4 in two steps.
        auto both = pitchShift({in}, 24.0, true, {}, 2.0);
        QVERIFY(std::abs(static_cast<double>(both[0].size()) - 2.0 * kRate) <= 2.0);
        QVERIFY2(std::abs(frequencyOf(both[0]) - 1760.0) < 8.0, qPrintable(QString::number(frequencyOf(both[0]))));
    }

    // Rhythmic: hits a quarter second apart, stretched twice, come half a second apart, as sharp as they were.
    void rhythmicKeepsHits() {
        std::vector<float> in(2 * kRate, 0.0f);
        for (int hit = 0; hit < 8; ++hit) {
            const std::size_t at = static_cast<std::size_t>(hit) * kRate / 4;
            for (std::size_t i = 0; i < 480; ++i) in[at + i] = static_cast<float>(0.8 * std::exp(-static_cast<double>(i) / 100.0));
        }
        const auto out = rhythmicStretch({in}, 2.0, -6.0, 100.0);
        QCOMPARE(out[0].size(), std::size_t{4 * kRate});
        for (int hit = 0; hit < 8; ++hit) {
            const std::size_t at = static_cast<std::size_t>(hit) * kRate / 2;
            QVERIFY2(std::abs(out[0][at] - 0.8f) < 1e-4f, qPrintable(QStringLiteral("hit %1: %2").arg(hit).arg(static_cast<double>(out[0][at]))));
            QVERIFY(std::abs(out[0][at + kRate / 4]) < 1e-4f); // silence where the stretch left room
        }
        // Squeezed to half, a piece longer than its place fades out before the next hit.
        const auto half = rhythmicStretch({in}, 0.5, -6.0, 5.0);
        QCOMPARE(half[0].size(), std::size_t{kRate});
        QVERIFY(std::abs(half[0][kRate / 8] - 0.8f) < 1e-4f);
    }

    // Time Shift's Monophonic mode moves the pitch formant-correct, and its Formant control moves the formants without
    // the pitch (p.859-860).
    void formants() {
        const std::vector<float> in = voice(kRate, 150.0, 800.0);
        QVERIFY(std::abs(pitchOf(in) - 150.0) < 3.0);
        QVERIFY(std::abs(formantOf(in) - 800.0) <= 80.0);
        StretchOptions o;
        o.minPitchHz = 80;
        // Up a fourth: the pitch moves, the formant stays (a resampled pitch shift takes the formant with it).
        const auto up = formantPitchShift({in}, 5.0, 0.0, 1.0, o);
        QVERIFY(std::abs(static_cast<double>(up[0].size()) - kRate) <= 1.0);
        const double fourth = 150.0 * std::pow(2.0, 5.0 / 12.0);
        QVERIFY2(std::abs(pitchOf(up[0]) - fourth) < 6.0, qPrintable(QString::number(pitchOf(up[0]))));
        QVERIFY2(std::abs(formantOf(up[0]) - 800.0) <= 110.0, qPrintable(QString::number(formantOf(up[0]))));
        const auto resampled = pitchShift({in}, 5.0, true, o);
        QVERIFY2(formantOf(resampled[0]) > 950.0, qPrintable(QString::number(formantOf(resampled[0]))));
        // The formants an octave down, the pitch kept.
        const auto down = formantPitchShift({in}, 0.0, -12.0, 1.0, o);
        QVERIFY2(std::abs(pitchOf(down[0]) - 150.0) < 4.0, qPrintable(QString::number(pitchOf(down[0]))));
        QVERIFY2(std::abs(formantOf(down[0]) - 400.0) <= 90.0, qPrintable(QString::number(formantOf(down[0]))));
        // With the length: twice as long, the pitch moved.
        const auto longer = formantPitchShift({in}, -5.0, 0.0, 2.0, o);
        QVERIFY(std::abs(static_cast<double>(longer[0].size()) - 2.0 * kRate) <= 2.0);
        QVERIFY2(std::abs(pitchOf(longer[0]) - 150.0 / std::pow(2.0, 5.0 / 12.0)) < 4.0, qPrintable(QString::number(pitchOf(longer[0]))));
        // Through the process: Monophonic with a Formant shift, the pitch kept.
        std::vector<std::vector<float>> c{in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"),
                                  {{QStringLiteral("mode"), 0.0}, {QStringLiteral("range"), 1.0}, {QStringLiteral("formant"), -12.0}}, c));
        QVERIFY(std::abs(pitchOf(c[0]) - 150.0) < 4.0);
        QVERIFY(formantOf(c[0]) < 550.0);
    }

    // Follow (Polyphonic, p.860): decaying hits stretched twice keep their decays' shape.
    void follow() {
        std::vector<float> in(kRate, 0.0f);
        for (int hit = 0; hit < 4; ++hit) {
            const std::size_t at = static_cast<std::size_t>(hit) * kRate / 4;
            for (std::size_t i = 0; i < kRate / 4; ++i) {
                in[at + i] = static_cast<float>(0.6 * std::exp(-static_cast<double>(i) / (0.02 * kRate)) *
                                                std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / kRate));
            }
        }
        // The level the stretched sound should have: the original's, each 10 ms twice as long.
        const std::vector<double> was = envelopeOf(in);
        auto error = [&](const std::vector<float>& out) {
            const std::vector<double> e = envelopeOf(out);
            double sum = 0;
            for (std::size_t k = 0; k < e.size(); ++k) sum += std::abs(e[k] - was[std::min(k / 2, was.size() - 1)]);
            return sum / static_cast<double>(e.size());
        };
        std::vector<std::vector<float>> plain{in}, followed{in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"), {{QStringLiteral("shift"), 50.0}}, plain));
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"),
                                  {{QStringLiteral("shift"), 50.0}, {QStringLiteral("follow"), 1.0}}, followed));
        QCOMPARE(followed[0].size(), std::size_t{2 * kRate});
        QVERIFY2(error(followed[0]) < 0.7 * error(plain[0]),
                 qPrintable(QStringLiteral("%1 against %2").arg(error(followed[0])).arg(error(plain[0]))));
        QVERIFY(std::abs(frequencyOf(followed[0]) - 440.0) < 3.0);
    }

    // The AudioSuite processes on the engine: master clips only, changing the length (p.801, p.856-860).
    void audioSuiteProcesses() {
        for (const char* id : {"suite.pitch_shift", "suite.time_compression", "suite.time_shift"}) {
            const auto info = findAudioSuitePlugin(QString::fromLatin1(id));
            QVERIFY(info && info->masterClipOnly && info->changesLength);
        }
        const std::vector<float> in = sine(kRate, 440.0);
        std::vector<std::vector<float>> c{in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_compression"), {{QStringLiteral("ratio"), 2.0}}, c));
        QCOMPARE(c[0].size(), std::size_t{2 * kRate});
        QVERIFY(std::abs(frequencyOf(c[0]) - 440.0) < 2.0);
        // Time Shift at 50%: twice as long (25% is four times, p.859); Varispeed links time and pitch (p.858).
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"), {{QStringLiteral("shift"), 50.0}}, c));
        QCOMPARE(c[0].size(), std::size_t{2 * kRate});
        QVERIFY(std::abs(frequencyOf(c[0]) - 440.0) < 2.0);
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"), {{QStringLiteral("shift"), 200.0}, {QStringLiteral("mode"), 3.0}}, c));
        QCOMPARE(c[0].size(), std::size_t{kRate / 2});
        QVERIFY(std::abs(frequencyOf(c[0]) - 880.0) < 2.0);
        // Monophonic, a fifth up at the same length.
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.time_shift"),
                                  {{QStringLiteral("mode"), 0.0}, {QStringLiteral("range"), 1.0}, {QStringLiteral("transpose"), 7.0}}, c));
        QVERIFY(std::abs(static_cast<double>(c[0].size()) - kRate) <= 1.0);
        QVERIFY2(std::abs(frequencyOf(c[0]) - 440.0 * std::pow(2.0, 7.0 / 12.0)) < 3.0, qPrintable(QString::number(frequencyOf(c[0]))));
        // Pitch Shift: Coarse and Fine together (p.850); its Gain first.
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.pitch_shift"),
                                  {{QStringLiteral("coarse"), -12.0}, {QStringLiteral("fine"), 50.0}, {QStringLiteral("gain"), -6.0}}, c));
        QVERIFY(std::abs(static_cast<double>(c[0].size()) - kRate) <= 1.0);
        QVERIFY(std::abs(frequencyOf(c[0]) - 440.0 * std::pow(2.0, -11.5 / 12.0)) < 2.0);
    }
};

QTEST_GUILESS_MAIN(TestTimeStretch)
#include "tst_time_stretch.moc"
