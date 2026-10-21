#include "lumenis/interop/avb_export.h"

#include "import_common.h"

#include "lumenis/core/branding.h"
#include "lumenis/interop/avb_writer.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <map>
#include <set>

// Native bins out as Media Composer bins (docs/spec/avb.md, "Export"): the import's rules
// (avb_import.cpp) in reverse, with the objects laid out as Media Composer's own bins hold them (the
// corpus: scripts/interop-corpus.py; `cutthulhu --dump-avb` shows them), and the layout of the AAF
// export (aaf_export.cpp), which writes the same mobs:
//
//   ABIN ── BVst (the bin view)
//     │
//     ├─ items: master mob (CMPO 2) ── tracks: SCLP ─→ file source mob (CMPO 3: CDCI/PCMA, MSML or
//     │         FILE locator) ── SCLP ─→ tape (MDTP) or import (MDES, FILE) source mob, TCCP
//     ├─ subclips (CMPO 1, usage 2) ── SCLP ─→ the master mob, from the subclip's offset
//     └─ sequences (CMPO 1, usage 0) ── TCCP (TC1), then a SEQU per track: FILL, SCLP, TNFX at
//        cuts, track effects (TKFX, SPED) around a segment's clip
//
// Every mob is an item of the bin: the user's items placed (user_placed), the source mobs and the
// reference clips (ExportOptions::resolve) not. Objects are added to avb::Builder as they are made;
// the ABIN comes last, as in Media Composer's bins, and is the root. What the model holds that the
// import would not read back, or that has no place in a bin we know of, is left out with a warning,
// once per kind (ExportResult::warnings). ctest avb_export_round_trip and avb_export_pyavb check the
// result against the corpus; tests/interop/tst_avb.cpp (exportsBin) a bin made in code.
namespace lumenis::interop::avb {

namespace {

using detail::SourceTimecode;
using model::BinItem;
using model::BinItemKind;
using model::ClipSource;

// Media kinds (Component's media_kind_id, as pyavb names them).
constexpr int kNone = 0;
constexpr int kPicture = 1;
constexpr int kSound = 2;
constexpr int kTimecode = 3;

// Mob types and the composition mob usages the import reads (CMPO's mob_type_id, usage_code).
constexpr int kComposition = 1;
constexpr int kMaster = 2;
constexpr int kSource = 3;
constexpr int kUsageSubclip = 2;

// MDES's mob_kind for file, tape and import source mobs, as Media Composer's bins (file 1, import 5)
// and pyavb's test_create.py (a tape: 2) have them.
constexpr int kFileKind = 1;
constexpr int kTapeKind = 2;
constexpr int kImportKind = 5;

// A filler proxy's (TRKR) length in Media Composer's bins: the largest s32.
constexpr qint64 kForever = 2147483647;

Property prop(const char* name, Value value) { return {QString::fromLatin1(name), std::move(value)}; }

// Media Composer's bin view columns (BVst): heading, format and column type, as every bin of the
// corpus lists them (test_file_01.avb's view, less the media resolutions' columns, whose types differ
// from bin to bin). Black-box observation; the format codes' meaning is not known.
struct Column {
    const char* title;
    int format;
    int type;
};
constexpr Column kColumns[] = {
    {"Color", 105, 51},
    {"   ", 0, 200},
    {"Name", 2, 201},
    {"Auxiliary TC3", 100, 27},
    {"Auxiliary TC4", 100, 28},
    {"Color Framing", 105, 32},
    {"Media Status", 0, 130},
    {"Frame", 106, 36},
    {"Auxiliary TC2", 100, 26},
    {"CFPS", 103, 10},
    {"Creation Date", 102, 12},
    {"Film TC", 100, 23},
    {"KN Mark IN", 101, 18},
    {"KN End", 101, 16},
    {"End", 100, 3},
    {"Ink Number", 101, 30},
    {"Auxiliary Ink", 101, 37},
    {"Perf", 1, 49},
    {"FPS", 103, 9},
    {"Duration", 100, 4},
    {"Project", 2, 204},
    {"Offline", 2, 35},
    {"KN Dur", 101, 17},
    {"KN IN-OUT", 101, 20},
    {"Pullin", 2, 21},
    {"KN Start", 101, 15},
    {"Pullout", 2, 22},
    {"KN Mark OUT", 101, 19},
    {"Sound TC", 100, 24},
    {"Drive", 2, 14},
    {"IN-OUT", 100, 7},
    {"Camera", 2, 46},
    {"Slip", 100, 47},
    {"Scene", 2, 39},
    {"Reel", 2, 134},
    {"Labroll", 2, 45},
    {"Shoot Date", 2, 44},
    {"VITC", 100, 31},
    {"Soundroll", 2, 42},
    {"Lock", 0, 48},
    {"Auxiliary TC1", 100, 25},
    {"Reel #", 2, 38},
    {"Take", 2, 41},
    {"Camroll", 2, 43},
    {"Mark IN", 100, 5},
    {"Data", 2, 30001},
    {"Modified Date", 102, 54},
    {"TC 25", 100, 58},
    {"TC 24", 100, 56},
    {"TC 25PD", 100, 57},
    {"Audio Format", 2, 55},
    {"TC 30", 100, 59},
    {"Audio Bit Depth", 100, 61},
    {"Cadence", 105, 62},
    {"Aux TC 24", 100, 63},
    {"Video File Format", 2, 64},
    {"Video Data Rate", 2, 139},
    {"Format", 2, 65},
    {"AuxInk Dur", 2, 66},
    {"AuxInk Edge", 105, 67},
    {"AuxInk End", 2, 68},
    {"AuxInk Film", 105, 69},
    {"DPX", 101, 70},
    {"VFX", 101, 71},
    {"VFX Reel", 2, 72},
    {"Ink Dur", 2, 73},
    {"Frame Count Start", 101, 135},
    {"Frame Count End", 101, 136},
    {"Frame Count Duration", 101, 137},
    {"Ink End", 2, 75},
    {"Ink Edge", 105, 74},
    {"Ink Film", 105, 76},
    {"KN Film", 105, 78},
    {"LUT", 2, 79},
    {"Master Dur", 2, 80},
    {"Master Edge", 105, 81},
    {"Master End", 2, 82},
    {"Master Film", 105, 83},
    {"Master Start", 101, 84},
    {"Transfer", 101, 85},
    {"UNC Path", 2, 86},
    {"TC 30NP", 100, 87},
    {"TC 60", 100, 88},
    {"Disk Label", 2, 89},
    {"Journalist", 2, 90},
    {"Production", 2, 91},
    {"IDataLink", 2, 92},
    {"ASC_SOP", 2, 93},
    {"ASC_SAT", 2, 94},
    {"Auxiliary TC5", 100, 29},
    {"AFD", 0, 131},
    {"Field Motion", 105, 95},
    {"Track Formats", 2, 96},
    {"Color Space", 2, 97},
    {"Chroma Subsampling", 2, 144},
    {"Image Aspect Ratio", 105, 98},
    {"Pixel Aspect Ratio", 2, 99},
    {"Reformat", 105, 100},
    {"Source File", 2, 101},
    {"Media File Path", 2, 145},
    {"Ancillary Data", 2, 102},
    {"Source Path", 2, 105},
    {"Color Transformation", 2, 103},
    {"S3D Contributors", 2, 106},
    {"S3D Leading Eye", 105, 107},
    {"S3D Channel", 105, 108},
    {"S3D Group Name", 2, 109},
    {"S3D Clip Name", 2, 104},
    {"S3D Inversion", 105, 110},
    {"S3D InversionR", 105, 111},
    {"S3D Eye Order", 105, 112},
    {"S3D Alignment", 105, 125},
    {"Marker", 0, 132},
    {"Vendor Name", 2, 113},
    {"Vendor URL", 2, 114},
    {"Vendor Asset ID", 2, 115},
    {"Vendor Asset Name", 2, 116},
    {"Vendor Asset Rights", 2, 117},
    {"Vendor Asset Price", 2, 118},
    {"Vendor Original Master", 2, 119},
    {"Vendor Asset Description", 2, 120},
    {"Vendor Asset Keywords", 2, 121},
    {"Vendor Asset Status", 2, 122},
    {"Vendor Invoice ID", 2, 123},
    {"Vendor Download Master", 2, 124},
    {"Image Framing", 2, 127},
    {"Raster Dimension", 2, 128},
    {"Image Size", 2, 129},
    {"BIN Start", 100, 141},
    {"BIN End", 100, 142},
    {"BIN Tracks", 2, 143},
    {"Mark OUT", 100, 6},
    {"Tracks", 2, 1},
    {"Start", 100, 2},
    {"Tape", 2, 8},
    {"Video", 2, 13},
    {"Plug-in", 2, 133},
    {"TapeID", 2, 50},
    {"Audio SR", 2, 11},
    {"Comments", 2, 40},
};

// The index in kColumns of a heading of ours (the guide's names, p.274-278), matched without regard
// to case ("Plug-In" is Media Composer's "Plug-in"); -1 when Media Composer has no such column.
int columnIndex(const QString& heading) {
    for (std::size_t i = 0; i < std::size(kColumns); ++i) {
        if (heading.compare(QLatin1String(kColumns[i].title), Qt::CaseInsensitive) == 0) return static_cast<int>(i);
    }
    return -1;
}

// An edit rate as a bin stores it (a double): whole rates as they are, others to a thousandth
// (24000/1001 is 23.976, as Media Composer writes it), which the import reads back exactly
// (detail::exactNtscRate).
double rateValue(const Rational& r) {
    if (r.den() == 1) return static_cast<double>(r.num());
    return std::round(r.toDouble() * 1000.0) / 1000.0;
}

// Effect IDs Media Composer's bins use for dissolves (the import reads them as Blend/Dissolve).
const QString kVideoDissolve = QStringLiteral("EFF2_BLEND_DISSOLVE");
const QString kAudioDissolve = QStringLiteral("BLEND_AUDIO_DISSOLVE");

// The kind of a track named in the model ("V1", "A2"); kNone for others.
int mediaKindOf(const QString& track) {
    const auto ref = model::parseTrackName(track);
    if (!ref) return kNone;
    return ref->kind == model::TrackKind::Video ? kPicture : ref->kind == model::TrackKind::Audio ? kSound : kNone;
}

// Whether a file is managed media: in an "Avid MediaFiles/MXF" folder (managed.h). Media Composer
// names such files by MobID (an MSML locator) and keeps their last known path as the physical media.
bool isManaged(const QString& path) {
    return QDir::fromNativeSeparators(path).contains(QLatin1String("/Avid MediaFiles/MXF/"), Qt::CaseInsensitive);
}

// Builds one bin. run() first gathers the items to write (the bin's, then the reference clips
// the resolver finds), then writes master and audio clips, subclips and sequences in that order, as
// the AAF export does, then the view and the bin.
class Exporter {
public:
    explicit Exporter(const ExportOptions& options) : options_(options) {
        if (!options_.when.isValid()) options_.when = QDateTime::currentDateTimeUtc();
        if (options_.productName.isEmpty()) options_.productName = QString::fromLatin1(branding::kProductName);
        if (options_.productVersion.isEmpty()) options_.productVersion = QString::fromLatin1(branding::kVersion);
        Header header;
        header.littleEndian = options_.littleEndian;
        header.lastSave = Time{seconds(options_.when)};
        header.creatorVersion = roman(options_.productName + QLatin1Char(' ') + options_.productVersion).left(30);
        b_ = Builder(header);
    }

