#include "lumenis/interop/edl.h"

#include "import_common.h"
#include "text_common.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

// Edit decision lists, read and written (edl.h; the format and what maps in docs/spec/edl.md).
//
// Reading goes in two passes. The first splits the text into events: an event line (number, reel,
// channels, edit type, a transition's length, source IN and OUT, record IN and OUT), a second line
// with the same number for a transition (its first line names the outgoing source), then the lines
// that belong to it: AUD (audio channels 3 and 4), M2 (a motion effect) and comments ("* FROM CLIP
// NAME:", "* LOC:"). The second pass, once the frame rate is known, edits each event into the
// sequence with Sequence::overwrite on each of its tracks, in the order of the list, as an edit
// controller records them; a later event that covers an earlier one wins.
//
// Writing walks each listed track of the sequence and makes a piece for each segment (its record
// and source timecodes and reel), moves the cut of a transition to where the transition starts (an
// EDL's dissolve starts at its event's record IN), then sorts the pieces by record IN and writes them
// as events, joining pieces of different tracks with the same edit into one event when Optimize EDL
// is on (p.958).

namespace lumenis::interop::edl {

namespace {

using detail::Notes;

// Reels that stand for black (no source), as edit controllers write them.
bool isBlackReel(const QString& reel) {
    const QString r = reel.toUpper();
    return r == QLatin1String("BL") || r == QLatin1String("BLK") || r == QLatin1String("BLACK");
}

// A timecode field: "01:00:00:00" (any of : ; . between fields), or a bare frame count.
bool looksLikeTimecode(const QString& t) {
    static const QRegularExpression tc(QStringLiteral("^\\d{1,2}[:;.]\\d{2}[:;.]\\d{2}[:;.]\\d{2}$"));
    static const QRegularExpression frames(QStringLiteral("^\\d+$"));
    return tc.match(t).hasMatch() || frames.match(t).hasMatch();
}

// An edit type: C (cut), D (dissolve), Wnnn (wipe), K, K B, K O (keys). "KB" and "KO" are taken too.
bool isEditType(const QString& t) {
    static const QRegularExpression type(QStringLiteral("^(C|D|W\\d{1,3}|K|KB|KO)$"));
    return type.match(t.toUpper()).hasMatch();
}

// The channels an event's channel field names, as track names: V (video), A (audio 1), A2, AA (both),
// B or A/V (video and audio 1), AA/V, A2/V; also An and Vn, which some lists write, and NONE (channels
// only through AUD). Nullopt for anything else.
std::optional<QStringList> channelTracks(const QString& code) {
    const QString c = code.toUpper();
    if (c == QLatin1String("V")) return QStringList{QStringLiteral("V1")};
    if (c == QLatin1String("A") || c == QLatin1String("A1")) return QStringList{QStringLiteral("A1")};
    if (c == QLatin1String("AA") || c == QLatin1String("A1A2")) return QStringList{QStringLiteral("A1"), QStringLiteral("A2")};
    if (c == QLatin1String("B") || c == QLatin1String("A/V") || c == QLatin1String("V/A")) {
        return QStringList{QStringLiteral("V1"), QStringLiteral("A1")};
    }
    if (c == QLatin1String("AA/V") || c == QLatin1String("V/AA")) {
        return QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")};
    }
    if (c == QLatin1String("A2/V") || c == QLatin1String("V/A2")) return QStringList{QStringLiteral("V1"), QStringLiteral("A2")};
    if (c == QLatin1String("NONE")) return QStringList{};
    static const QRegularExpression numbered(QStringLiteral("^([AV])(\\d{1,2})$"));
    const auto m = numbered.match(c);
    if (m.hasMatch() && m.captured(2).toInt() >= 1) return QStringList{m.captured(1) + QString::number(m.captured(2).toInt())};
    return std::nullopt;
}

// The channel field for a set of tracks (the inverse of channelTracks() for V1, A1 and A2; audio 3
// and 4 go on an AUD line).
QString channelCode(bool v, bool a1, bool a2) {
    if (v && a1 && a2) return QStringLiteral("AA/V");
    if (v && a1) return QStringLiteral("B");
    if (v && a2) return QStringLiteral("A2/V");
    if (v) return QStringLiteral("V");
    if (a1 && a2) return QStringLiteral("AA");
    if (a1) return QStringLiteral("A");
    if (a2) return QStringLiteral("A2");
    return QStringLiteral("NONE");
}

// One event or transition line, its fields as text.
struct RawLine {
    int line = 0; // in the file, from 1
    QString number;
    QString reel;
    QString channels;
    QString type; // C, D, Wnnn, K, K B, K O
    int duration = 0;
    QString tc[4]; // source IN, source OUT, record IN, record OUT
    bool drop = false; // FCM in force
};

// An event: its line, the outgoing line of a transition, and what follows it.
struct RawEvent {
    std::optional<RawLine> from; // a transition's first line
    RawLine main;
    QList<int> aud;                     // AUD: audio channels 3 and 4
    std::optional<double> speed;        // M2: frames per second of the source
    QStringList comments;               // after "*", trimmed
};

// Parses an event line from its right end, so that a reel name with spaces in it is read whole:
// four timecodes, a transition's length, the edit type (two words for K B and K O), the channels,
// then the reel. Nullopt when it is not one.
std::optional<RawLine> parseEventLine(const QString& text) {
    QStringList t = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (t.size() < 8) return std::nullopt;
    RawLine r;
    const qsizetype n = t.size();
    for (int i = 0; i < 4; ++i) {
        if (!looksLikeTimecode(t.at(n - 4 + i))) return std::nullopt;
        r.tc[i] = t.at(n - 4 + i);
    }
    qsizetype j = n - 5;
    // A transition's length comes after a D, W or K.
    static const QRegularExpression digits(QStringLiteral("^\\d{1,4}$"));
    if (j >= 3 && digits.match(t.at(j)).hasMatch() && (isEditType(t.at(j - 1)) || t.at(j - 1) == QLatin1String("B") ||
                                                       t.at(j - 1) == QLatin1String("O")) &&
        t.at(j - 1).toUpper() != QLatin1String("C")) {
        r.duration = t.at(j).toInt();
        --j;
    }
    if (j < 3) return std::nullopt;
    QString type = t.at(j).toUpper();
    if ((type == QLatin1String("B") || type == QLatin1String("O")) && j >= 4 && t.at(j - 1).toUpper() == QLatin1String("K")) {
        type = QStringLiteral("K ") + type;
        --j;
    } else if (type == QLatin1String("KB") || type == QLatin1String("KO")) {
        type = QStringLiteral("K ") + type.at(1);
    } else if (!isEditType(type)) {
        return std::nullopt;
    }
    if (type.startsWith(QLatin1Char('W'))) type = QStringLiteral("W%1").arg(type.mid(1).toInt(), 3, 10, QLatin1Char('0'));
    r.type = type;
    r.channels = t.at(j - 1);
    if (j - 2 < 1) return std::nullopt;
    r.reel = QStringList(t.mid(1, j - 2)).join(QLatin1Char(' '));
    r.number = t.at(0);
    static const QRegularExpression number(QStringLiteral("^\\d+$"));
    if (!number.match(r.number).hasMatch() || r.reel.isEmpty()) return std::nullopt;
    return r;
}

// The frame rate when the import is not given one: 29.97 for drop frame, else by the highest frame
// number in the list (24, 25, 29.97 or 59.94 frames a second). NTSC rates are taken over whole ones
// as their timecodes are the same.
Rational guessRate(const QList<RawEvent>& events, bool anyDrop) {
    if (anyDrop) return {30000, 1001};
    int highest = 0;
    auto look = [&](const RawLine& l) {
        for (const QString& tc : l.tc) {
            if (tc.size() >= 3 && !tc.at(tc.size() - 3).isDigit()) highest = std::max(highest, tc.right(2).toInt());
        }
    };
    for (const RawEvent& e : events) {
        look(e.main);
        if (e.from) look(*e.from);
    }
    if (highest >= 30) return {60000, 1001};
    if (highest >= 25) return {30000, 1001};
    if (highest >= 24) return {25, 1};
    return {24000, 1001};
}

// The frames of a timecode field at the rate (drop frame when `drop` and the rate has it); a bare
// number is a frame count. Nullopt when it is not a timecode at that rate.
std::optional<std::int64_t> frameOf(const QString& text, const Rational& rate, bool drop) {
    if (!text.contains(QLatin1Char(':')) && !text.contains(QLatin1Char(';')) && !text.contains(QLatin1Char('.'))) {
        bool ok = false;
        const qlonglong f = text.toLongLong(&ok);
        return ok ? std::optional<std::int64_t>(f) : std::nullopt;
    }
    TimecodeFormat format = TimecodeFormat::forRate(rate, drop);
    if (!format.isValid()) format = TimecodeFormat::forRate(rate, false);
    const auto tc = Timecode::parse(text.toStdString(), format);
    if (!tc) return std::nullopt;
    return tc->frameCount();
}

// The comment's key ("FROM CLIP NAME") and the text after it, for comments written "KEY: text".
std::pair<QString, QString> splitComment(const QString& comment) {
    const qsizetype colon = comment.indexOf(QLatin1Char(':'));
    if (colon < 0) return {comment.trimmed().toUpper(), {}};
    return {comment.left(colon).trimmed().toUpper(), comment.mid(colon + 1).trimmed()};
}

// The marker colour named in a LOC comment (any case), or empty.
QString markerColorNamed(const QString& word) {
    for (const model::MarkerColor& c : model::markerColors()) {
        if (c.name.compare(word, Qt::CaseInsensitive) == 0) return c.name;
    }
    return {};
}

// The Speed of a motion effect the import made (category "EDL", name "Motion Effect").
std::optional<double> speedOf(const model::Effect& e) {
    if (e.category != QLatin1String("EDL") || e.name != QLatin1String("Motion Effect")) return std::nullopt;
    const QJsonValue v = e.parameters.value(QStringLiteral("M2")).toObject().value(QStringLiteral("Speed"));
    return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
}

model::Effect motionEffect(double speed) {
    model::Effect e;
    e.category = QStringLiteral("EDL");
    e.name = QStringLiteral("Motion Effect");
    e.parameters = QJsonObject{{QStringLiteral("M2"), QJsonObject{{QStringLiteral("Speed"), speed}}}};
    return e;
}

// ---- Import

class Importer {
public:
    Importer(const QString& text, const ImportOptions& options) : options_(options) {
        if (!options_.when.isValid()) options_.when = QDateTime::currentDateTimeUtc();
        split(text);
    }

