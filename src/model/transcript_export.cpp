// Transcripts out (lumenis/model/transcript_export.h; Editing Guide p.450-453).
#include "lumenis/model/transcript_export.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace lumenis::model {

namespace {

QString wordsOf(const ShownLine& line) {
    QStringList words;
    for (const ShownWord& w : line.words) words << w.text;
    return words.join(QLatin1Char(' '));
}

// Rows of at most `max` characters, broken between words (a longer word gets a row of its own).
QStringList rowsOf(const QList<ShownWord>& words, int max) {
    QStringList rows;
    QString row;
    for (const ShownWord& w : words) {
        if (!row.isEmpty() && max > 0 && row.size() + 1 + w.text.size() > max) {
            rows << row;
            row.clear();
        }
        row += (row.isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
    }
    if (!row.isEmpty()) rows << row;
    return rows;
}

// Keeps each caption ending at or before the next one's start, and at least a frame long.
void separate(QList<Caption>& captions) {
    std::stable_sort(captions.begin(), captions.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
    for (qsizetype i = 0; i + 1 < captions.size(); ++i) captions[i].end = std::min(captions[i].end, captions[i + 1].start);
    captions.erase(std::remove_if(captions.begin(), captions.end(), [](const Caption& c) { return c.end <= c.start; }), captions.end());
}

} // namespace

QString exportedTranscriptText(const ShownTranscript& shown, const TimecodeFormat& format, std::int64_t startFrame) {
    QList<Caption> captions;
    for (const ShownLine& line : shown.lines) {
        if (line.words.isEmpty()) continue;
        Caption c;
        c.start = line.words.first().frame;
        c.end = std::max(c.start + 1, line.words.last().endFrame);
        c.text = line.speaker + QStringLiteral(":\n") + wordsOf(line);
        captions << c;
    }
    separate(captions);
    for (Caption& c : captions) {
        c.start += startFrame;
        c.end += startFrame;
    }
    return writeDsCaptions(captions, format);
}

QList<Caption> transcriptCaptions(const ShownTranscript& shown, const TranscriptCaptionOptions& options) {
    QList<Caption> out;
    for (const ShownLine& line : shown.lines) {
        if (line.words.isEmpty()) continue;
        if (options.maxCharacters <= 0) {
            out.append({line.words.first().frame, std::max(line.words.first().frame + 1, line.words.last().endFrame), wordsOf(line)});
            continue;
        }
        // Two rows to a caption, timed by the words they hold.
        QList<ShownWord> pending;
        auto flush = [&] {
            if (pending.isEmpty()) return;
            out.append({pending.first().frame, std::max(pending.first().frame + 1, pending.last().endFrame),
                        rowsOf(pending, options.maxCharacters).join(QLatin1Char('\n'))});
            pending.clear();
        };
        for (const ShownWord& w : line.words) {
            pending << w;
            if (rowsOf(pending, options.maxCharacters).size() > 2) {
                pending.removeLast();
                flush();
                pending << w;
            }
        }
        flush();
    }
    separate(out);
    if (options.range) {
        const auto [in, end] = *options.range;
        QList<Caption> kept;
        for (Caption c : out) {
            if (c.end <= in || c.start >= end) continue;
            c.start = std::max(c.start, in);
            c.end = std::min(c.end, end);
            kept << c;
        }
        out = kept;
    }
    return out;
}

std::optional<QString> createSubCapTrack(Sequence& sequence, const QList<Caption>& captions, QString* error) {
    if (captions.isEmpty()) {
        if (error) *error = QStringLiteral("There is no dialogue to caption.");
        return std::nullopt;
    }
    const auto track = sequence.addTrack(TrackKind::Video);
    if (!track) {
        if (error) *error = QStringLiteral("No video track can be added.");
        return std::nullopt;
    }
    std::int64_t first = captions.first().start;
    std::int64_t last = captions.first().end;
    for (const Caption& c : captions) {
        first = std::min(first, c.start);
        last = std::max(last, c.end);
    }
    Track* t = sequence.track(*track);
    Segment lead;
    lead.length = first;
    Segment effect;
    effect.length = last - first;
    effect.effect = Effect{QStringLiteral("Generator"), QStringLiteral("SubCap"), {}};
    t->segments.clear();
    if (lead.length > 0) t->segments << lead;
    t->segments << effect;
    if (!importCaptions(sequence, *track, first, captions, error)) return std::nullopt;
    return track;
}

QByteArray writeSharedTranscripts(const SharedTranscripts& shared) {
    QJsonArray list;
    for (const Transcript& t : shared.transcripts) list.append(t.toJson());
    const QJsonObject o{{QStringLiteral("format"), QStringLiteral("cutthulhu-transcripts")},
                        {QStringLiteral("version"), 1},
                        {QStringLiteral("languageHint"), shared.languageHint},
                        {QStringLiteral("transcripts"), list}};
    return QJsonDocument(o).toJson();
}

std::optional<SharedTranscripts> readSharedTranscripts(const QByteArray& data, QString* error) {
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("cutthulhu-transcripts")) {
        if (error) *error = QStringLiteral("This is not a shared transcripts file.");
        return std::nullopt;
    }
    if (o.value(QStringLiteral("version")).toInt() > 1) {
        if (error) *error = QStringLiteral("The shared transcripts file comes from a newer version.");
        return std::nullopt;
    }
    SharedTranscripts out;
    out.languageHint = o.value(QStringLiteral("languageHint")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("transcripts")).toArray()) {
        if (auto t = Transcript::fromJson(v.toObject()); t && !t->mob.isEmpty()) out.transcripts << *t;
    }
    return out;
}

} // namespace lumenis::model
