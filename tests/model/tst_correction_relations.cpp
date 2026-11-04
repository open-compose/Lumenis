// Relationship colour corrections (FX Guide p.596-598; lumenis/model/correction_relations.h): the keys segments are
// related by, the segments each relationship reaches, converting between relationships of a side, and the relationship
// kept with the sequence.
#include "lumenis/model/correction_relations.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t length) {
    Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

// Clips by id: a master clip with a tape, and a subclip of it.
struct Clips {
    QHash<QString, BinItem> items;
    void add(const QString& id, const QString& name, const QString& tape, BinItemKind kind = BinItemKind::MasterClip,
             const QString& master = {}) {
        BinItem i;
        i.id = id;
        i.name = name;
        i.kind = kind;
        i.mobId = MobId::generate();
        if (!tape.isEmpty()) i.fields.insert(QStringLiteral("Tape"), tape);
        if (!master.isEmpty()) {
            ClipSource from;
            from.kind = ClipSource::Kind::Clip;
            from.mob = items.value(master).mobId;
            i.sources << from;
        }
        items.insert(id, i);
    }
    CorrectionClipLookup lookup() const {
        return [this](const Segment& s) -> std::optional<BinItem> {
            if (!items.contains(s.clipId)) return std::nullopt;
            return items.value(s.clipId);
        };
    }
};

ColorCorrection gain(double v) {
    ColorCorrection c;
    c.set(QStringLiteral("hsl/controls/master/gain"), v);
    return c;
}

} // namespace

class TestCorrectionRelations : public QObject {
    Q_OBJECT

private slots:
    void names() {
        QCOMPARE(relationshipText(CorrectionRelationship::SourceClipName), QStringLiteral("Src Clip Name"));
        QCOMPARE(relationshipText(CorrectionRelationship::ProgramTrack), QStringLiteral("Prog Track"));
        for (int i = 0; i <= static_cast<int>(CorrectionRelationship::ProgramSegment); ++i) {
            const auto r = static_cast<CorrectionRelationship>(i);
            QCOMPARE(relationshipFromKey(relationshipKey(r)), std::optional(r));
        }
        QVERIFY(!relationshipFromKey(QStringLiteral("nonsense")));
        QCOMPARE(sideOf(CorrectionRelationship::SubClip), CorrectionSide::Source);
        QCOMPARE(sideOf(CorrectionRelationship::ProgramTrack), CorrectionSide::Program);
        QVERIFY(isSegmentOnly(CorrectionRelationship::SourceSegment) && isSegmentOnly(CorrectionRelationship::ProgramSegment));
        QVERIFY(!isSegmentOnly(CorrectionRelationship::ProgramTrack));
        // "This relationship ignores clip name extensions — .new and any subsequent characters" (p.597).
        QCOMPARE(baseClipName(QStringLiteral("Interview.new.01")), QStringLiteral("Interview"));
        QCOMPARE(baseClipName(QStringLiteral("Interview")), QStringLiteral("Interview"));
    }

    void keys() {
        Clips clips;
        clips.add(QStringLiteral("m"), QStringLiteral("Beach.new.02"), QStringLiteral("T1"));
        clips.add(QStringLiteral("s"), QStringLiteral("Beach Sub"), QStringLiteral("T1"), BinItemKind::Subclip, QStringLiteral("m"));
        const CorrectionKeys master = correctionKeysOf(clip(QStringLiteral("m"), 10), clips.items.value(QStringLiteral("m")));
        QCOMPARE(master.tape, QStringLiteral("T1"));
        QCOMPARE(master.clipName, QStringLiteral("Beach"));
        QCOMPARE(master.masterClip, QString::fromStdString(clips.items.value(QStringLiteral("m")).mobId.toString()));
        QVERIFY(master.subClip.isEmpty());
        const CorrectionKeys sub = correctionKeysOf(clip(QStringLiteral("s"), 10), clips.items.value(QStringLiteral("s")));
        QCOMPARE(sub.masterClip, master.masterClip); // a subclip is of its master clip too
        QCOMPARE(sub.subClip, QString::fromStdString(clips.items.value(QStringLiteral("s")).mobId.toString()));
        // A tape a subclip's sources name comes first; without a clip, the segment's name and MobID.
        const CorrectionKeys lone = correctionKeysOf(clip(QStringLiteral("x.new"), 10), std::nullopt);
        QCOMPARE(lone.clipName, QStringLiteral("x"));
        QVERIFY(lone.tape.isEmpty());
        QVERIFY(!lone.masterClip.isEmpty());
    }

