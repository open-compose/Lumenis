#include "corpus.h"
#include "lumenis/interop/aaf.h"
#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/interop/aaf_writer.h"
#include "lumenis/model/structure.h"
#include "lumenis/model/sequence.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <functional>
#include <limits>
#include <stdexcept>

using namespace lumenis;
using namespace lumenis::interop;

namespace {

QString corpus() { return lumenis::test::interopCorpus(); }

QString hexOf(const QByteArray& b) { return QString::fromLatin1(b.toHex()); }

} // namespace

class TestAaf : public QObject {
    Q_OBJECT

private slots:
    void auids();
    void mobIds();
    void builtInModel();
    void corpusDumps();
    void objects();
    void importsBin();
    void exportRoundTrip();
    void notAaf();
    void damaged();
    void builderUnchangedByWrite();
    void trailingTransition();
    void dropFrameStart();
    void numbersPastRational();
    void negativeGroupLength();
    void slowEditRate();
    void sharedStrongRefs();
    void hugeStart_data();
    void hugeStart();
    void hugeLengths();
};

void TestAaf::auids() {
    // AUIDs are stored as GUIDs: the first three fields little-endian.
    const QByteArray stored = QByteArray::fromHex("0101010d01010101060e2b3402060101");
    const QUuid auid = aaf::auidFromBytes(stored.constData());
    QCOMPARE(aaf::auidText(auid), QStringLiteral("0d010101-0101-0101-060e-2b3402060101"));
    QCOMPARE(aaf::auidBytes(auid), stored);
    QCOMPARE(aaf::auidFromText("0d010101-0101-0101-060e-2b3402060101"), auid);
}

void TestAaf::mobIds() {
    // A material number that is a SMPTE label (Media Composer's): its halves
    // are swapped in the file. pyaaf2 reads this one from test_file_01.aaf.
    const MobId avid = *MobId::fromString(
        "urn:smpte:umid:060a2b34.01010101.01010f00.13000000.060e2b34.7f7f2a80.48c9f200.5a940184");
    const QByteArray stored = aaf::mobIdBytes(avid);
    QCOMPARE(hexOf(stored.mid(16)), QStringLiteral("00f2c948945a8401060e2b347f7f2a80"));
    QCOMPARE(aaf::mobIdFromBytes(stored.constData()), avid);
    // One of ours: a UUID material number, stored as a GUID.
    const MobId ours = MobId::generate();
    const QByteArray b = aaf::mobIdBytes(ours);
    QCOMPARE(aaf::mobIdFromBytes(b.constData()), ours);
    QCOMPARE(static_cast<quint8>(b[16 + 3]), ours.bytes()[16]);
    QCOMPARE(static_cast<quint8>(b[24]), ours.bytes()[24]);
}

void TestAaf::builtInModel() {
    const aaf::MetaDictionary m = aaf::MetaDictionary::builtIn();
    QVERIFY(m.classCount() > 100);
    QVERIFY(m.typeCount() > 180);
    const aaf::ClassDef* sourceClip = m.classDef(QStringLiteral("SourceClip"));
    QVERIFY(sourceClip);
    QVERIFY(m.isA(sourceClip->auid, m.classDef(QStringLiteral("Component"))->auid));
    QVERIFY(!m.isA(m.classDef(QStringLiteral("Component"))->auid, sourceClip->auid));
    const aaf::PropertyDef* length = m.property(sourceClip->auid, QStringLiteral("Length")); // from Component
    QVERIFY(length);
    QCOMPARE(length->pid, 0x0202);
    QCOMPARE(m.byteSize(length->type), 8);
    QCOMPARE(m.byteSize(m.typeDef(QStringLiteral("MobIDType"))->auid), 32);
    QCOMPARE(m.byteSize(m.typeDef(QStringLiteral("Rational"))->auid), 8);
    QCOMPARE(m.byteSize(m.typeDef(QStringLiteral("aafString"))->auid), 0);
    QVERIFY(m.classDef(QStringLiteral("Root")));
}

// Every corpus AAF reads as pyaaf2 reads it: the same dump
// (scripts/aaf-dump.py), compared by digest (tests/data/interop_corpus.json).
void TestAaf::corpusDumps() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    QFile manifestFile(QStringLiteral(LUMENIS_TEST_DATA "/interop_corpus.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject files = QJsonDocument::fromJson(manifestFile.readAll()).object().value(QStringLiteral("aaf")).toObject();
    QVERIFY(!files.isEmpty());
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = QDir(folder).filePath(it.key());
        if (!QFile::exists(path)) continue;
        aaf::File file;
        QString error;
        QVERIFY2(file.open(path, &error), qPrintable(error));
        const QByteArray text = aaf::canonicalJson(aaf::dump(file));
        const QJsonObject want = it.value().toObject();
        QCOMPARE(static_cast<int>(text.count("\"class\":")), want.value(QStringLiteral("objects")).toInt());
        QVERIFY2(QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex()) ==
                     want.value(QStringLiteral("dump")).toString(),
                 qPrintable(it.key() + QLatin1String(": the dump is not pyaaf2's (scripts/aaf-dump.py --compare)")));
    }
}

