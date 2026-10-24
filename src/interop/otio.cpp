#include "lumenis/interop/otio.h"

#include "import_common.h"
#include "text_common.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

// OpenTimelineIO's JSON (docs/spec/otio.md). Every object names its type in "OTIO_SCHEMA" as
// "Name.version". A Timeline holds a Stack of Tracks ("Video" or "Audio"); a Track's children are
// Clips, Gaps and Transitions laid end to end (a Transition takes no time: it overlaps the items on
// either side of the cut), and may be nested Tracks or Stacks. Times are RationalTime {value, rate}
// and ranges TimeRange {start_time, duration}. A Clip's source_range is in its media's time, whose
// extent its media reference's available_range gives. The schema names and fields here are those of
// OpenTimelineIO's sample files (Apache-2.0) and of what its Python package reads and writes.
namespace lumenis::interop::otio {

using detail::clampFrames;
using detail::Notes;
using detail::saturatingAdd;

namespace {

// ---- Times

// An OTIO rate (a double: 24, 25, 23.976023976023978) as a Rational: whole rates as they are, NTSC
// rates exact (detail::exactNtscRate), others to a millionth. Nothing for a rate that is not positive
// and finite.
std::optional<Rational> rateOf(double r) {
    if (!std::isfinite(r) || r <= 0.0 || r > 1.0e6) return std::nullopt;
    const double whole = std::round(r);
    if (std::abs(r - whole) < 1.0e-6) return Rational(static_cast<std::int64_t>(whole));
    return detail::exactNtscRate(Rational(static_cast<std::int64_t>(std::llround(r * 1.0e6)), 1000000));
}

// A RationalTime: value frames at rate frames a second (either may be a double).
struct Time {
    double value = 0.0;
    double rate = 0.0;
};

std::optional<Time> timeOf(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    if (o.isEmpty()) return std::nullopt;
    Time t{o.value(QStringLiteral("value")).toDouble(), o.value(QStringLiteral("rate")).toDouble()};
    if (!std::isfinite(t.value) || !std::isfinite(t.rate) || t.rate <= 0.0) return std::nullopt;
    return t;
}

struct Range {
    Time start;
    Time duration;
};

std::optional<Range> rangeOf(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    const auto start = timeOf(o.value(QStringLiteral("start_time")));
    const auto duration = timeOf(o.value(QStringLiteral("duration")));
    if (!start || !duration) return std::nullopt;
    return Range{*start, *duration};
}

// A time as whole frames at `rate`, rounded to the nearest and clamped (detail::clampFrames): a
// damaged file can hold any number.
std::int64_t framesAt(const Time& t, const Rational& rate) {
    const double f = t.value * rate.toDouble() / t.rate;
    if (!std::isfinite(f)) return 0;
    const double limit = static_cast<double>(detail::kMaxFrames);
    return clampFrames(static_cast<std::int64_t>(std::llround(std::clamp(f, -limit, limit))));
}

// The schema's name without its version ("Clip.2" is "Clip"). "Sequence" is what a Track was called
// before OpenTimelineIO renamed it (generator_reference_test.otio holds one).
QString schemaOf(const QJsonObject& o) {
    const QString s = o.value(QStringLiteral("OTIO_SCHEMA")).toString();
    const qsizetype dot = s.indexOf(QLatin1Char('.'));
    const QString name = dot < 0 ? s : s.left(dot);
    return name == QLatin1String("Sequence") ? QStringLiteral("Track") : name;
}

// OpenTimelineIO's JSON allows the numbers Inf, Infinity and NaN (and their negatives), which JSON
// does not and QJsonDocument refuses (big_int.otio holds them, in metadata): outside strings they are
// read as null.
QByteArray withoutNonFinite(const QByteArray& json) {
    QByteArray out;
    out.reserve(json.size());
    bool inString = false;
    for (qsizetype i = 0; i < json.size(); ++i) {
        const char c = json.at(i);
        if (inString) {
            out.append(c);
            if (c == '\\' && i + 1 < json.size()) {
                out.append(json.at(++i));
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out.append(c);
            continue;
        }
        bool replaced = false;
        for (const char* word : {"-Infinity", "Infinity", "-Inf", "Inf", "NaN"}) {
            const qsizetype n = static_cast<qsizetype>(std::strlen(word));
            if (json.mid(i, n) == word) {
                out.append("null");
                i += n - 1;
                replaced = true;
                break;
            }
        }
        if (!replaced) out.append(c);
    }
    return out;
}

QJsonObject rationalTime(std::int64_t frames, const Rational& rate) {
    return QJsonObject{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")},
                       {QStringLiteral("rate"), rate.toDouble()},
                       {QStringLiteral("value"), static_cast<double>(frames)}};
}

QJsonObject timeRange(std::int64_t start, std::int64_t duration, const Rational& rate) {
    return QJsonObject{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
                       {QStringLiteral("start_time"), rationalTime(start, rate)},
                       {QStringLiteral("duration"), rationalTime(duration, rate)}};
}

// ---- Marker colours: OTIO's eleven (MarkerColor in its marker.h), all among ours.

const QStringList& otioColors() {
    static const QStringList colors{QStringLiteral("PINK"),   QStringLiteral("RED"),   QStringLiteral("ORANGE"),
                                    QStringLiteral("YELLOW"), QStringLiteral("GREEN"), QStringLiteral("CYAN"),
                                    QStringLiteral("BLUE"),   QStringLiteral("PURPLE"), QStringLiteral("MAGENTA"),
                                    QStringLiteral("BLACK"),  QStringLiteral("WHITE")};
    return colors;
}

// Our colour for an OTIO colour name (any case), or empty.
QString colorFromOtio(const QString& name) {
    for (const model::MarkerColor& c : model::markerColors()) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) return c.name;
    }
    return {};
}

// The OTIO colour for ours: the same name, or the nearest of OTIO's for the five it lacks.
QString colorToOtio(const QString& name, bool* exact) {
    const QString upper = name.toUpper();
    *exact = otioColors().contains(upper);
    if (*exact) return upper;
    static const std::map<QString, QString> nearest{{QStringLiteral("LIME"), QStringLiteral("GREEN")},
                                                    {QStringLiteral("TEAL"), QStringLiteral("CYAN")},
                                                    {QStringLiteral("NAVY"), QStringLiteral("BLUE")},
                                                    {QStringLiteral("BROWN"), QStringLiteral("ORANGE")},
                                                    {QStringLiteral("GRAY"), QStringLiteral("WHITE")}};
    const auto it = nearest.find(upper);
    return it != nearest.end() ? it->second : QStringLiteral("RED");
}

// A file URL's local path ("file:///Volumes/Media/a.mov" is "/Volumes/Media/a.mov"); other URLs and
// plain paths as they are.
QString pathOfUrl(const QString& url) {
    const QUrl u(url);
    if (u.isLocalFile()) return u.toLocalFile();
    return url;
}

// ---- Import

// What a track's items flatten to: its segments end to end, and the markers on them, frames counted
// from the first segment's start.
struct Flat {
    QList<model::Segment> segments;
    QList<model::Marker> markers;
    std::int64_t length() const {
        std::int64_t n = 0;
        for (const model::Segment& s : segments) n = saturatingAdd(n, s.length);
        return n;
    }
};

// `length` frames of `flat` from `start`, as a Track's or Stack's source_range trims its children:
// segments cut at the window's ends (a clip cut at its start begins later in its source, and loses
// the transition at its start), filler where the window passes the end.
Flat window(const Flat& flat, std::int64_t start, std::int64_t length) {
    Flat out;
    if (length <= 0) return out;
    const std::int64_t end = saturatingAdd(start, length);
    std::int64_t at = 0;
    for (const model::Segment& s : flat.segments) {
        const std::int64_t segEnd = saturatingAdd(at, s.length);
        const std::int64_t from = std::max(at, start);
        const std::int64_t to = std::min(segEnd, end);
        if (from < to) {
            model::Segment part = s;
            part.length = to - from;
            if (from > at) {
                if (!part.isFiller()) part.sourceIn = saturatingAdd(part.sourceIn, from - at);
                part.transition.reset();
            }
            out.segments << part;
        }
        at = segEnd;
        if (at >= end) break;
    }
    if (at < end) out.segments << model::Segment::filler(end - std::max(at, start));
    for (model::Marker m : flat.markers) {
        if (m.frame < start || m.frame >= end) continue;
        m.frame -= start;
        out.markers << m;
    }
    return out;
}

class Importer {
public:
    explicit Importer(const ImportOptions& options) : options_(options) {
        if (!options_.when.isValid()) options_.when = QDateTime::currentDateTimeUtc();
    }

