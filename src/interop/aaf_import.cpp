#include "lumenis/interop/aaf_import.h"

#include "import_common.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"

#include <QSet>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <exception>
#include <map>

// AAF import into bins (docs/spec/aaf.md, "Import"): the mobs an aaf::File reads (aaf.cpp) become bin
// items of the native model (docs/spec/native-model.md). Master mobs become master or audio clips with
// their source chains (file, then tape or import source mobs), composition mobs used as subclips become
// subclips, and other composition mobs become sequences, whose nested scopes, transitions and effects are
// flattened into tracks of segments. aaf_export.cpp applies the same rules in reverse. ctest
// aaf_import_bins compares what this makes with Media Composer's own bins of the corpus
// (tests/data/aaf_reference); tests/interop/tst_aaf.cpp (importsBin) checks what the dumps do not show.
namespace lumenis::interop::aaf {

namespace {

using detail::exactNtscRate;
using detail::rescale;
using detail::SourceTimecode;
using detail::saturatingAdd;
using detail::startAtRate;
using detail::timecodeText;
using detail::trackList;

// What a slot carries, by its segment's data definition.
enum class DataKind { Picture, Sound, Timecode, Edgecode, Other };

// Data definitions by AUID: AAF's (SMPTE labels) and the older ones Avid
// still writes.
DataKind dataKindOf(const QUuid& auid) {
    static const QHash<QUuid, DataKind> kinds{
        {auidFromText("01030202-0100-0000-060e-2b3404010101"), DataKind::Picture},
        {auidFromText("6f3c8ce1-6cef-11d2-807d-006008143e6f"), DataKind::Picture},
        {auidFromText("05cba731-1daa-11d3-80ad-006008143e6f"), DataKind::Picture}, // Matte
        {auidFromText("05cba732-1daa-11d3-80ad-006008143e6f"), DataKind::Picture}, // PictureWithMatte
        {auidFromText("01030202-0200-0000-060e-2b3404010101"), DataKind::Sound},
        {auidFromText("78e1ebe1-6cef-11d2-807d-006008143e6f"), DataKind::Sound},
        {auidFromText("01030201-0100-0000-060e-2b3404010101"), DataKind::Timecode},
        {auidFromText("01030201-0200-0000-060e-2b3404010101"), DataKind::Timecode},
        {auidFromText("01030201-0300-0000-060e-2b3404010101"), DataKind::Timecode},
        {auidFromText("7f275e81-77e5-11d2-807f-006008143e6f"), DataKind::Timecode},
        {auidFromText("d2bb2af0-d234-11d2-89ee-006097116212"), DataKind::Edgecode},
    };
    return kinds.value(auid, DataKind::Other);
}

// The component's kind, from its DataDefinition weak reference.
DataKind dataKind(const Object& component) {
    const auto key = component.weakKey(QStringLiteral("DataDefinition"));
    if (!key || key->key.size() != 16) return DataKind::Other;
    return dataKindOf(auidFromBytes(key->key.constData()));
}

// An edit rate (a Rational record) as a Rational; `fallback` when it is missing or not positive.
Rational rateOf(const QVariant& v, const Rational& fallback) {
    const QVariantMap m = v.toMap();
    const qint64 num = m.value(QStringLiteral("Numerator")).toLongLong();
    const qint64 den = m.value(QStringLiteral("Denominator")).toLongLong();
    if (num <= 0 || den <= 0) return fallback;
    // Media Composer writes NTSC rates rounded (23976/1000, 2997/100): they
    // are the exact ones (24000/1001, 30000/1001).
    return exactNtscRate(Rational(num, den));
}

// A TimeStamp record (date and time, UTC) as a QDateTime; invalid when the record is missing or damaged.
QDateTime timeOf(const QVariant& v) {
    const QVariantMap m = v.toMap();
    const QVariantMap d = m.value(QStringLiteral("date")).toMap();
    const QVariantMap t = m.value(QStringLiteral("time")).toMap();
    const QDate date(d.value(QStringLiteral("year")).toInt(), d.value(QStringLiteral("month")).toInt(),
                     d.value(QStringLiteral("day")).toInt());
    const QTime time(t.value(QStringLiteral("hour")).toInt(), t.value(QStringLiteral("minute")).toInt(),
                     t.value(QStringLiteral("second")).toInt());
    return date.isValid() && time.isValid() ? QDateTime(date, time, QTimeZone::utc()) : QDateTime();
}

// Reads one file's mobs. run() goes through them in four passes: index every mob by MobID, then master
// mobs, then subclips (which name their masters' items), then sequences (whose segments name either). The
// maps below carry what later passes need from earlier ones.
class Importer {
public:
    Importer(const File& file, const ImportOptions& options) : file_(file), options_(options) {}

