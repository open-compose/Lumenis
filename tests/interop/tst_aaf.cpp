#include "lumenis/interop/aaf.h"
#include "lumenis/interop/aaf_export.h"
#include "lumenis/interop/aaf_import.h"
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

using namespace lumenis;
using namespace lumenis::interop;

namespace {

QString corpus() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    return !folder.isEmpty() && QDir(folder).exists() ? folder : QString();
}

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
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.aaf"));
    QRandomGenerator random(7);
    int opened = 0;
    for (int i = 0; i < 200; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 16; ++k) {
            bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
        f.close();
        aaf::File file;
        if (!file.open(path)) continue;
        ++opened;
        aaf::canonicalJson(aaf::dump(file));
        for (const aaf::Object& m : file.mobs()) m.string(QStringLiteral("Name"));
    }
    QVERIFY(opened > 0);
}

QTEST_GUILESS_MAIN(TestAaf)
#include "tst_aaf.moc"