    // The top object: a Timeline, or a SerializableCollection of Timelines (and of further
    // collections). False when it holds none.
    bool read(const QJsonObject& top) {
        collect(top, 0);
        return !sequences_.isEmpty();
    }

    ImportResult finish() {
        ImportResult result;
        for (const QString& key : order_) result.items << clipFor(media_.at(key));
        result.items += sequences_;
        result.warnings = notes_.list();
        return result;
    }

private:
    // One piece of media: the master clip made for it, and what the timelines used of it.
    struct Media {
        model::BinItem item;
        QString url;           // its file, as the reference gives it
        Rational rate;         // the rate of the timeline that first used it
        bool hasRange = false; // available_range given: its start and length
        std::int64_t start = 0;
        std::int64_t length = 0;
        std::int64_t end = 0;  // the end of what was used, for media without a range
        std::set<std::pair<int, int>> tracks; // (0 video, 1 audio; number) used
    };

    void collect(const QJsonObject& o, int depth) {
        const QString schema = schemaOf(o);
        if (schema == QLatin1String("Timeline")) {
            timeline(o);
        } else if (schema == QLatin1String("SerializableCollection") && depth < 8) {
            for (const QJsonValue& c : o.value(QStringLiteral("children")).toArray()) collect(c.toObject(), depth + 1);
        } else if (depth > 0 && !schema.isEmpty()) {
            notes_.add(QStringLiteral("%1 objects outside a timeline left out").arg(schema));
        }
    }