    ImportResult run() {
        for (const Object& m : file_.mobs()) {
            const MobId id = m.mobId(QStringLiteral("MobID"));
            if (!id.isNull()) mobs_.emplace(id, m);
            order_.append(m);
        }
        for (const Object& m : order_) {
            if (m.isA(QStringLiteral("MasterMob"))) master(m);
        }
        for (const Object& m : order_) {
            if (!m.isA(QStringLiteral("CompositionMob"))) continue;
            const QString usage = m.string(QStringLiteral("UsageCode"));
            if (usage == QLatin1String("Usage_SubClip")) subclip(m);
        }
        for (const Object& m : order_) {
            if (!m.isA(QStringLiteral("CompositionMob"))) continue;
            const QString usage = m.string(QStringLiteral("UsageCode"));
            if (usage.isEmpty() || usage == QLatin1String("Usage_TopLevel")) {
                sequence(m);
            } else if (usage != QLatin1String("Usage_SubClip")) {
                note(QStringLiteral("composition mobs used as %1 (not imported)").arg(usage));
            }
        }
        for (auto it = notes_.begin(); it != notes_.end(); ++it) {
            result_.warnings << (it->second > 1 ? QStringLiteral("%1 × %2").arg(it->second).arg(it->first) : it->first);
        }
        return result_;
    }

private:
    // One picture or sound slot of a mob, named as a bin track ("V1", "A2").
    struct Track {
        QString name;
        DataKind kind = DataKind::Other;
        Rational rate{25};
        std::int64_t length = 0;
        Object segment;
    };

    // Counts a warning; run() lists each once, with its count.
    void note(const QString& what) { ++notes_[what]; }

    // A mob of the file by MobID; null when the file does not hold it.
    Object mob(const MobId& id) const {
        const auto it = mobs_.find(id);
        return it == mobs_.end() ? Object() : it->second;
    }
    template <typename T>
    static T valueIn(const std::map<MobId, T>& map, const MobId& id, const T& fallback = T()) {
        const auto it = map.find(id);
        return it == map.end() ? fallback : it->second;
    }

    // The first source clip in a segment: itself, or down sequences, effects' inputs and essence groups'
    // choices.
    static Object firstSourceClip(const Object& c, int depth = 0) {
        if (!c || depth > 16) return {};
        if (c.isA(QStringLiteral("SourceClip"))) return c;
        if (c.isA(QStringLiteral("Sequence"))) {
            for (const Object& x : c.objects(QStringLiteral("Components"))) {
                if (Object s = firstSourceClip(x, depth + 1)) return s;
            }
        }
        if (c.isA(QStringLiteral("OperationGroup"))) {
            for (const Object& x : c.objects(QStringLiteral("InputSegments"))) {
                if (Object s = firstSourceClip(x, depth + 1)) return s;
            }
        }
        if (c.isA(QStringLiteral("EssenceGroup"))) {
            for (const Object& x : c.objects(QStringLiteral("Choices"))) {
                if (Object s = firstSourceClip(x, depth + 1)) return s;
            }
        }
        return {};
    }

    // A mob's slot by SlotID; null when there is none.
    static Object slot(const Object& mob, qint64 slotId) {
        for (const Object& s : mob.objects(QStringLiteral("Slots"))) {
            if (s.integer(QStringLiteral("SlotID"), -1) == slotId) return s;
        }
        return {};
    }

