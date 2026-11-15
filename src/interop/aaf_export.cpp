#include "lumenis/interop/aaf_export.h"

#include "import_common.h"

#include "lumenis/core/branding.h"
#include "lumenis/core/timecode.h"
#include "lumenis/interop/aaf_writer.h"
#include "lumenis/model/sequence.h"

#include <QTimeZone>
#include <QUrl>

#include <map>
#include <set>

// AAF export of bins (docs/spec/aaf.md, "Export"): bin items of the native model laid out as Media
// Composer lays out its own AAF exports, the rules of aaf_import.cpp reversed. Master clips become a
// master mob, a file source mob per file and the tape or import source mob before them; subclips and
// sequences become composition mobs. The objects are built with aaf::Builder (aaf_writer.cpp), which
// writes the file. ctest aaf_round_trip imports, exports and imports every corpus file again and compares
// the results; tests/interop/tst_aaf.cpp (exportRoundTrip) checks a bin made in code.
namespace lumenis::interop::aaf {

namespace {

// Data and operation definitions (AAF Object Specification; the same
// AUIDs pyaaf2's model lists).
const QUuid kPicture = auidFromText("01030202-0100-0000-060e-2b3404010101");
const QUuid kSound = auidFromText("01030202-0200-0000-060e-2b3404010101");
const QUuid kTimecode = auidFromText("01030201-0100-0000-060e-2b3404010101");
const QUuid kVideoDissolve = auidFromText("0c3bea40-fc05-11d2-8a29-0050040ef7d2");
const QUuid kAudioDissolve = auidFromText("0c3bea41-fc05-11d2-8a29-0050040ef7d2");
// ContainerDef_AAF: the media is in this AAF file (embedded).
const QUuid kContainerAaf = auidFromText("4313b571-d8ba-11d2-809b-006008143e6f");
// Identification's ProductID, and the namespace of the AUIDs made for effects: name-based (version 5)
// UUIDs, the same on every export.
const QUuid kProductId = QUuid::createUuidV5(QUuid(QStringLiteral("{5d7b4c1e-3f0a-4a8e-9c2d-6b1f0e7a9d34}")),
                                             QStringLiteral("Cutthulhu AAF export"));

// A Rational record.
QVariantMap rational(const Rational& r) {
    return {{QStringLiteral("Numerator"), static_cast<qint64>(r.num())},
            {QStringLiteral("Denominator"), static_cast<qint64>(r.den())}};
}

// A TimeStamp record, in UTC, to the second.
QVariantMap timestamp(const QDateTime& when) {
    const QDateTime t = when.toUTC();
    return {{QStringLiteral("date"),
             QVariantMap{{QStringLiteral("year"), t.date().year()},
                         {QStringLiteral("month"), t.date().month()},
                         {QStringLiteral("day"), t.date().day()}}},
            {QStringLiteral("time"),
             QVariantMap{{QStringLiteral("hour"), t.time().hour()},
                         {QStringLiteral("minute"), t.time().minute()},
                         {QStringLiteral("second"), t.time().second()},
                         {QStringLiteral("fraction"), 0}}}};
}

// A timecode column ("01:00:00:00", ";" for drop frame) as frames at the rate (import_common.h).
std::optional<std::int64_t> frames(const QString& text, const Rational& rate, bool* drop) {
    return detail::timecodeFrames(text, rate, drop);
}

// Builds one file. run() writes master and audio clips first, then subclips, then sequences, so that each
// mob a later one names (by MobID and slot) is known by then; the Header, its Identification and the
// Dictionary's definitions come with them.
class Exporter {
public:
    explicit Exporter(const ExportOptions& options) : options_(options) {
        if (!options_.when.isValid()) options_.when = QDateTime::currentDateTimeUtc();
        if (options_.productName.isEmpty()) options_.productName = QString::fromLatin1(branding::kProductName);
    }

