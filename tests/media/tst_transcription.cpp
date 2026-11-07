// Speech to text for transcripts (lumenis/media/transcription.h): whisper-cli's JSON, a clip transcribed through a
// provider, and whisper-cli run through a stand-in program.
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/transcription.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace lumenis;

namespace {

const char kWhisperJson[] = R"({
  "result": {"language": "en"},
  "transcription": [
    {"offsets": {"from": 0, "to": 400}, "text": " Hello"},
    {"offsets": {"from": 400, "to": 900}, "text": " there.", "speaker_turn_next": true},
    {"offsets": {"from": 1200, "to": 1500}, "text": " Hi [_TT_150]"},
    {"offsets": {"from": 1500, "to": 1500}, "text": ""}
  ]})";

bool writeTone(const QString& path, double seconds) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * 48000));
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = 0.3f * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
    media::WriterSettings ws;
    ws.format = QStringLiteral("wav");
    ws.audioCodec = QStringLiteral("pcm_f32le");
    ws.channels = 1;
    ws.sampleRate = 48000;
    media::MediaWriter w;
    return w.open(path, ws) && w.writeAudio(samples.data(), static_cast<int>(samples.size())) && w.finish();
}

// A provider that hears two words, and how many samples it was given.
class FakeProvider : public ext::TranscriptionProvider {
public:
    ext::ProviderInfo info() const override { return {"test.fake", "Fake", ext::Category::Transcription, ext::LicenseTier::OpenSource, "", ""}; }
    ext::Transcript transcribe(std::span<const float> samples, int sampleRate, const ext::TranscriptionOptions& options,
                               const ext::TranscriptionProgress&) override {
        seconds = static_cast<double>(samples.size()) / sampleRate;
        language = options.language;
        ext::Transcript t;
        t.language = "en";
        t.words = {{"Good", 0.2, 0.5, 1.0f, false}, {"morning.", 0.6, 1.1, 1.0f, false}};
        return t;
    }
    double seconds = 0;
    std::string language;
};

} // namespace

class TestTranscription : public QObject {
    Q_OBJECT

private slots:
    void parsesWhisperJson() {
        QString language;
        const QList<model::EngineWord> words = media::parseWhisperJson(kWhisperJson, &language);
        QCOMPARE(language, QStringLiteral("en"));
        QCOMPARE(words.size(), qsizetype(3));
        QCOMPARE(words[0].text, QStringLiteral("Hello"));
        QCOMPARE(words[1].end, 0.9);
        QVERIFY(words[1].turnAfter);
        QCOMPARE(words[2].text, QStringLiteral("Hi"));
        QVERIFY(words[2].turnAfter);
        QVERIFY(media::parseWhisperJson("not json").isEmpty());
    }

    void transcribesAClip() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        QVERIFY(writeTone(wav, 3.0));
        media::LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> clips = media::linkFiles({wav}, o);
        QCOMPARE(clips.size(), qsizetype(1));
        FakeProvider provider;
        ext::TranscriptionOptions options;
        options.language = "fr";
        QString error;
        const auto t = media::transcribeClip(clips[0], provider, options, {}, &error);
        QVERIFY2(t, qPrintable(error));
        QVERIFY(std::abs(provider.seconds - 3.0) < 0.05);
        QCOMPARE(provider.language, std::string("fr"));
        QCOMPARE(t->mob, QString::fromStdString(model::mobIdOf(clips[0]).toString()));
        QCOMPARE(t->lines.size(), qsizetype(1));
        QCOMPARE(t->lines[0].text(), QStringLiteral("Good morning."));
        // Cancelled.
        QVERIFY(!media::transcribeClip(clips[0], provider, options, [](double) { return false; }, &error));
    }

    // WhisperProvider with a stand-in whisper-cli that writes the JSON above where -of says.
    void runsWhisperCli() {
#ifdef Q_OS_WIN
        QSKIP("The stand-in whisper-cli is a shell script.");
#else
        QTemporaryDir dir;
        const QString tool = dir.filePath(QStringLiteral("whisper-cli"));
        QFile script(tool);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nout=\"\"\nwhile [ $# -gt 0 ]; do [ \"$1\" = \"-of\" ] && out=\"$2\"; shift; done\ncat > \"$out.json\" <<'EOF'\n");
        script.write(kWhisperJson);
        script.write("\nEOF\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QString model = dir.filePath(QStringLiteral("ggml-small.en-tdrz.bin"));
        QFile m(model);
        QVERIFY(m.open(QIODevice::WriteOnly));
        m.close();
        qputenv("LUMENIS_WHISPER", tool.toLocal8Bit());
        qputenv("LUMENIS_WHISPER_MODEL", model.toLocal8Bit());
        media::WhisperProvider provider;
        QVERIFY2(provider.probe().available, provider.probe().reason.c_str());
        std::vector<float> samples(16000, 0.1f);
        const ext::Transcript t = provider.transcribe(samples, 16000, {}, {});
        QCOMPARE(t.language, std::string("en"));
        QCOMPARE(t.words.size(), std::size_t(3));
        QVERIFY(t.words[1].speakerTurnAfter);
        qunsetenv("LUMENIS_WHISPER");
        QVERIFY(!media::WhisperProvider().probe().available);
        qunsetenv("LUMENIS_WHISPER_MODEL");
#endif
    }
};

QTEST_GUILESS_MAIN(TestTranscription)
#include "tst_transcription.moc"