// The object API on a sequence exported from Media Composer 6.5.
void TestAaf::objects() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    aaf::File file;
    QString error;
    QVERIFY2(file.open(QDir(folder).filePath(QStringLiteral("pyaaf2/test_file_01.aaf")), &error), qPrintable(error));
    const aaf::Object header = file.header();
    QCOMPARE(header.className(), QStringLiteral("Header"));
    QCOMPARE(header.integer(QStringLiteral("ByteOrder")), 0x4949);
    const QVariantMap version = header.value(QStringLiteral("Version")).toMap();
    QCOMPARE(version.value(QStringLiteral("major")).toLongLong(), 1);
    const QList<aaf::Object> identification = header.objects(QStringLiteral("IdentificationList"));
    QCOMPARE(identification.size(), 1);
    QCOMPARE(identification.first().string(QStringLiteral("ProductName")), QStringLiteral("Avid Media Composer 6.5.2.25575"));

    const QList<aaf::Object> mobs = file.mobs();
    QCOMPARE(mobs.size(), 199);
    int sources = 0;
    int masters = 0;
    QList<aaf::Object> compositions;
    for (const aaf::Object& m : mobs) {
        QVERIFY(m.isA(QStringLiteral("Mob")));
        if (m.isA(QStringLiteral("SourceMob"))) ++sources;
        if (m.isA(QStringLiteral("MasterMob"))) ++masters;
        if (m.isA(QStringLiteral("CompositionMob"))) compositions.append(m);
    }
    QCOMPARE(sources, 133);
    QCOMPARE(masters, 65);
    QCOMPARE(compositions.size(), 1);
    const aaf::Object sequence = compositions.first();
    QCOMPARE(sequence.string(QStringLiteral("Name")), QStringLiteral("Demo_2012_v0002.Exported.01"));
    QCOMPARE(QString::fromStdString(sequence.mobId(QStringLiteral("MobID")).toString()),
             QStringLiteral("urn:smpte:umid:060a2b34.01010101.01010f00.13000000.060e2b34.7f7f2a80.523f7540.11a503b0"));
    const QList<aaf::Object> mobSlots = sequence.objects(QStringLiteral("Slots"));
    QCOMPARE(mobSlots.size(), 12);
    const QVariantMap rate = mobSlots.first().value(QStringLiteral("EditRate")).toMap();
    QCOMPARE(rate.value(QStringLiteral("Numerator")).toLongLong(), 23976);
    QCOMPARE(rate.value(QStringLiteral("Denominator")).toLongLong(), 1000);

    // A master clip's source clip, and the data definition it names by weak reference.
    aaf::Object master;
    for (const aaf::Object& m : mobs) {
        if (m.isA(QStringLiteral("MasterMob")) && m.string(QStringLiteral("Name")) == QLatin1String("52_64_2_03_05.mov")) {
            master = m;
            break;
        }
    }
    QVERIFY(master);
    const aaf::Object clip = master.objects(QStringLiteral("Slots")).first().object(QStringLiteral("Segment"));
    QVERIFY(clip.isA(QStringLiteral("SourceClip")));
    QCOMPARE(clip.integer(QStringLiteral("Length")), 99);
    QCOMPARE(QString::fromStdString(clip.mobId(QStringLiteral("SourceID")).toString()),
             QStringLiteral("urn:smpte:umid:060a2b34.01010101.01010f00.13000000.060e2b34.7f7f2a80.48c9f200.5a950184"));
    const QVariant definition = clip.value(QStringLiteral("DataDefinition"));
    QVERIFY(definition.userType() == qMetaTypeId<aaf::Object>());
    const aaf::Object picture = definition.value<aaf::Object>();
    QCOMPARE(picture.className(), QStringLiteral("DataDefinition"));
    QCOMPARE(picture.string(QStringLiteral("Name")), QStringLiteral("Picture"));
    QVERIFY(!clip.has(QStringLiteral("StartTime")) || clip.integer(QStringLiteral("StartTime"), -1) >= 0);
    QVERIFY(clip.string(QStringLiteral("NoSuchProperty"), QStringLiteral("none")) == QLatin1String("none"));
}

