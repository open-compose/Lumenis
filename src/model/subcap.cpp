// The SubCap effect's captions and caption files (lumenis/model/subcap.h).
#include "lumenis/model/subcap.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <map>

namespace lumenis::model {

bool isSubCap(const Effect& e) { return e.category == QLatin1String("Generator") && e.name == QLatin1String("SubCap"); }

QString captionText(const Effect& e) {
    return e.parameters.value(QLatin1String(kSubCapCategory)).toObject().value(QLatin1String(kCaptionTextKey)).toString();
}

void setCaptionText(Effect& e, const QString& text) {
    QJsonObject category = e.parameters.value(QLatin1String(kSubCapCategory)).toObject();
    category.insert(QLatin1String(kCaptionTextKey), text);
    e.parameters.insert(QLatin1String(kSubCapCategory), category);
}

// ---------------------------------------------------------------- Avid DS

std::optional<QList<Caption>> parseDsCaptions(const QString& text, const TimecodeFormat& format, QString* error) {
    static const QRegularExpression times(QStringLiteral("^\\s*(\\d{2}[:;]\\d{2}[:;]\\d{2}[:;.]\\d{2})\\s+(\\d{2}[:;]\\d{2}[:;]\\d{2}[:;.]\\d{2})\\s*$"));
    QList<Caption> out;
    std::optional<Caption> current;
    QStringList rows;
    auto close = [&] {
        if (!current) return;
        while (!rows.isEmpty() && rows.last().trimmed().isEmpty()) rows.removeLast();
        current->text = rows.join(QLatin1Char('\n'));
        out << *current;
        current.reset();
        rows.clear();
    };
    const QStringList lines = QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        const QRegularExpressionMatch m = times.match(line);
        if (m.hasMatch()) {
            close();
            const auto in = Timecode::parse(m.captured(1).toStdString(), format);
            const auto end = Timecode::parse(m.captured(2).toStdString(), format);
            if (!in || !end || end->frameCount() <= in->frameCount()) {
                if (error) *error = QObject::tr("Line %1: the times %2 and %3 are not a caption's.").arg(i + 1).arg(m.captured(1), m.captured(2));
                return std::nullopt;
            }
            current = Caption{in->frameCount(), end->frameCount(), {}};
            continue;
        }
        if (current) {
            if (line.trimmed().isEmpty() && !rows.isEmpty()) {
                close();
            } else if (!line.trimmed().isEmpty() || !rows.isEmpty()) {
                rows << line;
            }
        }
        // Lines before the first caption's times (a header) are skipped (ours).
    }
    close();
    return out;
}

QString writeDsCaptions(const QList<Caption>& captions, const TimecodeFormat& format) {
    QString out;
    for (const Caption& c : captions) {
        out += QString::fromStdString(Timecode(c.start, format).toString()) + QLatin1Char(' ') +
               QString::fromStdString(Timecode(c.end, format).toString()) + QLatin1Char('\n');
        out += c.text + QStringLiteral("\n\n");
    }
    return out;
}

// ---------------------------------------------------------------- EBU N19

namespace {

// A GSI field: `text` in `length` bytes, padded with spaces.
void putField(QByteArray& block, int at, int length, const QString& text) {
    QByteArray bytes = text.toLatin1().left(length);
    for (char& c : bytes) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
    }
    bytes.append(QByteArray(length - bytes.size(), ' '));
    block.replace(at, length, bytes);
}

QString field(const QByteArray& block, int at, int length) { return QString::fromLatin1(block.mid(at, length)).trimmed(); }

QString stlDate(const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("yyMMdd")) : QStringLiteral("      "); }

QDate fromStlDate(const QString& s) {
    if (s.size() != 6) return {};
    const QDate d = QDate::fromString(QStringLiteral("20") + s, QStringLiteral("yyyyMMdd"));
    return d;
}

QString digits(std::int64_t v, int width) { return QStringLiteral("%1").arg(v, width, 10, QLatin1Char('0')); }

void putTimecode(QByteArray& block, int at, std::int64_t frames, int fps) {
    const std::int64_t f = frames % fps;
    const std::int64_t s = (frames / fps) % 60;
    const std::int64_t m = (frames / fps / 60) % 60;
    const std::int64_t h = frames / fps / 3600 % 24;
    block[at] = static_cast<char>(h);
    block[at + 1] = static_cast<char>(m);
    block[at + 2] = static_cast<char>(s);
    block[at + 3] = static_cast<char>(f);
}

std::int64_t timecodeAt(const QByteArray& block, int at, int fps) {
    auto b = [&](int i) { return static_cast<std::int64_t>(static_cast<unsigned char>(block[at + i])); };
    return ((b(0) * 60 + b(1)) * 60 + b(2)) * fps + b(3);
}

} // namespace