    ExportResult run(const QList<model::BinItem>& items, const QString& path) {
        header();
        for (const model::BinItem& item : items) {
            if (item.kind == model::BinItemKind::Sequence || item.kind == model::BinItemKind::Subclip) continue;
            if (item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip) {
                master(item);
            } else {
                note(QStringLiteral("items that are not clips or sequences left out"));
            }
        }
        for (const model::BinItem& item : items) {
            if (item.kind == model::BinItemKind::Subclip) subclip(item);
        }
        for (const model::BinItem& item : items) {
            if (item.kind == model::BinItemKind::Sequence) sequence(item);
        }
        ExportResult result;
        for (auto it = notes_.begin(); it != notes_.end(); ++it) {
            result.warnings << (it->second > 1 ? QStringLiteral("%1 × %2").arg(it->second).arg(it->first) : it->first);
        }
        const QStringList missing = b_.missing();
        if (!missing.isEmpty()) {
            result.error = QStringLiteral("required AAF properties not written: ") + missing.join(QLatin1String(", "));
            return result;
        }
        result.ok = b_.save(path, &result.error);
        return result;
    }

private:
    using Ref = Builder::Ref;

    void note(const QString& what) { ++notes_[what]; }

    // The Header (little-endian, "II") and one Identification naming the product.
    void header() {
        const Ref h = b_.header();
        b_.set(h, QStringLiteral("ByteOrder"), 0x4949);
        b_.set(h, QStringLiteral("LastModified"), timestamp(options_.when));
        b_.set(h, QStringLiteral("Version"), QVariantMap{{QStringLiteral("major"), 1}, {QStringLiteral("minor"), 1}});
        b_.set(h, QStringLiteral("ObjectModelVersion"), 1);
        const Ref id = b_.create(QStringLiteral("Identification"));
        b_.set(id, QStringLiteral("CompanyName"), QString::fromLatin1(branding::kOrganization));
        b_.set(id, QStringLiteral("ProductName"), options_.productName);
        b_.set(id, QStringLiteral("ProductVersionString"),
               options_.productVersion.isEmpty() ? QString::fromLatin1(branding::kVersion) : options_.productVersion);
        b_.set(id, QStringLiteral("ProductID"), kProductId);
        b_.set(id, QStringLiteral("Date"), timestamp(options_.when));
        b_.set(id, QStringLiteral("GenerationAUID"), QUuid::createUuid());
        b_.append(h, QStringLiteral("IdentificationList"), id);
    }

    // A DataDefinition, entered in the Dictionary once; its AUID, for weak references.
    QUuid dataDef(const QUuid& auid, const QString& name) {
        if (!defined_.count(auid)) {
            const Ref d = b_.create(QStringLiteral("DataDefinition"));
            b_.set(d, QStringLiteral("Identification"), auid);
            b_.set(d, QStringLiteral("Name"), name);
            b_.set(d, QStringLiteral("Description"), QString());
            b_.append(b_.dictionary(), QStringLiteral("DataDefinitions"), d);
            defined_.insert(auid);
        }
        return auid;
    }
    // ContainerDef_AAF, entered in the Dictionary once (embedded media's ContainerFormat).
    QUuid containerAaf() {
        if (!defined_.count(kContainerAaf)) {
            const Ref d = b_.create(QStringLiteral("ContainerDefinition"));
            b_.set(d, QStringLiteral("Identification"), kContainerAaf);
            b_.set(d, QStringLiteral("Name"), QStringLiteral("AAF"));
            b_.set(d, QStringLiteral("Description"), QString());
            b_.append(b_.dictionary(), QStringLiteral("ContainerDefinitions"), d);
            defined_.insert(kContainerAaf);
        }
        return kContainerAaf;
    }
    QUuid picture() { return dataDef(kPicture, QStringLiteral("Picture")); }
    QUuid sound() { return dataDef(kSound, QStringLiteral("Sound")); }
    QUuid timecode() { return dataDef(kTimecode, QStringLiteral("Timecode")); }
    QUuid kindDef(model::TrackKind kind) { return kind == model::TrackKind::Video ? picture() : sound(); }