// AAF into a bin (docs/spec/aaf.md, "Import"). The structure as a whole is
// compared with Media Composer's bin by ctest aaf_import_bins; this checks
// what the structure dump does not show.
void TestAaf::importsBin() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    QString error;
    aaf::ImportOptions options;
    options.binId = QStringLiteral("bin-1");
    const auto result = aaf::importFile(QDir(folder).filePath(QStringLiteral("pyaaf2/test_file_01.aaf")), options, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->items.size(), 53); // 52 clips and the sequence; 13 rendered effect clips left out
    QVERIFY(result->warnings.contains(QStringLiteral("13 × rendered effect clips left out")));
    QCOMPARE(result->customColumns, QStringList{QStringLiteral("UNC Path")});

    const model::BinItem* clip = nullptr;
    const model::BinItem* sequenceItem = nullptr;
    for (const model::BinItem& item : result->items) {
        if (item.name == QLatin1String("52_64_2_03_05.mov")) clip = &item;
        if (item.kind == model::BinItemKind::Sequence) sequenceItem = &item;
    }
    QVERIFY(clip && sequenceItem);
    QCOMPARE(clip->kind, model::BinItemKind::MasterClip);
    QCOMPARE(clip->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1"));
    // The import source's 30 fps drop-frame timecode, at the clip's 24 fps.
    QCOMPARE(clip->fields.value(QStringLiteral("Start")), QStringLiteral("00:00:30:00"));
    QCOMPARE(clip->sources.size(), 2);
    QCOMPARE(clip->sources[0].kind, model::ClipSource::Kind::File);
    QCOMPARE(clip->sources[0].track, QStringLiteral("V1"));
    QCOMPARE(clip->sources[0].path, QStringLiteral("/Giraffe/Marty/Avid MediaFiles/MXF/1/52_64_2_03_05.mov.V48C9F200.mxf"));
    QCOMPARE(clip->sources[0].from, clip->sources[1].mob);
    QCOMPARE(clip->sources[1].kind, model::ClipSource::Kind::Import);
    QCOMPARE(clip->sources[1].startTimecode, QStringLiteral("00:00:30;00"));
    QVERIFY(clip->created.isValid());

    QCOMPARE(sequenceItem->editRate, Rational(24000, 1001));
    QCOMPARE(sequenceItem->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    const model::Sequence sequence = model::Sequence::fromItem(*sequenceItem);
    QVERIFY(sequence.hasTimecodeTrack());
    QCOMPARE(sequence.trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("V2"), QStringLiteral("V3"),
                                                 QStringLiteral("V4"), QStringLiteral("A1"), QStringLiteral("A2")}));
    QCOMPARE(sequence.duration(), 4794);
    // The first cut: a dissolve starting at it (Cut Point 0).
    const model::Track* v1 = sequence.track(QStringLiteral("V1"));
    QVERIFY(v1->segments[0].isFiller());
    const model::Segment& first = v1->segments[1];
    QVERIFY(first.transition);
    QCOMPARE(first.transition->duration, 33);
    QCOMPARE(first.transition->alignment, model::TransitionAlignment::StartingAtCut);
    QCOMPARE(first.transition->effect.name, QStringLiteral("Dissolve"));
    // Segments name their clips in the bin.
    QCOMPARE(first.binId, QStringLiteral("bin-1"));
    const auto named = std::find_if(result->items.begin(), result->items.end(),
                                    [&](const model::BinItem& i) { return i.id == first.clipId; });
    QVERIFY(named != result->items.end());
    QCOMPARE(named->mobId, first.sourceMob);
    QCOMPARE(first.sourceTrack, QStringLiteral("V1"));
}

namespace {

// Items as the structure dump shows them, ids and sources aside.
QJsonObject structureOf(const QList<model::BinItem>& items, const QStringList& custom = {}) {
    model::Bin bin;
    bin.name = QStringLiteral("Round trip");
    bin.items = items;
    bin.display.customColumns = custom;
    QJsonObject o = model::binStructure(bin);
    // Sources are checked on their own: export can add a tape source for
    // a Start that had none.
    QJsonArray stripped;
    for (const QJsonValue& v : o.value(QStringLiteral("items")).toArray()) {
        QJsonObject item = v.toObject();
        item.remove(QStringLiteral("sources"));
        stripped.append(item);
    }
    o.insert(QStringLiteral("items"), stripped);
    return o;
}

} // namespace

