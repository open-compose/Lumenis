// whisper-cli as the transcription engine, and clips to transcripts (lumenis/media/transcription.h).
#include "lumenis/media/transcription.h"

#include "lumenis/media/waveform_sync.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

constexpr int kEngineRate = 16000; // whisper.cpp reads 16 kHz mono

// Linear interpolation to `to` samples a second (speech needs nothing better).
std::vector<float> resampled(std::span<const float> in, int from, int to) {
    if (from == to || from <= 0 || in.empty()) return {in.begin(), in.end()};
    const auto count = static_cast<std::size_t>(static_cast<double>(in.size()) * to / from);
    std::vector<float> out(count);
    const double step = static_cast<double>(from) / to;
    for (std::size_t i = 0; i < count; ++i) {
        const double x = static_cast<double>(i) * step;
        const auto j = static_cast<std::size_t>(x);
        const float a = in[std::min(j, in.size() - 1)];
        const float b = in[std::min(j + 1, in.size() - 1)];
        const auto t = static_cast<float>(x - static_cast<double>(j));
        out[i] = a + (b - a) * t;
    }
    return out;
}

// A 16-bit PCM mono WAV file.
bool writeWav(const QString& path, const std::vector<float>& samples, int rate) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    QDataStream out(&f);
    out.setByteOrder(QDataStream::LittleEndian);
    const auto bytes = static_cast<quint32>(samples.size() * 2);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + bytes);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << bytes;
    for (const float s : samples) out << static_cast<qint16>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
    return out.status() == QDataStream::Ok;
}

} // namespace

QString whisperExecutable() {
    const QString env = qEnvironmentVariable("LUMENIS_WHISPER");
    if (!env.isEmpty()) return QFileInfo(env).isExecutable() ? env : QString();
    if (QCoreApplication::instance()) {
        const QString beside = QStandardPaths::findExecutable(QStringLiteral("whisper-cli"), {QCoreApplication::applicationDirPath()});
        if (!beside.isEmpty()) return beside;
    }
    return QStandardPaths::findExecutable(QStringLiteral("whisper-cli"));
}

QString whisperModel() {
    const QString env = qEnvironmentVariable("LUMENIS_WHISPER_MODEL");
    if (!env.isEmpty()) return QFileInfo::exists(env) ? env : QString();
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                             .filePath(QStringLiteral("Whisper/ggml-base.bin"));
    return QFileInfo::exists(path) ? path : QString();
}

QList<model::EngineWord> parseWhisperJson(const QByteArray& json, QString* language) {
    QList<model::EngineWord> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (language) *language = root.value(QStringLiteral("result")).toObject().value(QStringLiteral("language")).toString();
    for (const QJsonValue& v : root.value(QStringLiteral("transcription")).toArray()) {
        const QJsonObject seg = v.toObject();
        const QJsonObject offsets = seg.value(QStringLiteral("offsets")).toObject();
        model::EngineWord w;
        w.text = seg.value(QStringLiteral("text")).toString().trimmed();
        w.start = offsets.value(QStringLiteral("from")).toDouble() / 1000.0;
        w.end = offsets.value(QStringLiteral("to")).toDouble() / 1000.0;
        w.turnAfter = seg.value(QStringLiteral("speaker_turn_next")).toBool();
        // whisper.cpp marks a turn with a "[_TT_...]" token in the text too.
        if (w.text.contains(QStringLiteral("[_TT_"))) {
            w.turnAfter = true;
            w.text = w.text.left(w.text.indexOf(QStringLiteral("[_TT_"))).trimmed();
        }
        if (w.text.isEmpty()) {
            if (w.turnAfter && !out.isEmpty()) out.last().turnAfter = true;
            continue;
        }
        out << w;
    }
    return out;
}

ext::ProviderInfo WhisperProvider::info() const {
    return {"org.ggml.whisper-cli", "whisper.cpp", ext::Category::Transcription, ext::LicenseTier::OpenSource, "ggml", ""};
}

ext::Availability WhisperProvider::probe() const {
    if (whisperExecutable().isEmpty()) {
        return {false, "whisper.cpp's whisper-cli was not found (BUILDING.md, \"Transcripts\")."};
    }
    if (whisperModel().isEmpty()) return {false, "No whisper.cpp model file (BUILDING.md, \"Transcripts\")."};
    return {};
}

