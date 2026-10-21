// Tests of the Media Composer bin (.avb) reader (src/interop/avb.*,
// docs/spec/avb.md): the corpus bins dump as pyavb reads them (digests in
// tests/data/interop_corpus.json, made by `interop-corpus.py manifest`), the
// object API on test_file_01.avb, and damaged files; the import into bins
// (src/interop/avb_import.*) on test_file_01.avb and a retimes bin, for what
// the structure dumps (ctest avb_import_bins) do not show. And the writer
// (src/interop/avb_writer.*, docs/spec/avb.md, "Writing"): corpus bins
// written back byte for byte and in the other byte order, a bin built from
// scratch read back, and values the writer must refuse; forPyavb writes
// bins for pyavb to read (ctest avb_write_pyavb).

#include "lumenis/interop/aaf.h"
#include "lumenis/interop/avb.h"
#include "lumenis/interop/avb_import.h"
#include "lumenis/interop/avb_writer.h"
#include "lumenis/model/sequence.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QTimeZone>
#include <QtTest>

#include <exception>
#include <limits>

using namespace lumenis;
using namespace lumenis::interop;

namespace {

QString corpus() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    return !folder.isEmpty() && QDir(folder).exists() ? folder : QString();
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestAvb : public QObject {
    Q_OBJECT

private slots:
    void strings();
    void floats();
    void corpusDumps();
    void objects();
    void notAvb();
    void damaged();
    void importsBin();
    void importsRetime();
    void importsDamaged();
    void rewritesCorpus_data();
    void rewritesCorpus();
    void buildsBin_data();
    void buildsBin();
    void editRates();
    void writerRefuses();
    void rewritesDamaged();
    void forPyavb();
};

void TestAvb::strings() {
    // Mac OS Roman, as Python's mac_roman codec decodes it.
    QCOMPARE(avb::fromMacRoman(QByteArray("A\x80\xa5\xdb\xf0", 5)), QString::fromUtf8("A\xc3\x84\xe2\x80\xa2\xe2\x82\xac\xef\xa3\xbf"));
    // Strict UTF-8: overlong forms, surrogates and cut sequences are refused.
    QCOMPARE(*avb::fromUtf8Strict("caf\xc3\xa9"), QString::fromUtf8("caf\xc3\xa9"));
    QVERIFY(!avb::fromUtf8Strict("\xc0\xaf"));
    QVERIFY(!avb::fromUtf8Strict("\xed\xa0\x80"));
    QVERIFY(!avb::fromUtf8Strict("\xe2\x82"));
    QVERIFY(!avb::fromUtf8Strict("\xf4\x90\x80\x80"));
    QVERIFY(avb::fromUtf8Strict("\xf0\x9f\x98\x80").has_value());
}

void TestAvb::floats() {
    // Python's repr() of a float.
    QCOMPARE(avb::pythonRepr(25.0), QStringLiteral("25.0"));
    QCOMPARE(avb::pythonRepr(23976 * 0.001), QStringLiteral("23.976"));
    QCOMPARE(avb::pythonRepr(0.1 + 0.2), QStringLiteral("0.30000000000000004"));
    QCOMPARE(avb::pythonRepr(1e-5), QStringLiteral("1e-05"));
    QCOMPARE(avb::pythonRepr(0.0001), QStringLiteral("0.0001"));
    QCOMPARE(avb::pythonRepr(1e16), QStringLiteral("1e+16"));
    QCOMPARE(avb::pythonRepr(1e15), QStringLiteral("1000000000000000.0"));
    QCOMPARE(avb::pythonRepr(-2.5e-300), QStringLiteral("-2.5e-300"));
    QCOMPARE(avb::pythonRepr(-0.0), QStringLiteral("-0.0"));
    QCOMPARE(avb::pythonRepr(std::numeric_limits<double>::infinity()), QStringLiteral("inf"));
}

// Every corpus bin reads as pyavb reads it: the same dump
// (scripts/avb-dump.py), compared by digest.
void TestAvb::corpusDumps() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QJsonObject files = QJsonDocument::fromJson(readAll(QStringLiteral(LUMENIS_TEST_DATA "/interop_corpus.json")))
                                  .object()
                                  .value(QStringLiteral("avb"))
                                  .toObject();
    QCOMPARE(files.size(), 14);
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = QDir(folder).filePath(it.key());
        if (!QFile::exists(path)) continue;
        avb::File file;
        QString error;
        QVERIFY2(file.open(path, &error), qPrintable(error));
        for (std::uint32_t i = 1; i <= file.objectCount(); ++i) {
            const avb::Object& o = file.object(i);
            QVERIFY2(o.ok(), qPrintable(it.key() + QLatin1String(": ") + o.error));
        }
        const QByteArray text = aaf::canonicalJson(avb::dump(file));
        const QJsonObject want = it.value().toObject();
        QCOMPARE(static_cast<int>(text.count("\"class\":")), want.value(QStringLiteral("objects")).toInt());
        QVERIFY2(QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex()) ==
                     want.value(QStringLiteral("dump")).toString(),
                 qPrintable(it.key() + QLatin1String(": the dump is not pyavb's (scripts/avb-dump.py --compare)")));
    }
}

