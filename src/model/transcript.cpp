// Transcripts, their lines and the transcript store (lumenis/model/transcript.h).
#include "lumenis/model/transcript.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#include <algorithm>
#include <utility>
#include <vector>

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

namespace {

struct Sharing {
    TranscriptSharing mode = TranscriptSharing::Off;
    QString folder;
};

Sharing& sharing() {
    static Sharing s;
    return s;
}

} // namespace

void setTranscriptSharing(TranscriptSharing mode, const QString& folder) {
    sharing() = {folder.isEmpty() ? TranscriptSharing::Off : mode, QDir::cleanPath(folder)};
}

TranscriptSharing transcriptSharing() {
    return sharing().mode;
}

QString sharedTranscriptFolder() {
    return sharing().mode == TranscriptSharing::Off ? QString() : sharing().folder;
}

bool sharedTranscriptsReachable() {
    return sharing().mode == TranscriptSharing::Off || QFileInfo(sharing().folder).isDir();
}

QString transcriptStation() {
    const QString env = qEnvironmentVariable("LUMENIS_STATION");
    if (!env.isEmpty()) return env;
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) user = qEnvironmentVariable("USERNAME");
    return user + QLatin1Char('@') + QSysInfo::machineHostName();
}

TranscriptStore::TranscriptStore(QString folder) : folder_(std::move(folder)) {}

QString TranscriptStore::defaultFolder() {
    const QString shared = sharedTranscriptFolder();
    return shared.isEmpty() ? localFolder() : shared;
}

QString TranscriptStore::localFolder() {
    const QString env = qEnvironmentVariable("LUMENIS_TRANSCRIPTS");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("Transcripts"));
}

QString TranscriptStore::lockPath(const QString& mob) const {
    return QDir(folder_).filePath(fileStem(mob) + QStringLiteral(".lock"));
}

std::optional<TranscriptLock> TranscriptStore::lockOf(const QString& mob) const {
    if (fileStem(mob).isEmpty()) return std::nullopt;
    QFile f(lockPath(mob));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    TranscriptLock lock{o.value(QStringLiteral("station")).toString(),
                        QDateTime::fromString(o.value(QStringLiteral("since")).toString(), Qt::ISODate)};
    if (lock.station.isEmpty()) return std::nullopt;
    return lock;
}

bool TranscriptStore::lock(const QString& mob, bool takeOver, TranscriptLock* heldBy) const {
    if (fileStem(mob).isEmpty() || !QDir().mkpath(folder_)) return false;
    if (const auto held = lockOf(mob); held && held->station != transcriptStation() && !takeOver) {
        if (heldBy) *heldBy = *held;
        return false;
    }
    QSaveFile f(lockPath(mob));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("station"), transcriptStation()},
                                      {QStringLiteral("since"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}})
                .toJson(QJsonDocument::Compact));
    return f.commit();
}

void TranscriptStore::unlock(const QString& mob) const {
    if (const auto held = lockOf(mob); held && held->station == transcriptStation()) QFile::remove(lockPath(mob));
}

int hostTranscripts(const QString& local, const QString& shared) {
    if (!QDir().mkpath(shared)) return -1;
    const TranscriptStore from(local);
    const TranscriptStore to(shared);
    int copied = 0;
    if (QDir::cleanPath(local) != QDir::cleanPath(shared)) {
        for (const QString& mob : from.mobs()) {
            if (to.has(mob)) continue;
            if (const auto t = from.load(mob); t && to.save(*t)) ++copied;
        }
    }
    QSaveFile f(QDir(shared).filePath(QStringLiteral("Host.json")));
    if (!f.open(QIODevice::WriteOnly)) return -1;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("station"), transcriptStation()},
                                      {QStringLiteral("since"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}})
                .toJson());
    return f.commit() ? copied : -1;
}

