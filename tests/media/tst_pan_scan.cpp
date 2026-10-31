// Pan and Scan (FX Guide p.230-238, p.480, p.564; pan_scan.cpp, model/pan_scan.*): where the source picture and
// the wire frame lie for each ratio, the output filled from the wire frame, the wire frame dragged along one axis
// (Shift: all) and kept on the source, its shape kept when resized, the origin, and Subdivide Effect.
#include "lumenis/media/compositor.h"
#include "lumenis/media/effect_handles.h"
#include "lumenis/model/pan_scan.h"

#include <QJsonObject>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kW = 192;
constexpr int kH = 108;
const QSizeF kSize(kW, kH);
const model::EffectSpan kSpan(25);

model::Effect panScan(const QString& source = {}, const QString& target = {}) {
    model::Effect e{QStringLiteral("Reformat"), QStringLiteral("Pan and Scan"), {}};
    QJsonObject ratios;
    if (!source.isEmpty()) ratios.insert(QStringLiteral("Source"), source);
    if (!target.isEmpty()) ratios.insert(QStringLiteral("Target"), target);
    if (!ratios.isEmpty()) e.parameters.insert(QLatin1String(model::kPanScanCategory), ratios);
    return e;
}

void set(model::Effect& e, const char* group, const char* name, double v) {
    QJsonObject g = e.parameters.value(QLatin1String(group)).toObject();
    g.insert(QLatin1String(name), v);
    e.parameters.insert(QLatin1String(group), g);
}

model::Effect applied(model::Effect e, const QList<HandleValue>& values) {
    for (const HandleValue& v : values) {
        QJsonObject g = e.parameters.value(v.category).toObject();
        g.insert(v.name, v.value);
        e.parameters.insert(v.category, g);
    }
    return e;
}

bool near(const QRectF& a, const QRectF& b) {
    return std::abs(a.left() - b.left()) < 1e-6 && std::abs(a.top() - b.top()) < 1e-6 && std::abs(a.width() - b.width()) < 1e-6 &&
           std::abs(a.height() - b.height()) < 1e-6;
}

// A picture whose red is its column and green its row (0 to 1).
Picture ramp() {
    Picture p(kW, kH);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            float* c = p.pixel(x, y);
            c[0] = (static_cast<float>(x) + 0.5f) / kW;
            c[1] = (static_cast<float>(y) + 0.5f) / kH;
            c[3] = 1.0f;
        }
    }
    return p;
}

model::Segment clip(const QString& id, std::int64_t length) {
    model::Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = 10; // room for a dissolve's overlap
    s.length = length;
    return s;
}

} // namespace

class TestPanScan : public QObject {
    Q_OBJECT

private slots:
    // Until chosen, a 16:9 source made 4:3: the wire frame is the 4:3 middle of the picture, output pillarboxed.
    void defaults() {
        const auto f = panScanFrames(panScan(), 0.0, kSpan, kSize);
        QVERIFY(f);
        QVERIFY(near(f->source, QRectF(0, 0, kW, kH)));
        QVERIFY(near(f->wire, QRectF(24, 0, 144, 108)));
        QVERIFY(near(f->target, QRectF(24, 0, 144, 108)));
        const Picture out = panScanPicture(panScan(), ramp(), 0.0, kSpan);
        // The middle is the picture as it was; the sides are black.
        QVERIFY(std::abs(out.pixel(96, 54)[0] - ramp().pixel(96, 54)[0]) < 1e-3f);
        QCOMPARE(out.pixel(5, 54)[0], 0.0f);
        QCOMPARE(out.pixel(5, 54)[3], 1.0f);
    }

    void ratios() {
        // A 2.35 source is letterboxed in the 16:9 frame; a 4:3 wire frame in it is as high as it.
        const auto wide = panScanFrames(panScan(QStringLiteral("2.35")), 0.0, kSpan, kSize);
        QVERIFY(std::abs(wide->source.height() - kW / 2.35) < 1e-6);
        QVERIFY(std::abs(wide->wire.height() - wide->source.height()) < 1e-6);
        QVERIFY(std::abs(wide->wire.width() / wide->wire.height() - 4.0 / 3.0) < 1e-6);
        // A 16:9 anamorphic target fills the frame.
        const auto squeezed = panScanFrames(panScan(QStringLiteral("1.33 (4:3)"), QStringLiteral("16:9 anamorphic")), 0.0, kSpan, kSize);
        QVERIFY(near(squeezed->target, QRectF(0, 0, kW, kH)));
        // A 4:3 source pillarboxed in the frame; the 16:9 wire frame as wide as it.
        QVERIFY(near(squeezed->source, QRectF(24, 0, 144, 108)));
        QVERIFY(std::abs(squeezed->wire.width() - 144.0) < 1e-6);
        QVERIFY(std::abs(squeezed->wire.height() - 81.0) < 1e-6);
    }