    // An OperationDefinition for an effect, entered in the Dictionary once. Dissolves used as transitions
    // are AAF's VideoDissolve and MonoAudioDissolve; other effects get an AUID made from their name, so
    // that the same effect always has the same one.
    QUuid operation(const model::Effect& effect, bool video, bool transition) {
        QUuid auid;
        QString name;
        if (transition && effect.category == QLatin1String("Blend") && effect.name == QLatin1String("Dissolve")) {
            auid = video ? kVideoDissolve : kAudioDissolve;
            name = video ? QStringLiteral("VideoDissolve") : QStringLiteral("Audio Dissolve");
        } else {
            // Effects not matched to AAF's keep their names; those read
            // from AAF ("AAF" category) the name they came with.
            name = effect.category == QLatin1String("AAF") ? effect.name : effect.category + QLatin1Char('/') + effect.name;
            auid = QUuid::createUuidV5(QUuid(QStringLiteral("{5d7b4c1e-3f0a-4a8e-9c2d-6b1f0e7a9d34}")), name);
        }
        if (!defined_.count(auid)) {
            const Ref d = b_.create(QStringLiteral("OperationDefinition"));
            b_.set(d, QStringLiteral("Identification"), auid);
            b_.set(d, QStringLiteral("Name"), name);
            b_.set(d, QStringLiteral("Description"), QString());
            b_.setWeak(d, QStringLiteral("DataDefinition"), video ? picture() : sound());
            b_.set(d, QStringLiteral("IsTimeWarp"), false);
            b_.set(d, QStringLiteral("NumberInputs"), transition ? 2 : 1);
            b_.set(d, QStringLiteral("OperationCategory"), QStringLiteral("OperationCategory_Effect"));
            b_.append(b_.dictionary(), QStringLiteral("OperationDefinitions"), d);
            defined_.insert(auid);
        }
        return auid;
    }

    // A mob with its MobID, name and dates, added to the ContentStorage.
    Ref mob(const QString& className, const MobId& id, const QString& name, const QDateTime& created,
            const QDateTime& modified) {
        const Ref m = b_.create(className);
        b_.set(m, QStringLiteral("MobID"), QVariant::fromValue(id));
        b_.set(m, QStringLiteral("Name"), name);
        b_.set(m, QStringLiteral("CreationTime"), timestamp(created.isValid() ? created : options_.when));
        b_.set(m, QStringLiteral("LastModified"), timestamp(modified.isValid() ? modified : options_.when));
        b_.append(b_.content(), QStringLiteral("Mobs"), m);
        return m;
    }

    // A TimelineMobSlot holding `segment`, added to the mob's slots.
    Ref slot(Ref mob, int id, int physical, const Rational& rate, Ref segment, const QString& name = {}) {
        const Ref s = b_.create(QStringLiteral("TimelineMobSlot"));
        b_.set(s, QStringLiteral("SlotID"), id);
        b_.set(s, QStringLiteral("SlotName"), name);
        b_.set(s, QStringLiteral("PhysicalTrackNumber"), physical);
        b_.set(s, QStringLiteral("EditRate"), rational(rate));
        b_.set(s, QStringLiteral("Origin"), 0);
        b_.setObject(s, QStringLiteral("Segment"), segment);
        b_.append(mob, QStringLiteral("Slots"), s);
        return s;
    }

    // A SourceClip naming slot `sourceSlot` of mob `source` from `start`; a null MobID with slot 0 ends a
    // source chain.
    Ref sourceClip(const QUuid& def, std::int64_t length, const MobId& source, int sourceSlot, std::int64_t start) {
        const Ref c = b_.create(QStringLiteral("SourceClip"));
        b_.setWeak(c, QStringLiteral("DataDefinition"), def);
        b_.set(c, QStringLiteral("Length"), static_cast<qint64>(length));
        b_.set(c, QStringLiteral("SourceID"), QVariant::fromValue(source));
        b_.set(c, QStringLiteral("SourceMobSlotID"), sourceSlot);
        b_.set(c, QStringLiteral("StartTime"), static_cast<qint64>(start));
        return c;
    }

    // A Timecode segment: its FPS is the rate's nominal frames per second.
    Ref timecodeSegment(std::int64_t length, std::int64_t start, const Rational& rate, bool drop) {
        const Ref t = b_.create(QStringLiteral("Timecode"));
        b_.setWeak(t, QStringLiteral("DataDefinition"), timecode());
        b_.set(t, QStringLiteral("Length"), static_cast<qint64>(length));
        b_.set(t, QStringLiteral("Start"), static_cast<qint64>(start));
        b_.set(t, QStringLiteral("FPS"), TimecodeFormat::forRate(rate, false).nominalFps);
        b_.set(t, QStringLiteral("Drop"), drop);
        return t;
    }