// A bin made here, exported and imported again, is the same bin: clips with
// their sources and columns, a subclip, and a sequence with layered video,
// filler, a transition and effects.
void TestAaf::exportRoundTrip() {
    const QDateTime when(QDate(2026, 3, 1), QTime(9, 30), QTimeZone::utc());
    model::BinItem clip;
    clip.id = model::newItemId();
    clip.mobId = MobId::generate();
    clip.kind = model::BinItemKind::MasterClip;
    clip.name = QStringLiteral("A001_C002");
    clip.editRate = Rational(25);
    clip.durationFrames = 250;
    clip.created = clip.modified = when;
    clip.fields = {{QStringLiteral("Tracks"), QStringLiteral("V1 A1-2")},
                   {QStringLiteral("Start"), QStringLiteral("10:00:00:00")},
                   {QStringLiteral("End"), QStringLiteral("10:00:10:00")},
                   {QStringLiteral("Tape"), QStringLiteral("A001")},
                   {QStringLiteral("Circled"), QStringLiteral("yes")}};
    model::ClipSource tape{model::ClipSource::Kind::Tape, MobId::generate(), {}, QStringLiteral("A001"), {}, {}, 0, {}};
    const MobId file = MobId::generate();
    for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}) {
        clip.sources << model::ClipSource{model::ClipSource::Kind::File, file, t, QStringLiteral("A001_C002.mxf"),
                                          QStringLiteral("/media/A001_C002.mxf"), {}, 0, tape.mob};
    }
    clip.sources << tape;

    model::BinItem other = clip;
    other.id = model::newItemId();
    other.mobId = MobId::generate();
    other.name = QStringLiteral("A001_C003");
    other.fields.remove(QStringLiteral("Circled"));
    other.fields[QStringLiteral("Tracks")] = QStringLiteral("V1");
    other.sources = {};

    model::Bin bin;
    bin.items = {clip};
    const auto sub = model::subclipItem(bin, clip, 50, 150, when);
    QVERIFY(sub);
    model::BinItem subclip = *sub;
    subclip.mobId = MobId::generate();

    model::BinItem sequenceItem;
    sequenceItem.id = model::newItemId();
    sequenceItem.mobId = MobId::generate();
    sequenceItem.kind = model::BinItemKind::Sequence;
    sequenceItem.name = QStringLiteral("Cut 1");
    sequenceItem.editRate = Rational(25);
    sequenceItem.created = sequenceItem.modified = when;
    sequenceItem.fields = {{QStringLiteral("Start"), QStringLiteral("01:00:00:00")},
                           {QStringLiteral("End"), QStringLiteral("01:00:05:20")},
                           {QStringLiteral("Tracks"), QStringLiteral("V1-2 A1 TC1")}};
    auto segment = [&](const model::BinItem& c, const QString& track, std::int64_t in, std::int64_t length) {
        model::Segment s;
        s.clipId = c.id;
        s.sourceMob = c.mobId;
        s.name = c.name;
        s.sourceTrack = track;
        s.sourceIn = in;
        s.length = length;
        return s;
    };
    model::Sequence seq;
    seq.ensureTrack(QStringLiteral("V1"))->segments = {segment(clip, QStringLiteral("V1"), 10, 50),
                                                       segment(other, QStringLiteral("V1"), 20, 40),
                                                       model::Segment::filler(25), segment(clip, QStringLiteral("V1"), 100, 30)};
    model::Track* v1 = seq.track(QStringLiteral("V1"));
    model::Transition dissolve;
    dissolve.effect = {QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
    dissolve.duration = 10;
    dissolve.alignment = model::TransitionAlignment::CenteredOnCut;
    v1->segments[1].transition = dissolve;
    v1->segments[3].effect = model::Effect{QStringLiteral("Blend"), QStringLiteral("Superimpose"), {}};
    seq.ensureTrack(QStringLiteral("V2"))->segments = {model::Segment::filler(60), segment(other, QStringLiteral("V1"), 0, 20)};
    seq.ensureTrack(QStringLiteral("A1"))->segments = {segment(clip, QStringLiteral("A2"), 10, 145)};
    seq.storeIn(sequenceItem);

    const QList<model::BinItem> items{clip, other, subclip, sequenceItem};
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("Round trip.aaf"));
    aaf::ExportOptions options;
    options.when = when;
    const aaf::ExportResult written = aaf::exportItems(items, path, options);
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY2(written.warnings.isEmpty(), qPrintable(written.warnings.join(QLatin1Char('\n'))));

    QString error;
    const auto read = aaf::importFile(path, {}, &error);
    QVERIFY2(read, qPrintable(error));
    QCOMPARE(read->customColumns, QStringList{QStringLiteral("Circled")});
    const QJsonObject before = structureOf(items, {QStringLiteral("Circled")});
    const QJsonObject after = structureOf(read->items, read->customColumns);
    if (before != after) {
        // Both, for scripts/structdiff.py.
        const QString out = qEnvironmentVariable("CUTTHULHU_TEST_OUT", dir.path());
        for (const auto& [name, o] : {std::pair{QStringLiteral("before.json"), before}, std::pair{QStringLiteral("after.json"), after}}) {
            QFile f(QDir(out).filePath(name));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("cutthulhu-structure")},
                                                  {QStringLiteral("version"), 1},
                                                  {QStringLiteral("bins"), QJsonArray{o}}})
                            .toJson());
            }
        }
        QFAIL(qPrintable(QStringLiteral("the bin read back is not the bin written (before.json, after.json in %1)").arg(out)));
    }
    // The source chain survives too.
    const model::BinItem& back = read->items.first();
    QCOMPARE(back.sources.size(), 4);
    QCOMPARE(back.sources[0].path, QStringLiteral("/media/A001_C002.mxf"));
    QCOMPARE(back.sources[3].kind, model::ClipSource::Kind::Tape);
    QCOMPARE(back.sources[3].mob, tape.mob);
    QCOMPARE(back.fields.value(QStringLiteral("Tape")), QStringLiteral("A001"));
    QCOMPARE(back.created, when);
}

void TestAaf::notAaf() {
    QTemporaryDir dir;
    cfb::Writer writer;
    writer.addStream(cfb::Writer::root(), QStringLiteral("x"), QByteArray("y"));
    QVERIFY(writer.save(dir.filePath(QStringLiteral("plain.cfb"))));
    aaf::File file;
    QString error;
    QVERIFY(!file.open(dir.filePath(QStringLiteral("plain.cfb")), &error));
    QVERIFY(error.contains(QStringLiteral("not an AAF file")));
}

// Damaged AAF files: opening and dumping them never crashes or hangs.
void TestAaf::damaged() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    QFile source(QDir(folder).filePath(QStringLiteral("pyaaf2/empty.aaf")));
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QByteArray good = source.readAll();
    // From memory: hundreds of temporary files are slow on Windows, where each is scanned, and the
    // reader is the same as for a file (openData).
    QRandomGenerator random(7);
    int opened = 0;
    for (int i = 0; i < 200; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 16; ++k) {
            bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        }
        aaf::File file;
        if (!file.openData(bytes, QStringLiteral("bad.aaf"))) continue;
        ++opened;
        aaf::canonicalJson(aaf::dump(file));
        for (const aaf::Object& m : file.mobs()) m.string(QStringLiteral("Name"));
    }
    QVERIFY(opened > 0);
}

