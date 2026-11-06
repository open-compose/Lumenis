// Digital Cinema Packages (lumenis/media/dcp.h; docs/spec/dcp.md): the containers and frame rates, the CPL, PKL, Asset
// Map and Volume Index, and writeDcp's JPEG 2000 frames, sound and asdcp-wrap calls through a stand-in script that
// keeps its inputs (POSIX shells only), the package's hashes and sizes. With asdcplib's real asdcp-wrap installed
// (LUMENIS_REAL_ASDCP_WRAP), the package is made with it too.
#include "lumenis/media/dcp.h"
#include "lumenis/media/media.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QXmlStreamReader>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// Each leaf element's text by its name, in document order.
QHash<QString, QStringList> texts(const QByteArray& xml, QString* ns = nullptr) {
    QHash<QString, QStringList> out;
    QXmlStreamReader r(xml);
    QString name;
    QString text;
    bool leaf = false;
    while (!r.atEnd()) {
        const auto token = r.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (ns && ns->isEmpty()) *ns = r.namespaceUri().toString();
            name = r.name().toString();
            text.clear();
            leaf = true;
        } else if (token == QXmlStreamReader::Characters) {
            text += r.text();
        } else if (token == QXmlStreamReader::EndElement) {
            if (leaf) out[name] << text;
            leaf = false;
        }
    }
    return out;
}

MovieSource testSource(int frames) {
    MovieSource s;
    s.frames = frames;
    s.frameRate = Rational(24000, 1001);
    s.raster = QSize(320, 180);
    s.picture = [](std::int64_t f) {
        QImage image(320, 180, QImage::Format_RGB32);
        image.fill(QColor(static_cast<int>(f) * 40, 120, 200));
        return image;
    };
    s.sound = [](std::int64_t, int count, std::vector<std::vector<float>>& out) {
        out.assign(2, std::vector<float>(static_cast<std::size_t>(count), 0.25f));
    };
    return s;
}

} // namespace

class TestDcp : public QObject {
    Q_OBJECT

private slots:
    void containersAndRates() {
        QCOMPARE(dcpContainerSize(QStringLiteral("flat"), false), QSize(1998, 1080));
        QCOMPARE(dcpContainerSize(QStringLiteral("scope"), false), QSize(2048, 858));
        QCOMPARE(dcpContainerSize(QStringLiteral("full"), true), QSize(4096, 2160));
        QCOMPARE(dcpFrameRate(Rational(24000, 1001)), 24);
        QCOMPARE(dcpFrameRate(Rational(25)), 25);
        QCOMPARE(dcpFrameRate(Rational(30000, 1001)), 30);
        QCOMPARE(dcpFrameRate(Rational(50)), 50);
    }