    QByteArray run(const model::Bin& bin, ExportResult* result) {
        QList<BinItem> items = gather(bin.items, result);
        for (const BinItem& item : items) {
            switch (item.kind) {
            case BinItemKind::MasterClip:
            case BinItemKind::AudioClip: master(item); break;
            case BinItemKind::Subclip:
            case BinItemKind::Sequence: break;
            default: note(QStringLiteral("%1 left out").arg(model::kindLabel(item.kind).toLower()));
            }
        }
        for (const BinItem& item : items) {
            if (item.kind == BinItemKind::Subclip) subclip(item);
        }
        for (const BinItem& item : items) {
            if (item.kind == BinItemKind::Sequence) sequence(item);
        }
        if (missing_ > 0) {
            notes_[QStringLiteral("segments and subclips of clips not in the bin (offline where it is read)")] += missing_;
        }
        if (!derived_.empty()) {
            QStringList names(derived_.begin(), derived_.end());
            note(QStringLiteral("columns Media Composer works out from the media not written: %1").arg(names.join(QLatin1String(", "))));
        }
        const std::uint32_t view = binView(bin.display);
        binObject(bin, view);
        for (auto it = notes_.begin(); it != notes_.end(); ++it) {
            result->warnings << (it->second > 1 ? QStringLiteral("%1 × %2").arg(it->second).arg(it->first) : it->first);
        }
        QString error;
        const QByteArray bytes = b_.toBytes(&error);
        if (bytes.isEmpty()) {
            result->error = error;
            return {};
        }
        result->ok = true;
        return bytes;
    }

private:
    // One mob listed in the bin, with its item's user-placed flag.
    struct Listed {
        std::uint32_t mob = 0;
        bool placed = true;
    };

