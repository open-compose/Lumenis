// The Waveform and Vectorscope monitors' pictures (lumenis/media/scopes.h; FX Guide p.686-692): where each scope puts
// a frame's levels and colours.
#include "lumenis/media/scopes.h"

#include <QTest>

#include <cmath>

using namespace lumenis::media;

namespace {

// A frame of two halves.
QImage halves(const QColor& left, const QColor& right) {
    QImage image(64, 36, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) image.setPixelColor(x, y, x < 32 ? left : right);
    }
    return image;
}

QImage solid(const QColor& c) {
    return halves(c, c);
}

constexpr int kW = 200, kH = 140;
// scopeImage draws inside a 2-pixel margin.
const QRect kArea(2, 2, kW - 4, kH - 4);

// The brightest green near a point of the image.
int greenNear(const QImage& image, QPoint at, int reach = 2) {
    int best = 0;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const QPoint p = at + QPoint(dx, dy);
            if (image.rect().contains(p)) best = std::max(best, qGreen(image.pixel(p)));
        }
    }
    return best;
}

QPoint waveformPoint(double across, double level) {
    return {kArea.left() + static_cast<int>(std::lround(across * (kArea.width() - 1))),
            kArea.top() + static_cast<int>(std::lround(waveformRow(level, kArea.height())))};
}

} // namespace

class TestScopes : public QObject {
    Q_OBJECT

private slots:
    void conversions() {
        const YCbCr white = videoYCbCr(1, 1, 1);
        QCOMPARE(white.y, 235.0);
        QVERIFY(std::abs(white.cb) < 1e-9 && std::abs(white.cr) < 1e-9);
        QCOMPARE(videoYCbCr(0, 0, 0).y, 16.0);
        QVERIFY(std::abs(videoYCbCr(0, 0, 1).cb - 0.5) < 1e-9);
        QVERIFY(std::abs(videoYCbCr(1, 0, 0).cr - 0.5) < 1e-9);
        QVERIFY(std::abs(pqCode(10000) - 1.0) < 1e-9);
        QVERIFY(std::abs(pqCode(100) - 0.508) < 0.002); // SDR's 100 nits about half the PQ range (p.690)
        QVERIFY(waveformRow(235, 100) < waveformRow(16, 100));
    }

    void yWaveform() {
        // "If you see a bright object on the left side of the image, you see its peak on the left side of the
        // waveform" (p.687).
        const QImage scope = scopeImage(halves(Qt::white, Qt::black), Scope::YWaveform, QSize(kW, kH));
        QCOMPARE(scope.size(), QSize(kW, kH));
        QVERIFY(greenNear(scope, waveformPoint(0.25, 235)) > 120);
        QVERIFY(greenNear(scope, waveformPoint(0.75, 16)) > 120);
        QVERIFY(greenNear(scope, waveformPoint(0.75, 235), 1) < 120);
    }

    void parades() {
        // RGB Parade: red, green and blue side by side (p.688); a red frame peaks in the left band only.
        const QImage scope = scopeImage(solid(Qt::red), Scope::RgbParade, QSize(kW, kH));
        const int third = kArea.width() / 3;
        auto bandPoint = [&](int band, double level) {
            return QPoint(kArea.left() + band * third + third / 2, kArea.top() + static_cast<int>(std::lround(waveformRow(level, kArea.height()))));
        };
        auto redNear = [&](QPoint at) {
            int best = 0;
            for (int dy = -2; dy <= 2; ++dy) best = std::max(best, qRed(scope.pixel(at + QPoint(0, dy))));
            return best;
        };
        QVERIFY(redNear(bandPoint(0, 235)) > 120);
        QVERIFY(greenNear(scope, bandPoint(1, 16)) > 120);
        QVERIFY(greenNear(scope, bandPoint(1, 235), 1) < 100); // only the grey graticule there
        // YCbCr Parade: a grey frame puts Cb and Cr "halfway up the two right bands" (p.691), in white for Y.
        const QImage ycc = scopeImage(solid(QColor(128, 128, 128)), Scope::YcbcrParade, QSize(kW, kH));
        bool middle = false;
        for (int dy = -3; dy <= 3; ++dy) {
            const QRgb px = ycc.pixel(kArea.left() + 2 * third + third / 2, kArea.top() + kArea.height() / 2 + dy);
            middle = middle || qRed(px) > 100 || qGreen(px) > 100 || qBlue(px) > 100;
        }
        QVERIFY(middle);
    }

