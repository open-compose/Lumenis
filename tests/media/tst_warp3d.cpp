// The 3D effects' renderer (FX Guide ch.11, p.568-585; warp3d.cpp) and promoting a Picture-in-Picture to 3D
// (p.289; lumenis/model/promote3d.h): where the picture lands for each parameter, and that a promoted effect
// looks as the 2D one did.
#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/promote3d.h"

#include <QJsonObject>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kW = 192;
constexpr int kH = 108;

// Red on the left half, green on the right; a blue band along the top tenth.
Picture foreground() {
    Picture p(kW, kH);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            float* px = p.pixel(x, y);
            if (y < kH / 10) {
                px[2] = 1.0f;
            } else if (x < kW / 2) {
                px[0] = 1.0f;
            } else {
                px[1] = 1.0f;
            }
            px[3] = 1.0f;
        }
    }
    return p;
}

Picture grey() {
    return Picture::solid(kW, kH, QColor(128, 128, 128));
}

model::Effect warp(const QJsonObject& parameters = {}) {
    model::Effect e{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
    for (auto it = parameters.begin(); it != parameters.end(); ++it) e.parameters.insert(it.key(), it.value());
    return e;
}

// The colour at a pixel as 'r', 'g', 'b' (pure), 'k' grey, '?' else.
char at(const Picture& p, int x, int y) {
    const float* c = p.pixel(x, y);
    if (c[0] > 0.9f && c[1] < 0.1f && c[2] < 0.1f) return 'r';
    if (c[1] > 0.9f && c[0] < 0.1f && c[2] < 0.1f) return 'g';
    if (c[2] > 0.9f && c[0] < 0.1f && c[1] < 0.1f) return 'b';
    if (std::abs(c[0] - 0.5f) < 0.02f && std::abs(c[1] - 0.5f) < 0.02f) return 'k';
    return '?';
}

Picture render(const model::Effect& e) {
    return warp3dPicture(e, foreground(), grey(), 0.5, model::EffectSpan(10));
}

} // namespace

