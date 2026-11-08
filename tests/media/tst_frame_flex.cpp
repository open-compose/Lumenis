// The Reformat attribute, FrameFlex boxes and spatial adapters (Editing Guide p.475-478, p.527-535; 8.5a,
// docs/spec/frameflex.md): the data through JSON and bins, the adapter a clip gives its segments, Refresh Sequence, and
// the pictures the four Reformat options and a box make; the FrameFlex effect that pans a shot (8.5b3, p.533-534).
#include "lumenis/media/frame_flex.h"
#include "lumenis/model/effect_keyframes.h"
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

    // The FrameFlex effect (p.533-534): a segment's box as an effect, keyframed, bypassed and reset; found by effectAt.
    void framingEffect() {
        BinItem clip;
        clip.id = QStringLiteral("c1");
        FrameFlex flex;
        flex.x = 50;
        flex.size = 0.5;
        flex.frameAspect = 1.0;
        setFrameFlex(clip, flex);
        auto a = spatialAdapterOf(clip);
        QVERIFY(a && isFramingEffect(a->framing));
        // Without keyframes, the box all through.
        QVERIFY(framingAt(*a, 0, 11) == flex);
        // Keyframes on X at the first and last frames: halfway at frame 5.
        const EffectSpan span(11);
        setKeyframe(a->framing, QLatin1String(kFramingParameters), QStringLiteral("X"), 0.0, span, 50.0);
        setKeyframe(a->framing, QLatin1String(kFramingParameters), QStringLiteral("X"), 1.0, span, -50.0);
        QVERIFY(std::abs(framingAt(*a, 5, 11)->x) < 1e-6);
        QVERIFY(std::abs(framingAt(*a, 10, 11)->x + 50) < 1e-6);
        // The Reformat menu decides how the box fits.
        QJsonObject group = a->framing.parameters.value(QLatin1String(kFramingParameters)).toObject();
        group.insert(QStringLiteral("Reformat"), QStringLiteral("Center Crop"));
        a->framing.parameters.insert(QLatin1String(kFramingParameters), group);
        QCOMPARE(adapterAt(*a, 0, 11).reformat, Reformat::CenterCrop);
        keepFramingChange(*a);
        QVERIFY(a->creative && a->reformat == Reformat::CenterCrop);
        // JSON keeps it; an adapter from before 8.5b3 makes its effect from the box.
        QVERIFY(SpatialAdapter::fromJson(a->toJson()) == *a);
        QJsonObject old = a->toJson();
        old.remove(QStringLiteral("framing"));
        QVERIFY(isFramingEffect(SpatialAdapter::fromJson(old).framing));
        // The Layout button bypasses the box.
        setFramingBypassed(a->framing, true);
        QVERIFY(framingBypassed(a->framing) && !framingAt(*a, 5, 11));
        QVERIFY(!adapterAt(*a, 5, 11).frameFlex);
        setFramingBypassed(a->framing, false);
        // Alt+Layout: the keyframe at the last frame goes back to the box's X.
        QVERIFY(resetFramingAt(a->framing, flex, 1.0, 11));
        QVERIFY(std::abs(framingAt(*a, 10, 11)->x - 50) < 1e-6);
        QVERIFY(!resetFramingAt(a->framing, flex, 1.0, 11));
        QVERIFY(!resetFramingAt(a->framing, flex, 0.5, 11)); // no keyframe there

        // The Effect Editor finds it on a segment without an effect.
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = clip.id;
        s.binId = QStringLiteral("b");
        s.sourceTrack = QStringLiteral("V1");
        s.length = 11;
        s.spatial = *a;
        seq.track(QStringLiteral("V1"))->segments = {s};
        const auto ref = effectAt(seq, {QStringLiteral("V1")}, 3);
        QVERIFY(ref && ref->target == EffectTarget::Spatial);
        Effect* e = effectFor(seq, *ref);
        QVERIFY(e && isFramingEffect(*e));
        // Remove Effect leaves it.
        QVERIFY(removeEffect(seq, QStringLiteral("V1"), 3, EffectTarget::Spatial).isEmpty());
        // A segment effect comes first.
        seq.track(QStringLiteral("V1"))->segments[0].effect = Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
        QCOMPARE(effectAt(seq, {QStringLiteral("V1")}, 3)->target, EffectTarget::Segment);
    }

    // The box over the Effect Preview's whole picture and dragging it (p.533).
    void framingDrags() {
        const QSize source(200, 100);
        FrameFlex f;
        f.x = 50;
        f.size = 0.5;
        f.frameAspect = 1.0;
        const QSizeF out(200, 200); // the 2:1 image letterboxed: 200 x 100 from y = 50
        const QPolygonF outline = media::framingOutline(source, f, QSize(200, 200), out);
        QCOMPARE(outline.size(), 4);
        QCOMPARE(outline.boundingRect().toRect(), QRect(100, 50, 100, 100));
        const auto handles = media::framingHandles(source, f, QSize(200, 200), out);
        QCOMPARE(handles.size(), 6);
        QCOMPARE(handles.at(0).id, QStringLiteral("move"));
        QVERIFY(handles.last().id == QLatin1String("rotZ") && handles.last().at.y() < 50);
        // Moving by 40 preview pixels moves the box 40 image pixels; a corner twice as far from the centre doubles it;
        // the rotation handle turned a quarter about the centre turns the box a quarter.
        FrameFlex moved = media::dragFraming(source, f, QStringLiteral("move"), QPointF(150, 100), QPointF(110, 100), QSize(200, 200), out);
        QVERIFY(std::abs(moved.x - 10) < 1e-6 && std::abs(moved.y) < 1e-6);
        moved = media::dragFraming(source, f, QStringLiteral("corner3"), QPointF(200, 150), QPointF(250, 200), QSize(200, 200), out);
        QVERIFY(std::abs(moved.size - 1.0) < 1e-6);
        moved = media::dragFraming(source, f, QStringLiteral("rotZ"), QPointF(150, 40), QPointF(210, 100), QSize(200, 200), out);
        QVERIFY(std::abs(std::abs(moved.rotation) - 90) < 1e-6);
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
