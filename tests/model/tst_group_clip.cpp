// Group clips' model (Editing Guide p.1123-1131; docs/spec/multicamera.md): grouping by IN points, OUT points,
// source timecode and Auxiliary TC, the group's name, length and tracks, an angle played as a clip of the group's
// frames, edits from a group and the angle kept on segments.
#include "lumenis/model/group_clip.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

// A linked master clip of `frames` at 25 fps starting at timecode `start`.
BinItem clip(const QString& id, std::int64_t frames, const QString& start = QStringLiteral("01:00:00:00"),
             const QString& tracks = QStringLiteral("V1 A1-2")) {
    BinItem item;
    item.id = id;
    item.name = id;
    item.editRate = Rational(25);
    item.durationFrames = frames;
    item.fields.insert(QStringLiteral("Start"), start);
    item.fields.insert(QStringLiteral("Tracks"), tracks);
    item.link = QJsonObject{{QStringLiteral("path"), QStringLiteral("/media/%1.mov").arg(id)},
                            {QStringLiteral("rate"), QStringLiteral("25/1")},
                            {QStringLiteral("frames"), static_cast<qint64>(frames)},
                            {QStringLiteral("videoStream"), 0}};
    return item;
}

QList<std::pair<QString, BinItem>> inBin(const QList<BinItem>& items) {
    QList<std::pair<QString, BinItem>> out;
    for (const BinItem& i : items) out.append({QStringLiteral("bin"), i});
    return out;
}

} // namespace

