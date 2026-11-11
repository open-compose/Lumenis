// Film cut lists (cut_list.h; Editing Guide p.939-944, p.960-966; FX Guide p.79, p.412).
#include "lumenis/interop/cut_list.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/sync_dupes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace lumenis::interop::cutlist {

QString formatName(Format format) {
    switch (format) {
    case Format::Columnar: return QStringLiteral("Columnar");
    case Format::TabbedLists: return QStringLiteral("TabbedLists");
    case Format::WebLists: return QStringLiteral("WebLists");
    case Format::Xml: return QStringLiteral("XML");
    }
    return {};
}

std::optional<Format> formatFromName(const QString& name) {
    for (const Format f : {Format::Columnar, Format::TabbedLists, Format::WebLists, Format::Xml}) {
        if (formatName(f) == name) return f;
    }
    return std::nullopt;
}

std::optional<FilmNumber> parseFilmNumber(const QString& text, int framesPerFoot) {
    static const QRegularExpression tail(QStringLiteral("^(.*?)\\s*(\\d+)\\+(\\d+)\\s*$"));
    const QRegularExpressionMatch m = tail.match(text);
    if (!m.hasMatch()) {
        // A frame count alone ("Frame Count" edge types, p.966-967: "FXS32v01-000001").
        static const QRegularExpression count(QStringLiteral("^(.*?)[-\\s]?(\\d+)\\s*$"));
        const QRegularExpressionMatch c = count.match(text);
        if (framesPerFoot != 0 || !c.hasMatch()) return std::nullopt;
        return FilmNumber{c.captured(1).trimmed(), c.captured(2).toLongLong()};
    }
    const std::int64_t feet = m.captured(2).toLongLong();
    const std::int64_t frames = m.captured(3).toLongLong();
    const int perFoot = framesPerFoot > 0 ? framesPerFoot : 16;
    if (frames >= perFoot) return std::nullopt;
    return FilmNumber{m.captured(1).trimmed(), feet * perFoot + frames};
}

QString footage(std::int64_t frames, int framesPerFoot) {
    const QString sign = frames < 0 ? QStringLiteral("-") : QString();
    frames = std::abs(frames);
    if (framesPerFoot <= 0) return sign + QString::number(frames);
    return sign + QStringLiteral("%1+%2").arg(frames / framesPerFoot).arg(frames % framesPerFoot, 2, 10, QLatin1Char('0'));
}

QString filmNumberText(const FilmNumber& number, std::int64_t offset, int framesPerFoot) {
    const QString counted = framesPerFoot > 0 ? footage(number.frames + offset, framesPerFoot)
                                               : QString::number(number.frames + offset).rightJustified(6, QLatin1Char('0'));
    return number.prefix.isEmpty() ? counted : number.prefix + QLatin1Char(' ') + counted;
}

int framesPerFootOf(const QString& filmType) {
    if (filmType == QLatin1String("16mm")) return 40;
    if (filmType == QLatin1String("35mm, 4 perf")) return 16;
    return 0;
}

QString sortFieldName(SortField field) {
    switch (field) {
    case SortField::None: return QStringLiteral("None");
    case SortField::Labroll: return QStringLiteral("Labroll");
    case SortField::Camroll: return QStringLiteral("Camroll");
    case SortField::Soundroll: return QStringLiteral("Soundroll");
    case SortField::SceneTake: return QStringLiteral("Scene and Take");
    case SortField::Name: return QStringLiteral("Name");
    case SortField::Reel: return QStringLiteral("Reel #");
    }
    return {};
}

namespace {

// One event of a list: a segment of a listed track.
struct Event {
    int number = 0;
    QString track;
    std::int64_t record = 0; // sequence frame
    std::int64_t length = 0;
    const model::Segment* segment = nullptr;
    std::optional<model::BinItem> clip;
    QString name;
    // The frame reference: "KN" key numbers, "Ink" ink numbers, "TC" timecode; and the first frame's number.
    QString referenceKind;
    std::optional<FilmNumber> reference;
    std::int64_t sourceIn = 0;
    int optical = 0; // the optical's number, 0 for a straight cut
    int dupeSet = -1;
    QStringList flags;

