// The Illusion FX effects: the segment effects that colour, texture, draw and distort, and the
// transitions (ROADMAP 6.3d2-4; docs/spec/illusion-fx.md). A GUI test: Pattern Generator's number needs fonts.
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

// Red across and green down, so the colour tells where a pixel was taken from.
Picture ramp(int w = 100, int h = 100) {
    Picture p(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* px = p.pixel(x, y);
            px[0] = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            px[1] = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
            px[2] = 0.0f;
            px[3] = 1.0f;
        }
    }
    return p;
}

QJsonObject in(const QString& category, const QJsonObject& values) {
    return QJsonObject{{category, values}};
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
    void pinchAndSphere() {
        // Pinch pulls toward its point and pushes out with a negative Amount; Sphere the other way
        // round; nothing moves outside the circle (p.521, p.526). A pixel right of the centre, halfway
        // to the circle's edge (radius 30 at Size 60).
        const QString pinch = QStringLiteral("Pinch Effect Input Parameters");
        const int own = ramp().toImage().pixelColor(65, 50).red();
        QVERIFY(apply(QStringLiteral("Pinch"), ramp(), in(pinch, {{QStringLiteral("Amount"), 80}})).pixelColor(65, 50).red() > own + 5);
        QVERIFY(apply(QStringLiteral("Pinch"), ramp(), in(pinch, {{QStringLiteral("Amount"), -80}})).pixelColor(65, 50).red() < own - 5);
        QCOMPARE(apply(QStringLiteral("Pinch"), ramp(), in(pinch, {{QStringLiteral("Amount"), 80}})).pixelColor(90, 50),
                 ramp().toImage().pixelColor(90, 50));
        const QString sphere = QStringLiteral("Sphere Effect Input Parameters");
        QVERIFY(apply(QStringLiteral("Sphere"), ramp(), in(sphere, {{QStringLiteral("Amount"), 80}})).pixelColor(65, 50).red() < own - 5);
        QVERIFY(apply(QStringLiteral("Sphere"), ramp(), in(sphere, {{QStringLiteral("Amount"), -80}})).pixelColor(65, 50).red() > own + 5);
    }

    void swirl() {
        // "Positive values swirl it clockwise" (p.527): right of the centre shows what was above it.
        const QString cat = QStringLiteral("Swirl Effect Input Parameters");
        const int own = ramp().toImage().pixelColor(65, 50).green();
        QVERIFY(apply(QStringLiteral("Swirl"), ramp(), in(cat, {{QStringLiteral("Amount"), 30}})).pixelColor(65, 50).green() < own - 5);
        QVERIFY(apply(QStringLiteral("Swirl"), ramp(), in(cat, {{QStringLiteral("Amount"), -30}})).pixelColor(65, 50).green() > own + 5);
        // Anti-alias off takes whole pixels: only the two colours of the halves.
        const QImage hard = apply(QStringLiteral("Swirl"), halves(), in(cat, {{QStringLiteral("Anti-alias"), false}}));
        for (int y = 0; y < hard.height(); ++y) {
            for (int x = 0; x < hard.width(); ++x) {
                const QColor c = hard.pixelColor(x, y);
                QVERIFY(c == QColor(255, 0, 0) || c == QColor(0, 0, 255));
            }
        }
    }

    void twist() {
        // "twisting the two ends in opposite directions" (p.527): the centre's column stays, the ends
        // turn edge on and away (Amount 50: a quarter turn at the picture's ends).
        const QString cat = QStringLiteral("Twist Effect Input Parameters");
        const QImage out = apply(QStringLiteral("Twist"), ramp(), in(cat, {{QStringLiteral("Amount"), 50}}));
        QCOMPARE(out.pixelColor(50, 20), ramp().toImage().pixelColor(50, 20));
        QCOMPARE(out.pixelColor(99, 20), QColor(0, 0, 0));
        // Along Y the rows turn instead.
        const QImage alongY = apply(QStringLiteral("Twist"), ramp(), in(cat, {{QStringLiteral("Amount"), 50}, {QStringLiteral("Axis"), QStringLiteral("Y axis")}}));
        QCOMPARE(alongY.pixelColor(20, 99), QColor(0, 0, 0));
        QCOMPARE(alongY.pixelColor(20, 50), ramp().toImage().pixelColor(20, 50));
    }

    void wave() {
        // Random waves, the same for the same Random Seed; no distortion at both Factors 0 (p.528).
        const QString cat = QStringLiteral("Wave Effect Input Parameters");
        const QImage a = apply(QStringLiteral("Wave"), ramp());
        QVERIFY(a != ramp().toImage());
        QCOMPARE(apply(QStringLiteral("Wave"), ramp()), a);
        QVERIFY(apply(QStringLiteral("Wave"), ramp(), in(cat, {{QStringLiteral("Random Seed"), 7}})) != a);
        QCOMPARE(apply(QStringLiteral("Wave"), ramp(), in(cat, {{QStringLiteral("Horizontal Factor"), 0}, {QStringLiteral("Vertical Factor"), 0}})),
                 ramp().toImage());
    }

    void radialBlur() {
        // A blur out from the centre (p.522): across the halves' edge when the centre is left of it.
        const QString cat = QStringLiteral("Radial Blur Effect Input Parameters");
        const QColor blurred = apply(QStringLiteral("Radial Blur"), halves(), in(cat, {{QStringLiteral("Zoom"), 100}, {QStringLiteral("X"), 25}})).pixelColor(44, 27);
        QVERIFY(blurred.red() > 40 && blurred.blue() > 40);
        QCOMPARE(apply(QStringLiteral("Radial Blur"), halves(), in(cat, {{QStringLiteral("Zoom"), 0}})).pixelColor(44, 27), QColor(255, 0, 0));
        // Locate shows the picture with the centre marker.
        const QImage locate = apply(QStringLiteral("Radial Blur"), halves(), in(cat, {{QStringLiteral("Render Mode"), QStringLiteral("Locate")}}));
        QCOMPARE(locate.pixelColor(48, 27), QColor(255, 255, 255));
        QCOMPARE(locate.pixelColor(10, 10), QColor(255, 0, 0));
    }

    void ripple() {
        // Ripples that spread with Amount (p.523): none at Amount 0, the same for the same seed, and
        // more with Reflections.
        const QString cat = QStringLiteral("Ripple Effect Motion, Generation, and Illumination Parameters");
        QCOMPARE(apply(QStringLiteral("Ripple"), ramp(), in(cat, {{QStringLiteral("Amount"), 0}})), ramp().toImage());
        const QImage a = apply(QStringLiteral("Ripple"), ramp());
        QVERIFY(a != ramp().toImage());
        QCOMPARE(apply(QStringLiteral("Ripple"), ramp()), a);
        QVERIFY(apply(QStringLiteral("Ripple"), ramp(), in(cat, {{QStringLiteral("Reflections"), true}})) != a);
    }

    void illusionBorder() {
        // The Border keeps the source's edges out (p.508): with Left at 25 nothing left of a quarter of
        // the picture is taken, here by a Twist that moves nothing.
        QJsonObject params = in(QStringLiteral("Twist Effect Input Parameters"), {{QStringLiteral("Amount"), 0}});
        params.insert(QStringLiteral("Border Parameter for Illusion FX Effects"), QJsonObject{{QStringLiteral("Left"), 25}});
        const QImage out = apply(QStringLiteral("Twist"), ramp(), params);
        QVERIFY(std::abs(out.pixelColor(5, 50).red() - 64) <= 2);
        QCOMPARE(out.pixelColor(80, 50), ramp().toImage().pixelColor(80, 50));
    }
    // The transitions (6.3d4): red going out, blue coming in, at `p` of the way through.
    static QImage transition(const QString& name, double p, QJsonObject params = {}, int w = 96, int h = 54) {
        const Effect e{QStringLiteral("Illusion FX"), name, std::move(params)};
        return illusionTransitionPicture(e, Picture::solid(w, h, QColor(255, 0, 0)), Picture::solid(w, h, QColor(0, 0, 255)), p, p,
                                         EffectSpan(10, true))
            .toImage();
    }

    void transitionsRunFromOutgoingToIncoming() {
        // Every Illusion FX transition starts on the outgoing clip and ends on the incoming one, its
        // Amount following the transition until set (p.512); Swap Sources swaps them.
        const QStringList names = {QStringLiteral("Iris"),          QStringLiteral("Melt"),           QStringLiteral("Pagecurl"),
                                   QStringLiteral("Particle Blast"), QStringLiteral("Particle Orbit"), QStringLiteral("Particle Wind"),
                                   QStringLiteral("Random Blend"),  QStringLiteral("Rollup"),         QStringLiteral("Shear")};
        // (A picture 480 wide, so the particles at their sizes until set cover it at rest.)
        const QImage red = Picture::solid(480, 270, QColor(255, 0, 0)).toImage();
        const QImage blue = Picture::solid(480, 270, QColor(0, 0, 255)).toImage();
        for (const QString& name : names) {
            QVERIFY2(isIllusionTransition(Effect{QStringLiteral("Illusion FX"), name, {}}), qPrintable(name));
            QVERIFY2(transition(name, 0.0, {}, 480, 270) == red, qPrintable(name));
            QVERIFY2(transition(name, 1.0, {}, 480, 270) == blue, qPrintable(name));
            const QImage half = transition(name, 0.5, {}, 480, 270);
            QVERIFY2(half != red && half != blue, qPrintable(name));
            const QJsonObject swap{{name + QStringLiteral(" Effect Input Parameters"), QJsonObject{{QStringLiteral("Swap Sources"), true}}}};
            if (name != QLatin1String("Pagecurl")) QVERIFY2(transition(name, 0.0, swap, 480, 270) == blue, qPrintable(name));
        }
        QVERIFY(!isIllusionTransition(Effect{QStringLiteral("Illusion FX"), QStringLiteral("Swirl"), {}}));
    }

    void iris() {
        // "a circle that radiates out from the center" (p.466): the incoming clip in the middle first.
        const QImage half = transition(QStringLiteral("Iris"), 0.3);
        QCOMPARE(half.pixelColor(48, 27), QColor(0, 0, 255));
        QCOMPARE(half.pixelColor(1, 1), QColor(255, 0, 0));
        // Amount, once set, places it.
        const QJsonObject set = in(QStringLiteral("Iris Effect Input Parameters"), {{QStringLiteral("Amount"), 0}});
        QCOMPARE(transition(QStringLiteral("Iris"), 0.7, set).pixelColor(48, 27), QColor(255, 0, 0));
    }

    void melt() {
        // The foreground "melts downward off the screen" (p.466): the incoming clip shows at the top
        // first; with Melt Up at the bottom.
        const QImage down = transition(QStringLiteral("Melt"), 0.3);
        QCOMPARE(down.pixelColor(48, 1), QColor(0, 0, 255));
        QCOMPARE(down.pixelColor(48, 52), QColor(255, 0, 0));
        const QImage up = transition(QStringLiteral("Melt"), 0.3, in(QStringLiteral("Melt Effect Input Parameters"), {{QStringLiteral("Melt Up"), true}}));
        QCOMPARE(up.pixelColor(48, 52), QColor(0, 0, 255));
        QCOMPARE(up.pixelColor(48, 1), QColor(255, 0, 0));
    }

    void pagecurl() {
        // The page lifts from the bottom left corner (ours), or the bottom right with Turn Left (p.515).
        const QImage curl = transition(QStringLiteral("Pagecurl"), 0.25);
        QCOMPARE(curl.pixelColor(1, 52), QColor(0, 0, 255));
        QCOMPARE(curl.pixelColor(94, 1), QColor(255, 0, 0));
        const QImage left = transition(QStringLiteral("Pagecurl"), 0.25, in(QStringLiteral("Pagecurl Effect Input Parameters"), {{QStringLiteral("Turn Left"), true}}));
        QCOMPARE(left.pixelColor(94, 52), QColor(0, 0, 255));
        QCOMPARE(left.pixelColor(1, 1), QColor(255, 0, 0));
        // Reverse: the incoming page peels on, from the far corner.
        const QImage on = transition(QStringLiteral("Pagecurl"), 0.75, in(QStringLiteral("Pagecurl Effect Input Parameters"), {{QStringLiteral("Reverse"), true}}));
        QCOMPARE(on.pixelColor(1, 52), QColor(255, 0, 0));
        QCOMPARE(on.pixelColor(94, 1), QColor(0, 0, 255));
    }

    void rollupAndShear() {
        // Rollup toward Direction 0 (3 o'clock) uncovers the left first (p.524).
        const QImage roll = transition(QStringLiteral("Rollup"), 0.4);
        QCOMPARE(roll.pixelColor(2, 27), QColor(0, 0, 255));
        QCOMPARE(roll.pixelColor(93, 27), QColor(255, 0, 0));
        // Shear's strips move apart along Angle 0: horizontal strips, some showing blue, some red.
        const QImage strips = transition(QStringLiteral("Shear"), 0.3, in(QStringLiteral("Shear Effect Input Parameters"), {{QStringLiteral("Spread"), 0}}));
        bool red = false;
        bool blue = false;
        for (int y = 0; y < 54; ++y) {
            red = red || strips.pixelColor(2, y) == QColor(255, 0, 0);
            blue = blue || strips.pixelColor(2, y) == QColor(0, 0, 255);
        }
        QVERIFY(red && blue);
    }

    void particlesAndRandomBlend() {
        // Particle Blast blows from the centre outward: the middle clears first (p.466).
        const QImage blast = transition(QStringLiteral("Particle Blast"), 0.15, {}, 480, 270);
        QCOMPARE(blast.pixelColor(240, 135), QColor(0, 0, 255));
        QCOMPARE(blast.pixelColor(2, 2), QColor(255, 0, 0));
        // Random Blend at Random Scale 0 is a plain dissolve.
        const QColor mid = transition(QStringLiteral("Random Blend"), 0.5,
                                      in(QStringLiteral("Random Blend Effect Input Parameters"), {{QStringLiteral("Random Scale"), 0}}))
                               .pixelColor(10, 10);
        QVERIFY(std::abs(mid.red() - 128) <= 2 && std::abs(mid.blue() - 128) <= 2);
        // Above it, cells blend at different moments.
        const QImage random = transition(QStringLiteral("Random Blend"), 0.5);
        QVERIFY(random.pixelColor(10, 10) != random.pixelColor(11, 30) || random.pixelColor(40, 3) != random.pixelColor(70, 40));
    }
};

QTEST_MAIN(TestIllusionFx)
#include "tst_illusion_fx.moc"