    // The rate of a timeline: its global_start_time's, else that of its first range.
    static std::optional<Rational> rateIn(const QJsonValue& v, int depth = 0) {
        if (depth > 64) return std::nullopt;
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (schemaOf(o) == QLatin1String("RationalTime")) {
                if (const auto t = timeOf(o)) return rateOf(t->rate);
                return std::nullopt;
            }
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (const auto r = rateIn(it.value(), depth + 1)) return r;
            }
        } else if (v.isArray()) {
            for (const QJsonValue& e : v.toArray()) {
                if (const auto r = rateIn(e, depth + 1)) return r;
            }
        }
        return std::nullopt;
    }

    void timeline(const QJsonObject& o) {
        const auto globalStart = timeOf(o.value(QStringLiteral("global_start_time")));
        std::optional<Rational> rate = globalStart ? rateOf(globalStart->rate) : std::nullopt;
        if (!rate) rate = rateIn(o.value(QStringLiteral("tracks")));
        if (!rate) {
            rate = Rational(24);
            notes_.add(QStringLiteral("timelines without a rate read at 24 fps"));
        }
        rate_ = *rate;
        const std::int64_t start = globalStart ? framesAt(*globalStart, rate_) : 0;
        model::Sequence sequence;
        sequence.setTimecodeTrack(true);
        QList<model::Marker> markers;
        const QJsonObject stack = o.value(QStringLiteral("tracks")).toObject();
        int video = 0;
        int audio = 0;
        for (const QJsonValue& t : stack.value(QStringLiteral("children")).toArray()) {
            const QJsonObject track = t.toObject();
            if (schemaOf(track) != QLatin1String("Track")) {
                notes_.add(QStringLiteral("%1 objects in a timeline's stack left out").arg(schemaOf(track)));
                continue;
            }
            const QString kind = track.value(QStringLiteral("kind")).toString();
            QString name;
            if (kind == QLatin1String("Video")) {
                name = model::trackName(model::TrackKind::Video, ++video);
            } else if (kind == QLatin1String("Audio")) {
                name = model::trackName(model::TrackKind::Audio, ++audio);
            } else {
                notes_.add(QStringLiteral("tracks of kind \"%1\" left out").arg(kind));
                continue;
            }
            Flat flat = trackItems(track, name, 0);
            model::Track* native = sequence.ensureTrack(name);
            if (!native) {
                notes_.add(QStringLiteral("tracks past the limit left out"));
                continue;
            }
            native->segments = flat.segments;
            sequence.normalize(name);
            for (model::Marker m : flat.markers) {
                m.track = name;
                markers << m;
            }
        }
        // The stack's own markers are the timeline's: on TC1, as spanned sequence markers are.
        for (model::Marker m : markersOf(stack, Time{0.0, rate_.toDouble()})) {
            m.track = QStringLiteral("TC1");
            markers << m;
        }
        if (!stack.value(QStringLiteral("source_range")).isNull() && !stack.value(QStringLiteral("source_range")).isUndefined()) {
            notes_.add(QStringLiteral("a stack's source_range not read"));
        }
        if (sequence.tracks().isEmpty()) sequence = model::Sequence::withDefaultTracks();

        model::BinItem item;
        item.id = model::newItemId();
        item.kind = model::BinItemKind::Sequence;
        const QString name = o.value(QStringLiteral("name")).toString();
        item.name = !name.isEmpty() ? name : !options_.name.isEmpty() ? options_.name : QStringLiteral("Untitled Sequence");
        item.created = options_.when;
        item.modified = options_.when;
        item.editRate = rate_;
        sequence.storeIn(item);
        QList<int> v;
        QList<int> a;
        for (const model::Track& t : sequence.tracks()) (t.kind == model::TrackKind::Video ? v : a) << t.number;
        item.fields.insert(QStringLiteral("Tracks"), (detail::trackList(v, a) + QStringLiteral(" TC1")).trimmed());
        item.fields.insert(QStringLiteral("Start"), detail::timecodeText(start, rate_, false));
        item.fields.insert(QStringLiteral("End"), detail::timecodeText(saturatingAdd(start, item.durationFrames), rate_, false));
        if (!markers.isEmpty()) model::setMarkers(item, markers);
        sequences_ << item;
    }

    // An item's length in the sequence: its source_range's duration, else (a clip) its media's.
    std::optional<std::int64_t> lengthOf(const QJsonObject& o) const {
        if (const auto r = rangeOf(o.value(QStringLiteral("source_range")))) return framesAt(r->duration, rate_);
        return std::nullopt;
    }

    // A Track's children laid end to end, trimmed to its source_range when it has one.
    Flat trackItems(const QJsonObject& track, const QString& name, int depth) {
        Flat flat;
        std::optional<model::Transition> pending;
        const QJsonArray children = track.value(QStringLiteral("children")).toArray();
        for (qsizetype i = 0; i < children.size(); ++i) {
            const QJsonObject c = children.at(i).toObject();
            if (schemaOf(c) == QLatin1String("Transition")) {
                if (flat.segments.isEmpty() || i + 1 == children.size()) {
                    notes_.add(QStringLiteral("transitions at a track's start or end left out"));
                    continue;
                }
                pending = transitionOf(c);
                continue;
            }
            Flat part = item(c, name, depth);
            if (part.segments.isEmpty()) continue;
            if (pending) {
                part.segments.first().transition = pending;
                pending.reset();
            }
            const std::int64_t at = flat.length();
            for (model::Marker m : part.markers) {
                m.frame = saturatingAdd(m.frame, at);
                flat.markers << m;
            }
            flat.segments += part.segments;
        }
        for (model::Marker m : markersOf(track, Time{0.0, rate_.toDouble()})) flat.markers << m;
        return trimmed(flat, track);
    }

    // A Track's or Stack's items trimmed to its source_range. One that starts before the items
    // (screening_example.otio's track starts at -86243, its record timecode, for 1049 frames: none of
    // its clips would show) is ignored, with a warning: the items are read as they are laid out, as
    // OpenTimelineIO places them (transformed_time_range).
    Flat trimmed(const Flat& flat, const QJsonObject& o) {
        const auto r = rangeOf(o.value(QStringLiteral("source_range")));
        if (!r) return flat;
        const std::int64_t start = framesAt(r->start, rate_);
        if (start < 0) {
            notes_.add(QStringLiteral("source_ranges starting before their track ignored"));
            return flat;
        }
        return window(flat, start, framesAt(r->duration, rate_));
    }

    // In OTIO a transition overlaps the cut: in_offset frames of the outgoing item before it,
    // out_offset of the incoming after. Ours is on the incoming segment, `before()` frames before the
    // cut. SMPTE_Dissolve is a Dissolve; other types keep their name, with a warning.
    model::Transition transitionOf(const QJsonObject& c) {
        model::Transition t;
        const auto in = timeOf(c.value(QStringLiteral("in_offset")));
        const auto out = timeOf(c.value(QStringLiteral("out_offset")));
        const std::int64_t before = std::max<std::int64_t>(0, in ? framesAt(*in, rate_) : 0);
        const std::int64_t after = std::max<std::int64_t>(0, out ? framesAt(*out, rate_) : 0);
        t.duration = saturatingAdd(before, after);
        if (before == 0) {
            t.alignment = model::TransitionAlignment::StartingAtCut;
        } else if (after == 0) {
            t.alignment = model::TransitionAlignment::EndingAtCut;
        } else if (before == after) {
            t.alignment = model::TransitionAlignment::CenteredOnCut;
        } else {
            t.alignment = model::TransitionAlignment::CustomStart;
            t.start = before;
        }
        const QString type = c.value(QStringLiteral("transition_type")).toString();
        if (type == QLatin1String("SMPTE_Dissolve")) {
            t.effect.category = QStringLiteral("Blend");
            t.effect.name = QStringLiteral("Dissolve");
        } else {
            t.effect.category = QStringLiteral("OTIO");
            t.effect.name = type.isEmpty() ? c.value(QStringLiteral("name")).toString() : type;
            notes_.add(QStringLiteral("transitions other than SMPTE_Dissolve kept by their type"));
        }
        return t;
    }

    // An item's markers, frames counted from `origin` (the item's first frame in its own time); with
    // a `length`, those past it (a clip's markers on media it does not use) are left out too.
    QList<model::Marker> markersOf(const QJsonObject& o, const Time& origin, std::optional<std::int64_t> length = std::nullopt) {
        QList<model::Marker> out;
        for (const QJsonValue& v : o.value(QStringLiteral("markers")).toArray()) {
            const QJsonObject m = v.toObject();
            const auto r = rangeOf(m.value(QStringLiteral("marked_range")));
            if (!r) {
                notes_.add(QStringLiteral("markers without a marked_range left out"));
                continue;
            }
            model::Marker marker;
            marker.id = model::newItemId();
            marker.name = m.value(QStringLiteral("name")).toString();
            marker.comment = m.value(QStringLiteral("comment")).toString();
            marker.color = colorFromOtio(m.value(QStringLiteral("color")).toString());
            if (marker.color.isEmpty()) marker.color = model::markerColors().first().name;
            marker.frame = framesAt(r->start, rate_) - framesAt(origin, rate_);
            marker.length = std::max<std::int64_t>(1, framesAt(r->duration, rate_));
            if (marker.frame < 0 || (length && marker.frame >= *length)) {
                notes_.add(QStringLiteral("markers outside their item left out"));
                continue;
            }
            out << marker;
        }
        return out;
    }

    Flat item(const QJsonObject& c, const QString& track, int depth) {
        const QString schema = schemaOf(c);
        if (depth > 32) {
            notes_.add(QStringLiteral("items nested too deep left out"));
            return {};
        }
        if (schema == QLatin1String("Gap")) {
            Flat f;
            const std::int64_t length = lengthOf(c).value_or(0);
            if (length > 0) f.segments << model::Segment::filler(length);
            return f;
        }
        if (schema == QLatin1String("Clip")) return clip(c, track);
        if (schema == QLatin1String("Track")) return trackItems(c, track, depth + 1);
        if (schema == QLatin1String("Stack")) return stackItems(c, track, depth + 1);
        // Anything else (an unknown item) is a gap of its length, so the cuts after it stay put.
        notes_.add(QStringLiteral("%1 items read as gaps").arg(schema.isEmpty() ? QStringLiteral("unknown") : schema));
        Flat f;
        if (const auto length = lengthOf(c); length && *length > 0) f.segments << model::Segment::filler(*length);
        return f;
    }

    // A nested Stack (a sequence inside a track): its top track that has material, as the picture
    // shows it; the others are left out with a warning.
    Flat stackItems(const QJsonObject& stack, const QString& track, int depth) {
        QList<Flat> layers;
        for (const QJsonValue& t : stack.value(QStringLiteral("children")).toArray()) {
            const QJsonObject o = t.toObject();
            layers << (schemaOf(o) == QLatin1String("Track") ? trackItems(o, track, depth + 1) : item(o, track, depth + 1));
        }
        Flat chosen;
        int withMaterial = 0;
        for (const Flat& f : layers) {
            if (std::any_of(f.segments.begin(), f.segments.end(), [](const model::Segment& s) { return !s.isFiller(); })) {
                ++withMaterial;
                chosen = f;
            }
        }
        if (withMaterial > 1) notes_.add(QStringLiteral("nested stacks read as their top track"));
        if (withMaterial == 0 && !layers.isEmpty()) chosen = layers.last();
        for (model::Marker m : markersOf(stack, Time{0.0, rate_.toDouble()})) chosen.markers << m;
        return trimmed(chosen, stack);
    }

    // The media reference a clip uses: Clip.2's active one of media_references, Clip.1's
    // media_reference.
    static QJsonObject referenceOf(const QJsonObject& c) {
        const QJsonObject refs = c.value(QStringLiteral("media_references")).toObject();
        if (!refs.isEmpty()) {
            QString key = c.value(QStringLiteral("active_media_reference_key")).toString();
            if (key.isEmpty() || !refs.contains(key)) key = refs.contains(QStringLiteral("DEFAULT_MEDIA")) ? QStringLiteral("DEFAULT_MEDIA") : refs.keys().first();
            return refs.value(key).toObject();
        }
        return c.value(QStringLiteral("media_reference")).toObject();
    }

    Flat clip(const QJsonObject& c, const QString& track) {
        Flat f;
        const QJsonObject ref = referenceOf(c);
        const QString refSchema = schemaOf(ref);
        const auto source = rangeOf(c.value(QStringLiteral("source_range")));
        const auto available = rangeOf(ref.value(QStringLiteral("available_range")));
        std::int64_t start = 0;
        std::int64_t length = 0;
        if (source) {
            start = framesAt(source->start, rate_);
            length = framesAt(source->duration, rate_);
        } else if (available) {
            start = framesAt(available->start, rate_);
            length = framesAt(available->duration, rate_);
        } else {
            notes_.add(QStringLiteral("clips without a source_range or available_range left out"));
            return f;
        }
        if (rateOf(source ? source->start.rate : available->start.rate) != rate_) {
            notes_.add(QStringLiteral("times at other rates than the timeline's converted to its rate"));
        }
        if (length <= 0) return f;
        model::Segment s = model::Segment::filler(length);
        const QJsonValue enabled = c.value(QStringLiteral("enabled"));
        if (enabled.isBool() && !enabled.toBool()) {
            notes_.add(QStringLiteral("disabled clips read as gaps"));
            f.segments << s;
            return f;
        }
        if (refSchema == QLatin1String("GeneratorReference")) {
            notes_.add(QStringLiteral("generators (%1) read as gaps").arg(ref.value(QStringLiteral("generator_kind")).toString()));
            f.segments << s;
            return f;
        }
        // The media's key: its file, else the reference's name, else the clip's.
        QString url = ref.value(QStringLiteral("target_url")).toString();
        if (refSchema == QLatin1String("ImageSequenceReference")) {
            url = ref.value(QStringLiteral("target_url_base")).toString() + ref.value(QStringLiteral("name_prefix")).toString() +
                  QStringLiteral("#") + ref.value(QStringLiteral("name_suffix")).toString();
            notes_.add(QStringLiteral("image sequences named by their first file's pattern"));
        }
        const QString clipName = c.value(QStringLiteral("name")).toString();
        const QString refName = ref.value(QStringLiteral("name")).toString();
        QString key = !url.isEmpty() ? url : !refName.isEmpty() ? refName : clipName;
        if (key.isEmpty()) {
            key = QStringLiteral("untitled");
            notes_.add(QStringLiteral("clips without a name or media named \"untitled\""));
        }
        Media& m = mediaFor(key, url, !refName.isEmpty() ? refName : clipName, available);
        const auto ref2 = model::parseTrackName(track);
        if (ref2) m.tracks.insert({ref2->kind == model::TrackKind::Video ? 0 : 1, ref2->kind == model::TrackKind::Video ? 1 : ref2->number});
        s.clipId = m.item.id;
        s.binId = options_.binId;
        s.sourceMob = m.item.mobId;
        s.name = !clipName.isEmpty() ? clipName : m.item.name;
        s.sourceTrack = ref2 && ref2->kind == model::TrackKind::Video ? QStringLiteral("V1") : track;
        s.sourceIn = start - m.start;
        if (s.sourceIn < 0) {
            notes_.add(QStringLiteral("clips starting before their media's available_range"));
            s.sourceIn = 0;
        }
        m.end = std::max(m.end, saturatingAdd(s.sourceIn, length));
        // Speed changes: LinearTimeWarp's time_scalar, FreezeFrame (0); others are left out.
        std::optional<double> speed;
        for (const QJsonValue& e : c.value(QStringLiteral("effects")).toArray()) {
            const QJsonObject effect = e.toObject();
            const QString schema = schemaOf(effect);
            if (schema == QLatin1String("LinearTimeWarp")) {
                const double scalar = effect.value(QStringLiteral("time_scalar")).toDouble(1.0);
                speed = speed.value_or(1.0) * (std::isfinite(scalar) ? scalar : 1.0);
            } else if (schema == QLatin1String("FreezeFrame")) {
                speed = 0.0;
            } else {
                const QString name = effect.value(QStringLiteral("effect_name")).toString();
                notes_.add(QStringLiteral("effects not read (%1)").arg(name.isEmpty() ? schema : name));
            }
        }
        if (speed && *speed != 1.0) s.effect = detail::motionEffect(*speed);
        f.segments << s;
        // A clip's markers are in its media's time: counted from the source_range's start.
        f.markers = markersOf(c, source ? source->start : available->start, length);
        return f;
    }

    Media& mediaFor(const QString& key, const QString& url, const QString& name, const std::optional<Range>& available) {
        auto it = media_.find(key);
        if (it == media_.end()) {
            Media m;
            m.item.id = model::newItemId();
            m.item.mobId = mediaMobId(key);
            m.item.name = !name.isEmpty() ? name : QFileInfo(pathOfUrl(url)).completeBaseName();
            if (m.item.name.isEmpty()) m.item.name = key;
            m.item.created = options_.when;
            m.item.modified = options_.when;
            m.url = url;
            m.rate = rate_;
            if (available) {
                m.hasRange = true;
                m.start = framesAt(available->start, rate_);
                m.length = std::max<std::int64_t>(0, framesAt(available->duration, rate_));
            }
            order_ << key;
            it = media_.emplace(key, m).first;
        }
        return it->second;
    }

    // The master clip for a piece of media: offline (no media file linked), its file kept as its
    // import source so that Relink finds it (p.969).
    model::BinItem clipFor(const Media& m) const {
        model::BinItem item = m.item;
        item.editRate = m.rate;
        item.durationFrames = m.hasRange ? std::max(m.length, m.end) : m.end;
        item.fields.insert(QStringLiteral("Start"), detail::timecodeText(m.start, m.rate, false));
        item.fields.insert(QStringLiteral("End"), detail::timecodeText(saturatingAdd(m.start, item.durationFrames), m.rate, false));
        QList<int> video;
        QList<int> audio;
        for (const auto& [kind, number] : m.tracks) {
            if (kind == 0) {
                video << number;
            } else {
                for (int n = 1; n <= number; ++n) audio << n;
            }
        }
        if (video.isEmpty() && audio.isEmpty()) video << 1;
        item.fields.insert(QStringLiteral("Tracks"), detail::trackList(video, audio));
        item.kind = video.isEmpty() ? model::BinItemKind::AudioClip : model::BinItemKind::MasterClip;
        if (!m.url.isEmpty()) {
            model::ClipSource source;
            source.kind = model::ClipSource::Kind::Import;
            source.mob = model::derivedMobId(QStringLiteral("otio-file:") + m.url);
            source.path = pathOfUrl(m.url);
            source.name = QFileInfo(source.path).fileName();
            source.startTimecode = item.fields.value(QStringLiteral("Start"));
            item.sources << source;
            item.fields.insert(QStringLiteral("Source File"), source.name);
        }
        return item;
    }

    ImportOptions options_;
    Rational rate_{24};
    std::map<QString, Media> media_;
    QStringList order_;
    QList<model::BinItem> sequences_;
    Notes notes_;
};

