#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem clip(const QString& name, std::int64_t frames, const QString& tracks = {}) {
    BinItem i;
    i.id = name;
    i.name = name;
    i.durationFrames = frames;
    i.editRate = Rational(25);
    if (!tracks.isEmpty()) i.fields.insert(QStringLiteral("Tracks"), tracks);
    return i;
}

Segment piece(const QString& clipId, std::int64_t in, std::int64_t length, const QString& track = QStringLiteral("V1")) {
    Segment s;
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = track;
    s.sourceIn = in;
    s.length = length;
    return s;
}

// The track as "name:in+length" pieces, "-:length" for filler.
QStringList layout(const Sequence& s, const QString& track) {
    QStringList out;
    if (const Track* t = s.track(track)) {
        for (const Segment& seg : t->segments) {
            out << (seg.isFiller() ? QStringLiteral("-:%1").arg(seg.length)
                                   : QStringLiteral("%1:%2+%3").arg(seg.clipId).arg(seg.sourceIn).arg(seg.length));
        }
    }
    return out;
}

EditMarks marks(std::optional<std::int64_t> in, std::optional<std::int64_t> out, std::int64_t position,
                std::int64_t duration) {
    return EditMarks{in, out, position, duration};
}

} // namespace

Q_DECLARE_METATYPE(lumenis::model::EditMarks)