    // The tracks of a mob's picture and sound slots, named as in a bin.
    // The number is the slot's PhysicalTrackNumber, or else the count of slots of that kind so far.
    static QList<Track> tracksOf(const Object& m) {
        QList<Track> out;
        int pictures = 0;
        int sounds = 0;
        for (const Object& s : m.objects(QStringLiteral("Slots"))) {
            if (!s.isA(QStringLiteral("TimelineMobSlot"))) continue;
            Track t;
            t.segment = s.object(QStringLiteral("Segment"));
            t.kind = dataKind(t.segment);
            if (t.kind != DataKind::Picture && t.kind != DataKind::Sound) continue;
            const int counted = t.kind == DataKind::Picture ? ++pictures : ++sounds;
            const auto number = static_cast<int>(s.integer(QStringLiteral("PhysicalTrackNumber"), counted));
            t.name = QStringLiteral("%1%2").arg(t.kind == DataKind::Picture ? QLatin1Char('V') : QLatin1Char('A')).arg(number);
            t.rate = rateOf(s.value(QStringLiteral("EditRate")), Rational(25));
            t.length = t.segment.integer(QStringLiteral("Length"));
            out.append(t);
        }
        return out;
    }

    // The first Timecode segment among a mob's slots, also inside a Pulldown or as the first component of
    // a Sequence.
    static std::optional<SourceTimecode> timecodeOf(const Object& m) {
        for (const Object& s : m.objects(QStringLiteral("Slots"))) {
            Object c = s.object(QStringLiteral("Segment"));
            for (int depth = 0; c && depth < 8; ++depth) {
                if (c.isA(QStringLiteral("Timecode"))) {
                    return SourceTimecode{c.integer(QStringLiteral("Start")), static_cast<int>(c.integer(QStringLiteral("FPS"))),
                                          c.value(QStringLiteral("Drop")).toBool()};
                }
                if (c.isA(QStringLiteral("Pulldown"))) {
                    c = c.object(QStringLiteral("InputSegment"));
                } else if (c.isA(QStringLiteral("Sequence"))) {
                    const QList<Object> parts = c.objects(QStringLiteral("Components"));
                    c = parts.isEmpty() ? Object() : parts.first();
                } else {
                    break;
                }
            }
        }
        return std::nullopt;
    }

    // The media's path: the first locator URL of the mob's descriptor, as a local path when it is a file
    // URL.
    static QString locatorPath(const Object& m) {
        const Object descriptor = m.object(QStringLiteral("EssenceDescription"));
        for (const Object& l : descriptor.objects(QStringLiteral("Locator"))) {
            const QString url = l.string(QStringLiteral("URLString"));
            if (url.isEmpty()) continue;
            const QUrl u(url);
            return u.isLocalFile() ? u.toLocalFile() : url;
        }
        return {};
    }

    // Frames in `rate` where the clip starts on its source's timecode: down
    // the source chain from a slot's clip to the first mob with timecode.
    // Each source clip's StartTime is added up on the way. A timecode at another frame rate is scaled to
    // the nominal rate of `rate` (30 fps 00:00:30;00 is 24 fps 00:00:30:00).
    std::optional<std::int64_t> startOf(const Object& segment, const Rational& rate) const {
        Object clip = firstSourceClip(segment);
        std::int64_t offset = 0;
        for (int depth = 0; clip && depth < 8; ++depth) {
            offset = saturatingAdd(offset, clip.integer(QStringLiteral("StartTime")));
            const Object source = mob(clip.mobId(QStringLiteral("SourceID")));
            if (!source) return std::nullopt;
            if (const auto tc = timecodeOf(source)) {
                return saturatingAdd(startAtRate(tc->start, tc->fps, rate), offset);
            }
            const Object next = slot(source, clip.integer(QStringLiteral("SourceMobSlotID")));
            clip = next ? firstSourceClip(next.object(QStringLiteral("Segment"))) : Object();
        }
        return std::nullopt;
    }

