// Avid Pan & Zoom (FX Guide p.239-247, p.528-531; pan_zoom.cpp): the image fitted at Zoom Factor 1, zoomed and panned,
// the background (Color or Video), Display Source in the Effect Preview, 601 levels, the crosshair and Safe Action,
// and the cache.
#include "lumenis/media/compositor.h"
#include "lumenis/media/effect_handles.h"

#include <QImage>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kW = 192;
constexpr int kH = 108;
const QSizeF kSize(kW, kH);
const model::EffectSpan kSpan(25);
const QString kCategory = QStringLiteral("Avid Pan & Zoom Effect Parameters");

// 400 x 200: red on the left half, blue on the right.
QImage halves() {
    QImage image(400, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 400; ++x) image.setPixelColor(x, y, x < 200 ? QColor(255, 0, 0) : QColor(0, 0, 255));
    }
    return image;
}

model::Effect panZoom(const QString& path, const QJsonObject& more = {}) {
    model::Effect e{QStringLiteral("Image"), QStringLiteral("Avid Pan & Zoom"), {}};
    QJsonObject group = more;
    group.insert(QStringLiteral("Image"), path);
    e.parameters.insert(kCategory, group);
    return e;
}

char colourAt(const Picture& p, int x, int y) {
    const float* c = p.pixel(x, y);
    if (c[0] > 0.9f && c[1] < 0.1f && c[2] < 0.1f) return 'r';
    if (c[2] > 0.9f && c[0] < 0.1f && c[1] < 0.1f) return 'b';
    if (c[1] > 0.9f && c[0] < 0.1f && c[2] < 0.1f) return 'g';
    if (c[0] < 0.02f && c[1] < 0.02f && c[2] < 0.02f) return 'k';
    return '?';
}

} // namespace

class TestPanZoom : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;
    QString path_;

private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        path_ = dir_.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves().save(path_));
    }

    // "At 1, the effect applies no magnification to the image" (p.242): ours, the whole image fitted, letterboxed
    // over the Background Color (black until set).
    void fitted() {
        const auto v = panZoomView(panZoom(path_), 0.0, kSpan, kSize);
        QVERIFY(v);
        QCOMPARE(v->imageSize, QSize(400, 200));
        QCOMPARE(v->fitted, QRectF(0, 6, 192, 96));
        QCOMPARE(v->zoom, 1.0);
        QVERIFY(v->sourceDisplay); // Display is Source until set
        const Picture out = panZoomPicture(panZoom(path_), Picture::solid(kW, kH, Qt::green), 0.0, kSpan, false);
        QCOMPARE(colourAt(out, 10, 54), 'r');
        QCOMPARE(colourAt(out, 180, 54), 'b');
        QCOMPARE(colourAt(out, 96, 2), 'k');
        // Background Video: the placeholder segment shows outside the image (p.531).
        const Picture video = panZoomPicture(panZoom(path_, {{QStringLiteral("Background"), QStringLiteral("Video")}}),
                                             Picture::solid(kW, kH, Qt::green), 0.0, kSpan, false);
        QCOMPARE(colourAt(video, 96, 2), 'g');
        // No image (or a missing one): the placeholder as it is.
        const Picture none = panZoomPicture(panZoom(dir_.filePath(QStringLiteral("gone.png"))), Picture::solid(kW, kH, Qt::green), 0.0, kSpan, false);
        QCOMPARE(colourAt(none, 96, 54), 'g');
    }

    // Zooming in on the right half and panning there; Display Source shows the whole image in the Effect Preview only.
    void zoomAndPan() {
        const model::Effect e = panZoom(path_, {{QStringLiteral("Zoom Factor"), 4.0}, {QStringLiteral("X"), 50}});
        const auto v = panZoomView(e, 0.0, kSpan, kSize);
        QCOMPARE(v->field.center(), QPointF(144, 54));
        QCOMPARE(v->field.size(), QSizeF(48, 27));
        const Picture out = panZoomPicture(e, Picture(kW, kH), 0.0, kSpan, false);
        QCOMPARE(colourAt(out, 2, 2), 'b');
        QCOMPARE(colourAt(out, 189, 105), 'b');
        const Picture source = panZoomPicture(e, Picture(kW, kH), 0.0, kSpan, true);
        QCOMPARE(colourAt(source, 10, 54), 'r'); // the whole image
        model::Effect target = e;
        QJsonObject g = target.parameters.value(kCategory).toObject();
        g.insert(QStringLiteral("Display"), QStringLiteral("Target"));
        target.parameters.insert(kCategory, g);
        QCOMPARE(colourAt(panZoomPicture(target, Picture(kW, kH), 0.0, kSpan, true), 10, 54), 'b');
    }

    // The crosshair: the field of view's centre in Display Source, dragged over the image (p.242); Safe Action.
    void crosshair() {
        const model::Effect e = panZoom(path_, {{QStringLiteral("Zoom Factor"), 2.0}, {QStringLiteral("Show Safe Action"), true}});
        QVERIFY(hasEffectOutline(e));
        const QList<EffectHandle> handles = effectHandles(e, HandleTool::None, 0.0, kSpan, kSize);
        QCOMPARE(handles.size(), 1);
        QCOMPARE(handles.first().id, QStringLiteral("crosshair"));
        QCOMPARE(handles.first().at, QPointF(96, 54));
        const auto outline = effectOutline(e, 0.0, kSpan, kSize);
        QVERIFY(outline);
        QCOMPARE(outline->corners[0], QPointF(48, 27));
        const QList<HandleValue> moved = dragEffectHandle(e, HandleTool::None, QStringLiteral("crosshair"), QPointF(96, 54), QPointF(120, 54), 0.0, kSpan, kSize);
        QCOMPARE(moved.size(), 2);
        QVERIFY(std::abs(moved.at(0).value - 25.0) < 1e-9); // 24 of the fitted image's 96-pixel half-width
        QVERIFY(std::abs(moved.at(1).value) < 1e-9);
        const QList<QPolygonF> guides = effectGuides(e, 0.0, kSpan, kSize);
        QCOMPARE(guides.size(), 1);
        QCOMPARE(guides.first().boundingRect(), QRectF(52.8, 29.7, 86.4, 48.6));
    }

    // "Source has 601 Levels" (p.247): video black (16) becomes black in our full-range pictures.
    void levels() {
        QImage grey(16, 16, QImage::Format_RGB32);
        grey.fill(QColor(16, 16, 16));
        const QString path = dir_.filePath(QStringLiteral("grey.png"));
        QVERIFY(grey.save(path));
        const Picture rgb = panZoomPicture(panZoom(path), Picture(kW, kH), 0.0, kSpan, false);
        QVERIFY(rgb.pixel(96, 54)[0] > 0.05f);
        const Picture video = panZoomPicture(panZoom(path, {{QStringLiteral("Source Levels"), QStringLiteral("601")}}), Picture(kW, kH), 0.0, kSpan, false);
        QVERIFY(video.pixel(96, 54)[0] < 0.005f);
    }

    // Free Current Cache: the file is read again (p.247).
    void cache() {
        const QString path = dir_.filePath(QStringLiteral("changing.png"));
        QVERIFY(halves().save(path));
        QCOMPARE(panZoomImageSize(path), QSize(400, 200));
        QVERIFY(halves().scaled(100, 100).save(path));
        freePanZoomCache(path);
        QCOMPARE(panZoomImageSize(path), QSize(100, 100));
        freePanZoomCache();
    }
};

QTEST_MAIN(TestPanZoom)
#include "tst_pan_zoom.moc"