std::optional<QList<Caption>> parseStl(const QByteArray& data, int* fps, StlInfo* info, QString* error) {
    auto fail = [&](const QString& text) -> std::optional<QList<Caption>> {
        if (error) *error = text;
        return std::nullopt;
    };
    if (data.size() < 1024) return fail(QObject::tr("The file is too short for an EBU N19 caption file."));
    const QByteArray gsi = data.left(1024);
    const QString format = field(gsi, 3, 8);
    int rate = 25;
    if (format == QLatin1String("STL30.01")) rate = 30;
    else if (format != QLatin1String("STL25.01")) return fail(QObject::tr("The file's format %1 is not an EBU N19 one.").arg(format));
    if (fps) *fps = rate;
    if (info) {
        info->languageCode = field(gsi, 14, 2);
        info->originalProgramTitle = field(gsi, 16, 32);
        info->originalEpisodeTitle = field(gsi, 48, 32);
        info->translatedProgramTitle = field(gsi, 80, 32);
        info->translatedEpisodeTitle = field(gsi, 112, 32);
        info->translatorName = field(gsi, 144, 32);
        info->translatorContact = field(gsi, 176, 32);
        info->creationDate = fromStlDate(QString::fromLatin1(gsi.mid(224, 6)));
        info->revisionDate = fromStlDate(QString::fromLatin1(gsi.mid(230, 6)));
        info->countryOfOrigin = field(gsi, 274, 3);
        info->publisher = field(gsi, 277, 32);
        info->editorName = field(gsi, 309, 32);
        info->editorContact = field(gsi, 341, 32);
        info->userData = field(gsi, 448, 576);
    }
    QList<Caption> out;
    std::map<int, Caption> bySubtitle; // extension blocks of one subtitle number join its text (ours)
    for (qsizetype at = 1024; at + 128 <= data.size(); at += 128) {
        const QByteArray tti = data.mid(at, 128);
        const int number = static_cast<unsigned char>(tti[1]) | (static_cast<unsigned char>(tti[2]) << 8);
        // Comment blocks (CF 1) carry no caption.
        if (tti[15] == 1) continue;
        QString text;
        for (int i = 16; i < 128; ++i) {
            const auto c = static_cast<unsigned char>(tti[i]);
            if (c == 0x8f) break;
            if (c == 0x8a) {
                if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) text += QLatin1Char('\n');
            } else if (c >= 0x20 && c <= 0x7e) {
                text += QLatin1Char(static_cast<char>(c));
            }
        }
        auto it = bySubtitle.find(number);
        if (it == bySubtitle.end()) {
            bySubtitle.emplace(number, Caption{timecodeAt(tti, 5, rate), timecodeAt(tti, 9, rate), text.trimmed()});
        } else {
            it->second.text += text;
        }
    }
    for (auto& [n, c] : bySubtitle) {
        (void)n;
        if (c.end > c.start) out << c;
    }
    std::sort(out.begin(), out.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
    return out;
}

