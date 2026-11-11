// Film cut lists (cut_list.h; Editing Guide p.939-944, p.960-966; FX Guide p.79, p.412).
#include "lumenis/interop/cut_list.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/sync_dupes.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>

namespace lumenis::interop::cutlist {

QString formatName(Format format) {
    return format == Format::TabbedLists ? QStringLiteral("TabbedLists") : QStringLiteral("Columnar");
}

std::optional<Format> formatFromName(const QString& name) {
    if (name == QLatin1String("Columnar")) return Format::Columnar;
    if (name == QLatin1String("TabbedLists")) return Format::TabbedLists;
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

// A list as rows of columns, written Columnar or tabbed.
struct Table {
    QString name;
    QStringList head;
    QList<QStringList> rows;
};

QString render(const QList<Table>& tables, const Options& o) {
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
            return s.trimmed().isEmpty() ? s : QString(s).remove(QRegularExpression(QStringLiteral("\\s+$")));
        };
        out << line(t.head);
        QString rule;
        for (int i = 0; i < widths.size(); ++i) rule += (i ? QStringLiteral("  ") : QString()) + QString(widths[i], QLatin1Char('-'));
        out << rule;
        for (const QStringList& r : t.rows) out << line(r);
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
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

Result exportText(const model::BinItem& sequence, const Options& o) {
    Result r;
    if (sequence.kind != model::BinItemKind::Sequence) {
        r.error = QStringLiteral("Load a sequence to make a cut list.");
        return r;
    }
    const model::Sequence seq = model::Sequence::fromItem(sequence);
    const int fpf = o.framesPerFoot;
    // The events, track by track in the sequence's order.
    QList<Event> events;
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
        return r;
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
        if (o.shortCuts && e.length < *o.shortCuts) e.flags << QStringLiteral("Short cut");
        if (o.jumpCuts && i > 0 && events[i - 1].track == e.track && events[i - 1].record + events[i - 1].length == e.record &&
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
    QList<Table> tables;
    if (o.assemble) {
        // "The assemble list shows the order in which a sequence's clips, optical effects, or standard dissolves and
        // fades are assembled from start to finish in the edited sequence. The assemble list also flags duplicate
        // frames" (p.941).
        Table t{QStringLiteral("ASSEMBLE LIST"), head, {}};
        int n = 0;
        for (const Event& e : events) t.rows << cells(e, ++n);
        tables << t;
    }
    if (o.sceneAssemble) {
        // "The Scene/Take column must be filled in within the bin to generate a scene assemble list" (p.942).
        const bool filled = std::all_of(events.cbegin(), events.cend(), [](const Event& e) { return !e.field("Scene").isEmpty(); });
        if (!filled) {
            r.error = QStringLiteral("The Scene and Take columns must be filled in within the bin to generate a scene assemble list.");
            return r;
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
        Table t{QStringLiteral("SCENE ASSEMBLE LIST"), sceneHead, {}};
        for (int i : order) {
            QStringList c = cells(events[i], i + 1);
            c.insert(2, events[i].field("Scene") + QLatin1Char('/') + events[i].field("Take"));
            t.rows << c;
        }
        tables << t;
    }
    if (o.optical && opticals > 0) {
        // "The optical list presents each optical event in the order in which it appears in the sequence and includes
        // specifications based on either ink numbers, Aux ink numbers, or key numbers" (p.942).
        Table t{QStringLiteral("OPTICAL LIST"),
                {QStringLiteral("Optical"), QStringLiteral("Track"), QStringLiteral("Effect"), QStringLiteral("Footage In"),
                 QStringLiteral("Footage Out"), QStringLiteral("Duration"), QStringLiteral("Layer"), QStringLiteral("First Edge"),
                 QStringLiteral("Last Edge"), QStringLiteral("Clip Name"), QStringLiteral("Notes")},
                {}};
        for (int i = 0; i < events.size(); ++i) {
            const Event& e = events[i];
            if (!e.optical) continue;
            const model::Segment& s = *e.segment;
            const QString number = QString::number(e.optical);
            auto row = [&](const QString& effect, std::int64_t from, std::int64_t length, const QString& layer, const Event& src,
                           std::int64_t srcIn, const QString& notes) {
                t.rows << QStringList{number,
                                      e.track,
                                      effect,
                                      footage(o.startAt + from, fpf),
                                      footage(o.startAt + from + length - 1, fpf),
                                      footage(length, fpf),
                                      layer,
                                      referenceAt(src, srcIn, o),
                                      referenceAt(src, srcIn + length - 1, o),
                                      src.name,
                                      notes};
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
                        t.rows << QStringList{number, e.track, QString(), QString(), QString(), QString(), QString(), QString(), QString(),
                                              QString(), QStringLiteral("Key frames %1: %2").arg(key, at.join(QStringLiteral(", ")))};
                    }
                }
                // "When you create an optical list, each marker, along with its position in the segment on a
                // keyframe-by-keyframe basis, appears as an optical event" (FX Guide p.412).
                for (const model::PrevisualizationEvent& p : model::previsualizationEvents(fx, span, o.aspect)) {
                    const auto frame = static_cast<std::int64_t>(std::llround(span.frameOf(p.at)));
                    t.rows << QStringList{number, e.track, QStringLiteral("Previsualization Marker"),
                                          footage((o.opticalFootage ? 0 : o.startAt + e.record) + frame, fpf), QString(), QString(), QString(),
                                          QString(), QString(), QString(),
                                          QStringLiteral("Marker %1 key frame %2: %3 (X %4, Y %5) %6")
                                              .arg(p.marker)
                                              .arg(p.keyframe)
                                              .arg(compass(p.position), QString::number(p.position.x(), 'f', 2),
                                                   QString::number(p.position.y(), 'f', 2), p.color)};
                }
            }
        }
        tables << t;
    }
    if (o.dupe && !dupes.isEmpty()) {
        // "Each dupe group set provides the IN and OUT points of two or more entries that are duplicates of each
        // other" (p.942); Print w/Handles widens the entries shown (p.962).
        Table t{QStringLiteral("DUPE LIST"),
                {QStringLiteral("Dupe Group"), QStringLiteral("Track"), QStringLiteral("Footage In"), QStringLiteral("Footage Out"),
                 QStringLiteral("First Edge"), QStringLiteral("Last Edge"), QStringLiteral("Clip Name")},
                {}};
        QList<model::Dupe> sorted = dupes;
        std::stable_sort(sorted.begin(), sorted.end(), [](const model::Dupe& a, const model::Dupe& b) { return a.set < b.set; });
        for (const model::Dupe& d : sorted) {
            for (const Event& e : events) {
                if (e.track != d.track || d.start >= e.record + e.length || d.start + d.length <= e.record) continue;
                const std::int64_t first = d.start - e.record + e.sourceIn - o.printHandles;
                const std::int64_t length = d.length + 2 * o.printHandles;
                t.rows << QStringList{QString::number(d.set + 1), d.track, footage(o.startAt + d.start, fpf),
                                      footage(o.startAt + d.start + d.length - 1, fpf), referenceAt(e, first, o),
                                      referenceAt(e, first + length - 1, o), e.name};
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
        Table t{QStringLiteral("PULL LIST"),
                {QStringLiteral("#"), QStringLiteral("Track"), QStringLiteral("First Edge"), QStringLiteral("Last Edge"),
                 QStringLiteral("Duration"), QStringLiteral("Cam Roll"), QStringLiteral("Lab Roll"), QStringLiteral("Sound Roll"),
                 QStringLiteral("Clip Name"), QStringLiteral("Dupe")},
                {}};
        for (int i : order) {
            const Event& e = events[i];
            t.rows << QStringList{QString::number(i + 1),
                                  e.track,
                                  referenceAt(e, e.sourceIn, o),
                                  referenceAt(e, e.sourceIn + e.length - 1, o),
                                  footage(e.length, fpf),
                                  e.field("Camroll"),
                                  e.field("Labroll"),
                                  e.field("Soundroll"),
                                  e.name,
                                  e.dupeSet >= 0 ? QString::number(e.dupeSet + 1) : QString()};
        }
        tables << t;
    }
    if (tables.isEmpty()) {
        r.error = QStringLiteral("Choose a list to make in the List Options.");
        return r;
    }
    r.text = render(tables, o);
    r.ok = true;
    return r;
}

} // namespace lumenis::interop::cutlist
