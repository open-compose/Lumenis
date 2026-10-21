#include "lumenis/interop/avb_import.h"

#include "import_common.h"

#include "lumenis/model/sequence.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>

// Media Composer bins into bins of the native model (docs/spec/avb.md, "Import"). avb.cpp reads the
// bin's objects with pyavb's names (pyavb, MIT, Mark Reid, is the reference for what they mean); this
// file reads what they say. The bin (ABIN) lists its mobs (CMPO), each with a user-placed flag:
// master mobs become master or audio clips with their source chains (the file source mobs, then the
// tape or import source mob before them), composition mobs used as subclips become subclips, and
// other composition mobs become sequences, whose nested track groups (effects, essence groups),
// transitions and filler are flattened into tracks of segments. Source mobs are not items: they are
// the clips' sources. The rules follow aaf_import.cpp, which reads the same mobs from an AAF export,
// and share its helpers (import_common.h). ctest avb_import_bins compares what this makes with the
// reference dumps of the corpus bins (tests/data/aaf_reference, from scripts/avb-structure.py);
// tests/interop/tst_avb.cpp (importsBin) checks what the dumps do not show.
namespace lumenis::interop::avb {

namespace {

using detail::exactNtscRate;
using detail::SourceTimecode;
using detail::startAtRate;
using detail::timecodeText;
using detail::trackList;

// A component's media kind (its media_kind_id, as pyavb names them).
enum class Kind { None, Picture, Sound, Timecode, Edgecode, Other };

Kind kindOf(const Object& c) {
    switch (c.property(QStringLiteral("media_kind_id")).toInt()) {
    case 0: return Kind::None;
    case 1: return Kind::Picture;
    case 2: return Kind::Sound;
    case 3: return Kind::Timecode;
    case 4: return Kind::Edgecode;
    default: return Kind::Other;
    }
}

bool is(const Object* o, const char* classId) { return o && o->classId == QLatin1String(classId); }

// An edit rate as bins store it (a double, 23.976 for 24000/1001) as a Rational: whole rates as they
// are, others to a thousandth and NTSC rates exact (exactNtscRate). `fallback` when it is not a
// positive rate.
Rational rateOf(double r, const Rational& fallback) {
    if (!std::isfinite(r) || r <= 0 || r > 1e9) return fallback;
    const double whole = std::round(r);
    if (std::abs(r - whole) < 1e-9) return Rational(static_cast<std::int64_t>(whole));
    const auto thousandths = static_cast<std::int64_t>(std::llround(r * 1000));
    if (thousandths <= 0) return fallback;
    return exactNtscRate(Rational(thousandths, 1000));
}

// Descriptors of media files (pyavb's MediaFileDescriptor and its subclasses): the mob is a file
// source. A plain MDES is an import source (the file a clip was imported or linked from), MDTP a
// tape.
bool isFileDescriptor(const QString& cls) {
    static const QSet<QString> files{QStringLiteral("MDFL"), QStringLiteral("MULD"), QStringLiteral("WAVE"),
                                     QStringLiteral("AIFC"), QStringLiteral("PCMA"), QStringLiteral("MPGA"),
                                     QStringLiteral("DIDD"), QStringLiteral("CDCI"), QStringLiteral("MPGI"),
                                     QStringLiteral("JPED"), QStringLiteral("RGBA"), QStringLiteral("DATD"),
                                     QStringLiteral("ANCD")};
    return files.contains(cls);
}

// A composition mob's usage code as pyavb names it, for warnings.
QString usageName(qint64 code) {
    switch (code) {
    case 1: return QStringLiteral("precompute");
    case 3: return QStringLiteral("effect");
    case 4: return QStringLiteral("group");
    case 5: return QStringLiteral("groupoofter");
    case 6: return QStringLiteral("motion");
    case 7: return QStringLiteral("mastermob");
    case 9: return QStringLiteral("precompute_file");
    default: return QStringLiteral("usage %1").arg(code);
    }
}

// No source: a null MobID, or one whose material number is all zeros (as a source chain ends, and
// as scripts/avb-structure.py takes it in sequences).
bool noSource(const MobId& id) {
    const auto& b = id.bytes();
    return std::all_of(b.begin() + 16, b.end(), [](std::uint8_t x) { return x == 0; });
}

// Reads one bin. run() first indexes the bin's mobs by MobID, then goes through them in three passes:
// master mobs, then subclips (which name their masters' items), then sequences (whose segments name
// either). The maps below carry what later passes need from earlier ones.
class Importer {
public:
    Importer(const File& file, const ImportOptions& options) : file_(file), options_(options) {}