    // The media's path as a NetworkLocator's file URL.
    void locator(Ref descriptor, const QString& path) {
        if (path.isEmpty()) return;
        const Ref l = b_.create(QStringLiteral("NetworkLocator"));
        b_.set(l, QStringLiteral("URLString"), QUrl::fromLocalFile(path).toString());
        b_.append(descriptor, QStringLiteral("Locator"), l);
    }

    // The clip's tracks in slot order: its Tracks column, video first.
    static QStringList tracksOf(const model::BinItem& item) {
        QStringList names = model::parseTrackList(item.fields.value(QStringLiteral("Tracks")));
        if (names.isEmpty()) names = {item.kind == model::BinItemKind::AudioClip ? QStringLiteral("A1") : QStringLiteral("V1")};
        return names;
    }

    // A picture file's descriptor, from what the clip's columns say.
    // The picture size comes from the Format column's height ("1080i", "720p"), 1080 when there is none;
    // 16:9 from 720 lines up, else 720 wide and 4:3. The other values describe 8-bit 4:2:2 full frames.
    Ref pictureDescriptor(const model::BinItem& item, std::int64_t length) {
        const detail::PictureSize size = detail::pictureSizeOf(item.fields.value(QStringLiteral("Format")));
        const int height = size.height;
        const int width = size.width;
        const Ref d = b_.create(QStringLiteral("CDCIDescriptor"));
        b_.set(d, QStringLiteral("SampleRate"), rational(item.editRate));
        b_.set(d, QStringLiteral("Length"), static_cast<qint64>(length));
        b_.set(d, QStringLiteral("StoredHeight"), height);
        b_.set(d, QStringLiteral("StoredWidth"), width);
        b_.set(d, QStringLiteral("FrameLayout"), QStringLiteral("FullFrame"));
        b_.set(d, QStringLiteral("VideoLineMap"), QVariantList{0, 0});
        b_.set(d, QStringLiteral("ImageAspectRatio"), rational(size.wide ? Rational(16, 9) : Rational(4, 3)));
        b_.set(d, QStringLiteral("ComponentWidth"), 8);
        b_.set(d, QStringLiteral("HorizontalSubsampling"), 2);
        return d;
    }

    // A sound file's descriptor: one channel of 24-bit 48 kHz PCM.
    Ref soundDescriptor(const model::BinItem& item, std::int64_t length) {
        const Ref d = b_.create(QStringLiteral("PCMDescriptor"));
        b_.set(d, QStringLiteral("SampleRate"), rational(item.editRate));
        b_.set(d, QStringLiteral("Length"), static_cast<qint64>(length));
        b_.set(d, QStringLiteral("AudioSamplingRate"), rational(Rational(48000)));
        b_.set(d, QStringLiteral("Channels"), 1);
        b_.set(d, QStringLiteral("QuantizationBits"), 24);
        b_.set(d, QStringLiteral("BlockAlign"), 3);
        b_.set(d, QStringLiteral("AverageBPS"), 48000 * 3);
        return d;
    }