QByteArray writeStl(const QList<Caption>& captions, int fps, const StlInfo& info) {
    const int rate = fps == 30 ? 30 : 25;
    QByteArray gsi(1024, ' ');
    putField(gsi, 0, 3, QStringLiteral("850"));
    putField(gsi, 3, 8, rate == 30 ? QStringLiteral("STL30.01") : QStringLiteral("STL25.01"));
    putField(gsi, 11, 1, QStringLiteral("1")); // Display Standard Code: open subtitling
    putField(gsi, 12, 2, QStringLiteral("00")); // Latin, ISO 6937
    putField(gsi, 14, 2, info.languageCode.isEmpty() ? QStringLiteral("00") : info.languageCode);
    putField(gsi, 16, 32, info.originalProgramTitle);
    putField(gsi, 48, 32, info.originalEpisodeTitle);
    putField(gsi, 80, 32, info.translatedProgramTitle);
    putField(gsi, 112, 32, info.translatedEpisodeTitle);
    putField(gsi, 144, 32, info.translatorName);
    putField(gsi, 176, 32, info.translatorContact);
    putField(gsi, 224, 6, stlDate(info.creationDate.isValid() ? info.creationDate : QDate::currentDate()));
    putField(gsi, 230, 6, stlDate(info.revisionDate.isValid() ? info.revisionDate : QDate::currentDate()));
    putField(gsi, 236, 2, QStringLiteral("00"));
    putField(gsi, 238, 5, digits(captions.size(), 5));
    putField(gsi, 243, 5, digits(captions.size(), 5));
    putField(gsi, 248, 3, QStringLiteral("001"));
    int columns = 0;
    int rows = 0;
    for (const Caption& c : captions) {
        const QStringList lines = c.text.split(QLatin1Char('\n'));
        rows = std::max(rows, static_cast<int>(lines.size()));
        for (const QString& l : lines) columns = std::max(columns, static_cast<int>(l.size()));
    }
    putField(gsi, 251, 2, digits(std::clamp(columns, 0, 99), 2));
    putField(gsi, 253, 2, digits(std::clamp(rows, 0, 99), 2));
    putField(gsi, 255, 1, QStringLiteral("1"));
    const std::int64_t first = captions.isEmpty() ? 0 : captions.first().start;
    auto tcText = [&](std::int64_t frames) {
        return digits(frames / rate / 3600 % 24, 2) + digits(frames / rate / 60 % 60, 2) + digits(frames / rate % 60, 2) + digits(frames % rate, 2);
    };
    putField(gsi, 256, 8, tcText(first));
    putField(gsi, 264, 8, tcText(first));
    putField(gsi, 272, 1, QStringLiteral("1"));
    putField(gsi, 273, 1, QStringLiteral("1"));
    putField(gsi, 274, 3, info.countryOfOrigin);
    putField(gsi, 277, 32, info.publisher);
    putField(gsi, 309, 32, info.editorName);
    putField(gsi, 341, 32, info.editorContact);
    putField(gsi, 448, 576, info.userData);
    QByteArray out = gsi;
    for (int i = 0; i < captions.size(); ++i) {
        const Caption& c = captions.at(i);
        QByteArray tti(128, static_cast<char>(0x8f));
        tti[0] = 0;
        tti[1] = static_cast<char>((i + 1) & 0xff);
        tti[2] = static_cast<char>(((i + 1) >> 8) & 0xff);
        tti[3] = static_cast<char>(0xff); // the last block of its subtitle
        tti[4] = 0;
        putTimecode(tti, 5, c.start, rate);
        putTimecode(tti, 9, c.end, rate);
        tti[13] = 20; // vertical position, near the bottom (ours)
        tti[14] = 2;  // centred
        tti[15] = 0;
        int at = 16;
        const QStringList lines = c.text.split(QLatin1Char('\n'));
        for (int l = 0; l < lines.size() && at < 128; ++l) {
            if (l > 0) tti[at++] = static_cast<char>(0x8a);
            for (QChar ch : lines.at(l)) {
                if (at >= 128) break;
                const ushort u = ch.unicode();
                tti[at++] = u >= 0x20 && u <= 0x7e ? static_cast<char>(u) : '?';
            }
        }
        out += tti;
    }
    return out;
}

// ---------------------------------------------------------------- captions in sequences

QList<Caption> sequenceCaptions(const Sequence& sequence, const QStringList& tracks, bool topWins) {
    // Per track, highest number first when the top wins.
    QList<const Track*> video;
    for (const Track& t : sequence.tracks()) {
        if (t.kind != TrackKind::Video || (!tracks.isEmpty() && !tracks.contains(t.name()))) continue;
        video << &t;
    }
    std::sort(video.begin(), video.end(), [&](const Track* a, const Track* b) {
        const auto ra = parseTrackName(a->name());
        const auto rb = parseTrackName(b->name());
        return topWins ? (ra ? ra->number : 0) > (rb ? rb->number : 0) : (ra ? ra->number : 0) < (rb ? rb->number : 0);
    });
    QList<Caption> out;
    for (const Track* t : video) {
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            const std::int64_t start = at;
            at += s.length;
            if (!s.effect || !isSubCap(*s.effect)) continue;
            const QString text = captionText(*s.effect);
            if (text.trimmed().isEmpty()) continue;
            const Caption c{start, at, text};
            const bool overlaps = std::any_of(out.begin(), out.end(), [&](const Caption& o) { return o.start < c.end && c.start < o.end; });
            if (!overlaps) out << c;
        }
    }
    std::sort(out.begin(), out.end(), [](const Caption& a, const Caption& b) { return a.start < b.start; });
    return out;
}

std::optional<std::pair<std::int64_t, std::int64_t>> subcapSegmentRange(const Sequence& sequence, const QString& track, std::int64_t frame) {
    const Track* t = sequence.track(track);
    if (!t) return std::nullopt;
    std::int64_t start = 0;
    const int i = t->segmentAt(frame, &start);
    if (i < 0) return std::nullopt;
    const Segment& s = t->segments.at(i);
    if (!s.effect || !isSubCap(*s.effect)) return std::nullopt;
    return std::make_pair(start, start + s.length);
}