    ImportResult run() {
        bool anyDrop = drop_;
        for (const RawEvent& e : events_) anyDrop = anyDrop || e.main.drop;
        if (options_.rate && options_.rate->num() > 0) {
            rate_ = *options_.rate;
        } else {
            rate_ = guessRate(events_, anyDrop);
            notes_.add(QStringLiteral("the frame rate is not in an EDL: read as %1 fps")
                           .arg(QString::number(rate_.toDouble(), 'f', rate_.den() == 1 ? 0 : 3)));
        }
        drop_ = anyDrop && TimecodeFormat::forRate(rate_, true).dropFrame;
        if (anyDrop && !drop_) notes_.add(QStringLiteral("drop frame timecode at a rate without it: read as non-drop"));

        // Every event's record IN, for the sequence's start: the first of them.
        std::optional<std::int64_t> first;
        for (const RawEvent& e : events_) {
            for (const RawLine* l : {e.from ? &*e.from : nullptr, &e.main}) {
                if (!l) continue;
                if (const auto in = frameOf(l->tc[2], rate_, l->drop && drop_)) first = first ? std::min(*first, *in) : *in;
            }
        }
        const TimecodeFormat format = TimecodeFormat::forRate(rate_, drop_);
        start_ = first.value_or(model::kDefaultStartHours * 3600 * format.nominalFps);
        for (const RawEvent& e : events_) place(e);
        return finish();
    }

private:
    // The first pass: lines into events.
    void split(const QString& text) {
        const QStringList lines = detail::textLines(text);
        RawEvent* current = nullptr;
        for (int i = 0; i < lines.size(); ++i) {
            const QString s = lines.at(i).trimmed();
            const int lineNo = i + 1;
            if (s.isEmpty()) continue;
            const QString upper = s.toUpper();
            if (upper.startsWith(QLatin1String("TITLE:"))) {
                title_ = s.mid(6).trimmed();
                continue;
            }
            if (upper.startsWith(QLatin1String("FCM:"))) {
                drop_ = upper.contains(QLatin1String("DROP")) && !upper.contains(QLatin1String("NON"));
                continue;
            }
            if (s.startsWith(QLatin1Char('*'))) {
                if (current) current->comments << s.mid(1).trimmed();
                continue;
            }
            // Comments some lists write without the "*", and the line Media Composer writes after a
            // transition ("EFFECTS NAME IS CROSS DISSOLVE"), which says what the edit line says.
            if (upper.startsWith(QLatin1String("FROM CLIP NAME:")) || upper.startsWith(QLatin1String("TO CLIP NAME:"))) {
                if (current) current->comments << s;
                continue;
            }
            if (upper.startsWith(QLatin1String("EFFECTS NAME IS"))) continue;
            const QStringList tokens = s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            const QString head = tokens.first().toUpper();
            if (head == QLatin1String("M2")) {
                // M2 <reel> <speed> <entry timecode>: the speed is frames per second of the source.
                bool ok = false;
                const double speed = tokens.size() >= 4 ? tokens.at(tokens.size() - 2).toDouble(&ok) : 0.0;
                if (!current || !ok) {
                    notes_.add(QStringLiteral("line %1: M2 line not read").arg(lineNo));
                } else {
                    current->speed = speed;
                }
                continue;
            }
            if (head == QLatin1String("AUD")) {
                // AUD 3, AUD 4, AUD 3 4: audio channels 3 and 4 of the event before.
                if (!current) continue;
                for (const QString& t : tokens.mid(1)) {
                    for (const QChar c : t) {
                        if (c.isDigit() && c.digitValue() >= 1) current->aud << c.digitValue();
                    }
                }
                continue;
            }
            if (head.startsWith(QLatin1String("SPLIT"))) {
                notes_.add(QStringLiteral("split edits (SPLIT:) read as plain edits"));
                continue;
            }
            if (!head.isEmpty() && head.at(0).isDigit()) {
                auto line = parseEventLine(s);
                if (!line) {
                    noteLine(lineNo, QStringLiteral("not an event"));
                    current = nullptr;
                    continue;
                }
                line->line = lineNo;
                line->drop = drop_;
                // A transition's second line: the same number as the line before, which named the
                // outgoing source (comments between them belong to the event).
                if (current && !current->from && current->main.number == line->number && line->type != QLatin1String("C") &&
                    !current->speed) {
                    current->from = current->main;
                    current->main = *line;
                    continue;
                }
                events_.append(RawEvent{});
                current = &events_.last();
                current->main = *line;
                continue;
            }
            noteLine(lineNo, QStringLiteral("not read"));
        }
    }