class TestGroupClip : public QObject {
    Q_OBJECT

private slots:
    // "mark an IN point at the sync point at the start of each clip" (p.1124): the clips line up on their IN points,
    // and the group covers them all.
    void groupsByInPoints() {
        Bin bin;
        const BinItem a = clip(QStringLiteral("A"), 100);
        const BinItem b = clip(QStringLiteral("B"), 80);
        GroupOptions options;
        options.sync = GroupSync::Inpoints;
        options.marks = [](const BinItem& c, bool in) -> std::optional<std::int64_t> {
            if (!in) return std::nullopt;
            return c.id == QLatin1String("A") ? 30 : 10;
        };
        QString error;
        const auto group = groupClips(bin, inBin({a, b}), options, &error);
        QVERIFY2(group, qPrintable(error));
        QCOMPARE(group->kind, BinItemKind::Group);
        // "the name of the first clip in the group, followed by the file name extension Grp.n" (p.1124).
        QCOMPARE(group->name, QStringLiteral("A.Grp.1"));
        // A from -30 to 70 about the sync point, B from -10 to 70: the group from -30 to 70.
        QCOMPARE(group->durationFrames, std::int64_t(100));
        const auto g = groupOf(*group);
        QVERIFY(g);
        QCOMPARE(g->angles.size(), qsizetype(2));
        QCOMPARE(g->angles[0].offset, std::int64_t(0));
        QCOMPARE(g->angles[1].offset, std::int64_t(-20));
        QCOMPARE(g->selected, 0);
        QCOMPARE(sourceTracksOf(*group), (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(group->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
        // The next group of the same name counts on.
        bin.items << *group;
        QCOMPARE(groupName(bin, QStringLiteral("A")), QStringLiteral("A.Grp.2"));
    }

    void groupsByOutPointsAndTheBinsMarks() {
        Bin bin;
        BinItem a = clip(QStringLiteral("A"), 50);
        BinItem b = clip(QStringLiteral("B"), 50);
        // The bin's Mark OUT columns when the clips were not marked in the Source monitor.
        a.fields.insert(QStringLiteral("Mark OUT"), QStringLiteral("01:00:01:15")); // frame 40
        b.fields.insert(QStringLiteral("Mark OUT"), QStringLiteral("01:00:01:05")); // frame 30
        GroupOptions options;
        options.sync = GroupSync::Outpoints;
        const auto group = groupClips(bin, inBin({a, b}), options);
        QVERIFY(group);
        const auto g = groupOf(*group);
        QCOMPARE(g->angles[0].offset, std::int64_t(0));
        QCOMPARE(g->angles[1].offset, std::int64_t(-10));
        QCOMPARE(group->durationFrames, std::int64_t(60));
    }

    void groupsByTimecode() {
        Bin bin;
        const BinItem a = clip(QStringLiteral("A"), 100, QStringLiteral("10:00:00:00"));
        const BinItem b = clip(QStringLiteral("B"), 100, QStringLiteral("10:00:02:00")); // 50 frames later
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        const auto group = groupClips(bin, inBin({a, b}), options);
        QVERIFY(group);
        const auto g = groupOf(*group);
        QCOMPARE(g->angles[0].offset, std::int64_t(0));
        QCOMPARE(g->angles[1].offset, std::int64_t(-50));
        QCOMPARE(group->durationFrames, std::int64_t(150));
        // Auxiliary TC2.
        BinItem c = a;
        BinItem d = b;
        c.fields.insert(QStringLiteral("Auxiliary TC2"), QStringLiteral("00:00:10:00"));
        d.fields.insert(QStringLiteral("Auxiliary TC2"), QStringLiteral("00:00:09:00")); // 25 frames earlier
        options.sync = GroupSync::AuxTimecode;
        options.auxTc = 2;
        const auto aux = groupClips(bin, inBin({c, d}), options);
        QVERIFY(aux);
        QCOMPARE(groupOf(*aux)->angles[0].offset, std::int64_t(-25));
        QCOMPARE(groupOf(*aux)->angles[1].offset, std::int64_t(0));
    }

    void refusesWhatCannotBeGrouped() {
        Bin bin;
        const BinItem a = clip(QStringLiteral("A"), 100);
        QString error;
        GroupOptions options;
        options.sync = GroupSync::Inpoints;
        // One clip.
        QVERIFY(!groupClips(bin, inBin({a}), options, &error));
        QVERIFY(!error.isEmpty());
        // No IN point.
        QVERIFY(!groupClips(bin, inBin({a, clip(QStringLiteral("B"), 10)}), options, &error));
        QVERIFY(error.contains(QLatin1String("IN point")));
        // Another frame rate.
        BinItem other = clip(QStringLiteral("B"), 10);
        other.editRate = Rational(30);
        options.sync = GroupSync::SourceTimecode;
        QVERIFY(!groupClips(bin, inBin({a, other}), options, &error));
        // Waveform analysis without the sound's sync points.
        options.sync = GroupSync::Waveform;
        QVERIFY(!groupClips(bin, inBin({a, clip(QStringLiteral("B"), 10)}), options, &error));
    }

    // An angle plays as a clip of the group's frames: its link starts later or earlier by the offset, and frames
    // where it has no material are outside its range.
    void playsAnAngleAtTheGroupsFrames() {
        Bin bin;
        const BinItem a = clip(QStringLiteral("A"), 100, QStringLiteral("10:00:00:00"));
        const BinItem b = clip(QStringLiteral("B"), 100, QStringLiteral("10:00:02:00"));
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        BinItem group = *groupClips(bin, inBin({a, b}), options);
        const BinItem angleB = groupAngleClip(group, 1);
        QCOMPARE(angleB.id, QStringLiteral("B"));
        QCOMPARE(angleB.durationFrames, std::int64_t(150));
        QCOMPARE(angleB.link.value(QStringLiteral("first")).toInteger(), qint64(-50));
        QCOMPARE(groupAngleRange(angleB), (std::optional<std::pair<std::int64_t, std::int64_t>>{{50, 150}}));
        QCOMPARE(groupAngleRange(groupAngleClip(group, 0)), (std::optional<std::pair<std::int64_t, std::int64_t>>{{0, 100}}));
        QVERIFY(!groupAngleRange(a));
        // Out of range: the selected angle.
        auto g = *groupOf(group);
        g.selected = 1;
        setGroup(group, g);
        QCOMPARE(groupAngleClip(group, 7).id, QStringLiteral("B"));
        QCOMPARE(groupAngleClip(group, -1).id, QStringLiteral("B"));
        // "cycle through all the clips" (p.1131).
        QCOMPARE(stepAngle(1, 1, 2), 0);
        QCOMPARE(stepAngle(0, -1, 2), 1);
        QCOMPARE(stepAngle(2, -1, 9), 1);
    }

    // Segments from a group show its selected angle under the angle's name, keep it in the bin and play it.
    void editsKeepTheAngle() {
        Bin bin;
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        BinItem group = *groupClips(bin, inBin({clip(QStringLiteral("A"), 100), clip(QStringLiteral("B"), 100)}), options);
        auto g = *groupOf(group);
        g.selected = 1;
        setGroup(group, g);
        Sequence s = Sequence::withDefaultTracks();
        TrackPatch p;
        p.setSourceTracks(sourceTracksOf(group));
        applyEdit(s, EditKind::Overwrite, EditPlan{10, 0, 20, {}}, QStringLiteral("bin"), group, p);
        const Segment& v = s.track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(v.clipId, group.id);
        QCOMPARE(v.groupAngle, std::optional<int>(1));
        QCOMPARE(v.name, QStringLiteral("B"));
        QCOMPARE(clipForSegment(group, v).id, QStringLiteral("B"));
        // Kept with the sequence.
        const Sequence back = Sequence::fromJson(s.toJson());
        QCOMPARE(back.track(QStringLiteral("V1"))->segments.first().groupAngle, std::optional<int>(1));
        QCOMPARE(back.track(QStringLiteral("A1"))->segments.first().groupAngle, std::optional<int>(1));
        // And the group in its bin item.
        const auto again = binItemFromJson(binItemToJson(group));
        QVERIFY(again);
        QCOMPARE(groupOf(*again)->selected, 1);
        QCOMPARE(groupOf(*again)->angles[1].clip.name, QStringLiteral("B"));
    }

    // Waveform Analysis takes each clip's sync point from the sound (MainWindow's media::waveformOffset).
    void groupsByWaveform() {
        Bin bin;
        GroupOptions options;
        options.sync = GroupSync::Waveform;
        options.waveform = [](const BinItem& c) -> std::optional<std::int64_t> {
            if (c.id == QLatin1String("A")) return 0;
            if (c.id == QLatin1String("B")) return 15; // B's frame 15 sounds with A's frame 0
            return std::nullopt;
        };
        const auto group = groupClips(bin, inBin({clip(QStringLiteral("A"), 100), clip(QStringLiteral("B"), 100)}), options);
        QVERIFY(group);
        QCOMPARE(groupOf(*group)->angles[0].offset, std::int64_t(-15));
        QCOMPARE(groupOf(*group)->angles[1].offset, std::int64_t(0));
        QString error;
        QVERIFY(!groupClips(bin, inBin({clip(QStringLiteral("A"), 100), clip(QStringLiteral("C"), 100)}), options, &error));
        QVERIFY(error.contains(QLatin1String("sound")));
    }

    // Edit Group lays the angles out on their own tracks; Update Group reads them back, moved where the editor put
    // them, never shorter; a group of a plain sequence (Create Group).
    void editsAndUpdatesGroups() {
        Bin bin;
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        const BinItem a = clip(QStringLiteral("A"), 100, QStringLiteral("10:00:00:00"));
        const BinItem b = clip(QStringLiteral("B"), 100, QStringLiteral("10:00:02:00"));
        const BinItem c = clip(QStringLiteral("C"), 40, QStringLiteral("10:00:01:00"), QStringLiteral("A1-2")); // sound only
        const BinItem group = *groupClips(bin, inBin({a, b, c}), options);
        Sequence seq = groupEditSequence(QStringLiteral("bin"), group);
        QCOMPARE(seq.editedGroup(), (QStringList{"bin", group.id}));
        QVERIFY(seq.track(QStringLiteral("V1")) && seq.track(QStringLiteral("V2")));
        QVERIFY(!seq.track(QStringLiteral("V3"))); // C has no picture
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.size(), qsizetype(1));
        QCOMPARE(seq.track(QStringLiteral("V2"))->segments.at(0).length, std::int64_t(50)); // B 50 frames later: filler
        QCOMPARE(seq.track(QStringLiteral("V2"))->segments.at(1).clipId, QStringLiteral("B"));
        QCOMPARE(seq.track(QStringLiteral("A2"))->segments.last().clipId, QStringLiteral("B"));
        // Back again: the same offsets for the clips that are in the group's time.
        const GroupClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<BinItem> {
            for (const BinItem* i : {&a, &b, &c}) {
                if (i->id == id) return *i;
            }
            return std::nullopt;
        };
        auto back = groupFromSequence(seq, Rational(25), lookup, group.durationFrames);
        QVERIFY(back);
        QCOMPARE(back->first.angles[0].offset, std::int64_t(0));
        QCOMPARE(back->first.angles[1].offset, std::int64_t(-50));
        QCOMPARE(back->second, group.durationFrames);
        // The editor slides B 5 frames later: its offset follows.
        Track* v2 = seq.track(QStringLiteral("V2"));
        v2->segments[0].length += 5;
        back = groupFromSequence(seq, Rational(25), lookup, group.durationFrames);
        QCOMPARE(back->first.angles[1].offset, std::int64_t(-55));
        QCOMPARE(back->second, std::int64_t(155));
        // A sequence of one clip is no group.
        Sequence one;
        one.ensureTrack(QStringLiteral("V1"))->segments = seq.track(QStringLiteral("V1"))->segments;
        QString error;
        QVERIFY(!groupFromSequence(one, Rational(25), lookup, 0, &error));
        QVERIFY(!error.isEmpty());
    }

    // Refresh Sequence > Group Clips: the shown angle keeps its material; the segment takes the new offsets.
    void refreshesGroupEdits() {
        Bin bin;
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        BinItem group = *groupClips(bin, inBin({clip(QStringLiteral("A"), 100), clip(QStringLiteral("B"), 100)}), options);
        Sequence s = Sequence::withDefaultTracks();
        TrackPatch p;
        p.setSourceTracks(sourceTracksOf(group));
        applyEdit(s, EditKind::Overwrite, EditPlan{10, 0, 20, {}}, QStringLiteral("bin"), group, p);
        QCOMPARE(s.track(QStringLiteral("V1"))->segments.first().groupOffsets, (QList<std::int64_t>{0, 0}));
        // The group changes: A now 8 frames later than before.
        auto g = *groupOf(group);
        g.angles[0].offset = -8;
        setGroup(group, g);
        // Until refreshed the segment plays A by its own offsets.
        const Segment before = s.track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(groupAngleClip(group, 0, before.groupOffsets).link.value(QStringLiteral("first")).toInteger(), qint64(0));
        const GroupClipLookup lookup = [&](const QString&, const QString&) -> std::optional<BinItem> { return group; };
        QCOMPARE(refreshGroupEdits(s, lookup), 3); // V1, A1, A2
        const Segment after = s.track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(after.groupOffsets, (QList<std::int64_t>{-8, 0}));
        // The same clip frames: sourceIn + offset stays 10.
        QCOMPARE(after.sourceIn + after.groupOffsets[0], std::int64_t(10));
        QCOMPARE(refreshGroupEdits(s, lookup), 0);
    }

    // MultiCamera mode's cuts (p.1129-1132): the keypad's places, the group segment at a frame, and choosing an angle
    // with and without a cut.
    void multiCameraCuts() {
        QCOMPARE(keypadSplitPosition(7, 4), std::optional<int>(0));
        QCOMPARE(keypadSplitPosition(9, 4), std::optional<int>(1));
        QCOMPARE(keypadSplitPosition(1, 4), std::optional<int>(2));
        QCOMPARE(keypadSplitPosition(5, 4), std::optional<int>(3));
        QCOMPARE(keypadSplitPosition(7, 9), std::optional<int>(0));
        QCOMPARE(keypadSplitPosition(5, 9), std::optional<int>(4));
        QCOMPARE(keypadSplitPosition(3, 9), std::optional<int>(8));
        QVERIFY(!keypadSplitPosition(0, 4));
        Bin bin;
        GroupOptions options;
        options.sync = GroupSync::SourceTimecode;
        const BinItem group = *groupClips(bin, inBin({clip(QStringLiteral("A"), 100), clip(QStringLiteral("B"), 100)}), options);
        Sequence s = Sequence::withDefaultTracks();
        TrackPatch p;
        p.setSourceTracks(sourceTracksOf(group));
        applyEdit(s, EditKind::Overwrite, EditPlan{10, 0, 40, {}}, QStringLiteral("bin"), group, p);
        const auto at = groupSegmentAt(s, 15, {QStringLiteral("V2"), QStringLiteral("V1")});
        QVERIFY(at);
        QCOMPARE(at->track, QStringLiteral("V1"));
        QCOMPARE(at->groupFrame, std::int64_t(25));
        QVERIFY(!groupSegmentAt(s, 50, {QStringLiteral("V1")}));
        const GroupClip g = *groupOf(group);
        // Stopped: the whole segment switches.
        QVERIFY(setGroupAngleAt(s, QStringLiteral("V1"), 15, 1, false, g));
        QCOMPARE(s.track(QStringLiteral("V1"))->segments.size(), qsizetype(1));
        QCOMPARE(s.track(QStringLiteral("V1"))->segments.first().name, QStringLiteral("B"));
        QVERIFY(!setGroupAngleAt(s, QStringLiteral("V1"), 15, 1, false, g)); // already B
        QVERIFY(!setGroupAngleAt(s, QStringLiteral("V1"), 15, 5, false, g)); // no such angle
        // Playing: cut at 20, only what follows changes.
        QVERIFY(setGroupAngleAt(s, QStringLiteral("V1"), 20, 0, true, g));
        const Track* v1 = s.track(QStringLiteral("V1"));
        QCOMPARE(v1->segments.size(), qsizetype(2));
        QCOMPARE(v1->segments[0].groupAngle, std::optional<int>(1));
        QCOMPARE(v1->segments[0].length, std::int64_t(20));
        QCOMPARE(v1->segments[1].groupAngle, std::optional<int>(0));
        QCOMPARE(v1->segments[1].sourceIn, std::int64_t(30));
        // The audio stays as it was.
        QCOMPARE(s.track(QStringLiteral("A1"))->segments.size(), qsizetype(1));
    }
};

QTEST_GUILESS_MAIN(TestGroupClip)
#include "tst_group_clip.moc"