    void note(const QString& what) { ++notes_[what]; }

    static std::uint32_t seconds(const QDateTime& t) {
        return static_cast<std::uint32_t>(std::clamp<qint64>(t.toSecsSinceEpoch(), 0, 0xFFFFFFFFLL));
    }
    Value timeOf(const QDateTime& t) const { return Value::time(seconds(t.isValid() ? t : options_.when)); }

    // Text as bins store it, Mac OS Roman: characters it cannot hold (and pairs of UTF-16 surrogates)
    // become '?', with a warning. Mob and component names have no UTF-8 form in the bin (pyavb reads
    // none); only file paths do, and those are written in UTF-8 as well (fileLocator).
    QString roman(const QString& text, bool warn = true) {
        if (toMacRoman(text)) return text;
        QString out;
        for (qsizetype i = 0; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
                out += QLatin1Char('?');
                ++i;
            } else {
                out += toMacRoman(QString(c)) ? c : QLatin1Char('?');
            }
        }
        if (warn) note(QStringLiteral("names or columns with characters Mac OS Roman cannot hold, written with ?"));
        return out;
    }

    // ---- the items ----

    // The bin's items, then the clips they need that are not among them (a sequence's clips, a
    // subclip's master), found by the resolver and listed as reference clips; and again for those.
    // A mob listed twice is written once.
    QList<BinItem> gather(const QList<BinItem>& binItems, ExportResult* result) {
        QList<BinItem> items;
        for (const BinItem& item : binItems) {
            const MobId id = model::mobIdOf(item);
            if (known_.count(id)) {
                note(QStringLiteral("items with the same MobID as another written once"));
                continue;
            }
            known_.insert(id);
            items << item;
        }
        std::set<MobId> asked;
        for (qsizetype i = 0; i < items.size(); ++i) {
            QList<std::pair<MobId, QString>> needs;
            const BinItem item = items.at(i);
            if (item.kind == BinItemKind::Subclip) {
                for (const ClipSource& s : item.sources) {
                    if (s.kind == ClipSource::Kind::Clip) needs.append({s.mob, QString()});
                }
            } else if (item.kind == BinItemKind::Sequence) {
                const model::Sequence seq = model::Sequence::fromItem(item);
                for (const model::Track& t : seq.tracks()) {
                    for (const model::Segment& s : t.segments) {
                        if (!s.isFiller()) needs.append({s.sourceMobId(), s.clipId});
                    }
                }
            }
            for (const auto& [mob, clipId] : needs) {
                if (mob.isNull() || known_.count(mob) || asked.count(mob) || !options_.resolve) continue;
                asked.insert(mob);
                std::optional<BinItem> found = options_.resolve(mob, clipId);
                if (!found || known_.count(model::mobIdOf(*found))) continue;
                found->userPlaced = false;
                known_.insert(model::mobIdOf(*found));
                items << *found;
                ++result->referenceClips;
            }
        }
        return items;
    }

    // What every item's mob has: what the model keeps that a bin cannot (with a warning), and its
    // attributes: the user columns (_USER) with their text. Columns the bin's structure carries
    // (Start, End, Tracks, Tape) are not repeated; standard headings that cannot be typed into
    // (p.274-278: Video, Format, ...) Media Composer works out from the media, and are left out.
    std::uint32_t attributes(const BinItem& item) {
        if (!item.color.isEmpty()) note(QStringLiteral("clip colours not written"));
        if (item.locked) note(QStringLiteral("locks (Lock Bin Selection) not written"));
        if (!item.markers.isEmpty()) note(QStringLiteral("markers not written"));
        if (!item.script.isEmpty()) note(QStringLiteral("Script view text not written"));
        static const QSet<QString> structural{QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("Tracks"),
                                              QStringLiteral("Tape")};
        QStringList names = item.fields.keys();
        std::sort(names.begin(), names.end());
        Properties user;
        for (const QString& name : names) {
            const QString value = item.fields.value(name);
            if (structural.contains(name) || value.isEmpty()) continue;
            if (model::binHeadings().contains(name) && !model::isEditableHeading(name)) {
                derived_.insert(name);
                continue;
            }
            set(user, roman(name), Value::string(roman(value)));
        }
        const std::uint32_t columns = b_.add(QStringLiteral("ATTR"), std::move(user));
        return b_.add(QStringLiteral("ATTR"), {prop("_USER", Value::ref(columns))});
    }

    // ---- components ----

    Properties component(int kind, const Rational& rate) const {
        return {prop("media_kind_id", Value::integer(kind)), prop("edit_rate", Value::real(rateValue(rate)))};
    }
    static Properties with(Properties p, std::initializer_list<Property> more) {
        for (const Property& x : more) set(p, x.name, x.value);
        return p;
    }