// The object API on test_file_01.avb (Media Composer 18.9), with what pyavb
// reads from it.
void TestAvb::objects() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    avb::File file;
    QString error;
    QVERIFY2(file.open(QDir(folder).filePath(QStringLiteral("pyavb/test_file_01.avb")), &error), qPrintable(error));
    QVERIFY(file.littleEndian());
    QCOMPARE(file.objectCount(), 2190u);
    QCOMPARE(file.rootIndex(), 2190u);
    QCOMPARE(file.creatorVersion().trimmed(), QStringLiteral("Media Composer 18.9.0.49510"));
    QCOMPARE(file.lastSave().toDateTime().date(), QDate(2018, 11, 12));

    QMap<QString, int> classes;
    for (std::uint32_t i = 1; i <= file.objectCount(); ++i) ++classes[file.classIdAt(i)];
    QCOMPARE(classes.value(QStringLiteral("CMPO")), 160);
    QCOMPARE(classes.value(QStringLiteral("SCLP")), 220);
    QCOMPARE(classes.value(QStringLiteral("ATTR")), 906);
    QCOMPARE(classes.value(QStringLiteral("SEQU")), 27);
    QCOMPARE(classes.value(QStringLiteral("ABIN")), 1);
    QCOMPARE(file.classIdAt(0), QString());
    QCOMPARE(file.classIdAt(2191), QString());
    QVERIFY(!file.object(2191).ok());

    const avb::Object& bin = file.root();
    QVERIFY2(bin.ok(), qPrintable(bin.error));
    QCOMPARE(bin.classId, QStringLiteral("ABIN"));
    QCOMPARE(bin.property(QStringLiteral("large_bin")).toInt(), 0);
    const avb::Object& view = file.object(*bin.property(QStringLiteral("view_setting")).toRef());
    QCOMPARE(view.classId, QStringLiteral("BVst"));
    QCOMPARE(view.property(QStringLiteral("name")).toString(), QStringLiteral("Untitled"));
    QCOMPARE(view.property(QStringLiteral("kind")).toString(), QStringLiteral("Bin View"));
    QCOMPARE(view.property(QStringLiteral("columns")).toList().size(), std::size_t(186));

    const avb::ValueList& items = bin.property(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), std::size_t(160));
    const auto ref = items.front().member(QStringLiteral("mob")).toRef();
    QVERIFY(ref);
    QCOMPARE(ref->index, 2188u);
    const avb::Object& mob = file.object(*ref);
    QCOMPARE(mob.classId, QStringLiteral("CMPO"));
    QCOMPARE(mob.property(QStringLiteral("name")).toString(), QStringLiteral("646x01_02_dustbust.0001.tif"));
    QCOMPARE(mob.property(QStringLiteral("mob_type_id")).toInt(), 2); // a master mob
    QCOMPARE(QString::fromStdString(mob.property(QStringLiteral("mob_id")).toMobId()->toString()),
             QStringLiteral("urn:smpte:umid:060a2b34.01010101.01010f00.13000000.060e2b34.7f7f2a80.4fd557ff.a4e504d0"));
    QCOMPARE(mob.property(QStringLiteral("edit_rate")).toDouble(), 23.976);
    QCOMPARE(mob.property(QStringLiteral("tracks")).toList().size(), std::size_t(1));
    // A property that is not there reads as null.
    QVERIFY(mob.property(QStringLiteral("no such")).isNull());
    // The same object again comes from the cache.
    QCOMPARE(&file.object(*ref), &mob);
}

void TestAvb::notAvb() {
    avb::File file;
    QString error;
    QVERIFY(!file.load(QByteArray(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!file.load(QByteArray("\x06\x00Domaix", 8), &error));
    QVERIFY(!file.load(QByteArray("\x06\x00" "Domain", 8), &error)); // nothing after
    QVERIFY(!file.isOpen());
    QVERIFY(avb::dump(file).isNull());
}

// Randomly damaged bins open or are refused, and dump, without a crash or a
// hang: every read is bounds-checked, every count is limited by the bytes.
void TestAvb::damaged() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QByteArray good = readAll(QDir(folder).filePath(QStringLiteral("pyavb/retimes/bezier01.avb")));
    QVERIFY(!good.isEmpty());
    QRandomGenerator random(11);
    int opened = 0;
    int broken = 0;
    for (int i = 0; i < 200; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 16; ++k) {
            bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        }
        if (i % 10 == 9) bytes.truncate(random.bounded(static_cast<int>(bytes.size())));
        avb::File file;
        if (!file.load(bytes)) continue;
        ++opened;
        for (std::uint32_t j = 1; j <= file.objectCount(); ++j) {
            if (!file.object(j).ok()) ++broken;
        }
        aaf::canonicalJson(avb::dump(file));
    }
    QVERIFY(opened > 0);
    QVERIFY(broken > 0);
}