    // What every item takes from its mob: a new item ID, the MobID, Name, Created and Modified (`when`,
    // when the mob has none), and the bin columns.
    void commonFields(model::BinItem& item, const Object& m) {
        item.id = model::newItemId();
        item.mobId = m.mobId(QStringLiteral("MobID"));
        item.name = m.string(QStringLiteral("Name"));
        item.created = timeOf(m.value(QStringLiteral("CreationTime")));
        item.modified = timeOf(m.value(QStringLiteral("LastModified")));
        if (!item.created.isValid()) item.created = options_.when;
        if (!item.modified.isValid()) item.modified = item.created;
        // Bin columns travel as user comments (Name, Value).
        for (const Object& c : m.objects(QStringLiteral("UserComments"))) {
            const QString name = c.string(QStringLiteral("Name"));
            const QVariant value = c.value(QStringLiteral("Value"));
            if (name.isEmpty()) continue;
            const QString text = value.typeId() == QMetaType::QString ? value.toString()
                                 : value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::ULongLong
                                     ? value.toString()
                                     : QString();
            item.fields.insert(name, text);
            if (!model::binHeadings().contains(name) && !result_.customColumns.contains(name)) result_.customColumns << name;
        }
    }

    // The edit rate (the first picture track's, else the first track's), the duration (the longest track
    // of that kind, in that rate), the Tracks column, and Start and End when there is a start.
    void setTiming(model::BinItem& item, const QList<Track>& tracks, std::optional<std::int64_t> start, bool drop) {
        QList<int> video;
        QList<int> audio;
        const Track* first = nullptr;
        for (const Track& t : tracks) {
            (t.kind == DataKind::Picture ? video : audio) << t.name.mid(1).toInt();
            if (!first || (t.kind == DataKind::Picture && first->kind != DataKind::Picture)) first = &t;
        }
        item.editRate = first ? first->rate : Rational(25);
        for (const Track& t : tracks) {
            if (first && t.kind == first->kind) item.durationFrames = std::max(item.durationFrames, rescale(t.length, t.rate, item.editRate));
        }
        if (!tracks.isEmpty()) item.fields.insert(QStringLiteral("Tracks"), trackList(video, audio));
        if (start) {
            item.fields.insert(QStringLiteral("Start"), timecodeText(*start, item.editRate, drop));
            item.fields.insert(QStringLiteral("End"), timecodeText(saturatingAdd(*start, item.durationFrames), item.editRate, drop));
        }
    }

    // A clip Media Composer made for a rendered effect: its media files are
    // marked so (AppCode 9, "file mob with a precompute" in pyavb). Its bin
    // does not show it, and it is not imported.
    bool isPrecompute(const Object& m) const {
        for (const Track& t : tracksOf(m)) {
            const Object clip = firstSourceClip(t.segment);
            const Object fileMob = clip ? mob(clip.mobId(QStringLiteral("SourceID"))) : Object();
            if (fileMob && fileMob.integer(QStringLiteral("AppCode"), 0) == 9) return true;
        }
        return false;
    }

