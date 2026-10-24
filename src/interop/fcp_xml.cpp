#include "lumenis/interop/fcp_xml.h"

#include "import_common.h"
#include "text_common.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

// Final Cut Pro 7 XML (docs/spec/fcp-xml.md). An <xmeml> document holds sequences, directly or in
// projects and bins (inside <children>). A sequence's <media> holds <video> and <audio>, each with
// <track>s of <clipitem>, <generatoritem> and <transitionitem>. An item's <start> and <end> are its
// place in the track; a clip's <in> and <out> are frames of its media counted from the media's start,
// and its <file> names the media (<pathurl>, <timecode>, <duration>). A start or end of -1 is the cut
// point of the transition beside it. Values not given on an element are inherited from the elements
// it is inside (a clip's <rate> from its track's, its sequence's), and an element with an id given
// again later is that first element (a <file id> written once in full, then only referred to).
//
// The format is Apple's published Final Cut Pro XML Interchange Format. How programs write it in
// practice, and how it maps to OpenTimelineIO, follow OpenTimelineIO's FCP 7 XML adapter (Apache-2.0,
// github.com/OpenTimelineIO/otio-fcp-adapter), whose sample files are in the interchange corpus and
// whose reader checks this one (scripts/otio-check.py, ctest fcpxml_python). No code of it is copied.
namespace lumenis::interop::fcpxml {

using detail::Notes;

namespace {

// ---- A small element tree

constexpr int kMaxDepth = 256;            // deeper documents are refused
constexpr int kMaxNesting = 16;           // sequences nested in clips this deep are read as gaps
constexpr std::int64_t kMaxElements = 4'000'000;

struct Node {
    QString tag;
    QHash<QString, QString> attributes;
    QString text;
    std::vector<std::unique_ptr<Node>> children;

    const Node* child(QLatin1String name) const {
        for (const auto& c : children) {
            if (c->tag == name) return c.get();
        }
        return nullptr;
    }
    QString childText(QLatin1String name) const {
        const Node* c = child(name);
        return c ? c->text.trimmed() : QString();
    }
    std::vector<const Node*> all(QLatin1String name) const {
        std::vector<const Node*> out;
        for (const auto& c : children) {
            if (c->tag == name) out.push_back(c.get());
        }
        return out;
    }
};

std::unique_ptr<Node> parse(const QByteArray& xml, QString* error) {
    QXmlStreamReader reader(xml);
    std::unique_ptr<Node> root;
    std::vector<Node*> open;
    std::int64_t count = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (static_cast<int>(open.size()) >= kMaxDepth || ++count > kMaxElements) {
                if (error) *error = QStringLiteral("the document is nested too deep or too large");
                return nullptr;
            }
            auto node = std::make_unique<Node>();
            node->tag = reader.name().toString();
            for (const QXmlStreamAttribute& a : reader.attributes()) node->attributes.insert(a.name().toString(), a.value().toString());
            Node* raw = node.get();
            if (open.empty()) {
                if (root) break; // a second root: not well formed; the reader reports it
                root = std::move(node);
            } else {
                open.back()->children.push_back(std::move(node));
            }
            open.push_back(raw);
        } else if (token == QXmlStreamReader::EndElement) {
            if (!open.empty()) open.pop_back();
        } else if (token == QXmlStreamReader::Characters && !open.empty()) {
            open.back()->text += reader.text();
        }
    }
    if (reader.hasError()) {
        if (error) *error = QStringLiteral("not well-formed XML (line %1): %2").arg(reader.lineNumber()).arg(reader.errorString());
        return nullptr;
    }
    if (!root) {
        if (error) *error = QStringLiteral("no XML element in it");
        return nullptr;
    }
    return root;
}

// ---- Reading

// A frame count from an element's text ("120", "1055.0"); nothing when it is not a finite number.
std::optional<double> number(const QString& text) {
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (!ok || !std::isfinite(v)) return std::nullopt;
    return std::clamp(v, -static_cast<double>(detail::kMaxFrames), static_cast<double>(detail::kMaxFrames));
}

bool truth(const QString& text) {
    return text.compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0 || text == QLatin1String("1");
}

// A <rate>'s frames a second: <timebase>, times 1000/1001 when <ntsc> is TRUE.
std::optional<double> rateValue(const Node* rate) {
    if (!rate) return std::nullopt;
    const auto base = number(rate->childText(QLatin1String("timebase")));
    if (!base || *base <= 0.0 || *base > 1.0e5) return std::nullopt;
    return truth(rate->childText(QLatin1String("ntsc"))) ? *base * 1000.0 / 1001.0 : *base;
}

QJsonObject rationalTime(double value, double rate) {
    return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("RationalTime.1")}, {QStringLiteral("rate"), rate}, {QStringLiteral("value"), value}};
}
QJsonObject timeRange(double start, double startRate, double duration, double durationRate) {
    return {{QStringLiteral("OTIO_SCHEMA"), QStringLiteral("TimeRange.1")},
            {QStringLiteral("start_time"), rationalTime(start, startRate)},
            {QStringLiteral("duration"), rationalTime(duration, durationRate)}};
}
QJsonObject object(const QString& schema, const QString& name) {
    return {{QStringLiteral("OTIO_SCHEMA"), schema}, {QStringLiteral("name"), name}, {QStringLiteral("metadata"), QJsonObject{}}};
}

// The elements a value is looked up in, innermost last: FCP XML's inheritance (a clip without a
// <rate> has its track's). As the walk goes into an element it is pushed, so an element referred to
// by id inherits from where it is used, as OpenTimelineIO's adapter reads it.
using Context = std::vector<const Node*>;

