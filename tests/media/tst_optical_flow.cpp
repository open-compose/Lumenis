// FluidMotion's optical flow (FX Guide p.186, p.197; docs/spec/motion-effects.md): the motion found
// between two frames, and the in-between frame built along it rather than a mix of the two.
#include "lumenis/media/optical_flow.h"

#include <QColor>
#include <QPainter>
#include <QTest>

using namespace lumenis::media;

namespace {

// A dark frame with a little texture and a white square, with marks in it, at `x`.
QImage frame(int x) {
    QImage image(96, 64, QImage::Format_RGB32);
    image.fill(QColor(30, 30, 30));
    QPainter p(&image);
    // A grey grid that does not move, so the background holds still.
    p.setPen(QColor(60, 60, 60));
    for (int g = 0; g < 96; g += 16) p.drawLine(g, 0, g, 63);
    p.fillRect(x, 24, 14, 14, Qt::white);
    // Texture inside it, so its motion can be seen there too (a flat area matches anywhere within it).
    p.fillRect(x + 6, 27, 3, 3, QColor(90, 90, 90));
    p.fillRect(x + 3, 32, 2, 4, QColor(140, 140, 140));
    return image;
}

int red(const QImage& image, int x, int y) {
    return qRed(image.pixel(x, y));
}

} // namespace

class TestOpticalFlow : public QObject {
    Q_OBJECT

private slots:
    void findsTheMotion() {
        // The square moves 12 pixels right; the background stays.
        const FlowField flow = estimateFlow(frame(20), frame(32));
        QVERIFY(!flow.isEmpty());
        double dx = 0, dy = 0;
        flow.at(28, 28, &dx, &dy); // the centre of a block inside the square
        QVERIFY2(std::abs(dx - 12.0) <= 2.0, qPrintable(QString::number(dx)));
        QVERIFY(std::abs(dy) <= 2.0);
        flow.at(80, 8, &dx, &dy);
        QVERIFY(std::abs(dx) <= 1.0 && std::abs(dy) <= 1.0);
        // The same picture twice: no motion anywhere.
        const FlowField still = estimateFlow(frame(20), frame(20));
        for (std::size_t i = 0; i < still.dx.size(); ++i) QVERIFY(still.dx[i] == 0.0f && still.dy[i] == 0.0f);
    }

    void inBetween() {
        // Halfway, the square is whole and halfway along (x 26 to 39), where a mix of the two frames
        // would show two half-grey squares.
        const bool drafts[] = {false, true};
        for (const bool draft : drafts) {
            const QImage half = interpolateFrames(frame(20), frame(32), 0.5, draft);
            QCOMPARE(half.size(), QSize(96, 64));
            QVERIFY2(red(half, 31, 31) > 200, qPrintable(QString::number(red(half, 31, 31))));
            QVERIFY2(red(half, 27, 31) > 180, qPrintable(QString::number(red(half, 27, 31))));
            // Where only the outgoing square was, and well away from it, the background.
            QVERIFY2(red(half, 21, 31) < 120, qPrintable(QString::number(red(half, 21, 31))));
            QVERIFY(red(half, 80, 50) < 70);
        }
        // The ends are the frames themselves.
        QCOMPARE(red(interpolateFrames(frame(20), frame(32), 0.0), 22, 31), 255);
        QVERIFY(red(interpolateFrames(frame(20), frame(32), 1.0), 22, 31) < 70);
    }
};

QTEST_GUILESS_MAIN(TestOpticalFlow)
#include "tst_optical_flow.moc"