    // A source clip (SCLP) naming track `trackId` of mob `mob` from `start`; a null MobID (and track
    // 0) ends a source chain.
    std::uint32_t sourceClip(int kind, const Rational& rate, std::int64_t length, const MobId& mob, int trackId,
                             std::int64_t start) {
        return b_.add(QStringLiteral("SCLP"),
                      with(component(kind, rate), {prop("length", Value::integer(std::max<std::int64_t>(0, length))),
                                                   prop("track_id", Value::integer(trackId)),
                                                   prop("start_time", Value::integer(start)),
                                                   prop("mob_id", Value::mobId(mob))}));
    }

    // A timecode (TCCP): `start` counted at `fps` frames per second; bit 0 of its flags is drop frame.
    std::uint32_t timecode(const Rational& rate, std::int64_t length, int fps, std::int64_t start, bool drop) {
        return b_.add(QStringLiteral("TCCP"),
                      with(component(kTimecode, rate), {prop("length", Value::integer(std::max<std::int64_t>(0, length))),
                                                        prop("flags", Value::integer(drop ? 1 : 0)),
                                                        prop("fps", Value::integer(fps)),
                                                        prop("start", Value::integer(std::max<std::int64_t>(0, start)))}));
    }

    // A track of a mob or track group: its label (`index`, the track number), its component and, for
    // picture and sound, the filler proxy Media Composer gives every such track (a TRKR to the track
    // itself, of the largest length).
    Value track(int index, std::uint32_t componentRef, int kind, const Rational& rate) {
        Properties t{prop("index", Value::integer(index)), prop("component", Value::ref(componentRef))};
        if (kind == kPicture || kind == kSound) {
            const std::uint32_t proxy = b_.add(
                QStringLiteral("TRKR"), with(component(kind, rate), {prop("length", Value::integer(kForever)),
                                                                     prop("relative_scope", Value::integer(0)),
                                                                     prop("relative_track", Value::integer(-1))}));
            t.push_back(prop("filler_proxy", Value::ref(proxy)));
        }
        t.push_back(prop("lock_number", Value::integer(0)));
        return Value::record(std::move(t));
    }

    // A mob (CMPO) listed in the bin: its MobID, name, type, usage, dates and tracks.
    std::uint32_t mob(const MobId& id, const QString& name, int type, int usage, const Rational& rate, std::int64_t length,
                      const QDateTime& created, const QDateTime& modified, ValueList tracks, std::uint32_t descriptor,
                      std::uint32_t attributesRef, bool placed) {
        const std::uint32_t m = b_.add(
            QStringLiteral("CMPO"),
            with(component(kNone, rate),
                 {prop("name", Value::string(roman(name))), prop("attributes", Value::ref(attributesRef)),
                  prop("mc_mode", Value::integer(0)), prop("length", Value::integer(std::max<std::int64_t>(0, length))),
                  prop("num_scalars", Value::integer(0)), prop("tracks", Value::list(std::move(tracks))),
                  prop("last_modified", timeOf(modified.isValid() ? modified : created)),
                  prop("mob_type_id", Value::integer(type)), prop("usage_code", Value::integer(usage)),
                  prop("descriptor", Value::ref(descriptor)), prop("creation_time", timeOf(created)),
                  prop("mob_id", Value::mobId(id))}));
        (type == kSource ? sources_ : listed_).append({m, placed});
        return m;
    }

    // ---- clips and their sources ----

    // The clip's tracks: its Tracks column (video first), V1 or A1 when it has none.
    static QList<model::TrackRef> tracksOf(const BinItem& item) {
        QList<model::TrackRef> out;
        for (const QString& name : model::parseTrackList(item.fields.value(QStringLiteral("Tracks")))) {
            const auto ref = model::parseTrackName(name);
            if (ref && ref->kind != model::TrackKind::Data) out << *ref;
        }
        if (out.isEmpty()) {
            out << model::TrackRef{item.kind == BinItemKind::AudioClip ? model::TrackKind::Audio : model::TrackKind::Video, 1};
        }
        return out;
    }
    static int kindOf(const model::TrackRef& t) { return t.kind == model::TrackKind::Video ? kPicture : kSound; }

    // A file locator (FILE): the path as stored (Mac OS Roman), its POSIX form for an absolute POSIX
    // path, and the path in UTF-8, which the import reads first.
    std::uint32_t fileLocator(const QString& path) {
        const QString stored = QDir::fromNativeSeparators(path);
        Properties p{prop("path", Value::string(roman(stored, false)))};
        if (stored.startsWith(QLatin1Char('/')) && toMacRoman(stored)) p.push_back(prop("path_posix", Value::string(stored)));
        p.push_back(prop("path_utf8", Value::string(stored)));
        return b_.add(QStringLiteral("FILE"), std::move(p));
    }

