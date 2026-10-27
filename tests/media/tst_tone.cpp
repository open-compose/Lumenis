// The calibration tone (Editing Guide p.158-159, p.920; docs/spec/tone.md): a sine at its level and
// frequency, noise for 0 Hz and a sweep for -777, the same samples however it is cut up, and the
// clip names; and the player's output peaks, which the Audio Tool's meters show.
#include "lumenis/media/playback.h"
#include "lumenis/media/tone.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace lumenis;
using namespace lumenis::media;

namespace {

std::vector<float> render(const Tone& tone, std::int64_t at, int count, int rate = 48000) {
    std::vector<float> v(static_cast<std::size_t>(count));
    renderTone(tone, at, count, rate, v.data());
    return v;
}

float peak(const std::vector<float>& v) {
    float p = 0;
    for (float x : v) p = std::max(p, std::abs(x));
    return p;
}

// Rising zero crossings per second.
double frequency(const std::vector<float>& v, int rate) {
    int crossings = 0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i - 1] < 0 && v[i] >= 0) ++crossings;
    }
    return crossings * static_cast<double>(rate) / static_cast<double>(v.size());
}

} // namespace

class TestTone : public QObject {
    Q_OBJECT

private slots:
    // The default: -20 dB on the digital scale (a peak of 0.1) at 1000 Hz (p.158, p.920).
    void sine() {
        const std::vector<float> v = render(Tone{}, 0, 48000);
        QVERIFY(std::abs(peak(v) - 0.1f) < 1e-4f);
        QVERIFY(std::abs(frequency(v, 48000) - 1000) <= 1);
        const std::vector<float> loud = render(Tone{-6, 440}, 0, 48000);
        QVERIFY(std::abs(peak(loud) - 0.5012f) < 1e-3f);
        QVERIFY(std::abs(frequency(loud, 48000) - 440) <= 1);
    }

    // Any sample is the same whether rendered alone or in a run, also hours in.
    void sameInPieces() {
        for (const Tone& tone : {Tone{-20, 1000}, Tone{-20, kToneNoise}, Tone{-20, kToneSweep}}) {
            const std::int64_t far = std::int64_t{48000} * 3600 * 5 + 17;
            const std::vector<float> whole = render(tone, far, 1000);
            const std::vector<float> part = render(tone, far + 400, 600);
            for (int i = 0; i < 600; ++i) QCOMPARE(part[static_cast<std::size_t>(i)], whole[static_cast<std::size_t>(400 + i)]);
        }
    }

    // 0 Hz is noise within the level; -777 a sweep from low to high (p.159).
    void noiseAndSweep() {
        const std::vector<float> noise = render(Tone{-20, kToneNoise}, 0, 48000);
        QVERIFY(peak(noise) <= 0.1f);
        QVERIFY(peak(noise) > 0.09f);
        double mean = 0;
        for (float x : noise) mean += static_cast<double>(x);
        QVERIFY(std::abs(mean / 48000) < 0.002);
        const int rate = 48000;
        const std::vector<float> early = render(Tone{-20, kToneSweep}, 0, rate);
        const std::vector<float> late = render(Tone{-20, kToneSweep}, std::int64_t{rate} * 8, rate);
        QVERIFY(std::abs(peak(early) - 0.1f) < 1e-3f);
        QVERIFY(frequency(early, rate) < 100);
        QVERIFY(frequency(late, rate) > 2000);
        // After ten seconds it starts again.
        QCOMPARE(render(Tone{-20, kToneSweep}, std::int64_t{rate} * 10, 100), render(Tone{-20, kToneSweep}, 0, 100));
    }

    void names() {
        QCOMPARE(toneClipName(Tone{}, 1), QStringLiteral("Tone 1000 Hz -20 dB"));
        QCOMPARE(toneClipName(Tone{-18, kToneNoise}, 2), QStringLiteral("Noise -18 dB 2 Tracks"));
        QCOMPARE(toneClipName(Tone{-20, kToneSweep}, 1), QStringLiteral("Sweep -20 dB"));
    }

    // A tone piece plays without a file, on L and R, and the player keeps each channel's peak until
    // takePeaks().
    void playsAndPeaks() {
        AudioPiece piece;
        piece.tone = Tone{};
        piece.length = 100000;
        AudioPlayer player;
        AudioPlayer::PlayOptions options;
        options.device = false;
        QVERIFY(player.start(std::make_shared<AudioMix>(std::vector<AudioPiece>{piece}), 0, options));
        std::vector<float> out(2 * 4800);
        int got = 0;
        for (int tries = 0; tries < 200 && got == 0; ++tries) {
            got = player.pull(out.data(), 4800);
            if (got == 0) QTest::qWait(5);
        }
        QVERIFY(got > 0);
        const auto peaks = player.takePeaks();
        QVERIFY(std::abs(peaks[0] - 0.1f) < 2e-3f);
        QVERIFY(std::abs(peaks[1] - 0.1f) < 2e-3f);
        QCOMPARE(player.takePeaks()[0], 0.0f);
        player.stop();
    }
};

QTEST_GUILESS_MAIN(TestTone)
#include "tst_tone.moc"
