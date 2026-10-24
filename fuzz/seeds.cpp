// Small seed inputs made by our own writers (fuzz_targets.h, seeds()): the fuzzer starts from them
// as well as from the downloaded interchange corpus (scripts/fuzz.py seed), and ctest fuzz_replay
// runs them through every target, so the replay has inputs that parse on every platform without a
// download. Small inputs matter: libFuzzer mutates a 4 KB file far more usefully than a 500 KB one.
#include "fuzz_targets.h"

#include "lumenis/interop/aaf.h"
#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/avb_export.h"
#include "lumenis/interop/cfb.h"
#include "lumenis/interop/fcp_xml.h"
#include "lumenis/interop/otio.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

namespace lumenis::fuzz {

namespace {

namespace cfb = interop::cfb;

// A compound file with a storage in a storage, a stream in the mini stream, one of regular sectors
// and an empty one, in either sector size (version 3 or 4).
QByteArray tinyCompoundFile(int sectorSize) {
    cfb::Writer w(sectorSize);
    const int a = w.addStorage(cfb::Writer::root(), QStringLiteral("a"));
    const int b = w.addStorage(a, QStringLiteral("b"));
    w.addStream(a, QStringLiteral("small"), QByteArray(100, 's'));
    w.addStream(b, QStringLiteral("large"), QByteArray(5000, 'L'));
    w.addStream(cfb::Writer::root(), QStringLiteral("empty"), QByteArray());
    return w.toBytes();
}

// A bin in the terms the application makes them: a master clip of managed media from a tape, a
// subclip of it, and a sequence of both with filler and a dissolve.
model::Bin tinyBin() {
    const QDateTime when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc());
    model::Bin bin;
    bin.name = QStringLiteral("Seed");
    model::BinItem clip;
    clip.id = model::newItemId();
    clip.kind = model::BinItemKind::MasterClip;
    clip.name = QStringLiteral("Clip");
    clip.mobId = MobId::generate();
    clip.created = when;
    clip.modified = when;
    clip.editRate = Rational(25);
    clip.durationFrames = 50;
    clip.fields = {{QStringLiteral("Tracks"), QStringLiteral("V1 A1")},
                   {QStringLiteral("Start"), QStringLiteral("01:00:00:00")},
                   {QStringLiteral("Tape"), QStringLiteral("T1")}};
    model::ClipSource tape;
    tape.kind = model::ClipSource::Kind::Tape;
    tape.mob = MobId::generate();
    tape.name = QStringLiteral("T1");
    tape.startTimecode = QStringLiteral("01:00:00:00");
    for (const char* track : {"V1", "A1"}) {
        model::ClipSource f;
        f.kind = model::ClipSource::Kind::File;
        f.mob = MobId::generate();
        f.track = QString::fromLatin1(track);
        f.name = QStringLiteral("Clip.%1.mxf").arg(f.track);
        f.path = QStringLiteral("/Media/Avid MediaFiles/MXF/1/") + f.name;
        f.from = tape.mob;
        clip.sources << f;
    }
    clip.sources << tape;
    bin.items << clip;
    if (const auto sub = model::subclipItem(bin, clip, 5, 20, when)) bin.items << *sub;