    ImportResult run() {
        const Object& root = file_.root();
        if (root.classId != QLatin1String("ABIN") || !root.ok()) {
            note(QStringLiteral("the file's root is not a bin"));
            return finish();
        }
        for (const Value& v : root.property(QStringLiteral("items")).toList()) {
            const Object* m = deref(v.member(QStringLiteral("mob")));
            if (!is(m, "CMPO") || !m->ok()) continue;
            const MobId id = mobIdOf(*m);
            if (!id.isNull()) {
                // The same mob listed twice is one item.
                if (mobs_.count(id)) continue;
                mobs_.emplace(id, m);
            }
            const Value& placed = v.member(QStringLiteral("user_placed"));
            order_.append({m, placed.isNull() || placed.toInt() != 0});
        }
        for (const Entry& e : order_) {
            if (mobType(*e.mob) == 2) master(e);
        }
        for (const Entry& e : order_) {
            if (mobType(*e.mob) == 1 && usage(*e.mob) == 2) subclip(e);
        }
        for (const Entry& e : order_) {
            if (mobType(*e.mob) != 1) continue;
            const qint64 code = usage(*e.mob);
            if (code == 0) {
                sequence(e);
            } else if (code != 2) {
                note(QStringLiteral("composition mobs used as %1 (not imported)").arg(usageName(code)));
            }
        }
        return finish();
    }

private:
    // A mob of the bin and its bin item's user-placed flag.
    struct Entry {
        const Object* mob = nullptr;
        bool userPlaced = true;
    };

    // One picture or sound track of a mob, named as a bin track ("V1", "A2") by its label number.
    struct Track {
        QString name;
        Kind kind = Kind::Picture;
        const Object* component = nullptr;
        std::int64_t length = 0;
    };

    // Counts a warning; finish() lists each once, with its count.
    void note(const QString& what) { ++notes_[what]; }

    ImportResult finish() {
        // Objects the reader could not read (avb.cpp: a class not read yet, or damage), by class.
        std::map<QString, int> unread;
        for (std::uint32_t index : unreadable_) ++unread[file_.classIdAt(index)];
        for (const auto& [cls, count] : unread) {
            notes_[QStringLiteral("%1 objects that could not be read").arg(cls.isEmpty() ? QStringLiteral("?") : cls)] += count;
        }
        for (auto it = notes_.begin(); it != notes_.end(); ++it) {
            result_.warnings << (it->second > 1 ? QStringLiteral("%1 × %2").arg(it->second).arg(it->first) : it->first);
        }
        return result_;
    }

    // The object a reference names; null for no reference or one outside the table. An object the
    // reader could not read is returned (its class is known, its properties are empty) and counted.
    const Object* deref(const Value& v) {
        const auto ref = v.toRef();
        if (!ref || ref->index == 0 || ref->index > file_.objectCount()) return nullptr;
        const Object& o = file_.object(*ref);
        if (!o.ok()) unreadable_.insert(ref->index);
        return &o;
    }

    static MobId mobIdOf(const Object& o) { return o.property(QStringLiteral("mob_id")).toMobId().value_or(MobId()); }
    static qint64 mobType(const Object& m) { return m.property(QStringLiteral("mob_type_id")).toInt(); }
    static qint64 usage(const Object& m) { return m.property(QStringLiteral("usage_code")).toInt(); }

    // A mob of the bin by MobID; null when the bin does not hold it.
    const Object* mob(const MobId& id) const {
        const auto it = mobs_.find(id);
        return it == mobs_.end() ? nullptr : it->second;
    }
    template <typename T>
    static T valueIn(const std::map<MobId, T>& map, const MobId& id, const T& fallback = T()) {
        const auto it = map.find(id);
        return it == map.end() ? fallback : it->second;
    }

    // A component's length: a sequence's is its components' (less its transitions, which overlap
    // their neighbours), as pyavb computes it; every other component stores its own.
    std::int64_t lengthOf(const Object* c, int depth = 0) {
        if (!c || depth > 32) return 0;
        if (!is(c, "SEQU")) return c->property(QStringLiteral("length")).toInt();
        std::int64_t total = 0;
        for (const Value& v : c->property(QStringLiteral("components")).toList()) {
            const Object* x = deref(v);
            if (is(x, "TNFX")) {
                total -= x->property(QStringLiteral("length")).toInt();
            } else {
                total += lengthOf(x, depth + 1);
            }
        }
        return total;
    }