class Reader {
public:
    explicit Reader(const Node& root) : root_(root) { index(root); }

    // The document's sequences as OTIO JSON; empty when there is none.
    std::optional<QJsonObject> read() {
        std::vector<const Node*> sequences;
        for (const Node* s : root_.all(QLatin1String("sequence"))) sequences.push_back(deref(s));
        findInChildren(root_, sequences, 0);
        QJsonArray timelines;
        std::vector<const Node*> seen;
        for (const Node* s : sequences) {
            if (std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
            seen.push_back(s);
            timelines.append(timeline(*s));
        }
        if (timelines.isEmpty()) return std::nullopt;
        if (timelines.size() == 1) return timelines.first().toObject();
        QJsonObject collection = object(QStringLiteral("SerializableCollection.1"), QString());
        collection.insert(QStringLiteral("children"), timelines);
        return collection;
    }

    QStringList warnings() const { return notes_.list(); }

private:
    // The first element with each id (one with children, a definition, before a bare reference).
    void index(const Node& n) {
        const QString id = n.attributes.value(QStringLiteral("id"));
        if (!id.isEmpty()) {
            const QString key = n.tag + QLatin1Char('\n') + id;
            const auto it = ids_.find(key);
            if (it == ids_.end() || (it.value()->children.empty() && !n.children.empty())) ids_.insert(key, &n);
        }
        for (const auto& c : n.children) index(*c);
    }
    const Node* deref(const Node* n) const {
        if (!n) return n;
        const QString id = n->attributes.value(QStringLiteral("id"));
        if (id.isEmpty()) return n;
        return ids_.value(n->tag + QLatin1Char('\n') + id, n);
    }

    // Sequences in projects and bins: <children> at any depth outside clips and sequences.
    void findInChildren(const Node& n, std::vector<const Node*>& out, int depth) {
        if (depth > kMaxDepth) return;
        for (const auto& c : n.children) {
            if (c->tag == QLatin1String("sequence") || c->tag == QLatin1String("clip") || c->tag == QLatin1String("clipitem")) {
                if (n.tag == QLatin1String("children") && c->tag == QLatin1String("sequence")) out.push_back(deref(c.get()));
                continue;
            }
            findInChildren(*c, out, depth + 1);
        }
    }

    // The rate in force in the context: the innermost element with a <rate>; 24 fps, with a
    // warning, when none has.
    double rateIn(const Context& context) {
        for (auto it = context.rbegin(); it != context.rend(); ++it) {
            if (const auto r = rateValue((*it)->child(QLatin1String("rate")))) return *r;
        }
        notes_.add(QStringLiteral("elements without a rate read at 24 fps"));
        return 24.0;
    }

    // A <timecode>'s frame: its <frame>, else its <string> read at its rate.
    std::optional<double> timecodeFrame(const Node* tc, Context context) {
        if (!tc) return std::nullopt;
        context.push_back(tc);
        if (const auto frame = number(tc->childText(QLatin1String("frame")))) return std::round(*frame);
        const double rate = rateIn(context);
        const auto r = otioRate(rate);
        const auto frames = r ? detail::timecodeFrames(tc->childText(QLatin1String("string")), *r) : std::nullopt;
        if (!frames) return std::nullopt;
        return static_cast<double>(*frames);
    }
    static std::optional<Rational> otioRate(double r) {
        const double whole = std::round(r);
        if (std::abs(r - whole) < 1.0e-6) return Rational(static_cast<std::int64_t>(whole));
        return detail::exactNtscRate(Rational(static_cast<std::int64_t>(std::llround(r * 1.0e6)), 1000000));
    }

    QJsonObject timeline(const Node& sequence) {
        Context context{&sequence};
        const double rate = rateIn(context);
        QJsonObject t = object(QStringLiteral("Timeline.1"), sequence.childText(QLatin1String("name")));
        if (const auto start = timecodeFrame(deref(sequence.child(QLatin1String("timecode"))), context)) {
            t.insert(QStringLiteral("global_start_time"), rationalTime(*start, rate));
        } else {
            t.insert(QStringLiteral("global_start_time"), rationalTime(0.0, rate));
        }
        QJsonObject stack = stackOf(sequence, context, 0);
        stack.insert(QStringLiteral("name"), QStringLiteral("tracks"));
        // The sequence's markers are the timeline's: on the stack.
        QJsonArray markers = stack.value(QStringLiteral("markers")).toArray();
        for (const QJsonValue& m : markersOf(sequence, context, 0.0)) markers.append(m);
        stack.insert(QStringLiteral("markers"), markers);
        t.insert(QStringLiteral("tracks"), stack);
        return t;
    }

    // A sequence's tracks as a Stack: <media>'s video tracks, then its audio tracks (for a sequence
    // nested in a clip, only those of the clip's track's kind: a nested sequence on a video track is
    // its picture). Of audio that is "exploded" (a stereo pair as two tracks), the first of each
    // group is read, as one track.
    QJsonObject stackOf(const Node& sequence, Context context, int depth, std::optional<bool> onlyVideo = std::nullopt) {
        QJsonArray tracks;
        const Node* media = deref(sequence.child(QLatin1String("media")));
        if (media) {
            context.push_back(media);
            for (const auto& kind : media->children) {
                const bool video = kind->tag == QLatin1String("video");
                if (!video && kind->tag != QLatin1String("audio")) continue;
                if (onlyVideo && *onlyVideo != video) continue;
                Context kindContext = context;
                kindContext.push_back(kind.get());
                for (const Node* t : kind->all(QLatin1String("track"))) {
                    t = deref(t);
                    if (!video) {
                        const QString index = t->attributes.value(QStringLiteral("currentExplodedTrackIndex"), QStringLiteral("0"));
                        const QString count = t->attributes.value(QStringLiteral("totalExplodedTrackCount"), QStringLiteral("1"));
                        if (index != QLatin1String("0") && count != QLatin1String("1")) continue;
                    }
                    tracks.append(track(*t, video, kindContext, depth));
                }
            }
        }
        QJsonObject stack = object(QStringLiteral("Stack.1"), sequence.childText(QLatin1String("name")));
        stack.insert(QStringLiteral("source_range"), QJsonValue::Null);
        stack.insert(QStringLiteral("effects"), QJsonArray{});
        stack.insert(QStringLiteral("markers"), QJsonArray{});
        stack.insert(QStringLiteral("enabled"), true);
        stack.insert(QStringLiteral("children"), tracks);
        return stack;
    }

    static bool isItem(const Node& n) {
        return n.tag == QLatin1String("clipitem") || n.tag == QLatin1String("generatoritem") || n.tag == QLatin1String("transitionitem");
    }

    // A transition's cut point in the track: its start, its end, or between them (<alignment>).
    std::optional<double> cutPoint(const Node* transition) {
        if (!transition) return std::nullopt;
        const auto start = number(transition->childText(QLatin1String("start")));
        const auto end = number(transition->childText(QLatin1String("end")));
        if (!start || !end) return std::nullopt;
        const QString alignment = transition->childText(QLatin1String("alignment"));
        if (alignment == QLatin1String("end") || alignment == QLatin1String("end-black")) return *end;
        if (alignment == QLatin1String("start") || alignment == QLatin1String("start-black")) return *start;
        return std::floor((*start + *end) / 2.0);
    }

    // A track's items laid end to end, with gaps where an item starts after the one before ends.
    QJsonObject track(const Node& t, bool video, const Context& parent, int depth) {
        Context context = parent;
        context.push_back(&t);
        const double rate = rateIn(context);
        QJsonArray children;
        double at = 0.0;
        const Node* head = nullptr; // the transition before the item
        for (std::size_t i = 0; i < t.children.size(); ++i) {
            const Node* item = deref(t.children[i].get());
            if (!isItem(*item)) continue;
            const Node* tail = nullptr; // the transition right after it
            if (i + 1 < t.children.size() && t.children[i + 1]->tag == QLatin1String("transitionitem")) tail = deref(t.children[i + 1].get());
            if (item->tag == QLatin1String("transitionitem")) {
                if (const auto o = transition(*item, rate)) children.append(*o);
                head = item;
                continue;
            }
            auto start = number(item->childText(QLatin1String("start")));
            auto end = number(item->childText(QLatin1String("end")));
            double startOffset = 0.0; // how far into the clip the cut is, after a transition
            if (!start || *start < 0.0) {
                const auto cut = cutPoint(head);
                if (cut) {
                    startOffset = *cut - number(head->childText(QLatin1String("start"))).value_or(*cut);
                } else if (start) {
                    notes_.add(QStringLiteral("items starting at -1 with no transition before them placed after the item before"));
                }
                start = cut.value_or(at);
            }
            if (!end || *end < 0.0) {
                const auto cut = cutPoint(tail);
                if (!cut) {
                    const auto in = number(item->childText(QLatin1String("in")));
                    const auto out = number(item->childText(QLatin1String("out")));
                    if (end) notes_.add(QStringLiteral("items ending at -1 with no transition after them given their in-to-out length"));
                    end = *start + std::max(0.0, out.value_or(0.0) - in.value_or(0.0));
                } else {
                    end = *cut;
                }
            }
            if (*end <= *start) {
                notes_.add(QStringLiteral("items without length left out"));
                continue;
            }
            if (*start > at) {
                QJsonObject gap = object(QStringLiteral("Gap.1"), QString());
                gap.insert(QStringLiteral("source_range"), timeRange(0.0, rate, *start - at, rate));
                children.append(gap);
            } else if (*start < at - 0.5) {
                notes_.add(QStringLiteral("overlapping items placed one after another"));
            }
            children.append(clip(*item, *end - *start, startOffset, context, depth, video));
            at = std::max(at, *end);
        }
        QJsonObject o = object(QStringLiteral("Track.1"), t.childText(QLatin1String("name")));
        o.insert(QStringLiteral("kind"), video ? QStringLiteral("Video") : QStringLiteral("Audio"));
        o.insert(QStringLiteral("source_range"), QJsonValue::Null);
        o.insert(QStringLiteral("effects"), QJsonArray{});
        o.insert(QStringLiteral("markers"), QJsonArray{});
        const QString enabled = t.childText(QLatin1String("enabled"));
        o.insert(QStringLiteral("enabled"), enabled.isEmpty() || truth(enabled));
        o.insert(QStringLiteral("children"), children);
        return o;
    }

    std::optional<QJsonObject> transition(const Node& item, double rate) {
        const auto start = number(item.childText(QLatin1String("start")));
        const auto end = number(item.childText(QLatin1String("end")));
        const auto cut = cutPoint(&item);
        if (!start || !end || !cut || *end < *start) {
            notes_.add(QStringLiteral("transitions without a start and end left out"));
            return std::nullopt;
        }
        const Node* effect = item.child(QLatin1String("effect"));
        const QString name = effect ? effect->childText(QLatin1String("name")) : QString();
        const QString id = effect ? effect->childText(QLatin1String("effectid")) : QString();
        const bool dissolve = id.contains(QLatin1String("dissolve"), Qt::CaseInsensitive) ||
                              name.contains(QLatin1String("dissolve"), Qt::CaseInsensitive) || (id.isEmpty() && name.isEmpty());
        QJsonObject o = object(QStringLiteral("Transition.1"), name);
        o.insert(QStringLiteral("transition_type"), dissolve ? QStringLiteral("SMPTE_Dissolve") : QStringLiteral("Custom_Transition"));
        if (!dissolve) o.insert(QStringLiteral("name"), name.isEmpty() ? id : name);
        o.insert(QStringLiteral("in_offset"), rationalTime(*cut - *start, rate));
        o.insert(QStringLiteral("out_offset"), rationalTime(*end - *cut, rate));
        return o;
    }

    // A clip item: its media (a file, a generator, a nested sequence) and its source range.
    QJsonObject clip(const Node& item, double length, double startOffset, const Context& parent, int depth, bool video) {
        const double trackRate = rateIn(parent);
        Context context = parent;
        context.push_back(&item);
        const double rate = rateIn(context);
        const QString name = item.childText(QLatin1String("name"));
        const double in = number(item.childText(QLatin1String("in"))).value_or(0.0);
        // A nested sequence: a stack of its tracks.
        if (const Node* nested = deref(item.child(QLatin1String("sequence")))) {
            if (depth >= kMaxNesting) {
                notes_.add(QStringLiteral("sequences nested too deep read as gaps"));
                QJsonObject gap = object(QStringLiteral("Gap.1"), QString());
                gap.insert(QStringLiteral("source_range"), timeRange(0.0, trackRate, length, trackRate));
                return gap;
            }
            QJsonObject stack = stackOf(*nested, context, depth + 1, video);
            stack.insert(QStringLiteral("name"), name.isEmpty() ? nested->childText(QLatin1String("name")) : name);
            stack.insert(QStringLiteral("source_range"), timeRange(in + startOffset, rate, length, trackRate));
            return stack;
        }
        QJsonObject reference;
        double mediaStart = 0.0;
        if (item.tag == QLatin1String("generatoritem")) {
            const Node* effect = item.child(QLatin1String("effect"));
            reference = object(QStringLiteral("GeneratorReference.1"), effect ? effect->childText(QLatin1String("name")) : name);
            reference.insert(QStringLiteral("generator_kind"), effect ? effect->childText(QLatin1String("effectid")) : QString());
            reference.insert(QStringLiteral("parameters"), QJsonObject{});
            reference.insert(QStringLiteral("available_range"), QJsonValue::Null);
        } else if (const Node* file = deref(item.child(QLatin1String("file")))) {
            Context fileContext = context;
            fileContext.push_back(file);
            const double fileRate = rateIn(fileContext);
            mediaStart = timecodeFrame(file->child(QLatin1String("timecode")), fileContext).value_or(0.0);
            const QString path = file->childText(QLatin1String("pathurl"));
            const QString source = file->childText(QLatin1String("mediaSource"));
            const QString fileName = file->childText(QLatin1String("name"));
            if (!path.isEmpty()) {
                reference = object(QStringLiteral("ExternalReference.1"), fileName);
                reference.insert(QStringLiteral("target_url"), path); // "file://localhost/": see otio.cpp's media key
            } else if (!source.isEmpty()) {
                reference = object(QStringLiteral("GeneratorReference.1"), fileName);
                reference.insert(QStringLiteral("generator_kind"), source);
                reference.insert(QStringLiteral("parameters"), QJsonObject{});
            } else {
                reference = object(QStringLiteral("MissingReference.1"), fileName.isEmpty() ? name : fileName);
            }
            if (const auto duration = number(file->childText(QLatin1String("duration")))) {
                reference.insert(QStringLiteral("available_range"), timeRange(mediaStart, fileRate, std::max(0.0, *duration), fileRate));
            } else {
                reference.insert(QStringLiteral("available_range"), timeRange(mediaStart, fileRate, 0.0, fileRate));
            }
            // The clip's frames are counted at its rate; the media's start at the file's.
            if (fileRate != rate && fileRate > 0.0) mediaStart = mediaStart * rate / fileRate;
        } else {
            notes_.add(QStringLiteral("clip items without a file read as clips of missing media"));
            reference = object(QStringLiteral("MissingReference.1"), name);
            reference.insert(QStringLiteral("available_range"), QJsonValue::Null);
        }
        QJsonObject o = object(QStringLiteral("Clip.2"), name);
        o.insert(QStringLiteral("source_range"), timeRange(in + mediaStart + startOffset, rate, length, trackRate));
        o.insert(QStringLiteral("media_references"), QJsonObject{{QStringLiteral("DEFAULT_MEDIA"), reference}});
        o.insert(QStringLiteral("active_media_reference_key"), QStringLiteral("DEFAULT_MEDIA"));
        o.insert(QStringLiteral("effects"), effectsOf(item));
        // A clip's markers are frames of its media: in the source range's terms.
        o.insert(QStringLiteral("markers"), markersOf(item, context, mediaStart));
        const QString enabled = item.childText(QLatin1String("enabled"));
        o.insert(QStringLiteral("enabled"), enabled.isEmpty() || truth(enabled));
        return o;
    }

    // <filter>s: Time Remap's speed (a percentage; negative when reversed) as a LinearTimeWarp, a
    // speed of 0 as a FreezeFrame; other effects by name, which the import lists as not read.
    QJsonArray effectsOf(const Node& item) {
        QJsonArray out;
        for (const Node* filter : item.all(QLatin1String("filter"))) {
            const Node* effect = deref(filter->child(QLatin1String("effect")));
            if (!effect) continue;
            const QString id = effect->childText(QLatin1String("effectid"));
            const QString name = effect->childText(QLatin1String("name"));
            if (id.compare(QLatin1String("timeremap"), Qt::CaseInsensitive) == 0) {
                std::optional<double> speed;
                bool reverse = false;
                for (const Node* p : effect->all(QLatin1String("parameter"))) {
                    const QString pid = p->childText(QLatin1String("parameterid"));
                    if (pid == QLatin1String("speed")) speed = number(p->childText(QLatin1String("value")));
                    if (pid == QLatin1String("reverse")) reverse = truth(p->childText(QLatin1String("value")));
                }
                if (!speed) continue;
                const double scalar = (reverse ? -1.0 : 1.0) * *speed / 100.0;
                if (scalar == 1.0) continue;
                QJsonObject e = object(scalar == 0.0 ? QStringLiteral("FreezeFrame.1") : QStringLiteral("LinearTimeWarp.1"), QString());
                e.insert(QStringLiteral("effect_name"), scalar == 0.0 ? QStringLiteral("FreezeFrame") : QStringLiteral("LinearTimeWarp"));
                e.insert(QStringLiteral("time_scalar"), scalar);
                out.append(e);
                continue;
            }
            QJsonObject e = object(QStringLiteral("Effect.1"), name);
            e.insert(QStringLiteral("effect_name"), name.isEmpty() ? id : name);
            out.append(e);
        }
        return out;
    }

    // <marker>s: <in> and <out> frames (an <out> of -1 or not past the in: a marker of one frame),
    // counted from `origin`.
    QJsonArray markersOf(const Node& element, const Context& context, double origin) {
        QJsonArray out;
        const double rate = rateIn(context);
        for (const Node* m : element.all(QLatin1String("marker"))) {
            const auto in = number(m->childText(QLatin1String("in")));
            if (!in) {
                notes_.add(QStringLiteral("markers without an in left out"));
                continue;
            }
            const double outFrame = number(m->childText(QLatin1String("out"))).value_or(-1.0);
            QJsonObject o = object(QStringLiteral("Marker.2"), m->childText(QLatin1String("name")));
            o.insert(QStringLiteral("comment"), m->childText(QLatin1String("comment")));
            o.insert(QStringLiteral("color"), QStringLiteral("RED"));
            o.insert(QStringLiteral("marked_range"), timeRange(*in + origin, rate, outFrame > *in ? outFrame - *in : 0.0, rate));
            out.append(o);
        }
        return out;
    }

    const Node& root_;
    QHash<QString, const Node*> ids_;
    Notes notes_;
};

// ---- Writing

// A frame count of an OTIO time at `rate` frames a second.
std::int64_t framesOf(const QJsonValue& v, double rate) {
    const QJsonObject o = v.toObject();
    const double value = o.value(QStringLiteral("value")).toDouble();
    const double r = o.value(QStringLiteral("rate")).toDouble(rate);
    if (!std::isfinite(value) || !std::isfinite(r) || r <= 0.0) return 0;
    return detail::clampFrames(std::llround(value * rate / r));
}

class Writer {
public:
    explicit Writer(Notes& notes) : notes_(notes) {}