    // Lines that cannot be read are named one by one, up to a limit.
    void noteLine(int line, const QString& what) {
        if (++badLines_ <= 20) {
            notes_.add(QStringLiteral("line %1: %2").arg(line).arg(what));
        } else {
            notes_.add(QStringLiteral("more lines not read"));
        }
    }

    // A line's four timecodes as frames; false (with a warning) when one cannot be read.
    bool framesOf(const RawLine& l, std::int64_t out[4]) {
        for (int i = 0; i < 4; ++i) {
            const auto f = frameOf(l.tc[i], rate_, l.drop && drop_);
            if (!f) {
                noteLine(l.line, QStringLiteral("timecode %1 not read").arg(l.tc[i]));
                return false;
            }
            out[i] = *f;
        }
        return true;
    }

    // The source item of a reel, made the first time the reel is seen.
    model::BinItem& sourceFor(const QString& reel) {
        auto it = sources_.find(reel);
        if (it == sources_.end()) {
            model::BinItem s;
            s.id = model::newItemId();
            s.mobId = tapeMobId(reel);
            s.kind = model::BinItemKind::Source;
            s.name = reel;
            s.created = options_.when;
            s.modified = options_.when;
            reels_ << reel;
            it = sources_.emplace(reel, s).first;
        }
        return it->second;
    }

