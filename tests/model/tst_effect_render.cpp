// Rendering's model (FX Guide p.57-61; docs/spec/rendering.md): which effects can be rendered, render
// keys going out of date, renders kept through JSON, Clear Renders with its two options, and renders per proxy mode
// (Editing Guide p.527, p.550; 8.5e2).
#include "lumenis/model/effect_render.h"

#include <QJsonArray>
#include <QTest>

using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

Effect effect(const QString& name, QJsonObject params = {}) {
    return Effect{QStringLiteral("Blend"), name, std::move(params)};
}

// V1: a (0-50), b (50-100) with a 10-frame Dissolve centred on the cut; V2: filler then c (20-60)
// with a Picture-in-Picture.
Sequence sample() {
    Sequence s = Sequence::withDefaultTracks();
    s.ensureTrack(QStringLiteral("V2"));
    Track* v1 = s.track(QStringLiteral("V1"));
    v1->segments = {clip(QStringLiteral("a"), 0, 50), clip(QStringLiteral("b"), 100, 50)};
    Transition t;
    t.effect = effect(QStringLiteral("Dissolve"));
    t.duration = 10;
    v1->segments[1].transition = t;
    Track* v2 = s.track(QStringLiteral("V2"));
    Segment c = clip(QStringLiteral("c"), 0, 40);
    c.effect = effect(QStringLiteral("Picture-in-Picture"));
    v2->segments = {Segment::filler(20), c};
    return s;
}

// A precompute clip `frames` long.
QJsonObject precompute(std::int64_t frames) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::RenderedEffect;
    item.name = QStringLiteral("precompute");
    item.editRate = lumenis::Rational(25);
    item.durationFrames = frames;
    return binItemToJson(item);
}

EffectRender rendered(const Sequence& s, const RenderTarget& t) {
    return EffectRender{{precompute(t.length)}, renderKey(s, t), 0, {}};
}

} // namespace