namespace {

const QUuid kPictureDef = aaf::auidFromText("01030202-0100-0000-060e-2b3404010101");
const QUuid kTimecodeDef = aaf::auidFromText("01030201-0100-0000-060e-2b3404010101");

QVariantMap rationalOf(qint64 num, qint64 den) {
    return {{QStringLiteral("Numerator"), num}, {QStringLiteral("Denominator"), den}};
}

// A builder with the Header's basics and the Picture and Timecode data definitions, for hand-made files.
void startFile(aaf::Builder& b) {
    b.set(b.header(), QStringLiteral("ByteOrder"), 0x4949);
    b.set(b.header(), QStringLiteral("Version"), QVariantMap{{QStringLiteral("major"), 1}, {QStringLiteral("minor"), 1}});
    for (const auto& [auid, name] : {std::pair{kPictureDef, QStringLiteral("Picture")}, std::pair{kTimecodeDef, QStringLiteral("Timecode")}}) {
        const aaf::Builder::Ref d = b.create(QStringLiteral("DataDefinition"));
        b.set(d, QStringLiteral("Identification"), auid);
        b.set(d, QStringLiteral("Name"), name);
        b.set(d, QStringLiteral("Description"), QString());
        b.append(b.dictionary(), QStringLiteral("DataDefinitions"), d);
    }
}

// A mob of `className` with its MobID and name, in the file's content.
aaf::Builder::Ref addMob(aaf::Builder& b, const QString& className, const MobId& id, const QString& name) {
    const aaf::Builder::Ref m = b.create(className);
    b.set(m, QStringLiteral("MobID"), QVariant::fromValue(id));
    b.set(m, QStringLiteral("Name"), name);
    b.append(b.content(), QStringLiteral("Mobs"), m);
    return m;
}

// A timeline slot holding `segment`.
void addSlot(aaf::Builder& b, aaf::Builder::Ref mob, int id, const QVariantMap& rate, aaf::Builder::Ref segment) {
    const aaf::Builder::Ref s = b.create(QStringLiteral("TimelineMobSlot"));
    b.set(s, QStringLiteral("SlotID"), id);
    b.set(s, QStringLiteral("EditRate"), rate);
    b.set(s, QStringLiteral("Origin"), 0);
    b.setObject(s, QStringLiteral("Segment"), segment);
    b.append(mob, QStringLiteral("Slots"), s);
}

} // namespace

// toBytes() leaves the builder as it found it, also when it fails: no
// metadictionary objects are left behind to be written twice.
void TestAaf::builderUnchangedByWrite() {
    aaf::Builder good;
    const QByteArray first = good.toBytes();
    QVERIFY(!first.isEmpty());
    QCOMPARE(good.toBytes().size(), first.size());

    aaf::Builder b;
    const aaf::Builder::Ref before = b.create(QStringLiteral("Filler"));
    QVERIFY(!b.set(before, QStringLiteral("NoSuchProperty"), 1));
    QCOMPARE(b.errors().size(), 1);
    QString error;
    QVERIFY(b.toBytes(&error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("NoSuchProperty")));
    QCOMPARE(b.errors().size(), 1);
    // The next object takes the place after the last one made before the write.
    QCOMPARE(b.create(QStringLiteral("Filler")), before + 1);
}