    // Edits one line's material onto `tracks` for [recIn, recOut), with an optional transition at its
    // start. `name` is the clip's name from the comments.
    void edit(const RawLine& l, const QStringList& tracks, const QString& name, const std::optional<model::Transition>& transition,
              const std::optional<double>& speed, bool key, bool outgoing = false) {
        std::int64_t f[4];
        if (!framesOf(l, f)) return;
        const std::int64_t length = f[3] - f[2];
        if (length <= 0) {
            // A transition's outgoing line has no length: it only names the source.
            if (!outgoing) notes_.add(QStringLiteral("events without length"));
            return;
        }
        const std::int64_t at = f[2] - start_;
        const bool black = isBlackReel(l.reel);
        for (QString track : tracks) {
            // A key's foreground goes on the track above the background (V2).
            if (key && track == QLatin1String("V1")) track = QStringLiteral("V2");
            model::Segment s = model::Segment::filler(length);
            if (!black) {
                model::BinItem& source = sourceFor(l.reel);
                s.clipId = source.id;
                s.binId = options_.binId;
                s.sourceMob = source.mobId;
                s.name = name.isEmpty() ? l.reel : name;
                s.sourceTrack = track == QLatin1String("V2") && key ? QStringLiteral("V1") : track;
                s.sourceIn = f[0];
                usedTracks_[l.reel].insert(s.sourceTrack);
            }
            if (speed) s.effect = motionEffect(*speed);
            if (key) {
                model::Effect e;
                e.category = QStringLiteral("EDL");
                e.name = QStringLiteral("Key");
                s.effect = e;
                notes_.add(QStringLiteral("keys read as a second video track with an EDL Key effect"));
            }
            model::Track* t = sequence_.ensureTrack(track);
            if (!t) {
                notes_.add(QStringLiteral("tracks past the limit left out"));
                continue;
            }
            const QList<model::Segment> under = sequence_.material(track, at, length);
            if (std::any_of(under.begin(), under.end(), [](const model::Segment& u) { return !u.isFiller(); })) {
                notes_.add(QStringLiteral("events over earlier events: the later one kept"));
            }
            sequence_.overwrite(track, at, {s});
            if (transition) {
                model::Track* tr = sequence_.track(track);
                std::int64_t segStart = 0;
                const int i = tr->segmentAt(at, &segStart);
                if (i >= 0 && segStart == at) {
                    tr->segments[i].transition = transition;
                    if (transition->duration > length) notes_.add(QStringLiteral("transitions longer than their events"));
                }
            }
        }
    }

    // The second pass: one event into the sequence.
    void place(const RawEvent& e) {
        QString fromName;
        QString toName;
        for (const QString& c : e.comments) {
            const auto [key, value] = splitComment(c);
            if (key == QLatin1String("FROM CLIP NAME")) {
                fromName = value;
            } else if (key == QLatin1String("TO CLIP NAME")) {
                toName = value;
            } else if (key == QLatin1String("LOC")) {
                locator(value, e.main);
            } else if (c.contains(QLatin1String("FREEZE FRAME")) || key == QLatin1String("EFFECT NAME") ||
                       key == QLatin1String("BLEND, DISSOLVE") || c.toUpper() == QLatin1String("BLEND, DISSOLVE")) {
                // Says what the edit line says already.
            } else if (c.contains(QLatin1Char(':'))) {
                notes_.add(QStringLiteral("comments not kept (%1)").arg(key.left(32)));
            } else {
                notes_.add(QStringLiteral("comments not kept"));
            }
        }
        auto tracksOf = [&](const RawLine& l) -> std::optional<QStringList> {
            auto tracks = channelTracks(l.channels);
            if (!tracks) {
                noteLine(l.line, QStringLiteral("channels %1 not read").arg(l.channels));
                return std::nullopt;
            }
            for (int a : e.aud) {
                const QString name = QStringLiteral("A%1").arg(a);
                if (!tracks->contains(name)) *tracks << name;
            }
            return tracks;
        };
        const auto tracks = tracksOf(e.main);
        if (!tracks) return;
        const QString& type = e.main.type;
        const bool transition = type == QLatin1String("D") || type.startsWith(QLatin1Char('W'));
        const bool key = type == QLatin1String("K") || type == QLatin1String("K O");
        if (e.from) {
            // A transition's (or key's) first line: the outgoing source, or the key's background. It
            // is edited in as an event when it has a length; its name is FROM CLIP NAME.
            if (const auto fromTracks = tracksOf(*e.from)) {
                edit(*e.from, *fromTracks, fromName, std::nullopt, std::nullopt, false, true);
            }
        }
        std::optional<model::Transition> t;
        if (transition) {
            model::Transition tr;
            if (type == QLatin1String("D")) {
                tr.effect.category = QStringLiteral("Blend");
                tr.effect.name = QStringLiteral("Dissolve");
            } else {
                tr.effect.category = QStringLiteral("EDL");
                tr.effect.name = type;
                notes_.add(QStringLiteral("wipes kept by their EDL numbers"));
            }
            // An EDL's transition starts at its event's record IN, where the incoming source starts.
            tr.duration = e.main.duration;
            tr.alignment = model::TransitionAlignment::StartingAtCut;
            if (tr.duration > 0) t = tr;
            if (!e.from) notes_.add(QStringLiteral("transitions without an outgoing line"));
        } else if (type == QLatin1String("K B") && e.from) {
            notes_.add(QStringLiteral("key backgrounds after another line"));
        }
        // The incoming source of a two-line event is TO CLIP NAME; a one-line event's is FROM CLIP NAME.
        edit(e.main, *tracks, e.from ? toName : fromName, t, e.speed, key);
    }

