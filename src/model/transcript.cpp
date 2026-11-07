// Transcripts, their lines and the transcript store (lumenis/model/transcript.h).
#include "lumenis/model/transcript.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace lumenis::model {

namespace {

QJsonArray linesToJson(const QList<TranscriptLine>& lines) {
    QJsonArray out;
    for (const TranscriptLine& l : lines) {
        QJsonArray words;
        // Each word as [text, start, end]: transcripts run to thousands of words.
        for (const TranscriptWord& w : l.words) words.append(QJsonArray{w.text, w.start, w.end});
        out.append(QJsonObject{{QStringLiteral("speaker"), l.speaker}, {QStringLiteral("words"), words}});
    }
    return out;
}

QList<TranscriptLine> linesFromJson(const QJsonArray& array) {
    QList<TranscriptLine> out;
    for (const QJsonValue& v : array) {
        const QJsonObject o = v.toObject();
        TranscriptLine l;
        l.speaker = std::max(1, o.value(QStringLiteral("speaker")).toInt(1));
        for (const QJsonValue& w : o.value(QStringLiteral("words")).toArray()) {
            const QJsonArray a = w.toArray();
            if (a.size() < 3) continue;
            l.words << TranscriptWord{a.at(0).toString(), a.at(1).toDouble(), a.at(2).toDouble()};
        }
        out << l;
    }
    return out;
}

// A file name for a MobID: its letters and digits only (MobIDs are hex with separators).
QString fileStem(const QString& mob) {
    QString out;
    for (const QChar c : mob) {
        if (c.isLetterOrNumber()) out += c;
    }
    return out;
}

bool endsSentence(const QString& word) {
    const QString w = word.trimmed();
    return w.endsWith(QLatin1Char('.')) || w.endsWith(QLatin1Char('?')) || w.endsWith(QLatin1Char('!'));
}

} // namespace

QString TranscriptLine::text() const {
    QStringList out;
    for (const TranscriptWord& w : words) out << w.text;
    return out.join(QLatin1Char(' '));
}

QString Transcript::speakerName(int speaker) const {
    const QString name = speakers.value(speaker);
    return name.isEmpty() ? QStringLiteral("Speaker %1").arg(speaker) : name;
}

QString Transcript::plainText() const {
    QStringList out;
    for (const TranscriptLine& l : lines) out << QStringLiteral("%1: %2").arg(speakerName(l.speaker), l.text());
    return out.join(QLatin1Char('\n'));
}

QJsonObject Transcript::toJson() const {
    QJsonObject o{{QStringLiteral("mob"), mob},
                  {QStringLiteral("clip"), clipName},
                  {QStringLiteral("language"), language},
                  {QStringLiteral("created"), created.toString(Qt::ISODate)},
                  {QStringLiteral("lines"), linesToJson(lines)}};
    if (!original.isEmpty()) o.insert(QStringLiteral("original"), linesToJson(original));
    if (!speakers.isEmpty()) {
        QJsonObject names;
        for (auto it = speakers.cbegin(); it != speakers.cend(); ++it) names.insert(QString::number(it.key()), it.value());
        o.insert(QStringLiteral("speakers"), names);
    }
    return o;
}

std::optional<Transcript> Transcript::fromJson(const QJsonObject& o) {
    Transcript t;
    t.mob = o.value(QStringLiteral("mob")).toString();
    if (t.mob.isEmpty()) return std::nullopt;
    t.clipName = o.value(QStringLiteral("clip")).toString();
    t.language = o.value(QStringLiteral("language")).toString();
    t.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    t.lines = linesFromJson(o.value(QStringLiteral("lines")).toArray());
    t.original = linesFromJson(o.value(QStringLiteral("original")).toArray());
    const QJsonObject names = o.value(QStringLiteral("speakers")).toObject();
    for (auto it = names.begin(); it != names.end(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (ok && id >= 1 && !it.value().toString().isEmpty()) t.speakers.insert(id, it.value().toString());
    }
    return t;
}

QList<TranscriptLine> transcriptLines(const QList<EngineWord>& words, double pause, double longest) {
    QList<TranscriptLine> lines;
    int speaker = 1;
    int previous = 0; // the speaker before the current one, 0 for none yet
    int speakers = 1; // how many IDs are in use
    bool newLine = true;
    for (const EngineWord& w : words) {
        const QString text = w.text.trimmed();
        if (text.isEmpty()) continue;
        if (!newLine && !lines.isEmpty()) {
            const TranscriptLine& last = lines.last();
            if (w.start - last.end() > pause || (endsSentence(last.words.last().text) && last.end() - last.start() >= longest)) {
                newLine = true;
            }
        }
        if (newLine || lines.isEmpty()) {
            lines.append(TranscriptLine{speaker, {}});
            newLine = false;
        }
        lines.last().words << TranscriptWord{text, w.start, std::max(w.start, w.end)};
        if (w.turnAfter) {
            // Back to the speaker before (a conversation of two), else a new one.
            const int next = previous > 0 ? previous : ++speakers;
            previous = speaker;
            speaker = next;
            newLine = true;
        }
    }
    return lines;
}

TranscriptStore::TranscriptStore(QString folder) : folder_(std::move(folder)) {}

QString TranscriptStore::defaultFolder() {
    const QString env = qEnvironmentVariable("LUMENIS_TRANSCRIPTS");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Transcripts"));
}

QString TranscriptStore::path(const QString& mob) const {
    return QDir(folder_).filePath(fileStem(mob) + QStringLiteral(".json"));
}

bool TranscriptStore::has(const QString& mob) const {
    return !fileStem(mob).isEmpty() && QFile::exists(path(mob));
}

std::optional<Transcript> TranscriptStore::load(const QString& mob) const {
    if (fileStem(mob).isEmpty()) return std::nullopt;
    QFile f(path(mob));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? Transcript::fromJson(doc.object()) : std::nullopt;
}

bool TranscriptStore::save(const Transcript& transcript) const {
    if (fileStem(transcript.mob).isEmpty() || !QDir().mkpath(folder_)) return false;
    QSaveFile f(path(transcript.mob));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(transcript.toJson()).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool TranscriptStore::remove(const QString& mob) const {
    return has(mob) && QFile::remove(path(mob));
}

QStringList TranscriptStore::mobs() const {
    QStringList out;
    for (const QString& name : QDir(folder_).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        if (const auto t = load(name.chopped(5))) out << t->mob;
    }
    return out;
}

TranscriptionBins::TranscriptionBins(QString projectFolder)
    : file_(QDir(projectFolder).filePath(QStringLiteral("Transcription.json"))) {
    QFile f(file_);
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("excluded")).toArray()) {
        excluded_.insert(v.toString());
    }
}

bool TranscriptionBins::setIncluded(const QString& binId, bool included) {
    if (included) excluded_.remove(binId);
    else excluded_.insert(binId);
    if (file_.isEmpty()) return false;
    QStringList ids(excluded_.cbegin(), excluded_.cend());
    ids.sort();
    QSaveFile f(file_);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("excluded"), QJsonArray::fromStringList(ids)}}).toJson());
    return f.commit();
}

} // namespace lumenis::model