namespace {

const model::BinItem* named(const QList<model::BinItem>& items, const QString& name) {
    for (const model::BinItem& item : items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

} // namespace

// test_file_01.avb read into bin items: the kinds, a master clip's source
// chain and columns, a reference clip, the sequence's segments and
// transitions, and the warnings for what is read only in part.
void TestAvb::importsBin() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    avb::ImportOptions options;
    options.binId = QStringLiteral("bin-1");
    QString error;
    const auto imported = avb::importFile(QDir(folder).filePath(QStringLiteral("pyavb/test_file_01.avb")), options, &error);
    QVERIFY2(imported, qPrintable(error));
    QMap<model::BinItemKind, int> kinds;
    for (const model::BinItem& item : imported->items) ++kinds[item.kind];
    // 50 placed master clips and the title's clip, which is not placed.
    QCOMPARE(kinds.value(model::BinItemKind::MasterClip), 51);
    QCOMPARE(kinds.value(model::BinItemKind::AudioClip), 1);
    QCOMPARE(kinds.value(model::BinItemKind::Sequence), 1);
    QCOMPARE(kinds.value(model::BinItemKind::Subclip), 0);
    // Clips first, then sequences.
    QCOMPARE(imported->items.last().kind, model::BinItemKind::Sequence);
    QCOMPARE(imported->customColumns, QStringList{QStringLiteral("UNC Path")});

    const model::BinItem* clip = named(imported->items, QStringLiteral("52_64_2_03_05.mov"));
    QVERIFY(clip);
    QVERIFY(clip->userPlaced);
    QCOMPARE(QString::fromStdString(clip->mobId.toString()),
             QStringLiteral("urn:smpte:umid:060a2b34.01010101.01010f00.13000000.060e2b34.7f7f2a80.48c9f200.5a940184"));
    QCOMPARE(clip->editRate, Rational(24));
    QCOMPARE(clip->durationFrames, std::int64_t{99});
    QCOMPARE(clip->fields.value(QStringLiteral("Start")), QStringLiteral("00:00:30:00"));
    QCOMPARE(clip->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1"));
    QCOMPARE(clip->fields.value(QStringLiteral("UNC Path")), QStringLiteral("X:\\demo\\film\\luna\\quicktimes\\52_64_2_03_05.mov"));
    QVERIFY(!clip->fields.contains(QStringLiteral("Comments"))); // empty in the bin
    QVERIFY(clip->created.isValid());
    QVERIFY(clip->created <= clip->modified);
    // The source chain: the media file (managed, found by MobID: no path),
    // then the QuickTime file it was imported from, at 30 fps drop frame.
    QCOMPARE(clip->sources.size(), 2);
    const model::ClipSource& file = clip->sources.at(0);
    QCOMPARE(file.kind, model::ClipSource::Kind::File);
    QCOMPARE(file.track, QStringLiteral("V1"));
    QCOMPARE(file.name, QStringLiteral("52_64_2_03_05.mov.PHYS"));
    QVERIFY(file.path.isEmpty());
    const model::ClipSource& import = clip->sources.at(1);
    QCOMPARE(import.kind, model::ClipSource::Kind::Import);
    QCOMPARE(file.from, import.mob);
    QCOMPARE(import.path, QStringLiteral("/Giraffe/Bunny/X/demo/film/luna/quicktimes/52_64_2_03_05.mov"));
    QCOMPARE(import.startTimecode, QStringLiteral("00:00:30;00"));

    const model::BinItem* audio = named(imported->items, QStringLiteral("Beverly"));
    QVERIFY(audio);
    QCOMPARE(audio->kind, model::BinItemKind::AudioClip);
    QCOMPARE(audio->editRate, Rational(24000, 1001));
    QCOMPARE(audio->fields.value(QStringLiteral("Tracks")), QStringLiteral("A1-2"));
    // A clip the bin holds only for the sequence that uses it (pyavb's user_placed).
    const model::BinItem* title = named(imported->items, QStringLiteral("Temp Title Demo 2012.01"));
    QVERIFY(title);
    QVERIFY(!title->userPlaced);

    const model::BinItem& sequenceItem = imported->items.last();
    QCOMPARE(sequenceItem.name, QStringLiteral("Demo_2012_v0002.Exported.01"));
    QCOMPARE(sequenceItem.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1-4 A1-2 TC1"));
    QCOMPARE(sequenceItem.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    QCOMPARE(sequenceItem.durationFrames, std::int64_t{4794});
    const model::Sequence sequence = model::Sequence::fromItem(sequenceItem);
    const model::Track* v1 = sequence.track(QStringLiteral("V1"));
    QVERIFY(v1);
    QCOMPARE(v1->segments.size(), 54);
    QVERIFY(v1->segments.at(0).isFiller());
    QCOMPARE(v1->segments.at(0).length, std::int64_t{134});
    const model::Segment& first = v1->segments.at(1);
    const model::BinItem* morrwind = named(imported->items, QStringLiteral("morrwind_001.mov"));
    QVERIFY(morrwind);
    QCOMPARE(first.clipId, morrwind->id);
    QCOMPARE(first.binId, options.binId);
    QCOMPARE(first.sourceMob, morrwind->mobId);
    QCOMPARE(first.sourceTrack, QStringLiteral("V1"));
    QCOMPARE(first.sourceIn, std::int64_t{136});
    QCOMPARE(first.length, std::int64_t{219});
    // The dissolve into it sits at the cut.
    QVERIFY(first.transition);
    QCOMPARE(first.transition->effect.category, QStringLiteral("Blend"));
    QCOMPARE(first.transition->effect.name, QStringLiteral("Dissolve"));
    // The title over V2, an effect kept by its Media Composer effect ID.
    const model::Track* v2 = sequence.track(QStringLiteral("V2"));
    QVERIFY(v2 && !v2->segments.isEmpty());
    QCOMPARE(v2->segments.first().clipId, title->id);
    QVERIFY(v2->segments.first().effect);
    QCOMPARE(v2->segments.first().effect->name, QStringLiteral("EFF2_BLEND_GRAPHIC"));
    const model::Track* a1 = sequence.track(QStringLiteral("A1"));
    QVERIFY(a1 && a1->segments.size() == 1);
    QCOMPARE(a1->segments.first().clipId, audio->id);
    QCOMPARE(a1->segments.first().sourceTrack, QStringLiteral("A1"));
    QVERIFY(imported->warnings.contains(QStringLiteral("edgecode tracks left out")));
    QVERIFY(imported->warnings.contains(QStringLiteral("2 × clip gain and pan (Audio Pan Volume) not read")));
}

// A retimes bin: a 24 fps clip in a 23.976 sequence through a motion effect
// inside Media Composer's spatial adapter, which keeps the motion effect.
void TestAvb::importsRetime() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    avb::File file;
    QString error;
    QVERIFY2(file.open(QDir(folder).filePath(QStringLiteral("pyavb/retimes/linear01.avb")), &error), qPrintable(error));
    avb::ImportOptions options;
    options.binId = QStringLiteral("retimes");
    options.when = QDateTime(QDate(2024, 1, 2), QTime(3, 4, 5), QTimeZone::utc());
    const avb::ImportResult imported = avb::importBin(file, options);
    QCOMPARE(imported.items.size(), 2);
    const model::BinItem& clip = imported.items.at(0);
    QCOMPARE(clip.kind, model::BinItemKind::MasterClip);
    QCOMPARE(clip.durationFrames, std::int64_t{57});
    QCOMPARE(clip.fields.value(QStringLiteral("End")), QStringLiteral("00:00:32:09"));
    const model::BinItem& sequenceItem = imported.items.at(1);
    QCOMPARE(sequenceItem.kind, model::BinItemKind::Sequence);
    QCOMPARE(sequenceItem.editRate, Rational(24000, 1001));
    QCOMPARE(sequenceItem.created, QDateTime::fromSecsSinceEpoch(1532311687, QTimeZone::utc()));
    QVERIFY(sequenceItem.userPlaced);
    const model::Sequence sequence = model::Sequence::fromItem(sequenceItem);
    QCOMPARE(sequence.trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}));
    QVERIFY(sequence.hasTimecodeTrack());
    const model::Track* v1 = sequence.track(QStringLiteral("V1"));
    QVERIFY(v1 && v1->segments.size() == 1);
    const model::Segment& s = v1->segments.first();
    QCOMPARE(s.clipId, clip.id);
    QCOMPARE(s.binId, QStringLiteral("retimes"));
    QCOMPARE(s.length, std::int64_t{56});
    QVERIFY(s.effect);
    QCOMPARE(s.effect->category, QStringLiteral("AVB"));
    QCOMPARE(s.effect->name, QStringLiteral("EFF_ADV_MOTION_CTL"));
    QVERIFY(sequence.track(QStringLiteral("A1"))->segments.first().isFiller());
    QVERIFY(imported.warnings.contains(QStringLiteral("effects nested in effects: the innermost kept")));
}

// Damaged bins import or are refused, without a crash or a hang; a file
// that is not a bin is an error.
void TestAvb::importsDamaged() {
    QString error;
    QVERIFY(!avb::importFile(QStringLiteral("/no/such/bin.avb"), {}, &error));
    QVERIFY(!error.isEmpty());
    avb::File empty;
    QVERIFY(avb::importBin(empty).items.isEmpty());
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QByteArray good = readAll(QDir(folder).filePath(QStringLiteral("pyavb/test_file_01.avb")));
    QVERIFY(!good.isEmpty());
    QRandomGenerator random(23);
    int imported = 0;
    for (int i = 0; i < 100; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 32; ++k) {
            bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        }
        if (i % 10 == 9) bytes.truncate(random.bounded(static_cast<int>(bytes.size())));
        avb::File file;
        if (!file.load(bytes)) continue;
        try {
            const avb::ImportResult result = avb::importBin(file);
            if (!result.items.isEmpty()) ++imported;
        } catch (const std::exception&) {
            // Numbers past what Rational holds: importFile() reports these as errors.
        }
    }
    QVERIFY(imported > 0);
}