    // The first source clip in a component: itself, or down sequences and the tracks of track groups
    // (effects' inputs, essence groups' choices).
    const Object* firstClip(const Object* c, int depth = 0) {
        if (!c || depth > 16) return nullptr;
        if (is(c, "SCLP")) return c;
        for (const Value& v : c->property(QStringLiteral("components")).toList()) {
            if (const Object* s = firstClip(deref(v), depth + 1)) return s;
        }
        for (const Value& t : c->property(QStringLiteral("tracks")).toList()) {
            if (const Object* s = firstClip(deref(t.member(QStringLiteral("component"))), depth + 1)) return s;
        }
        return nullptr;
    }

    // A mob's picture and sound tracks, in the bin's order. The number is the track's label, or else
    // the count of tracks of that kind so far.
    QList<Track> tracksOf(const Object& m) {
        QList<Track> out;
        int pictures = 0;
        int sounds = 0;
        for (const Value& v : m.property(QStringLiteral("tracks")).toList()) {
            const Object* c = deref(v.member(QStringLiteral("component")));
            if (!c) continue;
            Track t;
            t.kind = kindOf(*c);
            if (t.kind != Kind::Picture && t.kind != Kind::Sound) continue;
            const int counted = t.kind == Kind::Picture ? ++pictures : ++sounds;
            const Value& label = v.member(QStringLiteral("index"));
            const auto number = label.isNull() ? counted : static_cast<int>(label.toInt());
            t.name = QStringLiteral("%1%2").arg(t.kind == Kind::Picture ? QLatin1Char('V') : QLatin1Char('A')).arg(number);
            t.component = c;
            t.length = lengthOf(c);
            out.append(t);
        }
        return out;
    }

    // The first track to time a clip by: its first picture track, else its first track.
    static const Track* firstTrack(const QList<Track>& tracks) {
        const Track* first = nullptr;
        for (const Track& t : tracks) {
            if (!first || (t.kind == Kind::Picture && first->kind != Kind::Picture)) first = &t;
        }
        return first;
    }

    // The first timecode among a mob's tracks, also as the first track of a track group (a capture
    // mask) or the first component of a sequence. Bit 0 of its flags is drop frame: it is set only at
    // 30 fps in the corpus, where one hour starts at frame 107892 (01:00:00;00).
    std::optional<SourceTimecode> timecodeOf(const Object& m) {
        for (const Value& v : m.property(QStringLiteral("tracks")).toList()) {
            const Object* c = deref(v.member(QStringLiteral("component")));
            for (int depth = 0; c && depth < 8; ++depth) {
                if (is(c, "TCCP")) {
                    return SourceTimecode{c->property(QStringLiteral("start")).toInt(),
                                          static_cast<int>(c->property(QStringLiteral("fps")).toInt()),
                                          (c->property(QStringLiteral("flags")).toInt() & 1) != 0};
                }
                const ValueList& tracks = c->property(QStringLiteral("tracks")).toList();
                const ValueList& parts = c->property(QStringLiteral("components")).toList();
                if (!tracks.empty()) {
                    c = deref(tracks.front().member(QStringLiteral("component")));
                } else if (!parts.empty()) {
                    c = deref(parts.front());
                } else {
                    break;
                }
            }
        }
        return std::nullopt;
    }

    // The component of a source mob's track that a source clip names: the track with the clip's
    // track_id as its label and the same media kind.
    const Object* trackComponent(const Object& source, qint64 label, Kind kind) {
        for (const Value& v : source.property(QStringLiteral("tracks")).toList()) {
            if (v.member(QStringLiteral("index")).toInt() != label) continue;
            const Object* c = deref(v.member(QStringLiteral("component")));
            if (c && kindOf(*c) == kind) return c;
        }
        return nullptr;
    }

    // Frames in `rate` where a clip starts on its source's timecode: down the source chain from the
    // track's first source clip to the first mob with timecode, adding up the source clips' start
    // times on the way. A timecode at another frame rate is scaled to the nominal rate of `rate`.
    std::optional<std::int64_t> startOf(const Track& track, const Rational& rate) {
        const Object* clip = firstClip(track.component);
        std::int64_t offset = 0;
        for (int depth = 0; clip && depth < 8; ++depth) {
            offset += clip->property(QStringLiteral("start_time")).toInt();
            const Object* source = mob(mobIdOf(*clip));
            if (!source) return std::nullopt;
            if (const auto tc = timecodeOf(*source)) return startAtRate(tc->start, tc->fps, rate) + offset;
            clip = firstClip(trackComponent(*source, clip->property(QStringLiteral("track_id")).toInt(), track.kind));
        }
        return std::nullopt;
    }