QString transcriptHost(const QString& folder) {
    QFile f(QDir(folder).filePath(QStringLiteral("Host.json")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("station")).toString();
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

bool TranscriptStore::save(const Transcript& transcript, TranscriptLock* heldBy) const {
    if (fileStem(transcript.mob).isEmpty() || !QDir().mkpath(folder_)) return false;
    // Held while it writes; a lock this station already had (an edit, a transcription) stays.
    const bool hadLock = [&] {
        const auto held = lockOf(transcript.mob);
        return held && held->station == transcriptStation();
    }();
    if (!hadLock && !lock(transcript.mob, false, heldBy)) return false;
    QSaveFile f(path(transcript.mob));
    bool ok = f.open(QIODevice::WriteOnly);
    if (ok) {
        f.write(QJsonDocument(transcript.toJson()).toJson(QJsonDocument::Compact));
        ok = f.commit();
    }
    if (!hadLock) unlock(transcript.mob);
    return ok;
}

bool TranscriptStore::remove(const QString& mob) const {
    if (!has(mob)) return false;
    QFile::remove(lockPath(mob));
    return QFile::remove(path(mob));
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

namespace lumenis::model {

namespace {

QString bareWord(const QString& word) {
    QString out;
    for (const QChar c : word.toLower()) {
        if (c.isLetterOrNumber()) out += c;
    }
    return out;
}

QStringList wordsOf(const QString& text) {
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

// Keeps the transcribed lines before the first change.
void keepOriginal(Transcript& t) {
    if (t.original.isEmpty()) t.original = t.lines;
}

// Spreads [start, end) evenly over `words`.
void spread(QList<TranscriptWord>& words, double start, double end) {
    if (words.isEmpty()) return;
    const double step = std::max(0.0, end - start) / static_cast<double>(words.size());
    for (int i = 0; i < words.size(); ++i) {
        words[i].start = start + step * i;
        words[i].end = start + step * (i + 1);
    }
}

} // namespace

bool editWords(Transcript& t, int line, int from, int to, const QString& text) {
    if (line < 0 || line >= t.lines.size()) return false;
    // Read through a const reference: a writable one taken before keepOriginal() would write into the shared copy too.
    const QList<TranscriptWord>& current = std::as_const(t.lines)[line].words;
    from = std::clamp(from, 0, static_cast<int>(current.size()));
    to = std::clamp(to, from, static_cast<int>(current.size()));
    if (from == to) return false;
    const QList<TranscriptWord> old = current.mid(from, to - from);
    const QStringList typed = wordsOf(text);
    QStringList oldText;
    for (const TranscriptWord& w : old) oldText << w.text;
    if (typed == oldText) return false;
    keepOriginal(t);
    QList<TranscriptWord> made;
    for (const QString& w : typed) made << TranscriptWord{w, 0.0, 0.0};
    // Align unchanged words: the longest common sequence of bare words.
    const auto n = old.size();
    const auto m = made.size();
    std::vector<std::vector<int>> lcs(static_cast<std::size_t>(n + 1), std::vector<int>(static_cast<std::size_t>(m + 1), 0));
    for (qsizetype i = n - 1; i >= 0; --i) {
        for (qsizetype j = m - 1; j >= 0; --j) {
            const auto ui = static_cast<std::size_t>(i);
            const auto uj = static_cast<std::size_t>(j);
            lcs[ui][uj] = bareWord(old[i].text) == bareWord(made[j].text) && !bareWord(made[j].text).isEmpty()
                              ? lcs[ui + 1][uj + 1] + 1
                              : std::max(lcs[ui + 1][uj], lcs[ui][uj + 1]);
        }
    }
    QList<std::pair<qsizetype, qsizetype>> kept; // {old index, new index}
    for (qsizetype i = 0, j = 0; i < n && j < m;) {
        const auto ui = static_cast<std::size_t>(i);
        const auto uj = static_cast<std::size_t>(j);
        if (bareWord(old[i].text) == bareWord(made[j].text) && !bareWord(made[j].text).isEmpty()) {
            kept.append({i, j});
            ++i;
            ++j;
        } else if (lcs[ui + 1][uj] >= lcs[ui][uj + 1]) {
            ++i;
        } else {
            ++j;
        }
    }
    // Kept words keep their times. The new words between two kept ones share the time of the old words between them;
    // with no old words there, they share the time of the kept word before them (or after them, at the start): "Multiple
    // words in one word's space: Timings split averaging across the new words".
    kept.prepend({-1, -1});
    kept.append({n, m});
    for (qsizetype k = 1; k + 1 < kept.size(); ++k) {
        const auto [oi, nj] = kept[k];
        made[nj] = TranscriptWord{made[nj].text, old[oi].start, old[oi].end};
    }
    auto spreadNew = [&](qsizetype first, qsizetype last, double start, double end) { // made[first, last)
        QList<TranscriptWord> part = made.mid(first, last - first);
        spread(part, start, end);
        for (qsizetype x = 0; x < part.size(); ++x) made[first + x] = part[x];
    };
    for (qsizetype k = 0; k + 1 < kept.size(); ++k) {
        const auto [oi, nj] = kept[k];
        const auto [oe, ne] = kept[k + 1];
        if (ne - nj <= 1) continue; // no new words between
        if (oe - oi > 1) {
            spreadNew(nj + 1, ne, old[oi + 1].start, old[oe - 1].end);
        } else if (oi >= 0) {
            spreadNew(nj, ne, old[oi].start, old[oi].end);
        } else if (oe < n) {
            spreadNew(0, ne + 1, old[oe].start, old[oe].end);
        }
    }
    QList<TranscriptWord>& words = t.lines[line].words; // after keepOriginal(): detached from the original
    words.remove(from, to - from);
    for (qsizetype i = 0; i < made.size(); ++i) words.insert(from + i, made[i]);
    if (words.isEmpty()) t.lines.removeAt(line);
    return true;
}

int replaceWords(Transcript& t, const QString& find, const QString& replace) {
    QStringList wanted;
    for (const QString& w : wordsOf(find)) {
        if (!bareWord(w).isEmpty()) wanted << bareWord(w);
    }
    if (wanted.isEmpty()) return 0;
    int count = 0;
    for (int l = 0; l < t.lines.size(); ++l) {
        for (int w = 0; w + wanted.size() <= t.lines[l].words.size(); ++w) {
            bool match = true;
            for (int k = 0; k < wanted.size() && match; ++k) match = bareWord(t.lines[l].words[w + k].text) == wanted[k];
            if (!match) continue;
            const int before = static_cast<int>(t.lines.size());
            if (editWords(t, l, w, w + static_cast<int>(wanted.size()), replace)) ++count;
            if (t.lines.size() < before) {
                --l; // the line went
                break;
            }
            w += std::max(0, static_cast<int>(wordsOf(replace).size()) - 1);
        }
    }
    return count;
}

QList<int> speakerIds(const Transcript& t) {
    QList<int> ids;
    for (const TranscriptLine& l : t.lines) {
        if (!ids.contains(l.speaker)) ids << l.speaker;
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool renameSpeaker(Transcript& t, int speaker, const QString& name) {
    const QString n = name.trimmed();
    if (t.speakers.value(speaker) == n) return false;
    keepOriginal(t);
    if (n.isEmpty()) t.speakers.remove(speaker);
    else t.speakers.insert(speaker, n);
    return true;
}

bool identifySpeaker(Transcript& t, int line, const QString& name) {
    const QString n = name.trimmed();
    if (line < 0 || line >= t.lines.size() || n.isEmpty()) return false;
    int id = 0;
    for (auto it = t.speakers.cbegin(); it != t.speakers.cend(); ++it) {
        if (it.value() == n) id = it.key();
    }
    if (id == 0) {
        // A speaker heard only on this line takes the name; otherwise the line gets a new ID with it.
        const int current = t.lines[line].speaker;
        const bool alone = std::count_if(t.lines.cbegin(), t.lines.cend(), [&](const TranscriptLine& l) { return l.speaker == current; }) == 1;
        if (alone && !t.speakers.contains(current)) return renameSpeaker(t, current, n);
        const QList<int> ids = speakerIds(t);
        id = std::max(ids.isEmpty() ? 0 : ids.last(), t.speakers.isEmpty() ? 0 : t.speakers.lastKey()) + 1;
        keepOriginal(t);
        t.speakers.insert(id, n);
    }
    if (t.lines[line].speaker == id) return false;
    keepOriginal(t);
    t.lines[line].speaker = id;
    return true;
}

bool correctSpeaker(Transcript& t, int line, int speaker) {
    if (line < 0 || line >= t.lines.size()) return false;
    if (speaker <= 0) {
        const QList<int> ids = speakerIds(t);
        speaker = std::max(ids.isEmpty() ? 0 : ids.last(), t.speakers.isEmpty() ? 0 : t.speakers.lastKey()) + 1;
    }
    if (t.lines[line].speaker == speaker) return false;
    keepOriginal(t);
    t.lines[line].speaker = speaker;
    return true;
}

bool startNewLine(Transcript& t, int line, int word) {
    if (line < 0 || line >= t.lines.size() || word <= 0 || word >= t.lines[line].words.size()) return false;
    keepOriginal(t);
    TranscriptLine second{t.lines[line].speaker, t.lines[line].words.mid(word)};
    t.lines[line].words = t.lines[line].words.mid(0, word);
    t.lines.insert(line + 1, second);
    return true;
}

bool revertLine(Transcript& t, int line) {
    if (line < 0 || line >= t.lines.size() || t.original.isEmpty()) return false;
    const double start = t.lines[line].start();
    const double end = t.lines[line].end();
    QList<TranscriptLine> back;
    for (const TranscriptLine& o : t.original) {
        if (o.end() > start && o.start() < end) back << o;
    }
    if (back.isEmpty() || (back.size() == 1 && back.first() == t.lines[line])) return false;
    // The edited lines in the same time go too, so nothing is shown twice.
    QList<TranscriptLine> out;
    for (int i = 0; i < t.lines.size(); ++i) {
        const TranscriptLine& l = t.lines[i];
        const bool overlaps = i == line || (l.end() > back.first().start() && l.start() < back.last().end());
        if (!overlaps) out << l;
        if (i == line) out += back;
    }
    t.lines = out;
    return true;
}

bool revertTranscript(Transcript& t) {
    if (t.original.isEmpty()) return false;
    t.lines = t.original;
    t.original.clear();
    return true;
}

} // namespace lumenis::model

namespace lumenis::model {

QStringList speakerNames(const Transcript& t) {
    QStringList out;
    for (const QString& n : t.speakers) {
        if (!n.isEmpty() && !out.contains(n)) out << n;
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

bool mergeSpeakerNames(Transcript& t, const QStringList& names, const QString& into) {
    const QString target = into.trimmed();
    if (target.isEmpty()) return false;
    QList<int> ids;
    for (auto it = t.speakers.cbegin(); it != t.speakers.cend(); ++it) {
        if (names.contains(it.value()) || it.value() == target) ids << it.key();
    }
    if (ids.isEmpty()) return false;
    std::sort(ids.begin(), ids.end());
    const bool alreadyOne = ids.size() == 1 && t.speakers.value(ids.first()) == target;
    // A transcript with only the target name among `names` is left alone; so is one with none of them.
    const bool hasAny = std::any_of(ids.cbegin(), ids.cend(), [&](int id) { return names.contains(t.speakers.value(id)); });
    if (alreadyOne || !hasAny) return false;
    keepOriginal(t);
    const int id = ids.first();
    for (TranscriptLine& l : t.lines) {
        if (ids.contains(l.speaker)) l.speaker = id;
    }
    for (const int other : ids) t.speakers.remove(other);
    t.speakers.insert(id, target);
    return true;
}

bool renameSpeakerName(Transcript& t, const QString& from, const QString& to) {
    const QString n = to.trimmed();
    if (n.isEmpty() || from == n) return false;
    return mergeSpeakerNames(t, {from}, n);
}

bool clearSpeakerName(Transcript& t, const QString& name) {
    QList<int> ids;
    for (auto it = t.speakers.cbegin(); it != t.speakers.cend(); ++it) {
        if (it.value() == name) ids << it.key();
    }
    if (ids.isEmpty()) return false;
    keepOriginal(t);
    for (const int id : ids) t.speakers.remove(id);
    return true;
}

ProjectSpeakers::ProjectSpeakers(QString projectFolder) : file_(QDir(projectFolder).filePath(QStringLiteral("Speakers.json"))) {
    QFile f(file_);
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("names")).toArray()) {
        const QString n = v.toString().trimmed();
        if (!n.isEmpty() && !names_.contains(n)) names_ << n;
    }
    names_.sort(Qt::CaseInsensitive);
}

bool ProjectSpeakers::write() {
    names_.sort(Qt::CaseInsensitive);
    if (file_.isEmpty()) return true;
    QSaveFile f(file_);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{QStringLiteral("names"), QJsonArray::fromStringList(names_)}}).toJson());
    return f.commit();
}

bool ProjectSpeakers::add(const QString& name) { return addAll({name}); }

bool ProjectSpeakers::addAll(const QStringList& names) {
    bool changed = false;
    for (const QString& name : names) {
        const QString n = name.trimmed();
        if (n.isEmpty() || names_.contains(n)) continue;
        names_ << n;
        changed = true;
    }
    return changed && write();
}

bool ProjectSpeakers::remove(const QString& name) { return names_.removeAll(name) > 0 && write(); }

bool ProjectSpeakers::rename(const QString& from, const QString& to) {
    const QString n = to.trimmed();
    if (n.isEmpty() || from == n) return false;
    // A name only in transcripts stays out of the list.
    if (names_.removeAll(from) == 0) return false;
    if (!names_.contains(n)) names_ << n;
    return write();
}

} // namespace lumenis::model