    // "* LOC: 01:00:01:14 RED     ANIM FIX NEEDED": a marker at that record timecode (as Media
    // Composer writes them: it falls inside its event's record times), on the event's first track.
    void locator(const QString& text, const RawLine& event) {
        const QStringList words = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const auto frame = words.isEmpty() ? std::nullopt : frameOf(words.first(), rate_, event.drop && drop_);
        if (!frame) {
            notes_.add(QStringLiteral("locators (LOC) not read"));
            return;
        }
        model::Marker m;
        m.id = model::newItemId();
        m.frame = *frame - start_;
        m.color = words.size() > 1 ? markerColorNamed(words.at(1)) : QString();
        const qsizetype skip = m.color.isEmpty() ? 1 : 2;
        // The comment: everything after the timecode and colour, spacing kept as it is.
        QString rest = text.trimmed();
        for (qsizetype i = 0; i < skip && !rest.isEmpty(); ++i) {
            const qsizetype space = rest.indexOf(QRegularExpression(QStringLiteral("\\s")));
            rest = space < 0 ? QString() : rest.mid(space).trimmed();
        }
        m.comment = rest;
        if (m.color.isEmpty()) m.color = model::markerColors().first().name;
        const auto tracks = channelTracks(event.channels);
        m.track = tracks && !tracks->isEmpty() ? tracks->first() : QStringLiteral("V1");
        if (m.frame < 0) {
            notes_.add(QStringLiteral("locators before the sequence's start left out"));
            return;
        }
        markers_ << m;
    }

    ImportResult finish() {
        ImportResult result;
        for (const QString& reel : reels_) {
            model::BinItem s = sources_.at(reel);
            s.editRate = rate_;
            // A tape: all of a day's timecode.
            s.durationFrames = TimecodeFormat::forRate(rate_, drop_).framesPerDay();
            s.fields.insert(QStringLiteral("Start"), detail::timecodeText(0, rate_, drop_));
            s.fields.insert(QStringLiteral("Tape"), reel);
            QList<int> video;
            QList<int> audio;
            for (const QString& t : usedTracks_[reel]) {
                if (const auto ref = model::parseTrackName(t)) (ref->kind == model::TrackKind::Video ? video : audio) << ref->number;
            }
            s.fields.insert(QStringLiteral("Tracks"), detail::trackList(video, audio));
            result.items << s;
        }
        model::BinItem item;
        item.id = model::newItemId();
        item.kind = model::BinItemKind::Sequence;
        item.name = !title_.isEmpty() ? title_ : !options_.name.isEmpty() ? options_.name : QStringLiteral("Untitled Sequence");
        item.created = options_.when;
        item.modified = options_.when;
        item.editRate = rate_;
        if (sequence_.tracks().isEmpty()) sequence_ = model::Sequence::withDefaultTracks();
        sequence_.setTimecodeTrack(true);
        sequence_.storeIn(item);
        QList<int> video;
        QList<int> audio;
        for (const model::Track& t : sequence_.tracks()) (t.kind == model::TrackKind::Video ? video : audio) << t.number;
        item.fields.insert(QStringLiteral("Tracks"), (detail::trackList(video, audio) + QStringLiteral(" TC1")).trimmed());
        item.fields.insert(QStringLiteral("Start"), detail::timecodeText(start_, rate_, drop_));
        item.fields.insert(QStringLiteral("End"), detail::timecodeText(start_ + item.durationFrames, rate_, drop_));
        if (!markers_.isEmpty()) model::setMarkers(item, markers_);
        result.items << item;
        result.warnings = notes_.list();
        return result;
    }

    ImportOptions options_;
    QString title_;
    bool drop_ = false;
    QList<RawEvent> events_;
    Rational rate_{24000, 1001};
    std::int64_t start_ = 0;
    model::Sequence sequence_;
    std::map<QString, model::BinItem> sources_;
    QStringList reels_; // in the order first seen
    std::map<QString, std::set<QString>> usedTracks_;
    QList<model::Marker> markers_;
    Notes notes_;
    int badLines_ = 0;
};

// ---- Export

// One edit of one track, before it is written.
struct Piece {
    int order = 0; // the track's place among those listed: V1 first
    QString track;
    std::int64_t recIn = 0;
    std::int64_t recOut = 0;
    QString reel;
    std::int64_t srcIn = 0;
    std::int64_t srcOut = 0;
    QString name;
    std::optional<double> speed;
    // A transition at the start: its type (D, Wnnn), length and the outgoing source.
    QString transition;
    std::int64_t transitionLength = 0;
    QString fromReel;
    std::int64_t fromSource = 0;
    QString fromName;
    QList<model::Marker> markers;

    // Whether two pieces are the same edit and can share an event (Optimize EDL).
    bool sameEdit(const Piece& o) const {
        return recIn == o.recIn && recOut == o.recOut && reel == o.reel && srcIn == o.srcIn && srcOut == o.srcOut &&
               name == o.name && speed == o.speed && transition == o.transition && transitionLength == o.transitionLength &&
               fromReel == o.fromReel && fromSource == o.fromSource && fromName == o.fromName;
    }
};

class Exporter {
public:
    Exporter(const model::BinItem& item, const ExportOptions& options) : item_(item), options_(options) {}

