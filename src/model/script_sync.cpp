// ScriptSync AI and Create Script from Clip (lumenis/model/script_sync.h; Editing Guide p.569-570, p.584-585).
#include "lumenis/model/script_sync.h"

#include "lumenis/model/phrase_find.h"

#include <QRegularExpression>

#include <algorithm>
#include <limits>
#include <vector>

namespace lumenis::model {

namespace {
// Alignments larger than this many cells are not tried (ours: about 64 MB of table); ScriptSync then marks nothing.
constexpr std::size_t kMaxCells = 16'000'000;
} // namespace

int indentOf(const QString& line) {
    int column = 0;
    for (const QChar c : line) {
        if (c == QLatin1Char(' ')) ++column;
        else if (c == QLatin1Char('\t')) column = (column / 8 + 1) * 8;
        else break;
    }
    return column;
}

QString spokenText(const QString& line, const ScriptSyncOptions& options) {
    if (line.trimmed().isEmpty()) return {};
    if (options.skipIndented && indentOf(line) < options.dialogIndent) return {};
    QString text = line;
    if (options.skipBeforeColon) {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        if (colon >= 0) text = text.mid(colon + 1);
    }
    if (options.skipBracketed) {
        static const QRegularExpression bracketed(QStringLiteral("\\([^)]*\\)|\\[[^\\]]*\\]"));
        text.remove(bracketed);
    }
    if (options.skipCapitals) {
        bool letters = false;
        bool lower = false;
        for (const QChar c : text) {
            letters = letters || c.isLetter();
            lower = lower || c.isLower();
        }
        if (letters && !lower) return {};
    }
    return text.simplified();
}

QList<SyncWord> syncWords(const ShownTranscript& transcript) {
    QList<SyncWord> out;
    for (const ShownLine& l : transcript.lines)
        for (const ShownWord& w : l.words) out << SyncWord{w.text, w.frame};
    return out;
}

int scriptSyncTake(Script& script, const QString& takeId, const QList<SyncWord>& words, const ScriptSyncOptions& options) {
    const ScriptTake* found = takeOf(script, takeId);
    if (!found) return 0;
    const ScriptTake take = *found;
    int first = take.first;
    int last = take.last;
    std::int64_t lowFrame = std::numeric_limits<std::int64_t>::min();
    std::int64_t highFrame = std::numeric_limits<std::int64_t>::max();
    if (options.betweenMarks && take.marks.size() >= 2) {
        // "Select this option if you want to sync between the first and last marks."
        first = take.marks.firstKey();
        last = take.marks.lastKey();
        lowFrame = std::min(take.marks.first(), take.marks.last());
        highFrame = std::max(take.marks.first(), take.marks.last());
    }
    // The script's words, each with its line, and the transcript's, each with its frame.
    std::vector<std::pair<QString, int>> a;
    QMap<int, int> lineWords;
    for (int l = first; l <= last && l < script.lines.size(); ++l) {
        for (const QString& w : phraseWords(spokenText(script.lines.at(l), options))) {
            a.emplace_back(w, l);
            ++lineWords[l];
        }
    }
    std::vector<std::pair<QString, std::int64_t>> b;
    for (const SyncWord& w : words) {
        if (w.frame < lowFrame || w.frame > highFrame) continue;
        for (const QString& part : phraseWords(w.text)) b.emplace_back(part, w.frame);
    }
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0 || m == 0 || (n + 1) * (m + 1) > kMaxCells) return 0;
    // The longest common sequence, as a table of suffix lengths.
    std::vector<int> table((n + 1) * (m + 1), 0);
    auto at = [&](std::size_t i, std::size_t j) -> int& { return table[i * (m + 1) + j]; };
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;)
            at(i, j) = a[i].first == b[j].first ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
    }
    QMap<int, int> matched;
    QMap<int, std::int64_t> firstFrame;
    for (std::size_t i = 0, j = 0; i < n && j < m;) {
        if (a[i].first == b[j].first) {
            const int line = a[i].second;
            ++matched[line];
            if (!firstFrame.contains(line)) firstFrame.insert(line, b[j].second);
            ++i;
            ++j;
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            ++i;
        } else {
            ++j;
        }
    }
    int placed = 0;
    for (auto it = firstFrame.cbegin(); it != firstFrame.cend(); ++it) {
        const int line = it.key();
        if (matched.value(line) * 2 < lineWords.value(line)) continue; // fewer than half its words heard
        const ScriptTake* now = takeOf(script, takeId);
        if (now->marks.contains(line) && (!options.overwrite || now->marks.value(line) == it.value())) continue;
        if (addScriptMark(script, takeId, line, it.value())) ++placed;
    }
    return placed;
}

Script scriptFromTranscripts(const QList<ScriptClip>& clips, const QList<ShownTranscript>& transcripts, const ScriptLayout& layout) {
    Script script;
    script.layout = layout;
    script.fromClips = true;
    struct Span {
        ScriptClip clip;
        int first = 0;
        QList<std::int64_t> frames;
    };
    QList<Span> spans;
    for (qsizetype i = 0; i < clips.size() && i < transcripts.size(); ++i) {
        if (transcripts[i].lines.isEmpty()) continue;
        if (!script.lines.isEmpty()) script.lines << QString();
        Span span{clips[i], static_cast<int>(script.lines.size()), {}};
        for (const ShownLine& l : transcripts[i].lines) {
            QStringList text;
            for (const ShownWord& w : l.words) text << w.text;
            script.lines << (l.speaker.isEmpty() ? text.join(QLatin1Char(' ')) : l.speaker + QStringLiteral(": ") + text.join(QLatin1Char(' ')));
            span.frames << l.frame;
        }
        spans << span;
    }
    for (const Span& s : spans) {
        const int last = s.first + static_cast<int>(s.frames.size()) - 1;
        const QString slate = addSlate(script, s.first, last, {s.clip});
        for (const ScriptSlate& sl : script.slates) {
            if (sl.id != slate) continue;
            const QString take = sl.takes.first().id;
            for (int k = 0; k < s.frames.size(); ++k) addScriptMark(script, take, s.first + k, s.frames[k]);
            break;
        }
    }
    return script;
}

} // namespace lumenis::model
