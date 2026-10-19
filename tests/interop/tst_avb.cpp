// Tests of the Media Composer bin (.avb) reader (src/interop/avb.*,
// docs/spec/avb.md): the corpus bins dump as pyavb reads them (digests in
// tests/data/interop_corpus.json, made by `interop-corpus.py manifest`), the
// object API on test_file_01.avb, and damaged files.

#include "lumenis/interop/aaf.h"
#include "lumenis/interop/avb.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QtTest>

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

QTEST_GUILESS_MAIN(TestAvb)
#include "tst_avb.moc"