    // The descriptor of embedded media and its EssenceData in the ContentStorage, its Data the essence's file
    // (Builder::setStream copies it when the AAF file is written). PCM as a PCMDescriptor of the samples; DNx as a
    // CDCIDescriptor with the frame header's size, depth, scan and compression label (as pyaaf2's
    // import_dnxhd_essence and import_audio_essence write them). The descriptor; -1, with nothing made, for other
    // kinds.
    Ref embedded(const MobId& id, const Essence& e, const model::BinItem& item) {
        Ref d = -1;
        if (e.kind == Essence::Kind::Pcm && e.channels > 0 && e.sampleRate > 0 && e.bits > 0) {
            d = b_.create(QStringLiteral("PCMDescriptor"));
            const int block = e.channels * ((e.bits + 7) / 8);
            b_.set(d, QStringLiteral("SampleRate"), rational(e.rate));
            b_.set(d, QStringLiteral("Length"), static_cast<qint64>(e.length));
            b_.set(d, QStringLiteral("AudioSamplingRate"), rational(Rational(e.sampleRate)));
            b_.set(d, QStringLiteral("Channels"), e.channels);
            b_.set(d, QStringLiteral("QuantizationBits"), e.bits);
            b_.set(d, QStringLiteral("BlockAlign"), block);
            b_.set(d, QStringLiteral("AverageBPS"), e.sampleRate * block);
        } else if (e.kind == Essence::Kind::Dnx && e.dnx.width > 0) {
            d = b_.create(QStringLiteral("CDCIDescriptor"));
            b_.set(d, QStringLiteral("SampleRate"), rational(e.rate));
            b_.set(d, QStringLiteral("Length"), static_cast<qint64>(e.length));
            b_.set(d, QStringLiteral("StoredWidth"), e.dnx.width);
            // A field's lines when the frames are interlaced (SeparateFields), as Media Composer describes them.
            b_.set(d, QStringLiteral("StoredHeight"), e.dnx.interlaced ? e.dnx.height / 2 : e.dnx.height);
            b_.set(d, QStringLiteral("FrameLayout"), e.dnx.interlaced ? QStringLiteral("SeparateFields") : QStringLiteral("FullFrame"));
            b_.set(d, QStringLiteral("VideoLineMap"), QVariantList{e.dnx.interlaced ? 21 : 42, e.dnx.interlaced ? 584 : 0});
            const detail::PictureSize size = detail::pictureSizeOf(item.fields.value(QStringLiteral("Format")));
            const bool wide = e.dnx.width * 9 >= e.dnx.height * 16 || size.wide;
            b_.set(d, QStringLiteral("ImageAspectRatio"), rational(wide ? Rational(16, 9) : Rational(4, 3)));
            b_.set(d, QStringLiteral("ComponentWidth"), e.dnx.bitDepth);
            b_.set(d, QStringLiteral("HorizontalSubsampling"), 2);
            if (const QUuid c = dnxCompression(e.dnx.cid); !c.isNull()) b_.set(d, QStringLiteral("Compression"), c);
            if (e.length > 0 && e.bytes % static_cast<std::uint64_t>(e.length) == 0) {
                b_.set(d, QStringLiteral("FrameSampleSize"), static_cast<qint64>(e.bytes / static_cast<std::uint64_t>(e.length)));
            }
        } else {
            return -1;
        }
        b_.setWeak(d, QStringLiteral("ContainerFormat"), containerAaf());
        const Ref data = b_.create(QStringLiteral("EssenceData"));
        b_.set(data, QStringLiteral("MobID"), QVariant::fromValue(id));
        b_.setStream(data, QStringLiteral("Data"), {}, e.file);
        b_.append(b_.content(), QStringLiteral("EssenceData"), data);
        return d;
    }

    // The columns that are not standard headings, as TaggedValue user comments holding strings, sorted by
    // name.
    void userComments(Ref m, const model::BinItem& item) {
        QStringList names = item.fields.keys();
        std::sort(names.begin(), names.end());
        for (const QString& name : names) {
            if (model::binHeadings().contains(name)) continue;
            const Ref c = b_.create(QStringLiteral("TaggedValue"));
            b_.set(c, QStringLiteral("Name"), name);
            b_.setIndirect(c, QStringLiteral("Value"), QStringLiteral("aafString"), item.fields.value(name));
            b_.append(m, QStringLiteral("UserComments"), c);
        }
    }