    // Where a file is: a file locator's path (UTF-8, else POSIX, else as stored). A media locator
    // (MSML, managed media) names its file by MobID and gives no path.
    QString locatorPath(const Object* descriptor) {
        const Object* l = descriptor ? deref(descriptor->property(QStringLiteral("locator"))) : nullptr;
        if (!is(l, "FILE") && !is(l, "WINF")) return {};
        for (const char* name : {"path_utf8", "path_posix", "path"}) {
            const QString path = l->property(QLatin1String(name)).toString();
            if (!path.isEmpty()) return path;
        }
        return {};
    }

    // What every item takes from its mob: a new item ID, the MobID, Name, Created and Modified (`when`,
    // when the mob has none), whether it was placed in the bin, and the user columns.
    void commonFields(model::BinItem& item, const Entry& e) {
        const Object& m = *e.mob;
        item.id = model::newItemId();
        item.mobId = mobIdOf(m);
        item.name = m.property(QStringLiteral("name")).toString();
        if (const auto t = m.property(QStringLiteral("creation_time")).toTime()) item.created = t->toDateTime();
        if (const auto t = m.property(QStringLiteral("last_modified")).toTime()) item.modified = t->toDateTime();
        if (!item.created.isValid()) item.created = options_.when;
        if (!item.modified.isValid()) item.modified = item.created;
        item.userPlaced = e.userPlaced;
        item.editRate = rateOf(m.property(QStringLiteral("edit_rate")).toDouble(), Rational(25));
        const Object* attributes = deref(m.property(QStringLiteral("attributes")));
        if (!is(attributes, "ATTR")) return;
        // The bin's user columns: the _USER attributes, each heading's text. An empty cell is no value.
        if (const Value* user = find(attributes->properties, QStringLiteral("_USER"))) {
            const Object* columns = deref(*user);
            if (is(columns, "ATTR")) {
                for (const Property& p : columns->properties) {
                    const auto& d = p.value.data();
                    const QString text = std::holds_alternative<QString>(d)   ? p.value.toString()
                                         : std::holds_alternative<qint64>(d) ? QString::number(p.value.toInt())
                                                                              : QString();
                    if (p.name.isEmpty() || text.isEmpty()) continue;
                    item.fields.insert(p.name, text);
                    if (!model::binHeadings().contains(p.name) && !result_.customColumns.contains(p.name)) {
                        result_.customColumns << p.name;
                    }
                }
            }
        }
        // _COLOR_R, _G and _B (16 bits each) are on one sequence of the corpus. They may be its clip
        // colour (p.264), but nothing seen confirms it: not read until a bin shows what they are.
        if (find(attributes->properties, QStringLiteral("_COLOR_R"))) note(QStringLiteral("_COLOR_R/G/B attributes not read"));
    }

    // The duration (the longest track of the first track's kind), the Tracks column, and Start and
    // End when there is a start. Lengths are in the mob's edit rate, as the bin stores them.
    void setTiming(model::BinItem& item, const QList<Track>& tracks, std::optional<std::int64_t> start, bool drop) {
        QList<int> video;
        QList<int> audio;
        for (const Track& t : tracks) (t.kind == Kind::Picture ? video : audio) << t.name.mid(1).toInt();
        const Track* first = firstTrack(tracks);
        for (const Track& t : tracks) {
            if (first && t.kind == first->kind) item.durationFrames = std::max(item.durationFrames, t.length);
        }
        if (!tracks.isEmpty()) item.fields.insert(QStringLiteral("Tracks"), trackList(video, audio));
        if (start) {
            item.fields.insert(QStringLiteral("Start"), timecodeText(*start, item.editRate, drop));
            item.fields.insert(QStringLiteral("End"), timecodeText(*start + item.durationFrames, item.editRate, drop));
        }
    }