// ---- The writer ------------------------------------------------------------------

namespace {

// Two values the same, lists and records compared by content.
bool sameValue(const avb::Value& a, const avb::Value& b) {
    const avb::Value::Data& x = a.data();
    const avb::Value::Data& y = b.data();
    if (x.index() != y.index()) return false;
    if (std::holds_alternative<std::shared_ptr<const avb::ValueList>>(x)) {
        const avb::ValueList& l = a.toList();
        const avb::ValueList& m = b.toList();
        if (l.size() != m.size()) return false;
        for (std::size_t i = 0; i < l.size(); ++i) {
            if (!sameValue(l[i], m[i])) return false;
        }
        return true;
    }
    if (std::holds_alternative<std::shared_ptr<const avb::Properties>>(x)) {
        const avb::Properties& p = a.toRecord();
        const avb::Properties& q = b.toRecord();
        if (p.size() != q.size()) return false;
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (p[i].name != q[i].name || !sameValue(p[i].value, q[i].value)) return false;
        }
        return true;
    }
    return x == y;
}

avb::Property prop(const char* name, avb::Value value) { return {QString::fromLatin1(name), std::move(value)}; }

// What every component starts with (Component's part), for an object of
// the given media kind and edit rate.
avb::Properties component(qint64 mediaKind, double editRate, const QString& name = {}) {
    using avb::Value;
    return {prop("left_bob", Value::ref(0)),
            prop("right_bob", Value::ref(0)),
            prop("media_kind_id", Value::integer(mediaKind)),
            prop("edit_rate", Value::real(editRate)),
            prop("name", name.isEmpty() ? Value() : Value::string(name)),
            prop("effect_id", Value()),
            prop("attributes", Value::ref(0)),
            prop("session_attrs", Value::ref(0)),
            prop("precomputed", Value::ref(0))};
}

