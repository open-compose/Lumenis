// Managing colour-corrected sequences (FX Guide ch.21, p.700-706; lumenis/model/correction_manage.h): the counts per
// relationship, Update, Merge (with the guide's example, p.703-704, and the segment relationships), Flatten and Remove.
#include "lumenis/model/correction_manage.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

// A segment of clip `id`, from source frame `in`.
Segment clip(const QString& id, std::int64_t length, std::int64_t in = 0) {
    Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

// Clips by id, each with a tape.
struct Clips {
    QHash<QString, BinItem> items;
    void add(const QString& id, const QString& tape) {
        BinItem i;
        i.id = id;
        i.name = id;
        i.kind = BinItemKind::MasterClip;
        i.mobId = MobId::generate();
        i.fields.insert(QStringLiteral("Tape"), tape);
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

Segment corrected(Segment s, const ColorCorrection& c, CorrectionRelationship r) {
    if (sideOf(r) == CorrectionSide::Source) {
        s.sourceCorrection = c;
        s.sourceRelationship = r;
    } else {
        s.programCorrection = c;
        s.programRelationship = r;
    }
    return s;
}

Sequence twoTracks() {
    Sequence seq;
    seq.addTrack(TrackKind::Video);
    seq.addTrack(TrackKind::Video);
    seq.addTrack(TrackKind::Audio);
    return seq;
}

const Segment& at(const Sequence& seq, const QString& track, int index) {
    return seq.track(track)->segments.at(index);
}

// The source tape of each V1 segment's Source correction's gain ("-" for none), to read the guide's example.
QString gains(const Sequence& seq) {
    QStringList out;
    for (const Segment& s : seq.track(QStringLiteral("V1"))->segments) {
        out << (s.sourceCorrection.isEmpty() ? QStringLiteral("-") : QString::number(s.sourceCorrection.number(QStringLiteral("hsl/controls/master/gain"))));
    }
    return out.join(QLatin1Char(' '));
}

} // namespace

class TestCorrectionManage : public QObject {
    Q_OBJECT

private slots:
    void counts() {
        Clips clips;
        Sequence seq = twoTracks();
        seq.track(QStringLiteral("V1"))->segments = {corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape),
                                                     corrected(clip(QStringLiteral("b"), 10), gain(120), CorrectionRelationship::SourceSegment),
                                                     Segment::filler(5)};
        seq.track(QStringLiteral("V2"))->segments = {corrected(corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape),
                                                               gain(90), CorrectionRelationship::ProgramTrack)};
        seq.track(QStringLiteral("A1"))->segments = {corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape)};
        const QMap<CorrectionRelationship, int> n = correctionCounts(seq);
        QCOMPARE(n.value(CorrectionRelationship::SourceTape), 2); // video tracks only
        QCOMPARE(n.value(CorrectionRelationship::SourceSegment), 1);
        QCOMPARE(n.value(CorrectionRelationship::ProgramTrack), 1);
        QCOMPARE(n.value(CorrectionRelationship::ProgramSegment), 0);
        QCOMPARE(n.size(), 7);
        QCOMPARE(sourceRelationships().size() + programRelationships().size(), 7);
    }

    // "Color correction is automatically applied to segment C based on its Source Tape relationship with segment A"
    // (p.700).
    void update() {
        Clips clips;
        clips.add(QStringLiteral("a"), QStringLiteral("T1"));
        clips.add(QStringLiteral("b"), QStringLiteral("T2"));
        clips.add(QStringLiteral("c"), QStringLiteral("T1"));
        Sequence seq = twoTracks();
        seq.track(QStringLiteral("V1"))->segments = {corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape),
                                                     corrected(clip(QStringLiteral("b"), 10), gain(120), CorrectionRelationship::SourceSegment),
                                                     clip(QStringLiteral("c"), 10), clip(QStringLiteral("b"), 10)};
        seq.track(QStringLiteral("V2"))->segments = {clip(QStringLiteral("c"), 10)};
        seq.track(QStringLiteral("V1"))->segments[0] = corrected(at(seq, QStringLiteral("V1"), 0), gain(80), CorrectionRelationship::ProgramTrack);
        Sequence before = seq;
        QCOMPARE(updateCorrections(seq, {}, clips.lookup()), 0);
        QCOMPARE(updateCorrections(seq, {CorrectionRelationship::SourceTape}, clips.lookup()), 2);
        QCOMPARE(at(seq, QStringLiteral("V1"), 2).sourceCorrection, gain(110));
        QCOMPARE(at(seq, QStringLiteral("V1"), 2).sourceRelationship, CorrectionRelationship::SourceTape);
        QCOMPARE(at(seq, QStringLiteral("V2"), 0).sourceCorrection, gain(110));
        // A Src Segment correction goes nowhere: the other "b" stays uncorrected.
        QVERIFY(at(seq, QStringLiteral("V1"), 3).sourceCorrection.isEmpty());
        // Prog Track: the rest of V1, not V2.
        QCOMPARE(updateCorrections(seq, {CorrectionRelationship::ProgramTrack}, clips.lookup()), 3);
        QCOMPARE(at(seq, QStringLiteral("V1"), 3).programCorrection, gain(80));
        QVERIFY(at(seq, QStringLiteral("V2"), 0).programCorrection.isEmpty());
        QCOMPARE(updateCorrections(seq, sourceRelationships() + programRelationships(), clips.lookup()), 0);
    }

    // The guide's example (p.703-704): tapes 2 1 2 3 1 4 1; tapes 2 and 4 corrected; the editor replaces the fifth
    // segment with tape 2, adds tape 4 near the end and doubles the second; the colorist adds a tape 1 correction.
    void mergeExample() {
        Clips clips;
        for (int tape = 1; tape <= 4; ++tape) {
            for (int take = 0; take < 3; ++take) clips.add(QStringLiteral("t%1_%2").arg(tape).arg(take), QStringLiteral("Tape %1").arg(tape));
        }
        const auto seg = [&](int tape, int take, std::int64_t length = 25) { return clip(QStringLiteral("t%1_%2").arg(tape).arg(take), length); };
        const ColorCorrection t1 = gain(101), t2 = gain(102), t4 = gain(104);
        Sequence original = twoTracks();
        original.track(QStringLiteral("V1"))->segments = {
            corrected(seg(2, 0), t2, CorrectionRelationship::SourceTape), seg(1, 0), corrected(seg(2, 1), t2, CorrectionRelationship::SourceTape),
            seg(3, 0), seg(1, 1), corrected(seg(4, 0), t4, CorrectionRelationship::SourceTape), seg(1, 2)};
        Sequence editor = original;
        QList<Segment>& e = editor.track(QStringLiteral("V1"))->segments;
        e[1].length = 50;
        e[4] = seg(2, 2);
        e.insert(6, seg(4, 1));
        Sequence colorist = original;
        for (Segment& s : colorist.track(QStringLiteral("V1"))->segments) {
            if (s.clipId.startsWith(QLatin1String("t1_"))) s = corrected(s, t1, CorrectionRelationship::SourceTape);
        }
        QCOMPARE(gains(editor), QStringLiteral("102 - 102 - - 104 - -"));
        // The colorist's copy in the Source monitor, the editor's in the Record monitor.
        Sequence merged = editor;
        QVERIFY(mergeCorrections(merged, colorist, sourceRelationships(), MergeConflict::PreferLatest, true, clips.lookup()) > 0);
        QCOMPARE(gains(merged), QStringLiteral("102 101 102 - 102 104 104 101"));
        QCOMPARE(merged.track(QStringLiteral("V1"))->segments.at(1).length, 50); // the editor's trim stays
        QCOMPARE(at(merged, QStringLiteral("V1"), 1).sourceRelationship, CorrectionRelationship::SourceTape);
        // Without Src Tape selected nothing comes over.
        Sequence none = editor;
        QCOMPARE(mergeCorrections(none, colorist, {CorrectionRelationship::SourceClipName}, MergeConflict::PreferLatest, true, clips.lookup()), 0);
    }

    // "In case of conflict" (p.703).
    void mergeConflicts() {
        Clips clips;
        clips.add(QStringLiteral("a"), QStringLiteral("T1"));
        Sequence record = twoTracks();
        record.track(QStringLiteral("V1"))->segments = {corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape)};
        Sequence source = record;
        source.track(QStringLiteral("V1"))->segments[0].sourceCorrection = gain(150);
        const auto merged = [&](MergeConflict c, bool sourceLater) {
            Sequence r = record;
            mergeCorrections(r, source, sourceRelationships(), c, sourceLater, clips.lookup());
            return at(r, QStringLiteral("V1"), 0).sourceCorrection.number(QStringLiteral("hsl/controls/master/gain"));
        };
        QCOMPARE(merged(MergeConflict::PreferSource, false), 150.0);
        QCOMPARE(merged(MergeConflict::PreferRecord, true), 110.0);
        QCOMPARE(merged(MergeConflict::PreferLatest, true), 150.0);
        QCOMPARE(merged(MergeConflict::PreferLatest, false), 110.0);
    }

    // Segment relationships merge by source and timecode, the source segment with the most overlap; tried first for
    // segments without a correction; a relationship correction keeps its type (p.702-703).
    void mergeSegments() {
        Clips clips;
        clips.add(QStringLiteral("m"), QStringLiteral("T1"));
        clips.add(QStringLiteral("n"), QStringLiteral("T1"));
        Sequence source = twoTracks();
        source.track(QStringLiteral("V1"))->segments = {
            corrected(clip(QStringLiteral("m"), 10, 5), gain(120), CorrectionRelationship::SourceSegment), // frames 5-14
            corrected(clip(QStringLiteral("m"), 18, 12), gain(130), CorrectionRelationship::SourceSegment), // frames 12-29
            corrected(clip(QStringLiteral("n"), 10), gain(140), CorrectionRelationship::SourceTape)};
        Sequence record = twoTracks();
        record.track(QStringLiteral("V1"))->segments = {
            clip(QStringLiteral("m"), 20, 0),  // overlaps 10 and 8 frames: the first
            clip(QStringLiteral("m"), 10, 20), // overlaps only the second
            clip(QStringLiteral("m"), 5, 40),  // overlaps none
            corrected(clip(QStringLiteral("m"), 10, 0), gain(90), CorrectionRelationship::SourceTape)};
        Sequence r = record;
        // Not selected by default: only the tape correction comes (to the uncorrected "m" segments of tape T1).
        mergeCorrections(r, source, {CorrectionRelationship::SourceTape}, MergeConflict::PreferRecord, false, clips.lookup());
        QCOMPARE(gains(r), QStringLiteral("90 90 90 90"));
        r = record;
        mergeCorrections(r, source, {CorrectionRelationship::SourceSegment, CorrectionRelationship::SourceTape}, MergeConflict::PreferRecord,
                         false, clips.lookup());
        QCOMPARE(gains(r), QStringLiteral("120 130 90 90"));
        QCOMPARE(at(r, QStringLiteral("V1"), 0).sourceRelationship, CorrectionRelationship::SourceSegment);
        QCOMPARE(at(r, QStringLiteral("V1"), 2).sourceRelationship, CorrectionRelationship::SourceTape);
        // The tape-corrected segment overlaps the first source segment but keeps its Src Tape correction.
        QCOMPARE(at(r, QStringLiteral("V1"), 3).sourceRelationship, CorrectionRelationship::SourceTape);
        // Prog Segment needs the same track.
        Sequence ps = twoTracks();
        ps.track(QStringLiteral("V2"))->segments = {corrected(clip(QStringLiteral("m"), 10), gain(60), CorrectionRelationship::ProgramSegment)};
        Sequence pr = twoTracks();
        pr.track(QStringLiteral("V1"))->segments = {clip(QStringLiteral("m"), 10)};
        QCOMPARE(mergeCorrections(pr, ps, programRelationships(), MergeConflict::PreferSource, true, clips.lookup()), 0);
        pr.track(QStringLiteral("V2"))->segments = {clip(QStringLiteral("m"), 10)};
        QCOMPARE(mergeCorrections(pr, ps, programRelationships(), MergeConflict::PreferSource, true, clips.lookup()), 1);
        QCOMPARE(at(pr, QStringLiteral("V2"), 0).programCorrection, gain(60));
    }

    // "all the color corrections become segment-only relationships" (p.704); Remove Correction (p.705-706).
    void flattenAndRemove() {
        Clips clips;
        Sequence seq = twoTracks();
        seq.track(QStringLiteral("V1"))->segments = {corrected(clip(QStringLiteral("a"), 10), gain(110), CorrectionRelationship::SourceTape),
                                                     corrected(clip(QStringLiteral("b"), 10), gain(120), CorrectionRelationship::MasterClip),
                                                     corrected(clip(QStringLiteral("c"), 10), gain(130), CorrectionRelationship::SourceSegment)};
        seq.track(QStringLiteral("V1"))->segments[0] = corrected(at(seq, QStringLiteral("V1"), 0), gain(70), CorrectionRelationship::ProgramTrack);
        Sequence flat = seq;
        QCOMPARE(flattenCorrections(flat, {CorrectionRelationship::SourceTape, CorrectionRelationship::ProgramTrack}), 2);
        QCOMPARE(at(flat, QStringLiteral("V1"), 0).sourceRelationship, CorrectionRelationship::SourceSegment);
        QCOMPARE(at(flat, QStringLiteral("V1"), 0).programRelationship, CorrectionRelationship::ProgramSegment);
        QCOMPARE(at(flat, QStringLiteral("V1"), 0).sourceCorrection, gain(110)); // the values stay
        QCOMPARE(at(flat, QStringLiteral("V1"), 1).sourceRelationship, CorrectionRelationship::MasterClip);
        QCOMPARE(flattenCorrections(flat, {CorrectionRelationship::SourceTape}), 0);
        Sequence gone = seq;
        QCOMPARE(removeCorrections(gone, {CorrectionRelationship::MasterClip, CorrectionRelationship::SourceSegment}), 2);
        QVERIFY(at(gone, QStringLiteral("V1"), 1).sourceCorrection.isEmpty());
        QVERIFY(at(gone, QStringLiteral("V1"), 2).sourceCorrection.isEmpty());
        QCOMPARE(at(gone, QStringLiteral("V1"), 0).sourceCorrection, gain(110));
        QCOMPARE(correctionCounts(gone).value(CorrectionRelationship::MasterClip), 0);
        QCOMPARE(removeCorrections(gone, programRelationships()), 1);
        QVERIFY(at(gone, QStringLiteral("V1"), 0).programCorrection.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCorrectionManage)
#include "tst_correction_manage.moc"