class TestEffectRender : public QObject {
    Q_OBJECT

private slots:
    void targets() {
        const Sequence s = sample();
        const QStringList both{QStringLiteral("V1"), QStringLiteral("V2")};
        const QList<RenderTarget> all = renderTargets(s, both, 0, 100);
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.at(0), (RenderTarget{QStringLiteral("V1"), 1, true, 45, 10}));
        QCOMPARE(all.at(1), (RenderTarget{QStringLiteral("V2"), 1, false, 20, 40}));
        // Only the tracks asked for, and only what overlaps.
        QCOMPARE(renderTargets(s, {QStringLiteral("V1")}, 0, 100).size(), 1);
        QCOMPARE(renderTargets(s, both, 0, 20).size(), 0);
        QCOMPARE(renderTargets(s, both, 44, 46).size(), 2);
        // At a frame: the transition where it is, else the segment's effect.
        QCOMPARE(renderTargetAt(s, QStringLiteral("V1"), 47)->transition, true);
        QCOMPARE(renderTargetAt(s, QStringLiteral("V1"), 52)->segment, 1);
        QVERIFY(!renderTargetAt(s, QStringLiteral("V1"), 30));
        QCOMPARE(renderTargetAt(s, QStringLiteral("V2"), 30)->transition, false);
        QVERIFY(!renderTargetAt(s, QStringLiteral("A1"), 30));
    }

    void keys() {
        Sequence s = sample();
        const RenderTarget pip = *renderTargetAt(s, QStringLiteral("V2"), 30);
        QVERIFY(setRender(s, pip, rendered(s, pip)));
        QVERIFY(isRendered(s, pip));
        const QString key = renderKey(s, pip);
        // A parameter changed: out of date.
        Sequence changed = s;
        changed.track(QStringLiteral("V2"))->segments[1].effect->parameters =
            QJsonObject{{QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 80}}}};
        QVERIFY(!isRendered(changed, pip));
        // A different clip under it on V1: out of date (the render holds the tracks below).
        Sequence below = s;
        below.track(QStringLiteral("V1"))->segments[0].sourceIn = 5;
        QVERIFY(!isRendered(below, pip));
        // A track above it does not matter.
        Sequence above = s;
        above.ensureTrack(QStringLiteral("V3"))->segments = {clip(QStringLiteral("d"), 0, 100)};
        QVERIFY(isRendered(above, pip));
        // The whole picture moved later together keeps the key.
        Sequence moved = s;
        moved.track(QStringLiteral("V1"))->segments.prepend(Segment::filler(10));
        moved.track(QStringLiteral("V2"))->segments[0].length += 10;
        const RenderTarget later = *renderTargetAt(moved, QStringLiteral("V2"), 40);
        QCOMPARE(later.start, 30);
        QCOMPARE(renderKey(moved, later), key);
        QVERIFY(isRendered(moved, later));
    }

    void keptInJson() {
        Sequence s = sample();
        for (const RenderTarget& t : renderTargets(s, {QStringLiteral("V1"), QStringLiteral("V2")}, 0, 100)) {
            QVERIFY(setRender(s, t, rendered(s, t)));
        }
        const Sequence back = Sequence::fromJson(s.toJson());
        QCOMPARE(back.toJson(), s.toJson());
        for (const RenderTarget& t : renderTargets(back, {QStringLiteral("V1"), QStringLiteral("V2")}, 0, 100)) {
            QVERIFY(isRendered(back, t));
        }
    }

    // "If you change the proxy mode, any previously rendered media at that proxy mode will not be available for playback
    // until you return to that proxy mode" (p.526): the mode is in the key, and the renders of other modes are kept.
    void proxyModes() {
        Sequence s = sample();
        const RenderTarget pip = renderTargets(s, {QStringLiteral("V2")}, 0, 100).front();
        const QString full = renderKey(s, pip);
        QVERIFY(setRender(s, pip, rendered(s, pip)));
        setRenderProxy(4);
        QCOMPARE(renderProxy(), 4);
        QVERIFY(renderKey(s, pip) != full);
        QVERIFY(!isRendered(s, pip));
        EffectRender quarter = rendered(s, pip);
        quarter.proxy = 4;
        QVERIFY(setRender(s, pip, quarter));
        QVERIFY(isRendered(s, pip));
        QCOMPARE(s.track(QStringLiteral("V2"))->segments[1].render->kept.size(), 1);
        // Back to none: the first render plays again, and its key is as before.
        setRenderProxy(0);
        QCOMPARE(renderKey(s, pip), full);
        QVERIFY(isRendered(s, pip));
        QCOMPARE(renderOf(s, pip)->proxy, 0);
        // Through JSON.
        const Sequence back = Sequence::fromJson(s.toJson());
        QVERIFY(isRendered(back, pip));
        setRenderProxy(4);
        QVERIFY(isRendered(back, pip));
        QCOMPARE(renderOf(back, pip)->proxy, 4);
        // A new render at 1/4 replaces the 1/4 one and keeps the full one once.
        QVERIFY(setRender(s, pip, quarter));
        QCOMPARE(s.track(QStringLiteral("V2"))->segments[1].render->kept.size(), 1);
        // Removing drops them all.
        QVERIFY(setRender(s, pip, std::nullopt));
        setRenderProxy(0);
        QVERIFY(!isRendered(s, pip));
        setRenderProxy(7);
        QCOMPARE(renderProxy(), 0);
    }

    void partial() {
        // A render shorter than its effect is partial (p.150); a second precompute finishes it, the two
        // laid end to end.
        Sequence s = sample();
        const RenderTarget pip = *renderTargetAt(s, QStringLiteral("V2"), 30); // 40 frames
        QVERIFY(setRender(s, pip, EffectRender{{precompute(15)}, renderKey(s, pip), 0, {}}));
        QVERIFY(!isRendered(s, pip));
        QVERIFY(isPartlyRendered(s, pip));
        QVERIFY(isRenderedAt(s, pip, 14));
        QVERIFY(!isRenderedAt(s, pip, 15));
        EffectRender whole = *renderOf(s, pip);
        whole.clips << precompute(25);
        QCOMPARE(renderedFrames(whole), 40);
        QCOMPARE(renderedClipAt(whole, 20)->second, 5);
        QVERIFY(!renderedClipAt(whole, 40));
        QVERIFY(setRender(s, pip, whole));
        QVERIFY(isRendered(s, pip));
        QVERIFY(!isPartlyRendered(s, pip));
        QCOMPARE(Sequence::fromJson(s.toJson()).toJson(), s.toJson());
        // 6.4a's single {clip} still reads.
        QJsonObject json = s.toJson();
        QJsonArray tracks = json.value(QStringLiteral("tracks")).toArray();
        for (int i = 0; i < tracks.size(); ++i) {
            QJsonObject t = tracks.at(i).toObject();
            if (t.value(QStringLiteral("name")).toString() != QLatin1String("V2")) continue;
            QJsonArray segments = t.value(QStringLiteral("segments")).toArray();
            QJsonObject seg = segments.at(1).toObject();
            seg.insert(QStringLiteral("render"), QJsonObject{{QStringLiteral("clip"), precompute(40)}, {QStringLiteral("key"), renderKey(s, pip)}});
            segments.replace(1, seg);
            t.insert(QStringLiteral("segments"), segments);
            tracks.replace(i, t);
        }
        json.insert(QStringLiteral("tracks"), tracks);
        QVERIFY(isRendered(Sequence::fromJson(json), pip));
    }

    void expert() {
        // ExpertRender (p.140-147). sample(): V1 a dissolve at 45-55, V2 a PIP over 20-60.
        Sequence s = sample();
        const QStringList both{QStringLiteral("V1"), QStringLiteral("V2")};
        ExpertRender options;
        // Two real-time effects stacked play in real time: nothing to render.
        QVERIFY(expertRender(s, both, 0, 100, options).isEmpty());
        // A third over 45-55 (on V3) is too many there: its own effect, the topmost, takes them all in.
        Track* v3 = s.ensureTrack(QStringLiteral("V3"));
        Segment d = clip(QStringLiteral("d"), 0, 10);
        d.effect = effect(QStringLiteral("Flop"));
        v3->segments = {Segment::filler(45), d};
        const QStringList all{QStringLiteral("V1"), QStringLiteral("V2"), QStringLiteral("V3")};
        QList<RenderTarget> got = expertRender(s, all, 0, 100, options);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.at(0).track, QStringLiteral("V3"));
        // Only V1 and V2 selected: V3 is above them and not looked at.
        QVERIFY(expertRender(s, both, 0, 100, options).isEmpty());
        // A non-real-time effect needs rendering on its own: the PIP, say.
        options.realTime = [](const Effect& e) { return e.name != QLatin1String("Picture-in-Picture"); };
        got = expertRender(s, both, 0, 100, options);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.at(0).track, QStringLiteral("V2"));
        // ... unless V2 is not selected: V1's dissolve is not slow, and the PIP is not V1's to render.
        QVERIFY(expertRender(s, {QStringLiteral("V1")}, 0, 100, options).isEmpty());
        // Recommended ranges only: none there, nothing.
        options.mode = ExpertMode::RecommendedRanges;
        options.recommended = {{70, 80}};
        QVERIFY(expertRender(s, both, 0, 100, options).isEmpty());
        options.recommended = {{25, 30}};
        QCOMPARE(expertRender(s, both, 0, 100, options).size(), 1);
        // Prepare for Digital Cut: every unrendered effect, the top one where they stack.
        options = ExpertRender{};
        options.mode = ExpertMode::DigitalCut;
        got = expertRender(s, both, 0, 100, options);
        QCOMPARE(got.size(), 1); // the PIP over 20-60 takes in the dissolve beneath it
        QCOMPARE(got.at(0).track, QStringLiteral("V2"));
        got = expertRender(s, {QStringLiteral("V1")}, 0, 100, options);
        QCOMPARE(got.size(), 1);
        QVERIFY(got.at(0).transition);
        // Rendered effects are not recommended again.
        const RenderTarget pip = got.at(0);
        QVERIFY(setRender(s, pip, rendered(s, pip)));
        QVERIFY(expertRender(s, {QStringLiteral("V1")}, 0, 100, options).isEmpty());
    }

    void clear() {
        Sequence s = sample();
        // A motion effect on V2 instead of the PIP, rendered too.
        s.track(QStringLiteral("V2"))->segments[1].effect = motionEffect(0.5);
        const QList<RenderTarget> all = renderTargets(s, {QStringLiteral("V1"), QStringLiteral("V2")}, 0, 100);
        for (const RenderTarget& t : all) QVERIFY(setRender(s, t, rendered(s, t)));
        // "Skip Motion Effects" keeps the motion effect's render (p.60).
        Sequence a = s;
        QCOMPARE(clearRenders(a, all, ClearRenders{}), 1);
        QVERIFY(!renderOf(a, all.at(0)));
        QVERIFY(renderOf(a, all.at(1)));
        // Without it both go; the precompute is not the sequence's to delete.
        Sequence b = s;
        QCOMPARE(clearRenders(b, all, ClearRenders{false, true, {}}), 2);
        // "Skip Unknown Effects" keeps effects that are not installed.
        Sequence c = s;
        const ClearRenders unknown{false, true, [](const Effect& e) { return e.name != QLatin1String("Dissolve"); }};
        QCOMPARE(clearRenders(c, all, unknown), 1);
        QVERIFY(renderOf(c, all.at(0)));
    }
};

QTEST_GUILESS_MAIN(TestEffectRender)
#include "tst_effect_render.moc"