    void documents() {
        DcpComposition c;
        c.cplId = QStringLiteral("11111111-1111-1111-1111-111111111111");
        c.pklId = QStringLiteral("22222222-2222-2222-2222-222222222222");
        c.assetMapId = QStringLiteral("33333333-3333-3333-3333-333333333333");
        c.reelId = QStringLiteral("44444444-4444-4444-4444-444444444444");
        c.options.title = QStringLiteral("Harbor & Sea");
        c.options.kind = QStringLiteral("trailer");
        c.options.issueDate = QDateTime(QDate(2026, 11, 6), QTime(12, 0), QTimeZone::utc());
        c.frameRate = 24;
        c.container = QSize(1998, 1080);
        c.assets << DcpAsset{QStringLiteral("55555555-5555-5555-5555-555555555555"), QStringLiteral("picture.mxf"), QStringLiteral("picture"), 48, 1000, QByteArray(20, 'a')}
                 << DcpAsset{QStringLiteral("66666666-6666-6666-6666-666666666666"), QStringLiteral("sound.mxf"), QStringLiteral("sound"), 48, 500, QByteArray(20, 'b')};
        QString ns;
        auto cpl = texts(dcpCplXml(c), &ns);
        QCOMPARE(ns, QStringLiteral("http://www.smpte-ra.org/schemas/429-7/2006/CPL"));
        QCOMPARE(cpl.value(QStringLiteral("ContentTitleText")), QStringList{QStringLiteral("Harbor & Sea")});
        QCOMPARE(cpl.value(QStringLiteral("ContentKind")), QStringList{QStringLiteral("trailer")});
        QCOMPARE(cpl.value(QStringLiteral("IssueDate")).first(), QStringLiteral("2026-11-06T12:00:00+00:00"));
        QCOMPARE(cpl.value(QStringLiteral("EditRate")), (QStringList{QStringLiteral("24 1"), QStringLiteral("24 1")}));
        QCOMPARE(cpl.value(QStringLiteral("Duration")), (QStringList{QStringLiteral("48"), QStringLiteral("48")}));
        QCOMPARE(cpl.value(QStringLiteral("ScreenAspectRatio")), QStringList{QStringLiteral("1998 1080")});
        QVERIFY(dcpCplXml(c).contains("<MainPicture>"));
        QVERIFY(dcpCplXml(c).contains("<MainSound>"));
        c.assets << DcpAsset{c.cplId, QStringLiteral("cpl.xml"), QStringLiteral("cpl"), 0, 300, QByteArray(20, 'c')};
        ns.clear();
        auto pkl = texts(dcpPklXml(c), &ns);
        QCOMPARE(ns, QStringLiteral("http://www.smpte-ra.org/schemas/429-8/2007/PKL"));
        QCOMPARE(pkl.value(QStringLiteral("Hash")).first(), QString::fromLatin1(QByteArray(20, 'a').toBase64()));
        QCOMPARE(pkl.value(QStringLiteral("Type")), (QStringList{QStringLiteral("application/mxf"), QStringLiteral("application/mxf"), QStringLiteral("text/xml")}));
        const DcpAsset pklAsset{c.pklId, QStringLiteral("pkl.xml"), QStringLiteral("pkl"), 0, 700, {}};
        ns.clear();
        auto am = texts(dcpAssetMapXml(c, pklAsset), &ns);
        QCOMPARE(ns, QStringLiteral("http://www.smpte-ra.org/schemas/429-9/2007/AM"));
        QCOMPARE(am.value(QStringLiteral("PackingList")), QStringList{QStringLiteral("true")});
        QCOMPARE(am.value(QStringLiteral("Path")),
                 (QStringList{QStringLiteral("pkl.xml"), QStringLiteral("picture.mxf"), QStringLiteral("sound.mxf"), QStringLiteral("cpl.xml")}));
        QCOMPARE(am.value(QStringLiteral("Length")).first(), QStringLiteral("700"));
        QVERIFY(dcpVolumeIndexXml().contains("<Index>1</Index>"));
    }