// A transition last in its track has no segment after it to go onto: it is
// left out with a warning, not silently.
void TestAaf::trailingTransition() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    const aaf::Builder::Ref seq = b.create(QStringLiteral("Sequence"));
    b.setWeak(seq, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(seq, QStringLiteral("Length"), qint64(40));
    const aaf::Builder::Ref filler = b.create(QStringLiteral("Filler"));
    b.setWeak(filler, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(filler, QStringLiteral("Length"), qint64(50));
    b.append(seq, QStringLiteral("Components"), filler);
    const aaf::Builder::Ref t = b.create(QStringLiteral("Transition"));
    b.setWeak(t, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(t, QStringLiteral("Length"), qint64(10));
    b.set(t, QStringLiteral("CutPoint"), qint64(5));
    b.append(seq, QStringLiteral("Components"), t);
    addSlot(b, m, 1, rationalOf(25, 1), seq);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("trailing.aaf"));
    QString error;
    QVERIFY2(b.save(path, &error), qPrintable(error));

    const auto result = aaf::importFile(path, {}, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->items.size(), 1);
    QVERIFY2(result->warnings.contains(QStringLiteral("transitions at the end of a track left out")),
             qPrintable(result->warnings.join(QLatin1Char('\n'))));
}

// A master clip's Start and End follow its tape's drop-frame timecode.
void TestAaf::dropFrameStart() {
    aaf::Builder b;
    startFile(b);
    const QVariantMap ntsc = rationalOf(30000, 1001);
    const MobId tapeId = MobId::generate();
    const aaf::Builder::Ref tape = addMob(b, QStringLiteral("SourceMob"), tapeId, QStringLiteral("TAPE1"));
    b.setObject(tape, QStringLiteral("EssenceDescription"), b.create(QStringLiteral("TapeDescriptor")));
    const aaf::Builder::Ref tc = b.create(QStringLiteral("Timecode"));
    b.setWeak(tc, QStringLiteral("DataDefinition"), kTimecodeDef);
    b.set(tc, QStringLiteral("Length"), qint64(1000000));
    b.set(tc, QStringLiteral("Start"), qint64(107892)); // 01:00:00;00 at 29.97 drop frame
    b.set(tc, QStringLiteral("FPS"), 30);
    b.set(tc, QStringLiteral("Drop"), true);
    addSlot(b, tape, 1, ntsc, tc);

    const aaf::Builder::Ref master = addMob(b, QStringLiteral("MasterMob"), MobId::generate(), QStringLiteral("Clip"));
    const aaf::Builder::Ref clip = b.create(QStringLiteral("SourceClip"));
    b.setWeak(clip, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(clip, QStringLiteral("Length"), qint64(1800));
    b.set(clip, QStringLiteral("SourceID"), QVariant::fromValue(tapeId));
    b.set(clip, QStringLiteral("SourceMobSlotID"), 1);
    b.set(clip, QStringLiteral("StartTime"), qint64(0));
    addSlot(b, master, 1, ntsc, clip);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("drop.aaf"));
    QString error;
    QVERIFY2(b.save(path, &error), qPrintable(error));

    const auto result = aaf::importFile(path, {}, &error);
    QVERIFY2(result, qPrintable(error));
    QCOMPARE(result->items.size(), 1);
    const model::BinItem& item = result->items.first();
    QCOMPARE(item.fields.value(QStringLiteral("Tape")), QStringLiteral("TAPE1"));
    QCOMPARE(item.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00;00"));
    QCOMPARE(item.fields.value(QStringLiteral("End")), QStringLiteral("01:01:00;02"));
}

namespace {

// The file without its metadictionary, in 512-byte sectors: the reader has the model built in, so it
// reads the same, and the file is small enough to keep as a fuzz regression input (fuzz/seeds.cpp
// makes seeds the same way). `edit`, when given, may change each stream's bytes on the way (by name).
QByteArray compact(const QByteArray& bytes,
                   const std::function<QByteArray(const QString&, const QByteArray&)>& edit = {}) {
    aaf::File file;
    if (!file.openData(bytes)) return {};
    const int meta = file.root().object(QStringLiteral("MetaDictionary")).entry();
    cfb::Reader& r = file.container();
    cfb::Writer w(512);
    w.setHeaderClsid(r.headerClsid());
    w.setClsid(cfb::Writer::root(), r.entry(cfb::Reader::root()).clsid);
    std::function<void(int, int)> copy = [&](int from, int to) {
        for (const int id : r.children(from)) {
            if (id == meta) continue;
            const cfb::Entry& e = r.entry(id);
            if (e.isStorage()) {
                const int s = w.addStorage(to, e.name);
                w.setClsid(s, e.clsid);
                copy(id, s);
            } else {
                const QByteArray data = r.readAll(id);
                w.addStream(to, e.name, edit ? edit(e.name, data) : data);
            }
        }
    };
    copy(cfb::Reader::root(), cfb::Writer::root());
    return w.toBytes();
}

} // namespace

// Found by reading the import alongside fuzz_avb's same hang in the bin import (fuzz/regressions/aaf):
// an effect (OperationGroup) whose Length is negative left the loop that fits its input to its length
// with one empty segment, forever. It now imports as an empty effect. With CUTTHULHU_FUZZ_REGRESSIONS
// set to fuzz/regressions, the file is written to aaf/ there.
void TestAaf::negativeGroupLength() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    const aaf::Builder::Ref seq = b.create(QStringLiteral("Sequence"));
    b.setWeak(seq, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(seq, QStringLiteral("Length"), qint64(10));
    const aaf::Builder::Ref group = b.create(QStringLiteral("OperationGroup"));
    b.setWeak(group, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(group, QStringLiteral("Length"), qint64(-5));
    const aaf::Builder::Ref filler = b.create(QStringLiteral("Filler"));
    b.setWeak(filler, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(filler, QStringLiteral("Length"), qint64(10));
    b.append(group, QStringLiteral("InputSegments"), filler);
    b.append(seq, QStringLiteral("Components"), group);
    addSlot(b, m, 1, rationalOf(25, 1), seq);
    QString error;
    const QByteArray bytes = compact(b.toBytes(&error));
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("aaf/negative-group-length.aaf")));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    aaf::File file;
    QVERIFY2(file.openData(bytes, QStringLiteral("negative.aaf"), &error), qPrintable(error));
    const aaf::ImportResult result = aaf::importMobs(file);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().kind, model::BinItemKind::Sequence);
}

// Found by fuzz_aaf (fuzz/regressions/aaf/slow-edit-rate.aaf): a sequence whose picture slot's edit
// rate is 1/3 (under one frame a second, so no timecode: 0 nominal frames a second), with a timecode
// slot. Its Start and End were made at that rate, whose frames per day are 0, and the wrap into one
// day divided by zero (a crash, SIGFPE). They are now left empty. CUTTHULHU_FUZZ_REGRESSIONS: see
// negativeGroupLength.
void TestAaf::slowEditRate() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    const aaf::Builder::Ref filler = b.create(QStringLiteral("Filler"));
    b.setWeak(filler, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(filler, QStringLiteral("Length"), qint64(10));
    addSlot(b, m, 1, rationalOf(1, 3), filler);
    const aaf::Builder::Ref tc = b.create(QStringLiteral("Timecode"));
    b.setWeak(tc, QStringLiteral("DataDefinition"), kTimecodeDef);
    b.set(tc, QStringLiteral("Length"), qint64(10));
    b.set(tc, QStringLiteral("Start"), qint64(90000));
    b.set(tc, QStringLiteral("FPS"), 25);
    b.set(tc, QStringLiteral("Drop"), false);
    addSlot(b, m, 2, rationalOf(25, 1), tc);
    QString error;
    const QByteArray bytes = compact(b.toBytes(&error));
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("aaf/slow-edit-rate.aaf")));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    aaf::File file;
    QVERIFY2(file.openData(bytes, QStringLiteral("slow.aaf"), &error), qPrintable(error));
    const aaf::ImportResult result = aaf::importMobs(file);
    QCOMPARE(result.items.size(), 1);
    QVERIFY(result.items.first().fields.value(QStringLiteral("Start")).isEmpty());
}