    // A master mob as a master clip, or an audio clip when it has no picture. `slotTracks` maps the mob's
    // SlotIDs to track names, so that sequences' and subclips' source clips (which name a slot) find the
    // track.
    void master(const Object& m) {
        if (isPrecompute(m)) {
            note(QStringLiteral("rendered effect clips left out"));
            return;
        }
        model::BinItem item;
        commonFields(item, m);
        const QList<Track> tracks = tracksOf(m);
        bool picture = false;
        QHash<qint64, QString> slotTracks;
        for (const Object& s : m.objects(QStringLiteral("Slots"))) {
            for (const Track& t : tracks) {
                if (t.segment == s.object(QStringLiteral("Segment"))) slotTracks.insert(s.integer(QStringLiteral("SlotID")), t.name);
            }
        }
        for (const Track& t : tracks) picture = picture || t.kind == DataKind::Picture;
        item.kind = picture || tracks.isEmpty() ? model::BinItemKind::MasterClip : model::BinItemKind::AudioClip;

        // The source chain: each track's file, then the tape or file before them.
        QList<MobId> before;
        for (const Track& t : tracks) {
            const Object clip = firstSourceClip(t.segment);
            const Object fileMob = clip ? mob(clip.mobId(QStringLiteral("SourceID"))) : Object();
            if (!fileMob || !fileMob.isA(QStringLiteral("SourceMob"))) continue;
            // A clip straight from its tape or import, with no file between.
            const Object descriptor = fileMob.object(QStringLiteral("EssenceDescription"));
            if (descriptor.isA(QStringLiteral("TapeDescriptor")) || descriptor.isA(QStringLiteral("ImportDescriptor"))) {
                const MobId id = fileMob.mobId(QStringLiteral("MobID"));
                if (!before.contains(id)) before << id;
                continue;
            }
            model::ClipSource s;
            s.kind = model::ClipSource::Kind::File;
            s.mob = fileMob.mobId(QStringLiteral("MobID"));
            s.track = t.name;
            s.name = fileMob.string(QStringLiteral("Name"));
            s.path = locatorPath(fileMob);
            const Object next = slot(fileMob, clip.integer(QStringLiteral("SourceMobSlotID")));
            const Object nextClip = next ? firstSourceClip(next.object(QStringLiteral("Segment"))) : Object();
            if (nextClip) s.from = nextClip.mobId(QStringLiteral("SourceID"));
            if (!s.from.isNull() && !before.contains(s.from)) before << s.from;
            item.sources << s;
        }
        bool drop = false;
        // The sources before the files: a Tape source (which also gives the Tape column) or an Import
        // source, each with its own start timecode.
        for (const MobId& id : before) {
            const Object source = mob(id);
            const Object descriptor = source.object(QStringLiteral("EssenceDescription"));
            model::ClipSource s;
            s.mob = id;
            s.name = source.string(QStringLiteral("Name"));
            if (descriptor.isA(QStringLiteral("TapeDescriptor"))) {
                s.kind = model::ClipSource::Kind::Tape;
                item.fields.insert(QStringLiteral("Tape"), s.name);
            } else if (descriptor.isA(QStringLiteral("ImportDescriptor"))) {
                s.kind = model::ClipSource::Kind::Import;
                s.path = locatorPath(source);
            } else {
                continue;
            }
            if (const auto tc = timecodeOf(source)) {
                drop = drop || tc->drop;
                s.startTimecode = timecodeText(tc->start, Rational(tc->fps > 0 ? tc->fps : 25), tc->drop);
            }
            item.sources << s;
        }
        const Track* first = nullptr;
        for (const Track& t : tracks) {
            if (!first || (t.kind == DataKind::Picture && first->kind != DataKind::Picture)) first = &t;
        }
        std::optional<std::int64_t> start;
        if (first) start = startOf(first->segment, first->rate);
        setTiming(item, tracks, start, drop);

        items_[item.mobId] = item.id;
        slotTracks_[item.mobId] = slotTracks;
        names_[item.mobId] = item.name;
        tapes_[item.mobId] = item.fields.value(QStringLiteral("Tape"));
        if (start) starts_[item.mobId] = *start;
        result_.items << item;
    }

    // A composition mob used as Usage_SubClip: a Clip source naming its master (the first track's source
    // clip) and where in it the subclip starts. Start is the master's plus that offset.
    void subclip(const Object& m) {
        model::BinItem item;
        commonFields(item, m);
        item.kind = model::BinItemKind::Subclip;
        const QList<Track> tracks = tracksOf(m);
        QHash<qint64, QString> slotTracks;
        for (const Object& s : m.objects(QStringLiteral("Slots"))) {
            for (const Track& t : tracks) {
                if (t.segment == s.object(QStringLiteral("Segment"))) slotTracks.insert(s.integer(QStringLiteral("SlotID")), t.name);
            }
        }
        std::optional<std::int64_t> start;
        if (!tracks.isEmpty()) {
            const Object clip = firstSourceClip(tracks.first().segment);
            if (clip) {
                const MobId master = clip.mobId(QStringLiteral("SourceID"));
                model::ClipSource s;
                s.kind = model::ClipSource::Kind::Clip;
                s.mob = master;
                s.name = valueIn(names_, master, mob(master).string(QStringLiteral("Name")));
                s.offset = clip.integer(QStringLiteral("StartTime"));
                // A subclip shows its master's tape (p.407).
                const QString tape = valueIn(tapes_, master);
                if (!tape.isEmpty()) item.fields.insert(QStringLiteral("Tape"), tape);
                if (starts_.count(master)) {
                    start = starts_.at(master) + s.offset;
                    s.startTimecode = timecodeText(*start, tracks.first().rate, false);
                }
                item.sources << s;
            }
        }
        setTiming(item, tracks, start, false);
        items_[item.mobId] = item.id;
        slotTracks_[item.mobId] = slotTracks;
        names_[item.mobId] = item.name;
        result_.items << item;
    }