avb::Properties with(avb::Properties p, std::initializer_list<avb::Property> more) {
    for (const avb::Property& e : more) avb::set(p, e.name, e.value);
    return p;
}

avb::Value track(std::uint32_t component, qint64 label) {
    using avb::Value;
    return Value::record({prop("index", Value::integer(label)), prop("component", Value::ref(component)),
                          prop("lock_number", Value::integer(0))});
}

const MobId kMasterMob = MobId::withMaterial({0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
                                              0xdd, 0xee, 0xff, 0x01});
const MobId kSourceMob = MobId::withMaterial({0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78, 0x87, 0x96, 0xa5, 0xb4,
                                              0xc3, 0xd2, 0xe1, 0xf0});

// A bin made from scratch with the writer's API, as 3.4b will make them: a
// master clip (CMPO, mob type 2) with a picture track whose source clip
// names a source mob (mob type 3) imported from a file, its user columns
// (ATTR), a view (BVst) and the bin (ABIN), whose items list both mobs.
avb::Builder scratchBin(bool littleEndian) {
    using avb::Value;
    avb::Header header;
    header.littleEndian = littleEndian;
    header.lastSave = avb::Time{1700000000};
    header.lastSaveText = QStringLiteral("2023/11/14 22:13:20");
    header.creatorVersion = QStringLiteral("Cutthulhu 3.4a");
    avb::Builder b(header);
    const std::uint32_t user = b.add(QStringLiteral("ATTR"), {prop("Comments", Value::string(QStringLiteral("made from scratch"))),
                                                              prop("Take", Value::integer(3))});
    const std::uint32_t attributes = b.add(QStringLiteral("ATTR"), {prop("_USER", Value::ref(user)),
                                                                    prop("_VERSION", Value::integer(2))});
    const std::uint32_t locator = b.add(
        QStringLiteral("FILE"), {prop("path", Value::string(QStringLiteral("clip.mov"))),
                                 prop("path_posix", Value::string(QStringLiteral("/Media/clip.mov"))),
                                 prop("path_utf8", Value::string(QString::fromUtf8("/Media/cl\xc3\xafp.mov")))});
    const std::uint32_t descriptor = b.add(QStringLiteral("MDES"), {prop("mob_kind", Value::integer(5)),
                                                                    prop("locator", Value::ref(locator)),
                                                                    prop("intermediate", Value::boolean(false)),
                                                                    prop("physical_media", Value::ref(0))});
    // The source mob's own track ends the chain: a source clip with a null MobID.
    const std::uint32_t chainEnd = b.add(
        QStringLiteral("SCLP"), with(component(1, 25.0), {prop("length", Value::integer(100)),
                                                          prop("track_id", Value::integer(0)),
                                                          prop("start_time", Value::integer(0)),
                                                          prop("mob_id", Value::mobId(MobId()))}));
    const avb::Properties mobBase = with(component(0, 25.0), {prop("mc_mode", Value::integer(0)),
                                                              prop("length", Value::integer(100)),
                                                              prop("num_scalars", Value::integer(0))});
    const std::uint32_t sourceMob = b.add(
        QStringLiteral("CMPO"),
        with(mobBase, {prop("name", Value::string(QStringLiteral("clip.mov"))),
                       prop("tracks", Value::list({track(chainEnd, 1)})),
                       prop("last_modified", Value::time(1700000000)), prop("mob_type_id", Value::integer(3)),
                       prop("usage_code", Value::integer(0)), prop("descriptor", Value::ref(descriptor)),
                       prop("creation_time", Value::time(1700000000)), prop("mob_id", Value::mobId(kSourceMob))}));
    const std::uint32_t clip = b.add(
        QStringLiteral("SCLP"), with(component(1, 25.0), {prop("length", Value::integer(100)),
                                                          prop("track_id", Value::integer(1)),
                                                          prop("start_time", Value::integer(10)),
                                                          prop("mob_id", Value::mobId(kSourceMob))}));
    const std::uint32_t master = b.add(
        QStringLiteral("CMPO"),
        with(mobBase, {prop("name", Value::string(QStringLiteral("Scratch clip"))),
                       prop("attributes", Value::ref(attributes)), prop("tracks", Value::list({track(clip, 1)})),
                       prop("last_modified", Value::time(1700000100)), prop("mob_type_id", Value::integer(2)),
                       prop("usage_code", Value::integer(0)), prop("descriptor", Value::ref(0)),
                       prop("creation_time", Value::time(1700000000)), prop("mob_id", Value::mobId(kMasterMob))}));
    auto column = [](const char* title, qint64 format, qint64 type) {
        return Value::record({prop("title", Value::string(QString::fromLatin1(title))), prop("format", Value::integer(format)),
                              prop("type", Value::integer(type)), prop("hidden", Value::boolean(false))});
    };
    const std::uint32_t view = b.add(
        QStringLiteral("BVst"),
        {prop("name", Value::string(QStringLiteral("Untitled"))), prop("kind", Value::string(QStringLiteral("Bin View"))),
         prop("attr_count", Value::integer(0)), prop("attr_type", Value::integer(0)), prop("attributes", Value::ref(0)),
         prop("columns", Value::list({column("Name", 20, 201), column("Duration", 100, 4)}))});
    auto item = [](std::uint32_t mob, bool placed) {
        return Value::record({prop("mob", Value::ref(mob)), prop("x", Value::integer(0)), prop("y", Value::integer(0)),
                              prop("keyframe", Value::integer(0)), prop("user_placed", Value::boolean(placed))});
    };
    avb::ValueList sifts;
    for (int i = 0; i < 6; ++i) {
        sifts.push_back(Value::record({prop("method", Value::integer(1)), prop("string", Value::string(QString())),
                                       prop("column", Value::string(QStringLiteral("Any")))}));
    }
    auto ints = [](std::initializer_list<qint64> l) {
        avb::ValueList out;
        for (const qint64 i : l) out.push_back(Value::integer(i));
        return Value::list(std::move(out));
    };
    b.add(QStringLiteral("ABIN"),
          {prop("large_bin", Value::boolean(false)), prop("view_setting", Value::ref(view)),
           prop("uid", Value::uint64(0x8000000000000001ull)),
           prop("items", Value::list({item(master, true), item(sourceMob, false)})),
           prop("display_mask", Value::integer(98999)), prop("display_mode", Value::integer(0)),
           prop("sifted", Value::boolean(false)), prop("sifted_settings", Value::list(sifts)),
           prop("sort_columns", Value::list({Value::list({Value::integer(0), Value::string(QStringLiteral("Name"))})})),
           prop("mac_font", Value::integer(139)), prop("mac_font_size", Value::integer(11)),
           prop("mac_image_scale", Value::integer(5)), prop("home_rect", ints({0, 0, 400, 600})),
           prop("background_color", ints({25600, 25600, 25600})), prop("forground_color", ints({52224, 52224, 52224})),
           prop("ql_image_scale", Value::integer(6)), prop("attributes", Value::ref(0)),
           prop("was_iconic", Value::boolean(false))});
    return b;
}

} // namespace