    ExportResult run() {
        ExportResult result;
        if (item_.kind != model::BinItemKind::Sequence) {
            result.error = QStringLiteral("%1 is not a sequence").arg(item_.name);
            return result;
        }
        const model::Sequence sequence = model::Sequence::fromItem(item_);
        rate_ = item_.editRate;
        drop_ = model::isDropFrame(item_);
        nominal_ = TimecodeFormat::forRate(rate_, false).nominalFps;
        start_ = detail::timecodeFrames(item_.fields.value(QStringLiteral("Start")), rate_)
                     .value_or(model::kDefaultStartHours * 3600 * nominal_);

        // The tracks listed: one video track, audio 1 to 4.
        QStringList listed = options_.tracks.isEmpty() ? sequence.trackNames() : options_.tracks;
        QString video;
        QStringList audio;
        for (const QString& name : sequence.trackNames()) {
            if (!listed.contains(name)) continue;
            const auto ref = model::parseTrackName(name);
            if (!ref) continue;
            if (ref->kind == model::TrackKind::Data) {
                notes_.add(QStringLiteral("data tracks left out"));
            } else if (ref->kind == model::TrackKind::Video) {
                if (video.isEmpty()) {
                    video = name;
                } else {
                    notes_.add(QStringLiteral("%1 left out: an EDL lists one video track").arg(name));
                }
            } else if (ref->number > 4) {
                notes_.add(QStringLiteral("%1 left out: an EDL lists audio tracks 1 to 4").arg(name));
            } else {
                audio << name;
            }
        }
        QList<Piece> pieces;
        int order = 0;
        for (const QString& name : (video.isEmpty() ? QStringList() : QStringList{video}) + audio) {
            pieces += piecesOf(*sequence.track(name), order++);
        }
        attachMarkers(pieces);
        std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
            return a.recIn != b.recIn ? a.recIn < b.recIn : a.order < b.order;
        });
        // Events: a piece each, or with Optimize EDL the pieces of one edit on different tracks.
        QList<QList<Piece>> events;
        for (const Piece& p : pieces) {
            bool joined = false;
            // Any event with the same record IN may be the same edit on another track.
            for (qsizetype k = events.size() - 1; options_.optimize && k >= 0 && events.at(k).first().recIn == p.recIn; --k) {
                QList<Piece>& e = events[k];
                const bool trackFree = std::none_of(e.begin(), e.end(), [&](const Piece& q) { return q.track == p.track; });
                if (trackFree && e.first().sameEdit(p)) {
                    e << p;
                    joined = true;
                    break;
                }
            }
            if (!joined) events.append({p});
        }
        reelWidth_ = 8;
        if (options_.format == Format::File129) {
            for (const Piece& p : pieces) reelWidth_ = std::max(reelWidth_, static_cast<int>(std::max(p.reel.size(), p.fromReel.size())));
        }
        QStringList lines;
        const QString title = options_.title.isEmpty() ? item_.name : options_.title;
        lines << QStringLiteral("TITLE:   %1").arg(text(title));
        lines << (drop_ ? QStringLiteral("FCM: DROP FRAME") : QStringLiteral("FCM: NON-DROP FRAME"));
        int number = std::max(options_.startEvent, 0);
        for (const QList<Piece>& e : events) {
            if (number > 999 && options_.format == Format::Cmx3600) notes_.add(QStringLiteral("event numbers past 999"));
            write(lines, e, number++);
        }
        result.ok = true;
        result.events = static_cast<int>(events.size());
        result.text = lines.join(QLatin1String("\r\n")) + QStringLiteral("\r\n");
        result.warnings = notes_.list();
        return result;
    }

private:
    // Names are capitalized unless the option keeps their case (p.950).
    QString text(const QString& s) const { return options_.upperCase ? s.toUpper() : s; }

    // A clip's reel: its Tape column, its tape source, a source item's name, its import source's file
    // name, else its own name; spaces become underscores and a name too long for the format loses its
    // end (Tape or File Name truncation: End, p.960).
    QString reelOf(const model::BinItem& clip) {
        QString reel = clip.fields.value(QStringLiteral("Tape"));
        for (const model::ClipSource& s : clip.sources) {
            if (reel.isEmpty() && s.kind == model::ClipSource::Kind::Tape) reel = s.name;
        }
        for (const model::ClipSource& s : clip.sources) {
            if (reel.isEmpty() && s.kind == model::ClipSource::Kind::Import) {
                reel = QFileInfo(s.name.isEmpty() ? s.path : s.name).completeBaseName();
            }
        }
        if (reel.isEmpty()) reel = clip.name;
        return reelText(reel);
    }

    QString reelText(QString reel) {
        reel = text(reel.trimmed());
        reel.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
        if (reel.isEmpty()) reel = QStringLiteral("AX");
        const int limit = reelLength(options_.format);
        if (reel.size() > limit) {
            notes_.add(QStringLiteral("reel names longer than %1 characters cut short").arg(limit));
            reel.truncate(limit);
        }
        return reel;
    }

    // The reel and the timecode of the frame a segment starts at, from its clip.
    struct Source {
        QString reel;
        std::int64_t in = 0;
    };
    Source sourceOf(const model::Segment& s) {
        std::optional<model::BinItem> clip;
        if (options_.resolve) clip = options_.resolve(s.sourceMobId(), s.clipId);
        if (!clip) {
            notes_.add(QStringLiteral("clips not found: the segment's name is its reel, its IN point from 00:00:00:00"));
            return {reelText(s.name), s.sourceIn};
        }
        std::int64_t start = detail::timecodeFrames(clip->fields.value(QStringLiteral("Start")), clip->editRate).value_or(0);
        const int clipFps = TimecodeFormat::forRate(clip->editRate, false).nominalFps;
        if (clipFps != nominal_) {
            notes_.add(QStringLiteral("clips at other frame rates: their timecode counted at the sequence's"));
            start = detail::startAtRate(start, clipFps, rate_);
        }
        return {reelOf(*clip), start + s.sourceIn};
    }