    QByteArray write(const QJsonObject& timeline) {
        QByteArray out;
        QXmlStreamWriter w(&out);
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(2);
        w.writeStartDocument();
        w.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
        w.writeStartElement(QStringLiteral("xmeml"));
        w.writeAttribute(QStringLiteral("version"), QStringLiteral("5"));
        sequence(w, timeline);
        w.writeEndElement();
        w.writeEndDocument();
        return out;
    }

private:
    void rate(QXmlStreamWriter& w, double fps) {
        w.writeStartElement(QStringLiteral("rate"));
        const double base = std::ceil(fps - 1.0e-6);
        w.writeTextElement(QStringLiteral("timebase"), QString::number(static_cast<std::int64_t>(base)));
        w.writeTextElement(QStringLiteral("ntsc"), std::abs(base - fps) > 1.0e-6 ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
        w.writeEndElement();
    }
    void timecode(QXmlStreamWriter& w, std::int64_t frame, double fps) {
        w.writeStartElement(QStringLiteral("timecode"));
        rate(w, fps);
        const Rational r = ratRate(fps);
        w.writeTextElement(QStringLiteral("string"), detail::timecodeText(frame, r, false));
        w.writeTextElement(QStringLiteral("frame"), QString::number(frame));
        w.writeTextElement(QStringLiteral("displayformat"), QStringLiteral("NDF"));
        w.writeEndElement();
    }
    static Rational ratRate(double fps) {
        const double whole = std::round(fps);
        if (std::abs(fps - whole) < 1.0e-6) return Rational(static_cast<std::int64_t>(whole));
        return detail::exactNtscRate(Rational(static_cast<std::int64_t>(std::llround(fps * 1.0e6)), 1000000));
    }
    void marker(QXmlStreamWriter& w, const QJsonObject& m, std::int64_t origin) {
        const QJsonObject range = m.value(QStringLiteral("marked_range")).toObject();
        const std::int64_t in = framesOf(range.value(QStringLiteral("start_time")), rate_) - origin;
        const std::int64_t length = framesOf(range.value(QStringLiteral("duration")), rate_);
        w.writeStartElement(QStringLiteral("marker"));
        w.writeTextElement(QStringLiteral("name"), m.value(QStringLiteral("name")).toString());
        w.writeTextElement(QStringLiteral("comment"), m.value(QStringLiteral("comment")).toString());
        w.writeTextElement(QStringLiteral("in"), QString::number(in));
        w.writeTextElement(QStringLiteral("out"), length > 1 ? QString::number(in + length) : QStringLiteral("-1"));
        w.writeEndElement();
        if (m.value(QStringLiteral("color")).toString() != QLatin1String("RED")) notes_.add(QStringLiteral("marker colours left out"));
    }

    void sequence(QXmlStreamWriter& w, const QJsonObject& timeline) {
        const QJsonObject start = timeline.value(QStringLiteral("global_start_time")).toObject();
        rate_ = start.value(QStringLiteral("rate")).toDouble(24.0);
        if (!std::isfinite(rate_) || rate_ <= 0.0) rate_ = 24.0;
        const std::int64_t startFrame = framesOf(start, rate_);
        const QJsonObject stack = timeline.value(QStringLiteral("tracks")).toObject();
        const QJsonArray tracks = stack.value(QStringLiteral("children")).toArray();
        std::int64_t duration = 0;
        for (const QJsonValue& t : tracks) duration = std::max(duration, lengthOf(t.toObject()));
        w.writeStartElement(QStringLiteral("sequence"));
        w.writeAttribute(QStringLiteral("id"), QStringLiteral("sequence-1"));
        w.writeTextElement(QStringLiteral("name"), timeline.value(QStringLiteral("name")).toString());
        w.writeTextElement(QStringLiteral("duration"), QString::number(duration));
        rate(w, rate_);
        timecode(w, startFrame, rate_);
        w.writeStartElement(QStringLiteral("media"));
        for (const bool video : {true, false}) {
            w.writeStartElement(video ? QStringLiteral("video") : QStringLiteral("audio"));
            for (const QJsonValue& t : tracks) {
                const QJsonObject o = t.toObject();
                if ((o.value(QStringLiteral("kind")).toString() == QLatin1String("Video")) == video) track(w, o, video);
            }
            w.writeEndElement();
        }
        w.writeEndElement(); // media
        for (const QJsonValue& m : stack.value(QStringLiteral("markers")).toArray()) marker(w, m.toObject(), 0);
        w.writeEndElement(); // sequence
    }

    // Where a track's last clip ends: xmeml has no gaps, so one after the last clip is not kept.
    std::int64_t lengthOf(const QJsonObject& track) const {
        std::int64_t n = 0;
        std::int64_t end = 0;
        for (const QJsonValue& c : track.value(QStringLiteral("children")).toArray()) {
            const QJsonObject o = c.toObject();
            const QString schema = o.value(QStringLiteral("OTIO_SCHEMA")).toString();
            if (schema.startsWith(QLatin1String("Transition"))) continue;
            n = detail::saturatingAdd(n, framesOf(o.value(QStringLiteral("source_range")).toObject().value(QStringLiteral("duration")), rate_));
            if (schema.startsWith(QLatin1String("Clip"))) end = n;
        }
        return end;
    }

    // One track: clips at their places, gaps left empty, a transition where two items meet. A clip
    // beside a transition starts or ends at -1, and its in or out covers the transition's half on its
    // side, as FCP writes them.
    void track(QXmlStreamWriter& w, const QJsonObject& t, bool video) {
        ++trackNumber_[video ? 0 : 1];
        w.writeStartElement(QStringLiteral("track"));
        const QJsonArray items = t.value(QStringLiteral("children")).toArray();
        // Where each item starts, and the transitions' cuts.
        std::vector<std::int64_t> at(static_cast<std::size_t>(items.size()), 0);
        std::int64_t pos = 0;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const QJsonObject o = items[i].toObject();
            at[static_cast<std::size_t>(i)] = pos;
            if (o.value(QStringLiteral("OTIO_SCHEMA")).toString().startsWith(QLatin1String("Transition"))) continue;
            pos = detail::saturatingAdd(pos, framesOf(o.value(QStringLiteral("source_range")).toObject().value(QStringLiteral("duration")), rate_));
        }
        auto schemaAt = [&](qsizetype i) {
            return i >= 0 && i < items.size() ? items[i].toObject().value(QStringLiteral("OTIO_SCHEMA")).toString() : QString();
        };
        // A transition is kept between two clips; beside a gap (a fade to or from black) xmeml would
        // hold it next to nothing, which reads back as a transition at the track's end.
        auto isClip = [&](qsizetype i) { return schemaAt(i).startsWith(QLatin1String("Clip")); };
        auto isTransition = [&](qsizetype i) { return schemaAt(i).startsWith(QLatin1String("Transition")) && isClip(i - 1) && isClip(i + 1); };
        for (qsizetype i = 0; i < items.size(); ++i) {
            const QJsonObject o = items[i].toObject();
            const QString schema = schemaAt(i);
            const std::int64_t start = at[static_cast<std::size_t>(i)];
            if (schema.startsWith(QLatin1String("Gap"))) continue;
            if (schema.startsWith(QLatin1String("Transition"))) {
                if (isTransition(i)) {
                    transitionItem(w, o, start, video);
                } else {
                    notes_.add(QStringLiteral("transitions beside a gap left out"));
                }
                continue;
            }
            if (!schema.startsWith(QLatin1String("Clip"))) {
                notes_.add(QStringLiteral("%1 items left out").arg(schema));
                continue;
            }
            const std::int64_t length = framesOf(o.value(QStringLiteral("source_range")).toObject().value(QStringLiteral("duration")), rate_);
            std::optional<std::int64_t> headIn;
            std::optional<std::int64_t> tailOut;
            if (isTransition(i - 1)) headIn = offsets(items[i - 1].toObject()).first;
            if (isTransition(i + 1)) tailOut = offsets(items[i + 1].toObject()).second;
            clipItem(w, o, start, length, headIn, tailOut, video);
        }
        w.writeTextElement(QStringLiteral("enabled"), QStringLiteral("TRUE"));
        w.writeTextElement(QStringLiteral("locked"), QStringLiteral("FALSE"));
        w.writeEndElement();
    }

