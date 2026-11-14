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
