// Audio timecode and sample rate changes (Editing Guide p.668-669, p.731; docs/spec/clip-audio.md):
// our LTC encoder and decoder against each other (rates, drop frame, polarity, level, user bits),
// reading a linked clip's LTC with and without Fill Undecodeable Frames, and Change Sample Rate's new
// media.
#include "lumenis/media/link.h"
#include "lumenis/media/ltc.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"

#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kRate = 48000;

// A WAV of `channels` channels: silence, with `ltc` on channel `on` from sample `at`.
bool writeWav(const QString& path, int channels, int on, const std::vector<float>& ltc, std::int64_t at,
              std::int64_t total, int rate = kRate) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = channels;
    s.sampleRate = rate;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(static_cast<std::size_t>(total * channels), 0.0f);
    for (std::size_t i = 0; i < ltc.size(); ++i) {
        const std::int64_t n = at + static_cast<std::int64_t>(i);
        if (n < total) v[static_cast<std::size_t>(n * channels + on)] = ltc[i];
    }
    return w.writeAudio(v.data(), static_cast<int>(total)) && w.finish();
}

std::vector<ltc::Frame> decode(const std::vector<float>& samples, int rate, double fps) {
    ltc::Decoder d(rate, fps);
    // In uneven pieces, as a reader gives them.
    std::size_t at = 0;
    for (std::size_t piece = 997; at < samples.size(); at += piece) {
        d.feed(samples.data() + at, std::min(piece, samples.size() - at));
    }
    return d.frames();
}

QString text(const TimecodeFields& f) {
    return QStringLiteral("%1:%2:%3:%4").arg(f.hours, 2, 10, QLatin1Char('0')).arg(f.minutes, 2, 10, QLatin1Char('0'))
        .arg(f.seconds, 2, 10, QLatin1Char('0')).arg(f.frames, 2, 10, QLatin1Char('0'));
}

} // namespace

class TestLtc : public QObject {
    Q_OBJECT

private slots:
    // Every frame encoded comes back, in order, at its place, for 24, 25 and 30 drop frame.
    void roundTrips_data() {
        QTest::addColumn<int>("num");
        QTest::addColumn<int>("den");
        QTest::addColumn<bool>("drop");
        QTest::addColumn<QString>("start");
        QTest::newRow("25") << 25 << 1 << false << QStringLiteral("10:59:59:20");
        QTest::newRow("24") << 24 << 1 << false << QStringLiteral("01:00:00:00");
        QTest::newRow("29.97 DF") << 30000 << 1001 << true << QStringLiteral("00:00:59;25");
    }
    void roundTrips() {
        QFETCH(int, num);
        QFETCH(int, den);
        QFETCH(bool, drop);
        QFETCH(QString, start);
        const TimecodeFormat format = TimecodeFormat::forRate(Rational(num, den), drop);
        const auto tc = Timecode::parse(start.toStdString(), format);
        QVERIFY(tc);
        // A frame more: the last bit of a recording ends at a transition that never comes.
        const std::vector<float> s = ltc::encode(*tc, 13, kRate);
        const auto frames = decode(s, kRate, format.nominalFps);
        QCOMPARE(static_cast<int>(frames.size()), 12);
        const double perFrame = kRate / (format.nominalFps * 1.0);
        for (int k = 0; k < 12; ++k) {
            const ltc::Frame& f = frames.at(static_cast<std::size_t>(k));
            QCOMPARE(text(f.time), text(Timecode(tc->frameCount() + k, format).fields()));
            QCOMPARE(f.dropFrame, drop);
            QVERIFY(std::abs(static_cast<double>(f.sample) - k * perFrame) < 2.0);
        }
    }