    // A clip Media Composer made for a rendered effect: its file mobs are used as "file mob with a
    // precompute" (usage code 9, pyavb). Its bin does not show it, and it is not imported (as the
    // AAF import leaves out the same clips, marked AppCode 9 there).
    bool isPrecompute(const QList<Track>& tracks) {
        for (const Track& t : tracks) {
            const Object* clip = firstClip(t.component);
            const Object* fileMob = clip ? mob(mobIdOf(*clip)) : nullptr;
            if (fileMob && usage(*fileMob) == 9) return true;
        }
        return false;
    }

    // A master mob as a master clip, or an audio clip when it has no picture. Its source chain: each
    // track's file source mob, then the tape or import source before them.
    void master(const Entry& e) {
        const Object& m = *e.mob;
        const QList<Track> tracks = tracksOf(m);
        if (isPrecompute(tracks)) {
            note(QStringLiteral("rendered effect clips left out"));
            return;
        }
        model::BinItem item;
        commonFields(item, e);
        bool picture = false;
        for (const Track& t : tracks) picture = picture || t.kind == Kind::Picture;
        item.kind = picture || tracks.isEmpty() ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;

        QList<MobId> before;
        for (const Track& t : tracks) {
            const Object* clip = firstClip(t.component);
            const Object* fileMob = clip ? mob(mobIdOf(*clip)) : nullptr;
            if (!fileMob || mobType(*fileMob) != 3) continue;
            const Object* descriptor = deref(fileMob->property(QStringLiteral("descriptor")));
            const QString cls = descriptor ? descriptor->classId : QString();
            // A clip straight from its tape or import, with no file between.
            if (cls == QLatin1String("MDTP") || cls == QLatin1String("MDES")) {
                const MobId id = mobIdOf(*fileMob);
                if (!before.contains(id)) before << id;
                continue;
            }
            if (!isFileDescriptor(cls)) {
                note(QStringLiteral("sources described as %1 left out").arg(cls.isEmpty() ? QStringLiteral("nothing") : cls));
                continue;
            }
            model::ClipSource s;
            s.kind = model::ClipSource::Kind::File;
            s.mob = mobIdOf(*fileMob);
            s.track = t.name;
            s.name = fileMob->property(QStringLiteral("name")).toString();
            s.path = locatorPath(descriptor);
            const Object* next = firstClip(trackComponent(*fileMob, clip->property(QStringLiteral("track_id")).toInt(), t.kind));
            if (next && !noSource(mobIdOf(*next))) s.from = mobIdOf(*next);
            if (!s.from.isNull() && !before.contains(s.from)) before << s.from;
            item.sources << s;
        }
        bool drop = false;
        // The sources before the files: a Tape source (which also gives the Tape column) or an Import
        // source, each with its own start timecode.
        for (const MobId& id : before) {
            const Object* source = mob(id);
            if (!source) continue;
            const Object* descriptor = deref(source->property(QStringLiteral("descriptor")));
            model::ClipSource s;
            s.mob = id;
            s.name = source->property(QStringLiteral("name")).toString();
            if (is(descriptor, "MDTP")) {
                s.kind = model::ClipSource::Kind::Tape;
                item.fields.insert(QStringLiteral("Tape"), s.name);
            } else if (is(descriptor, "MDES")) {
                s.kind = model::ClipSource::Kind::Import;
                s.path = locatorPath(descriptor);
            } else {
                note(QStringLiteral("sources described as %1 left out").arg(descriptor ? descriptor->classId : QStringLiteral("nothing")));
                continue;
            }
            if (const auto tc = timecodeOf(*source)) {
                drop = drop || tc->drop;
                s.startTimecode = timecodeText(tc->start, Rational(tc->fps > 0 ? tc->fps : 25), tc->drop);
            }
            item.sources << s;
        }
        std::optional<std::int64_t> start;
        if (const Track* first = firstTrack(tracks)) start = startOf(*first, item.editRate);
        setTiming(item, tracks, start, drop);

        items_[item.mobId] = item.id;
        names_[item.mobId] = item.name;
        tapes_[item.mobId] = item.fields.value(QStringLiteral("Tape"));
        if (start) starts_[item.mobId] = *start;
        result_.items << item;
    }

