// Syncing by sound (Editing Guide p.1124, p.1126; lumenis/media/waveform_sync.h): the lag between two sounds made of the
// same loudness pattern, at one rate and at two, silence and unrelated sounds refused, and two linked WAV files'
// frame offset.
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/waveform_sync.h"

#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <cstdint>

using namespace lumenis;

namespace {

// `seconds` of noise whose loudness changes every 20 ms by a pattern from `seed`, at `rate`; the pattern's first
// `delay` seconds are silence.
std::vector<float> pattern(double seconds, int rate, std::uint32_t seed, double delay = 0.0) {
    std::vector<float> out(static_cast<std::size_t>(seconds * rate), 0.0f);
    std::uint32_t level = seed;
    std::uint32_t noise = 12345;
    const auto block = static_cast<std::size_t>(rate / 50);
    const auto skip = static_cast<std::size_t>(delay * rate);
    float amp = 0.0f;
    for (std::size_t i = skip; i < out.size(); ++i) {
        // The loudness follows time since the pattern started, so a delayed pattern is the same sound later.
        if ((i - skip) % block == 0) {
            level = level * 1664525u + 1013904223u;
            amp = static_cast<float>((level >> 24) & 0xff) / 255.0f;
        }
        noise = noise * 22695477u + 1u;
        out[i] = amp * (static_cast<float>((noise >> 16) & 0x7fff) / 16384.0f - 1.0f) * 0.5f;
    }
    return out;
}

bool writeWav(const QString& path, const std::vector<float>& samples, int rate) {
    media::WriterSettings ws;
    ws.format = QStringLiteral("wav");
    ws.audioCodec = QStringLiteral("pcm_f32le");
    ws.channels = 1;
    ws.sampleRate = rate;
    media::MediaWriter w;
    return w.open(path, ws) && w.writeAudio(samples.data(), static_cast<int>(samples.size())) && w.finish();
}

} // namespace

class TestWaveformSync : public QObject {
    Q_OBJECT

private slots:
    void lagOfTheSamePattern() {
        const auto a = pattern(6.0, 48000, 7);
        const auto b = pattern(6.0, 48000, 7, 0.4); // the same sound 0.4 s later
        const auto lag = media::soundLag(a, 48000, b, 48000);
        QVERIFY(lag);
        QVERIFY2(std::abs(*lag - 0.4) < 0.002, qPrintable(QString::number(*lag)));
        // The other way round.
        const auto back = media::soundLag(b, 48000, a, 48000);
        QVERIFY(back && std::abs(*back + 0.4) < 0.002);
        // At another rate.
        const auto c = pattern(6.0, 44100, 7, 0.25);
        const auto mixed = media::soundLag(a, 48000, c, 44100);
        QVERIFY(mixed && std::abs(*mixed - 0.25) < 0.002);
        // Limited to 0.1 s it finds nothing as good.
        const auto limited = media::soundLag(a, 48000, b, 48000, 0.1);
        QVERIFY(!limited || std::abs(*limited - 0.4) > 0.05);
    }

    void refusesWhatDoesNotMatch() {
        const std::vector<float> silence(48000 * 2, 0.0f);
        const auto a = pattern(4.0, 48000, 7);
        QVERIFY(!media::soundLag(a, 48000, silence, 48000));
        QVERIFY(!media::soundLag(a, 48000, pattern(4.0, 48000, 99), 48000));
    }

    // Two linked files: frame f of the first sounds with frame f + 10 of the second at 25 fps.
    void offsetOfLinkedClips() {
        QTemporaryDir dir;
        const QString a = dir.filePath(QStringLiteral("a.wav"));
        const QString b = dir.filePath(QStringLiteral("b.wav"));
        QVERIFY(writeWav(a, pattern(8.0, 48000, 3), 48000));
        QVERIFY(writeWav(b, pattern(8.0, 48000, 3, 0.4), 48000));
        media::LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> clips = media::linkFiles({a, b}, o);
        QCOMPARE(clips.size(), qsizetype(2));
        QString error;
        const auto offset = media::waveformOffset(clips[0], clips[1], &error);
        QVERIFY2(offset, qPrintable(error));
        QCOMPARE(*offset, std::int64_t(10));
        // A clip without sound.
        model::BinItem none = clips[0];
        none.link = {};
        QVERIFY(!media::waveformOffset(none, clips[1], &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWaveformSync)
#include "tst_waveform_sync.moc"