    // A track's segments as pieces, with each transition's cut moved to where it starts.
    QList<Piece> piecesOf(const model::Track& track, int order) {
        QList<Piece> out;
        std::int64_t pos = 0;
        // The piece made for the segment before, if any.
        std::optional<qsizetype> before;
        for (qsizetype i = 0; i < track.segments.size(); ++i) {
            const model::Segment& s = track.segments.at(i);
            const std::int64_t at = pos;
            pos += s.length;
            const bool dissolves = track.kind == model::TrackKind::Video ? options_.pictureDissolves : options_.soundDissolves;
            const bool transition = s.transition && dissolves && s.transition->duration > 0;
            const bool last = i + 1 == track.segments.size();
            if (s.isFiller() && !(options_.blackEdits && !last) && !transition) {
                before.reset();
                continue;
            }
            Piece p;
            p.order = order;
            p.track = track.name();
            p.recIn = start_ + at;
            p.recOut = start_ + pos;
            if (s.isFiller()) {
                p.reel = QStringLiteral("BL");
                p.srcIn = 0;
                p.srcOut = s.length;
            } else {
                const Source src = sourceOf(s);
                p.reel = src.reel;
                p.srcIn = src.in;
                p.name = s.name;
                p.speed = s.effect ? speedOf(*s.effect) : std::nullopt;
                // A motion effect uses the source frames its speed covers; a freeze frame (speed 0)
                // one frame.
                p.srcOut = p.srcIn + (p.speed ? std::max<std::int64_t>(1, std::llround(static_cast<double>(s.length) * std::abs(*p.speed) /
                                                                                         static_cast<double>(nominal_)))
                                               : s.length);
                if (s.effect && !p.speed) {
                    notes_.add(QStringLiteral("segment effects left out (%1/%2)").arg(s.effect->category, s.effect->name));
                }
            }
            if (transition) {
                const model::Transition& t = *s.transition;
                if (t.effect.category == QLatin1String("Blend") && t.effect.name == QLatin1String("Dissolve")) {
                    p.transition = QStringLiteral("D");
                } else if (t.effect.category == QLatin1String("EDL") && t.effect.name.startsWith(QLatin1Char('W'))) {
                    p.transition = t.effect.name;
                } else {
                    p.transition = QStringLiteral("D");
                    notes_.add(QStringLiteral("transitions written as dissolves (%1/%2)").arg(t.effect.category, t.effect.name));
                }
                p.transitionLength = t.duration;
                // An EDL's transition starts at its event's record IN: the cut moves back by the
                // frames of the transition before it, and the segment before ends there.
                std::int64_t b = t.before();
                const std::int64_t room = before ? out.at(*before).recOut - out.at(*before).recIn : (i > 0 ? track.segments.at(i - 1).length : 0);
                if (b > room) {
                    notes_.add(QStringLiteral("transitions longer than the segment before them"));
                    b = room;
                }
                p.recIn -= b;
                p.srcIn -= b;
                if (before) {
                    Piece& prev = out[*before];
                    prev.recOut -= b;
                    prev.srcOut -= b;
                    p.fromReel = prev.reel;
                    p.fromSource = prev.srcOut;
                    p.fromName = prev.name;
                    if (prev.recOut <= prev.recIn) out.removeAt(*before);
                } else {
                    p.fromReel = QStringLiteral("BL");
                }
            }
            out << p;
            before = out.size() - 1;
        }
        return out;
    }

    // Markers (List Options' Markers, p.958) go with the event whose record times hold them, on their
    // track when it is listed.
    void attachMarkers(QList<Piece>& pieces) {
        if (!options_.markers) return;
        for (const model::Marker& m : model::markersOf(item_)) {
            const std::int64_t at = start_ + m.frame;
            Piece* found = nullptr;
            for (Piece& p : pieces) {
                if (at < p.recIn || at >= p.recOut) continue;
                if (!found || (p.track == m.track && found->track != m.track)) found = &p;
            }
            if (found) {
                found->markers << m;
            } else {
                notes_.add(QStringLiteral("markers outside the events left out"));
            }
        }
    }

    QString tc(std::int64_t frames) const { return detail::timecodeText(frames, rate_, drop_); }