    // A composition mob used as a subclip (usage code 2): a Clip source naming its master (the first
    // track's source clip) and where in it the subclip starts. Start is the master's plus that offset.
    void subclip(const Entry& e) {
        model::BinItem item;
        commonFields(item, e);
        item.kind = model::BinItemKind::Subclip;
        const QList<Track> tracks = tracksOf(*e.mob);
        std::optional<std::int64_t> start;
        if (!tracks.isEmpty()) {
            if (const Object* clip = firstClip(tracks.first().component)) {
                const MobId master = mobIdOf(*clip);
                const Object* masterMob = mob(master);
                model::ClipSource s;
                s.kind = model::ClipSource::Kind::Clip;
                s.mob = master;
                s.name = valueIn(names_, master, masterMob ? masterMob->property(QStringLiteral("name")).toString() : QString());
                s.offset = clip->property(QStringLiteral("start_time")).toInt();
                // A subclip shows its master's tape (p.407).
                const QString tape = valueIn(tapes_, master);
                if (!tape.isEmpty()) item.fields.insert(QStringLiteral("Tape"), tape);
                if (starts_.count(master)) {
                    start = starts_.at(master) + s.offset;
                    s.startTimecode = timecodeText(*start, item.editRate, false);
                }
                item.sources << s;
            }
        }
        setTiming(item, tracks, start, false);
        items_[item.mobId] = item.id;
        names_[item.mobId] = item.name;
        result_.items << item;
    }

    // ---- sequences ----

    // Turns one track's components into the native model's segments. Lengths are in the sequence's
    // edit rate, as the bin stores its tracks' components. `letter` is V or A, for the tracks
    // clips are named by. A transition is kept `pending` and goes onto the next segment pushed;
    // finish() reports one that no segment follows.
    struct Flattener {
        Importer& importer;
        QChar letter;
        QList<model::Segment> out;
        std::optional<model::Transition> pending;
        std::int64_t pendingCut = 0;

        // Applies a pending transition to the segment after it: that segment starts `pendingCut`
        // frames later.
        void push(model::Segment s) {
            if (pending) {
                if (!s.isFiller()) s.sourceIn += pendingCut;
                s.length = std::max<std::int64_t>(0, s.length - pendingCut);
                s.transition = *pending;
                pending.reset();
            }
            out << s;
        }

        // Ends the track (or an effect's input). A transition last in its sequence has nothing to go
        // onto: it is left out, with a warning.
        void finish() {
            if (!pending) return;
            importer.note(QStringLiteral("transitions at the end of a track left out"));
            pending.reset();
            pendingCut = 0;
        }

        void add(const Object* c, int depth = 0) {
            if (!c || depth > 32) return;
            const std::int64_t length = importer.lengthOf(c);
            if (is(c, "SEQU")) {
                for (const Value& v : c->property(QStringLiteral("components")).toList()) add(importer.deref(v), depth + 1);
            } else if (is(c, "SCLP")) {
                const MobId id = mobIdOf(*c);
                push(noSource(id) ? model::Segment::filler(length) : importer.segmentFor(*c, id, length, letter));
            } else if (is(c, "TNFX")) {
                // A transition of length L with its cut point at c overlaps the segments on either
                // side. In a sequence it sits at the cut: the segment before is shortened by L - c
                // here, and the one after starts c frames later (push). The alignment follows c.
                const std::int64_t cut = std::clamp<std::int64_t>(c->property(QStringLiteral("cutpoint")).toInt(), 0, std::max<std::int64_t>(0, length));
                if (!out.isEmpty()) out.last().length = std::max<std::int64_t>(0, out.last().length - (length - cut));
                model::Transition t;
                t.effect = importer.effectOf(*c);
                t.duration = length;
                t.start = cut;
                t.alignment = cut == 0              ? model::TransitionAlignment::StartingAtCut
                              : cut == length       ? model::TransitionAlignment::EndingAtCut
                              : cut * 2 == length   ? model::TransitionAlignment::CenteredOnCut
                                                    : model::TransitionAlignment::CustomStart;
                pending = t;
                pendingCut = cut;
            } else if (!c->property(QStringLiteral("tracks")).isNull()) {
                group(*c, length, depth);
            } else {
                // Filler, or something not read as material: empty for its length.
                if (!is(c, "FILL")) importer.note(QStringLiteral("%1 components read as filler").arg(c->classId));
                push(model::Segment::filler(length));
            }
        }