    // A transition's frames before and after its cut, centred (FCP has no other alignment between two
    // clips): the cut stays where it is.
    std::pair<std::int64_t, std::int64_t> offsets(const QJsonObject& t) {
        std::int64_t in = framesOf(t.value(QStringLiteral("in_offset")), rate_);
        std::int64_t out = framesOf(t.value(QStringLiteral("out_offset")), rate_);
        in = std::max<std::int64_t>(0, in);
        out = std::max<std::int64_t>(0, out);
        if (std::abs(in - out) > 1 || in > out) {
            const std::int64_t length = in + out;
            in = length / 2;
            out = length - in;
            notes_.add(QStringLiteral("transitions not centred on their cut written centred"));
        }
        return {in, out};
    }

    void transitionItem(QXmlStreamWriter& w, const QJsonObject& t, std::int64_t cut, bool video) {
        const auto [in, out] = offsets(t);
        const bool dissolve = t.value(QStringLiteral("transition_type")).toString() == QLatin1String("SMPTE_Dissolve");
        if (!dissolve) notes_.add(QStringLiteral("transitions other than dissolves written as Cross Dissolve"));
        w.writeStartElement(QStringLiteral("transitionitem"));
        w.writeTextElement(QStringLiteral("start"), QString::number(cut - in));
        w.writeTextElement(QStringLiteral("end"), QString::number(cut + out));
        w.writeTextElement(QStringLiteral("alignment"), QStringLiteral("center"));
        rate(w, rate_);
        w.writeStartElement(QStringLiteral("effect"));
        w.writeTextElement(QStringLiteral("name"), video ? QStringLiteral("Cross Dissolve") : QStringLiteral("Cross Fade (+3dB)"));
        w.writeTextElement(QStringLiteral("effectid"), video ? QStringLiteral("Cross Dissolve") : QStringLiteral("KGAudioTransCrossFade3dB"));
        w.writeTextElement(QStringLiteral("effectcategory"), QStringLiteral("Dissolve"));
        w.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
        w.writeTextElement(QStringLiteral("mediatype"), video ? QStringLiteral("video") : QStringLiteral("audio"));
        w.writeEndElement();
        w.writeEndElement();
    }

