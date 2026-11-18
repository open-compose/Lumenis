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
        // NTSC Has Setup off (p.1192): the scale's labels change (black at 0 IRE); PAL's millivolts do not.
        const QImage noSetup = scopeImage(solid(QColor(128, 128, 128)), Scope::YcWaveform, QSize(kW, kH), false, nullptr, false);
        QVERIFY(noSetup != grey);
        const QImage pal = scopeImage(solid(QColor(128, 128, 128)), Scope::YcWaveform, QSize(kW, kH), true);
        QCOMPARE(scopeImage(solid(QColor(128, 128, 128)), Scope::YcWaveform, QSize(kW, kH), true, nullptr, false), pal);
    }

    void safeColorLimits() {
        // "The Y Waveform, YC Waveform, and RGB Parade displays incorporate any safe color limits you have set" (p.727):
        // legal luma green, illegal white; legal composite cyan, illegal yellow; an illegal RGB channel white.
        lumenis::model::SafeColorLimits tight;
        tight.luma = {16.0, 200.0, lumenis::model::SafeUnit::Bit8};
        tight.rgb = {16.0, 200.0, lumenis::model::SafeUnit::Bit8};
        const auto whiteAt = [](const QImage& image, QPoint at) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const QPoint p = at + QPoint(dx, dy);
                    if (!image.rect().contains(p)) continue;
                    const QRgb c = image.pixel(p);
                    if (qRed(c) > 150 && qGreen(c) > 150 && qBlue(c) > 150) return true;
                }
            }
            return false;
        };
        // White (level 235) is past a luma limit of 200: white, not green; without limits, green.
        const QImage y = scopeImage(solid(Qt::white), Scope::YWaveform, QSize(kW, kH), false, &tight);
        QVERIFY(whiteAt(y, waveformPoint(0.5, 235)));
        const QImage plain = scopeImage(solid(Qt::white), Scope::YWaveform, QSize(kW, kH));
        QVERIFY(!whiteAt(plain, waveformPoint(0.5, 235)));
        QVERIFY(greenNear(plain, waveformPoint(0.5, 235)) > 120);
        // Red's red channel (235) is past an RGB limit of 200 in the RGB Parade's red band: white there.
        const QImage parade = scopeImage(solid(Qt::red), Scope::RgbParade, QSize(kW, kH), false, &tight);
        const int third = kArea.width() / 3;
        const QPoint redTop(kArea.left() + third / 2, kArea.top() + static_cast<int>(std::lround(waveformRow(235, kArea.height()))));
        QVERIFY(whiteAt(parade, redTop));
        // A composite peak past the limits is yellow; the default 120 IRE lies above the display, which shows it as the red
        // edge (p.690), so the limit is lowered to 90 IRE here: a pale yellow's peak (about 99 IRE) is drawn yellow.
        lumenis::model::SafeColorLimits composite;
        composite.composite = {0.0, 90.0, lumenis::model::SafeUnit::Ire};
        const QImage yc = scopeImage(solid(QColor(220, 220, 120)), Scope::YcWaveform, QSize(kW, kH), false, &composite);
        int yellow = 0;
        for (int yy = kArea.top() + 4; yy < kArea.bottom(); ++yy) {
            for (int x = kArea.left(); x < kArea.right(); ++x) {
                const QRgb c = yc.pixel(x, yy);
                yellow += qRed(c) > 150 && qGreen(c) > 150 && qBlue(c) < 90 ? 1 : 0;
            }
        }
        QVERIFY(yellow > 0);
    }

    void pictureViolations() {
        // Every pixel as the level it stands for (6.10e4b): white is 235, within the defaults; tightened, past them.
        const lumenis::model::SafeColorLimits defaults;
        QVERIFY(!lumenis::media::pictureViolations(solid(Qt::white), defaults).any());
        lumenis::model::SafeColorLimits tight;
        tight.rgb = {16.0, 200.0, lumenis::model::SafeUnit::Bit8};
        const auto v = lumenis::media::pictureViolations(halves(Qt::black, Qt::red), tight);
        QVERIFY(v.rgbHigh[0] && !v.rgbHigh[1] && !v.rgbLow[0]);
        // Vivid blue's composite dips below 0 IRE ("those from vivid blue might be unacceptably low", p.727).
        QVERIFY(lumenis::media::pictureViolations(solid(Qt::blue), defaults).compositeLow);
        QVERIFY(!lumenis::media::pictureViolations(QImage(), defaults).any());
    }

    void safeColorAnalysis() {
        // The Safe Color Limiter's analysis display (FX Guide p.729-730): the picture dimmed, pixels outside the limits
        // Yellow (composite), White (luminance), Red, Green, Blue (RGB gamut); one colour per pixel.
        const lumenis::model::SafeColorLimits defaults;
        // Vivid yellow is past 120 IRE composite: yellow; grey is within: a third of its levels.
        const QImage a = safeColorAnalysisImage(halves(Qt::yellow, QColor(120, 120, 120)), defaults, true, true);
        QCOMPARE(a.pixel(5, 5), qRgb(240, 220, 40));
        QCOMPARE(a.pixel(50, 5), qRgb(40, 40, 40));
        // With the Composite/Luma Levels Enable button off, yellow is only dimmed.
        QCOMPARE(safeColorAnalysisImage(solid(Qt::yellow), defaults, false, true).pixel(5, 5), qRgb(85, 85, 0));
        // RGB: white past a gamut of 200 in every channel shows red, the first of them; luma past 90 IRE shows white
        // first.
        lumenis::model::SafeColorLimits tight;
        tight.rgb = {16.0, 200.0, lumenis::model::SafeUnit::Bit8};
        QCOMPARE(safeColorAnalysisImage(solid(Qt::white), tight, true, true).pixel(5, 5), qRgb(230, 40, 40));
        tight.luma = {0.0, 90.0, lumenis::model::SafeUnit::Ire};
        QCOMPARE(safeColorAnalysisImage(solid(Qt::white), tight, true, true).pixel(5, 5), qRgb(240, 240, 240));
        QVERIFY(safeColorAnalysisImage(QImage(), defaults, true, true).isNull());
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
