#include "lumenis/interop/cfb.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace lumenis::interop;

namespace {

QByteArray pattern(qsizetype size, quint32 seed) {
    QByteArray b(size, Qt::Uninitialized);
    QRandomGenerator random(seed);
    for (qsizetype i = 0; i < size; ++i) b[i] = static_cast<char>(random.bounded(256));
    return b;
}

// A stream's path and contents, or a storage's path and an empty value.
using Listing = QMap<QString, QByteArray>;

Listing listing(cfb::Reader& reader) {
    Listing out;
    for (const QString& path : reader.paths()) {
        const int id = reader.find(path);
        out.insert(path, reader.entry(id).isStream() ? reader.readAll(id) : QByteArray());
    }
    return out;
}

QString write(const cfb::Writer& writer, const QTemporaryDir& dir, const QString& name) {
    const QString path = dir.filePath(name);
    QString error;
    if (!writer.save(path, &error)) qWarning() << error;
    return path;
}

} // namespace

class TestCfb : public QObject {
    Q_OBJECT

private slots:
    void names();
    void roundTrip_data();
    void roundTrip();
    void manyEntries();
    void difat();
    void readsPartly();
    void damaged();
    void hugeStreamSize();
    void corpus();
    void forOlefile();
};

void TestCfb::names() {
    QVERIFY(cfb::compareNames(QStringLiteral("b"), QStringLiteral("aa")) < 0); // shorter first
    QCOMPARE(cfb::compareNames(QStringLiteral("Header"), QStringLiteral("HEADER")), 0);
    QVERIFY(cfb::compareNames(QStringLiteral("abc"), QStringLiteral("ABD")) < 0);
    QVERIFY(cfb::isValidName(QStringLiteral("Header-2")));
    QVERIFY(!cfb::isValidName(QString()));
    QVERIFY(!cfb::isValidName(QStringLiteral("a/b")));
    QVERIFY(!cfb::isValidName(QString(32, QLatin1Char('x'))));
    QVERIFY(cfb::isValidName(QString(31, QLatin1Char('x'))));

    cfb::Writer writer;
    QString error;
    QVERIFY(writer.addStream(cfb::Writer::root(), QStringLiteral("Same"), {}) > 0);
    QCOMPARE(writer.addStream(cfb::Writer::root(), QStringLiteral("SAME"), {}, &error), -1);
    QVERIFY(error.contains(QStringLiteral("taken")));
    const int stream = writer.addStream(cfb::Writer::root(), QStringLiteral("s"), {});
    QCOMPARE(writer.addStorage(stream, QStringLiteral("x"), &error), -1);
}

void TestCfb::roundTrip_data() {
    QTest::addColumn<int>("sectorSize");
    QTest::newRow("512") << 512;
    QTest::newRow("4096") << 4096;
}

