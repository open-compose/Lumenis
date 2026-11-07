// PhraseFind AI over transcripts (lumenis/model/phrase_find.h; Editing Guide p.436-441).
#include "lumenis/model/phrase_find.h"

#include "lumenis/model/group_clip.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

namespace lumenis::model {

namespace {

QString bare(const QString& word) {
    QString out;
    for (const QChar c : word.toLower()) {
        if (c.isLetterOrNumber()) out += c;
    }
    return out;
}

QString lineText(const ShownLine& line) {
    QStringList words;
    for (const ShownWord& w : line.words) words << w.text;
    return words.join(QLatin1Char(' '));
}

bool hitMatches(const PhraseHit& hit, const QList<FindFilter>& filters, const TextFindOptions& options) {
    for (const FindFilter& f : filters) {
        if (f.text.isEmpty()) continue;
        if (f.column == QLatin1String("Score")) {
            if (!textMatches(QString::number(hit.score), f.text, options)) return false;
        } else if (f.column == QLatin1String("Transcription")) {
            if (!textMatches(hit.text, f.text, options)) return false;
        } else if (f.column.isEmpty()) {
            if (!textMatches(hit.text, f.text, options) && !itemMatches(hit.item, f.text, {}, options)) return false;
        } else if (!itemMatches(hit.item, QString(), {f}, options)) {
            return false;
        }
    }
    return true;
}

// The hits in one clip's shown transcript.
QList<PhraseHit> clipHits(const ShownTranscript& shown, const QStringList& query) {
    QList<PhraseHit> out;
    // The words in time order with the line each is in, so that a phrase may run from one line into the next.
    struct Flat {
        QString word;
        const ShownWord* shown;
        int line;
    };
    QList<Flat> flat;
    for (int l = 0; l < shown.lines.size(); ++l) {
        for (const ShownWord& w : shown.lines[l].words) flat.append({bare(w.text), &w, l});
    }
    QSet<int> exactLines;
    const auto n = query.size();
    for (qsizetype i = 0; i + n <= flat.size(); ++i) {
        bool match = true;
        for (qsizetype k = 0; k < n && match; ++k) match = flat[i + k].word == query[k];
        if (!match) continue;
        PhraseHit hit;
        hit.score = 100;
        hit.frame = flat[i].shown->frame;
        hit.endFrame = flat[i + n - 1].shown->endFrame;
        QStringList texts;
        for (int l = flat[i].line; l <= flat[i + n - 1].line; ++l) {
            texts << lineText(shown.lines[l]);
            exactLines.insert(l);
        }
        hit.text = texts.join(QLatin1Char(' '));
        out << hit;
    }
    // Lines holding half or more of the words, in any order (ours: a word that only begins with one of them counts as
    // half of it). One hit per line, from its first matched word.
    for (int l = 0; l < shown.lines.size(); ++l) {
        if (exactLines.contains(l) || n < 2) continue;
        const ShownLine& line = shown.lines[l];
        double found = 0.0;
        std::optional<std::int64_t> first;
        std::int64_t last = 0;
        for (const QString& q : query) {
            double best = 0.0;
            for (const ShownWord& w : line.words) {
                const QString b = bare(w.text);
                const double m = b == q ? 1.0 : (q.size() >= 3 && b.startsWith(q) ? 0.5 : 0.0);
                if (m > 0.0) {
                    if (!first || w.frame < *first) first = w.frame;
                    last = std::max(last, w.endFrame);
                }
                best = std::max(best, m);
            }
            found += best;
        }
        const double share = found / static_cast<double>(n);
        if (share < 0.5 || !first) continue;
        PhraseHit hit;
        hit.score = std::min(90, 50 + static_cast<int>(std::lround(40.0 * share)));
        hit.frame = *first;
        hit.endFrame = last;
        hit.text = lineText(line);
        out << hit;
    }
    return out;
}

} // namespace

bool phraseSearchable(const BinItem& item) {
    switch (item.kind) {
    case BinItemKind::MasterClip:
    case BinItemKind::AudioClip:
    case BinItemKind::Subclip: return true;
    case BinItemKind::Group: return groupOf(item).has_value() && !isMultigroup(item);
    default: return false;
    }
}

QStringList phraseWords(const QString& text) {
    QStringList out;
    for (const QString& w : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const QString b = bare(w);
        if (!b.isEmpty()) out << b;
    }
    return out;
}

QList<PhraseHit> findPhrase(const QList<FindBin>& bins, const TranscriptFor& transcriptFor, const QString& query,
                            const QList<FindFilter>& filters, const TextFindOptions& options) {
    QList<PhraseHit> out;
    const QStringList words = phraseWords(query);
    if (words.isEmpty() || !transcriptFor) return out;
    for (const FindBin& b : bins) {
        for (const BinItem& item : b.bin.items) {
            if (!phraseSearchable(item)) continue;
            const ShownTranscript shown = shownTranscript(item, transcriptFor);
            if (shown.lines.isEmpty()) continue;
            for (PhraseHit hit : clipHits(shown, words)) {
                hit.binId = b.id;
                hit.binName = b.name;
                hit.item = item;
                if (hitMatches(hit, filters, options)) out << hit;
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const PhraseHit& a, const PhraseHit& b) { return a.score > b.score; });
    return out;
}

} // namespace lumenis::model
