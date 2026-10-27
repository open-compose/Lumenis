// The audio settings that change what plays and what is made (Editing Guide p.1147-1149;
// docs/spec/audio-settings.md): Pitch Correction during Shuttle, Convert Sample Rates When Playing
// (AudioMix::setRequiredRate), and new audio media at the project's sample rate
// (renderedAudioClip with MediaCreation::sampleRate); and the Import settings' Audio tab (p.1198-1199):
// pull rates kept, Broadcast WAV time stamps and their alignment, monophonic groups.
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/media/tone.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// Pulls `count` stereo samples from a player started without a device.
std::vector<float> pullAll(AudioPlayer& player, int count) {
    std::vector<float> out(2 * static_cast<std::size_t>(count));
    int got = 0;
    for (int tries = 0; tries < 2000 && got < count; ++tries) {
        const int n = player.pull(out.data() + 2 * got, std::min(count - got, 2048));
        got += n;
        if (n == 0) QTest::qWait(2);
    }
    out.resize(2 * static_cast<std::size_t>(got));
    return out;
}

// Rising zero crossings of the left channel per second of output.
double frequency(const std::vector<float>& stereo) {
    int crossings = 0;
    const std::size_t n = stereo.size() / 2;
    for (std::size_t i = 1; i < n; ++i) {
        if (stereo[2 * (i - 1)] < 0 && stereo[2 * i] >= 0) ++crossings;
    }
    return crossings * static_cast<double>(kPlaybackRate) / static_cast<double>(n);
}

// The left channel's energy at `hz` (Goertzel): where the tone's pitch is.
double energyAt(const std::vector<float>& stereo, double hz) {
    const double k = 2.0 * std::cos(2.0 * 3.141592653589793 * hz / kPlaybackRate);
    double s1 = 0;
    double s2 = 0;
    for (std::size_t i = 0; i < stereo.size() / 2; ++i) {
        const double s0 = static_cast<double>(stereo[2 * i]) + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

// A mono 24-bit WAV of `seconds` of a ramp at `rate`, with a Broadcast WAV time reference when
// `timeReference` is not negative.
bool writeWav(const QString& path, int rate, double seconds, std::int64_t timeReference = -1) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_s24le");
    s.channels = 1;
    s.sampleRate = rate;
    if (timeReference >= 0) {
        s.formatOptions = {{QStringLiteral("write_bext"), QStringLiteral("1")}};
        s.metadata = {{QStringLiteral("time_reference"), QString::number(timeReference)}};
    }
    MediaWriter w;
    if (!w.open(path, s)) return false;
    const auto n = static_cast<int>(rate * seconds);
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = 0.5f * static_cast<float>(i % 100) / 100.0f + 0.01f;
    return w.writeAudio(v.data(), n) && w.finish();
}

std::shared_ptr<AudioMix> toneMix(int hz) {
    AudioPiece piece;
    piece.tone = Tone{-6, hz};
    piece.length = std::int64_t{kPlaybackRate} * 600;
    return std::make_shared<AudioMix>(std::vector<AudioPiece>{piece});
}

} // namespace

class TestAudioProject : public QObject {
    Q_OBJECT

private slots:
    // Shuttling at 2 and 3 times speed raises the pitch as tape does; with Pitch Correction during
    // Shuttle the tone keeps its frequency (p.1148), forward and backward.
    void pitchCorrection() {
        for (int speed : {2, 3, -2}) {
            AudioPlayer plain;
            AudioPlayer::PlayOptions o;
            o.device = false;
            o.speed = speed;
            QVERIFY(plain.start(toneMix(500), std::int64_t{kPlaybackRate} * 300, o));
            const double raised = frequency(pullAll(plain, kPlaybackRate / 2));
            plain.stop();
            QVERIFY2(std::abs(raised - 500.0 * std::abs(speed)) < 20, qPrintable(QString::number(raised)));
            AudioPlayer corrected;
            o.pitchCorrect = true;
            QVERIFY(corrected.start(toneMix(500), std::int64_t{kPlaybackRate} * 300, o));
            const std::vector<float> out = pullAll(corrected, kPlaybackRate / 2);
            corrected.stop();
            // The tone's own pitch carries the sound, not the shuttle's.
            const double at500 = energyAt(out, 500);
            const double shifted = energyAt(out, 500.0 * std::abs(speed));
            QVERIFY2(at500 > 20 * shifted, qPrintable(QStringLiteral("%1 %2").arg(at500).arg(shifted)));
            // The overlapping grains keep the level: a -6 dB tone stays near 0.5.
            float peak = 0;
            for (float v : out) peak = std::max(peak, std::abs(v));
            QVERIFY2(peak > 0.4f && peak < 0.6f, qPrintable(QString::number(static_cast<double>(peak))));
        }
    }

