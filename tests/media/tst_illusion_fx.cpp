// The Illusion FX segment effects that colour, texture or draw (ROADMAP 6.3d2;
// docs/spec/illusion-fx.md). A GUI test: Pattern Generator's number needs fonts.
#include "lumenis/media/compositor.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::media;
using namespace lumenis::model;

namespace {

Picture halves(int w = 96, int h = 54) {
    Picture p = Picture::solid(w, h, QColor(0, 0, 255));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w / 2; ++x) {
            float* px = p.pixel(x, y);
            px[0] = 1.0f;
            px[2] = 0.0f;
        }
    }
    return p;
}

QImage apply(const QString& name, const Picture& in, QJsonObject params = {}, double time = 0.5, std::int64_t length = 11) {
    const Effect e{QStringLiteral("Illusion FX"), name, std::move(params)};
    return imageEffectPicture(e, in, time, EffectSpan(length)).toImage();
}

int brightest(const QImage& image) {
    int best = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) best = std::max(best, image.pixelColor(x, y).lightness());
    }
    return best;
}

} // namespace

class TestIllusionFx : public QObject {
    Q_OBJECT

private slots:
    void colorMix() {
        // "blend a solid color with the image" (p.464): orange at Color Level 50 until set (ours).
        const QColor out = apply(QStringLiteral("Color Mix"), Picture::solid(8, 8, QColor(0, 0, 255))).pixelColor(4, 4);
        QVERIFY(out.red() > 100 && out.blue() < 140);
        const QJsonObject none{{QStringLiteral("Color Mix Effect Input Parameters"), QJsonObject{{QStringLiteral("Color Level"), 0}}}};
        QCOMPARE(apply(QStringLiteral("Color Mix"), Picture::solid(8, 8, QColor(0, 0, 255)), none).pixelColor(4, 4), QColor(0, 0, 255));
    }

    void filmGrain() {
        // Grain that changes every frame and is the same for the same frame (p.509).
        const Picture grey = Picture::solid(32, 32, QColor(128, 128, 128));
        const QImage a = apply(QStringLiteral("Film Grain"), grey, {}, 0.1);
        QCOMPARE(apply(QStringLiteral("Film Grain"), grey, {}, 0.1), a);
        QVERIFY(apply(QStringLiteral("Film Grain"), grey, {}, 0.2) != a);
        QVERIFY(a != grey.toImage());
        const QJsonObject off{{QStringLiteral("Film Grain Effect Input Parameters"), QJsonObject{{QStringLiteral("Level"), 0}}}};
        QCOMPARE(apply(QStringLiteral("Film Grain"), grey, off), grey.toImage());
    }

    void crystal() {
        // Polygons, each one colour from the picture (p.509).
        const QImage out = apply(QStringLiteral("Crystal"), halves(),
                                 {{QStringLiteral("Crystal Effect Input Parameters"), QJsonObject{{QStringLiteral("Size"), 200}}}});
        for (int y = 0; y < out.height(); y += 5) {
            for (int x = 0; x < out.width(); x += 5) {
                const QColor c = out.pixelColor(x, y);
                QVERIFY(c == QColor(255, 0, 0) || c == QColor(0, 0, 255));
            }
        }
        QCOMPARE(out.pixelColor(2, 27), QColor(255, 0, 0));
        QCOMPARE(out.pixelColor(93, 27), QColor(0, 0, 255));
    }

    void flare() {
        // A ring of light round the centre (p.510); Brightness 0 shows none.
        const Picture black = Picture::solid(96, 54, QColor(0, 0, 0));
        QVERIFY(brightest(apply(QStringLiteral("Flare"), black)) > 100);
        const QJsonObject off{{QStringLiteral("Flare Effect Input Parameters"), QJsonObject{{QStringLiteral("Brightness"), 0}}}};
        QCOMPARE(brightest(apply(QStringLiteral("Flare"), black, off)), 0);
    }