    // A media file's descriptor: CDCI (8-bit 4:2:2, the size from the Format column) for a picture
    // file, PCMA (one channel of 24-bit 48 kHz) for sound, as the AAF export describes them. Managed
    // media is named by MobID (an MSML locator), with its path, when known, as the physical media's
    // file locator, as Media Composer's bins have it; a linked file has a file locator.
    std::uint32_t fileDescriptor(const BinItem& item, bool picture, const ClipSource& s) {
        std::uint32_t locator = 0;
        std::uint32_t physical = 0;
        if (s.path.isEmpty() || isManaged(s.path)) {
            locator = b_.add(QStringLiteral("MSML"), {prop("last_known_volume", Value::string(QString())),
                                                      prop("domain_type", Value::integer(0)),
                                                      prop("mob_id", Value::mobId(s.mob))});
            if (!s.path.isEmpty()) {
                physical = b_.add(QStringLiteral("MDES"), {prop("mob_kind", Value::integer(kFileKind)),
                                                           prop("locator", Value::ref(fileLocator(s.path))),
                                                           prop("intermediate", Value::boolean(false)),
                                                           prop("physical_media", Value::ref(0))});
            }
        } else {
            locator = fileLocator(s.path);
        }
        Properties p{prop("mob_kind", Value::integer(kFileKind)), prop("locator", Value::ref(locator)),
                     prop("intermediate", Value::boolean(false)), prop("physical_media", Value::ref(physical)),
                     prop("edit_rate", Value::real(rateValue(item.editRate))),
                     prop("length", Value::integer(std::max<std::int64_t>(0, item.durationFrames))),
                     prop("is_omfi", Value::integer(0)), prop("data_offset", Value::integer(0))};
        if (!picture) {
            for (const Property& x : std::initializer_list<Property>{
                     prop("channels", Value::integer(1)), prop("quantization_bits", Value::integer(24)),
                     prop("sample_rate", Value::real(48000.0)), prop("block_align", Value::integer(3)),
                     prop("average_bps", Value::integer(48000 * 3))}) {
                p.push_back(x);
            }
            return b_.add(QStringLiteral("PCMA"), std::move(p));
        }
        const detail::PictureSize size = detail::pictureSizeOf(item.fields.value(QStringLiteral("Format")));
        for (const char* name : {"stored", "sampled", "display"}) {
            set(p, QStringLiteral("%1_height").arg(QLatin1String(name)), Value::integer(size.height));
            set(p, QStringLiteral("%1_width").arg(QLatin1String(name)), Value::integer(size.width));
        }
        const auto pair = [](qint64 a, qint64 b) { return Value::list({Value::integer(a), Value::integer(b)}); };
        for (const Property& x : std::initializer_list<Property>{
                 prop("frame_layout", Value::integer(0)), prop("aspect_ratio", size.wide ? pair(16, 9) : pair(4, 3)),
                 prop("line_map", pair(0, 0)), prop("horizontal_subsampling", Value::integer(2)),
                 prop("vertical_subsampling", Value::integer(1)), prop("component_width", Value::integer(8)),
                 prop("color_sitting", Value::integer(4)), prop("black_ref_level", Value::integer(16)),
                 prop("white_ref_level", Value::integer(235)), prop("color_range", Value::integer(225))}) {
            p.push_back(x);
        }
        return b_.add(QStringLiteral("CDCI"), std::move(p));
    }

    // The timecode a tape or import source mob carries. Its text (ClipSource::startTimecode) does not
    // say its frames per second: it is the first rate (drop frame: 30 or 60; else the clip's, then
    // the usual ones) at which it reads as a timecode and gives the clip's own Start, as the import
    // reckons it (startAtRate). Without such a rate, the clip's Start at its own rate, with a warning
    // when the text was there.
    std::optional<SourceTimecode> sourceTimecode(const QString& text, std::optional<std::int64_t> start, bool startDrop,
                                                 const Rational& rate) {
        const int nominal = TimecodeFormat::forRate(rate, false).nominalFps;
        if (!text.isEmpty()) {
            const bool drop = text.contains(QLatin1Char(';'));
            const QList<int> rates = drop ? QList<int>{30, 60} : QList<int>{nominal, 24, 25, 30, 50, 60};
            for (const int fps : rates) {
                bool isDrop = false;
                const auto frames = detail::timecodeFrames(text, Rational(fps), &isDrop);
                if (!frames || isDrop != drop) continue;
                if (!start || detail::startAtRate(*frames, fps, rate) == *start) return SourceTimecode{*frames, fps, drop};
            }
            if (start) note(QStringLiteral("source timecodes that do not give the clip's Start written from the Start"));
        }
        if (!start) return std::nullopt;
        return SourceTimecode{*start, nominal, startDrop};
    }

