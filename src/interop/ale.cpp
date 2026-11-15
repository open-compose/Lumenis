#include "lumenis/interop/ale.h"

#include "import_common.h"
#include "text_common.h"

#include "lumenis/core/mob_id.h"
#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

// Avid Log Exchange shot logs, read and written (ale.h; the format and what maps in
// docs/spec/ale.md). The reader is lenient where logs in the field differ from the guide's
// specification (p.106-112): titles are matched to the bin's standard headings without regard to
// case, rows with fewer cells than titles are padded, and lines may end in CR LF, LF or CR. Clips
// read from a log are new clips with new MobIDs; their tape (or the file they were logged from) is
// their source.

namespace lumenis::interop::ale {

namespace {

using detail::Notes;

// Columns read into the clip's members rather than its fields, or not kept: the dates, Color and
// Lock are shown from the item's own members, and FPS is its edit rate.
bool isMemberColumn(const QString& title) {
    static const QStringList members{QStringLiteral("Name"),          QStringLiteral("Tracks"),        QStringLiteral("Start"),
                                     QStringLiteral("End"),           QStringLiteral("Duration"),      QStringLiteral("FPS"),
                                     QStringLiteral("Tape"),          QStringLiteral("Creation Date"), QStringLiteral("Modified Date"),
                                     QStringLiteral("Color"),         QStringLiteral("Lock")};
    return members.contains(title);
}

// The standard bin heading a title names (any case), or empty.
QString standardHeading(const QString& title) {
    static const QHash<QString, QString> byLower = [] {
        QHash<QString, QString> h;
        for (const QString& heading : model::binHeadings()) h.insert(heading.toLower(), heading);
        return h;
    }();
    return byLower.value(title.toLower());
}

// A log's Tracks value ("V", "VA1A2", "A1A2", "D", p.110) as the bin's Tracks column ("V1 A1-2");
// a value in the bin's own form is taken too. Empty when it names no track.
QString tracksFromLog(const QString& text) {
    const QString t = text.trimmed().toUpper();
    static const QRegularExpression logForm(QStringLiteral("^(V\\d*)?((A\\d+)*)(D\\d*)?$"));
    const auto m = logForm.match(t);
    if (!t.isEmpty() && m.hasMatch() && !t.contains(QLatin1Char(' '))) {
        QList<int> video;
        QList<int> audio;
        if (!m.captured(1).isEmpty()) video << std::max(1, m.captured(1).mid(1).toInt());
        static const QRegularExpression audioTrack(QStringLiteral("A(\\d+)"));
        for (auto it = audioTrack.globalMatch(m.captured(2)); it.hasNext();) audio << it.next().captured(1).toInt();
        QString list = detail::trackList(video, audio);
        if (!m.captured(4).isEmpty()) list = (list + QStringLiteral(" D1")).trimmed();
        return list;
    }
    QList<int> video;
    QList<int> audio;
    bool data = false;
    for (const QString& name : model::parseTrackList(text)) {
        const auto ref = model::parseTrackName(name);
        if (!ref) continue;
        if (ref->kind == model::TrackKind::Video) video << ref->number;
        if (ref->kind == model::TrackKind::Audio) audio << ref->number;
        if (ref->kind == model::TrackKind::Data) data = true;
    }
    const QString list = detail::trackList(video, audio);
    return data ? (list + QStringLiteral(" D1")).trimmed() : list;
}

// The bin's Tracks column as a log writes it: V, then A1A2..., then D ("VA1A2", p.110).
QString tracksForLog(const QString& tracks, Notes& notes) {
    QString out;
    QList<int> audio;
    bool video = false;
    bool data = false;
    for (const QString& name : model::parseTrackList(tracks)) {
        const auto ref = model::parseTrackName(name);
        if (!ref) continue;
        if (ref->kind == model::TrackKind::Video) {
            if (video) notes.add(QStringLiteral("video tracks past V1 not in the log's Tracks"));
            video = true;
        } else if (ref->kind == model::TrackKind::Audio) {
            audio << ref->number;
        } else {
            data = true;
        }
    }
    std::sort(audio.begin(), audio.end());
    if (video) out += QLatin1Char('V');
    for (int a : audio) out += QStringLiteral("A%1").arg(a);
    if (data) out += QLatin1Char('D');
    if (audio.size() > 4) notes.add(QStringLiteral("clips with more than four audio tracks (logs carry four, p.110)"));
    return out;
}

// VIDEO_FORMAT for a clip's Format column ("1080i/59.94"): 1080, 720, NTSC or PAL; 1080 when it
// gives no size.
QString videoFormatOf(const QString& format) {
    if (format.isEmpty()) return QStringLiteral("1080");
    const detail::PictureSize p = detail::pictureSizeOf(format);
    if (p.height == 486 || p.height == 480) return QStringLiteral("NTSC");
    if (p.height == 576) return QStringLiteral("PAL");
    if (p.height == 720) return QStringLiteral("720");
    return QStringLiteral("1080");
}

class Importer {
public:
    explicit Importer(const ImportOptions& options) : options_(options) {
        if (!options_.when.isValid()) options_.when = QDateTime::currentDateTimeUtc();
    }