    void vectorscope() {
        // Chroma around the circle, no luma; red away from the centre where its Cb and Cr put it (p.688).
        const QImage scope = scopeImage(solid(Qt::red), Scope::Vectorscope, QSize(kW, kH));
        const YCbCr red = videoYCbCr(1, 0, 0);
        const QPointF at = QPointF(kArea.topLeft()) + vectorscopePoint(red.cb, red.cr, kArea.size());
        QVERIFY(greenNear(scope, at.toPoint()) > 120);
        const QPointF centre = QPointF(kArea.topLeft()) + vectorscopePoint(0, 0, kArea.size());
        QVERIFY(at.y() < centre.y() && at.x() < centre.x() + 1e-9 + kArea.width());
        // A grey frame sits at the centre.
        const QImage grey = scopeImage(solid(QColor(100, 100, 100)), Scope::Vectorscope, QSize(kW, kH));
        QVERIFY(greenNear(grey, centre.toPoint()) > 120);
    }

    void ycWaveform() {
        // 100% yellow's Y + C goes past the composite range: "A red edge on the display" (p.690).
        const QImage scope = scopeImage(solid(Qt::yellow), Scope::YcWaveform, QSize(kW, kH));
        const QRgb top = scope.pixel(kArea.center().x(), kArea.top());
        QVERIFY(qRed(top) > 200 && qGreen(top) < 80);
        // A grey frame has no chroma and no red edge.
        const QImage grey = scopeImage(solid(QColor(128, 128, 128)), Scope::YcWaveform, QSize(kW, kH));
        QVERIFY(qRed(grey.pixel(kArea.center().x(), kArea.top())) < 200);
    }

    void histogramsAndQuad() {
        // Histograms and Quad Display draw something in every part.
        for (Scope s : {Scope::RgbHistogram, Scope::YcbcrHistogram, Scope::YWaveformHdr1000, Scope::YWaveformHdr10000}) {
            const QImage scope = scopeImage(halves(Qt::white, QColor(40, 80, 160)), s, QSize(kW, kH));
            int lit = 0;
            for (int y = 0; y < kH; ++y) {
                for (int x = 0; x < kW; ++x) lit += qGray(scope.pixel(x, y)) > 60 ? 1 : 0;
            }
            QVERIFY2(lit > 50, qPrintable(scopeName(s)));
        }
        // "clockwise from the top left corner: YC Waveform, Vectorscope, RGB Histogram, RGB Parade" (p.687).
        const QImage quad = scopeImage(halves(Qt::white, Qt::red), Scope::QuadDisplay, QSize(kW, kH));
        for (const QRect& r : {QRect(0, 0, kW / 2, kH / 2), QRect(kW / 2, 0, kW / 2, kH / 2), QRect(0, kH / 2, kW / 2, kH / 2),
                               QRect(kW / 2, kH / 2, kW / 2, kH / 2)}) {
            int lit = 0;
            for (int y = r.top(); y <= r.bottom(); ++y) {
                for (int x = r.left(); x <= r.right(); ++x) lit += qGray(quad.pixel(x, y)) > 60 ? 1 : 0;
            }
            QVERIFY(lit > 20);
        }
        // Nothing to measure: black.
        QCOMPARE(scopeImage(QImage(), Scope::YWaveform, QSize(10, 10)).pixel(5, 5), qRgb(0, 0, 0));
    }
};

QTEST_MAIN(TestScopes)
#include "tst_scopes.moc"