    void writeWithoutTheTool() {
        QTemporaryDir dir;
        DcpOptions o;
        o.title = QStringLiteral("Nothing");
        o.wrapTool = dir.filePath(QStringLiteral("missing-asdcp-wrap"));
        qputenv("LUMENIS_ASDCP_WRAP", dir.filePath(QStringLiteral("missing")).toLocal8Bit());
        DcpOptions none = o;
        none.wrapTool.clear();
        QString error;
        QVERIFY(!writeDcp(dir.filePath(QStringLiteral("DCP")), testSource(2), none, {}, &error));
        QVERIFY2(error.contains(QStringLiteral("asdcp-wrap")), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("DCP"))));
        qunsetenv("LUMENIS_ASDCP_WRAP");
    }

    void writePackage() {
#ifdef Q_OS_WIN
        QSKIP("the stand-in asdcp-wrap is a POSIX shell script");
#else
        QTemporaryDir dir;
        // A stand-in for asdcp-wrap: it keeps its arguments and inputs beside the output, and writes the output.
        const QString stub = dir.filePath(QStringLiteral("asdcp-wrap"));
        {
            QFile f(stub);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n"
                    "for out; do :; done\n"
                    "echo \"$@\" > \"$out.args\"\n"
                    "mkdir -p \"$out.in\"\n"
                    "for a; do [ \"$a\" = \"$out\" ] && break; [ -e \"$a\" ] && cp -R \"$a\" \"$out.in/\"; done\n"
                    "echo MXF > \"$out\"\n");
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        DcpOptions o;
        o.title = QStringLiteral("Harbor");
        o.wrapTool = stub;
        o.container = QStringLiteral("scope");
        const QString folder = dir.filePath(QStringLiteral("Harbor_DCP"));
        QString error;
        double last = 0.0;
        QVERIFY2(writeDcp(folder, testSource(3), o, [&](double p) { last = p; return true; }, &error), qPrintable(error));
        QCOMPARE(last, 1.0);
        QDir d(folder);
        QVERIFY(d.exists(QStringLiteral("ASSETMAP.xml")));
        QVERIFY(d.exists(QStringLiteral("VOLINDEX.xml")));
        QCOMPARE(d.entryList({QStringLiteral("cpl_*.xml")}).size(), 1);
        QCOMPARE(d.entryList({QStringLiteral("pkl_*.xml")}).size(), 1);
        // asdcp-wrap's arguments: SMPTE labels, the asset ID that the CPL names, 24 frames a second, 5.1 sound.
        QFile args(d.filePath(QStringLiteral("picture.mxf.args")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QStringList pictureArgs = QString::fromUtf8(args.readAll()).trimmed().split(QLatin1Char(' '));
        QCOMPARE(pictureArgs.mid(0, 5).at(0), QStringLiteral("-L"));
        QCOMPARE(pictureArgs.at(3), QStringLiteral("-p"));
        QCOMPARE(pictureArgs.at(4), QStringLiteral("24"));
        QFile cplFile(d.filePath(d.entryList({QStringLiteral("cpl_*.xml")}).first()));
        QVERIFY(cplFile.open(QIODevice::ReadOnly));
        const auto cpl = texts(cplFile.readAll());
        QVERIFY(cpl.value(QStringLiteral("Id")).contains(QStringLiteral("urn:uuid:") + pictureArgs.at(2)));
        QCOMPARE(cpl.value(QStringLiteral("ScreenAspectRatio")), QStringList{QStringLiteral("2048 858")});
        QFile soundArgs(d.filePath(QStringLiteral("sound.mxf.args")));
        QVERIFY(soundArgs.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(soundArgs.readAll()).contains(QStringLiteral("-l 5.1")));
        // The frames: three JPEG 2000 codestreams of the container's size in X'Y'Z'.
        const QDir j2c(d.filePath(QStringLiteral("picture.mxf.in/j2c")));
        const QStringList codestreams = j2c.entryList({QStringLiteral("*.j2c")}, QDir::Files, QDir::Name);
        QCOMPARE(codestreams.size(), 3);
        const auto info = probe(j2c.filePath(codestreams.first()));
        QVERIFY(info && info->firstVideo());
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("jpeg2000"));
        QCOMPARE(info->firstVideo()->width, 2048);
        QCOMPARE(info->firstVideo()->height, 858);
        // The sound: six channels of 24-bit 48 kHz WAVE.
        const auto wav = probe(d.filePath(QStringLiteral("sound.mxf.in/sound.wav")));
        QVERIFY(wav && !wav->audio().empty());
        QCOMPARE(wav->audio().front()->channels, 6);
        QCOMPARE(wav->audio().front()->sampleRate, 48000);
        // The Packing List's hashes and sizes are the files'.
        QFile pklFile(d.filePath(d.entryList({QStringLiteral("pkl_*.xml")}).first()));
        QVERIFY(pklFile.open(QIODevice::ReadOnly));
        const auto pkl = texts(pklFile.readAll());
        const QStringList names = pkl.value(QStringLiteral("OriginalFileName"));
        QCOMPARE(names.size(), 3);
        for (qsizetype i = 0; i < names.size(); ++i) {
            QFile f(d.filePath(names.at(i)));
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray bytes = f.readAll();
            QCOMPARE(pkl.value(QStringLiteral("Size")).at(i), QString::number(bytes.size()));
            QCOMPARE(pkl.value(QStringLiteral("Hash")).at(i),
                     QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toBase64()));
        }
        // An existing folder is left alone.
        QVERIFY(!writeDcp(folder, testSource(1), o, {}, &error));
        QVERIFY(QFileInfo::exists(d.filePath(QStringLiteral("ASSETMAP.xml"))));
#endif
    }
};

QTEST_MAIN(TestDcp)
#include "tst_dcp.moc"