    // ---- sequences ----

    // Turns one track's AAF components into the native model's segments, at the sequence's edit rate
    // (`rate`; lengths in the file are in the slot's, `slotRate`). `letter` is V or A, for track names of
    // clips the file does not hold. A transition is kept `pending` and goes onto the next segment pushed;
    // finish() reports one that no segment follows.
    struct Flattener {
        Importer& importer;
        Rational rate;
        Rational slotRate;
        QChar letter;
        QList<model::Segment> out;
        std::optional<model::Transition> pending;
        std::int64_t pendingCut = 0;

        std::int64_t frames(const Object& c) const {
            return rescale(c.integer(QStringLiteral("Length")), slotRate, rate);
        }

        // Applies a pending transition to the segment after it: that segment starts `pendingCut` frames
        // later.
        void push(model::Segment s) {
            if (pending) {
                if (!s.isFiller()) s.sourceIn += pendingCut;
                s.length = std::max<std::int64_t>(0, s.length - pendingCut);
                s.transition = *pending;
                pending.reset();
            }
            out << s;
        }

        // Ends the track (or an effect's input). AAF puts a transition between two segments, so one left
        // pending, last in its sequence, has nothing to go onto: it is left out, with a warning.
        void finish() {
            if (!pending) return;
            importer.note(QStringLiteral("transitions at the end of a track left out"));
            pending.reset();
            pendingCut = 0;
        }

        void add(const Object& c, int depth = 0) {
            if (!c || depth > 32) return;
            if (c.isA(QStringLiteral("Sequence"))) {
                for (const Object& x : c.objects(QStringLiteral("Components"))) add(x, depth + 1);
            } else if (c.isA(QStringLiteral("SourceClip"))) {
                const MobId id = c.mobId(QStringLiteral("SourceID"));
                if (id.isNull()) {
                    push(model::Segment::filler(frames(c)));
                } else {
                    push(importer.segmentFor(c, id, frames(c), letter));
                }
            } else if (c.isA(QStringLiteral("Transition"))) {
                // A transition of length L with its cut point at c overlaps the segments on either side.
                // In a sequence it sits at the cut: the segment before is shortened by L - c here, and the
                // one after starts c frames later (push). The alignment follows c.
                const std::int64_t length = frames(c);
                const std::int64_t cut = std::clamp<std::int64_t>(rescale(c.integer(QStringLiteral("CutPoint")), slotRate, rate), 0, length);
                if (!out.isEmpty()) out.last().length = std::max<std::int64_t>(0, out.last().length - (length - cut));
                model::Transition t;
                t.effect = importer.effectOf(c.object(QStringLiteral("OperationGroup")));
                t.duration = length;
                t.start = cut;
                t.alignment = cut == 0                ? model::TransitionAlignment::StartingAtCut
                              : cut == length         ? model::TransitionAlignment::EndingAtCut
                              : cut * 2 == length     ? model::TransitionAlignment::CenteredOnCut
                                                      : model::TransitionAlignment::CustomStart;
                pending = t;
                pendingCut = cut;
            } else if (c.isA(QStringLiteral("OperationGroup"))) {
                // The effect's material: its first input that has some.
                Flattener inner{importer, rate, slotRate, letter, {}, {}, 0};
                for (const Object& input : c.objects(QStringLiteral("InputSegments"))) {
                    inner.out.clear();
                    inner.add(input, depth + 1);
                    inner.finish();
                    if (std::any_of(inner.out.begin(), inner.out.end(), [](const model::Segment& s) { return !s.isFiller(); })) break;
                }
                if (inner.out.isEmpty()) inner.out << model::Segment::filler(frames(c));
                // A timewarp's input runs at its own speed: the effect lasts
                // as long as the group does.
                std::int64_t total = 0;
                for (const model::Segment& s : inner.out) total += s.length;
                // A damaged file's Length can be negative: fitting the input to it below would then
                // never end (tst_aaf negativeGroupLength; the bin import had the same, found by fuzz_avb).
                const std::int64_t want = std::max<std::int64_t>(0, frames(c));
                while (total > want && !inner.out.isEmpty()) {
                    model::Segment& last = inner.out.last();
                    const std::int64_t cut = std::min(last.length, total - want);
                    last.length -= cut;
                    total -= cut;
                    if (last.length == 0 && inner.out.size() > 1) inner.out.removeLast();
                }
                if (total < want) inner.out.last().length += want - total;
                const QString name = importer.operationName(c);
                // Clip gain and pan are not effects in a bin (p.702).
                const bool mix = name == QLatin1String("Audio Gain") || name == QLatin1String("Audio Pan");
                for (model::Segment& s : inner.out) {
                    if (!mix) s.effect = importer.effectOf(c);
                    push(s);
                }
            } else if (c.isA(QStringLiteral("Selector"))) {
                add(c.object(QStringLiteral("Selected")), depth + 1);
            } else if (c.isA(QStringLiteral("EssenceGroup"))) {
                const QList<Object> choices = c.objects(QStringLiteral("Choices"));
                if (choices.isEmpty()) push(model::Segment::filler(frames(c)));
                else add(choices.first(), depth + 1);
            } else {
                // Filler, a reference to the track below (ScopeReference), or
                // something not read: empty for its length.
                if (!c.isA(QStringLiteral("Filler")) && !c.isA(QStringLiteral("ScopeReference"))) {
                    importer.note(QStringLiteral("%1 components read as filler").arg(c.className()));
                }
                push(model::Segment::filler(frames(c)));
            }
        }
    };

