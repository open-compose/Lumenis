// Copy Media (Editing Guide p.512-513; 8.7a2, docs/spec/proxy-media.md): where media::planCopyMedia puts each file on a
// drive and in a folder, the next numbered folders, files already at the target, offline files, the sizes and the copy.
#include "lumenis/media/copy_media.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;

namespace {

QString write(const QString& path, int bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(QByteArray(bytes, 'x'));
    return QFileInfo(path).absoluteFilePath();
}

} // namespace

class TestCopyMedia : public QObject {
    Q_OBJECT

private slots:
    void nextNumber() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mxf = dir.filePath(QStringLiteral("MXF"));
        // None there: "1".
        QCOMPARE(QFileInfo(media::nextNumberedFolder(mxf)).fileName(), QStringLiteral("1"));
        QDir().mkpath(QDir(mxf).filePath(QStringLiteral("1")));
        QDir().mkpath(QDir(mxf).filePath(QStringLiteral("4")));
        QDir().mkpath(QDir(mxf).filePath(QStringLiteral("other")));
        QCOMPARE(QFileInfo(media::nextNumberedFolder(mxf)).fileName(), QStringLiteral("5"));
    }

    void toDrive() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = write(dir.filePath(QStringLiteral("src/MXF/1/a.mxf")), 1000);
        const QString b = write(dir.filePath(QStringLiteral("src/MXF/2/a.mxf")), 2000); // the same name in another folder
        const QString p = write(dir.filePath(QStringLiteral("src/Proxy/1/a.proxy.mp4")), 300);
        const QString r = write(dir.filePath(QStringLiteral("src/MXF/1/render.mxf")), 400);
        const QString bin = write(dir.filePath(QStringLiteral("P/Bin 1.cthbin")), 50);
        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QDir().mkpath(QDir(drive).filePath(QStringLiteral("Avid MediaFiles/MXF/1")));
        media::CopyMediaRequest request;
        request.managedFiles = {a, b, dir.filePath(QStringLiteral("src/gone.mxf"))};
        request.proxyFiles = {p};
        request.renderFiles = {r};
        request.binFiles = {bin};
        request.target = drive;
        request.binsTarget = drive;
        media::CopyMediaPlan plan = media::planCopyMedia(request);
        // "Managed media is placed into a new folder within MXF and the folder is named using the next available number";
        // proxies in Proxy's (p.513).
        QVERIFY(plan.mediaFolder.endsWith(QLatin1String("Avid MediaFiles/MXF/2")));
        QVERIFY(plan.proxyFolder.endsWith(QLatin1String("Avid MediaFiles/Proxy/1")));
        QVERIFY(plan.binFolder.endsWith(QLatin1String("Avid Copied Bins")));
        QCOMPARE(plan.media.size(), 4);
        QCOMPARE(plan.bins.size(), 1);
        QCOMPARE(plan.mediaBytes, 1000 + 2000 + 300 + 400);
        QCOMPARE(plan.binBytes, 50);
        QCOMPARE(plan.missing.size(), 1);
        // Two files of one name into one folder: the second renamed.
        QCOMPARE(QFileInfo(plan.media.at(1).to).fileName(), QStringLiteral("a 2.mxf"));
        QVERIFY(media::planFits(plan, request));
        // The boxes: without renders and bins.
        request.renders = false;
        request.bins = false;
        QCOMPARE(media::planCopyMedia(request).media.size(), 3);
        QVERIFY(media::planCopyMedia(request).bins.isEmpty());
        request.renders = true;
        request.bins = true;

        QStringList warnings;
        QCOMPARE(media::copyMedia(plan, {}, &warnings), 5);
        QVERIFY(warnings.isEmpty());
        QVERIFY(QFileInfo::exists(QDir(drive).filePath(QStringLiteral("Avid MediaFiles/MXF/2/a.mxf"))));
        QVERIFY(QFileInfo::exists(QDir(drive).filePath(QStringLiteral("Avid MediaFiles/Proxy/1/a.proxy.mp4"))));
        QVERIFY(QFileInfo::exists(QDir(drive).filePath(QStringLiteral("Avid Copied Bins/Bin 1.cthbin"))));
        // Again: everything is already there.
        plan = media::planCopyMedia(request);
        QVERIFY(plan.isEmpty());
        QCOMPARE(plan.existing, 5);
    }

    void toFolder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = write(dir.filePath(QStringLiteral("src/a.mxf")), 10);
        const QString p = write(dir.filePath(QStringLiteral("src/a.proxy.mp4")), 20);
        media::CopyMediaRequest request;
        request.managedFiles = {a};
        request.proxyFiles = {p};
        request.toFolder = true;
        request.target = dir.filePath(QStringLiteral("Out"));
        request.binsTarget = request.target;
        const media::CopyMediaPlan plan = media::planCopyMedia(request);
        // "the media appears in an 'Avid Copied Media' folder at the target location" (p.513).
        QVERIFY(plan.mediaFolder.endsWith(QLatin1String("Out/Avid Copied Media")));
        QCOMPARE(plan.proxyFolder, plan.mediaFolder);
        QCOMPARE(media::copyMedia(plan), 2);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Out/Avid Copied Media/a.proxy.mp4"))));
        // The target does not exist yet: its space is the nearest folder's.
        QVERIFY(media::availableSpace(dir.filePath(QStringLiteral("Out/not/yet"))) > 0);
    }

    void sizes() {
        QCOMPARE(media::megabytes(0), QStringLiteral("0.00 MB"));
        QCOMPARE(media::megabytes(4571791), QStringLiteral("4.36 MB"));
        QCOMPARE(media::gigabytes(qint64(82.73 * 1024 * 1024 * 1024)), QStringLiteral("82.73 GB"));
    }
};

QTEST_GUILESS_MAIN(TestCopyMedia)
#include "tst_copy_media.moc"