// ---- Export

class Exporter {
public:
    Exporter(const model::BinItem& item, const ExportOptions& options) : item_(item), options_(options), rate_(item.editRate) {}

    ExportResult run() {
        ExportResult result;
        if (item_.kind != model::BinItemKind::Sequence) {
            result.error = QStringLiteral("%1 is not a sequence").arg(item_.name);
            return result;
        }
        const std::int64_t start = detail::timecodeFrames(item_.fields.value(QStringLiteral("Start")), rate_).value_or(0);
        const model::Sequence sequence = model::Sequence::fromItem(item_);
        QJsonArray tracks;
        QSet<QString> written;
        for (const model::TrackKind kind : {model::TrackKind::Video, model::TrackKind::Audio}) {
            for (const model::Track& t : sequence.tracks()) {
                if (t.kind != kind) continue;
                if (!options_.tracks.isEmpty() && !options_.tracks.contains(t.name())) continue;
                tracks.append(track(t));
                written.insert(t.name());
            }
        }
        if (std::any_of(sequence.tracks().begin(), sequence.tracks().end(),
                        [](const model::Track& t) { return t.kind == model::TrackKind::Data; })) {
            notes_.add(QStringLiteral("data tracks left out"));
        }
        // Markers: on their track when it was written, the others on the stack (the timeline's).
        QJsonArray stackMarkers;
        for (const model::Marker& m : model::markersOf(item_)) {
            QJsonObject marker = markerJson(m);
            bool placed = false;
            if (written.contains(m.track)) {
                for (qsizetype i = 0; i < tracks.size(); ++i) {
                    QJsonObject t = tracks.at(i).toObject();
                    if (t.value(QStringLiteral("name")).toString() != m.track) continue;
                    QJsonArray list = t.value(QStringLiteral("markers")).toArray();
                    list.append(marker);
                    t.insert(QStringLiteral("markers"), list);
                    tracks.replace(i, t);
                    placed = true;
                    break;
                }
            }
            if (!placed) stackMarkers.append(marker);
        }
        QJsonObject stack{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Stack.1")},
                          {QStringLiteral("name"), QStringLiteral("tracks")},
                          {QStringLiteral("metadata"), QJsonObject{}},
                          {QStringLiteral("source_range"), QJsonValue::Null},
                          {QStringLiteral("effects"), QJsonArray{}},
                          {QStringLiteral("markers"), stackMarkers},
                          {QStringLiteral("enabled"), true},
                          {QStringLiteral("children"), tracks}};
        QJsonObject timeline{{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("Timeline.1")},
                             {QStringLiteral("name"), item_.name},
                             {QStringLiteral("metadata"), QJsonObject{}},
                             {QStringLiteral("global_start_time"), rationalTime(start, rate_)},
                             {QStringLiteral("tracks"), stack}};
        result.ok = true;
        result.json = QJsonDocument(timeline).toJson(QJsonDocument::Indented);
        result.warnings = notes_.list();
        result.clips = clips_;
        return result;
    }

private:
    static QJsonObject common(const QString& schema, const QString& name) {
        return QJsonObject{{QStringLiteral("OTIO_SCHEMA"), schema},
                           {QStringLiteral("name"), name},
                           {QStringLiteral("metadata"), QJsonObject{}}};
    }