    // One event: its line (two for a transition), AUD for audio 3 and 4, M2, then the comments.
    void write(QStringList& lines, const QList<Piece>& event, int number) {
        const Piece& p = event.first();
        bool v = false;
        bool a1 = false;
        bool a2 = false;
        QStringList aud;
        for (const Piece& q : event) {
            if (q.track.startsWith(QLatin1Char('V'))) v = true;
            if (q.track == QLatin1String("A1")) a1 = true;
            if (q.track == QLatin1String("A2")) a2 = true;
            if (q.track == QLatin1String("A3") || q.track == QLatin1String("A4")) aud << q.track.mid(1);
        }
        std::sort(aud.begin(), aud.end());
        const QString channels = channelCode(v, a1, a2);
        const QString num = QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
        auto line = [&](const QString& reel, const QString& type, std::int64_t length, std::int64_t sIn, std::int64_t sOut, std::int64_t rIn,
                        std::int64_t rOut) {
            const QString edit = type.leftJustified(4) + QLatin1Char(' ') +
                                 (type == QLatin1String("C") ? QStringLiteral("   ") : QStringLiteral("%1").arg(length, 3, 10, QLatin1Char('0')));
            lines << QStringLiteral("%1  %2 %3 %4 %5 %6 %7 %8")
                         .arg(num, reel.leftJustified(reelWidth_), channels.leftJustified(5), edit, tc(sIn), tc(sOut), tc(rIn), tc(rOut));
        };
        if (!p.transition.isEmpty()) {
            line(p.fromReel, QStringLiteral("C"), 0, p.fromSource, p.fromSource, p.recIn, p.recIn);
            line(p.reel, p.transition, p.transitionLength, p.srcIn, p.srcOut, p.recIn, p.recOut);
        } else {
            line(p.reel, QStringLiteral("C"), 0, p.srcIn, p.srcOut, p.recIn, p.recOut);
        }
        if (!aud.isEmpty()) lines << QStringLiteral("AUD  ") + aud.join(QStringLiteral("    "));
        if (p.speed) {
            lines << QStringLiteral("M2   %1       %2                %3")
                         .arg(p.reel.leftJustified(reelWidth_))
                         .arg(QStringLiteral("%1%2").arg(*p.speed < 0 ? QStringLiteral("-") : QString(),
                                                         QString::number(std::abs(*p.speed), 'f', 1).rightJustified(5, QLatin1Char('0'))))
                         .arg(tc(p.srcIn));
        }
        if (options_.clipNames) {
            // Media Composer writes two spaces before the name (as OpenTimelineIO notes).
            if (!p.transition.isEmpty()) {
                if (!p.fromName.isEmpty()) lines << QStringLiteral("* FROM CLIP NAME:  %1").arg(text(p.fromName));
                if (!p.name.isEmpty()) lines << QStringLiteral("* TO CLIP NAME:  %1").arg(text(p.name));
            } else if (!p.name.isEmpty()) {
                lines << QStringLiteral("* FROM CLIP NAME:  %1").arg(text(p.name));
            }
        }
        for (const Piece& q : event) {
            for (const model::Marker& m : q.markers) {
                lines << QStringLiteral("* LOC: %1 %2 %3").arg(tc(start_ + m.frame), m.color.toUpper().leftJustified(7), text(m.comment)).trimmed();
            }
        }
    }

    const model::BinItem& item_;
    ExportOptions options_;
    Rational rate_{24000, 1001};
    bool drop_ = false;
    int nominal_ = 24;
    std::int64_t start_ = 0;
    int reelWidth_ = 8;
    Notes notes_;
};

} // namespace

QString formatName(Format format) {
    return format == Format::File129 ? QStringLiteral("File_129") : QStringLiteral("CMX_3600");
}

std::optional<Format> formatFromName(const QString& name) {
    const QString n = name.toUpper().remove(QLatin1Char('_')).remove(QLatin1Char(' '));
    if (n == QLatin1String("CMX3600")) return Format::Cmx3600;
    if (n == QLatin1String("FILE129")) return Format::File129;
    return std::nullopt;
}

// CMX 3600 has eight characters for the reel; File_129, as its name says, 129.
int reelLength(Format format) {
    return format == Format::File129 ? 129 : 8;
}

MobId tapeMobId(const QString& reel) {
    return model::derivedMobId(QStringLiteral("edl-reel:") + reel);
}

const model::BinItem* ImportResult::sequence() const {
    return !items.isEmpty() && items.last().kind == model::BinItemKind::Sequence ? &items.last() : nullptr;
}

ImportResult importText(const QString& text, const ImportOptions& options) {
    // Timecodes past what an int64 holds cannot come from a text list of eight digits, but a
    // Rational overflow still must not end the program.
    try {
        return Importer(text, options).run();
    } catch (const std::exception& e) {
        ImportResult r;
        r.warnings << QStringLiteral("the list could not be read: %1").arg(QString::fromUtf8(e.what()));
        return r;
    }
}

std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options, QString* error) {
    const auto text = detail::readText(path, error);
    if (!text) return std::nullopt;
    ImportOptions o = options;
    if (o.name.isEmpty()) o.name = QFileInfo(path).completeBaseName();
    return importText(*text, o);
}

void reuseSources(ImportResult& result, const QList<model::BinItem>& existing) {
    QHash<QString, QString> replaced; // new id -> existing id
    QList<model::BinItem> kept;
    for (const model::BinItem& item : result.items) {
        if (item.kind == model::BinItemKind::Source) {
            const auto it = std::find_if(existing.begin(), existing.end(), [&](const model::BinItem& e) {
                return e.kind == model::BinItemKind::Source && model::mobIdOf(e) == model::mobIdOf(item);
            });
            if (it != existing.end()) {
                replaced.insert(item.id, it->id);
                continue;
            }
        }
        kept << item;
    }
    if (replaced.isEmpty()) return;
    for (model::BinItem& item : kept) {
        if (item.kind != model::BinItemKind::Sequence) continue;
        model::Sequence sequence = model::Sequence::fromItem(item);
        for (const QString& name : sequence.trackNames()) {
            for (model::Segment& s : sequence.track(name)->segments) {
                if (replaced.contains(s.clipId)) s.clipId = replaced.value(s.clipId);
            }
        }
        sequence.storeIn(item);
    }
    result.items = kept;
}

ExportResult exportText(const model::BinItem& sequence, const ExportOptions& options) {
    return Exporter(sequence, options).run();
}

ExportResult exportFile(const model::BinItem& sequence, const QString& path, const ExportOptions& options) {
    ExportResult result = exportText(sequence, options);
    if (!result.ok) return result;
    QString error;
    if (!detail::writeText(path, result.text, &error)) {
        result.ok = false;
        result.error = error;
    }
    return result;
}

} // namespace lumenis::interop::edl