    ImportResult run(const QString& text) {
        enum class Section { None, Heading, Column, Data };
        Section section = Section::None;
        const QStringList lines = detail::textLines(text);
        for (int i = 0; i < lines.size(); ++i) {
            const QString& line = lines.at(i);
            const QString word = line.trimmed();
            if (word.compare(QLatin1String("Heading"), Qt::CaseInsensitive) == 0) {
                section = Section::Heading;
                continue;
            }
            if (word.compare(QLatin1String("Column"), Qt::CaseInsensitive) == 0) {
                section = Section::Column;
                continue;
            }
            if (word.compare(QLatin1String("Data"), Qt::CaseInsensitive) == 0) {
                section = Section::Data;
                if (titles_.isEmpty()) notes_.add(QStringLiteral("a Data section without column titles"));
                continue;
            }
            if (word.isEmpty()) continue;
            switch (section) {
            case Section::None:
                notes_.add(QStringLiteral("lines before the Heading left out"));
                break;
            case Section::Heading: heading(line); break;
            case Section::Column:
                if (titles_.isEmpty()) {
                    for (const QString& t : line.split(QLatin1Char('\t'))) titles_ << t.trimmed();
                } else {
                    notes_.add(QStringLiteral("a second line of column titles left out"));
                }
                break;
            case Section::Data: data(i + 1, line); break;
            }
        }
        if (titles_.isEmpty()) notes_.add(QStringLiteral("no Column section"));
        else if (!titles_.contains(QStringLiteral("Name"))) notes_.add(QStringLiteral("no Name column (it is required, p.106)"));
        result_.warnings = notes_.list();
        return result_;
    }

private:
    // A global title: its name, a tab, its value (spaces taken too, for logs edited by hand). Some
    // writers put several titles on one line ("FIELD_DELIM<tab>TABS<tab>FPS<tab>23.98"): pairs.
    void heading(const QString& line) {
        QStringList parts = line.trimmed().split(QLatin1Char('\t'));
        if (parts.size() < 2) {
            const qsizetype sep = line.trimmed().indexOf(QRegularExpression(QStringLiteral("\\s")));
            parts = sep < 0 ? QStringList{line.trimmed(), QString()} : QStringList{line.trimmed().left(sep), line.trimmed().mid(sep)};
        }
        for (qsizetype i = 0; i < parts.size(); i += 2) heading(parts.at(i).trimmed(), i + 1 < parts.size() ? parts.at(i + 1).trimmed() : QString());
    }

    void heading(const QString& key, const QString& value) {
        result_.heading.append({key, value});
        const QString upper = key.toUpper();
        if (upper == QLatin1String("FIELD_DELIM") && value.toUpper() != QLatin1String("TABS")) {
            notes_.add(QStringLiteral("FIELD_DELIM %1 read as tabs").arg(value));
        } else if (upper == QLatin1String("FPS")) {
            fps_ = rateFromFps(value);
            if (!fps_) notes_.add(QStringLiteral("FPS %1 not read").arg(value));
        } else if (upper == QLatin1String("TAPE")) {
            tape_ = value;
        }
    }