    QJsonObject markerJson(const model::Marker& m) {
        QJsonObject o = common(QStringLiteral("Marker.2"), m.name);
        bool exact = true;
        o.insert(QStringLiteral("color"), colorToOtio(m.color, &exact));
        if (!exact) notes_.add(QStringLiteral("marker colours OTIO lacks written as the nearest"));
        o.insert(QStringLiteral("comment"), m.comment);
        o.insert(QStringLiteral("marked_range"), timeRange(m.frame, m.length > 1 ? m.length : 0, rate_));
        return o;
    }

    QJsonObject track(const model::Track& t) {
        QJsonObject o = common(QStringLiteral("Track.1"), t.name());
        o.insert(QStringLiteral("kind"), t.kind == model::TrackKind::Video ? QStringLiteral("Video") : QStringLiteral("Audio"));
        o.insert(QStringLiteral("source_range"), QJsonValue::Null);
        o.insert(QStringLiteral("effects"), QJsonArray{});
        o.insert(QStringLiteral("markers"), QJsonArray{});
        o.insert(QStringLiteral("enabled"), true);
        QJsonArray children;
        for (qsizetype i = 0; i < t.segments.size(); ++i) {
            const model::Segment& s = t.segments.at(i);
            if (s.transition && s.transition->duration > 0) {
                if (i == 0) {
                    notes_.add(QStringLiteral("transitions at a track's start left out"));
                } else {
                    children.append(transition(*s.transition));
                }
            }
            children.append(s.isFiller() ? gap(s) : clip(s));
        }
        o.insert(QStringLiteral("children"), children);
        return o;
    }