    void clipItem(QXmlStreamWriter& w, const QJsonObject& c, std::int64_t start, std::int64_t length, std::optional<std::int64_t> headIn,
                  std::optional<std::int64_t> tailOut, bool video) {
        const QJsonObject refs = c.value(QStringLiteral("media_references")).toObject();
        const QJsonObject ref = refs.value(c.value(QStringLiteral("active_media_reference_key")).toString(QStringLiteral("DEFAULT_MEDIA"))).toObject();
        const QJsonObject available = ref.value(QStringLiteral("available_range")).toObject();
        const std::int64_t mediaStart = available.isEmpty() ? 0 : framesOf(available.value(QStringLiteral("start_time")), rate_);
        const std::int64_t mediaLength = available.isEmpty() ? 0 : framesOf(available.value(QStringLiteral("duration")), rate_);
        const std::int64_t sourceStart = framesOf(c.value(QStringLiteral("source_range")).toObject().value(QStringLiteral("start_time")), rate_);
        std::int64_t in = sourceStart - mediaStart;
        std::int64_t out = in + length;
        if (headIn) in -= *headIn;
        if (tailOut) out += *tailOut;
        w.writeStartElement(QStringLiteral("clipitem"));
        w.writeAttribute(QStringLiteral("id"), QStringLiteral("clipitem-%1").arg(++clipItems_));
        w.writeTextElement(QStringLiteral("name"), c.value(QStringLiteral("name")).toString());
        w.writeTextElement(QStringLiteral("enabled"), QStringLiteral("TRUE"));
        w.writeTextElement(QStringLiteral("duration"), QString::number(std::max(mediaLength, out)));
        rate(w, rate_);
        w.writeTextElement(QStringLiteral("start"), headIn ? QStringLiteral("-1") : QString::number(start));
        w.writeTextElement(QStringLiteral("end"), tailOut ? QStringLiteral("-1") : QString::number(start + length));
        w.writeTextElement(QStringLiteral("in"), QString::number(in));
        w.writeTextElement(QStringLiteral("out"), QString::number(out));
        file(w, ref, mediaStart, std::max(mediaLength, out), video);
        if (!video) {
            w.writeStartElement(QStringLiteral("sourcetrack"));
            w.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("audio"));
            w.writeTextElement(QStringLiteral("trackindex"), QString::number(trackNumber_[1]));
            w.writeEndElement();
        }
        for (const QJsonValue& e : c.value(QStringLiteral("effects")).toArray()) speed(w, e.toObject());
        for (const QJsonValue& m : c.value(QStringLiteral("markers")).toArray()) marker(w, m.toObject(), mediaStart);
        w.writeEndElement();
    }