    // A master clip and its sources. The source mobs come first, and each only once across clips
    // (`written_`): the tape or import source (made up as a tape when the clip has a Start but no such
    // source), whose slots are the clip's tracks in order and then a timecode slot with the Start; then a
    // file source mob for each file, its slots naming the tape's or import's; then the master mob, a slot
    // per track naming its file's slot. `slotsOf_` keeps the track order, as later mobs name slots by
    // number.
    void master(const model::BinItem& item) {
        const MobId id = model::mobIdOf(item);
        const QStringList tracks = tracksOf(item);
        bool drop = false;
        const std::optional<std::int64_t> start = frames(item.fields.value(QStringLiteral("Start")), item.editRate, &drop);

        // The tape or import source: its slots are the clip's tracks, then
        // its timecode, which gives the clip its Start.
        const model::ClipSource* before = nullptr;
        for (const model::ClipSource& s : item.sources) {
            if (s.kind == model::ClipSource::Kind::Tape || s.kind == model::ClipSource::Kind::Import) before = &s;
        }
        model::ClipSource made;
        if (!before && start) {
            made.kind = model::ClipSource::Kind::Tape;
            made.mob = MobId::generate();
            made.name = item.fields.value(QStringLiteral("Tape"), item.name);
            before = &made;
        }
        if (before && !written_.count(before->mob)) {
            const Ref m = mob(QStringLiteral("SourceMob"), before->mob, before->name, item.created, item.modified);
            const bool tape = before->kind == model::ClipSource::Kind::Tape;
            const Ref d = b_.create(tape ? QStringLiteral("TapeDescriptor") : QStringLiteral("ImportDescriptor"));
            if (!tape) locator(d, before->path);
            b_.setObject(m, QStringLiteral("EssenceDescription"), d);
            int n = 1;
            for (const QString& t : tracks) {
                const auto ref = model::parseTrackName(t);
                const QUuid def = kindDef(ref ? ref->kind : model::TrackKind::Video);
                slot(m, n++, ref ? ref->number : 1, item.editRate, sourceClip(def, item.durationFrames, MobId(), 0, 0));
            }
            if (start) slot(m, n, 1, item.editRate, timecodeSegment(item.durationFrames, *start, item.editRate, drop));
            written_.insert(before->mob);
        }

        // Each file source: one mob for the tracks it carries.
        std::map<MobId, QStringList> files;
        std::map<QString, const model::ClipSource*> fileOf;
        for (const model::ClipSource& s : item.sources) {
            if (s.kind != model::ClipSource::Kind::File) continue;
            files[s.mob] << s.track;
            fileOf[s.track] = &s;
        }
        for (const auto& [fileId, fileTracks] : files) {
            if (written_.count(fileId)) continue;
            const model::ClipSource* s = fileOf.at(fileTracks.first());
            const auto first = model::parseTrackName(fileTracks.first());
            const bool video = first && first->kind == model::TrackKind::Video;
            const Ref m = mob(QStringLiteral("SourceMob"), fileId, s->name, item.created, item.modified);
            // Embedded in AAF: the descriptor says what the media is, an EssenceData holds it (no locator).
            const std::optional<Essence> essence = options_.embed ? options_.embed(item, *s) : std::nullopt;
            Ref d = essence ? embedded(fileId, *essence, item) : -1;
            if (d < 0) {
                d = video ? pictureDescriptor(item, item.durationFrames) : soundDescriptor(item, item.durationFrames);
                locator(d, s->path);
            }
            b_.setObject(m, QStringLiteral("EssenceDescription"), d);
            int n = 1;
            for (const QString& t : fileTracks) {
                const auto ref = model::parseTrackName(t);
                const QUuid def = kindDef(ref ? ref->kind : model::TrackKind::Video);
                const int fromSlot = static_cast<int>(tracks.indexOf(t)) + 1;
                slot(m, n++, ref ? ref->number : 1, item.editRate,
                     sourceClip(def, item.durationFrames, before ? before->mob : MobId(), before ? fromSlot : 0, 0));
            }
            written_.insert(fileId);
        }

        const Ref m = mob(QStringLiteral("MasterMob"), id, item.name, item.created, item.modified);
        int n = 1;
        for (const QString& t : tracks) {
            const auto ref = model::parseTrackName(t);
            const QUuid def = kindDef(ref ? ref->kind : model::TrackKind::Video);
            MobId file;
            int fileSlot = 0;
            if (const auto it = fileOf.find(t); it != fileOf.end()) {
                file = it->second->mob;
                fileSlot = static_cast<int>(files.at(file).indexOf(t)) + 1;
            } else if (before) {
                // No file: the clip comes straight from its tape or import.
                file = before->mob;
                fileSlot = n;
            }
            slot(m, n++, ref ? ref->number : 1, item.editRate, sourceClip(def, item.durationFrames, file, fileSlot, 0), item.name);
        }
        userComments(m, item);
        slotsOf_[id] = tracks;
    }

