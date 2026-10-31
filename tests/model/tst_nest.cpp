// Nested effects (FX Guide p.219-227; lumenis/model/nest.h): a segment's nest, made for Step In and by
// nesting an effect inside another, kept through JSON, trims and Add Edits; Collapse into a Submaster.
#include "lumenis/model/effect_render.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

const Effect kFlop{QStringLiteral("Image"), QStringLiteral("Flop"), {}};
const Effect kPip{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};

} // namespace

class TestNest : public QObject {
    Q_OBJECT

private slots:
    void stepInMakesANest() {
        // "You can step into any segment effect" (p.220): the segment's clip goes on the nest's V1.
        Segment s = clip(QStringLiteral("a"), 10, 20);
        QVERIFY(!ensureNest(s)); // no effect: nothing to step into
        s.effect = kFlop;
        QVERIFY(ensureNest(s));
        QVERIFY(s.hasNest());
        QCOMPARE(s.effect, kFlop);
        const Sequence nest = nestOf(s);
        const Segment& inner = nest.track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(inner.clipId, QStringLiteral("a"));
        QCOMPARE(inner.sourceIn, 10);
        QCOMPARE(inner.length, 20);
        QVERIFY(!inner.effect);
        // Once made, it stays.
        QVERIFY(ensureNest(s));
        // A Submaster on filler nests an empty track.
        Segment submaster = Segment::filler(8);
        submaster.effect = Effect{QStringLiteral("Image"), QStringLiteral("Submaster"), {}};
        QVERIFY(ensureNest(submaster));
        QVERIFY(nestOf(submaster).track(QStringLiteral("V1"))->segments.at(0).isFiller());
    }

    void nestingAnEffect() {
        // Alt+drag (p.216): the new effect outside, the old one inside with the clip.
        Segment s = clip(QStringLiteral("a"), 0, 30);
        s.effect = kFlop;
        nestEffect(s, kPip);
        QCOMPARE(s.effect, kPip);
        QCOMPARE(s.clipId, QStringLiteral("a")); // the segment still names its clip
        const Segment inner = nestOf(s).track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(inner.effect, kFlop);
        QCOMPARE(inner.clipId, QStringLiteral("a"));
        // Again: three levels.
        nestEffect(s, kFlop);
        const Segment middle = nestOf(s).track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(middle.effect, kPip);
        QVERIFY(middle.hasNest());
        QCOMPARE(nestOf(middle).track(QStringLiteral("V1"))->segments.at(0).effect, kFlop);
        // Without an effect it is just applied.
        Segment plain = clip(QStringLiteral("b"), 0, 5);
        nestEffect(plain, kFlop);
        QVERIFY(!plain.hasNest());
        QCOMPARE(plain.effect, kFlop);
        // "it must be the innermost effect" (p.177): no Timewarp outside.
        Sequence seq = Sequence::withDefaultTracks();
        Segment flopped = clip(QStringLiteral("c"), 0, 10);
        flopped.effect = kFlop;
        seq.overwrite(QStringLiteral("V1"), 0, {flopped});
        QVERIFY(!applySegmentEffect(seq, QStringLiteral("V1"), 0, Effect{QStringLiteral("Timewarp"), QStringLiteral("Timewarp"), {}}, true));
        QVERIFY(applySegmentEffect(seq, QStringLiteral("V1"), 0, kPip, true));
        QVERIFY(seq.track(QStringLiteral("V1"))->segments.at(0).hasNest());
    }

    void keptThroughEdits() {
        Sequence seq = Sequence::withDefaultTracks();
        Segment s = clip(QStringLiteral("a"), 0, 30);
        s.effect = kFlop;
        nestEffect(s, kPip);
        seq.overwrite(QStringLiteral("V1"), 0, {s});
        // JSON keeps it.
        const Sequence again = Sequence::fromJson(seq.toJson());
        QCOMPARE(again.track(QStringLiteral("V1"))->segments.at(0).nest, s.nest);
        // An Add Edit: the second part shows the nest from where it starts.
        QVERIFY(seq.addEdit(QStringLiteral("V1"), 12));
        const QList<Segment>& parts = seq.track(QStringLiteral("V1"))->segments;
        QCOMPARE(parts.size(), 2);
        QCOMPARE(parts.at(0).nestIn, 0);
        QCOMPARE(parts.at(1).nestIn, 12);
        QCOMPARE(parts.at(1).sourceIn, 12);
        // Taking frames off a start moves the nest with the clip.
        Segment t = parts.at(1);
        t.shiftStart(3);
        QCOMPARE(t.nestIn, 15);
        QCOMPARE(t.sourceIn, 15);
        // Filler with a nest (a Submaster) is not merged into the filler around it.
        Sequence fill = Sequence::withDefaultTracks();
        Segment sub = Segment::filler(10);
        sub.effect = Effect{QStringLiteral("Image"), QStringLiteral("Submaster"), {}};
        ensureNest(sub);
        sub.nestIn = 4;
        fill.overwrite(QStringLiteral("V1"), 5, {sub});
        const Segment kept = fill.track(QStringLiteral("V1"))->segments.at(1);
        QVERIFY(kept.hasNest());
        QCOMPARE(kept.nestIn, 4);
        QCOMPARE(Sequence::fromJson(fill.toJson()).track(QStringLiteral("V1"))->segments.at(1).nestIn, 4);
    }

