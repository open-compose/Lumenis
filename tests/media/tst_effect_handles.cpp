// Direct manipulation's geometry (FX Guide p.92-93, p.292-293; effect_handles.cpp): the outline matches what
// the compositor draws, each handle follows the pointer when dragged, the rotation handles turn the picture,
// and the motion path has a dot at each keyframe.
#include "lumenis/media/compositor.h"
#include "lumenis/media/effect_handles.h"
#include "lumenis/model/effect_keyframes.h"

#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <optional>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kW = 192;
constexpr int kH = 108;
const QSizeF kSize(kW, kH);
const model::EffectSpan kSpan(25);

model::Effect pip() {
    return model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
}

model::Effect warp(const QJsonObject& parameters = {}) {
    model::Effect e{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
    for (auto it = parameters.begin(); it != parameters.end(); ++it) e.parameters.insert(it.key(), it.value());
    return e;
}

// `e` with the dragged values set, as the Effect Editor stores them.
model::Effect applied(model::Effect e, const QList<HandleValue>& values) {
    for (const HandleValue& v : values) {
        QJsonObject group = e.parameters.value(v.category).toObject();
        group.insert(v.name, v.value);
        e.parameters.insert(v.category, group);
    }
    return e;
}

double distance(const QPointF& a, const QPointF& b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

QPointF handle(const model::Effect& e, HandleTool tool, const QString& id) {
    for (const EffectHandle& h : effectHandles(e, tool, 0.5, kSpan, kSize)) {
        if (h.id == id) return h.at;
    }
    return {-1e9, -1e9};
}

// Set when a handle dragged by dragged() did not land under the pointer.
bool gMissed = false;

// Drags handle `id` by `by` and checks that it lands there.
model::Effect dragged(const model::Effect& e, HandleTool tool, const QString& id, const QPointF& by, double tolerance = 0.05) {
    const QPointF from = handle(e, tool, id);
    const QList<HandleValue> values = dragEffectHandle(e, tool, id, from, from + by, 0.5, kSpan, kSize);
    const model::Effect after = applied(e, values);
    const double off = distance(handle(after, tool, id), from + by);
    if (off > tolerance) {
        qWarning() << id << "missed by" << off << "after" << values.size() << "values";
        gMissed = true;
    }
    return after;
}

double value(const model::Effect& e, const char* category, const char* name) {
    return e.parameters.value(QLatin1String(category)).toObject().value(QLatin1String(name)).toDouble();
}

} // namespace

class TestEffectHandles : public QObject {
    Q_OBJECT

private slots:
    // A Picture-in-Picture at its half size: the outline, its handles, and what the pointer is over.
    void pipOutline() {
        QVERIFY(hasEffectOutline(pip()));
        QVERIFY(!hasEffectOutline(model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}));
        const auto o = effectOutline(pip(), 0.5, kSpan, kSize);
        QVERIFY(o);
        QCOMPARE(o->corners[0], QPointF(48, 27));
        QCOMPARE(o->corners[2], QPointF(144, 81));
        QCOMPARE(o->center, QPointF(96, 54));
        const QList<EffectHandle> handles = effectHandles(pip(), HandleTool::None, 0.5, kSpan, kSize);
        QCOMPARE(handles.size(), 4);
        QCOMPARE(handle(pip(), HandleTool::None, QStringLiteral("top")), QPointF(96, 27));
        QCOMPARE(handle(pip(), HandleTool::None, QStringLiteral("side")), QPointF(144, 54));
        QCOMPARE(handle(pip(), HandleTool::None, QStringLiteral("corner")), QPointF(144, 27));
        QCOMPARE(effectHandleAt(pip(), HandleTool::None, 0.5, kSpan, kSize, QPointF(143, 28), 5), QStringLiteral("corner"));
        QCOMPARE(effectHandleAt(pip(), HandleTool::None, 0.5, kSpan, kSize, QPointF(70, 70), 5), QStringLiteral("move"));
        QCOMPARE(effectHandleAt(pip(), HandleTool::None, 0.5, kSpan, kSize, QPointF(10, 10), 5), QString());
    }

    // "Drag the wire frame to reposition the effect. Drag the resize handles to resize the image" (p.92).
    void pipDrags() {
        const model::Effect moved = dragged(pip(), HandleTool::None, QStringLiteral("move"), QPointF(10, 5));
        QCOMPARE(qRound(value(moved, "Position", "X")), 100); // pixels of a 1920-wide picture
        QCOMPARE(qRound(value(moved, "Position", "Y")), 50);
        const model::Effect wider = dragged(pip(), HandleTool::None, QStringLiteral("side"), QPointF(12, 0));
        QVERIFY(std::abs(value(wider, "Scaling", "X") - 62.5) < 0.01);
        QVERIFY(!wider.parameters.value(QStringLiteral("Scaling")).toObject().contains(QStringLiteral("Y")));
        const model::Effect taller = dragged(pip(), HandleTool::None, QStringLiteral("top"), QPointF(0, -9));
        QVERIFY(std::abs(value(taller, "Scaling", "Y") - 66.67) < 0.01);
        QVERIFY(!gMissed);
        // The corner keeps the aspect ratio: both scale by the same amount (the pointer is met as closely as
        // that allows).
        const QPointF corner = handle(pip(), HandleTool::None, QStringLiteral("corner"));
        const model::Effect both = applied(pip(), dragEffectHandle(pip(), HandleTool::None, QStringLiteral("corner"), corner,
                                                                   corner + QPointF(12, -6.75), 0.5, kSpan, kSize));
        QVERIFY(std::abs(value(both, "Scaling", "X") - 62.5) < 0.01);
        QVERIFY(std::abs(value(both, "Scaling", "Y") - 62.5) < 0.01);
    }

    // A keyframed parameter: the drag starts from its value at the position, and the result is a plain value
    // (the Effect Editor puts it on the keyframes).
    void keyframedStart() {
        model::Effect e = pip();
        model::setKeyframe(e, QStringLiteral("Position"), QStringLiteral("X"), 0.0, kSpan, -400);
        model::setKeyframe(e, QStringLiteral("Position"), QStringLiteral("X"), 1.0, kSpan, 400);
        const QPointF from = handle(e, HandleTool::None, QStringLiteral("move"));
        const QList<HandleValue> values = dragEffectHandle(e, HandleTool::None, QStringLiteral("move"), from, from + QPointF(1, 0), 0.5, kSpan, kSize);
        QCOMPARE(values.size(), 2);
        QVERIFY(std::abs(values.at(0).value - (model::numberAt(e, QStringLiteral("Position"), QStringLiteral("X"), 0.5, kSpan).value_or(0) + 10)) < 0.01);
    }

    // The 3D outline is where warp3dPicture draws the picture, through Position, Rotation and Perspective.
    void outline3DMatchesPicture() {
        model::Effect e = warp({{QStringLiteral("Position (3D)"), QJsonObject{{QStringLiteral("X"), 150}, {QStringLiteral("Z"), -900}}},
                                {QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Y"), 35}, {QStringLiteral("Z"), 10}}}});
        const auto untouched = effectOutline(warp(), 0.5, kSpan, kSize);
        QVERIFY(untouched);
        QVERIFY(distance(untouched->corners[0], QPointF(0, 0)) < 1e-6);
        QVERIFY(distance(untouched->corners[2], QPointF(kW, kH)) < 1e-6);
        const auto o = effectOutline(e, 0.5, kSpan, kSize);
        QVERIFY(o);
        const Picture picture = warp3dPicture(e, Picture::solid(kW, kH, Qt::red), Picture::solid(kW, kH, Qt::black), 0.5, kSpan);
        // Just inside each corner the picture; just outside it the background (where they are on the screen).
        auto red = [&](const QPointF& p) -> std::optional<bool> {
            if (p.x() < 0 || p.y() < 0 || p.x() >= kW || p.y() >= kH) return std::nullopt;
            return picture.pixel(static_cast<int>(p.x()), static_cast<int>(p.y()))[0] > 0.5f;
        };
        int seen = 0;
        for (const QPointF& c : o->corners) {
            const auto inside = red(c + (o->center - c) * 0.1);
            const auto outside = red(c - (o->center - c) * 0.1);
            if (inside) QVERIFY(*inside);
            if (outside) QVERIFY(!*outside);
            seen += (inside ? 1 : 0) + (outside ? 1 : 0);
        }
        QVERIFY(seen >= 4);
    }

    // Each 3D handle follows the pointer, also on a turned picture seen in perspective (p.292-293).
    void handlesFollow3D() {
        const model::Effect e = warp({{QStringLiteral("Position (3D)"), QJsonObject{{QStringLiteral("Z"), -500}}},
                                      {QStringLiteral("Scaling (3D)"), QJsonObject{{QStringLiteral("X"), 60}, {QStringLiteral("Y"), 60}}},
                                      {QStringLiteral("Rotation"), QJsonObject{{QStringLiteral("Y"), 20}}}});
        QVERIFY(effectHandles(e, HandleTool::None, 0.5, kSpan, kSize).isEmpty());
        QCOMPARE(effectHandles(e, HandleTool::CornerPin, 0.5, kSpan, kSize).size(), 4);
        QCOMPARE(effectHandles(e, HandleTool::XRotation, 0.5, kSpan, kSize).size(), 2);
        QVERIFY(effectHandles(e, HandleTool::ZRotation, 0.5, kSpan, kSize).first().from);

        const model::Effect moved = dragged(e, HandleTool::XYPosition, QStringLiteral("move"), QPointF(20, -10));
        QVERIFY(value(moved, "Position (3D)", "Y") > 0.0); // up is positive Y (p.577)
        dragged(e, HandleTool::Scale, QStringLiteral("side"), QPointF(8, 0), 0.6); // a turned plane: the nearest it can
        dragged(e, HandleTool::Scale, QStringLiteral("top"), QPointF(0, -6));
        dragged(e, HandleTool::Crop, QStringLiteral("crop:T"), QPointF(0, 5));
        dragged(e, HandleTool::Axis, QStringLiteral("axis"), QPointF(-15, 7));

        // Corner Pin: the first drag pins all four corners where they were; the dragged one moves.
        const QPointF c2 = handle(e, HandleTool::CornerPin, QStringLiteral("corner2"));
        const QPointF c4 = handle(e, HandleTool::CornerPin, QStringLiteral("corner4"));
        const QList<HandleValue> pinned = dragEffectHandle(e, HandleTool::CornerPin, QStringLiteral("corner2"), c2, c2 + QPointF(10, 10), 0.5, kSpan, kSize);
        QCOMPARE(pinned.size(), 8);
        const model::Effect p = applied(e, pinned);
        QVERIFY(distance(handle(p, HandleTool::CornerPin, QStringLiteral("corner2")), c2 + QPointF(10, 10)) < 0.1);
        QVERIFY(distance(handle(p, HandleTool::CornerPin, QStringLiteral("corner4")), c4) < 0.1);
        QVERIFY(!gMissed);
        QCOMPARE(dragEffectHandle(p, HandleTool::CornerPin, QStringLiteral("corner4"), c4, c4, 0.5, kSpan, kSize).size(), 2);
    }

    void rotations() {
        const model::Effect e = warp();
        const QPointF centre(kW / 2.0, kH / 2.0);
        // Z: the angle swept about the centre, clockwise positive.
        const auto z = dragEffectHandle(e, HandleTool::ZRotation, QStringLiteral("rotZ"), centre + QPointF(20, 0), centre + QPointF(0, 20), 0.5, kSpan, kSize);
        QCOMPARE(z.size(), 1);
        QCOMPARE(z.first().name, QStringLiteral("Z"));
        QVERIFY(std::abs(z.first().value - 90.0) < 1e-6);
        // X: half a turn over the picture's height.
        const auto x = dragEffectHandle(e, HandleTool::XRotation, QStringLiteral("rotX:top"), QPointF(96, 0), QPointF(96, kH / 4.0), 0.5, kSpan, kSize);
        QVERIFY(std::abs(x.first().value - 45.0) < 1e-6);
        const auto xb = dragEffectHandle(e, HandleTool::XRotation, QStringLiteral("rotX:bottom"), QPointF(96, kH), QPointF(96, kH * 0.75), 0.5, kSpan, kSize);
        QVERIFY(std::abs(xb.first().value - 45.0) < 1e-6);
        const auto y = dragEffectHandle(e, HandleTool::YRotation, QStringLiteral("rotY:right"), QPointF(kW, 54), QPointF(kW * 0.75, 54), 0.5, kSpan, kSize);
        QVERIFY(std::abs(y.first().value - 45.0) < 1e-6);
        // XZ Position: down brings the picture closer.
        const auto xz = dragEffectHandle(e, HandleTool::XZPosition, QStringLiteral("move"), centre, centre + QPointF(0, 10), 0.5, kSpan, kSize);
        QCOMPARE(xz.size(), 2);
        QVERIFY(xz.at(1).value > 0.0);
    }

    // "Each dot on the path represents the location of the center of the PIP at a keyframe" (p.93).
    void path() {
        model::Effect e = pip();
        QVERIFY(effectPath(e, kSpan, kSize).line.isEmpty());
        model::setKeyframe(e, QStringLiteral("Position"), QStringLiteral("X"), 0.0, kSpan, -200);
        model::setKeyframe(e, QStringLiteral("Position"), QStringLiteral("X"), 1.0, kSpan, 200);
        const EffectPath p = effectPath(e, kSpan, kSize);
        QCOMPARE(p.dots.size(), 2);
        QCOMPARE(p.dots.first(), QPointF(76, 54));
        QCOMPARE(p.dots.last(), QPointF(116, 54));
        QCOMPARE(p.line.size(), 25);
    }
};

QTEST_MAIN(TestEffectHandles)
#include "tst_effect_handles.moc"