// Streams on both sides of the mini stream cutoff and of sector edges, in
// nested storages, with class IDs and times, read back the same.
void TestCfb::roundTrip() {
    QFETCH(int, sectorSize);
    cfb::Writer writer(sectorSize);
    const cfb::Clsid header{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    writer.setHeaderClsid(header);
    const cfb::Clsid rootClass{0xa5, 0x98, 0xb3, 0xb3};
    writer.setClsid(cfb::Writer::root(), rootClass);
    const int a = writer.addStorage(cfb::Writer::root(), QStringLiteral("Content"));
    const int b = writer.addStorage(a, QStringLiteral("Mobs{2}"));
    writer.setTimes(b, 0x01d0000000000001ull, 0x01d0000000000002ull);
    writer.setStateBits(b, 7);
    Listing expected;
    const QList<qsizetype> sizes{0, 1, 63, 64, 65, 4095, 4096, 4097, 8192, 70000};
    int n = 0;
    for (const qsizetype size : sizes) {
        const QByteArray data = pattern(size, static_cast<quint32>(size));
        const int parent = n % 3 == 0 ? cfb::Writer::root() : (n % 3 == 1 ? a : b);
        const QString name = QStringLiteral("s%1 é").arg(size);
        QVERIFY(writer.addStream(parent, name, data) > 0);
        const QString prefix = parent == cfb::Writer::root() ? QString() : (parent == a ? QStringLiteral("/Content") : QStringLiteral("/Content/Mobs{2}"));
        expected.insert(prefix + QLatin1Char('/') + name, data);
        ++n;
    }
    expected.insert(QStringLiteral("/Content"), {});
    expected.insert(QStringLiteral("/Content/Mobs{2}"), {});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = write(writer, dir, QStringLiteral("t.cfb"));
    QVERIFY(QFileInfo(path).size() % sectorSize == 0);

    cfb::Reader reader;
    QString error;
    QVERIFY2(reader.open(path, &error), qPrintable(error));
    QCOMPARE(reader.majorVersion(), sectorSize == 512 ? 3 : 4);
    QCOMPARE(reader.sectorSize(), sectorSize);
    QCOMPARE(reader.headerClsid(), header);
    QCOMPARE(reader.entry(cfb::Reader::root()).clsid, rootClass);
    QCOMPARE(listing(reader), expected);
    const int mobs = reader.find(QStringLiteral("/content/MOBS{2}")); // case does not matter
    QVERIFY(mobs > 0);
    QCOMPARE(reader.entry(mobs).created, 0x01d0000000000001ull);
    QCOMPARE(reader.entry(mobs).modified, 0x01d0000000000002ull);
    QCOMPARE(reader.entry(mobs).stateBits, 7u);
    QCOMPARE(reader.find(QStringLiteral("/Content/none")), -1);
    // Children come in directory order.
    QStringList names;
    for (const int id : reader.children(cfb::Reader::root())) names.append(reader.entry(id).name);
    QStringList sorted = names;
    std::sort(sorted.begin(), sorted.end(), [](const QString& x, const QString& y) { return cfb::compareNames(x, y) < 0; });
    QCOMPARE(names, sorted);
}

// A storage with hundreds of children spans many directory sectors, and
// every child is found through the tree.
void TestCfb::manyEntries() {
    cfb::Writer writer(512);
    const int storage = writer.addStorage(cfb::Writer::root(), QStringLiteral("Big"));
    for (int i = 0; i < 700; ++i) {
        QVERIFY(writer.addStream(storage, QStringLiteral("item %1").arg(i), QByteArray::number(i)) > 0);
    }
    QTemporaryDir dir;
    cfb::Reader reader;
    QString error;
    QVERIFY2(reader.open(write(writer, dir, QStringLiteral("many.cfb")), &error), qPrintable(error));
    const int big = reader.find(QStringLiteral("/Big"));
    QCOMPARE(reader.children(big).size(), 700);
    for (int i = 0; i < 700; ++i) {
        const int id = reader.child(big, QStringLiteral("ITEM %1").arg(i));
        QVERIFY(id > 0);
        QCOMPARE(reader.readAll(id), QByteArray::number(i));
    }
    // Names in a storage this large are looked up through an index (Reader::child): also a name
    // that is not there.
    QCOMPARE(reader.child(big, QStringLiteral("item 700")), -1);
    QCOMPARE(reader.child(big, QStringLiteral("item")), -1);
}

// More than 109 FAT sectors: the rest are listed in DIFAT sectors.
void TestCfb::difat() {
    cfb::Writer writer(512);
    const QByteArray data = pattern(9 * 1024 * 1024, 9); // 18432 sectors: 144 FAT sectors
    writer.addStream(cfb::Writer::root(), QStringLiteral("Essence"), data);
    writer.addStream(cfb::Writer::root(), QStringLiteral("Small"), QByteArray("small"));
    QTemporaryDir dir;
    const QString path = write(writer, dir, QStringLiteral("difat.cfb"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray header = file.read(512);
    QVERIFY(qFromLittleEndian<quint32>(header.constData() + 44) > 109);
    QVERIFY(qFromLittleEndian<quint32>(header.constData() + 72) >= 1);
    cfb::Reader reader;
    QString error;
    QVERIFY2(reader.open(path, &error), qPrintable(error));
    QCOMPARE(reader.readAll(reader.find(QStringLiteral("/Essence"))), data);
    QCOMPARE(reader.readAll(reader.find(QStringLiteral("/Small"))), QByteArray("small"));
}

// Parts of streams, from anywhere, regular and mini.
void TestCfb::readsPartly() {
    cfb::Writer writer;
    const QByteArray big = pattern(50000, 1);
    const QByteArray small = pattern(3000, 2);
    writer.addStream(cfb::Writer::root(), QStringLiteral("big"), big);
    writer.addStream(cfb::Writer::root(), QStringLiteral("small"), small);
    QTemporaryDir dir;
    cfb::Reader reader;
    QVERIFY(reader.open(write(writer, dir, QStringLiteral("p.cfb"))));
    const int b = reader.find(QStringLiteral("big"));
    const int s = reader.find(QStringLiteral("small"));
    for (const auto& [offset, length] : QList<QPair<quint64, quint64>>{{0, 1}, {4095, 2}, {4096, 4096}, {10, 45000}, {49990, 100}}) {
        QCOMPARE(reader.read(b, offset, length), big.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(length)));
    }
    for (const auto& [offset, length] : QList<QPair<quint64, quint64>>{{0, 64}, {63, 2}, {100, 1000}, {2999, 10}}) {
        QCOMPARE(reader.read(s, offset, length), small.mid(static_cast<qsizetype>(offset), static_cast<qsizetype>(length)));
    }
    QVERIFY(reader.read(s, 5000, 10).isEmpty());
}

// Damaged files are refused, not read past their end or round in loops.
void TestCfb::damaged() {
    cfb::Writer writer(512);
    writer.addStream(cfb::Writer::root(), QStringLiteral("a"), pattern(10000, 3));
    writer.addStream(cfb::Writer::root(), QStringLiteral("b"), pattern(100, 4));
    const QByteArray good = writer.toBytes();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.cfb"));
    auto tryRead = [&](const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(bytes);
        f.close();
        cfb::Reader reader;
        QString error;
        if (!reader.open(path, &error)) return false;
        for (const QString& p : reader.paths()) {
            const int id = reader.find(p);
            if (reader.entry(id).isStream() && reader.readAll(id, &error).size() != static_cast<qsizetype>(reader.entry(id).size)) {
                return false;
            }
        }
        return true;
    };
    QVERIFY(tryRead(good));
    QVERIFY(!tryRead(good.left(100)));
    QByteArray noSignature = good;
    noSignature[0] = 0;
    QVERIFY(!tryRead(noSignature));
    QVERIFY(!tryRead(good.left(good.size() / 2)));
    // A FAT chain that loops on itself: stream "a" starts at sector 0.
    QByteArray loop = good;
    const quint32 fatSector = qFromLittleEndian<quint32>(good.constData() + 76);
    qToLittleEndian<quint32>(0, loop.data() + (fatSector + 1) * 512 + 4 * 3);
    QVERIFY(!tryRead(loop));
    // A directory entry that points at itself.
    QByteArray cycle = good;
    const quint32 dirSector = qFromLittleEndian<quint32>(good.constData() + 48);
    qToLittleEndian<quint32>(1, cycle.data() + (dirSector + 1) * 512 + 128 + 68);
    QVERIFY(!tryRead(cycle));
    // Random damage never crashes or hangs.
    QRandomGenerator random(42);
    for (int i = 0; i < 300; ++i) {
        QByteArray bytes = good;
        for (int k = 0; k < 8; ++k) bytes[random.bounded(static_cast<int>(bytes.size()))] = static_cast<char>(random.bounded(256));
        tryRead(bytes);
    }
}

// Found by fuzz_cfb (fuzz/regressions/cfb/huge-stream-size.cfb): a version 4 file's stream with a
// 64-bit size near 2^64. Its sector count, rounded up, wrapped to 0, and read() indexed the empty
// chain. It is refused as a chain shorter than the size, also when read from memory (openData).
// With CUTTHULHU_FUZZ_REGRESSIONS set to fuzz/regressions, the file is written to cfb/ there.
void TestCfb::hugeStreamSize() {
    cfb::Writer writer(4096);
    writer.addStream(cfb::Writer::root(), QStringLiteral("a"), pattern(4097, 5));
    QByteArray bytes = writer.toBytes();
    const quint32 dirSector = qFromLittleEndian<quint32>(bytes.constData() + 48);
    const qsizetype entry = (dirSector + 1) * 4096 + 128; // entry 1, after the root
    QCOMPARE(qFromLittleEndian<quint64>(bytes.constData() + entry + 120), quint64{4097});
    qToLittleEndian<quint64>(0xFFFFFFFFFFFFFF93ull, bytes.data() + entry + 120);
    const QString out = qEnvironmentVariable("CUTTHULHU_FUZZ_REGRESSIONS");
    if (!out.isEmpty()) {
        QFile f(QDir(out).filePath(QStringLiteral("cfb/huge-stream-size.cfb")));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
    }
    cfb::Reader reader;
    QString error;
    QVERIFY2(reader.openData(bytes, QStringLiteral("huge.cfb"), &error), qPrintable(error));
    const int a = reader.find(QStringLiteral("/a"));
    QVERIFY(a > 0);
    QCOMPARE(reader.entry(a).size, quint64{0xFFFFFFFFFFFFFF93ull});
    QVERIFY(reader.readAll(a, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("shorter than its size")), qPrintable(error));
    QVERIFY(reader.read(a, 1u << 20, 100, &error).isEmpty());
}

// The interchange corpus (scripts/interop-corpus.py fetch): every file reads,
// with the same storages and streams, and the same bytes in each, as
// olefile, another reader, found (tests/data/interop_corpus.json). Each file, copied entry by
// entry through the Writer, reads back the same.
void TestCfb::corpus() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    if (folder.isEmpty() || !QDir(folder).exists()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    QFile manifestFile(QStringLiteral(LUMENIS_TEST_DATA "/interop_corpus.json"));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonObject files = manifest.value(QStringLiteral("cfb")).toObject();
    QVERIFY(!files.isEmpty());
    QTemporaryDir dir;
    int read = 0;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = QDir(folder).filePath(it.key());
        if (!QFile::exists(path)) {
            qWarning() << "not in the corpus:" << it.key();
            continue;
        }
        cfb::Reader reader;
        QString error;
        QVERIFY2(reader.open(path, &error), qPrintable(error));
        // The listing as interop-corpus.py's listing_digest makes it.
        QStringList lines;
        int storages = 0;
        int streams = 0;
        for (const QString& p : reader.paths()) {
            const int id = reader.find(p);
            const cfb::Entry& e = reader.entry(id);
            if (e.isStream()) {
                const QByteArray data = reader.readAll(id, &error);
                QVERIFY2(data.size() == static_cast<qsizetype>(e.size), qPrintable(error));
                lines.append(QStringLiteral("%1\tstream\t%2\t%3")
                                 .arg(p)
                                 .arg(e.size)
                                 .arg(QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())));
                ++streams;
            } else {
                lines.append(p + QLatin1String("\tstorage"));
                ++storages;
            }
        }
        lines.sort();
        const QJsonObject want = it.value().toObject();
        QCOMPARE(storages, want.value(QStringLiteral("storages")).toInt());
        QCOMPARE(streams, want.value(QStringLiteral("streams")).toInt());
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(), QCryptographicHash::Sha256).toHex()),
                 want.value(QStringLiteral("listing")).toString());

        // Copied through the Writer, with the same sector size.
        cfb::Writer writer(reader.sectorSize());
        writer.setHeaderClsid(reader.headerClsid());
        writer.setClsid(cfb::Writer::root(), reader.entry(cfb::Reader::root()).clsid);
        std::function<void(int, int)> copy = [&](int from, int to) {
            for (const int id : reader.children(from)) {
                const cfb::Entry& e = reader.entry(id);
                if (e.isStream()) {
                    writer.addStream(to, e.name, reader.readAll(id));
                } else {
                    const int storage = writer.addStorage(to, e.name);
                    writer.setClsid(storage, e.clsid);
                    writer.setTimes(storage, e.created, e.modified);
                    copy(id, storage);
                }
            }
        };
        copy(cfb::Reader::root(), cfb::Writer::root());
        cfb::Reader copied;
        QVERIFY2(copied.open(write(writer, dir, QStringLiteral("copy.aaf")), &error), qPrintable(error));
        QCOMPARE(listing(copied), listing(reader));
        for (const QString& p : reader.paths()) {
            QCOMPARE(copied.entry(copied.find(p)).clsid, reader.entry(reader.find(p)).clsid);
        }
        ++read;
    }
    QVERIFY(read > 0);
}