    QString field(const char* heading) const { return clip ? clip->fields.value(QLatin1String(heading)) : QString(); }
};

// A list as rows of columns. `key` names it for the web pages and XML ("assemble", "scene", "optical", "dupe", "pull");
// `events` gives, for each row, the assemble list's event number it is about (0 for none), for the links and the XML's
// columns.
// `group` sets one reel's pages apart in WebLists ("" for the first, "2", "Old" ...); a row's event below 0 is an event
// of the old sequence, whose assemble list is in `oldGroup` (change lists).
struct Table {
    QString key;
    QString name;
    QString group;
    QString oldGroup;
    QStringList head;
    QList<QStringList> rows;
    QList<int> events;
    void add(int event, const QStringList& cells) {
        rows << cells;
        events << event;
    }
};

QString columnar(const QList<Table>& tables, const Options& o) {
    QStringList out;
    out << (o.title.isEmpty() ? QStringLiteral("CUT LIST") : o.title);
    for (const Table& t : tables) {
        out << QString() << t.name;
        if (o.format == Format::TabbedLists) {
            out << t.head.join(QLatin1Char('\t'));
            for (const QStringList& r : t.rows) out << r.join(QLatin1Char('\t'));
            continue;
        }
        // Columnar: each column as wide as its widest cell, two spaces apart.
        QList<int> widths;
        for (const QString& h : t.head) widths << static_cast<int>(h.size());
        for (const QStringList& r : t.rows) {
            for (int i = 0; i < r.size(); ++i) {
                if (i >= widths.size()) widths << 0;
                widths[i] = std::max(widths[i], static_cast<int>(r[i].size()));
            }
        }
        auto line = [&](const QStringList& cells) {
            QString s;
            for (int i = 0; i < cells.size(); ++i) s += (i ? QStringLiteral("  ") : QString()) + cells[i].leftJustified(widths[i]);
            return s.remove(QRegularExpression(QStringLiteral("\\s+$")));
        };
        out << line(t.head);
        QString rule;
        for (int i = 0; i < widths.size(); ++i) rule += (i ? QStringLiteral("  ") : QString()) + QString(widths[i], QLatin1Char('-'));
        out << rule;
        for (const QStringList& r : t.rows) out << line(r);
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString escaped(const QString& text) { return text.toHtmlEscaped(); }

// The XML template (p.957): "a file that includes all the information about the list: it shows you every column
// heading and every custom column". Ours: <CutList> with a <List> per list, each <Row> its cells named by heading and,
// for a row about an event, the clip's bin columns as <Column> elements.
QString xml(const QList<Table>& tables, const Options& o, const std::function<const model::BinItem*(const QString& group, int event)>& clipOf) {
    QStringList out;
    out << QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    out << QStringLiteral("<CutList title=\"%1\" framesPerFoot=\"%2\">").arg(escaped(o.title)).arg(o.framesPerFoot);
    for (const Table& t : tables) {
        out << (t.group.isEmpty() ? QStringLiteral("  <List type=\"%1\" name=\"%2\">").arg(t.key, escaped(t.name))
                                  : QStringLiteral("  <List type=\"%1\" name=\"%2\" group=\"%3\">").arg(t.key, escaped(t.name), escaped(t.group)));
        for (int r = 0; r < t.rows.size(); ++r) {
            out << (t.events[r] > 0   ? QStringLiteral("    <Row event=\"%1\">").arg(t.events[r])
                    : t.events[r] < 0 ? QStringLiteral("    <Row oldEvent=\"%1\">").arg(-t.events[r])
                                      : QStringLiteral("    <Row>"));
            for (int c = 0; c < t.rows[r].size() && c < t.head.size(); ++c) {
                out << QStringLiteral("      <Cell heading=\"%1\">%2</Cell>").arg(escaped(t.head[c]), escaped(t.rows[r][c]));
            }
            if (const model::BinItem* clip = clipOf(t.group, t.events[r])) {
                QStringList headings = clip->fields.keys();
                std::sort(headings.begin(), headings.end());
                for (const QString& h : headings) {
                    out << QStringLiteral("      <Column heading=\"%1\">%2</Column>").arg(escaped(h), escaped(clip->fields.value(h)));
                }
            }
            out << QStringLiteral("    </Row>");
        }
        out << QStringLiteral("  </List>");
    }
    out << QStringLiteral("</CutList>");
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString pageOf(const QString& key, const QString& group = {}) {
    static const QHash<QString, QString> pages{{QStringLiteral("assemble"), QStringLiteral("CutAsm")},
                                               {QStringLiteral("scene"), QStringLiteral("CutScene")},
                                               {QStringLiteral("optical"), QStringLiteral("CutOpt")},
                                               {QStringLiteral("dupe"), QStringLiteral("CutDupe")},
                                               {QStringLiteral("pull"), QStringLiteral("CutPull")},
                                               {QStringLiteral("change"), QStringLiteral("CutChg")},
                                               {QStringLiteral("changepull"), QStringLiteral("CutChgPull")},
                                               {QStringLiteral("changediscard"), QStringLiteral("CutChgDisc")}};
    return pages.value(key, key) + group + QStringLiteral(".htm");
}

// WebLists (p.957-958): an HTML page per list, "hyper linked, which allows you to jump between lists": the assemble
// list's opticals and dupes link to the optical and dupe lists, whose entries, like the pull list's, link back to the
// event in the assemble list. The style sheet goes in the Files sub folder ("control files are also copied to the Files
// sub folder"). Ours: no banner or logo pictures and no script.
QMap<QString, QByteArray> webLists(const QList<Table>& tables, const Options& o) {
    QMap<QString, QByteArray> files;
    const QString title = escaped(o.title.isEmpty() ? QStringLiteral("Cut List") : o.title);
    QStringList nav;
    for (const Table& t : tables) {
        const QString label = t.group.isEmpty() ? t.name : QStringLiteral("%1 (%2)").arg(t.name, t.group);
        nav << QStringLiteral("<a href=\"%1\">%2</a>").arg(pageOf(t.key, t.group), escaped(label));
    }
    static const QRegularExpression optical(QStringLiteral("Optical (\\d+)"));
    static const QRegularExpression dupe(QStringLiteral("Dupe (\\d+)"));
    auto has = [&](const char* key, const QString& group) {
        return std::any_of(tables.cbegin(), tables.cend(), [&](const Table& t) { return t.key == QLatin1String(key) && t.group == group; });
    };
    for (const Table& t : tables) {
        const bool hasOptical = has("optical", t.group);
        const bool hasDupe = has("dupe", t.group);
        QStringList h;
        h << QStringLiteral("<!DOCTYPE html>") << QStringLiteral("<html><head><meta charset=\"utf-8\">")
          << QStringLiteral("<title>%1 - %2</title>").arg(title, escaped(t.name))
          << QStringLiteral("<link rel=\"stylesheet\" href=\"Files/cutlist.css\"></head><body>")
          << QStringLiteral("<h1>%1</h1>").arg(title) << QStringLiteral("<nav>%1</nav>").arg(nav.join(QStringLiteral(" | ")))
          << QStringLiteral("<h2>%1</h2>").arg(escaped(t.name)) << QStringLiteral("<table>") << QStringLiteral("<tr>");
        for (const QString& head : t.head) h << QStringLiteral("<th>%1</th>").arg(escaped(head));
        h << QStringLiteral("</tr>");
        QSet<QString> anchored;
        for (int r = 0; r < t.rows.size(); ++r) {
            const QStringList& row = t.rows[r];
            const int event = t.events[r];
            // Each row's anchor: the event in the assemble list, the optical or dupe group's first row elsewhere.
            QString anchor;
            if (t.key == QLatin1String("assemble")) anchor = QStringLiteral("e%1").arg(event);
            if (t.key == QLatin1String("optical") || t.key == QLatin1String("dupe")) {
                const QString a = (t.key == QLatin1String("optical") ? QStringLiteral("o") : QStringLiteral("d")) + row.value(0);
                if (!anchored.contains(a)) {
                    anchored.insert(a);
                    anchor = a;
                }
            }
            h << (anchor.isEmpty() ? QStringLiteral("<tr>") : QStringLiteral("<tr id=\"%1\">").arg(anchor));
            for (int c = 0; c < row.size(); ++c) {
                QString cell = escaped(row[c]);
                if (t.key == QLatin1String("assemble") && c == row.size() - 1) {
                    if (hasOptical) cell.replace(optical, QStringLiteral("<a href=\"%1#o\\1\">Optical \\1</a>").arg(pageOf(QStringLiteral("optical"), t.group)));
                    if (hasDupe) cell.replace(dupe, QStringLiteral("<a href=\"%1#d\\1\">Dupe \\1</a>").arg(pageOf(QStringLiteral("dupe"), t.group)));
                } else if (t.key != QLatin1String("assemble") && c == 0 && event != 0) {
                    // An event of the new sequence (or the cut list's), or with a negative number one of the old sequence.
                    const QString page = pageOf(QStringLiteral("assemble"), event > 0 ? t.group : t.oldGroup);
                    cell = QStringLiteral("<a href=\"%1#e%2\">%3</a>").arg(page).arg(std::abs(event)).arg(cell);
                }
                h << QStringLiteral("<td>%1</td>").arg(cell);
            }
            h << QStringLiteral("</tr>");
        }
        h << QStringLiteral("</table></body></html>");
        files.insert(pageOf(t.key, t.group), h.join(QLatin1Char('\n')).toUtf8() + '\n');
    }
    files.insert(QStringLiteral("Files/cutlist.css"),
                 QByteArrayLiteral("body { font-family: sans-serif; margin: 1em; }\n"
                                   "table { border-collapse: collapse; }\n"
                                   "th, td { border: 1px solid #999; padding: 2px 6px; text-align: left; white-space: nowrap; }\n"
                                   "th { background: #ddd; }\n"
                                   "tr:target { background: #ffeb99; }\n"
                                   "nav { margin-bottom: 1em; }\n"));
    return files;
}

// The frame reference of a clip's first frame: key numbers, else ink numbers, else its Start timecode.
void setReference(Event& e, int framesPerFoot) {
    if (!e.clip) return;
    if (const auto kn = parseFilmNumber(e.field("KN Start"), framesPerFoot)) {
        e.referenceKind = QStringLiteral("KN");
        e.reference = kn;
    } else if (const auto ink = parseFilmNumber(e.field("Ink Number"), framesPerFoot)) {
        e.referenceKind = QStringLiteral("Ink");
        e.reference = ink;
    } else if (const auto tc = model::timecodeFrames(e.field("Start"), e.clip->editRate)) {
        e.referenceKind = QStringLiteral("TC");
        e.reference = FilmNumber{QString(), *tc};
    }
}

QString referenceAt(const Event& e, std::int64_t offset, const Options& o) {
    if (!e.reference) return QStringLiteral("-");
    if (e.referenceKind == QLatin1String("TC")) {
        return QString::fromStdString(Timecode(e.reference->frames + offset, model::timecodeFormatOf(*e.clip)).toString());
    }
    return filmNumberText(*e.reference, offset, o.framesPerFoot);
}

// Effects the optical list describes: every effect but the standard film masks ("Cut lists do not describe standard
// film masks ... Custom masks, which are effects, are described in the optical list", p.942).
bool isOptical(const model::Effect& e) {
    return !(e.category == QLatin1String("Film") && e.name.endsWith(QLatin1String("Mask")));
}

// "N 0.25 E 0.40": a previsualization marker's place as compass and X, Y coordinates from the frame's centre
// (FX Guide p.412: "Position is described in terms of compass coordinates and X, Y coordinates").
QString compass(QPointF p) {
    const QString ns = p.y() <= 0 ? QStringLiteral("N") : QStringLiteral("S");
    const QString ew = p.x() >= 0 ? QStringLiteral("E") : QStringLiteral("W");
    return QStringLiteral("%1 %2 %3 %4").arg(ns, QString::number(std::abs(p.y()), 'f', 2), ew, QString::number(std::abs(p.x()), 'f', 2));
}

QString sortKey(const Event& e, SortField f) {
    switch (f) {
    case SortField::None: return {};
    case SortField::Labroll: return e.field("Labroll");
    case SortField::Camroll: return e.field("Camroll");
    case SortField::Soundroll: return e.field("Soundroll");
    case SortField::SceneTake: return e.field("Scene") + QLatin1Char('/') + e.field("Take");
    case SortField::Name: return e.name;
    case SortField::Reel: return e.field("Reel #").isEmpty() ? e.field("Tape") : e.field("Reel #");
    }
    return {};
}

} // namespace

namespace {

// The lists of one sequence, as `o` chooses them (no list chosen: none), with the sequence and its events kept for
// the events' segments. Not copied: the events point into `seq`.
struct Built {
    model::Sequence seq;
    QList<Event> events;
    QList<Table> tables;
};

bool build(Built& built, const model::BinItem& sequence, const Options& o, const QString& group, Result& r) {
    if (sequence.kind != model::BinItemKind::Sequence) {
        r.error = QStringLiteral("Load a sequence to make a cut list.");
        return false;
    }
    built.seq = model::Sequence::fromItem(sequence);
    const model::Sequence& seq = built.seq;
    const int fpf = o.framesPerFoot;
    // The events, track by track in the sequence's order.
    QList<Event>& events = built.events;
    QList<std::pair<QString, const model::Segment*>> previous; // per event, the segment before it on its track
    int opticals = 0;
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Data || (!o.tracks.isEmpty() && !o.tracks.contains(t.name()))) continue;
        std::int64_t at = 0;
        const model::Segment* before = nullptr;
        for (const model::Segment& s : t.segments) {
            if (!s.isFiller()) {
                Event e;
                e.track = t.name();
                e.record = at;
                e.length = s.length;
                e.segment = &s;
                e.sourceIn = s.sourceIn;
                if (o.resolve) e.clip = o.resolve(s.sourceMobId(), s.clipId);
                e.name = e.clip ? e.clip->name : s.name;
                setReference(e, fpf);
                if (!e.reference) r.warnings << QStringLiteral("%1 on %2 has no key numbers, ink numbers or timecode.").arg(e.name, t.name());
                if ((s.effect && isOptical(*s.effect)) || s.transition) e.optical = ++opticals;
                events << e;
                previous << std::pair{t.name(), before};
            }
            before = s.isFiller() ? nullptr : &s;
            at += s.length;
        }
    }
    if (events.isEmpty()) {
        r.error = QStringLiteral("The listed tracks have no material.");
        return false;
    }
    // Dupes: the Dupe Detection's sets, with the Assume Handles (p.962).
    const QList<model::Dupe> dupes = model::dupes(seq, {model::TrackKind::Video, model::TrackKind::Audio}, o.assumeHandles);
    for (Event& e : events) {
        for (const model::Dupe& d : dupes) {
            if (d.track == e.track && d.start < e.record + e.length && d.start + d.length > e.record) e.dupeSet = d.set;
        }
    }
    // Short cuts and jump cuts (p.961).
    for (int i = 0; i < events.size(); ++i) {
        Event& e = events[i];
        // Mark Short Cuts and Mark Jump Cuts are "Not included in XML output" (p.961).
        const bool marks = o.format != Format::Xml;
        if (marks && o.shortCuts && e.length < *o.shortCuts) e.flags << QStringLiteral("Short cut");
        if (marks && o.jumpCuts && i > 0 && events[i - 1].track == e.track && events[i - 1].record + events[i - 1].length == e.record &&
            events[i - 1].segment->clipId == e.segment->clipId && events[i - 1].segment->sourceTrack == e.segment->sourceTrack) {
            const std::int64_t missing = e.sourceIn - (events[i - 1].sourceIn + events[i - 1].length);
            if (missing > 0 && missing < *o.jumpCuts) e.flags << QStringLiteral("Jump cut (%1 missing)").arg(footage(missing, fpf));
        }
        if (e.dupeSet >= 0) e.flags << QStringLiteral("Dupe %1").arg(e.dupeSet + 1);
        if (e.optical) e.flags << QStringLiteral("Optical %1").arg(e.optical);
    }
    auto cells = [&](const Event& e, int number) {
        QStringList c{QString::number(number), e.track, footage(o.startAt + e.record, fpf), footage(o.startAt + e.record + e.length - 1, fpf),
                      footage(e.length, fpf), referenceAt(e, e.sourceIn, o), referenceAt(e, e.sourceIn + e.length - 1, o)};
        if (o.reelNames) c << e.field("Camroll") << e.field("Soundroll");
        if (o.clipNames) c << e.name;
        QStringList notes = e.flags;
        if (o.clipComments && !e.segment->note.isEmpty()) notes << e.segment->note;
        c << notes.join(QStringLiteral("; "));
        return c;
    };
    QStringList head{QStringLiteral("#"), QStringLiteral("Track"), QStringLiteral("Footage In"), QStringLiteral("Footage Out"),
                     QStringLiteral("Duration"), QStringLiteral("First Edge"), QStringLiteral("Last Edge")};
    if (o.reelNames) head << QStringLiteral("Cam Roll") << QStringLiteral("Sound Roll");
    if (o.clipNames) head << QStringLiteral("Clip Name");
    head << QStringLiteral("Comments");
    QList<Table>& tables = built.tables;
    if (o.assemble) {
        // "The assemble list shows the order in which a sequence's clips, optical effects, or standard dissolves and
        // fades are assembled from start to finish in the edited sequence. The assemble list also flags duplicate
        // frames" (p.941).
        Table t{QStringLiteral("assemble"), QStringLiteral("ASSEMBLE LIST"), group, {}, head, {}, {}};
        int n = 0;
        for (const Event& e : events) {
            ++n;
            t.add(n, cells(e, n));
        }
        tables << t;
    }
    if (o.sceneAssemble) {
        // "The Scene/Take column must be filled in within the bin to generate a scene assemble list" (p.942).
        const bool filled = std::all_of(events.cbegin(), events.cend(), [](const Event& e) { return !e.field("Scene").isEmpty(); });
        if (!filled) {
            r.error = QStringLiteral("The Scene and Take columns must be filled in within the bin to generate a scene assemble list.");
            return false;
        }
        // Ours: by scene and take, then reel, assembly order kept within them.
        QList<int> order;
        for (int i = 0; i < events.size(); ++i) order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const auto key = [&](const Event& e) { return std::tuple{e.field("Scene"), e.field("Take"), sortKey(e, SortField::Reel)}; };
            return key(events[a]) < key(events[b]);
        });
        QStringList sceneHead = head;
        sceneHead.insert(2, QStringLiteral("Scene/Take"));
        Table t{QStringLiteral("scene"), QStringLiteral("SCENE ASSEMBLE LIST"), group, {}, sceneHead, {}, {}};
        for (int i : order) {
            QStringList c = cells(events[i], i + 1);
            c.insert(2, events[i].field("Scene") + QLatin1Char('/') + events[i].field("Take"));
            t.add(i + 1, c);
        }
        tables << t;
    }
    if (o.optical && opticals > 0) {
        // "The optical list presents each optical event in the order in which it appears in the sequence and includes
        // specifications based on either ink numbers, Aux ink numbers, or key numbers" (p.942).
        Table t{QStringLiteral("optical"), QStringLiteral("OPTICAL LIST"), group, {},
                {QStringLiteral("Optical"), QStringLiteral("Track"), QStringLiteral("Effect"), QStringLiteral("Footage In"),
                 QStringLiteral("Footage Out"), QStringLiteral("Duration"), QStringLiteral("Layer"), QStringLiteral("First Edge"),
                 QStringLiteral("Last Edge"), QStringLiteral("Clip Name"), QStringLiteral("Notes")},
                {},
                {}};
        for (int i = 0; i < events.size(); ++i) {
            const Event& e = events[i];
            if (!e.optical) continue;
            const model::Segment& s = *e.segment;
            const QString number = QString::number(e.optical);
            auto row = [&](const QString& effect, std::int64_t from, std::int64_t length, const QString& layer, const Event& src,
                           std::int64_t srcIn, const QString& notes) {
                t.add(i + 1, QStringList{number,
                                      e.track,
                                      effect,
                                      footage(o.startAt + from, fpf),
                                      footage(o.startAt + from + length - 1, fpf),
                                      footage(length, fpf),
                                      layer,
                                      referenceAt(src, srcIn, o),
                                      referenceAt(src, srcIn + length - 1, o),
                                      src.name,
                                      notes});
            };
            if (s.transition) {
                // A transition: the outgoing shot's tail (A) and the incoming shot's head (B) over the effect.
                const model::Transition& tr = *s.transition;
                const std::int64_t from = e.record - tr.before();
                const QString effect = tr.effect.name;
                const model::Segment* out = previous[i].second;
                const Event* outgoing = nullptr;
                for (const Event& other : events) {
                    if (other.segment == out) outgoing = &other;
                }
                if (outgoing) row(effect, from, tr.duration, QStringLiteral("A"), *outgoing, outgoing->sourceIn + outgoing->length - tr.before(), {});
                row(effect, from, tr.duration, QStringLiteral("B"), e, e.sourceIn - tr.before(), {});
            }
            if (s.effect && isOptical(*s.effect)) {
                const model::Effect& fx = *s.effect;
                row(fx.name, e.record, e.length, QStringLiteral("A"), e, e.sourceIn, {});
                const model::EffectSpan span(std::max<std::int64_t>(e.length, 1));
                if (o.keyFrames) {
                    // "Key Frames: Controls whether or not optical keyframes from the sequence are shown" (p.961).
                    for (const QString& key : model::keyframedParameters(fx)) {
                        const QString category = key.section(QLatin1Char('/'), 0, 0), name = key.section(QLatin1Char('/'), 1);
                        const model::ParameterKeyframes keys = model::keyframesOf(fx, category, name);
                        QStringList at;
                        for (const model::EffectKeyframe& k : keys.keys) {
                            const auto frame = static_cast<std::int64_t>(std::llround(span.frameOf(model::keyframeTime(keys, k, span))));
                            at << footage((o.opticalFootage ? 0 : o.startAt + e.record) + frame, fpf);
                        }
                        t.add(i + 1, QStringList{number, e.track, QString(), QString(), QString(), QString(), QString(), QString(), QString(),
                                              QString(), QStringLiteral("Key frames %1: %2").arg(key, at.join(QStringLiteral(", ")))});
                    }
                }
                // "When you create an optical list, each marker, along with its position in the segment on a
                // keyframe-by-keyframe basis, appears as an optical event" (FX Guide p.412).
                for (const model::PrevisualizationEvent& p : model::previsualizationEvents(fx, span, o.aspect)) {
                    const auto frame = static_cast<std::int64_t>(std::llround(span.frameOf(p.at)));
                    t.add(i + 1, QStringList{number, e.track, QStringLiteral("Previsualization Marker"),
                                          footage((o.opticalFootage ? 0 : o.startAt + e.record) + frame, fpf), QString(), QString(), QString(),
                                          QString(), QString(), QString(),
                                          QStringLiteral("Marker %1 key frame %2: %3 (X %4, Y %5) %6")
                                              .arg(p.marker)
                                              .arg(p.keyframe)
                                              .arg(compass(p.position), QString::number(p.position.x(), 'f', 2),
                                                   QString::number(p.position.y(), 'f', 2), p.color)});
                }
            }
        }
        tables << t;
    }
    if (o.dupe && !dupes.isEmpty()) {
        // "Each dupe group set provides the IN and OUT points of two or more entries that are duplicates of each
        // other" (p.942); Print w/Handles widens the entries shown (p.962).
        Table t{QStringLiteral("dupe"), QStringLiteral("DUPE LIST"), group, {},
                {QStringLiteral("Dupe Group"), QStringLiteral("Track"), QStringLiteral("Footage In"), QStringLiteral("Footage Out"),
                 QStringLiteral("First Edge"), QStringLiteral("Last Edge"), QStringLiteral("Clip Name")},
                {},
                {}};
        QList<model::Dupe> sorted = dupes;
        std::stable_sort(sorted.begin(), sorted.end(), [](const model::Dupe& a, const model::Dupe& b) { return a.set < b.set; });
        for (const model::Dupe& d : sorted) {
            for (int i = 0; i < events.size(); ++i) {
                const Event& e = events[i];
                if (e.track != d.track || d.start >= e.record + e.length || d.start + d.length <= e.record) continue;
                const std::int64_t first = d.start - e.record + e.sourceIn - o.printHandles;
                const std::int64_t length = d.length + 2 * o.printHandles;
                t.add(i + 1, QStringList{QString::number(d.set + 1), d.track, footage(o.startAt + d.start, fpf),
                                      footage(o.startAt + d.start + d.length - 1, fpf), referenceAt(e, first, o),
                                      referenceAt(e, first + length - 1, o), e.name});
            }
        }
        tables << t;
    }
    if (o.pull) {
        // "Pull lists display selected elements of the sequence in various sort orders. The pull lists show dupes but
        // not comments or locators" (p.943): sorted by the sort fields, then by edge number, Heads Out or Tails Out.
        QList<int> order;
        for (int i = 0; i < events.size(); ++i) order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const Event& x = events[a];
            const Event& y = events[b];
            for (SortField f : {o.firstSort, o.secondSort}) {
                const QString kx = sortKey(x, f), ky = sortKey(y, f);
                if (kx != ky) return kx < ky;
            }
            const auto edge = [](const Event& e) {
                return std::pair{e.reference ? e.reference->prefix : QString(), e.reference ? e.reference->frames + e.sourceIn : 0};
            };
            return o.headsOut ? edge(x) < edge(y) : edge(y) < edge(x);
        });
        Table t{QStringLiteral("pull"), QStringLiteral("PULL LIST"), group, {},
                {QStringLiteral("#"), QStringLiteral("Track"), QStringLiteral("First Edge"), QStringLiteral("Last Edge"),
                 QStringLiteral("Duration"), QStringLiteral("Cam Roll"), QStringLiteral("Lab Roll"), QStringLiteral("Sound Roll"),
                 QStringLiteral("Clip Name"), QStringLiteral("Dupe")},
                {},
                {}};
        for (int i : order) {
            const Event& e = events[i];
            t.add(i + 1, QStringList{QString::number(i + 1),
                                  e.track,
                                  referenceAt(e, e.sourceIn, o),
                                  referenceAt(e, e.sourceIn + e.length - 1, o),
                                  footage(e.length, fpf),
                                  e.field("Camroll"),
                                  e.field("Labroll"),
                                  e.field("Soundroll"),
                                  e.name,
                                  e.dupeSet >= 0 ? QString::number(e.dupeSet + 1) : QString()});
        }
        tables << t;
    }
    return true;
}