    QJsonObject transition(const model::Transition& t) {
        const bool dissolve = t.effect.name == QLatin1String("Dissolve");
        QJsonObject o = common(QStringLiteral("Transition.1"), t.effect.name);
        o.insert(QStringLiteral("transition_type"), dissolve ? QStringLiteral("SMPTE_Dissolve") : QStringLiteral("Custom_Transition"));
        if (!dissolve) notes_.add(QStringLiteral("transitions other than dissolves written as Custom_Transition"));
        o.insert(QStringLiteral("in_offset"), rationalTime(t.before(), rate_));
        o.insert(QStringLiteral("out_offset"), rationalTime(t.after(), rate_));
        return o;
    }

    QJsonObject gap(const model::Segment& s) {
        QJsonObject o = common(QStringLiteral("Gap.1"), QString());
        o.insert(QStringLiteral("source_range"), timeRange(0, s.length, rate_));
        o.insert(QStringLiteral("effects"), QJsonArray{});
        o.insert(QStringLiteral("markers"), QJsonArray{});
        o.insert(QStringLiteral("enabled"), true);
        if (s.effect) notes_.add(QStringLiteral("effects on filler left out"));
        return o;
    }

    // The clip's media file: its link, else a file or import source with a path.
    static QString mediaPath(const model::BinItem& clip) {
        const QString linked = clip.link.value(QStringLiteral("path")).toString();
        if (!linked.isEmpty()) return linked;
        for (const model::ClipSource& s : clip.sources) {
            if ((s.kind == model::ClipSource::Kind::File || s.kind == model::ClipSource::Kind::Import) && !s.path.isEmpty()) return s.path;
        }
        return {};
    }