        // A track group (an effect, a motion effect, an essence group): the segments of its first
        // track that has material, fitted to the group's length (a motion effect's input runs at
        // its own speed), with the group's effect.
        void group(const Object& c, std::int64_t length, int depth) {
            Flattener inner{importer, letter, {}, {}, 0};
            for (const Value& t : c.property(QStringLiteral("tracks")).toList()) {
                const Object* input = importer.deref(t.member(QStringLiteral("component")));
                if (!input) continue;
                inner.out.clear();
                inner.add(input, depth + 1);
                inner.finish();
                if (std::any_of(inner.out.begin(), inner.out.end(), [](const model::Segment& s) { return !s.isFiller(); })) break;
            }
            // As the bin's own tidying: no empty segments, neighbouring filler joined.
            QList<model::Segment> segments;
            for (const model::Segment& s : inner.out) {
                if (s.length <= 0) continue;
                if (s.isFiller() && !s.hasEffects() && !segments.isEmpty() && segments.last().isFiller() && !segments.last().hasEffects()) {
                    segments.last().length += s.length;
                } else {
                    segments << s;
                }
            }
            if (segments.isEmpty()) segments << model::Segment::filler(length);
            std::int64_t total = 0;
            for (const model::Segment& s : segments) total += s.length;
            while (total > length && !segments.isEmpty()) {
                model::Segment& last = segments.last();
                const std::int64_t cut = std::min(last.length, total - length);
                last.length -= cut;
                total -= cut;
                if (last.length == 0 && segments.size() > 1) segments.removeLast();
            }
            if (total < length) segments.last().length += length - total;
            // Audio Pan Volume (PVOL) is clip gain and pan (p.702), not an effect; essence groups and
            // plain track groups are not effects either.
            const QString id = c.property(QStringLiteral("effect_id")).toString();
            const bool effect = !id.isEmpty() && !is(&c, "PVOL") && !is(&c, "RSET") && !is(&c, "TRKG");
            if (is(&c, "PVOL")) importer.note(QStringLiteral("clip gain and pan (Audio Pan Volume) not read"));
            for (model::Segment& s : segments) {
                if (effect) {
                    // The model keeps one effect a segment: the innermost, nearest the material
                    // (a motion effect inside Media Composer's adapters).
                    if (!s.effect) {
                        s.effect = importer.effectOf(c);
                    } else {
                        importer.note(QStringLiteral("effects nested in effects: the innermost kept"));
                    }
                }
                push(s);
            }
        }
    };

    // An effect of the native model for an effect component: dissolves are Blend/Dissolve; others
    // keep their Media Composer effect IDs, in the category "AVB", until effects are matched to the
    // Effect Palette's (Phase 6), as the AAF import keeps AAF names.
    model::Effect effectOf(const Object& c) {
        model::Effect e;
        const QString id = c.property(QStringLiteral("effect_id")).toString();
        if (id == QLatin1String("EFF2_BLEND_DISSOLVE") || id == QLatin1String("BLEND_AUDIO_DISSOLVE") ||
            id == QLatin1String("BLEND_DISSOLVE")) {
            e.category = QStringLiteral("Blend");
            e.name = QStringLiteral("Dissolve");
        } else {
            e.category = QStringLiteral("AVB");
            e.name = id.isEmpty() ? QStringLiteral("Effect") : id;
            note(QStringLiteral("effects kept by their Media Composer effect IDs"));
        }
        return e;
    }

    // A segment for a source clip naming `id`: its clip item when the bin holds the clip, its start
    // time as the IN point, the clip's track by the source clip's media kind and track_id.
    model::Segment segmentFor(const Object& clip, const MobId& id, std::int64_t length, QChar letter) {
        model::Segment s;
        s.sourceMob = id;
        s.length = length;
        s.sourceIn = clip.property(QStringLiteral("start_time")).toInt();
        const Kind kind = kindOf(clip);
        const QChar trackLetter = kind == Kind::Picture ? QLatin1Char('V') : kind == Kind::Sound ? QLatin1Char('A') : letter;
        s.sourceTrack = QStringLiteral("%1%2").arg(trackLetter).arg(clip.property(QStringLiteral("track_id")).toInt());
        if (items_.count(id)) {
            s.clipId = items_.at(id);
            s.binId = options_.binId;
            s.name = names_.at(id);
        } else {
            // A clip the bin does not hold: kept by its MobID, offline.
            s.clipId = QStringLiteral("avb:") + QString::fromStdString(id.toString());
            const Object* m = mob(id);
            s.name = m ? m->property(QStringLiteral("name")).toString() : QString();
            note(QStringLiteral("segments of clips not in the bin"));
        }
        return s;
    }

