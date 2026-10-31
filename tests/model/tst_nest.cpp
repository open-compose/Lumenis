// Nested effects (FX Guide p.219-227; lumenis/model/nest.h): a segment's nest, made for Step In and by
// nesting an effect inside another, kept through JSON, trims and Add Edits; Collapse into a Submaster.
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
