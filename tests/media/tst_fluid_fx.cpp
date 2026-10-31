// FluidBlur, FluidColorMap and FluidMorph (FX Guide p.467, p.510-512; lumenis/media/fluid_fx.h): only moving
// things are blurred, along their motion; motion's direction becomes colour on the vectorscope's compass and
// its size brightness; the morph goes from one picture to the other.
#include "lumenis/media/fluid_fx.h"

#include <QTest>

#include <cmath>

using namespace lumenis::media;

namespace {

// A 128x72 grey picture with a textured 24-pixel square at (x, y): the texture makes its motion visible.
QImage scene(int x, int y) {
    QImage image(128, 72, QImage::Format_ARGB32);
    image.fill(qRgb(60, 60, 60));
    for (int j = 0; j < 24; ++j) {
        for (int i = 0; i < 24; ++i) {
            // A texture that does not repeat, so no shift of it looks like another.
            const unsigned h = (static_cast<unsigned>(i) * 73856093u) ^ (static_cast<unsigned>(j) * 19349663u);
            const int v = 110 + static_cast<int>((h >> 3) % 130u);
            image.setPixel(x + i, y + j, qRgb(v, v, v));
        }
    }
    return image;
}

// How much the picture changes across a row's pixels inside [x0, x1): sharp texture changes a lot.
double rowRoughness(const QImage& image, int y, int x0, int x1) {
    double sum = 0;
    for (int x = x0 + 1; x < x1; ++x) sum += std::abs(qGray(image.pixel(x, y)) - qGray(image.pixel(x - 1, y)));
    return sum / (x1 - x0 - 1);
}

} // namespace

class TestFluidFx : public QObject {
    Q_OBJECT

private slots:
    void blur() {
        // The square moves 6 pixels right a frame (90 pixels of a 1920-wide picture).
        const QImage before = scene(34, 24);
        const QImage frame = scene(40, 24);
        const QImage next = scene(46, 24);
        FluidBlurOptions o;
        o.amount = 100;
        const QImage blurred = fluidBlur(before, frame, next, o);
        // The still background is left alone, the moving square blurred along its motion.
        QCOMPARE(blurred.pixel(5, 5), frame.pixel(5, 5));
        QVERIFY(rowRoughness(blurred, 36, 44, 60) < rowRoughness(frame, 36, 44, 60) * 0.7);
        // Above the Upper Motion Threshold nothing is blurred.
        o.upper = 50;
        const QImage over = fluidBlur(before, frame, next, o);
        QCOMPARE(rowRoughness(over, 36, 44, 60), rowRoughness(frame, 36, 44, 60));
        // Nothing moving: nothing blurred.
        QCOMPARE(fluidBlur(frame, frame, frame, FluidBlurOptions{}), frame.convertToFormat(QImage::Format_ARGB32));
    }

    void colorMap() {
        FluidColorMapOptions o;
        o.maxMotion = 100;
        // Still: black.
        const QImage still = fluidColorMap(scene(40, 24), scene(40, 24), o);
        QCOMPARE(qGray(still.pixel(52, 36)), 0);
        // Moving right: Cb up, a blue colour; brighter than the still background.
        const QImage right = fluidColorMap(scene(40, 24), scene(46, 24), o);
        const QRgb r = right.pixel(52, 36);
        QVERIFY(qBlue(r) > qRed(r));
        QVERIFY(qGray(r) > 10);
        QCOMPARE(qGray(right.pixel(5, 5)), 0);
        // Moving up the picture: Cr up, a red colour.
        const QRgb up = fluidColorMap(scene(40, 30), scene(40, 24), o).pixel(52, 40);
        QVERIFY(qRed(up) > qBlue(up));
        // Constant Saturation at its maximum: the colour's strength for its brightness does not change with
        // the motion's size.
        o.constantSaturation = 100;
        o.maxMotion = 200;
        const QRgb slow = fluidColorMap(scene(40, 24), scene(43, 24), o).pixel(52, 36);
        const QRgb fast = fluidColorMap(scene(40, 24), scene(46, 24), o).pixel(52, 36);
        QVERIFY(qGray(fast) > qGray(slow));
    }

    void morph() {
        QImage dark(32, 16, QImage::Format_ARGB32);
        dark.fill(qRgb(20, 20, 20));
        QImage light(32, 16, QImage::Format_ARGB32);
        light.fill(qRgb(220, 220, 220));
        QCOMPARE(fluidMorph(dark, light, 0.0, false).pixel(3, 3), dark.pixel(3, 3));
        QCOMPARE(fluidMorph(dark, light, 1.0, false).pixel(3, 3), light.pixel(3, 3));
        // By brightness: going to a brighter picture changes over early; to a darker one, late.
        QVERIFY(qGray(fluidMorph(dark, light, 0.35, false).pixel(3, 3)) > 120);
        QVERIFY(qGray(fluidMorph(light, dark, 0.35, false).pixel(3, 3)) > 200);
        // Feature Match follows the motion between the pictures: half way, the square half way along.
        const QImage mid = fluidMorph(scene(30, 24), scene(42, 24), 0.5, true);
        QVERIFY(qGray(mid.pixel(38, 36)) > 100);
    }
};

QTEST_GUILESS_MAIN(TestFluidFx)
#include "tst_fluid_fx.moc"