    // The media, in full the first time, then by its id alone.
    void file(QXmlStreamWriter& w, const QJsonObject& ref, std::int64_t mediaStart, std::int64_t mediaLength, bool video) {
        const QString schema = ref.value(QStringLiteral("OTIO_SCHEMA")).toString();
        const QString url = ref.value(QStringLiteral("target_url")).toString();
        const QString name = ref.value(QStringLiteral("name")).toString();
        // Media is one file by its path and its start: files of one path but different starts are
        // different media (as the import reads them, otio.cpp's media key).
        const QString key = (url.isEmpty() ? QStringLiteral("name:") + name : url) + QStringLiteral("|%1").arg(mediaStart);
        const auto known = files_.find(key);
        w.writeStartElement(QStringLiteral("file"));
        if (known != files_.end()) {
            w.writeAttribute(QStringLiteral("id"), known.value());
            w.writeEndElement();
            return;
        }
        const QString id = QStringLiteral("file-%1").arg(files_.size() + 1);
        files_.insert(key, id);
        w.writeAttribute(QStringLiteral("id"), id);
        w.writeTextElement(QStringLiteral("name"), !name.isEmpty() ? name : QFileInfo(QUrl(url).path()).fileName());
        if (schema.startsWith(QLatin1String("ExternalReference")) && !url.isEmpty()) w.writeTextElement(QStringLiteral("pathurl"), url);
        rate(w, rate_);
        w.writeTextElement(QStringLiteral("duration"), QString::number(mediaLength));
        timecode(w, mediaStart, rate_);
        w.writeStartElement(QStringLiteral("media"));
        w.writeEmptyElement(video ? QStringLiteral("video") : QStringLiteral("audio"));
        w.writeEndElement();
        w.writeEndElement();
    }