// The lists written in the chosen format into `r`.
void render(const QList<Table>& tables, const QHash<QString, const QList<Event>*>& events, const Options& o, Result& r) {
    switch (o.format) {
    case Format::Columnar:
    case Format::TabbedLists: r.text = columnar(tables, o); break;
    case Format::Xml:
        r.text = xml(tables, o, [&](const QString& group, int event) -> const model::BinItem* {
            const QList<Event>* list = events.value(group);
            if (!list || event < 1 || event > list->size() || !(*list)[event - 1].clip) return nullptr;
            return &*(*list)[event - 1].clip;
        });
        break;
    case Format::WebLists:
        r.files = webLists(tables, o);
        r.text = QString::fromUtf8(r.files.value(pageOf(tables.first().key, tables.first().group)));
        break;
    }
    r.ok = true;
}

} // namespace

Result exportText(const model::BinItem& sequence, const Options& o) {
    Result r;
    Built b;
    if (!build(b, sequence, o, {}, r)) return r;
    if (b.tables.isEmpty()) {
        r.error = QStringLiteral("Choose a list to make in the List Options.");
        return r;
    }
    render(b.tables, {{QString(), &b.events}}, o, r);
    return r;
}

namespace {

// One line of a change list. `source` is the event whose frames it names, from `first` (a source frame of it) for
// `length` frames; `at` is where in the new sequence it applies, `oldAt` where it was in the old one (-1: nowhere).
// `event` numbers the event in its assemble list, below 0 in the old sequence's.
struct Change {
    QString kind;
    QString track;
    std::int64_t at = 0;
    std::int64_t oldAt = -1;
    std::int64_t length = 0;
    const Event* source = nullptr;
    std::int64_t first = 0;
    int event = 0;
    QString name;
};

std::int64_t overlap(const Event& a, const Event& b) {
    if (a.segment->clipId != b.segment->clipId || a.segment->sourceTrack != b.segment->sourceTrack) return 0;
    return std::min(a.sourceIn + a.length, b.sourceIn + b.length) - std::max(a.sourceIn, b.sourceIn);
}

// The changes from `before` to `after` (p.940: insertions, deletions, trims, moves; and with `all` the unchanged).
QList<Change> changesBetween(const Built& before, const Built& after, bool all) {
    QList<Change> out;
    QStringList tracks;
    for (const Built* b : {&after, &before}) {
        for (const Event& e : b->events) {
            if (!tracks.contains(e.track)) tracks << e.track;
        }
    }
    for (const QString& track : tracks) {
        QList<int> olds, news;
        for (int i = 0; i < before.events.size(); ++i) {
            if (before.events[i].track == track) olds << i;
        }
        for (int i = 0; i < after.events.size(); ++i) {
            if (after.events[i].track == track) news << i;
        }
        // The same shot: the old event sharing the most source frames, each old event matched once.
        QHash<int, int> matchOf; // new index -> position in olds
        QSet<int> taken;
        for (int n : news) {
            int best = -1;
            std::int64_t most = 0;
            for (int k = 0; k < olds.size(); ++k) {
                if (taken.contains(k)) continue;
                const std::int64_t shared = overlap(before.events[olds[k]], after.events[n]);
                if (shared > most) {
                    most = shared;
                    best = k;
                }
            }
            if (best >= 0) {
                matchOf.insert(n, best);
                taken.insert(best);
            }
        }
        // Those kept in their order: the longest increasing run of old positions in the new order; the rest moved.
        QList<int> matched; // new indices in order
        for (int n : news) {
            if (matchOf.contains(n)) matched << n;
        }
        QList<int> length(matched.size(), 1), back(matched.size(), -1);
        int bestEnd = -1;
        for (int a = 0; a < matched.size(); ++a) {
            for (int b = 0; b < a; ++b) {
                if (matchOf[matched[b]] < matchOf[matched[a]] && length[b] + 1 > length[a]) {
                    length[a] = length[b] + 1;
                    back[a] = b;
                }
            }
            if (bestEnd < 0 || length[a] > length[bestEnd]) bestEnd = a;
        }
        QSet<int> kept;
        for (int a = bestEnd; a >= 0; a = back[a]) kept.insert(matched[a]);
        for (int n : news) {
            const Event& now = after.events[n];
            auto add = [&](const QString& kind, std::int64_t at, std::int64_t len, const Event& src, std::int64_t first, int event,
                           std::int64_t oldAt = -1) {
                out << Change{kind, track, at, oldAt, len, &src, first, event, src.name};
            };
            if (!matchOf.contains(n)) {
                add(QStringLiteral("Insert"), now.record, now.length, now, now.sourceIn, n + 1);
                continue;
            }
            const int o = olds[matchOf[n]];
            const Event& was = before.events[o];
            if (!kept.contains(n)) {
                add(QStringLiteral("Move"), now.record, now.length, now, now.sourceIn, n + 1, was.record);
                continue;
            }
            bool changed = false;
            const std::int64_t head = now.sourceIn - was.sourceIn;
            if (head < 0) add(QStringLiteral("Insert Head"), now.record, -head, now, now.sourceIn, n + 1, was.record);
            if (head > 0) add(QStringLiteral("Delete Head"), now.record, head, was, was.sourceIn, -(o + 1), was.record);
            const std::int64_t nowEnd = now.sourceIn + now.length, wasEnd = was.sourceIn + was.length;
            if (nowEnd > wasEnd) add(QStringLiteral("Insert Tail"), now.record + now.length - (nowEnd - wasEnd), nowEnd - wasEnd, now, wasEnd, n + 1,
                                     was.record + was.length);
            if (nowEnd < wasEnd) add(QStringLiteral("Delete Tail"), now.record + now.length, wasEnd - nowEnd, was, nowEnd, -(o + 1),
                                     was.record + was.length - (wasEnd - nowEnd));
            changed = head != 0 || nowEnd != wasEnd;
            if (!changed && all) add(QStringLiteral("No change"), now.record, now.length, now, now.sourceIn, n + 1, was.record);
        }
        // Deletions, where they were: after the new place of the last old shot before them that stayed in its order.
        QHash<int, int> newOf; // position in olds -> new index, for those kept
        for (int n : kept) newOf.insert(matchOf[n], n);
        for (int k = 0; k < olds.size(); ++k) {
            if (taken.contains(k)) continue;
            const Event& was = before.events[olds[k]];
            std::int64_t at = 0;
            for (int j = k - 1; j >= 0; --j) {
                if (newOf.contains(j)) {
                    const Event& anchor = after.events[newOf[j]];
                    at = anchor.record + anchor.length;
                    break;
                }
            }
            out << Change{QStringLiteral("Delete"), track, at, was.record, was.length, &was, was.sourceIn, -(olds[k] + 1), was.name};
        }
    }
    // In the new sequence's order, track by track as the events are; deletions before what follows them.
    std::stable_sort(out.begin(), out.end(), [&](const Change& a, const Change& b) {
        const auto rank = [&](const Change& c) { return static_cast<int>(tracks.indexOf(c.track)); };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (a.at != b.at) return a.at < b.at;
        const bool da = a.kind.startsWith(QLatin1String("Delete")), db = b.kind.startsWith(QLatin1String("Delete"));
        return da && !db;
    });
    return out;
}

} // namespace