    // A composition mob used as Usage_SubClip: each slot names the master's slot of the same track, from
    // the subclip's offset in the master.
    void subclip(const model::BinItem& item) {
        const MobId id = model::mobIdOf(item);
        const QStringList tracks = tracksOf(item);
        const model::ClipSource* master = nullptr;
        for (const model::ClipSource& s : item.sources) {
            if (s.kind == model::ClipSource::Kind::Clip) master = &s;
        }
        const Ref m = mob(QStringLiteral("CompositionMob"), id, item.name, item.created, item.modified);
        b_.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_SubClip"));
        int n = 1;
        for (const QString& t : tracks) {
            const auto ref = model::parseTrackName(t);
            const QUuid def = kindDef(ref ? ref->kind : model::TrackKind::Video);
            const QStringList masterTracks = master && slotsOf_.count(master->mob) ? slotsOf_.at(master->mob) : tracks;
            slot(m, n++, ref ? ref->number : 1, item.editRate,
                 sourceClip(def, item.durationFrames, master ? master->mob : MobId(),
                            static_cast<int>(masterTracks.indexOf(t)) + 1, master ? master->offset : 0));
        }
        userComments(m, item);
        slotsOf_[id] = tracks;
    }

    // A track's segments as components: transitions moved back over the
    // cut they sit at (the import's rule, reversed).
    Ref trackSequence(const model::Track& track, bool upper) {
        const bool video = track.kind == model::TrackKind::Video;
        const QUuid def = video ? picture() : sound();
        const Ref seq = b_.create(QStringLiteral("Sequence"));
        b_.setWeak(seq, QStringLiteral("DataDefinition"), def);
        QList<model::Segment> segments = track.segments;
        QList<std::int64_t> lengths;
        QList<std::int64_t> ins;
        for (const model::Segment& s : segments) {
            lengths << s.length;
            ins << s.sourceIn;
        }
        // The transition's frames after the cut lengthen the segment before it, and its frames before the
        // cut lengthen the one after, which then starts that much earlier in its source.
        for (qsizetype i = 0; i < segments.size(); ++i) {
            if (!segments[i].transition) continue;
            if (i == 0) {
                note(QStringLiteral("transitions at the start of a track left out"));
                segments[i].transition.reset();
                continue;
            }
            const model::Transition& t = *segments[i].transition;
            lengths[i - 1] += t.after();
            lengths[i] += t.before();
            if (!segments[i].isFiller()) ins[i] -= t.before();
        }
        std::int64_t total = 0;
        // One segment's component: Filler (or a ScopeReference to the track below, slot 1 of the same
        // scope, on the tracks above V1) or a SourceClip naming the clip's slot for the segment's source
        // track; wrapped in an OperationGroup when the segment has an effect.
        auto material = [&](const model::Segment& s, std::int64_t length, std::int64_t in) -> Ref {
            Ref c;
            if (s.isFiller()) {
                // On the tracks above V1 the track below shows through.
                c = b_.create(upper ? QStringLiteral("ScopeReference") : QStringLiteral("Filler"));
                b_.setWeak(c, QStringLiteral("DataDefinition"), def);
                b_.set(c, QStringLiteral("Length"), static_cast<qint64>(length));
                if (upper) {
                    b_.set(c, QStringLiteral("RelativeScope"), 0);
                    b_.set(c, QStringLiteral("RelativeSlot"), 1);
                }
            } else {
                const QStringList clipTracks = slotsOf_.count(s.sourceMobId()) ? slotsOf_.at(s.sourceMobId()) : QStringList{};
                qsizetype slotIndex = clipTracks.indexOf(s.sourceTrack);
                if (slotIndex < 0) slotIndex = model::parseTrackName(s.sourceTrack) ? model::parseTrackName(s.sourceTrack)->number - 1 : 0;
                c = sourceClip(def, length, s.sourceMobId(), static_cast<int>(slotIndex) + 1, in);
            }
            if (!s.effect) return c;
            const Ref group = b_.create(QStringLiteral("OperationGroup"));
            b_.setWeak(group, QStringLiteral("DataDefinition"), def);
            b_.set(group, QStringLiteral("Length"), static_cast<qint64>(length));
            b_.setWeak(group, QStringLiteral("Operation"), operation(*s.effect, video, false));
            b_.append(group, QStringLiteral("InputSegments"), c);
            return group;
        };
        // Each transition goes before the segment it sits on, its OperationGroup naming the effect. A
        // Sequence's length is its components' lengths less its transitions', which overlap them.
        for (qsizetype i = 0; i < segments.size(); ++i) {
            const model::Segment& s = segments[i];
            if (s.transition) {
                const Ref t = b_.create(QStringLiteral("Transition"));
                b_.setWeak(t, QStringLiteral("DataDefinition"), def);
                b_.set(t, QStringLiteral("Length"), static_cast<qint64>(s.transition->duration));
                b_.set(t, QStringLiteral("CutPoint"), static_cast<qint64>(s.transition->before()));
                const Ref group = b_.create(QStringLiteral("OperationGroup"));
                b_.setWeak(group, QStringLiteral("DataDefinition"), def);
                b_.set(group, QStringLiteral("Length"), static_cast<qint64>(s.transition->duration));
                b_.setWeak(group, QStringLiteral("Operation"), operation(s.transition->effect, video, true));
                b_.setObject(t, QStringLiteral("OperationGroup"), group);
                b_.append(seq, QStringLiteral("Components"), t);
                total -= s.transition->duration;
            }
            b_.append(seq, QStringLiteral("Components"), material(s, lengths[i], ins[i]));
            total += lengths[i];
        }
        b_.set(seq, QStringLiteral("Length"), static_cast<qint64>(total));
        return seq;
    }