    // A composition mob as a sequence. Its first timecode track gives Start (scaled to the edit
    // rate's nominal frames per second) and the TC1 track; picture and sound tracks give V and A
    // tracks, by label.
    void sequence(const Entry& e) {
        const Object& m = *e.mob;
        model::BinItem item;
        commonFields(item, e);
        item.kind = model::BinItemKind::Sequence;
        model::Sequence seq;
        seq.setTimecodeTrack(false);
        std::optional<std::int64_t> start;
        bool drop = false;
        int timecodes = 0;
        for (const Value& v : m.property(QStringLiteral("tracks")).toList()) {
            const Object* c = deref(v.member(QStringLiteral("component")));
            if (!c) continue;
            switch (kindOf(*c)) {
            case Kind::Timecode:
                seq.setTimecodeTrack(true);
                ++timecodes;
                if (!start && is(c, "TCCP")) {
                    drop = (c->property(QStringLiteral("flags")).toInt() & 1) != 0;
                    start = startAtRate(c->property(QStringLiteral("start")).toInt(),
                                        static_cast<int>(c->property(QStringLiteral("fps")).toInt()), item.editRate);
                }
                break;
            case Kind::Edgecode: note(QStringLiteral("edgecode tracks left out")); break;
            case Kind::Picture:
            case Kind::Sound: break;
            default: note(QStringLiteral("data tracks left out")); break;
            }
        }
        // The model has one timecode track (TC1); Media Composer's sequences carry more.
        if (timecodes > 1) note(QStringLiteral("timecode tracks after the first left out"));
        QList<Track> tracks = tracksOf(m);
        std::stable_sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) {
            if (a.kind != b.kind) return a.kind == Kind::Picture;
            return a.name.mid(1).toInt() < b.name.mid(1).toInt();
        });
        QSet<QString> seen;
        for (const Track& t : tracks) {
            if (seen.contains(t.name)) {
                note(QStringLiteral("tracks with the same number as another left out"));
                continue;
            }
            seen.insert(t.name);
            model::Track* track = seq.ensureTrack(t.name);
            if (!track) {
                note(QStringLiteral("tracks past the limit left out"));
                continue;
            }
            Flattener f{*this, t.kind == Kind::Picture ? QLatin1Char('V') : QLatin1Char('A'), {}, {}, 0};
            f.add(t.component);
            f.finish();
            track->segments = f.out;
            seq.normalize(t.name);
        }
        seq.storeIn(item);
        QList<int> video;
        QList<int> audio;
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video) video << t.number;
            if (t.kind == model::TrackKind::Audio) audio << t.number;
        }
        QString list = trackList(video, audio);
        if (seq.hasTimecodeTrack()) list += QStringLiteral(" TC1");
        item.fields.insert(QStringLiteral("Tracks"), list.trimmed());
        if (start) {
            item.fields.insert(QStringLiteral("Start"), timecodeText(*start, item.editRate, drop));
            item.fields.insert(QStringLiteral("End"), timecodeText(*start + item.durationFrames, item.editRate, drop));
        }
        result_.items << item;
    }

    // The bin's mobs in item order and by MobID, and for each imported clip or subclip (by MobID): its
    // item ID and name, and for master clips its start in frames and its Tape column. notes_ counts
    // the warnings, unreadable_ the objects the reader could not read.
    const File& file_;
    ImportOptions options_;
    ImportResult result_;
    QList<Entry> order_;
    std::map<MobId, const Object*> mobs_;
    std::map<MobId, QString> items_;
    std::map<MobId, QString> names_;
    std::map<MobId, std::int64_t> starts_;
    std::map<MobId, QString> tapes_;
    std::map<QString, int> notes_;
    std::set<std::uint32_t> unreadable_;
};

} // namespace

// `when` defaults to now, so that items always have dates.
ImportResult importBin(const File& file, const ImportOptions& options) {
    ImportOptions o = options;
    if (!o.when.isValid()) o.when = QDateTime::currentDateTimeUtc();
    return Importer(file, o).run();
}

std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options, QString* error) {
    File file;
    if (!file.open(path, error)) return std::nullopt;
    const Object& root = file.root();
    if (root.classId != QLatin1String("ABIN") || !root.ok()) {
        if (error) {
            *error = QStringLiteral("%1: not a bin (%2)").arg(path, root.ok() ? root.classId : root.error);
        }
        return std::nullopt;
    }
    // A damaged bin's numbers can be past what Rational holds (it throws rather than wrap): the bin is
    // then unreadable, not a crash.
    try {
        return importBin(file, options);
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, QString::fromUtf8(e.what()));
        return std::nullopt;
    }
}

} // namespace lumenis::interop::avb