    void nestedEffectsInTheEditor() {
        // "if you have a Picture-in-Picture effect with a Color Correction effect nested inside it, you can see
        // both effects" (p.221): the outer effect first, then in.
        const Effect mosaic{QStringLiteral("Image"), QStringLiteral("Mosaic Effect"), {}};
        Segment s = clip(QStringLiteral("a"), 0, 30);
        QVERIFY(nestedEffects(s, 0).isEmpty());
        s.effect = kFlop;
        nestEffect(s, kPip);
        nestEffect(s, mosaic);
        auto names = [](const QList<NestedEffect>& chain) {
            QStringList out;
            for (const NestedEffect& e : chain) out << e.effect.name;
            return out;
        };
        QCOMPARE(names(nestedEffects(s, 5)), (QStringList{QStringLiteral("Mosaic Effect"), QStringLiteral("Picture-in-Picture"), QStringLiteral("Flop")}));
        QCOMPARE(nestedEffects(s, 5).at(2).length, 30);
        // A nested effect's parameters change where it is.
        Effect flop = kFlop;
        flop.parameters.insert(QStringLiteral("Grid"), QJsonObject{{QStringLiteral("Show Grid"), true}});
        QVERIFY(setNestedEffect(s, 5, 2, flop));
        QCOMPARE(nestedEffects(s, 5).at(2).effect, flop);
        QCOMPARE(s.effect, mosaic);
        QVERIFY(!setNestedEffect(s, 5, 3, flop));
        // "Drag the effect icon for the effect you want to move to the position you want it to occupy" (p.222).
        QString error;
        QVERIFY(moveNestedEffect(s, 5, 2, 0, &error));
        QCOMPARE(names(nestedEffects(s, 5)), (QStringList{QStringLiteral("Flop"), QStringLiteral("Mosaic Effect"), QStringLiteral("Picture-in-Picture")}));
        QCOMPARE(nestedEffects(s, 5).at(0).effect, flop); // with its parameters
        QCOMPARE(nestOf(nestOf(s).track(QStringLiteral("V1"))->segments.at(0)).track(QStringLiteral("V1"))->segments.at(0).clipId,
                 QStringLiteral("a")); // the clip stays innermost
        QVERIFY(moveNestedEffect(s, 5, 0, 1, &error));
        QCOMPARE(names(nestedEffects(s, 5)).first(), QStringLiteral("Mosaic Effect"));
        // A Timewarp stays innermost (p.177).
        Sequence seq = Sequence::withDefaultTracks();
        seq.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("b"), 0, 20)});
        QVERIFY(applySegmentEffect(seq, QStringLiteral("V1"), 0, Effect{QStringLiteral("Timewarp"), QStringLiteral("Timewarp"), {}}));
        QVERIFY(applySegmentEffect(seq, QStringLiteral("V1"), 0, kPip, true));
        Segment warped = seq.track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(nestedEffects(warped, 0).size(), 2);
        QVERIFY(!moveNestedEffect(warped, 0, 1, 0, &error));
        QVERIFY(error.contains(QStringLiteral("innermost")));
        // A Submaster over several tracks keeps its inputs (p.222).
        Sequence layered = Sequence::withDefaultTracks();
        layered.addTrack(TrackKind::Video);
        layered.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("c"), 0, 20)});
        layered.overwrite(QStringLiteral("V2"), 0, {clip(QStringLiteral("d"), 0, 20)});
        QVERIFY(collapseTracks(layered, {QStringLiteral("V1"), QStringLiteral("V2")}, 0, 20));
        Segment sub = layered.track(QStringLiteral("V1"))->segments.at(0);
        nestEffect(sub, kFlop);
        QCOMPARE(names(nestedEffects(sub, 0)), (QStringList{QStringLiteral("Flop"), QStringLiteral("Submaster")}));
        QVERIFY(!moveNestedEffect(sub, 0, 1, 0, &error));
        QVERIFY(error.contains(QStringLiteral("inputs")));
        // After an Add Edit the second part's nested effects start before it.
        Sequence cut = Sequence::withDefaultTracks();
        cut.overwrite(QStringLiteral("V1"), 0, {s});
        QVERIFY(cut.addEdit(QStringLiteral("V1"), 12));
        const QList<NestedEffect> second = nestedEffects(cut.track(QStringLiteral("V1"))->segments.at(1), 0);
        QCOMPARE(second.size(), 3);
        QCOMPARE(second.at(1).start, -12);
        QCOMPARE(second.at(1).length, 30);
    }

    void nestInRenderKey() {
        // A change inside a nest puts the outer effect's render out of date.
        Sequence seq = Sequence::withDefaultTracks();
        Segment s = clip(QStringLiteral("k"), 0, 10);
        s.effect = kFlop;
        nestEffect(s, kPip);
        seq.overwrite(QStringLiteral("V1"), 0, {s});
        const QList<RenderTarget> targets = renderTargets(seq, {QStringLiteral("V1")}, 0, 10);
        QVERIFY(!targets.isEmpty());
        const QString before = renderKey(seq, targets.first());
        Effect flop = kFlop;
        flop.parameters.insert(QStringLiteral("Grid"), QJsonObject{{QStringLiteral("Show Grid"), true}});
        QVERIFY(setNestedEffect(seq.track(QStringLiteral("V1"))->segments[0], 0, 1, flop));
        QVERIFY(renderKey(seq, targets.first()) != before);
    }

    void collapse() {
        // Two layers collapsed between 10 and 30 (p.226-227): a Submaster on V1 whose nest holds both, in order.
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("bottom"), 0, 40)});
        seq.overwrite(QStringLiteral("V2"), 0, {clip(QStringLiteral("top"), 100, 40)});
        QString error;
        QVERIFY(collapseTracks(seq, {QStringLiteral("V2"), QStringLiteral("V1")}, 10, 30, &error));
        const Track* v1 = seq.track(QStringLiteral("V1"));
        const Track* v2 = seq.track(QStringLiteral("V2"));
        std::int64_t start = 0;
        const Segment& sub = v1->segments.at(v1->segmentAt(15, &start));
        QCOMPARE(start, 10);
        QCOMPARE(sub.length, 20);
        QVERIFY(sub.effect && isSubmaster(*sub.effect));
        QVERIFY(v2->segments.at(v2->segmentAt(15)).isFiller());
        const Sequence nest = nestOf(sub);
        QCOMPARE(nest.track(QStringLiteral("V1"))->segments.at(0).clipId, QStringLiteral("bottom"));
        QCOMPARE(nest.track(QStringLiteral("V1"))->segments.at(0).sourceIn, 10);
        QCOMPARE(nest.track(QStringLiteral("V2"))->segments.at(0).clipId, QStringLiteral("top"));
        QCOMPARE(nest.track(QStringLiteral("V2"))->segments.at(0).sourceIn, 110);
        // Outside the marks nothing changed.
        QCOMPARE(v2->segments.at(0).clipId, QStringLiteral("top"));
        QCOMPARE(v2->segments.at(0).length, 10);
        // "The tracks must be adjacent", marks needed, locked tracks refused.
        Sequence three = Sequence::withDefaultTracks();
        three.addTrack(TrackKind::Video);
        three.addTrack(TrackKind::Video);
        QVERIFY(!collapseTracks(three, {QStringLiteral("V1"), QStringLiteral("V3")}, 0, 10, &error));
        QVERIFY(!collapseTracks(three, {QStringLiteral("V1")}, 5, 5, &error));
        three.track(QStringLiteral("V2"))->locked = true;
        QVERIFY(!collapseTracks(three, {QStringLiteral("V1"), QStringLiteral("V2")}, 0, 10, &error));
        QCOMPARE(nestLevelLabel(0), QString());
        QCOMPARE(nestLevelLabel(2), QStringLiteral("N2"));
    }
};

QTEST_GUILESS_MAIN(TestNest)
#include "tst_nest.moc"