// Found by fuzz_aaf in the 24-hour run (25 s on a 250 KB file): strong reference vectors whose index
// lists one element many times, nested, so that the dump (aaf::dump) dumped each element as often as it
// was listed, the product of the counts at every level. Each storage is now dumped once; where it is
// reached again the dump has null. Here two nested sequences each list their only component 300 times
// (90,000 fillers before). CUTTHULHU_FUZZ_REGRESSIONS: see negativeGroupLength.
void TestAaf::sharedStrongRefs() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    const aaf::Builder::Ref outer = b.create(QStringLiteral("Sequence"));
    b.setWeak(outer, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(outer, QStringLiteral("Length"), qint64(10));
    const aaf::Builder::Ref inner = b.create(QStringLiteral("Sequence"));
    b.setWeak(inner, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(inner, QStringLiteral("Length"), qint64(10));
    const aaf::Builder::Ref filler = b.create(QStringLiteral("Filler"));
    b.setWeak(filler, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(filler, QStringLiteral("Length"), qint64(10));
    b.append(inner, QStringLiteral("Components"), filler);
    b.append(outer, QStringLiteral("Components"), inner);
    addSlot(b, m, 1, rationalOf(25, 1), outer);
    QString error;
    const QByteArray built = b.toBytes(&error);
    QVERIFY2(!built.isEmpty(), qPrintable(error));
    // Each Components index (count, two words, then one local key per element) lists its one key 300
    // times.
    constexpr quint32 kRepeats = 300;
    int edited = 0;
    const QByteArray bytes = compact(built, [&](const QString& name, const QByteArray& data) {
        if (!name.startsWith(QLatin1String("Components")) || !name.endsWith(QLatin1String(" index")) ||
            data.size() < 16 || qFromLittleEndian<quint32>(data.constData()) != 1) {
            return data;
        }
        ++edited;
        QByteArray index = data.left(12);
        qToLittleEndian<quint32>(kRepeats, index.data());
        for (quint32 i = 0; i < kRepeats; ++i) index.append(data.mid(12, 4));
        return index;
    });
    QVERIFY(!bytes.isEmpty());
    QCOMPARE(edited, 2);
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("aaf/shared-strong-refs.aaf")));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    aaf::File file;
    QVERIFY2(file.openData(bytes, QStringLiteral("shared.aaf"), &error), qPrintable(error));
    const QByteArray dumped = aaf::canonicalJson(aaf::dump(file));
    QCOMPARE(dumped.count("\"class\":\"Filler\""), 1);
    QCOMPARE(dumped.count("\"class\":\"Sequence\""), 2);
}

void TestAaf::hugeStart_data() {
    QTest::addColumn<qint64>("tapeStart");
    QTest::addColumn<qint64>("startTime");
    QTest::addColumn<QString>("start");
    // Frame counts from the file are clamped to 2^40 (detail::kMaxFrames) before they are converted
    // or added. A 30 fps start of 3 * 10^18 frames, whose start * 24 overflowed, is 2^40 frames: at
    // 24 fps 879,609,302,220, 07:13:12:12 into its day.
    QTest::newRow("scaled") << qint64(3'000'000'000'000'000'000) << qint64(0) << QStringLiteral("07:13:12:12");
    // A clip's StartTime added to the tape's start overflowed; clamped, 30 frames (24 at 24 fps) plus
    // 2^40 is 21:01:31:16 into its day.
    QTest::newRow("offset") << qint64(30) << std::numeric_limits<qint64>::max() << QStringLiteral("21:01:31:16");
    // The most negative start is -2^40 frames, -879,609,302,221 at 24 fps with the StartTime of -1; a
    // timecode before 00:00:00:00 wraps into the day before.
    QTest::newRow("negative") << std::numeric_limits<qint64>::min() << qint64(-1) << QStringLiteral("16:46:47:11");
}

// Found by fuzz_aaf in the 24-hour run (UBSan: signed integer overflow): a master clip whose tape
// timecode starts near 2^62 frames, at 30 fps on a 24 fps clip. Converting the start to the clip's
// rate (startAtRate) multiplied before dividing, and the clip's StartTime was added unchecked; both
// overflowed std::int64_t, undefined behaviour, as did End (Start plus the duration). The conversion
// now divides first, and the sums are held at the limits of std::int64_t (detail::saturatingAdd, which
// the AVB import shares). CUTTHULHU_FUZZ_REGRESSIONS: see negativeGroupLength.
void TestAaf::hugeStart() {
    QFETCH(qint64, tapeStart);
    QFETCH(qint64, startTime);
    QFETCH(QString, start);
    aaf::Builder b;
    startFile(b);
    const MobId tapeId = MobId::generate();
    const aaf::Builder::Ref tape = addMob(b, QStringLiteral("SourceMob"), tapeId, QStringLiteral("TAPE1"));
    b.setObject(tape, QStringLiteral("EssenceDescription"), b.create(QStringLiteral("TapeDescriptor")));
    const aaf::Builder::Ref tc = b.create(QStringLiteral("Timecode"));
    b.setWeak(tc, QStringLiteral("DataDefinition"), kTimecodeDef);
    b.set(tc, QStringLiteral("Length"), qint64(1000000));
    b.set(tc, QStringLiteral("Start"), tapeStart);
    b.set(tc, QStringLiteral("FPS"), 30);
    b.set(tc, QStringLiteral("Drop"), false);
    addSlot(b, tape, 1, rationalOf(30, 1), tc);
    const aaf::Builder::Ref master = addMob(b, QStringLiteral("MasterMob"), MobId::generate(), QStringLiteral("Clip"));
    const aaf::Builder::Ref clip = b.create(QStringLiteral("SourceClip"));
    b.setWeak(clip, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(clip, QStringLiteral("Length"), qint64(48));
    b.set(clip, QStringLiteral("SourceID"), QVariant::fromValue(tapeId));
    b.set(clip, QStringLiteral("SourceMobSlotID"), 1);
    b.set(clip, QStringLiteral("StartTime"), startTime);
    addSlot(b, master, 1, rationalOf(24, 1), clip);
    QString error;
    const QByteArray bytes = compact(b.toBytes(&error));
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("aaf/huge-start-%1.aaf").arg(QString::fromLatin1(QTest::currentDataTag()))));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    aaf::File file;
    QVERIFY2(file.openData(bytes, QStringLiteral("huge.aaf"), &error), qPrintable(error));
    const aaf::ImportResult result = aaf::importMobs(file);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().fields.value(QStringLiteral("Start")), start);
}