    // "When you make the wire frame smaller, the image inside the wire frame is enlarged to fit" (p.236).
    void zoom() {
        model::Effect e = panScan();
        set(e, "Scaling", "X", 50);
        set(e, "Scaling", "Y", 50);
        set(e, "Position", "X", 200); // 20 pixels of this 192-wide frame
        const auto f = panScanFrames(e, 0.0, kSpan, kSize);
        QVERIFY(near(f->wire, QRectF(96 + 20 - 36, 54 - 27, 72, 54)));
        const Picture out = panScanPicture(e, ramp(), 0.0, kSpan);
        // The output's middle shows the wire frame's middle.
        QVERIFY(std::abs(out.pixel(95, 53)[0] - (116.0f / kW)) < 0.01f);
        QVERIFY(std::abs(out.pixel(95, 53)[1] - 0.5f) < 0.02f);
    }

    // Dragging inside the wire frame: across only, from 16:9 to 4:3; Shift in all directions; never off the source
    // picture (p.236-237). The handles keep its shape.
    void wireFrame() {
        const model::Effect e = panScan();
        QVERIFY(hasEffectOutline(e));
        QCOMPARE(effectHandles(e, HandleTool::None, 0.0, kSpan, kSize).size(), 4);
        const QPointF centre(96, 54);
        const auto across = dragEffectHandle(e, HandleTool::None, QStringLiteral("move"), centre, centre + QPointF(10, 10), 0.0, kSpan, kSize);
        QCOMPARE(across.size(), 1);
        QCOMPARE(across.first().name, QStringLiteral("X"));
        QVERIFY(std::abs(across.first().value - 100.0) < 1e-6);
        // As far as the picture's edge, and no further.
        const auto far = dragEffectHandle(e, HandleTool::None, QStringLiteral("move"), centre, centre + QPointF(500, 0), 0.0, kSpan, kSize);
        QVERIFY(std::abs(far.first().value - 240.0) < 1e-6); // 24 pixels
        // Shift: both ways; the wire frame is as high as the picture, so it stays centred up and down.
        model::Effect smaller = e;
        set(smaller, "Scaling", "X", 50);
        set(smaller, "Scaling", "Y", 50);
        const auto both = dragEffectHandle(smaller, HandleTool::None, QStringLiteral("move"), centre, centre + QPointF(10, 10), 0.0, kSpan, kSize, true);
        QCOMPARE(both.size(), 2);
        QVERIFY(std::abs(both.at(1).value - 100.0) < 1e-6);
        // The side handle scales both ways.
        const auto side = effectHandles(e, HandleTool::None, 0.0, kSpan, kSize).at(2);
        QCOMPARE(side.id, QStringLiteral("side"));
        const model::Effect resized = applied(e, dragEffectHandle(e, HandleTool::None, QStringLiteral("side"), side.at, side.at - QPointF(36, 0), 0.0, kSpan, kSize));
        const auto f = panScanFrames(resized, 0.0, kSpan, kSize);
        QVERIFY(std::abs(f->wire.width() / f->wire.height() - 4.0 / 3.0) < 1e-6);
        QVERIFY(f->wire.width() < 144.0);
    }

    // Establish Origin, the origin back, and a new Target forgetting it (p.235, p.564).
    void origin() {
        model::Effect e = panScan();
        QCOMPARE(model::panScanOrigin(e), model::PanScanOrigin{});
        set(e, "Scaling", "X", 80);
        set(e, "Scaling", "Y", 80);
        set(e, "Position", "X", -30);
        model::establishOrigin(e, 0.5, kSpan);
        QCOMPARE(model::panScanOrigin(e), (model::PanScanOrigin{80, 80, -30, 0}));
        model::clearOrigin(e);
        QCOMPARE(model::panScanOrigin(e), model::PanScanOrigin{});
    }

    // Subdivide Effect: at the cuts of the selected tracks below, not at cuts with a transition (p.235-238).
    void subdivide() {
        model::Sequence s = model::Sequence::withDefaultTracks();
        QVERIFY(s.addTrack(model::TrackKind::Video));
        s.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("a"), 20), clip(QStringLiteral("b"), 20), clip(QStringLiteral("c"), 20)});
        model::Segment cover = model::Segment::filler(60);
        cover.effect = panScan();
        s.splice(QStringLiteral("V2"), 0, {cover});
        const model::Transition dissolve{model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}, 4,
                                         model::TransitionAlignment::CenteredOnCut, 0};
        QVERIFY(model::applyTransition(s, QStringLiteral("V1"), 40, dissolve));
        QVERIFY(!model::subdivideEffect(s, QStringLiteral("V2"), 10, {QStringLiteral("V2")})); // no guide below
        QVERIFY(!model::subdivideEffect(s, QStringLiteral("V1"), 10, {QStringLiteral("V1")})); // not a Pan and Scan
        QVERIFY(model::subdivideEffect(s, QStringLiteral("V2"), 10, {QStringLiteral("V1"), QStringLiteral("V2")}));
        const model::Track* v2 = s.track(QStringLiteral("V2"));
        QCOMPARE(v2->segments.size(), 2); // at 20, not at 40 (a dissolve)
        QCOMPARE(v2->segments.at(0).length, 20);
        QCOMPARE(v2->segments.at(1).length, 40);
        QVERIFY(v2->segments.at(1).effect && model::isPanAndScanEffect(*v2->segments.at(1).effect));
    }
};

QTEST_MAIN(TestPanScan)
#include "tst_pan_scan.moc"
