// Waveform peaks for the Timeline (Editing Guide p.677; docs/spec/waveforms.md): the peaks of a
// known sound, the coarse level, the cache file and its refusal of a changed source, and the
// cache's thread.
#include "lumenis/media/media.h"
#include "lumenis/media/waveform.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// One second at 48 kHz on two channels: channel 0 silent for 0.25 s, then +0.5, then -0.25 from
// 0.5 s; channel 1 a constant 0.75.
bool writeTest(const QString& path) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = 2;
    s.sampleRate = 48000;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(2 * 48000);
    for (int i = 0; i < 48000; ++i) {
        v[2 * static_cast<std::size_t>(i)] = i < 12000 ? 0.0f : i < 24000 ? 0.5f : -0.25f;
        v[2 * static_cast<std::size_t>(i) + 1] = 0.75f;
    }
    return w.writeAudio(v.data(), 48000) && w.finish();
}

} // namespace

class TestWaveform : public QObject {
    Q_OBJECT

private slots:
    void peaks() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("t.wav"));
        QVERIFY(writeTest(wav));
        QString error;
        const auto w = computeWaveform({wav, -1, 0}, &error);
        QVERIFY2(w, qPrintable(error));
        QCOMPARE(w->sampleRate, 48000);
        QCOMPARE(w->samples, std::int64_t{48000});
        QCOMPARE(static_cast<int>(w->low.size()), (48000 + Waveform::kBucket - 1) / Waveform::kBucket);
        auto near = [](std::pair<float, float> got, float lo, float hi) {
            return std::abs(got.first - lo) < 0.01f && std::abs(got.second - hi) < 0.01f;
        };
        QVERIFY(near(w->range(0, 11000), 0, 0));
        QVERIFY(near(w->range(13000, 20000), 0.5f, 0.5f));
        QVERIFY(near(w->range(30000, 47000), -0.25f, -0.25f));
        // Across all three, through the coarse peaks.
        QVERIFY(near(w->range(0, 48000), -0.25f, 0.5f));
        QVERIFY(near(w->range(40000, 90000), -0.25f, -0.25f));
        QVERIFY(near(w->range(60000, 90000), 0, 0)); // past the end
        // Peaks are rounded outwards: never drawn smaller than they are.
        const auto r = w->range(13000, 20000);
        QVERIFY(r.second >= 0.5f);
        const auto other = computeWaveform({wav, -1, 1});
        QVERIFY(other && near(other->range(0, 48000), 0.75f, 0.75f));
        QVERIFY(!computeWaveform({wav, -1, 2})); // no third channel
        QVERIFY(!computeWaveform({dir.filePath("missing.wav"), -1, 0}));
    }

    // Saved and read back as it was; a source changed since is refused.
    void cacheFile() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("t.wav"));
        QVERIFY(writeTest(wav));
        const WaveformKey key{wav, -1, 0};
        const auto w = computeWaveform(key);
        QVERIFY(w);
        const QString file = dir.filePath(QStringLiteral("cache/t.wfm"));
        QVERIFY(saveWaveform(file, key, *w));
        const auto back = loadWaveform(file, key);
        QVERIFY(back);
        QCOMPARE(back->low, w->low);
        QCOMPARE(back->high, w->high);
        QCOMPARE(back->coarseHigh, w->coarseHigh);
        QCOMPARE(back->samples, w->samples);
        QVERIFY(!loadWaveform(file, WaveformKey{wav, -1, 1})); // another channel's key
        // The source rewritten: longer, so its size differs.
        {
            QFile f(wav);
            QVERIFY(f.open(QIODevice::Append));
            f.write(QByteArray(16, '\0'));
        }
        QVERIFY(!loadWaveform(file, key));
    }

    // get() queues, the thread works it out and calls back; the file is kept in the folder.
    void cacheThread() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("t.wav"));
        QVERIFY(writeTest(wav));
        std::atomic<int> ready{0};
        {
            WaveformCache cache([&ready] { ++ready; });
            cache.setFolder(dir.filePath(QStringLiteral("WaveformCache")));
            QVERIFY(!cache.get({wav, -1, 0}));
            QVERIFY(!cache.get({dir.filePath("missing.wav"), -1, 0}));
            cache.wait();
            QCOMPARE(ready.load(), 1);
            const auto w = cache.get({wav, -1, 0});
            QVERIFY(w);
            QCOMPARE(w->samples, std::int64_t{48000});
            QVERIFY(!cache.get({dir.filePath("missing.wav"), -1, 0})); // not tried again
            cache.wait();
            QCOMPARE(ready.load(), 1);
            QCOMPARE(QDir(dir.filePath(QStringLiteral("WaveformCache"))).entryList({QStringLiteral("*.wfm")}).size(), 1);
        }
        // A new cache reads the file rather than the sound.
        WaveformCache again;
        again.setFolder(dir.filePath(QStringLiteral("WaveformCache")));
        QVERIFY(!again.get({wav, -1, 0}));
        again.wait();
        QVERIFY(again.get({wav, -1, 0}));
    }
};

QTEST_GUILESS_MAIN(TestWaveform)
#include "tst_waveform.moc"