    // A master or audio clip and its sources: the tape or import source mob (made up from the Tape
    // column, or as an import source, when the clip has a Start or Tape but no such source, so that
    // they travel), then a file source mob for each file, each naming the source before it, then the
    // master mob, a track for each of its tracks naming its file (or straight its tape or import).
    // Source mobs are written once across clips (written_).
    void master(const BinItem& item) {
        const MobId id = model::mobIdOf(item);
        const QList<model::TrackRef> tracks = tracksOf(item);
        const Rational rate = item.editRate;
        const std::int64_t length = item.durationFrames;
        bool drop = false;
        const std::optional<std::int64_t> start = detail::timecodeFrames(item.fields.value(QStringLiteral("Start")), rate, &drop);
        const QString tape = item.fields.value(QStringLiteral("Tape"));

        const ClipSource* before = nullptr;
        for (const ClipSource& s : item.sources) {
            if (s.kind == ClipSource::Kind::Tape || s.kind == ClipSource::Kind::Import) before = &s;
        }
        ClipSource made;
        if (!before && (start || !tape.isEmpty())) {
            made.kind = tape.isEmpty() ? ClipSource::Kind::Import : ClipSource::Kind::Tape;
            made.mob = model::derivedMobId(item.id + QStringLiteral("/source"));
            made.name = tape.isEmpty() ? item.name : tape;
            before = &made;
        } else if (before && before->kind == ClipSource::Kind::Import && !tape.isEmpty()) {
            note(QStringLiteral("Tape columns of clips imported from files not written"));
        }
        MobId beforeId;
        if (before) {
            beforeId = before->mob.isNull() ? model::derivedMobId(item.id + QStringLiteral("/source")) : before->mob;
            if (!written_.count(beforeId)) {
                written_.insert(beforeId);
                ValueList t;
                for (const model::TrackRef& r : tracks) {
                    t.push_back(track(r.number, sourceClip(kindOf(r), rate, length, MobId(), 0, 0), kindOf(r), rate));
                }
                if (const auto tc = sourceTimecode(before->startTimecode, start, drop, rate)) {
                    t.push_back(track(1, timecode(rate, length, tc->fps, tc->start, tc->drop), kTimecode, rate));
                }
                const bool isTape = before->kind == ClipSource::Kind::Tape;
                Properties d{prop("mob_kind", Value::integer(isTape ? kTapeKind : kImportKind)),
                             prop("locator", Value::ref(!isTape && !before->path.isEmpty() ? fileLocator(before->path) : 0)),
                             prop("intermediate", Value::boolean(false)), prop("physical_media", Value::ref(0))};
                if (isTape) d.push_back(prop("cframe", Value::integer(0)));
                const std::uint32_t descriptor = b_.add(isTape ? QStringLiteral("MDTP") : QStringLiteral("MDES"), std::move(d));
                mob(beforeId, before->name, kSource, 0, rate, length, item.created, item.modified, std::move(t), descriptor,
                    0, false);
            }
        }

        // The file sources by MobID, in the order the clip lists them, with the tracks each carries.
        QList<MobId> fileOrder;
        std::map<MobId, QList<const ClipSource*>> files;
        std::map<int, std::map<int, MobId>> fileOf; // media kind -> track number -> file
        for (const ClipSource& s : item.sources) {
            if (s.kind != ClipSource::Kind::File) continue;
            const auto ref = model::parseTrackName(s.track);
            if (!ref || ref->kind == model::TrackKind::Data) {
                note(QStringLiteral("file sources of no picture or sound track not written"));
                continue;
            }
            const MobId fileId = s.mob.isNull() ? model::derivedMobId(item.id + QStringLiteral("/file/") + s.track) : s.mob;
            if (!files.count(fileId)) fileOrder << fileId;
            files[fileId] << &s;
            fileOf[kindOf(*ref)][ref->number] = fileId;
        }
        for (const MobId& fileId : fileOrder) {
            if (written_.count(fileId)) continue;
            written_.insert(fileId);
            const QList<const ClipSource*>& carried = files.at(fileId);
            ClipSource first = *carried.first();
            first.mob = fileId;
            ValueList t;
            for (const ClipSource* s : carried) {
                const auto ref = *model::parseTrackName(s->track);
                const MobId from = !s->from.isNull() ? s->from : beforeId;
                t.push_back(track(ref.number, sourceClip(kindOf(ref), rate, length, from, from.isNull() ? 0 : ref.number, 0),
                                  kindOf(ref), rate));
            }
            const std::uint32_t descriptor = fileDescriptor(item, mediaKindOf(first.track) == kPicture, first);
            mob(fileId, first.name.isEmpty() ? item.name : first.name, kSource, 0, rate, length, item.created,
                item.modified, std::move(t), descriptor, 0, false);
        }

        ValueList t;
        for (const model::TrackRef& r : tracks) {
            const int kind = kindOf(r);
            MobId source = beforeId;
            if (const auto k = fileOf.find(kind); k != fileOf.end()) {
                if (const auto f = k->second.find(r.number); f != k->second.end()) source = f->second;
            }
            t.push_back(track(r.number, sourceClip(kind, rate, length, source, source.isNull() ? 0 : r.number, 0), kind, rate));
        }
        mob(id, item.name, kMaster, 0, rate, length, item.created, item.modified, std::move(t), 0, attributes(item),
            item.userPlaced);
    }

    // A subclip: a composition mob used as a subclip, each track naming the same track of its master
    // from the subclip's offset (its Clip source).
    void subclip(const BinItem& item) {
        const ClipSource* master = nullptr;
        for (const ClipSource& s : item.sources) {
            if (s.kind == ClipSource::Kind::Clip) master = &s;
        }
        if (!master) {
            note(QStringLiteral("subclips with no master clip written with no source"));
        } else if (!known_.count(master->mob)) {
            ++missing_;
        }
        ValueList t;
        for (const model::TrackRef& r : tracksOf(item)) {
            const int kind = kindOf(r);
            t.push_back(track(r.number,
                              sourceClip(kind, item.editRate, item.durationFrames, master ? master->mob : MobId(),
                                         master ? r.number : 0, master ? master->offset : 0),
                              kind, item.editRate));
        }
        mob(model::mobIdOf(item), item.name, kComposition, kUsageSubclip, item.editRate, item.durationFrames, item.created,
            item.modified, std::move(t), 0, attributes(item), item.userPlaced);
    }

    // ---- sequences ----

    // The effect ID a bin gives an effect of ours: dissolves are Media Composer's (video or audio),
    // an effect the import kept by its Media Composer effect ID ("AVB") has it back; others have
    // none we know (Phase 6 matches the Effect Palette's to Media Composer's).
    static std::optional<QString> effectId(const model::Effect& e, bool video) {
        if (e.category == QLatin1String("Blend") && e.name == QLatin1String("Dissolve")) return video ? kVideoDissolve : kAudioDissolve;
        if (e.category == QLatin1String("AVB") && !e.name.isEmpty()) return e.name;
        return std::nullopt;
    }

    // A segment's effect around its component: a motion effect (an effect ID of Media Composer's
    // time warps, EFF_TIMEWARP...) as SPED, at the speed of its input (the model keeps no speed),
    // others as a track effect (TKFX), with the component as their one track. Its own component when
    // the effect has no effect ID we know, with a warning.
    std::uint32_t effectGroup(const model::Effect& e, int kind, const Rational& rate, std::int64_t length,
                              std::uint32_t input) {
        const auto id = effectId(e, kind == kPicture);
        if (!id) {
            note(QStringLiteral("effects with no Media Composer effect ID left out (%1/%2)").arg(e.category, e.name));
            return input;
        }
        if (!e.parameters.isEmpty()) note(QStringLiteral("effect parameters not written"));
        const bool timeWarp = id->startsWith(QLatin1String("EFF_TIMEWARP"));
        if (timeWarp) note(QStringLiteral("motion effects written at the speed of their clips"));
        Properties p = with(component(kind, rate),
                            {prop("effect_id", Value::string(roman(*id))), prop("mc_mode", Value::integer(0)),
                             prop("length", Value::integer(std::max<std::int64_t>(0, length))),
                             prop("num_scalars", Value::integer(0)),
                             prop("tracks", Value::list({Value::record({prop("index", Value::integer(1)),
                                                                        prop("component", Value::ref(input)),
                                                                        prop("lock_number", Value::integer(0))})}))});
        if (timeWarp) {
            set(p, QStringLiteral("phase_offset"), Value::integer(0));
            set(p, QStringLiteral("speed_ratio"), Value::list({Value::integer(1), Value::integer(1)}));
            return b_.add(QStringLiteral("SPED"), std::move(p));
        }
        set(p, QStringLiteral("info_version"), Value::integer(2));
        return b_.add(QStringLiteral("TKFX"), std::move(p));
    }