class TestSequence : public QObject {
    Q_OBJECT

private slots:
    void trackNames() {
        QVERIFY(parseTrackName(QStringLiteral("V99"))); // up to 99 (p.634)
        QCOMPARE(trackName(TrackKind::Audio, 2), QStringLiteral("A2"));
        QVERIFY(parseTrackName(QStringLiteral("V12")));
        QVERIFY(!parseTrackName(QStringLiteral("TC1")));
        QVERIFY(!parseTrackName(QStringLiteral("A0")));
        QCOMPARE(parseTrackList(QStringLiteral("V1 A1-2 TC1")), (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(parseTrackList(QStringLiteral("V1-2, A3")), (QStringList{"V1", "V2", "A3"}));
    }

    void defaultTracks() {
        // V1, A1, A2 and TC1 (p.459).
        const Sequence s = Sequence::withDefaultTracks();
        QCOMPARE(s.trackNames(), (QStringList{"V1", "A1", "A2"}));
        QVERIFY(s.hasTimecodeTrack());
        QCOMPARE(s.duration(), 0);
    }

    void addsTracksInOrder() {
        Sequence s = Sequence::withDefaultTracks();
        QCOMPARE(*s.addTrack(TrackKind::Video), QStringLiteral("V2"));
        QCOMPARE(*s.addTrack(TrackKind::Audio, AudioFormat::Stereo), QStringLiteral("A3"));
        QCOMPARE(*s.addTrack(TrackKind::Data), QStringLiteral("D1"));
        QVERIFY(!s.addTrack(TrackKind::Data)); // one data track (p.624)
        QCOMPARE(s.trackNames(), (QStringList{"V1", "V2", "A1", "A2", "A3", "D1"}));
        QCOMPARE(s.track(QStringLiteral("A3"))->format, AudioFormat::Stereo);
    }

    void spliceInsertsAndMovesLaterMaterial() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece("a", 0, 100)});
        s.splice(QStringLiteral("V1"), 40, {piece("b", 10, 20)});
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+40", "b:10+20", "a:40+60"}));
        QCOMPARE(s.duration(), 120);
        // Past the end, filler fills the gap.
        s.splice(QStringLiteral("A1"), 10, {piece("c", 0, 5, "A1")});
        QCOMPARE(layout(s, "A1"), (QStringList{"-:10", "c:0+5"}));
    }

    void overwriteReplaces() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece("a", 0, 50), piece("b", 0, 50)});
        s.overwrite(QStringLiteral("V1"), 30, {piece("c", 5, 40)});
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+30", "c:5+40", "b:20+30"}));
        QCOMPARE(s.duration(), 100); // no longer (p.467)
        // Going past the end lengthens it.
        s.overwrite(QStringLiteral("V1"), 90, {piece("d", 0, 20)});
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+30", "c:5+40", "b:20+20", "d:0+20"}));
        QCOMPARE(s.duration(), 110);
    }

    void liftLeavesFillerExtractCloses() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece("a", 0, 50), piece("b", 0, 50)});
        Sequence lifted = s;
        const QList<Segment> removed = lifted.lift(QStringLiteral("V1"), 40, 20);
        QCOMPARE(removed.size(), 2);
        QCOMPARE(layout(lifted, "V1"), (QStringList{"a:0+40", "-:20", "b:10+40"}));
        QCOMPARE(lifted.duration(), 100); // the same length (p.478)
        Sequence extracted = s;
        extracted.extract(QStringLiteral("V1"), 40, 20);
        QCOMPARE(layout(extracted, "V1"), (QStringList{"a:0+40", "b:10+40"}));
        QCOMPARE(extracted.duration(), 80); // shorter
        // Filler next to filler merges.
        lifted.lift(QStringLiteral("V1"), 60, 10);
        QCOMPARE(layout(lifted, "V1"), (QStringList{"a:0+40", "-:30", "b:20+30"}));
        // Nothing past the end.
        QVERIFY(lifted.extract(QStringLiteral("V1"), 200, 10).isEmpty());
    }

    void materialAndEditPoints() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece("a", 0, 50), piece("b", 0, 50)});
        s.splice(QStringLiteral("A1"), 0, {piece("c", 0, 30, "A1")});
        const QList<Segment> m = s.material(QStringLiteral("V1"), 40, 70);
        QCOMPARE(m.size(), 3);
        QCOMPARE(m.at(0), piece("a", 40, 10));
        QCOMPARE(m.at(1), piece("b", 0, 50));
        QVERIFY(m.at(2).isFiller());
        QCOMPARE(m.at(2).length, 10);
        QCOMPARE(s.editPoints(), (QList<std::int64_t>{0, 30, 50, 100}));
        QCOMPARE(s.editPoints({QStringLiteral("A1")}), (QList<std::int64_t>{0, 30}));
    }

    void storesInItem() {
        Sequence s = Sequence::withDefaultTracks();
        s.addTrack(TrackKind::Audio, AudioFormat::Surround51);
        s.splice(QStringLiteral("V1"), 10, {piece("a", 3, 50)});
        BinItem item;
        item.kind = BinItemKind::Sequence;
        s.storeIn(item);
        QCOMPARE(item.durationFrames, 60);
        QCOMPARE(Sequence::fromItem(item), s);
        // A sequence saved before it had tracks keeps its length.
        BinItem old;
        old.kind = BinItemKind::Sequence;
        old.durationFrames = 500;
        QCOMPARE(Sequence::fromItem(old).duration(), 500);
    }

    void sourceTracks() {
        QCOMPARE(sourceTracksOf(clip("a", 10)), (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(sourceTracksOf(clip("a", 10, "V1 A1-4")), (QStringList{"V1", "A1", "A2", "A3", "A4"}));
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        Sequence s = Sequence::withDefaultTracks();
        s.addTrack(TrackKind::Video);
        s.storeIn(seq);
        QCOMPARE(sourceTracksOf(seq), (QStringList{"V1", "V2", "A1", "A2"}));
    }

    void patching() {
        TrackPatch p;
        p.setSourceTracks({"V1", "A1", "A2"});
        QVERIFY(p.isSourceSelected("A1"));
        QCOMPARE(p.recordFor("A1"), QStringLiteral("A1"));
        // Audio only to audio, video only to video, never data (p.629).
        QVERIFY(!p.patch("A1", "V1"));
        QVERIFY(!TrackPatch::canPatch("D1", "D1"));
        QVERIFY(p.patch("A1", "A3"));
        QCOMPARE(p.recordFor("A1"), QStringLiteral("A3"));
        QCOMPARE(p.sourceFor("A3"), QStringLiteral("A1"));
        // A2 to A3 too: A1 takes A2's old place.
        QVERIFY(p.patch("A2", "A3"));
        QCOMPARE(p.recordFor("A1"), QStringLiteral("A2"));
        p.restoreDefault();
        QCOMPARE(p.recordFor("A1"), QStringLiteral("A1"));
        // A new clip keeps the selection of tracks it shares.
        p.setSourceSelected("A2", false);
        p.setSourceTracks({"V1", "A1", "A2", "A3"});
        QVERIFY(!p.isSourceSelected("A2"));
        QVERIFY(p.isSourceSelected("A3"));
    }

    void editPairsFollowSelections() {
        // Edited only when selected on both sides (p.626); missing record
        // tracks appear.
        TrackPatch p;
        p.setSourceTracks({"V1", "A1", "A2", "A3"});
        Sequence s = Sequence::withDefaultTracks();
        p.setRecordSelected("A2", false);
        p.setSourceSelected("A1", false);
        const auto pairs = p.editPairs(s);
        QCOMPARE(pairs.size(), 2);
        QCOMPARE(pairs.at(0), std::make_pair(QStringLiteral("V1"), QStringLiteral("V1")));
        QCOMPARE(pairs.at(1), std::make_pair(QStringLiteral("A3"), QStringLiteral("A3")));
        QCOMPARE(p.selectedRecordTracks(s), (QStringList{"V1", "A1"}));
    }

    void threePointEdits_data() {
        QTest::addColumn<EditMarks>("source");
        QTest::addColumn<EditMarks>("record");
        QTest::addColumn<std::int64_t>("sourceStart");
        QTest::addColumn<std::int64_t>("recordStart");
        QTest::addColumn<std::int64_t>("length");
        // Source IN and OUT, record IN or position (p.466).
        QTest::newRow("source in/out, record position")
            << marks(10, 19, 0, 100) << marks({}, {}, 50, 200) << std::int64_t(10) << std::int64_t(50) << std::int64_t(10);
        QTest::newRow("source in/out, record in")
            << marks(10, 19, 0, 100) << marks(30, {}, 50, 200) << std::int64_t(10) << std::int64_t(30) << std::int64_t(10);
        // Record OUT only: backtimed.
        QTest::newRow("source in/out, record out")
            << marks(10, 19, 0, 100) << marks({}, 59, 0, 200) << std::int64_t(10) << std::int64_t(50) << std::int64_t(10);
        // Record IN and OUT with a source IN (p.467).
        QTest::newRow("record in/out, source in")
            << marks(5, {}, 0, 100) << marks(20, 29, 0, 200) << std::int64_t(5) << std::int64_t(20) << std::int64_t(10);
        QTest::newRow("record in/out, source out")
            << marks({}, 50, 0, 100) << marks(20, 29, 0, 200) << std::int64_t(41) << std::int64_t(20) << std::int64_t(10);
        // One mark: the rest of the clip at the record position (p.501).
        QTest::newRow("source in only")
            << marks(60, {}, 0, 100) << marks({}, {}, 7, 200) << std::int64_t(60) << std::int64_t(7) << std::int64_t(40);
        QTest::newRow("no marks")
            << marks({}, {}, 25, 100) << marks({}, {}, 0, 0) << std::int64_t(25) << std::int64_t(0) << std::int64_t(75);
        // Single-mark: a source OUT and the position as IN (p.468).
        QTest::newRow("source out, position")
            << marks({}, 40, 30, 100) << marks({}, {}, 0, 0) << std::int64_t(30) << std::int64_t(0) << std::int64_t(11);
    }

    void threePointEdits() {
        QFETCH(EditMarks, source);
        QFETCH(EditMarks, record);
        const EditPlan plan = planEdit(source, record);
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QTEST(plan.sourceStart, "sourceStart");
        QTEST(plan.recordStart, "recordStart");
        QTEST(plan.length, "length");
    }

    void badEdits() {
        QVERIFY(!planEdit(marks({}, {}, 0, 0), marks({}, {}, 0, 0)).ok());
        // Not enough source before a backtimed OUT.
        QVERIFY(!planEdit(marks({}, 5, 0, 100), marks(0, 29, 0, 100)).ok());
        // Source OUT before the position.
        QVERIFY(!planEdit(marks({}, 5, 10, 100), marks({}, {}, 0, 0)).ok());
        // Backtimed before the start of the sequence.
        QVERIFY(!planEdit(marks(0, 49, 0, 100), marks({}, 9, 0, 100)).ok());
    }

    void appliesEditsToPatchedTracks() {
        Sequence s = Sequence::withDefaultTracks();
        const BinItem a = clip("a", 100, "V1 A1-2");
        TrackPatch p;
        p.setSourceTracks(sourceTracksOf(a));
        EditPlan plan{10, 0, 50, {}};
        QCOMPARE(applyEdit(s, EditKind::SpliceIn, plan, "bin", a, p), (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(layout(s, "A2"), (QStringList{"a:10+50"}));
        QCOMPARE(s.track("A2")->segments.first().sourceTrack, QStringLiteral("A2"));
        QCOMPARE(s.track("A2")->segments.first().binId, QStringLiteral("bin"));
        // Video only, patched to a new V2.
        p.setSourceSelected("A1", false);
        p.setSourceSelected("A2", false);
        QVERIFY(p.patch("V1", "V2"));
        plan = EditPlan{0, 20, 10, {}};
        QCOMPARE(applyEdit(s, EditKind::Overwrite, plan, "bin", a, p), (QStringList{"V2"}));
        QCOMPARE(layout(s, "V2"), (QStringList{"-:20", "a:0+10"}));
        QCOMPARE(layout(s, "V1"), (QStringList{"a:10+50"}));
        // Lift and extract on chosen tracks.
        QCOMPARE(removeMaterial(s, true, 0, 10, {"V1", "V2"}), (QStringList{"V1", "V2"}));
        QCOMPARE(layout(s, "V2"), (QStringList{"-:10", "a:0+10"}));
        QCOMPARE(layout(s, "V1"), (QStringList{"a:20+40"}));
    }

    void editsFromASequence() {
        Sequence inner = Sequence::withDefaultTracks();
        inner.splice("V1", 0, {piece("x", 0, 10), piece("y", 0, 10)});
        BinItem seq;
        seq.id = "seq";
        seq.kind = BinItemKind::Sequence;
        inner.storeIn(seq);
        Sequence s = Sequence::withDefaultTracks();
        TrackPatch p;
        p.setSourceTracks({"V1"});
        applyEdit(s, EditKind::SpliceIn, EditPlan{5, 0, 10, {}}, "bin", seq, p);
        QCOMPARE(layout(s, "V1"), (QStringList{"x:5+5", "y:0+5"}));
    }

    void locksAndSyncLocks() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice("V1", 0, {piece("a", 0, 100)});
        s.splice("A1", 0, {piece("a", 0, 100, "A1")});
        s.splice("A2", 0, {piece("a", 0, 100, "A2")});
        // A locked track takes no edits (p.633).
        s.track("A2")->locked = true;
        TrackPatch p;
        p.setSourceTracks({"V1", "A1", "A2"});
        QCOMPARE(applyEdit(s, EditKind::Overwrite, EditPlan{0, 10, 5, {}}, "bin", clip("b", 50), p),
                 (QStringList{"V1", "A1"}));
        QCOMPARE(removeMaterial(s, true, 0, 10, {"V1", "A1", "A2"}), (QStringList{"V1", "A1"}));
        QCOMPARE(s.track("A2")->length(), 100);
        // A splice into a sync-locked track puts filler into the other
        // sync-locked tracks (p.493).
        s.track("A2")->locked = false;
        s.track("V1")->syncLocked = true;
        s.track("A2")->syncLocked = true;
        p.setSourceSelected("A1", false);
        p.setSourceSelected("A2", false);
        applyEdit(s, EditKind::SpliceIn, EditPlan{0, 20, 7, {}}, "bin", clip("c", 50), p);
        QCOMPARE(s.track("V1")->length(), 97);
        QCOMPARE(s.track("A2")->length(), 107);
        QVERIFY(s.track("A2")->segments.at(1).isFiller());
        QCOMPARE(s.track("A1")->length(), 90); // not sync-locked
        // Extract on a sync-locked track extracts from all of them (p.479).
        QCOMPARE(removeMaterial(s, true, 20, 7, {"V1"}), (QStringList{"V1", "A2"}));
        QCOMPARE(s.track("A2")->length(), 100);
        // Locks are saved.
        BinItem item;
        item.kind = BinItemKind::Sequence;
        s.storeIn(item);
        QVERIFY(Sequence::fromItem(item).track("V1")->syncLocked);
    }

    void addEditsAndMatchFrames() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice("V1", 0, {piece("a", 0, 100)});
        s.splice("A1", 40, {piece("b", 0, 20, "A1")});
        // An add edit splits a clip seamlessly (p.639).
        QVERIFY(s.addEdit("V1", 30));
        QVERIFY(!s.addEdit("V1", 30));
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+30", "a:30+70"}));
        QVERIFY(Sequence::isMatchFrame(s.track("V1")->segments.at(0), s.track("V1")->segments.at(1)));
        // Filler keeps its add edit.
        QVERIFY(s.addEdit("A1", 10));
        QCOMPARE(layout(s, "A1"), (QStringList{"-:10", "-:30", "b:0+20"}));
        BinItem item;
        item.kind = BinItemKind::Sequence;
        s.storeIn(item);
        QCOMPARE(Sequence::fromItem(item), s);
        // Remove Match Frame Edits, only inside the range (p.640).
        QCOMPARE(s.removeMatchFrameEdits({}, 0, 5), 0);
        QCOMPARE(s.removeMatchFrameEdits({}, 0, 100), 2);
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+100"}));
        QCOMPARE(layout(s, "A1"), (QStringList{"-:40", "b:0+20"}));
        // A real cut is not a match frame.
        QVERIFY(!Sequence::isMatchFrame(piece("a", 0, 10), piece("a", 20, 10)));
    }

    void clipboardAndPaste() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice("V1", 0, {piece("a", 0, 50), piece("b", 0, 50)});
        s.splice("A1", 0, {piece("c", 0, 20, "A1")});
        // The Clipboard holds the marked material (p.479).
        const Sequence clip = s.excerpt({"V1", "A1", "A2"}, 40, 20);
        QCOMPARE(clip.trackNames(), (QStringList{"V1"})); // A1 and A2 have nothing there
        QCOMPARE(layout(clip, "V1"), (QStringList{"a:40+10", "b:0+10"}));
        // Pasted onto the same tracks.
        QCOMPARE(paste(s, clip, 0, EditKind::SpliceIn), (QStringList{"V1"}));
        QCOMPARE(s.track("V1")->length(), 120);
        QCOMPARE(paste(s, clip, 100, EditKind::Overwrite), (QStringList{"V1"}));
        QCOMPARE(s.track("V1")->length(), 120);
    }

    void replaceEdits() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice("V1", 0, {piece("a", 0, 50), piece("b", 0, 30), piece("c", 0, 20)});
        // The source position lines up with the record position; the
        // segment's length is kept (p.467-468).
        EditPlan plan = planReplace(s, "V1", marks({}, {}, 40, 200), marks({}, {}, 60, 100));
        QVERIFY2(plan.ok(), qPrintable(plan.error));
        QCOMPARE(plan.recordStart, 50);
        QCOMPARE(plan.length, 30);
        QCOMPARE(plan.sourceStart, 30);
        // Record IN to OUT instead of the segment.
        plan = planReplace(s, "V1", marks({}, {}, 40, 200), marks(55, 64, 60, 100));
        QCOMPARE(plan.recordStart, 55);
        QCOMPARE(plan.length, 10);
        QCOMPARE(plan.sourceStart, 35);
        // Not enough source before the sync frame.
        QVERIFY(!planReplace(s, "V1", marks({}, {}, 5, 200), marks({}, {}, 60, 100)).ok());
        QVERIFY(!planReplace(s, "V1", marks({}, {}, 40, 200), marks({}, {}, 150, 100)).ok());
    }

    void segmentMoves() {
        Sequence base = Sequence::withDefaultTracks();
        base.splice("V1", 0, {piece("a", 0, 30), piece("b", 0, 20), piece("c", 0, 50)});
        // Lift/Overwrite: filler left behind (p.620).
        Sequence s = base;
        QString error;
        auto moved = moveSegments(s, {{"V1", 30}}, 40, 0, EditKind::Overwrite, false, &error);
        QVERIFY2(!moved.isEmpty(), qPrintable(error));
        QCOMPARE(moved, (QList<SegmentRef>{{"V1", 70}}));
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+30", "-:20", "c:0+20", "b:0+20", "c:40+10"}));
        // Extract/Splice-in: the gap closes, the segment goes in.
        s = base;
        moved = moveSegments(s, {{"V1", 30}}, 40, 0, EditKind::SpliceIn, false);
        // Dropped 20 frames into c, which moved up 20 frames.
        QCOMPARE(layout(s, "V1"), (QStringList{"a:0+30", "c:0+20", "b:0+20", "c:20+30"}));
        QCOMPARE(moved, (QList<SegmentRef>{{"V1", 50}}));
        // Up a track: a new V2 (p.635). Copy with Alt leaves the original.
        s = base;
        moved = moveSegments(s, {{"V1", 0}}, 0, 1, EditKind::Overwrite, true);
        QCOMPARE(layout(s, "V2"), (QStringList{"a:0+30"}));
        QCOMPARE(layout(s, "V1"), layout(base, "V1"));
        // Not onto a locked track, and not below V1.
        s.track("V2")->locked = true;
        QVERIFY(moveSegments(s, {{"V1", 30}}, 0, 1, EditKind::Overwrite, false, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(moveSegments(s, {{"V1", 30}}, 0, -1, EditKind::Overwrite, false).isEmpty());
        QVERIFY(moveSegments(s, {{"V1", 30}}, -100, 0, EditKind::Overwrite, false).isEmpty());
        // Two segments on one track, spliced earlier.
        s = base;
        moved = moveSegments(s, {{"V1", 30}, {"V1", 50}}, -30, 0, EditKind::SpliceIn, false);
        // c lands 20 frames into a, b at the start.
        QCOMPARE(layout(s, "V1"), (QStringList{"b:0+20", "a:0+20", "c:0+50", "a:20+10"}));
        QCOMPARE(moved.size(), 2);
        QVERIFY(moved.contains(SegmentRef{"V1", 0}));
        QVERIFY(moved.contains(SegmentRef{"V1", 40}));
    }

    void deleteAndCopySegments() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice("V1", 0, {piece("a", 0, 30), piece("b", 0, 20), piece("c", 0, 50)});
        s.splice("A1", 0, {piece("d", 0, 100, "A1")});
        // Copy: only the selected segments, from the first one's start.
        std::int64_t start = -1;
        const Sequence copied = copySegments(s, {{"V1", 30}, {"A1", 0}}, &start);
        QCOMPARE(start, 0);
        QCOMPARE(layout(copied, "V1"), (QStringList{"-:30", "b:0+20", "-:50"}));
        QCOMPARE(layout(copied, "A1"), (QStringList{"d:0+100"}));
        // Delete: Lift leaves filler, Extract closes up (p.621).
        Sequence lifted = s;
        QVERIFY(deleteSegments(lifted, {{"V1", 0}, {"V1", 50}}, false));
        QCOMPARE(layout(lifted, "V1"), (QStringList{"-:30", "b:0+20", "-:50"}));
        QVERIFY(deleteSegments(s, {{"V1", 0}, {"V1", 50}}, true));
        QCOMPARE(layout(s, "V1"), (QStringList{"b:0+20"}));
        QVERIFY(!deleteSegments(s, {{"V1", 7}}, true)); // no segment starts there
    }

    void matchesFrames() {
        Sequence s = Sequence::withDefaultTracks();
        s.addTrack(TrackKind::Video);
        s.splice("V1", 0, {piece("a", 10, 100)});
        s.splice("V2", 50, {piece("b", 5, 20, "V1")});
        s.splice("A1", 0, {piece("c", 0, 100, "A1")});
        // The top video track first (p.434).
        auto m = matchFrame(s, {}, 60);
        QVERIFY(m);
        QCOMPARE(m->segment.clipId, QStringLiteral("b"));
        QCOMPARE(m->frame, 15);
        m = matchFrame(s, {}, 20);
        QCOMPARE(m->segment.clipId, QStringLiteral("a"));
        QCOMPARE(m->frame, 30);
        m = matchFrame(s, {"A1"}, 20);
        QCOMPARE(m->segment.clipId, QStringLiteral("c"));
        QVERIFY(!matchFrame(s, {"A2"}, 20));
    }

    void undoRedo() {
        EditHistory h;
        QVERIFY(!h.canUndo());
        for (int i = 0; i < EditHistory::kLevels + 5; ++i) {
            BinItem after;
            after.durationFrames = i;
            h.push(EditStep{QStringLiteral("Edit %1").arg(i), "bin", BinItem{}, after});
        }
        // 100 levels (p.466).
        QCOMPARE(h.undoCount(), EditHistory::kLevels);
        QCOMPARE(h.descriptions().first(), QStringLiteral("Edit 104"));
        const EditStep* undone = h.undo();
        QCOMPARE(undone->after.durationFrames, 104);
        QVERIFY(h.canRedo());
        QCOMPARE(h.redo()->after.durationFrames, 104);
        QVERIFY(!h.canRedo());
        h.undo();
        h.push(EditStep{QStringLiteral("New"), "bin", BinItem{}, BinItem{}});
        QVERIFY(!h.canRedo()); // a new edit drops what could be redone
        QCOMPARE(h.descriptions().first(), QStringLiteral("New"));
    }
};

QTEST_GUILESS_MAIN(TestSequence)
#include "tst_sequence.moc"