    QJsonObject clip(const model::Segment& s) {
        ++clips_;
        std::optional<model::BinItem> source = options_.resolve ? options_.resolve(s.sourceMobId(), s.clipId) : std::nullopt;
        std::int64_t mediaStart = 0;
        QJsonObject reference;
        if (source) {
            const Rational clipRate = source->editRate;
            const auto clipStart = detail::timecodeFrames(source->fields.value(QStringLiteral("Start")), clipRate);
            mediaStart = clipStart ? detail::rescale(*clipStart, clipRate, rate_) : 0;
            if (clipRate != rate_) notes_.add(QStringLiteral("clips at other rates than the sequence's written at its rate"));
            const std::int64_t length = detail::rescale(source->durationFrames, clipRate, rate_);
            const QString path = mediaPath(*source);
            if (!path.isEmpty()) {
                reference = common(QStringLiteral("ExternalReference.1"), QFileInfo(path).fileName());
                // An absolute path as a file URL; a relative one (as an import of "black.mov" keeps it) as
                // the relative URL it was.
                reference.insert(QStringLiteral("target_url"), QFileInfo(path).isAbsolute() || path.startsWith(QLatin1String("//"))
                                                                    ? QUrl::fromLocalFile(path).toString()
                                                                    : path);
            } else {
                reference = common(QStringLiteral("MissingReference.1"), source->name);
                notes_.add(QStringLiteral("clips without a media file written with a MissingReference"));
            }
            reference.insert(QStringLiteral("available_range"), timeRange(mediaStart, length, rate_));
        } else {
            reference = common(QStringLiteral("MissingReference.1"), s.name);
            reference.insert(QStringLiteral("available_range"), QJsonValue::Null);
            notes_.add(QStringLiteral("clips not found: their media named by the segment, counted from 0"));
        }
        QJsonObject o = common(QStringLiteral("Clip.2"), s.name);
        o.insert(QStringLiteral("source_range"), timeRange(saturatingAdd(mediaStart, s.sourceIn), s.length, rate_));
        o.insert(QStringLiteral("media_references"), QJsonObject{{QStringLiteral("DEFAULT_MEDIA"), reference}});
        o.insert(QStringLiteral("active_media_reference_key"), QStringLiteral("DEFAULT_MEDIA"));
        QJsonArray effects;
        if (s.effect) {
            if (const auto speed = detail::speedOf(*s.effect)) {
                if (*speed == 0.0) {
                    QJsonObject e = common(QStringLiteral("FreezeFrame.1"), QString());
                    e.insert(QStringLiteral("effect_name"), QStringLiteral("FreezeFrame"));
                    e.insert(QStringLiteral("time_scalar"), 0.0);
                    effects.append(e);
                } else {
                    QJsonObject e = common(QStringLiteral("LinearTimeWarp.1"), QString());
                    e.insert(QStringLiteral("effect_name"), QStringLiteral("LinearTimeWarp"));
                    e.insert(QStringLiteral("time_scalar"), *speed);
                    effects.append(e);
                }
            } else {
                notes_.add(QStringLiteral("effects other than speed changes left out (%1)").arg(s.effect->name));
            }
        }
        if (s.gain != 0.0 || s.pan != 0) notes_.add(QStringLiteral("clip gain and pan left out"));
        o.insert(QStringLiteral("effects"), effects);
        o.insert(QStringLiteral("markers"), QJsonArray{});
        o.insert(QStringLiteral("enabled"), true);
        return o;
    }