    model::BinItem seqItem;
    seqItem.id = model::newItemId();
    seqItem.kind = model::BinItemKind::Sequence;
    seqItem.name = QStringLiteral("Cut");
    seqItem.mobId = MobId::generate();
    seqItem.created = when;
    seqItem.modified = when;
    seqItem.editRate = Rational(25);
    model::Sequence seq;
    model::Segment first;
    first.clipId = clip.id;
    first.sourceMob = clip.mobId;
    first.name = clip.name;
    first.sourceTrack = QStringLiteral("V1");
    first.length = 20;
    model::Segment second = first;
    second.sourceIn = 25;
    model::Transition dissolve;
    dissolve.effect = {QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
    dissolve.duration = 6;
    dissolve.alignment = model::TransitionAlignment::CenteredOnCut;
    second.transition = dissolve;
    seq.ensureTrack(QStringLiteral("V1"))->segments = {first, model::Segment::filler(5), second};
    model::Segment sound = first;
    sound.sourceTrack = QStringLiteral("A1");
    seq.ensureTrack(QStringLiteral("A1"))->segments = {sound};
    seq.normalize(QStringLiteral("V1"));
    seq.storeIn(seqItem);
    bin.items << seqItem;
    return bin;
}

// The bin exported as AAF. The exporter writes to a file only, so through a temporary folder (seeds
// are made once, not per fuzz input).
QByteArray tinyAaf(const model::Bin& bin) {
    QTemporaryDir dir;
    if (!dir.isValid()) return {};
    const QString path = dir.filePath(QStringLiteral("seed.aaf"));
    interop::aaf::ExportOptions options;
    options.when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc());
    if (!interop::aaf::exportItems(bin.items, path, options).ok) return {};
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Copies the tree below `from` under `to`, leaving out the entry `skip` and what is below it.
void copyTree(cfb::Reader& r, int from, cfb::Writer& w, int to, int skip) {
    for (const int id : r.children(from)) {
        if (id == skip) continue;
        const cfb::Entry& e = r.entry(id);
        if (e.isStorage()) {
            const int s = w.addStorage(to, e.name);
            w.setClsid(s, e.clsid);
            copyTree(r, id, w, s, skip);
        } else {
            w.addStream(to, e.name, r.readAll(id));
        }
    }
}

// An AAF file laid out again in 512-byte sectors, which makes it smaller (fewer bytes of padding)
// and a version 3 compound file; with `withMeta` false, without its metadictionary, which the
// reader does not need (it has the model built in), leaving a far smaller file for the fuzzer.
QByteArray relaid(const QByteArray& aafBytes, bool withMeta) {
    interop::aaf::File file;
    if (!file.openData(aafBytes)) return {};
    const int meta = withMeta ? -1 : file.root().object(QStringLiteral("MetaDictionary")).entry();
    cfb::Reader& r = file.container();
    cfb::Writer w(512);
    w.setHeaderClsid(r.headerClsid());
    w.setClsid(cfb::Writer::root(), r.entry(cfb::Reader::root()).clsid);
    copyTree(r, cfb::Reader::root(), w, cfb::Writer::root(), meta);
    return w.toBytes();
}

} // namespace

std::vector<Seed> seeds(const QString& target) {
    std::vector<Seed> out;
    auto add = [&](const QString& name, const QByteArray& data) {
        if (!data.isEmpty()) out.push_back({name, data});
    };
    if (target == QLatin1String("cfb")) {
        add(QStringLiteral("tiny_v3.cfb"), tinyCompoundFile(512));
        add(QStringLiteral("tiny_v4.cfb"), tinyCompoundFile(4096));
    } else if (target == QLatin1String("aaf")) {
        const QByteArray aaf = tinyAaf(tinyBin());
        add(QStringLiteral("tiny.aaf"), aaf);
        for (Seed& s : compactAaf(QStringLiteral("tiny"), aaf)) out.push_back(std::move(s));
    } else if (target == QLatin1String("avb") || target == QLatin1String("avb_rewrite")) {
        const model::Bin bin = tinyBin();
        interop::avb::ExportOptions little;
        little.when = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::utc());
        add(QStringLiteral("tiny.avb"), interop::avb::exportBytes(bin, little));
        interop::avb::ExportOptions big = little;
        big.littleEndian = false;
        add(QStringLiteral("tiny_be.avb"), interop::avb::exportBytes(bin, big));
    } else if (target == QLatin1String("otio")) {
        // The bin's sequence as the List Tool's OTIO output writes it, its clip found in the bin.
        const model::Bin bin = tinyBin();
        interop::otio::ExportOptions options;
        options.resolve = [&bin](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
            for (const model::BinItem& i : bin.items) {
                if (i.kind != model::BinItemKind::Sequence && (i.id == clipId || model::mobIdOf(i) == mob)) return i;
            }
            return std::nullopt;
        };
        for (const model::BinItem& i : bin.items) {
            if (i.kind == model::BinItemKind::Sequence) add(QStringLiteral("tiny.otio"), interop::otio::exportJson(i, options).json);
        }
    } else if (target == QLatin1String("fcpxml")) {
        // The same sequence as FCP 7 XML.
        const model::Bin bin = tinyBin();
        interop::otio::ExportOptions options;
        options.resolve = [&bin](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
            for (const model::BinItem& i : bin.items) {
                if (i.kind != model::BinItemKind::Sequence && (i.id == clipId || model::mobIdOf(i) == mob)) return i;
            }
            return std::nullopt;
        };
        for (const model::BinItem& i : bin.items) {
            if (i.kind == model::BinItemKind::Sequence) add(QStringLiteral("tiny.xml"), interop::fcpxml::exportXml(i, options).json);
        }
    }
    return out;
}

std::vector<Seed> compactAaf(const QString& name, const QByteArray& aaf) {
    std::vector<Seed> out;
    for (const bool withMeta : {true, false}) {
        const QByteArray data = relaid(aaf, withMeta);
        if (!data.isEmpty()) out.push_back({name + (withMeta ? QStringLiteral(".v3.aaf") : QStringLiteral(".v3_nometa.aaf")), data});
    }
    return out;
}

} // namespace lumenis::fuzz
