// The Reformat attribute, FrameFlex boxes and spatial adapters (Editing Guide p.475-478, p.527-535; 8.5a,
// docs/spec/frameflex.md): the data through JSON and bins, the adapter a clip gives its segments, Refresh Sequence, and
// the pictures the four Reformat options and a box make.
#include "lumenis/media/frame_flex.h"
#include "lumenis/model/sequence.h"

#include <QImage>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

// 200 x 100: the left half red, the right half blue.
QImage halves() {
    QImage image(200, 100, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int y = 0; y < 100; ++y) {
        for (int x = 100; x < 200; ++x) image.setPixel(x, y, qRgb(0, 0, 255));
    }
    return image;
}

bool near(QRgb a, QRgb b, int tolerance = 40) {
    return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance &&
           std::abs(qBlue(a) - qBlue(b)) <= tolerance;
}

const QRgb kRed = qRgb(255, 0, 0);
const QRgb kBlue = qRgb(0, 0, 255);
const QRgb kBlack = qRgb(0, 0, 0);

} // namespace

class TestFrameFlex : public QObject {
    Q_OBJECT

private slots:
    void reformatTexts() {
        for (Reformat r : {Reformat::Stretch, Reformat::Letterbox, Reformat::CenterCrop, Reformat::CenterKeepSize}) {
            QCOMPARE(reformatFromText(reformatText(r)), std::optional<Reformat>(r));
        }
        QCOMPARE(reformatText(Reformat::Letterbox), QStringLiteral("Pillarbox/Letterbox"));
        QCOMPARE(reformatFromText(QStringLiteral("keepSize")), std::optional<Reformat>(Reformat::CenterKeepSize));
        QVERIFY(!reformatFromText(QStringLiteral("Raw Pixel")));
    }

    // A clip's attribute and box make its segments' adapter; a full-image box is none; JSON and the bin keep them.
    void adapterOfClips() {
        BinItem clip;
        clip.id = QStringLiteral("c1");
        QVERIFY(!spatialAdapterOf(clip));
        clip.fields.insert(QLatin1String(kReformatHeading), QStringLiteral("Center Crop"));
        auto a = spatialAdapterOf(clip);
        QVERIFY(a);
        QCOMPARE(a->reformat, Reformat::CenterCrop);
        QVERIFY(!a->frameFlex);
        FrameFlex flex;
        setFrameFlex(clip, flex);
        QVERIFY(!spatialAdapterOf(clip)->frameFlex); // the whole image is no box
        flex.x = 40;
        flex.size = 0.5;
        flex.rotation = 12;
        flex.imageSize = QSize(4096, 2160);
        setFrameFlex(clip, flex);
        a = spatialAdapterOf(clip);
        QVERIFY(a && a->frameFlex);
        QVERIFY(*a->frameFlex == flex);
        QVERIFY(SpatialAdapter::fromJson(a->toJson()) == *a);
        // Through a bin file's JSON.
        const auto back = binItemFromJson(binItemToJson(clip));
        QVERIFY(back && frameFlexOf(*back) && *frameFlexOf(*back) == flex);
        setFrameFlex(clip, std::nullopt);
        QVERIFY(!frameFlexOf(clip));
        // Sequences have none.
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        seq.fields.insert(QLatin1String(kReformatHeading), QStringLiteral("Stretch"));
        QVERIFY(!spatialAdapterOf(seq));
    }

    // "Size matches project raster" (p.530): a UHD clip in an HD project frames the HD-sized middle.
    void sizeMatchesRaster() {
        const FrameFlex f = sizeMatchingRaster(FrameFlex{}, QSize(3840, 2160), QSize(1920, 1080));
        QVERIFY(std::abs(f.size - 0.5) < 1e-9);
        QVERIFY(std::abs(f.frameAspect - 16.0 / 9.0) < 1e-9);
        const QPolygonF box = media::frameFlexBox(QSize(3840, 2160), f);
        QCOMPARE(box.boundingRect().toRect(), QRect(960, 540, 1920, 1080));
    }