    // A sequence as a composition mob used as Usage_TopLevel: a timecode slot (TC1) with the Start, one
    // NestedScope slot holding the video tracks, then a slot per audio track. The Export options leave
    // video or audio tracks out.
    void sequence(const model::BinItem& item) {
        const model::Sequence seq = model::Sequence::fromItem(item);
        const Ref m = mob(QStringLiteral("CompositionMob"), model::mobIdOf(item), item.name, item.created, item.modified);
        b_.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
        const std::int64_t duration = seq.duration();
        int n = 1;
        if (seq.hasTimecodeTrack()) {
            bool drop = false;
            const std::int64_t start = frames(item.fields.value(QStringLiteral("Start")), item.editRate, &drop).value_or(0);
            slot(m, n++, 1, item.editRate, timecodeSegment(duration, start, item.editRate, drop));
        }
        // Media Composer's layout: all video tracks in one nested scope,
        // V1 first; every track number up to the highest is there.
        int highest = 0;
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video && options_.includeVideo) highest = std::max(highest, t.number);
        }
        if (highest > 0) {
            const Ref scope = b_.create(QStringLiteral("NestedScope"));
            b_.setWeak(scope, QStringLiteral("DataDefinition"), picture());
            b_.set(scope, QStringLiteral("Length"), static_cast<qint64>(duration));
            for (int v = 1; v <= highest; ++v) {
                const model::Track* t = seq.track(model::trackName(model::TrackKind::Video, v));
                model::Track empty{model::TrackKind::Video, v, model::AudioFormat::Mono, {model::Segment::filler(duration)}};
                b_.append(scope, QStringLiteral("Slots"), trackSequence(t ? *t : empty, v > 1));
            }
            slot(m, n++, 1, item.editRate, scope);
        }
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Audio || !options_.includeAudio) continue;
            slot(m, n++, t.number, item.editRate, trackSequence(t, false));
        }
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Data && options_.includeVideo) note(QStringLiteral("data tracks left out"));
        }
        userComments(m, item);
    }

    ExportOptions options_;
    Builder b_;
    std::set<QUuid> defined_;
    std::set<MobId> written_;
    std::map<MobId, QStringList> slotsOf_;
    std::map<QString, int> notes_;
};

} // namespace

ExportResult exportItems(const QList<model::BinItem>& items, const QString& path, const ExportOptions& options) {
    return Exporter(options).run(items, path);
}

} // namespace lumenis::interop::aaf