void TestAvb::rewritesCorpus_data() {
    QTest::addColumn<QString>("name");
    QTest::newRow("test_file_01") << QStringLiteral("pyavb/test_file_01.avb");
    QTest::newRow("retimes/spline02") << QStringLiteral("pyavb/retimes/spline02.avb");
}

// A corpus bin read and written back is the same file, byte for byte; in
// big-endian order it reads the same, and written back little-endian from
// that it is the same file again.
void TestAvb::rewritesCorpus() {
    QFETCH(QString, name);
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QByteArray original = readAll(QDir(folder).filePath(name));
    QVERIFY(!original.isEmpty());
    avb::File file;
    QString error;
    QVERIFY2(file.load(original, &error), qPrintable(error));
    const QByteArray written = avb::rewrite(file, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QCOMPARE(written.size(), original.size());
    QVERIFY(written == original);

    const QByteArray big = avb::rewrite(file, &error, false);
    QVERIFY2(!big.isEmpty(), qPrintable(error));
    QVERIFY(big.startsWith(QByteArray("\x00\x06" "Domain", 8)));
    avb::File bigFile;
    QVERIFY2(bigFile.load(big, &error), qPrintable(error));
    QVERIFY(!bigFile.littleEndian());
    QCOMPARE(bigFile.objectCount(), file.objectCount());
    for (std::uint32_t i = 1; i <= bigFile.objectCount(); ++i) QVERIFY2(bigFile.object(i).ok(), qPrintable(bigFile.object(i).error));
    QCOMPARE(aaf::canonicalJson(avb::dump(bigFile)), aaf::canonicalJson(avb::dump(file)));
    QCOMPARE(bigFile.creatorVersion(), file.creatorVersion());
    QCOMPARE(bigFile.lastSaveText(), file.lastSaveText());
    QVERIFY(avb::rewrite(bigFile, &error, true) == original);
}

void TestAvb::buildsBin_data() {
    QTest::addColumn<bool>("littleEndian");
    QTest::newRow("little-endian") << true;
    QTest::newRow("big-endian") << false;
}

// A bin built from scratch reads back with every property it was given,
// imports as a master clip, and writes back to the same bytes.
void TestAvb::buildsBin() {
    QFETCH(bool, littleEndian);
    const avb::Builder builder = scratchBin(littleEndian);
    QString error;
    const QByteArray bytes = builder.toBytes(&error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    avb::File file;
    QVERIFY2(file.load(bytes, &error), qPrintable(error));
    QCOMPARE(file.littleEndian(), littleEndian);
    QCOMPARE(file.objectCount(), builder.count());
    QCOMPARE(file.rootIndex(), builder.count());
    QCOMPARE(file.lastSaveText(), QStringLiteral("2023/11/14 22:13:20"));
    QCOMPARE(file.lastSave().seconds, 1700000000u);
    QCOMPARE(file.creatorVersion(), QStringLiteral("Cutthulhu 3.4a").leftJustified(30));
    avb::Builder copy = builder;
    for (std::uint32_t i = 1; i <= builder.count(); ++i) {
        const avb::Object& built = copy.object(i);
        const avb::Object& read = file.object(i);
        QVERIFY2(read.ok(), qPrintable(read.error));
        QCOMPARE(read.classId, built.classId);
        for (const avb::Property& p : built.properties) {
            const avb::Value* got = avb::find(read.properties, p.name);
            QVERIFY2(got, qPrintable(built.classId + QLatin1Char('.') + p.name + QLatin1String(" was not read")));
            QVERIFY2(sameValue(*got, p.value), qPrintable(built.classId + QLatin1Char('.') + p.name + QLatin1String(" differs")));
        }
    }
    // Optional extensions are written only when set: the source clips have
    // their MobIDs, the components no parameter list.
    const avb::Object& bin = file.root();
    QCOMPARE(bin.classId, QStringLiteral("ABIN"));
    const avb::Object& master = file.object(bin.property(QStringLiteral("items")).toList()[0].member(QStringLiteral("mob")).toRef()->index);
    QCOMPARE(master.property(QStringLiteral("name")).toString(), QStringLiteral("Scratch clip"));
    QCOMPARE(*master.property(QStringLiteral("mob_id")).toMobId(), kMasterMob);
    QVERIFY(!avb::find(master.properties, QStringLiteral("param_list")));
    QCOMPARE(file.object(5).property(QStringLiteral("mob_id")).toMobId(), std::optional<MobId>(MobId()));

    // The import (3.3b) reads it as a master clip.
    const avb::ImportResult imported = avb::importBin(file);
    const model::BinItem* clip = named(imported.items, QStringLiteral("Scratch clip"));
    QVERIFY(clip);
    QCOMPARE(clip->kind, model::BinItemKind::MasterClip);
    QCOMPARE(clip->mobId, kMasterMob);
    QCOMPARE(clip->durationFrames, std::int64_t{100});
    QVERIFY(clip->userPlaced);

    // What is read writes back to the same bytes.
    QVERIFY(avb::rewrite(file, &error) == bytes);
}

// Edit rates and sample rates as a mantissa and a power of ten: the rates
// Media Composer uses come back as the same doubles.
void TestAvb::editRates() {
    using avb::Value;
    for (const double rate : {25.0, 24.0, 23.976, 29.97, 59.94, 50.0, 48000.0, 0.5, -12.25, 0.0}) {
        avb::Builder b;
        const std::uint32_t fill = b.add(QStringLiteral("FILL"), with(component(1, rate), {prop("length", Value::integer(1))}));
        b.add(QStringLiteral("ABIN"), {prop("items", Value::list({}))});
        QString error;
        const QByteArray bytes = b.toBytes(&error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        avb::File file;
        QVERIFY2(file.load(bytes, &error), qPrintable(error));
        QCOMPARE(file.object(fill).property(QStringLiteral("edit_rate")).toDouble(), rate);
        QVERIFY(file.root().ok());
    }
    // 59.94 is stored as Media Composer stores it: 59940e-3.
    avb::Builder b;
    b.add(QStringLiteral("FILL"), with(component(1, 59.94), {prop("length", Value::integer(1))}));
    const QByteArray bytes = b.toBytes();
    QVERIFY(bytes.contains(QByteArray("\x24\xea\x00\x00\xfd\xff", 6)));
}

// Values the writer cannot write are errors, not crashes or bad files.
void TestAvb::writerRefuses() {
    using avb::Value;
    QString error;
    QVERIFY(avb::Builder().toBytes(&error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("no objects")));
    QVERIFY(avb::rewrite(avb::File(), &error).isEmpty());

    auto refused = [&](const QString& cls, avb::Properties p, const char* why) {
        avb::Builder b;
        b.add(cls, std::move(p));
        error.clear();
        const QByteArray bytes = b.toBytes(&error);
        if (!bytes.isEmpty()) return QStringLiteral("written");
        return error.contains(QLatin1String(why)) ? QString() : error;
    };
    QCOMPARE(refused(QStringLiteral("XXXX"), {}, "class not written"), QString());
    QCOMPARE(refused(QStringLiteral("TOO LONG"), {}, "not 4 characters"), QString());
    QCOMPARE(refused(QStringLiteral("TRKR"), with(component(1, 25), {prop("relative_scope", Value::integer(70000))}),
                     "relative_scope: 70000 is out of range"),
             QString());
    QCOMPARE(refused(QStringLiteral("TRKR"), with(component(1, 25), {prop("length", Value::integer(-1))}), "length"), QString());
    QCOMPARE(refused(QStringLiteral("FILL"), with(component(1, 25), {prop("length", Value::string(QStringLiteral("x")))}),
                     "length: not an integer"),
             QString());
    QCOMPARE(refused(QStringLiteral("FILL"), with(component(1, 25), {prop("attributes", Value::ref(9))}), "reference to object 9"),
             QString());
    QCOMPARE(refused(QStringLiteral("FILL"), with(component(1, 25, QStringLiteral("中")), {}), "Mac OS Roman"), QString());
    QCOMPARE(refused(QStringLiteral("FILL"), component(1, std::numeric_limits<double>::quiet_NaN()), "not a finite number"), QString());
    QCOMPARE(refused(QStringLiteral("FILL"), component(1, 1e30), "32-bit mantissa"), QString());
    QCOMPARE(refused(QStringLiteral("ECCP"), with(component(3, 25), {prop("header", Value::bytes("short"))}), "not 8"), QString());
    QCOMPARE(refused(QStringLiteral("PRCL"), with(component(1, 25), {prop("value_type", Value::integer(3))}), "unknown type"), QString());
    QCOMPARE(refused(QStringLiteral("SEQU"), with(component(1, 25), {prop("components", Value::integer(1))}), "not a list"), QString());
    QCOMPARE(refused(QStringLiteral("TRKG"), with(component(1, 25), {prop("tracks", Value::list({Value::integer(1)}))}),
                     "not a record"),
             QString());
    QCOMPARE(refused(QStringLiteral("CMPO"), {prop("mob_id", Value::string(QStringLiteral("x")))}, "not a MobID"), QString());
    QCOMPARE(refused(QStringLiteral("ATTR"), {prop("x", Value::real(1.5))}, "attribute x"), QString());
    QCOMPARE(refused(QStringLiteral("ABIN"), {prop("uid", Value::integer(-1))}, "negative"), QString());
    QCOMPARE(refused(QStringLiteral("FILE"), {prop("path", Value::string(QString(70000, QLatin1Char('a'))))}, "too long"), QString());

    // An object that was not read cannot be written from its properties.
    avb::Object unread;
    unread.classId = QStringLiteral("CMPO");
    unread.error = QStringLiteral("damaged");
    QVERIFY(avb::encodeObject(unread, true, 1, &error).isEmpty());
    // The root must be in the table.
    avb::Builder b;
    b.add(QStringLiteral("ABIN"), {});
    b.setRoot(2);
    QVERIFY(b.toBytes(&error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("root index 2")));
    // Header fields of the wrong size.
    b.setRoot(1);
    b.header().reserved = QByteArray(3, 'x');
    QVERIFY(b.toBytes(&error).isEmpty());
    b.header().reserved.clear();
    b.header().creatorVersion = QStringLiteral("中");
    QVERIFY(b.toBytes(&error).isEmpty());
    b.header().creatorVersion = QStringLiteral("Cutthulhu");
    QVERIFY(!b.toBytes(&error).isEmpty());
}

// Damaged bins that open are written again without a crash: objects that
// were read are written from their properties, the rest copied as stored,
// and what is written opens again; in the other byte order the unread
// objects are an error.
void TestAvb::rewritesDamaged() {
    const QString folder = corpus();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QByteArray good = readAll(QDir(folder).filePath(QStringLiteral("pyavb/retimes/linear01.avb")));
    QVERIFY(!good.isEmpty());
    QRandomGenerator random(41);
    int written = 0;
    for (int i = 0; i < 200; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 16; ++k) {
            bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        }
        if (i % 10 == 9) bytes.truncate(random.bounded(static_cast<int>(bytes.size())));
        avb::File file;
        if (!file.load(bytes)) continue;
        QString error;
        const QByteArray out = avb::rewrite(file, &error);
        if (out.isEmpty()) {
            QVERIFY(!error.isEmpty()); // an object cut short
        } else {
            ++written;
            avb::File again;
            QVERIFY2(again.load(out, &error), qPrintable(error));
            QCOMPARE(again.objectCount(), file.objectCount());
        }
        avb::rewrite(file, &error, false);
    }
    QVERIFY(written > 0);
}

// Bins for pyavb to read (scripts/interop-corpus.py check-avb-write, ctest
// avb_write_pyavb): the bin from scratch in both byte orders, and each
// corpus bin written big-endian, into $CUTTHULHU_AVB_OUT, with written.json
// saying what each should dump as.
void TestAvb::forPyavb() {
    const QString out = qEnvironmentVariable("CUTTHULHU_AVB_OUT");
    if (out.isEmpty()) QSKIP("CUTTHULHU_AVB_OUT is not set");
    QVERIFY(QDir().mkpath(out));
    QJsonObject scratch;
    for (const bool little : {true, false}) {
        const QString name = little ? QStringLiteral("scratch.avb") : QStringLiteral("scratch-be.avb");
        QString error;
        QVERIFY2(scratchBin(little).save(QDir(out).filePath(name), &error), qPrintable(error));
        avb::File file;
        QVERIFY2(file.open(QDir(out).filePath(name), &error), qPrintable(error));
        scratch.insert(name, QString::fromUtf8(aaf::canonicalJson(avb::dump(file))));
    }
    QJsonObject bigEndian;
    const QString folder = corpus();
    if (!folder.isEmpty()) {
        const QJsonObject files = QJsonDocument::fromJson(readAll(QStringLiteral(LUMENIS_TEST_DATA "/interop_corpus.json")))
                                      .object()
                                      .value(QStringLiteral("avb"))
                                      .toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            const QString path = QDir(folder).filePath(it.key());
            if (!QFile::exists(path)) continue;
            avb::File file;
            QString error;
            QVERIFY2(file.open(path, &error), qPrintable(error));
            const QByteArray big = avb::rewrite(file, &error, false);
            QVERIFY2(!big.isEmpty(), qPrintable(error));
            const QString name = QStringLiteral("be/") + it.key();
            const QString target = QDir(out).filePath(name);
            QVERIFY(QDir().mkpath(QFileInfo(target).path()));
            QFile f(target);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(big), big.size());
            bigEndian.insert(name, path);
        }
    }
    QFile json(QDir(out).filePath(QStringLiteral("written.json")));
    QVERIFY(json.open(QIODevice::WriteOnly));
    json.write(QJsonDocument(QJsonObject{{QStringLiteral("scratch"), scratch}, {QStringLiteral("big_endian"), bigEndian}})
                   .toJson());
}

QTEST_GUILESS_MAIN(TestAvb)
#include "tst_avb.moc"