    // The name of a group's OperationDefinition (a weak reference); empty when it cannot be found.
    QString operationName(const Object& group) const {
        const QVariant op = group.value(QStringLiteral("Operation"));
        if (op.userType() == qMetaTypeId<Object>()) return op.value<Object>().string(QStringLiteral("Name"));
        return {};
    }

    // An effect of the native model for an OperationGroup: dissolves (by AUID, or by Media Composer's
    // names) are Blend/Dissolve; others are read back from Cutthulhu's "Category/Name" or kept by their
    // AAF names.
    model::Effect effectOf(const Object& group) {
        model::Effect e;
        const QString name = operationName(group);
        const QVariant op = group.value(QStringLiteral("Operation"));
        const QUuid id = op.userType() == qMetaTypeId<Object>() ? op.value<Object>().auid(QStringLiteral("Identification")) : QUuid();
        static const QUuid videoDissolve = auidFromText("0c3bea40-fc05-11d2-8a29-0050040ef7d2");
        static const QUuid audioDissolve = auidFromText("0c3bea41-fc05-11d2-8a29-0050040ef7d2");
        if (id == videoDissolve || id == audioDissolve || name.startsWith(QLatin1String("VideoDissolve")) ||
            name == QLatin1String("Audio Dissolve")) {
            e.category = QStringLiteral("Blend");
            e.name = QStringLiteral("Dissolve");
        } else if (name.contains(QLatin1Char('/'))) {
            // Cutthulhu's own: "Category/Name" (aaf_export.cpp).
            e.category = name.section(QLatin1Char('/'), 0, 0);
            e.name = name.section(QLatin1Char('/'), 1);
        } else {
            // Kept by its AAF name until effects are matched (Phase 6, docs/spec/aaf.md).
            e.category = QStringLiteral("AAF");
            e.name = name.isEmpty() ? QStringLiteral("Effect") : name;
            note(QStringLiteral("effects kept by their AAF names"));
        }
        return e;
    }

    // A segment for a source clip naming `id`: its clip item and track when the clip is in the file, its
    // StartTime as the IN point.
    model::Segment segmentFor(const Object& clip, const MobId& id, std::int64_t length, QChar letter) {
        model::Segment s;
        s.sourceMob = id;
        s.length = length;
        s.sourceIn = clip.integer(QStringLiteral("StartTime"));
        const qint64 slotId = clip.integer(QStringLiteral("SourceMobSlotID"));
        if (items_.count(id)) {
            s.clipId = items_.at(id);
            s.binId = options_.binId;
            s.name = names_.at(id);
            s.sourceTrack = valueIn(slotTracks_, id).value(slotId);
        } else {
            // A clip the file does not hold: kept by its MobID, offline.
            s.clipId = QStringLiteral("aaf:") + QString::fromStdString(id.toString());
            s.name = mob(id).string(QStringLiteral("Name"));
            note(QStringLiteral("segments of clips not in the file"));
        }
        if (s.sourceTrack.isEmpty()) s.sourceTrack = QStringLiteral("%1%2").arg(letter).arg(slotId);
        return s;
    }