    // A data line: a clip, or a warning saying why not.
    void data(int lineNo, const QString& line) {
        QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() > titles_.size()) {
            // Trailing empty cells are harmless; values past the titles are not read.
            while (cells.size() > titles_.size() && cells.last().trimmed().isEmpty()) cells.removeLast();
            if (cells.size() > titles_.size()) notes_.add(QStringLiteral("values past the last column left out"));
        }
        QHash<QString, QString> value;
        for (qsizetype c = 0; c < titles_.size() && c < cells.size(); ++c) value.insert(titles_.at(c), cells.at(c).trimmed());
        auto cell = [&](const QString& title) {
            if (value.contains(title)) return value.value(title);
            for (auto it = value.cbegin(); it != value.cend(); ++it) {
                if (it.key().compare(title, Qt::CaseInsensitive) == 0) return it.value();
            }
            return QString();
        };
        const QString name = cell(QStringLiteral("Name"));
        if (name.isEmpty()) {
            notes_.add(QStringLiteral("lines without a Name left out"));
            return;
        }
        Rational rate = fps_ ? *fps_ : options_.rate.value_or(Rational(24000, 1001));
        if (const QString fps = cell(QStringLiteral("FPS")); !fps.isEmpty()) {
            if (const auto r = rateFromFps(fps)) rate = *r;
        }
        if (!fps_ && !options_.rate) notes_.add(QStringLiteral("no FPS in the Heading: read as 23.976"));
        bool drop = false;
        const auto start = detail::timecodeFrames(cell(QStringLiteral("Start")), rate, &drop);
        if (!start) {
            notes_.add(QStringLiteral("clips without a Start timecode left out"));
            return;
        }
        const std::int64_t day = TimecodeFormat::forRate(rate, drop).framesPerDay();
        std::optional<std::int64_t> length;
        if (const auto end = detail::timecodeFrames(cell(QStringLiteral("End")), rate)) length = ((*end - *start) % day + day) % day;
        if (const auto duration = detail::timecodeFrames(cell(QStringLiteral("Duration")), rate)) {
            // "Media Composer compares the video duration to the video out minus the video in ... and
            // does not bring the clip into the bin" when they differ (p.114).
            if (length && *length != *duration) {
                notes_.add(QStringLiteral("line %1: %2: Start, End and Duration disagree; left out").arg(lineNo).arg(name));
                return;
            }
            length = *duration;
        }
        if (!length) {
            notes_.add(QStringLiteral("clips without an End or Duration left out"));
            return;
        }
        model::BinItem item;
        item.id = model::newItemId();
        item.mobId = MobId::generate();
        item.name = name;
        item.created = options_.when;
        item.modified = options_.when;
        item.editRate = rate;
        item.durationFrames = *length;
        const QString startText = detail::timecodeText(*start, rate, drop);
        item.fields.insert(QStringLiteral("Start"), startText);
        item.fields.insert(QStringLiteral("End"), detail::timecodeText(*start + *length, rate, drop));
        QString tracks = tracksFromLog(cell(QStringLiteral("Tracks")));
        if (tracks.isEmpty()) {
            notes_.add(QStringLiteral("clips without Tracks read as V1"));
            tracks = QStringLiteral("V1");
        }
        item.fields.insert(QStringLiteral("Tracks"), tracks);
        item.kind = tracks.contains(QLatin1Char('V')) ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;
        // The source: the clip's Tape, its Source File, the global TAPE, or the log file's name.
        const QString sourceFile = cell(QStringLiteral("Source File"));
        QString tape = cell(QStringLiteral("Tape"));
        if (tape.isEmpty() && sourceFile.isEmpty()) tape = !tape_.isEmpty() ? tape_ : options_.tape;
        model::ClipSource source;
        source.mob = MobId::generate();
        source.startTimecode = startText;
        if (!tape.isEmpty()) {
            source.kind = model::ClipSource::Kind::Tape;
            source.name = tape;
            item.fields.insert(QStringLiteral("Tape"), tape);
            item.sources << source;
        } else if (!sourceFile.isEmpty()) {
            source.kind = model::ClipSource::Kind::Import;
            source.name = sourceFile;
            const QString folder = cell(QStringLiteral("Source Path"));
            source.path = folder.isEmpty() || QFileInfo(sourceFile).isAbsolute() ? sourceFile : folder + QLatin1Char('/') + sourceFile;
            item.sources << source;
        }
        // The other columns: standard headings (any case) into the fields, the rest as custom
        // columns, each only when it has a value.
        for (const QString& title : titles_) {
            if (title.isEmpty()) continue;
            const QString v = value.value(title);
            const QString standard = standardHeading(title);
            if (isMemberColumn(standard.isEmpty() ? title : standard)) {
                if ((standard == QLatin1String("Color") || standard == QLatin1String("Lock")) && !v.isEmpty()) {
                    notes_.add(QStringLiteral("%1 column not read").arg(standard));
                }
                continue;
            }
            if (!standard.isEmpty()) {
                if (!v.isEmpty()) item.fields.insert(standard, v);
            } else {
                if (!result_.customColumns.contains(title)) result_.customColumns << title;
                if (!v.isEmpty()) item.fields.insert(title, v);
            }
        }
        result_.items << item;
    }