Result exportChangeList(const QList<model::BinItem>& oldSequences, const QList<model::BinItem>& newSequences, const Options& o,
                        const ChangeOptions& c) {
    Result r;
    if (oldSequences.isEmpty() || newSequences.isEmpty()) {
        r.error = QStringLiteral("Load the old and the new sequences to make a change list.");
        return r;
    }
    if (oldSequences.size() != newSequences.size()) {
        r.error = QStringLiteral("You must have an equal number of old and new sequences.");
        return r;
    }
    // The reels: by the sequences' Reel # when they all have one, else in their order.
    const auto reelOf = [](const model::BinItem& s) { return s.fields.value(QStringLiteral("Reel #")).trimmed(); };
    const bool byReel = std::all_of(oldSequences.cbegin(), oldSequences.cend(), [&](const model::BinItem& s) { return !reelOf(s).isEmpty(); }) &&
                        std::all_of(newSequences.cbegin(), newSequences.cend(), [&](const model::BinItem& s) { return !reelOf(s).isEmpty(); });
    QList<std::pair<model::BinItem, model::BinItem>> pairs;
    for (int i = 0; i < newSequences.size(); ++i) {
        if (!byReel) {
            pairs.append({oldSequences[i], newSequences[i]});
            continue;
        }
        const auto match = std::find_if(oldSequences.cbegin(), oldSequences.cend(),
                                        [&](const model::BinItem& s) { return reelOf(s) == reelOf(newSequences[i]); });
        if (match == oldSequences.cend()) {
            r.error = QStringLiteral("No old sequence has the Reel # %1 of \"%2\": the old and new versions must have the same reel number.")
                          .arg(reelOf(newSequences[i]), newSequences[i].name);
            return r;
        }
        pairs.append({*match, newSequences[i]});
    }
    const int fpf = o.framesPerFoot;
    const bool web = o.format == Format::WebLists;
    QList<Table> tables;
    QHash<QString, const QList<Event>*> eventsByGroup;
    std::vector<std::unique_ptr<Built>> kept; // the events the tables' rows point into
    for (int p = 0; p < pairs.size(); ++p) {
        const QString group = p == 0 ? QString() : QString::number(p + 1);
        const QString oldGroup = QStringLiteral("Old") + group;
        const QString reel = pairs.size() == 1 ? QString()
                             : byReel        ? QStringLiteral(" (REEL %1)").arg(reelOf(pairs[p].second))
                                             : QStringLiteral(" (REEL %1)").arg(p + 1);
        Options oldOptions = o;
        oldOptions.assemble = true;
        oldOptions.sceneAssemble = oldOptions.optical = oldOptions.dupe = oldOptions.pull = false;
        Options newOptions = oldOptions;
        newOptions.optical = o.optical;
        auto before = std::make_unique<Built>();
        auto after = std::make_unique<Built>();
        if (!build(*before, pairs[p].first, oldOptions, oldGroup, r) || !build(*after, pairs[p].second, newOptions, group, r)) return r;
        QList<Change> changes = changesBetween(*before, *after, !c.showOnlyChanges);
        if (c.combineDeletions) {
            // Each run of deletions next to each other in the old sequence, on one track, as one event.
            QList<Change> combined;
            for (const Change& ch : changes) {
                if (!combined.isEmpty() && ch.kind == QLatin1String("Delete") && combined.last().kind == QLatin1String("Delete") &&
                    combined.last().track == ch.track && combined.last().oldAt + combined.last().length == ch.oldAt) {
                    combined.last().length += ch.length;
                    combined.last().name += QStringLiteral(", ") + ch.name;
                    continue;
                }
                combined << ch;
            }
            changes = combined;
        }
        QStringList head{QStringLiteral("#")};
        if (c.previewCode) head << QStringLiteral("Code");
        head << QStringLiteral("Change") << QStringLiteral("Track") << QStringLiteral("Footage") << QStringLiteral("Old Footage")
             << QStringLiteral("Length") << QStringLiteral("First Edge") << QStringLiteral("Last Edge");
        if (o.clipNames) head << QStringLiteral("Clip Name");
        Table changeTable{QStringLiteral("change"), QStringLiteral("CHANGE LIST") + reel, group, oldGroup, head, {}, {}};
        Table pullTable{QStringLiteral("changepull"),
                        QStringLiteral("CHANGE PULL LIST") + reel,
                        group,
                        oldGroup,
                        {QStringLiteral("#"), QStringLiteral("Change"), QStringLiteral("Track"), QStringLiteral("First Edge"),
                         QStringLiteral("Last Edge"), QStringLiteral("Duration"), QStringLiteral("Cam Roll"), QStringLiteral("Lab Roll"),
                         QStringLiteral("Sound Roll"), QStringLiteral("Clip Name")},
                        {},
                        {}};
        Table discardTable{QStringLiteral("changediscard"),
                           QStringLiteral("CHANGE DISCARD LIST") + reel,
                           group,
                           oldGroup,
                           {QStringLiteral("#"), QStringLiteral("Change"), QStringLiteral("Track"), QStringLiteral("Old Footage"),
                            QStringLiteral("First Edge"), QStringLiteral("Last Edge"), QStringLiteral("Duration"), QStringLiteral("Clip Name")},
                           {},
                           {}};
        QList<int> pulled;
        int number = 0;
        for (int k = 0; k < changes.size(); ++k) {
            const Change& ch = changes[k];
            const Event& src = *ch.source;
            QStringList row{QString::number(++number)};
            if (c.previewCode) row << QString::number(number).rightJustified(3, QLatin1Char('0'));
            row << ch.kind << ch.track << footage(o.startAt + ch.at, fpf)
                << (ch.oldAt >= 0 ? footage(o.startAt + ch.oldAt, fpf) : QString()) << footage(ch.length, fpf)
                << referenceAt(src, ch.first, o) << referenceAt(src, ch.first + ch.length - 1, o);
            if (o.clipNames) row << ch.name;
            changeTable.add(ch.event, row);
            const bool removed = ch.kind.startsWith(QLatin1String("Delete"));
            if (removed) {
                discardTable.add(ch.event, {QString::number(discardTable.rows.size() + 1), ch.kind, ch.track,
                                            footage(o.startAt + std::max<std::int64_t>(ch.oldAt, 0), fpf), referenceAt(src, ch.first, o),
                                            referenceAt(src, ch.first + ch.length - 1, o), footage(ch.length, fpf), ch.name});
            } else if (ch.kind != QLatin1String("No change")) {
                pulled << k;
            }
        }
        // The change pull list in the pull list's order: the sort fields, then the edges, Heads Out or Tails Out.
        std::stable_sort(pulled.begin(), pulled.end(), [&](int a, int b) {
            const Change& x = changes[a];
            const Change& y = changes[b];
            for (SortField f : {o.firstSort, o.secondSort}) {
                const QString kx = sortKey(*x.source, f), ky = sortKey(*y.source, f);
                if (kx != ky) return kx < ky;
            }
            const auto edge = [](const Change& ch) {
                return std::pair{ch.source->reference ? ch.source->reference->prefix : QString(),
                                 ch.source->reference ? ch.source->reference->frames + ch.first : 0};
            };
            return o.headsOut ? edge(x) < edge(y) : edge(y) < edge(x);
        });
        for (int k : pulled) {
            const Change& ch = changes[k];
            const Event& src = *ch.source;
            pullTable.add(ch.event, {QString::number(pullTable.rows.size() + 1), ch.kind, ch.track, referenceAt(src, ch.first, o),
                                     referenceAt(src, ch.first + ch.length - 1, o), footage(ch.length, fpf), src.field("Camroll"),
                                     src.field("Labroll"), src.field("Soundroll"), ch.name});
        }
        tables << changeTable;
        if (c.changePull) tables << pullTable;
        if (c.changeDiscard) tables << discardTable;
        for (Table& t : after->tables) {
            // The new sequence's optical list; for WebLists also both assemble lists, which the changes link to ("Change
            // lists that are hyper linked to the old and new Assemble and Pull Lists", p.958).
            if (t.key == QLatin1String("optical") || (web && t.key == QLatin1String("assemble"))) {
                if (t.key == QLatin1String("assemble")) t.name = QStringLiteral("NEW ASSEMBLE LIST") + reel;
                tables << t;
            }
        }
        if (web) {
            for (Table& t : before->tables) {
                t.name = QStringLiteral("OLD ASSEMBLE LIST") + reel;
                tables << t;
            }
        }
        eventsByGroup.insert(group, &after->events);
        eventsByGroup.insert(oldGroup, &before->events);
        kept.push_back(std::move(before));
        kept.push_back(std::move(after));
    }
    render(tables, eventsByGroup, o, r);
    return r;
}

bool writeFiles(const Result& result, const QString& folder, QString* error) {
    for (auto it = result.files.cbegin(); it != result.files.cend(); ++it) {
        const QString path = QDir(folder).filePath(it.key());
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(it.value()) != it.value().size()) {
            if (error) *error = QStringLiteral("%1 cannot be written.").arg(QDir::toNativeSeparators(path));
            return false;
        }
    }
    return true;
}

} // namespace lumenis::interop::cutlist