    // A composition mob as a sequence. The first timecode slot gives the TC1 track and Start (scaled to
    // the edit rate's nominal frames per second); picture and sound slots give V and A tracks.
    void sequence(const Object& m) {
        model::BinItem item;
        commonFields(item, m);
        item.kind = model::BinItemKind::Sequence;
        model::Sequence seq;
        seq.setTimecodeTrack(false);
        const QList<Track> tracks = tracksOf(m);
        const Track* first = nullptr;
        for (const Track& t : tracks) {
            if (!first || (t.kind == DataKind::Picture && first->kind != DataKind::Picture)) first = &t;
        }
        item.editRate = first ? first->rate : Rational(25);
        std::optional<std::int64_t> start;
        bool drop = false;
        for (const Object& s : m.objects(QStringLiteral("Slots"))) {
            const Object segment = s.object(QStringLiteral("Segment"));
            if (dataKind(segment) == DataKind::Timecode && segment.isA(QStringLiteral("Timecode"))) {
                seq.setTimecodeTrack(true);
                if (!start) {
                    drop = segment.value(QStringLiteral("Drop")).toBool();
                    start = startAtRate(segment.integer(QStringLiteral("Start")),
                                        static_cast<int>(segment.integer(QStringLiteral("FPS"))), item.editRate);
                }
            }
        }
        for (const Track& t : tracks) {
            const QChar letter = t.kind == DataKind::Picture ? QLatin1Char('V') : QLatin1Char('A');
            // Media Composer keeps its video tracks as the slots of one
            // nested scope, V1 first.
            QList<QPair<QString, Object>> layers;
            if (t.kind == DataKind::Picture && t.segment.isA(QStringLiteral("NestedScope"))) {
                const int base = t.name.mid(1).toInt();
                int i = 0;
                for (const Object& layer : t.segment.objects(QStringLiteral("Slots"))) {
                    layers.append({QStringLiteral("V%1").arg(base + i++), layer});
                }
            } else {
                layers.append({t.name, t.segment});
            }
            for (const auto& [name, layer] : layers) {
                model::Track* track = seq.ensureTrack(name);
                if (!track) {
                    note(QStringLiteral("tracks past the limit left out"));
                    continue;
                }
                Flattener f{*this, item.editRate, t.rate, letter, {}, {}, 0};
                f.add(layer);
                f.finish();
                track->segments = f.out;
                seq.normalize(name);
            }
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
            item.fields.insert(QStringLiteral("End"), timecodeText(saturatingAdd(*start, item.durationFrames), item.editRate, drop));
        }
        result_.items << item;
    }

    // The mobs in file order and by MobID, and for each imported clip or subclip (by MobID): its item ID,
    // its SlotIDs' track names, its name, and for master clips its start in frames and its Tape
    // column. notes_ counts the warnings.
    const File& file_;
    ImportOptions options_;
    ImportResult result_;
    QList<Object> order_;
    std::map<MobId, Object> mobs_;
    std::map<MobId, QString> items_;
    std::map<MobId, QHash<qint64, QString>> slotTracks_;
    std::map<MobId, QString> names_;
    std::map<MobId, std::int64_t> starts_;
    std::map<MobId, QString> tapes_;
    std::map<QString, int> notes_;
};

} // namespace

// `when` defaults to now, so that items always have dates.
ImportResult importMobs(const File& file, const ImportOptions& options) {
    ImportOptions o = options;
    if (!o.when.isValid()) o.when = QDateTime::currentDateTimeUtc();
    return Importer(file, o).run();
}

std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options, QString* error) {
    File file;
    if (!file.open(path, error)) return std::nullopt;
    // A damaged file's numbers can be past what Rational holds (it throws rather than wrap): the file
    // is then unreadable, not a crash.
    try {
        return importMobs(file, options);
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, QString::fromUtf8(e.what()));
        return std::nullopt;
    }
}

} // namespace lumenis::interop::aaf