bool importCaptions(Sequence& sequence, const QString& track, std::int64_t frame, const QList<Caption>& captions, QString* error) {
    const auto range = subcapSegmentRange(sequence, track, frame);
    if (!range) {
        if (error) *error = QObject::tr("There is no SubCap effect at the position on %1.").arg(track);
        return false;
    }
    const auto [from, to] = *range;
    for (const Caption& c : captions) {
        if (c.start < from || c.end > to || c.end <= c.start) {
            if (error) *error = QObject::tr("The caption file's timecode is outside the effect's: no import.");
            return false;
        }
    }
    Sequence edited = sequence;
    for (const Caption& c : captions) {
        if (c.start > from) edited.addEdit(track, c.start);
        if (c.end < to) edited.addEdit(track, c.end);
    }
    Track* t = edited.track(track);
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t start = at;
        at += s.length;
        if (start < from || at > to || !s.effect || !isSubCap(*s.effect)) continue;
        QString text;
        for (const Caption& c : captions) {
            if (c.start <= start && at <= c.end) text = c.text;
        }
        setCaptionText(*s.effect, text);
    }
    sequence = edited;
    return true;
}

QString subcapSyncMode(const Effect& e, const QString& name) {
    const QString mode = e.parameters.value(QLatin1String(kSubCapPropertiesCategory)).toObject().value(QLatin1String(kSynchronizeKey)).toObject().value(name).toString();
    return mode == QLatin1String("Clip") || mode == QLatin1String("Track") ? mode : QStringLiteral("Sequence");
}

int synchronizeSubCap(Sequence& sequence, const QString& track, std::int64_t frame, const Effect& before) {
    const Track* home = sequence.track(track);
    if (!home) return 0;
    std::int64_t start = 0;
    const int index = home->segmentAt(frame, &start);
    if (index < 0 || !home->segments[index].effect || !isSubCap(*home->segments[index].effect)) return 0;
    const Effect after = *home->segments[index].effect;
    // What changed, per category and name; the Synchronize choices themselves go to the whole sequence (ours).
    struct Change {
        QString category, name;
        QJsonValue value;
        QString mode;
    };
    QList<Change> changes;
    for (const char* category : {kSubCapCategory, kSubCapPropertiesCategory}) {
        const QJsonObject was = before.parameters.value(QLatin1String(category)).toObject();
        const QJsonObject now = after.parameters.value(QLatin1String(category)).toObject();
        QStringList names = now.keys();
        for (const QString& k : was.keys()) {
            if (!names.contains(k)) names << k;
        }
        for (const QString& name : names) {
            if (name == QLatin1String(kCaptionTextKey) || was.value(name) == now.value(name)) continue;
            const bool sync = name == QLatin1String(kSynchronizeKey);
            changes << Change{QString::fromLatin1(category), name, now.value(name), sync ? QStringLiteral("Sequence") : subcapSyncMode(after, name)};
        }
    }
    int changed = 0;
    QStringList names;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Video) names << t.name();
    }
    for (const QString& name : names) {
        Track& t = *sequence.track(name);
        for (int i = 0; i < t.segments.size(); ++i) {
            Segment& s = t.segments[i];
            if (!s.effect || !isSubCap(*s.effect) || (name == track && i == index)) continue;
            bool touched = false;
            for (const Change& c : changes) {
                if (c.mode == QLatin1String("Clip") || (c.mode == QLatin1String("Track") && name != track)) continue;
                QJsonObject category = s.effect->parameters.value(c.category).toObject();
                if (c.value.isUndefined()) category.remove(c.name);
                else category.insert(c.name, c.value);
                s.effect->parameters.insert(c.category, category);
                touched = true;
            }
            if (touched) ++changed;
        }
    }
    return changed;
}

StlInfo subcapStlInfo(const Sequence& sequence) {
    StlInfo info;
    for (const Track& t : sequence.tracks()) {
        for (const Segment& s : t.segments) {
            if (!s.effect || !isSubCap(*s.effect)) continue;
            const QJsonObject p = s.effect->parameters.value(QLatin1String(kSubCapPropertiesCategory)).toObject();
            auto text = [&](const char* name) { return p.value(QLatin1String(name)).toString(); };
            info.publisher = text("Publisher");
            info.countryOfOrigin = text("Country of Origin");
            info.editorName = text("Editor's Name");
            info.editorContact = text("Editor's Contact Details");
            info.translatorName = text("Translator's Name");
            info.translatorContact = text("Translator's Contact Details");
            info.userData = text("Custom");
            info.revisionDate = QDate::fromString(text("Revision Date"), Qt::ISODate);
            info.creationDate = QDate::fromString(text("Creation Date"), Qt::ISODate);
            info.languageCode = text("Language");
            info.originalProgramTitle = text("Original Program Title");
            info.originalEpisodeTitle = text("Original Episode Title");
            info.translatedProgramTitle = text("Translated Program Title");
            info.translatedEpisodeTitle = text("Translated Episode Title");
            return info;
        }
    }
    return info;
}

} // namespace lumenis::model