ext::Transcript WhisperProvider::transcribe(std::span<const float> samples, int sampleRate,
                                            const ext::TranscriptionOptions& options, const ext::TranscriptionProgress& progress) {
    ext::Transcript out;
    const QString tool = whisperExecutable();
    const QString model = whisperModel();
    QTemporaryDir dir;
    if (tool.isEmpty() || model.isEmpty() || !dir.isValid()) return out;
    const QString wav = dir.filePath(QStringLiteral("sound.wav"));
    if (!writeWav(wav, resampled(samples, sampleRate, kEngineRate), kEngineRate)) return out;
    QStringList args{QStringLiteral("-m"), model, QStringLiteral("-f"), wav, QStringLiteral("-oj"), QStringLiteral("-of"),
                     dir.filePath(QStringLiteral("words")), QStringLiteral("-ml"), QStringLiteral("1"), QStringLiteral("-sow"),
                     QStringLiteral("-l"), options.language.empty() ? QStringLiteral("auto") : QString::fromStdString(options.language),
                     QStringLiteral("-pp")};
    if (QFileInfo(model).fileName().contains(QStringLiteral("tdrz"))) args << QStringLiteral("-tdrz");
    QProcess process;
    process.start(tool, args);
    if (!process.waitForStarted()) return out;
    // whisper-cli prints "progress = n%" with -pp.
    while (!process.waitForFinished(250)) {
        const QString err = QString::fromUtf8(process.readAllStandardError());
        const qsizetype at = err.lastIndexOf(QStringLiteral("progress ="));
        double done = 0.0;
        if (at >= 0) done = err.mid(at + 10).section(QLatin1Char('%'), 0, 0).trimmed().toDouble() / 100.0;
        if (progress && !progress(done)) {
            process.kill();
            process.waitForFinished();
            return out;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return out;
    QFile json(dir.filePath(QStringLiteral("words.json")));
    if (!json.open(QIODevice::ReadOnly)) return out;
    QString language;
    for (const model::EngineWord& w : parseWhisperJson(json.readAll(), &language)) {
        ext::TranscribedWord t;
        t.text = w.text.toStdString();
        t.startSeconds = w.start;
        t.endSeconds = w.end;
        t.confidence = 1.0f;
        t.speakerTurnAfter = w.turnAfter;
        out.words.push_back(std::move(t));
    }
    out.language = language.toStdString();
    return out;
}

std::optional<model::Transcript> transcribeClip(const model::BinItem& clip, ext::TranscriptionProvider& provider,
                                                const ext::TranscriptionOptions& options,
                                                const ext::TranscriptionProgress& progress, QString* error) {
    const auto fail = [&](const QString& why) -> std::optional<model::Transcript> {
        if (error) *error = why;
        return std::nullopt;
    };
    // Up to the clip's own length.
    const double seconds = clip.editRate.toDouble() > 0 ? static_cast<double>(clip.durationFrames) / clip.editRate.toDouble() : 3600.0;
    QString why;
    const ClipSound sound = clipSound(clip, seconds > 0 ? seconds : 3600.0, &why);
    if (sound.samples.empty() || sound.rate <= 0) return fail(why.isEmpty() ? QStringLiteral("\"%1\" has no sound.").arg(clip.name) : why);
    const ext::Transcript words = provider.transcribe(sound.samples, sound.rate, options, progress);
    if (progress && !progress(1.0)) return fail(QStringLiteral("Transcription was cancelled."));
    QList<model::EngineWord> engine;
    for (const ext::TranscribedWord& w : words.words) {
        engine << model::EngineWord{QString::fromStdString(w.text), w.startSeconds, w.endSeconds, w.speakerTurnAfter};
    }
    model::Transcript t;
    t.mob = QString::fromStdString(model::mobIdOf(clip).toString());
    t.clipName = clip.name;
    t.language = QString::fromStdString(words.language);
    t.created = QDateTime::currentDateTimeUtc();
    t.lines = model::transcriptLines(engine);
    return t;
}

} // namespace lumenis::media