    const model::BinItem& item_;
    const ExportOptions& options_;
    Rational rate_;
    Notes notes_;
    int clips_ = 0;
};

} // namespace

MobId mediaMobId(const QString& key) {
    return model::derivedMobId(QStringLiteral("otio-media:") + key);
}

std::optional<ImportResult> importJson(const QByteArray& json, const ImportOptions& options, QString* error) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(withoutNonFinite(json), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = parseError.error != QJsonParseError::NoError ? parseError.errorString() : QStringLiteral("not an OTIO file");
        return std::nullopt;
    }
    Importer importer(options);
    if (!importer.read(doc.object())) {
        if (error) *error = QStringLiteral("no timeline in it");
        return std::nullopt;
    }
    return importer.finish();
}

std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return std::nullopt;
    }
    ImportOptions o = options;
    if (o.name.isEmpty()) o.name = QFileInfo(path).completeBaseName();
    return importJson(f.readAll(), o, error);
}

void reuseClips(ImportResult& result, const QList<model::BinItem>& existing) {
    std::map<QString, model::BinItem> replaced; // dropped clip id -> the existing clip
    QList<model::BinItem> kept;
    for (const model::BinItem& item : result.items) {
        if (item.kind != model::BinItemKind::Sequence) {
            const auto found = std::find_if(existing.begin(), existing.end(), [&](const model::BinItem& e) {
                return e.kind != model::BinItemKind::Sequence && model::mobIdOf(e) == model::mobIdOf(item);
            });
            if (found != existing.end()) {
                replaced.emplace(item.id, *found);
                continue;
            }
        }
        kept << item;
    }
    for (model::BinItem& item : kept) {
        if (item.kind != model::BinItemKind::Sequence) continue;
        model::Sequence sequence = model::Sequence::fromItem(item);
        for (const QString& name : sequence.trackNames()) {
            model::Track* t = sequence.track(name);
            if (!t) continue;
            for (model::Segment& s : t->segments) {
                const auto it = replaced.find(s.clipId);
                if (it == replaced.end()) continue;
                s.clipId = it->second.id;
                s.sourceMob = model::mobIdOf(it->second);
            }
        }
        sequence.storeIn(item);
    }
    result.items = kept;
}

ExportResult exportJson(const model::BinItem& sequence, const ExportOptions& options) {
    return Exporter(sequence, options).run();
}

ExportResult exportFile(const model::BinItem& sequence, const QString& path, const ExportOptions& options) {
    ExportResult result = exportJson(sequence, options);
    if (!result.ok) return result;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(result.json) != result.json.size() || !f.commit()) {
        result.ok = false;
        result.error = f.errorString();
    }
    return result;
}

} // namespace lumenis::interop::otio