    // The four Reformat options on a 2:1 picture in a square frame (p.476-477, p.534).
    void reformatPictures() {
        const QImage source = halves();
        const QSize raster(100, 100);
        SpatialAdapter a;
        a.reformat = Reformat::Stretch;
        QImage out = media::spatialAdaptedImage(source, a, raster, raster);
        QVERIFY(near(out.pixel(2, 2), kRed) && near(out.pixel(97, 97), kBlue));
        a.reformat = Reformat::Letterbox;
        out = media::spatialAdaptedImage(source, a, raster, raster);
        QVERIFY(near(out.pixel(50, 5), kBlack));  // the bar above
        QVERIFY(near(out.pixel(2, 50), kRed));
        QVERIFY(near(out.pixel(97, 50), kBlue));
        a.reformat = Reformat::CenterCrop;
        out = media::spatialAdaptedImage(source, a, raster, raster);
        QVERIFY(near(out.pixel(2, 2), kRed) && near(out.pixel(97, 2), kBlue)); // no bars, the middle 100 x 100
        a.reformat = Reformat::CenterKeepSize;
        out = media::spatialAdaptedImage(source, a, QSize(400, 400), raster); // a quarter of a 400-pixel project
        QVERIFY(near(out.pixel(50, 50 - 15), kBlack) && near(out.pixel(30, 50), kRed) && near(out.pixel(70, 50), kBlue));
        QVERIFY(near(out.pixel(10, 50), kBlack));
    }

    // A box over the right half shows only blue; turned a quarter, the red half comes in above or below (p.529-532).
    void framingBox() {
        const QImage source = halves();
        SpatialAdapter a;
        a.reformat = Reformat::Stretch;
        FrameFlex f;
        f.x = 50;
        f.size = 0.5;
        f.frameAspect = 1.0;
        a.frameFlex = f;
        QImage out = media::spatialAdaptedImage(source, a, QSize(100, 100), QSize(100, 100));
        QVERIFY(near(out.pixel(5, 5), kBlue) && near(out.pixel(95, 95), kBlue));
        // The box on the middle, turned 90 degrees: the halves go above and below.
        f.x = 0;
        f.rotation = 90;
        a.frameFlex = f;
        out = media::spatialAdaptedImage(source, a, QSize(100, 100), QSize(100, 100));
        const QRgb top = out.pixel(50, 10);
        const QRgb bottom = out.pixel(50, 90);
        QVERIFY((near(top, kRed) && near(bottom, kBlue)) || (near(top, kBlue) && near(bottom, kRed)));
        // The Image Size override scales the box with it: the same box at twice the Image Size is half as large.
        f = FrameFlex{};
        f.imageSize = QSize(400, 200);
        f.x = 100;
        f.size = 0.5;
        f.frameAspect = 1.0;
        a.frameFlex = f;
        out = media::spatialAdaptedImage(source, a, QSize(100, 100), QSize(100, 100));
        QVERIFY(near(out.pixel(5, 50), kBlue) && near(out.pixel(95, 50), kBlue));
    }

    // Refresh Sequence (p.478): segments take their clip's attribute; framing changed in the sequence stays unless All
    // FrameFlex including creative framing.
    void refresh() {
        BinItem clip;
        clip.id = QStringLiteral("c1");
        clip.durationFrames = 50;
        clip.fields.insert(QLatin1String(kReformatHeading), QStringLiteral("Stretch"));
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = clip.id;
        s.binId = QStringLiteral("b");
        s.sourceTrack = QStringLiteral("V1");
        s.length = 10;
        seq.track(QStringLiteral("V1"))->segments = {s, s};
        const auto resolve = [&](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        QCOMPARE(refreshSpatialAdapters(seq, resolve, false), 2);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments[0].spatial->reformat, Reformat::Stretch);
        QCOMPARE(refreshSpatialAdapters(seq, resolve, false), 0);
        // The sequence round-trips the adapter.
        BinItem item;
        item.kind = BinItemKind::Sequence;
        seq.storeIn(item);
        QVERIFY(Sequence::fromItem(item).track(QStringLiteral("V1"))->segments[0].spatial == seq.track(QStringLiteral("V1"))->segments[0].spatial);
        seq.track(QStringLiteral("V1"))->segments[1].spatial->creative = true;
        clip.fields.insert(QLatin1String(kReformatHeading), QStringLiteral("Center Crop"));
        QCOMPARE(refreshSpatialAdapters(seq, resolve, false), 1);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments[1].spatial->reformat, Reformat::Stretch);
        QCOMPARE(refreshSpatialAdapters(seq, resolve, true), 1);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments[1].spatial->reformat, Reformat::CenterCrop);
    }
};

QTEST_GUILESS_MAIN(TestFrameFlex)
#include "tst_frame_flex.moc"