class TestWarp3d : public QObject {
    Q_OBJECT

private slots:
    void untouchedIsThePicture() {
        QVERIFY(is3DEffect(warp()));
        QVERIFY(is3DEffect(model::Effect{QStringLiteral("Xpress 3D Effect"), QStringLiteral("3D PIP"), {}}));
        QVERIFY(!is3DEffect(model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}}));
        const Picture p = render(warp());
        QCOMPARE(at(p, 10, 50), 'r');
        QCOMPARE(at(p, 180, 50), 'g');
        QCOMPARE(at(p, 100, 2), 'b');
    }

    void hierarchy() {
        // Scaling (3D) 50: the picture half size in the middle, the background around it (p.579).
        Picture p = render(warp({{QStringLiteral("Scaling (3D)"), QJsonObject{{QStringLiteral("X"), 50}, {QStringLiteral("Y"), 50}}}}));
        QCOMPARE(at(p, 5, 54), 'k');
        QCOMPARE(at(p, 60, 54), 'r');
        QCOMPARE(at(p, 130, 54), 'g');
        // Position X 960 (pixels of a 1920-wide picture): half a width to the right; Y up (p.577).
        p = render(warp({{QStringLiteral("Position (3D)"), QJsonObject{{QStringLiteral("X"), 960}}}}));
        QCOMPARE(at(p, 20, 54), 'k');
        QCOMPARE(at(p, 110, 54), 'r');
        p = render(warp({{QStringLiteral("Position (3D)"), QJsonObject{{QStringLiteral("Y"), 540}}}}));
        QCOMPARE(at(p, 50, 100), 'k'); // moved up: the bottom shows the background
        // Rotation Z 180: upside down and left to right.
        p = render(warp({{QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Z"), 180}}}}));
        QCOMPARE(at(p, 10, 50), 'g');
        QCOMPARE(at(p, 100, 105), 'b');
        // Rotation Y 90: edge on, nothing seen.
        p = render(warp({{QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Y"), 90}}}}));
        QCOMPARE(at(p, 30, 54), 'k');
        QCOMPARE(at(p, 160, 54), 'k');
        // Rotation Y 45 with perspective: the near edge grows taller than the far one.
        p = render(warp({{QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Y"), 45}}}}));
        int left = 0, right = 0;
        for (int y = 0; y < kH; ++y) {
            if (at(p, 45, y) != 'k') ++left;
            if (at(p, 147, y) != 'k') ++right;
        }
        QVERIFY(left != right);
        // Crop (3D) T 0: the top half gone (p.572).
        p = render(warp({{QStringLiteral("Crop (3D)"), QJsonObject{{QStringLiteral("T"), 0}}}}));
        QCOMPARE(at(p, 10, 20), 'k');
        QCOMPARE(at(p, 10, 80), 'r');
        // Target Size 50 shrinks the whole result (post-transformation, p.569).
        p = render(warp({{QStringLiteral("Target"), QJsonObject{{QStringLiteral("Size"), 50}}}}));
        QCOMPARE(at(p, 5, 54), 'k');
        QCOMPARE(at(p, 60, 54), 'r');
    }

    void cornerPinShadowBorder() {
        // Corner Pin: the picture's corners pinned to the right half of the screen (p.571).
        Picture p = render(warp({{QStringLiteral("Corner Pin"),
                                  QJsonObject{{QStringLiteral("Corner 1: X"), 0}, {QStringLiteral("Corner 4: X"), 0}}}}));
        QCOMPARE(at(p, 40, 54), 'k');
        QCOMPARE(at(p, 110, 54), 'r');
        QCOMPARE(at(p, 170, 54), 'g');
        // Shadow, down and to the right of a half-size picture (p.579).
        p = render(warp({{QStringLiteral("Scaling (3D)"), QJsonObject{{QStringLiteral("X"), 50}, {QStringLiteral("Y"), 50}}},
                         {QStringLiteral("Shadow"), QJsonObject{{QStringLiteral("Opac."), 100}}}}));
        const float* s = p.pixel(145, 82); // just past the picture's bottom right corner (2 pixels of shadow here)
        QVERIFY(s[0] < 0.1f && s[1] < 0.1f);
        QCOMPARE(at(p, 30, 20), 'k'); // the other side untouched
        // Border (3D): a black band inside the edges (p.570-571).
        p = render(warp({{QStringLiteral("Border (3D)"), QJsonObject{{QStringLiteral("Wid"), 100}}}}));
        const float* b = p.pixel(1, 54);
        QVERIFY(b[0] < 0.1f && b[1] < 0.1f && b[2] < 0.1f);
        QCOMPARE(at(p, 30, 54), 'r');
        // Opacity and Swap Sources (p.574).
        p = render(warp({{QStringLiteral("Foreground (3D)"), QJsonObject{{QStringLiteral("Swap Sources"), true}}}}));
        QCOMPARE(at(p, 10, 50), 'k');
        p = render(warp({{QStringLiteral("Foreground (3D)"), QJsonObject{{QStringLiteral("Opac."), 0}}}}));
        QCOMPARE(at(p, 10, 50), 'k');
    }

    void shapes() {
        // Page Fold, Angle 90 ("the right edge", p.583), half curled: the right side is folded over onto the
        // left, so the left shows the flap (the green right half, mirrored) and the right the background.
        auto shape = [](const QJsonObject& o) { return warp({{QStringLiteral("Shape"), o}}); };
        Picture p = render(shape({{QStringLiteral("Type"), QStringLiteral("Page Fold")}, {QStringLiteral("Angle"), 90},
                                  {QStringLiteral("Curl"), 30}, {QStringLiteral("Radius"), 1}}));
        QCOMPARE(at(p, 185, 54), 'k');
        QCOMPARE(at(p, 10, 54), 'r');   // far from the fold: the page as it was
        // Curl 0: no rollup (p.581); Curl 100: off the screen.
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Page Fold")}, {QStringLiteral("Curl"), 0}}));
        QCOMPARE(at(p, 10, 54), 'r');
        QCOMPARE(at(p, 185, 54), 'g');
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Page Fold")}, {QStringLiteral("Curl"), 100}}));
        QCOMPARE(at(p, 10, 54), 'k');
        QCOMPARE(at(p, 96, 54), 'k');
        // Show Top Only hides the flap: the folded part leaves the background.
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Page Fold")}, {QStringLiteral("Angle"), 90},
                          {QStringLiteral("Curl"), 30}, {QStringLiteral("Tuck and Flap"), QStringLiteral("Show Top Only")}}));
        QCOMPARE(at(p, 185, 54), 'k');
        // Ball with Curve 0, "a flat circle" (p.581): a small radius leaves the corners empty.
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Ball")},
                          {QStringLiteral("Additional Parameters for the Ball Shape: Radius"), 30}, {QStringLiteral("Curve"), 0}}));
        QCOMPARE(at(p, 2, 2), 'k');
        QCOMPARE(at(p, 190, 105), 'k');
        QVERIFY(at(p, 96, 54) != 'k');
        // Slats with Ampl: alternating bands move opposite ways (p.581-582); Ampl 0 leaves the picture.
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Slats")}, {QStringLiteral("Ampl"), 0}}));
        QCOMPARE(at(p, 10, 54), 'r');
        p = render(shape({{QStringLiteral("Type"), QStringLiteral("Slats")}, {QStringLiteral("Ampl"), 30}, {QStringLiteral("Freq"), 2}}));
        // Two horizontal bands moving left and right: one side of each band shows the background.
        const char topLeft = at(p, 10, 30), bottomLeft = at(p, 10, 80);
        QVERIFY(topLeft != bottomLeft);
        // The Xpress 3D effects name their shape: 3D Page Fold at the end of a segment is off the screen.
        const Picture end = warp3dPicture(model::Effect{QStringLiteral("Xpress 3D Effect"), QStringLiteral("3D Page Fold"), {}},
                                          foreground(), grey(), 1.0, model::EffectSpan(10));
        QCOMPARE(at(end, 96, 54), 'k');
        const Picture start = warp3dPicture(model::Effect{QStringLiteral("Xpress 3D Effect"), QStringLiteral("3D Page Fold"), {}},
                                            foreground(), grey(), 0.0, model::EffectSpan(10));
        QCOMPARE(at(start, 10, 54), 'r');
    }

    void keysAndBorders() {
        auto fgParams = [](const QJsonObject& o) { return warp({{QStringLiteral("Foreground (3D)"), o}}); };
        // Luma Key: "All brightness values above the Thresh value display" (p.575): the green half (bright)
        // stays, the red half and blue band (dark) let the background through.
        Picture p = render(fgParams({{QStringLiteral("Type"), QStringLiteral("Luma Key")}}));
        QCOMPARE(at(p, 180, 54), 'g');
        QCOMPARE(at(p, 10, 54), 'k');
        // Chroma Key takes out its colour, blue until set (p.575).
        p = render(fgParams({{QStringLiteral("Type"), QStringLiteral("Chroma Key")}}));
        QCOMPARE(at(p, 100, 2), 'k');
        QCOMPARE(at(p, 10, 54), 'r');
        // Invert turns the key around.
        p = render(fgParams({{QStringLiteral("Type"), QStringLiteral("Chroma Key")}, {QStringLiteral("Invert"), true}}));
        QCOMPARE(at(p, 100, 2), 'b');
        QCOMPARE(at(p, 10, 54), 'k');
        // Show Alpha: the key as grey levels (p.574).
        p = render(fgParams({{QStringLiteral("Type"), QStringLiteral("Chroma Key")}, {QStringLiteral("Show Alpha"), true}}));
        const float* white = p.pixel(10, 54);
        const float* black = p.pixel(100, 2);
        QVERIFY(white[0] > 0.9f && white[1] > 0.9f);
        QVERIFY(black[0] < 0.1f && black[1] < 0.1f);
        // Border types (p.570-571): Bevel 1 lit along the top and left, dark along the bottom and right.
        p = render(warp({{QStringLiteral("Border (3D)"), QJsonObject{{QStringLiteral("Wid"), 100}, {QStringLiteral("Type"), QStringLiteral("Bevel 1")}}}}));
        QVERIFY(p.pixel(1, 54)[0] > 0.7f);
        QVERIFY(p.pixel(190, 54)[0] < 0.4f);
        QVERIFY(p.pixel(190, 54)[0] > 0.2f);
    }

    void promotion() {
        // "if you have a 2D Picture-in-Picture effect, you can promote the effect to 3D" (p.289): it looks the same.
        model::Effect pip{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
        pip.parameters.insert(QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 400}, {QStringLiteral("Y"), -200}});
        pip.parameters.insert(QStringLiteral("Crop"), QJsonObject{{QStringLiteral("T"), 200}});
        model::ParameterKeyframes keys;
        keys.keys = {model::EffectKeyframe{0.0, 100}, model::EffectKeyframe{1.0, -100}};
        model::setKeyframes(pip, QStringLiteral("Position"), QStringLiteral("Y"), keys);
        QVERIFY(model::canPromoteTo3D(pip));
        const auto promoted = model::promoteTo3D(pip);
        QVERIFY(promoted);
        QCOMPARE(promoted->name, QStringLiteral("3D Warp"));
        QCOMPARE(promoted->parameters.value(QStringLiteral("Scaling (3D)")).toObject().value(QStringLiteral("X")).toInt(), 50);
        QCOMPARE(promoted->parameters.value(QStringLiteral("Position (3D)")).toObject().value(QStringLiteral("Y")).toInt(), 200);
        QCOMPARE(promoted->parameters.value(QStringLiteral("Crop (3D)")).toObject().value(QStringLiteral("T")).toInt(), 599);
        QCOMPARE(model::keyframesOf(*promoted, QStringLiteral("Position (3D)"), QStringLiteral("Y")).keys.at(0).value.toInt(), -100);
        // The same picture, 2D and 3D, at a frame without the keyframes.
        model::Effect still = pip;
        model::setKeyframes(still, QStringLiteral("Position"), QStringLiteral("Y"), {});
        const Picture flat = over(imageEffectPicture(still, foreground(), 0.5, model::EffectSpan(10)), grey());
        const Picture deep = warp3dPicture(*model::promoteTo3D(still), foreground(), grey(), 0.5, model::EffectSpan(10));
        int differ = 0;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) differ += at(flat, x, y) != at(deep, x, y) ? 1 : 0;
        }
        QVERIFY2(differ < kW * kH / 50, qPrintable(QString::number(differ))); // edges may round differently
        QVERIFY(!model::canPromoteTo3D(*promoted)); // "You cannot demote" nor promote again
    }
};

QTEST_GUILESS_MAIN(TestWarp3d)
#include "tst_warp3d.moc"