    void patternGenerator() {
        // Color bars at 75% until set (ours): white at 75% on the left, blue on the right (p.520).
        const Picture any = Picture::solid(70, 40, QColor(10, 200, 10));
        const QString cat = QStringLiteral("Pattern Generator Effect Background, Number, and Circle Parameters");
        QImage bars = apply(QStringLiteral("Pattern Generator"), any);
        QCOMPARE(bars.pixelColor(2, 20), QColor(191, 191, 191));
        QCOMPARE(bars.pixelColor(67, 20), QColor(0, 0, 191));
        // The Y axis stacks them.
        bars = apply(QStringLiteral("Pattern Generator"), any, {{cat, QJsonObject{{QStringLiteral("Axis"), QStringLiteral("Y axis")}}}});
        QCOMPARE(bars.pixelColor(35, 1), QColor(191, 191, 191));
        QCOMPARE(bars.pixelColor(35, 38), QColor(0, 0, 191));
        // Solid colour; Grid with a black background under a light base colour.
        const QImage solid = apply(QStringLiteral("Pattern Generator"), any,
                                   {{cat, QJsonObject{{QStringLiteral("Pattern Type"), QStringLiteral("Solid color")},
                                                      {QStringLiteral("Color controls"), QStringLiteral("#336699")}}}});
        QCOMPARE(solid.pixelColor(30, 30), QColor(0x33, 0x66, 0x99));
        const QImage grid = apply(QStringLiteral("Pattern Generator"), any,
                                  {{cat, QJsonObject{{QStringLiteral("Pattern Type"), QStringLiteral("Grid")},
                                                     {QStringLiteral("Color controls"), QStringLiteral("#ffffff")}}}});
        QCOMPARE(grid.pixelColor(6, 6), QColor(0, 0, 0));
        // The frame number, once it has an opacity.
        const QJsonObject numbered{{QStringLiteral("Pattern Type"), QStringLiteral("Solid color")},
                                   {QStringLiteral("Color controls"), QStringLiteral("#000000")},
                                   {QStringLiteral("Opacity"), 100},
                                   {QStringLiteral("Text Size"), 300}};
        QVERIFY(brightest(apply(QStringLiteral("Pattern Generator"), any, {{cat, numbered}})) > 200);
    }

    void lightning() {
        // A bolt down the picture (ours); Amount 0 draws nothing (p.514).
        const Picture black = Picture::solid(96, 54, QColor(0, 0, 0));
        QVERIFY(brightest(apply(QStringLiteral("Lightning"), black)) > 200);
        const QJsonObject none{{QStringLiteral("Lightning Effect Generation, Core, and Glow Parameters"), QJsonObject{{QStringLiteral("Amount"), 0}}}};
        QCOMPARE(brightest(apply(QStringLiteral("Lightning"), black, none)), 0);
    }

    void sparkler() {
        // Sparks from the centre once some have been born; none at Birth Rate 0 (p.525).
        const Picture black = Picture::solid(96, 54, QColor(0, 0, 0));
        QVERIFY(brightest(apply(QStringLiteral("Sparkler"), black, {}, 0.5, 21)) > 100);
        const QJsonObject none{{QStringLiteral("Sparkler Effect Generation, Movement, Core, and Glow Parameters"), QJsonObject{{QStringLiteral("Birth Rate"), 0}}}};
        QCOMPARE(brightest(apply(QStringLiteral("Sparkler"), black, none, 0.5, 21)), 0);
    }

    void kaleidoscope() {
        // Two mirrors about the centre: the right half mirrored onto the left (p.513).
        const QJsonObject two{{QStringLiteral("Kaleidoscope Effect Input, Center, and Offset Parameters"), QJsonObject{{QStringLiteral("Symmetry"), 2}}}};
        const QImage out = apply(QStringLiteral("Kaleidoscope"), halves(), two);
        QCOMPARE(out.pixelColor(10, 20), QColor(0, 0, 255));
        QCOMPARE(out.pixelColor(85, 20), QColor(0, 0, 255));
    }

    void motionBlur() {
        // A blur along its Direction: across the red-blue edge at 0 degrees, along it at 90 (p.515).
        const QJsonObject across{{QStringLiteral("Motion Blur Effect Input Parameters"), QJsonObject{{QStringLiteral("Amount"), 400}}}};
        const QColor blurred = apply(QStringLiteral("Motion Blur"), halves(), across).pixelColor(47, 27);
        QVERIFY(blurred.red() > 40 && blurred.blue() > 40);
        const QJsonObject along{{QStringLiteral("Motion Blur Effect Input Parameters"),
                                 QJsonObject{{QStringLiteral("Amount"), 400}, {QStringLiteral("Direction"), 90}}}};
        QCOMPARE(apply(QStringLiteral("Motion Blur"), halves(), along).pixelColor(40, 27), QColor(255, 0, 0));
    }
};

QTEST_MAIN(TestIllusionFx)
#include "tst_illusion_fx.moc"