    // Convert Sample Rates When Playing: Never plays other rates as silence (p.1148-1149); tone
    // pieces still play.
    void requiredRate() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("t.wav"));
        {
            WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_f32le");
            s.channels = 1;
            s.sampleRate = 44100;
            MediaWriter w;
            QVERIFY(w.open(wav, s));
            std::vector<float> v(44100, 0.25f);
            QVERIFY(w.writeAudio(v.data(), 44100) && w.finish());
        }
        AudioPiece piece;
        piece.path = wav;
        piece.length = 4800;
        piece.left = 1;
        piece.right = 1;
        AudioMix any({piece});
        std::vector<float> out(2 * 4800);
        any.render(0, 4800, out.data());
        QVERIFY(std::abs(out[2000] - 0.25f) < 1e-3f);
        AudioMix strict({piece});
        strict.setRequiredRate(48000);
        strict.render(0, 4800, out.data());
        QCOMPARE(out[2000], 0.0f);
        strict.setRequiredRate(44100);
        strict.render(0, 4800, out.data());
        QVERIFY(std::abs(out[2000] - 0.25f) < 1e-3f);
        AudioPiece tone;
        tone.tone = Tone{};
        tone.length = 4800;
        AudioMix toneOnly({tone});
        toneOnly.setRequiredRate(44100);
        toneOnly.render(0, 4800, out.data());
        float peak = 0;
        for (float v : out) peak = std::max(peak, std::abs(v));
        QVERIFY(peak > 0.09f);
    }

    // "Defines the audio sample rate for the entire system for playing and recording" (p.1148): what
    // Cutthulhu renders (mixdowns, tone media, punch-in) is written at the project's rate, WAV at
    // other rates than 48 kHz, as long as its frames.
    void renderedAtProjectRate() {
        QTemporaryDir dir;
        MediaCreation creation;
        creation.videoDrive = dir.path();
        for (int rate : {44100, 96000, 48000}) {
            creation.sampleRate = rate;
            const Tone tone{-6, 1000};
            auto render = [tone](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
                for (auto& track : out) renderTone(tone, at, count, kPlaybackRate, track.data());
            };
            QString error;
            const auto clip = renderedAudioClip(QStringLiteral("Tone"), Rational(25), 50, 1, render, QString(), creation, &error);
            QVERIFY2(clip, qPrintable(error));
            QCOMPARE(clip->fields.value(QStringLiteral("Audio SR")), QString::number(rate));
            QCOMPARE(clip->fields.value(QStringLiteral("Video File Format")), rate == 48000 ? QStringLiteral("MXF") : QStringLiteral("WAV"));
            const auto link = linkOf(*clip);
            QVERIFY(link);
            QVERIFY(link->audioFile(0).endsWith(rate == 48000 ? QLatin1String(".mxf") : QLatin1String(".wav")));
            AudioReader r;
            QVERIFY(r.open(link->audioFile(0)));
            QCOMPARE(r.sampleRate(), rate);
            const std::vector<float> all = r.samples(0, rate * 3);
            QCOMPARE(static_cast<int>(all.size()), rate * 2); // two seconds, no more
            float peak = 0;
            int crossings = 0;
            for (std::size_t i = 1000; i < all.size(); ++i) {
                peak = std::max(peak, std::abs(all[i]));
                if (all[i - 1] < 0 && all[i] >= 0) ++crossings;
            }
            QVERIFY(std::abs(peak - 0.501f) < 0.02f);
            QVERIFY(std::abs(crossings * rate / static_cast<double>(all.size() - 1000) - 1000) < 5);
        }
    }

    void pullRates() {
        QVERIFY(isPullRate(47952, 48000));
        QVERIFY(isPullRate(48048, 48000));
        QVERIFY(isPullRate(44056, 44100));
        QVERIFY(isPullRate(44144, 44100));
        QVERIFY(!isPullRate(48000, 48000));
        QVERIFY(!isPullRate(44100, 48000));
        QVERIFY(isImportRate(96000) && !isImportRate(47952));
    }

    // Frame-accurate time stamps: the frame a sample falls in, and with Subframe Alignment the nearest
    // frame with silence added or up to half a frame left out (p.1199).
    void alignment() {
        const Rational fps(25);
        // 1920 samples a frame at 48 kHz.
        QCOMPARE(alignToFrame(1920 * 100, 48000, fps).startFrame, std::int64_t{100});
        QCOMPARE(alignToFrame(1920 * 100, 48000, fps).shift, std::int64_t{0});
        const BwfAlignment early = alignToFrame(1920 * 100 + 300, 48000, fps);
        QCOMPARE(early.startFrame, std::int64_t{100});
        QCOMPARE(early.shift, std::int64_t{300}); // 300 samples of silence first
        const BwfAlignment late = alignToFrame(1920 * 100 + 1500, 48000, fps);
        QCOMPARE(late.startFrame, std::int64_t{101});
        QCOMPARE(late.shift, std::int64_t{-420}); // 420 samples left out
        // 29.97: frames are 1601.6 samples.
        const BwfAlignment ntsc = alignToFrame(1602, 48000, Rational(30000, 1001));
        QCOMPARE(ntsc.startFrame, std::int64_t{1});
        QCOMPARE(ntsc.shift, std::int64_t{0});
    }

    // A Broadcast WAV's time reference is the linked clip's Start; a pull rate is kept unconverted
    // with relabelPullRates; audioShift adds silence before the sound or leaves some out.
    void importAudio() {
        QTemporaryDir dir;
        const QString bwf = dir.filePath(QStringLiteral("take.wav"));
        // 10:00:00:00 at 25 fps plus 300 samples.
        const std::int64_t tr = std::int64_t{48000} * 36000 + 300;
        QVERIFY(writeWav(bwf, 48000, 1.0, tr));
        const auto info = probe(bwf);
        QVERIFY(info);
        QCOMPARE(bwfTimeReference(*info), std::optional<std::int64_t>(tr));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*info, o);
        QVERIFY(clip);
        QCOMPARE(clip->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:00"));

        MediaCreation creation;
        creation.videoDrive = dir.filePath(QStringLiteral("drive"));
        QDir().mkpath(creation.videoDrive);
        creation.audioShift = 300;
        QString error;
        auto made = transcodeClip(*clip, 0, -1, creation, &error);
        QVERIFY2(made, qPrintable(error));
        AudioReader r;
        QVERIFY(r.open(linkOf(*made)->audioFile(0)));
        std::vector<float> got = r.samples(0, 400);
        QCOMPARE(got.at(299), 0.0f);
        QVERIFY(std::abs(got.at(300) - 0.01f) < 1e-3f); // the file's first sample
        QCOMPARE(static_cast<int>(r.samples(0, 96000).size()), 48000);
        creation.audioShift = -420;
        made = transcodeClip(*clip, 0, -1, creation, &error);
        QVERIFY2(made, qPrintable(error));
        QVERIFY(r.open(linkOf(*made)->audioFile(0)));
        got = r.samples(0, 10);
        QVERIFY(std::abs(got.at(0) - (0.5f * 20 / 100 + 0.01f)) < 1e-3f); // sample 420 of the file

        // 47.952 kHz kept as it is: the same samples, called 48 kHz.
        const QString pulled = dir.filePath(QStringLiteral("pulled.wav"));
        QVERIFY(writeWav(pulled, 47952, 1.0));
        const auto pulledClip = linkClip(*probe(pulled), o);
        QVERIFY(pulledClip);
        creation.audioShift = 0;
        creation.relabelPullRates = true;
        made = transcodeClip(*pulledClip, 0, -1, creation, &error);
        QVERIFY2(made, qPrintable(error));
        QVERIFY(r.open(linkOf(*made)->audioFile(0)));
        QCOMPARE(r.sampleRate(), 48000);
        got = r.samples(0, 300);
        for (int i : {0, 99, 150, 299}) QVERIFY(std::abs(got.at(static_cast<std::size_t>(i)) - (0.5f * static_cast<float>(i % 100) / 100.0f + 0.01f)) < 1e-3f);
    }

    // name_1.wav ... name_3.wav become one clip with A1-A3 in that order; others stay as they are.
    void monoGroups() {
        QTemporaryDir dir;
        for (const char* name : {"Scene4_2.wav", "Scene4_1.wav", "Scene4_3.wav", "Other_1.wav", "Music.wav"}) {
            QVERIFY(writeWav(dir.filePath(QString::fromLatin1(name)), 48000, 0.5));
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> clips =
            linkFiles({dir.filePath("Scene4_2.wav"), dir.filePath("Scene4_1.wav"), dir.filePath("Music.wav"), dir.filePath("Scene4_3.wav"),
                       dir.filePath("Other_1.wav")},
                      o);
        QCOMPARE(clips.size(), 5);
        const QList<model::BinItem> grouped = groupMonoWavs(clips);
        QCOMPARE(grouped.size(), 3);
        QCOMPARE(grouped.at(0).name, QStringLiteral("Scene4"));
        QCOMPARE(grouped.at(1).name, QStringLiteral("Music"));
        QCOMPARE(grouped.at(2).name, QStringLiteral("Other_1")); // a group of one is not a group
        QCOMPARE(grouped.at(0).fields.value(QStringLiteral("Tracks")), QStringLiteral("A1-3"));
        const auto link = linkOf(grouped.at(0));
        QVERIFY(link);
        QCOMPARE(link->channels, 3);
        for (int t = 0; t < 3; ++t) QVERIFY(link->audioFile(t).endsWith(QStringLiteral("Scene4_%1.wav").arg(t + 1)));
        // A file source for each track, each from an import source the clip carries.
        int files = 0;
        for (const model::ClipSource& src : grouped.at(0).sources) {
            if (src.kind != model::ClipSource::Kind::File) continue;
            ++files;
            QVERIFY(std::any_of(grouped.at(0).sources.cbegin(), grouped.at(0).sources.cend(),
                                [&](const model::ClipSource& i) { return i.kind == model::ClipSource::Kind::Import && i.mob == src.from; }));
        }
        QCOMPARE(files, 3);
        // It imports as a three-track clip.
        MediaCreation creation;
        creation.videoDrive = dir.filePath(QStringLiteral("drive"));
        QDir().mkpath(creation.videoDrive);
        QString error;
        const auto made = transcodeClip(grouped.at(0), 0, -1, creation, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(linkOf(*made)->channels, 3);
    }
};

QTEST_GUILESS_MAIN(TestAudioProject)
#include "tst_audio_project.moc"