    // Each relationship's segments (p.597), on the video tracks only, the current one first; filler never.
    void related() {
        Clips clips;
        clips.add(QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("T1"));
        clips.add(QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("T2"));
        clips.add(QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("T1"));
        clips.add(QStringLiteral("sa"), QStringLiteral("A sub"), QStringLiteral("T1"), BinItemKind::Subclip, QStringLiteral("a"));
        Sequence seq;
        seq.addTrack(TrackKind::Video);
        seq.addTrack(TrackKind::Video);
        seq.addTrack(TrackKind::Audio);
        seq.track(QStringLiteral("V1"))->segments = {clip(QStringLiteral("a"), 10), clip(QStringLiteral("b"), 10), Segment::filler(5),
                                                     clip(QStringLiteral("c"), 10), clip(QStringLiteral("sa"), 10)};
        seq.track(QStringLiteral("V2"))->segments = {clip(QStringLiteral("c"), 10), clip(QStringLiteral("a"), 5)};
        seq.track(QStringLiteral("A1"))->segments = {clip(QStringLiteral("a"), 10)};
        const auto refs = [&](int index, CorrectionRelationship r) {
            QStringList out;
            for (const SegmentRef& ref : relatedSegments(seq, QStringLiteral("V1"), index, r, clips.lookup()))
                out << ref.track + QLatin1Char(':') + QString::number(ref.start);
            return out.join(QLatin1Char(' '));
        };
        QCOMPARE(refs(0, CorrectionRelationship::SourceSegment), QStringLiteral("V1:0"));
        QCOMPARE(refs(0, CorrectionRelationship::ProgramSegment), QStringLiteral("V1:0"));
        QCOMPARE(refs(0, CorrectionRelationship::SourceTape), QStringLiteral("V1:0 V1:25 V1:35 V2:0 V2:10"));
        QCOMPARE(refs(0, CorrectionRelationship::MasterClip), QStringLiteral("V1:0 V1:35 V2:10"));
        QCOMPARE(refs(0, CorrectionRelationship::SourceClipName), QStringLiteral("V1:0 V2:10"));
        QCOMPARE(refs(0, CorrectionRelationship::SubClip), QStringLiteral("V1:0")); // a master clip is no subclip
        QCOMPARE(refs(4, CorrectionRelationship::SubClip), QStringLiteral("V1:35"));
        QCOMPARE(refs(1, CorrectionRelationship::ProgramTrack), QStringLiteral("V1:10 V1:0 V1:25 V1:35"));
        QVERIFY(relatedSegments(seq, QStringLiteral("V1"), 2, CorrectionRelationship::SourceTape, clips.lookup()).isEmpty());

        // Applying goes to them all on the relationship's side, with the relationship; once more changes nothing.
        QVERIFY(applyRelationshipCorrection(seq, QStringLiteral("V1"), 0, CorrectionRelationship::MasterClip, gain(120), clips.lookup()));
        QVERIFY(!applyRelationshipCorrection(seq, QStringLiteral("V1"), 0, CorrectionRelationship::MasterClip, gain(120), clips.lookup()));
        const Track& t1 = *seq.track(QStringLiteral("V1"));
        QCOMPARE(t1.segments.at(0).sourceCorrection, gain(120));
        QCOMPARE(t1.segments.at(0).sourceRelationship, CorrectionRelationship::MasterClip);
        QCOMPARE(t1.segments.at(4).sourceRelationship, CorrectionRelationship::MasterClip);
        QVERIFY(t1.segments.at(1).sourceCorrection.isEmpty());
        QCOMPARE(seq.track(QStringLiteral("V2"))->segments.at(1).sourceCorrection, gain(120));
        QVERIFY(seq.track(QStringLiteral("A1"))->segments.at(0).sourceCorrection.isEmpty());
        QVERIFY(t1.segments.at(0).programCorrection.isEmpty());

        // Converting to Src Clip Name (p.598): the correction goes to the clip name's segments; the subclip it reached by
        // Master Clip keeps its values as a Src Segment correction.
        QVERIFY(convertRelationship(seq, QStringLiteral("V1"), 0, CorrectionRelationship::SourceClipName, clips.lookup()));
        const Track& c1 = *seq.track(QStringLiteral("V1"));
        QCOMPARE(c1.segments.at(0).sourceRelationship, CorrectionRelationship::SourceClipName);
        QCOMPARE(c1.segments.at(4).sourceRelationship, CorrectionRelationship::SourceSegment);
        QCOMPARE(c1.segments.at(4).sourceCorrection, gain(120));
        QCOMPARE(seq.track(QStringLiteral("V2"))->segments.at(1).sourceRelationship, CorrectionRelationship::SourceClipName);
        // A side without a correction, or the same relationship, is no conversion.
        QVERIFY(!convertRelationship(seq, QStringLiteral("V1"), 0, CorrectionRelationship::SourceClipName, clips.lookup()));
        QVERIFY(!convertRelationship(seq, QStringLiteral("V1"), 0, CorrectionRelationship::ProgramTrack, clips.lookup()));
        // Removing through a relationship leaves the side's segment relationship.
        QVERIFY(applyRelationshipCorrection(seq, QStringLiteral("V1"), 0, CorrectionRelationship::SourceClipName, {}, clips.lookup()));
        QCOMPARE(seq.track(QStringLiteral("V2"))->segments.at(1).sourceRelationship, CorrectionRelationship::SourceSegment);
        QVERIFY(seq.track(QStringLiteral("V2"))->segments.at(1).sourceCorrection.isEmpty());
    }

    // The relationship is kept with the sequence, only while the side has a correction, and keeps an Add Edit apart.
    void kept() {
        Sequence seq;
        seq.addTrack(TrackKind::Video);
        Segment s = clip(QStringLiteral("a"), 10);
        s.sourceCorrection = gain(110);
        s.sourceRelationship = CorrectionRelationship::SourceTape;
        s.programRelationship = CorrectionRelationship::ProgramTrack; // no Program correction: not kept
        seq.track(QStringLiteral("V1"))->segments = {s};
        BinItem item;
        item.kind = BinItemKind::Sequence;
        seq.storeIn(item);
        const Segment back = Sequence::fromItem(item).track(QStringLiteral("V1"))->segments.at(0);
        QCOMPARE(back.sourceRelationship, CorrectionRelationship::SourceTape);
        QCOMPARE(back.programRelationship, CorrectionRelationship::ProgramSegment);
        Segment other = s;
        other.sourceRelationship = CorrectionRelationship::SourceSegment;
        QVERIFY(!Sequence::isMatchFrame(s, other));
    }
};

QTEST_GUILESS_MAIN(TestCorrectionRelations)
#include "tst_correction_relations.moc"
