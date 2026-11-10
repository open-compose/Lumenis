// Proxy media (Editing Guide ch.16, p.508-510; 8.7a, docs/spec/proxy-media.md): a clip's proxies through its Source
// Settings, the link the Play button's proxy modes play at each video quality, the bin's proxy columns, and the H.264 and
// Avid DNx LB files media::makeProxyMedia writes.
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/proxy_media.h"
#include "lumenis/model/proxy.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem clipWithProxy() {
    BinItem clip;
    clip.id = newItemId();
    clip.kind = BinItemKind::MasterClip;
    clip.name = QStringLiteral("A");
    clip.editRate = Rational(25);
    clip.durationFrames = 10;
    clip.link = QJsonObject{{QStringLiteral("path"), QStringLiteral("/m/a.mov")}};
    ProxyMedia p;
    p.h264Link = QJsonObject{{QStringLiteral("path"), QStringLiteral("/p/a.proxy.mp4")}};
    p.lbLink = QJsonObject{{QStringLiteral("path"), QStringLiteral("/p/a.proxy_lb.mov")}};
    p.folder = QStringLiteral("/p");
    p.audio = true;
    p.raster = QSize(960, 540);
    setProxy(clip, p);
    return clip;
}

QString pathOf(const QJsonObject& link) {
    return link.value(QStringLiteral("path")).toString();
}

} // namespace

class TestProxy : public QObject {
    Q_OBJECT

private slots:
    void data() {
        BinItem clip = clipWithProxy();
        QVERIFY(hasProxy(clip));
        const auto p = proxyOf(clip);
        QVERIFY(p);
        QCOMPARE(p->folder, QStringLiteral("/p"));
        QVERIFY(p->audio);
        QCOMPARE(p->raster, QSize(960, 540));
        QCOMPARE(ProxyMedia::pathOf(p->h264Link), QStringLiteral("/p/a.proxy.mp4"));
        QVERIFY(removeProxy(clip));
        QVERIFY(!hasProxy(clip));
        QVERIFY(!removeProxy(clip));
        // What can have proxies made (p.508).
        BinItem item;
        for (const auto kind : {BinItemKind::MasterClip, BinItemKind::Subclip, BinItemKind::Group, BinItemKind::Sequence}) {
            item.kind = kind;
            QVERIFY(canHaveProxy(item));
        }
        item.kind = BinItemKind::Effect;
        QVERIFY(!canHaveProxy(item));
        QCOMPARE(proxyPlaybackText(ProxyPlayback::ProxyPreferred), QStringLiteral("Proxy Preferred"));
    }

    void playedLinks() {
        const BinItem clip = clipWithProxy();
        auto all = [](const QString&) { return true; };
        auto onlyLb = [](const QString& path) { return path.endsWith(QLatin1String("_lb.mov")); };
        // High-Resolution Only: the clip's own media.
        QVERIFY(playedProxyLink(clip, ProxyPlayback::HighResolutionOnly, true, true, all).isEmpty());
        // Proxy Preferred: H.264 at Full Quality, DNx LB at Draft (p.510); the other when one is gone (ours).
        QCOMPARE(pathOf(playedProxyLink(clip, ProxyPlayback::ProxyPreferred, true, true, all)), QStringLiteral("/p/a.proxy.mp4"));
        QCOMPARE(pathOf(playedProxyLink(clip, ProxyPlayback::ProxyPreferred, false, true, all)), QStringLiteral("/p/a.proxy_lb.mov"));
        QCOMPARE(pathOf(playedProxyLink(clip, ProxyPlayback::ProxyPreferred, true, true, onlyLb)), QStringLiteral("/p/a.proxy_lb.mov"));
        // "If the proxy is offline or not available but the high-resolution, local media is, then the high-resolution
        // media will be played" (p.509).
        QVERIFY(playedProxyLink(clip, ProxyPlayback::ProxyPreferred, true, true, [](const QString&) { return false; }).isEmpty());
        // High-Resolution Preferred: the proxy only when the clip's own media is offline.
        QVERIFY(playedProxyLink(clip, ProxyPlayback::HighResolutionPreferred, true, true, all).isEmpty());
        QCOMPARE(pathOf(playedProxyLink(clip, ProxyPlayback::HighResolutionPreferred, true, false, all)),
                 QStringLiteral("/p/a.proxy.mp4"));
        // A clip without proxies.
        BinItem plain = clip;
        removeProxy(plain);
        QVERIFY(playedProxyLink(plain, ProxyPlayback::ProxyPreferred, true, true, all).isEmpty());
    }