// Found by fuzz_aaf in the third 24-hour run (UBSan: signed integer overflow in the import's
// Flattener): an effect whose input segments' lengths, near 2^63, were added up to fit them to the
// effect's length, overflowing. Lengths are now clamped to 2^40 frames as they are read
// (detail::clampFrames), so no such sum can overflow; the effect lasts its own 10 frames.
// CUTTHULHU_FUZZ_REGRESSIONS: see negativeGroupLength.
void TestAaf::hugeLengths() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    const aaf::Builder::Ref inputs = b.create(QStringLiteral("Sequence"));
    b.setWeak(inputs, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(inputs, QStringLiteral("Length"), std::numeric_limits<qint64>::max());
    for (const qint64 length : {std::numeric_limits<qint64>::max() - 1, qint64{1} << 62}) {
        const aaf::Builder::Ref filler = b.create(QStringLiteral("Filler"));
        b.setWeak(filler, QStringLiteral("DataDefinition"), kPictureDef);
        b.set(filler, QStringLiteral("Length"), length);
        b.append(inputs, QStringLiteral("Components"), filler);
    }
    const aaf::Builder::Ref group = b.create(QStringLiteral("OperationGroup"));
    b.setWeak(group, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(group, QStringLiteral("Length"), qint64(10));
    b.append(group, QStringLiteral("InputSegments"), inputs);
    const aaf::Builder::Ref seq = b.create(QStringLiteral("Sequence"));
    b.setWeak(seq, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(seq, QStringLiteral("Length"), qint64(10));
    b.append(seq, QStringLiteral("Components"), group);
    addSlot(b, m, 1, rationalOf(25, 1), seq);
    QString error;
    const QByteArray bytes = compact(b.toBytes(&error));
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("aaf/huge-lengths.aaf")));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    aaf::File file;
    QVERIFY2(file.openData(bytes, QStringLiteral("lengths.aaf"), &error), qPrintable(error));
    const aaf::ImportResult result = aaf::importMobs(file);
    QCOMPARE(result.items.size(), 1);
    const model::Sequence sequence = model::Sequence::fromItem(result.items.first());
    QCOMPARE(sequence.duration(), 10);
}

// A damaged file whose lengths and rates do not fit a Rational is an error, not a crash: Rational
// throws rather than wrap, and importFile() reports it.
void TestAaf::numbersPastRational() {
    aaf::Builder b;
    startFile(b);
    const aaf::Builder::Ref m = addMob(b, QStringLiteral("CompositionMob"), MobId::generate(), QStringLiteral("Cut"));
    b.set(m, QStringLiteral("UsageCode"), QStringLiteral("Usage_TopLevel"));
    // Two picture tracks at different rates: the clip's duration converts the second's length to
    // the first's rate.
    const aaf::Builder::Ref first = b.create(QStringLiteral("Filler"));
    b.setWeak(first, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(first, QStringLiteral("Length"), qint64(25));
    addSlot(b, m, 1, rationalOf(25, 1), first);
    const aaf::Builder::Ref second = b.create(QStringLiteral("Filler"));
    b.setWeak(second, QStringLiteral("DataDefinition"), kPictureDef);
    b.set(second, QStringLiteral("Length"), std::numeric_limits<qint64>::max() / 3);
    // Lengths are clamped to 2^40 frames (detail::kMaxFrames); at one frame in 2^31 - 1 seconds, that
    // is still past what converts to 25 fps.
    addSlot(b, m, 2, rationalOf(1, 2147483647), second);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("huge.aaf"));
    QString error;
    QVERIFY2(b.save(path, &error), qPrintable(error));
    const auto result = aaf::importFile(path, {}, &error);
    QVERIFY2(!result, "the lengths should not fit");
    QVERIFY2(error.contains(QStringLiteral("huge.aaf")), qPrintable(error));
    // It is the overflow that importFile() turned into the error.
    aaf::File file;
    QVERIFY(file.open(path, &error));
    QVERIFY_THROWS_EXCEPTION(std::overflow_error, aaf::importMobs(file));
}

QTEST_GUILESS_MAIN(TestAaf)
#include "tst_aaf.moc"