    // A speed change as FCP's Time Remap filter: the speed as a percentage, reverse for a negative one.
    void speed(QXmlStreamWriter& w, const QJsonObject& e) {
        const QString schema = e.value(QStringLiteral("OTIO_SCHEMA")).toString();
        double scalar = 1.0;
        if (schema.startsWith(QLatin1String("LinearTimeWarp"))) {
            scalar = e.value(QStringLiteral("time_scalar")).toDouble(1.0);
        } else if (schema.startsWith(QLatin1String("FreezeFrame"))) {
            scalar = 0.0;
        } else {
            notes_.add(QStringLiteral("effects other than speed changes left out"));
            return;
        }
        w.writeStartElement(QStringLiteral("filter"));
        w.writeStartElement(QStringLiteral("effect"));
        w.writeTextElement(QStringLiteral("name"), QStringLiteral("Time Remap"));
        w.writeTextElement(QStringLiteral("effectid"), QStringLiteral("timeremap"));
        w.writeTextElement(QStringLiteral("effectcategory"), QStringLiteral("motion"));
        w.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("motion"));
        w.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("video"));
        for (const auto& [id, value] : {std::pair{QStringLiteral("variablespeed"), QStringLiteral("0")},
                                        std::pair{QStringLiteral("speed"), QString::number(std::abs(scalar) * 100.0, 'g', 10)},
                                        std::pair{QStringLiteral("reverse"), scalar < 0.0 ? QStringLiteral("TRUE") : QStringLiteral("FALSE")}}) {
            w.writeStartElement(QStringLiteral("parameter"));
            w.writeTextElement(QStringLiteral("parameterid"), id);
            w.writeTextElement(QStringLiteral("name"), id);
            w.writeTextElement(QStringLiteral("value"), value);
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
    }

    Notes& notes_;
    double rate_ = 24.0;
    int trackNumber_[2] = {0, 0};
    int clipItems_ = 0;
    QHash<QString, QString> files_; // media key -> file id
};

} // namespace