    // Upside down, quiet, with a little noise and a DC offset: still read.
    void robust() {
        const TimecodeFormat format = TimecodeFormat::forRate(Rational(25), false);
        std::vector<float> s = ltc::encode(Timecode(90000, format), 11, kRate);
        std::uint32_t seed = 1;
        for (float& v : s) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.01f;
            v = -0.1f * v + noise + 0.005f;
        }
        const auto frames = decode(s, kRate, 25);
        QCOMPARE(static_cast<int>(frames.size()), 10);
        QCOMPARE(text(frames.front().time), QStringLiteral("01:00:00:00"));
        // Silence gives nothing.
        QVERIFY(decode(std::vector<float>(kRate, 0.0f), kRate, 25).empty());
    }

    void userBits() {
        const TimecodeFormat format = TimecodeFormat::forRate(Rational(25), false);
        const TimecodeFields u{12, 34, 56, 7};
        const std::vector<float> s = ltc::encode(Timecode(0, format), 2, kRate, ltc::userBitsFor(u));
        const auto frames = decode(s, kRate, 25);
        QVERIFY(!frames.empty());
        QCOMPARE(text(frames.front().userTime()), QStringLiteral("12:34:56:07"));
        QCOMPARE(text(frames.front().time), QStringLiteral("00:00:00:00"));
    }

    // Read Audio Timecode on a linked clip: the LTC is on A2. A frame of silence first means the clip's
    // first frame does not decode, which Fill Undecodeable Frames works back from (p.669).
    void readClip() {
        QTemporaryDir dir;
        const TimecodeFormat format = TimecodeFormat::forRate(Rational(25), false);
        const Timecode start(*Timecode::parse("01:00:20:20", format));
        const QString wav = dir.filePath(QStringLiteral("ltc.wav"));
        QVERIFY(writeWav(wav, 2, 1, ltc::encode(start, 50, kRate), 0, 2 * kRate));
        LinkOptions o;
        o.editRate = Rational(25);
        const model::BinItem clip = linkFiles({wav}, o).first();
        QString error;
        auto tc = ltc::readClipTimecode(clip, 1, false, false, &error);
        QVERIFY2(tc, qPrintable(error));
        QCOMPARE(QString::fromStdString(tc->toString()), QStringLiteral("01:00:20:20"));
        QVERIFY(!ltc::readClipTimecode(clip, 0, false, true, &error)); // A1 is silent
        QVERIFY(!error.isEmpty());

        const QString late = dir.filePath(QStringLiteral("late.wav"));
        QVERIFY(writeWav(late, 1, 0, ltc::encode(start, 50, kRate), kRate / 25, 2 * kRate));
        const model::BinItem lateClip = linkFiles({late}, o).first();
        QVERIFY(!ltc::readClipTimecode(lateClip, 0, false, false, &error));
        tc = ltc::readClipTimecode(lateClip, 0, false, true, &error);
        QVERIFY2(tc, qPrintable(error));
        // 01:00:20:20 is on the clip's frame 1, so frame 0 is 01:00:20:19.
        QCOMPARE(QString::fromStdString(tc->toString()), QStringLiteral("01:00:20:19"));
    }

    // Change Sample Rate: the clip's sound in new files at the new rate, as long as before; 48 kHz
    // stays MXF, other rates are WAV.
    void changeSampleRate() {
        QTemporaryDir dir;
        const QString drive = dir.filePath(QStringLiteral("drive"));
        QDir().mkpath(drive);
        const QString wav = dir.filePath(QStringLiteral("tone.wav"));
        std::vector<float> tone(static_cast<std::size_t>(kRate));
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / kRate));
        QVERIFY(writeWav(wav, 1, 0, tone, 0, kRate));
        LinkOptions o;
        o.editRate = Rational(25);
        const model::BinItem clip = linkFiles({wav}, o).first();
        MediaCreation creation;
        creation.videoDrive = drive;
        creation.sampleRate = 44100;
        creation.resampleFilter = 8;
        QStringList old;
        QString error;
        const auto made = resampleClipAudio(clip, creation, &old, &error);
        QVERIFY2(made, qPrintable(error));
        QVERIFY(old.isEmpty()); // the linked file is not managed media
        const auto link = linkOf(*made);
        QVERIFY(link);
        QVERIFY(link->audioFile(0).endsWith(QLatin1String(".wav")));
        QVERIFY(link->audioFile(0).contains(QLatin1String("Avid MediaFiles")));
        QCOMPARE(made->fields.value(QStringLiteral("Audio SR")), QStringLiteral("44100"));
        AudioReader r;
        QVERIFY(r.open(link->audioFile(0)));
        QCOMPARE(r.sampleRate(), 44100);
        const std::vector<float> back = r.samples(0, 44100);
        QCOMPARE(static_cast<int>(back.size()), 44100);
        double peak = 0;
        for (float v : back) peak = std::max(peak, static_cast<double>(std::abs(v)));
        QVERIFY(std::abs(peak - 0.25) < 0.02);
        // Back to 48 kHz: managed MXF, and the 44.1 kHz file is now the managed original.
        creation.sampleRate = 48000;
        const auto again = resampleClipAudio(*made, creation, &old, &error);
        QVERIFY2(again, qPrintable(error));
        QVERIFY(linkOf(*again)->audioFile(0).endsWith(QLatin1String(".mxf")));
        QCOMPARE(old, QStringList{link->audioFile(0)});
    }
};

QTEST_GUILESS_MAIN(TestLtc)
#include "tst_ltc.moc"