    void columns() {
        const BinItem clip = clipWithProxy();
        auto all = [](const QString&) { return true; };
        QCOMPARE(proxyHeadings().size(), 4);
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Video"), true, all), QStringLiteral("H.264"));
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Video"), false, all), QStringLiteral("Avid DNx LB"));
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Audio"), true, all), QStringLiteral("AAC"));
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Path"), true, all), QStringLiteral("/p"));
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Offline"), true, all), QString());
        QCOMPARE(proxyCellText(clip, QStringLiteral("Proxy Offline"), true, [](const QString&) { return false; }),
                 QStringLiteral("V A"));
        BinItem plain = clip;
        removeProxy(plain);
        QCOMPARE(proxyCellText(plain, QStringLiteral("Proxy Video"), true, all), QString());
    }

    void makeProxies() {
        QCOMPARE(media::proxyRaster(QSize(1920, 1080)), QSize(960, 540));
        QCOMPARE(media::proxyRaster(QSize(40, 30)), QSize(256, 144));
        if (!media::patentCodecsAllowed()) QSKIP("H.264 and DNx are left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // A blue 25 fps clip, picture only.
        const QString movie = dir.filePath(QStringLiteral("blue.mov"));
        {
            media::WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("ffv1");
            s.pixelFormat = QStringLiteral("bgr0");
            s.width = 256;
            s.height = 144;
            s.frameRate = Rational(25);
            media::MediaWriter w;
            QVERIFY(w.open(movie, s));
            QImage image(256, 144, QImage::Format_RGBA8888);
            image.fill(QColor(0, 0, 255));
            for (int i = 0; i < 6; ++i) QVERIFY(w.writeVideo(image));
            QVERIFY(w.finish());
        }
        const auto info = media::probe(movie);
        QVERIFY(info);
        const auto clip = media::linkClip(*info, media::LinkOptions{});
        QVERIFY(clip);
        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QVERIFY(QDir().mkpath(drive));
        QString why;
        const auto proxy = media::makeProxyMedia(*clip, drive, QSize(256, 144), {}, &why);
        QVERIFY2(proxy, qPrintable(why));
        QCOMPARE(proxy->folder, media::proxyFolder(drive));
        QVERIFY(proxy->folder.endsWith(QLatin1String("Avid MediaFiles/Proxy/1")));
        QCOMPARE(proxy->raster, QSize(256, 144));
        QVERIFY(!proxy->audio);
        const QString h264 = ProxyMedia::pathOf(proxy->h264Link);
        const QString lb = ProxyMedia::pathOf(proxy->lbLink);
        QVERIFY(QFileInfo::exists(h264) && h264.endsWith(QLatin1String(".mp4")));
        QVERIFY(QFileInfo::exists(lb) && lb.endsWith(QLatin1String(".mov")));
        // Each plays the clip's frames: blue.
        for (const QJsonObject& link : {proxy->h264Link, proxy->lbLink}) {
            BinItem played = *clip;
            played.link = link;
            media::FrameCache cache;
            const QImage frame = cache.frame(played, 3, QSize(256, 144));
            QVERIFY(!frame.isNull());
            const QColor c(frame.pixel(128, 72));
            QVERIFY2(c.blue() > 200 && c.red() < 50, qPrintable(c.name()));
        }
        // No picture: refused.
        BinItem none = *clip;
        none.link = {};
        QVERIFY(!media::makeProxyMedia(none, drive, QSize(256, 144), {}, &why));
    }
};

QTEST_GUILESS_MAIN(TestProxy)
#include "tst_proxy.moc"