bool isFcpXml(const QByteArray& data) {
    QXmlStreamReader reader(data);
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement) return reader.name() == QLatin1String("xmeml");
    }
    return false;
}

bool isFcpXmlFile(const QString& path) {
    if (!path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) return false;
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && isFcpXml(f.read(4096));
}

std::optional<QByteArray> toOtioJson(const QByteArray& xml, QString* error, QStringList* warnings) {
    const auto root = parse(xml, error);
    if (!root) return std::nullopt;
    if (root->tag != QLatin1String("xmeml")) {
        if (error) *error = QStringLiteral("not an FCP XML file (the root element is <%1>, not <xmeml>)").arg(root->tag);
        return std::nullopt;
    }
    Reader reader(*root);
    const auto otio = reader.read();
    if (warnings) *warnings = reader.warnings();
    if (!otio) {
        if (error) *error = QStringLiteral("no sequence in it");
        return std::nullopt;
    }
    return QJsonDocument(*otio).toJson(QJsonDocument::Compact);
}

std::optional<otio::ImportResult> importXml(const QByteArray& xml, const otio::ImportOptions& options, QString* error) {
    QStringList warnings;
    const auto json = toOtioJson(xml, error, &warnings);
    if (!json) return std::nullopt;
    auto result = otio::importJson(*json, options, error);
    if (result) result->warnings = warnings + result->warnings;
    return result;
}

std::optional<otio::ImportResult> importFile(const QString& path, const otio::ImportOptions& options, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return std::nullopt;
    }
    otio::ImportOptions o = options;
    if (o.name.isEmpty()) o.name = QFileInfo(path).completeBaseName();
    return importXml(f.readAll(), o, error);
}

otio::ExportResult exportXml(const model::BinItem& sequence, const otio::ExportOptions& options) {
    otio::ExportResult result = otio::exportJson(sequence, options);
    if (!result.ok) return result;
    const QJsonObject timeline = QJsonDocument::fromJson(result.json).object();
    Notes notes;
    Writer writer(notes);
    result.json = writer.write(timeline);
    // The OTIO export's own notes, then what xmeml cannot hold of the timeline.
    result.warnings += notes.list();
    return result;
}

otio::ExportResult exportFile(const model::BinItem& sequence, const QString& path, const otio::ExportOptions& options) {
    otio::ExportResult result = exportXml(sequence, options);
    if (!result.ok) return result;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(result.json) != result.json.size() || !f.commit()) {
        result.ok = false;
        result.error = f.errorString();
    }
    return result;
}

} // namespace lumenis::interop::fcpxml