// Files for scripts/interop-corpus.py to read back with olefile (ctest
// cfb_olefile): written to CUTTHULHU_CFB_OUT with a listing of what they hold.
void TestCfb::forOlefile() {
    const QString out = qEnvironmentVariable("CUTTHULHU_CFB_OUT");
    if (out.isEmpty()) QSKIP("CUTTHULHU_CFB_OUT is not set");
    QVERIFY(QDir().mkpath(out));
    QJsonObject files;
    for (const int sectorSize : {512, 4096}) {
        cfb::Writer writer(sectorSize);
        const int a = writer.addStorage(cfb::Writer::root(), QStringLiteral("Header{2}"));
        writer.setClsid(a, {0x0d, 0x01, 0x02, 0x01});
        QJsonObject entries;
        entries.insert(QStringLiteral("/Header{2}"), QJsonObject{{QStringLiteral("type"), QStringLiteral("storage")}});
        for (const qsizetype size : {0, 5, 64, 4095, 4096, 100000}) {
            const QByteArray data = pattern(size, static_cast<quint32>(size + sectorSize));
            const QString name = QStringLiteral("properties %1").arg(size);
            writer.addStream(size % 2 ? cfb::Writer::root() : a, name, data);
            const QString p = (size % 2 ? QString() : QStringLiteral("/Header{2}")) + QLatin1Char('/') + name;
            entries.insert(p, QJsonObject{{QStringLiteral("type"), QStringLiteral("stream")},
                                          {QStringLiteral("size"), static_cast<qint64>(size)},
                                          {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}});
        }
        for (int i = 0; i < 40; ++i) writer.addStream(a, QStringLiteral("n%1").arg(i), QByteArray::number(i));
        for (int i = 0; i < 40; ++i) {
            const QByteArray data = QByteArray::number(i);
            entries.insert(QStringLiteral("/Header{2}/n%1").arg(i),
                           QJsonObject{{QStringLiteral("type"), QStringLiteral("stream")},
                                       {QStringLiteral("size"), static_cast<qint64>(data.size())},
                                       {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}});
        }
        const QString name = QStringLiteral("written-%1.cfb").arg(sectorSize);
        QString error;
        QVERIFY2(writer.save(QDir(out).filePath(name), &error), qPrintable(error));
        files.insert(name, QJsonObject{{QStringLiteral("entries"), entries}});
    }
    QFile listingFile(QDir(out).filePath(QStringLiteral("written.json")));
    QVERIFY(listingFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    listingFile.write(QJsonDocument(QJsonObject{{QStringLiteral("cfb"), files}}).toJson());
}

QTEST_GUILESS_MAIN(TestCfb)
#include "tst_cfb.moc"