    // A track's segments as a SEQU, between the zero-length fillers Media Composer puts at each end.
    // A transition goes back over the cut it sits at (the import's rule reversed): the segment before
    // is lengthened by its frames after the cut, the one after by its frames before the cut and starts
    // that much earlier in its clip; the TNFX between them overlaps both by its length, its cut point
    // the frames before the cut.
    std::uint32_t trackSequence(const model::Track& track, const Rational& rate) {
        const bool video = track.kind == model::TrackKind::Video;
        const int kind = video ? kPicture : kSound;
        QList<model::Segment> segments = track.segments;
        QList<std::int64_t> lengths;
        QList<std::int64_t> ins;
        QList<std::optional<QString>> transitions;
        for (const model::Segment& s : segments) {
            lengths << s.length;
            ins << s.sourceIn;
            transitions << std::nullopt;
        }
        for (qsizetype i = 0; i < segments.size(); ++i) {
            if (!segments[i].transition) continue;
            const model::Transition& tr = *segments[i].transition;
            if (i == 0) {
                note(QStringLiteral("transitions at the start of a track left out"));
                continue;
            }
            const auto id = effectId(tr.effect, video);
            if (!id) {
                note(QStringLiteral("transitions with no Media Composer effect ID left out (%1/%2)").arg(tr.effect.category, tr.effect.name));
                continue;
            }
            if (!tr.effect.parameters.isEmpty()) note(QStringLiteral("effect parameters not written"));
            transitions[i] = *id;
            lengths[i - 1] += tr.after();
            lengths[i] += tr.before();
            if (!segments[i].isFiller()) ins[i] -= tr.before();
        }
        auto filler = [&](std::int64_t length) {
            return b_.add(QStringLiteral("FILL"), with(component(kind, rate), {prop("length", Value::integer(length))}));
        };
        ValueList components{Value::ref(filler(0))};
        for (qsizetype i = 0; i < segments.size(); ++i) {
            const model::Segment& s = segments[i];
            if (transitions[i]) {
                const model::Transition& tr = *s.transition;
                components.push_back(Value::ref(b_.add(
                    QStringLiteral("TNFX"),
                    with(component(kind, rate),
                         {prop("effect_id", Value::string(roman(*transitions[i]))), prop("mc_mode", Value::integer(0)),
                          prop("length", Value::integer(tr.duration)), prop("num_scalars", Value::integer(0)),
                          prop("tracks", Value::list({Value::record({prop("lock_number", Value::integer(0))}),
                                                      Value::record({prop("lock_number", Value::integer(0))})})),
                          prop("cutpoint", Value::integer(tr.before())), prop("info_version", Value::integer(2))}))));
            }
            if (s.addEdit) note(QStringLiteral("add edits in filler not kept"));
            if (s.gain != 0.0 || s.pan != 0) note(QStringLiteral("clip gain and pan not written"));
            std::uint32_t c = 0;
            if (s.isFiller()) {
                c = filler(lengths[i]);
            } else {
                const auto ref = model::parseTrackName(s.sourceTrack);
                const int sourceKind = ref && ref->kind != model::TrackKind::Data ? kindOf(*ref) : kind;
                const MobId source = s.sourceMobId();
                if (!known_.count(source)) ++missing_;
                c = sourceClip(sourceKind, rate, lengths[i], source, ref ? ref->number : 1, ins[i]);
            }
            if (s.effect) c = effectGroup(*s.effect, kind, rate, lengths[i], c);
            components.push_back(Value::ref(c));
        }
        components.push_back(Value::ref(filler(0)));
        return b_.add(QStringLiteral("SEQU"), with(component(kind, rate), {prop("components", Value::list(std::move(components)))}));
    }