    ImportOptions options_;
    ImportResult result_;
    QStringList titles_;
    std::optional<Rational> fps_;
    QString tape_;
    Notes notes_;
};

} // namespace

QString fpsText(const Rational& rate) {
    if (rate.den() == 1) return QString::number(rate.num());
    if (rate == Rational(24000, 1001)) return QStringLiteral("23.98");
    return QString::number(rate.toDouble(), 'f', 2);
}

std::optional<Rational> rateFromFps(const QString& text) {
    bool ok = false;
    const double v = text.trimmed().toDouble(&ok);
    if (!ok || !(v > 0.0) || v > 1000.0) return std::nullopt;
    const auto whole = static_cast<std::int64_t>(std::llround(v));
    if (std::abs(v - static_cast<double>(whole)) < 1e-6) return Rational(whole);
    // 23.98, 23.976, 29.97, 59.94: n×1000/1001 written short.
    const auto ntscBase = static_cast<std::int64_t>(std::llround(v * 1.001));
    if (std::abs(v - static_cast<double>(ntscBase) * 1000.0 / 1001.0) < 0.01) return Rational(ntscBase * 1000, 1001);
    return detail::exactNtscRate(Rational(static_cast<std::int64_t>(std::llround(v * 1000.0)), 1000));
}

ImportResult importText(const QString& text, const ImportOptions& options) {
    try {
        return Importer(options).run(text);
    } catch (const std::exception& e) {
        ImportResult r;
        r.warnings << QStringLiteral("the log could not be read: %1").arg(QString::fromUtf8(e.what()));
        return r;
    }
}

std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options, QString* error) {
    const auto text = detail::readText(path, error);
    if (!text) return std::nullopt;
    ImportOptions o = options;
    if (o.tape.isEmpty()) o.tape = QFileInfo(path).completeBaseName();
    return importText(*text, o);
}

bool isShotLog(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("ale")) return true;
    // A log may be any text file: "You can use the file name extension .txt, but it is not required"
    // (p.106). Only .txt files are looked into, so that media files are not read.
    if (suffix != QLatin1String("txt")) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QString head = detail::decodeText(file.read(256));
    for (const QString& line : detail::textLines(head)) {
        if (line.trimmed().isEmpty()) continue;
        return line.trimmed().compare(QLatin1String("Heading"), Qt::CaseInsensitive) == 0;
    }
    return false;
}