    // A sequence: a composition mob with a timecode track (TC1, its Start at the edit rate's nominal
    // frames per second) and a track for each video and audio track, labelled by number. The Export
    // options leave video or audio tracks out.
    void sequence(const BinItem& item) {
        const model::Sequence seq = model::Sequence::fromItem(item);
        const Rational rate = item.editRate;
        const std::int64_t duration = seq.duration();
        ValueList tracks;
        if (seq.hasTimecodeTrack()) {
            bool drop = false;
            const std::int64_t start = detail::timecodeFrames(item.fields.value(QStringLiteral("Start")), rate, &drop).value_or(0);
            const int fps = TimecodeFormat::forRate(rate, false).nominalFps;
            tracks.push_back(track(1, timecode(rate, duration, fps, start, drop), kTimecode, rate));
        }
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Data) {
                note(QStringLiteral("data tracks left out"));
                continue;
            }
            const bool video = t.kind == model::TrackKind::Video;
            if ((video && !options_.includeVideo) || (!video && !options_.includeAudio)) continue;
            if (!video && t.format != model::AudioFormat::Mono) note(QStringLiteral("stereo and surround audio tracks written as mono tracks"));
            tracks.push_back(track(t.number, trackSequence(t, rate), video ? kPicture : kSound, rate));
        }
        mob(model::mobIdOf(item), item.name, kComposition, 0, rate, duration, item.created, item.modified, std::move(tracks),
            0, attributes(item), item.userPlaced);
    }

    // ---- the bin ----

    // The bin view (BVst): Media Composer's columns, the bin's Text view headings shown in their order
    // after the icon, colour and Name columns, the others hidden; headings Media Composer has no column
    // for are left out with a warning (custom columns travel in the clips' _USER attributes).
    std::uint32_t binView(const model::BinDisplay& d) {
        if (d.mode != model::BinViewMode::Text) note(QStringLiteral("Frame and Script views written as the Text view"));
        if (!d.sort.isEmpty()) note(QStringLiteral("the bin's sort order not written"));
        if (d.sifted) note(QStringLiteral("the bin's sift not written"));
        if (!d.framePositions.isEmpty()) note(QStringLiteral("Frame view positions not written"));
        std::vector<bool> shown(std::size(kColumns), false);
        std::vector<int> order;
        for (const char* first : {"Color", "   ", "Name"}) {
            const int i = columnIndex(QLatin1String(first));
            shown[static_cast<std::size_t>(i)] = true;
            order.push_back(i);
        }
        for (const QString& heading : d.columns) {
            const int i = columnIndex(heading);
            if (i < 0) {
                if (!d.customColumns.contains(heading)) {
                    note(QStringLiteral("bin view headings with no Media Composer column left out (%1)").arg(heading));
                }
                continue;
            }
            if (shown[static_cast<std::size_t>(i)]) continue;
            shown[static_cast<std::size_t>(i)] = true;
            order.push_back(i);
        }
        const std::size_t visible = order.size();
        for (std::size_t i = 0; i < std::size(kColumns); ++i) {
            if (!shown[i]) order.push_back(static_cast<int>(i));
        }
        ValueList columns;
        for (std::size_t n = 0; n < order.size(); ++n) {
            const Column& c = kColumns[static_cast<std::size_t>(order[n])];
            columns.push_back(Value::record({prop("title", Value::string(QString::fromLatin1(c.title))),
                                             prop("format", Value::integer(c.format)), prop("type", Value::integer(c.type)),
                                             prop("hidden", Value::boolean(n >= visible))}));
        }
        const std::uint32_t attributes = b_.add(QStringLiteral("ATTR"), {});
        return b_.add(QStringLiteral("BVst"),
                      {prop("name", Value::string(roman(d.viewName.isEmpty() ? QStringLiteral("Untitled") : d.viewName))),
                       prop("kind", Value::string(QStringLiteral("Bin View"))), prop("attr_count", Value::integer(1)),
                       prop("attr_type", Value::integer(20)), prop("attributes", Value::ref(attributes)),
                       prop("columns", Value::list(std::move(columns)))});
    }

    // The bin (ABIN), the root: its mobs (the items, then the source mobs), the view, and the
    // display values every bin of the corpus has (font, image scales, home rectangle, colours, six
    // empty sift settings), but the background colour (p.263), which is the bin's. Its ID is made
    // from the time and the bin's name.
    void binObject(const model::Bin& bin, std::uint32_t view) {
        ValueList items;
        for (const QList<Listed>* list : {&listed_, &sources_}) {
            for (const Listed& l : *list) {
                items.push_back(Value::record({prop("mob", Value::ref(l.mob)), prop("x", Value::integer(-30000)),
                                               prop("y", Value::integer(-30000)), prop("keyframe", Value::integer(0)),
                                               prop("user_placed", Value::boolean(l.placed))}));
            }
        }
        ValueList sifts;
        for (int i = 0; i < 6; ++i) {
            sifts.push_back(Value::record({prop("method", Value::integer(1)), prop("string", Value::string(QString())),
                                           prop("column", Value::string(QStringLiteral("Any")))}));
        }
        auto ints = [](std::initializer_list<qint64> l) {
            ValueList out;
            for (const qint64 i : l) out.push_back(Value::integer(i));
            return Value::list(std::move(out));
        };
        Value background = ints({25600, 25600, 25600});
        // "#rrggbb" as three 16-bit channels.
        if (const QString& hex = bin.display.background; hex.size() == 7 && hex.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint rgb = hex.mid(1).toUInt(&ok, 16);
            if (ok) background = ints({((rgb >> 16) & 0xFF) * 257, ((rgb >> 8) & 0xFF) * 257, (rgb & 0xFF) * 257});
        }
        const std::uint64_t uid = (static_cast<std::uint64_t>(seconds(options_.when)) << 32) |
                                  (QUuid::createUuidV5(QUuid(), bin.name).data1 & 0x7FFFFFFFu);
        b_.add(QStringLiteral("ABIN"),
               {prop("large_bin", Value::boolean(items.size() > 0xFFFF)), prop("view_setting", Value::ref(view)),
                prop("uid", Value::uint64(uid)), prop("items", Value::list(std::move(items))),
                prop("display_mask", Value::integer(98999)), prop("display_mode", Value::integer(0)),
                prop("sifted", Value::boolean(false)), prop("sifted_settings", Value::list(std::move(sifts))),
                prop("sort_columns", Value::list(ValueList{})), prop("mac_font", Value::integer(139)),
                prop("mac_font_size", Value::integer(11)), prop("mac_image_scale", Value::integer(5)),
                prop("home_rect", ints({280, 1477, 474, 1920})), prop("background_color", background),
                prop("forground_color", ints({52224, 52224, 52224})), prop("ql_image_scale", Value::integer(6)),
                prop("attributes", Value::ref(0)), prop("was_iconic", Value::boolean(false))});
    }

    ExportOptions options_;
    Builder b_;
    std::map<QString, int> notes_;
    std::set<MobId> known_;   // the MobIDs of the items written
    std::set<MobId> written_; // source mobs written
    QList<Listed> listed_;    // the items' mobs, in order
    QList<Listed> sources_;   // the source mobs
    int missing_ = 0;         // references to clips not in the bin
    std::set<QString> derived_; // standard columns left out (attributes())
};

} // namespace

QByteArray exportBytes(const model::Bin& bin, const ExportOptions& options, ExportResult* result) {
    ExportResult local;
    ExportResult* r = result ? result : &local;
    *r = ExportResult{};
    // A damaged item's numbers can be past what Rational holds (it throws): an error, not a crash.
    try {
        return Exporter(options).run(bin, r);
    } catch (const std::exception& e) {
        r->ok = false;
        r->error = QString::fromUtf8(e.what());
        return {};
    }
}

ExportResult exportBin(const model::Bin& bin, const QString& path, const ExportOptions& options) {
    ExportResult result;
    const QByteArray bytes = exportBytes(bin, options, &result);
    if (!result.ok) return result;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        result.ok = false;
        result.error = QStringLiteral("%1: %2").arg(path, out.errorString());
    }
    return result;
}

} // namespace lumenis::interop::avb