ExportResult exportText(const QList<model::BinItem>& items, const ExportOptions& options) {
    ExportResult result;
    Notes notes;
    QList<const model::BinItem*> clips;
    for (const model::BinItem& item : items) {
        const bool clip = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip ||
                          (options.subclips && item.kind == model::BinItemKind::Subclip);
        if (clip) {
            clips << &item;
        } else {
            notes.add(QStringLiteral("items that are not %1 left out").arg(options.subclips ? QStringLiteral("master clips or subclips")
                                                                                            : QStringLiteral("master clips")));
        }
    }
    if (clips.isEmpty()) {
        result.error = QStringLiteral("no clips to export");
        result.warnings = notes.list();
        return result;
    }
    const Rational rate = clips.first()->editRate;
    const bool mixedRates = std::any_of(clips.begin(), clips.end(), [&](const model::BinItem* c) { return c->editRate != rate; });
    auto tapeOf = [](const model::BinItem& c) {
        QString tape = c.fields.value(QStringLiteral("Tape"));
        for (const model::ClipSource& s : c.sources) {
            if (tape.isEmpty() && s.kind == model::ClipSource::Kind::Tape) tape = s.name;
        }
        return tape;
    };
    const bool anyTape = std::any_of(clips.begin(), clips.end(), [&](const model::BinItem* c) { return !tapeOf(*c).isEmpty(); });
    QStringList titles{QStringLiteral("Name"), QStringLiteral("Tracks"), QStringLiteral("Start"), QStringLiteral("End"),
                       QStringLiteral("Duration")};
    if (anyTape) titles << QStringLiteral("Tape");
    if (mixedRates) titles << QStringLiteral("FPS");
    QStringList rest = options.columns;
    if (rest.isEmpty()) {
        // Every heading the clips have: standard ones in the guide's order, then the others.
        QStringList others;
        for (const model::BinItem* c : clips) {
            for (auto it = c->fields.cbegin(); it != c->fields.cend(); ++it) {
                if (!model::binHeadings().contains(it.key()) && !others.contains(it.key())) others << it.key();
            }
        }
        std::sort(others.begin(), others.end());
        for (const QString& h : model::binHeadings()) {
            if (std::any_of(clips.begin(), clips.end(), [&](const model::BinItem* c) { return c->fields.contains(h); })) rest << h;
        }
        rest += others;
    }
    for (const QString& h : rest) {
        if (!isMemberColumn(h) && !titles.contains(h) && !h.isEmpty()) titles << h;
    }
    // FIELD_DELIM, VIDEO_FORMAT, AUDIO_FORMAT and FPS are four of the 64 titles (p.106).
    if (titles.size() + 4 > 64) notes.add(QStringLiteral("more than 64 titles (the limit, p.106)"));

    QStringList lines;
    if (options.tabDelimited) {
        lines << titles.join(QLatin1Char('\t'));
    } else {
        lines << QStringLiteral("Heading") << QStringLiteral("FIELD_DELIM\tTABS")
              << QStringLiteral("VIDEO_FORMAT\t%1")
                     .arg(options.videoFormat.isEmpty() ? videoFormatOf(clips.first()->fields.value(QStringLiteral("Format")))
                                                        : options.videoFormat)
              << QStringLiteral("AUDIO_FORMAT\t%1").arg(options.audioFormat.isEmpty() ? QStringLiteral("48kHz") : options.audioFormat)
              << QStringLiteral("FPS\t%1").arg(fpsText(rate)) << QString() << QStringLiteral("Column")
              << titles.join(QLatin1Char('\t')) << QString() << QStringLiteral("Data");
    }
    for (const model::BinItem* c : clips) {
        bool drop = false;
        const std::int64_t start = detail::timecodeFrames(c->fields.value(QStringLiteral("Start")), c->editRate, &drop).value_or(0);
        if (!c->fields.contains(QStringLiteral("Start"))) notes.add(QStringLiteral("clips without a Start written from 00:00:00:00"));
        QStringList row;
        for (const QString& t : titles) {
            QString v;
            if (t == QLatin1String("Name")) v = c->name;
            else if (t == QLatin1String("Tracks")) v = tracksForLog(c->fields.value(QStringLiteral("Tracks"), QStringLiteral("V1")), notes);
            else if (t == QLatin1String("Start")) v = detail::timecodeText(start, c->editRate, drop);
            else if (t == QLatin1String("End")) v = detail::timecodeText(start + c->durationFrames, c->editRate, drop);
            else if (t == QLatin1String("Duration")) v = detail::timecodeText(c->durationFrames, c->editRate, drop);
            else if (t == QLatin1String("Tape")) v = tapeOf(*c);
            else if (t == QLatin1String("FPS")) v = fpsText(c->editRate);
            else v = c->fields.value(t);
            if (v.contains(QLatin1Char('\t')) || v.contains(QLatin1Char('\n')) || v.contains(QLatin1Char('\r'))) {
                notes.add(QStringLiteral("tabs and line breaks in values written as spaces"));
                v.replace(QRegularExpression(QStringLiteral("[\\t\\r\\n]+")), QStringLiteral(" "));
            }
            row << v;
        }
        lines << row.join(QLatin1Char('\t'));
    }
    result.ok = true;
    result.clips = static_cast<int>(clips.size());
    result.text = lines.join(QLatin1String("\r\n")) + QStringLiteral("\r\n");
    result.warnings = notes.list();
    return result;
}

ExportResult exportFile(const QList<model::BinItem>& items, const QString& path, const ExportOptions& options) {
    ExportResult result = exportText(items, options);
    if (!result.ok) return result;
    QString error;
    if (!detail::writeText(path, result.text, &error)) {
        result.ok = false;
        result.error = error;
    }
    return result;
}

} // namespace lumenis::interop::ale
