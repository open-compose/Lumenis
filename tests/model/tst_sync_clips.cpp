// Sync Clips' model (9.3, the guide's AutoSync; Editing Guide p.496-498; docs/spec/sync-clips.md): syncing by source
// timecode, Auxiliary TC, Film TC/Sound TC and IN points, the subclip's range, name, timecode and tracks, the audio
// track options, the errors, and editing a synced subclip into a sequence.
#include "lumenis/model/sequence.h"
#include "lumenis/model/sync_clips.h"

#include <QJsonObject>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem clip(const QString& id, std::int64_t frames, const QString& start, const QString& tracks) {
    BinItem item;
    item.id = id;
    item.name = id;
    item.kind = tracks.contains(QLatin1Char('V')) ? BinItemKind::MasterClip : BinItemKind::AudioClip;
    item.editRate = Rational(25);
    item.durationFrames = frames;
    item.fields.insert(QStringLiteral("Start"), start);
    item.fields.insert(QStringLiteral("Tracks"), tracks);
    item.fields.insert(QStringLiteral("Tape"), id + QStringLiteral("_tape"));
    return item;
}

const Segment& only(const Sequence& s, const QString& track, int index = 0) { return s.track(track)->segments.at(index); }

} // namespace

class TestSyncClips : public QObject {
    Q_OBJECT

private slots:
    // "the later starting timecode becomes the start of the new subclip"; "the longer clip is truncated" (p.497).
    void bySourceTimecode() {
        Bin bin;
        const BinItem picture = clip(QStringLiteral("Take1"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("V1 A1-2"));
        const BinItem sound = clip(QStringLiteral("Sound1"), 200, QStringLiteral("01:00:00:10"), QStringLiteral("A1-2"));
        const SyncClipsResult r = syncClips(bin, QStringLiteral("bin"), {picture, sound}, {});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.subclips.size(), 1);
        const BinItem& s = r.subclips.first();
        QCOMPARE(s.kind, BinItemKind::Subclip);
        QVERIFY(isSyncedSubclip(s));
        // "named by default after the video clip with the file name extension .sync.n" (p.498).
        QCOMPARE(s.name, QStringLiteral("Take1.sync.1"));
        QCOMPARE(s.durationFrames, std::int64_t(90));
        QCOMPARE(s.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:10"));
        QCOMPARE(s.fields.value(QStringLiteral("End")), QStringLiteral("01:00:04:00"));
        QCOMPARE(s.fields.value(QStringLiteral("Tape")), QStringLiteral("Take1_tape"));
        QCOMPARE(sourceTracksOf(s), (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}));
        const Sequence seq = *composedSequence(s);
        QCOMPARE(only(seq, QStringLiteral("V1")).clipId, picture.id);
        QCOMPARE(only(seq, QStringLiteral("V1")).sourceIn, std::int64_t(10));
        // The video clip's own sound is left out by default: the sound clip's tracks.
        QCOMPARE(only(seq, QStringLiteral("A1")).clipId, sound.id);
        QCOMPARE(only(seq, QStringLiteral("A1")).sourceIn, std::int64_t(0));
        QCOMPARE(only(seq, QStringLiteral("A2")).sourceTrack, QStringLiteral("A2"));
        QCOMPARE(only(seq, QStringLiteral("A2")).length, std::int64_t(90));
        QCOMPARE(s.sources.size(), 2);
        // A second sync of the same clip takes the next number.
        bin.items << s;
        QCOMPARE(syncClips(bin, QStringLiteral("bin"), {picture, sound}, {}).subclips.first().name, QStringLiteral("Take1.sync.2"));
    }

    void audioTrackOptions() {
        Bin bin;
        const BinItem picture = clip(QStringLiteral("Take1"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("V1 A1-2"));
        const BinItem sound = clip(QStringLiteral("Sound1"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("A1-4"));
        // "Keep audio on clip with video": its A1-A2 first, then the sound clip's tracks.
        SyncClipsOptions o;
        o.keepVideoAudio = std::pair{1, 2};
        Sequence seq = *composedSequence(syncClips(bin, QStringLiteral("bin"), {picture, sound}, o).subclips.first());
        QCOMPARE(only(seq, QStringLiteral("A1")).clipId, picture.id);
        QCOMPARE(only(seq, QStringLiteral("A2")).clipId, picture.id);
        QCOMPARE(only(seq, QStringLiteral("A3")).clipId, sound.id);
        QCOMPARE(only(seq, QStringLiteral("A6")).sourceTrack, QStringLiteral("A4"));
        // "Include audio from audio-only clips" A3-A4 only: they stay at their places, the tracks before them empty.
        o = {};
        o.audioOnlyTracks = std::pair{3, 4};
        seq = *composedSequence(syncClips(bin, QStringLiteral("bin"), {picture, sound}, o).subclips.first());
        QCOMPARE(only(seq, QStringLiteral("A1")).sourceTrack, QStringLiteral("A3"));
        QCOMPARE(only(seq, QStringLiteral("A2")).sourceTrack, QStringLiteral("A4"));
        // With the video clip's A2 only and the sound clip's A2-A3: A1 has no sound; "Collapse Audio Tracks" removes it.
        o = {};
        o.keepVideoAudio = std::pair{2, 2};
        o.audioOnlyTracks = std::pair{2, 3};
        seq = *composedSequence(syncClips(bin, QStringLiteral("bin"), {picture, sound}, o).subclips.first());
        QCOMPARE(only(seq, QStringLiteral("A1")).sourceTrack, QStringLiteral("A2"));
        QCOMPARE(only(seq, QStringLiteral("A2")).sourceTrack, QStringLiteral("A2"));
        QCOMPARE(only(seq, QStringLiteral("A2")).clipId, sound.id);
        o.keepVideoAudio = std::pair{1, 2};
        BinItem silentFirst = picture;
        silentFirst.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A2"));
        seq = *composedSequence(syncClips(bin, QStringLiteral("bin"), {silentFirst, sound}, o).subclips.first());
        QVERIFY(seq.track(QStringLiteral("A1"))->segments.isEmpty());
        o.collapseAudio = true;
        seq = *composedSequence(syncClips(bin, QStringLiteral("bin"), {silentFirst, sound}, o).subclips.first());
        QCOMPARE(only(seq, QStringLiteral("A1")).clipId, picture.id);
        QCOMPARE(only(seq, QStringLiteral("A2")).clipId, sound.id);
    }

    void otherMethods() {
        Bin bin;
        BinItem picture = clip(QStringLiteral("Take1"), 100, QStringLiteral("10:00:00:00"), QStringLiteral("V1"));
        BinItem sound = clip(QStringLiteral("Sound1"), 100, QStringLiteral("20:00:00:00"), QStringLiteral("A1"));
        // Auxiliary TC2.
        picture.fields.insert(QStringLiteral("Auxiliary TC2"), QStringLiteral("05:00:00:20"));
        sound.fields.insert(QStringLiteral("Auxiliary TC2"), QStringLiteral("05:00:00:00"));
        SyncClipsOptions o;
        o.sync.sync = GroupSync::AuxTimecode;
        o.sync.auxTc = 2;
        SyncClipsResult r = syncClips(bin, QStringLiteral("bin"), {picture, sound}, o);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.subclips.first().durationFrames, std::int64_t(80));
        QCOMPARE(only(*composedSequence(r.subclips.first()), QStringLiteral("A1")).sourceIn, std::int64_t(20));
        // Film TC/Sound TC: the video clip's Film TC with the sound clip's Sound TC.
        picture.fields.insert(QStringLiteral("Film TC"), QStringLiteral("02:00:00:00"));
        sound.fields.insert(QStringLiteral("Sound TC"), QStringLiteral("02:00:00:05"));
        o.sync.sync = GroupSync::FilmTc;
        r = syncClips(bin, QStringLiteral("bin"), {picture, sound}, o);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(only(*composedSequence(r.subclips.first()), QStringLiteral("V1")).sourceIn, std::int64_t(5));
        // In points: "the whole clip is taken into the subclip" (p.497), sound where the audio clip has it.
        o.sync.sync = GroupSync::Inpoints;
        o.sync.marks = [](const BinItem& c, bool in) -> std::optional<std::int64_t> {
            if (!in) return std::nullopt;
            return c.id == QLatin1String("Take1") ? 30 : 10; // the slate's clap
        };
        r = syncClips(bin, QStringLiteral("bin"), {picture, sound}, o);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.subclips.first().durationFrames, std::int64_t(100));
        const Sequence seq = *composedSequence(r.subclips.first());
        QCOMPARE(only(seq, QStringLiteral("V1")).sourceIn, std::int64_t(0));
        QVERIFY(only(seq, QStringLiteral("A1")).isFiller());
        QCOMPARE(only(seq, QStringLiteral("A1")).length, std::int64_t(20));
        QCOMPARE(only(seq, QStringLiteral("A1"), 1).sourceIn, std::int64_t(0));
        QCOMPARE(only(seq, QStringLiteral("A1"), 1).length, std::int64_t(80));
    }

    void errors() {
        Bin bin;
        const BinItem a = clip(QStringLiteral("Take1"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("V1"));
        const BinItem b = clip(QStringLiteral("Take2"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("V1"));
        // "You can autosync audio clips with video clips only."
        QVERIFY(!syncClips(bin, QStringLiteral("bin"), {a, b}, {}).ok());
        const BinItem late = clip(QStringLiteral("Sound"), 50, QStringLiteral("02:00:00:00"), QStringLiteral("A1"));
        const SyncClipsResult apart = syncClips(bin, QStringLiteral("bin"), {a, late}, {});
        QVERIFY(!apart.ok());
        QVERIFY(apart.error.contains(QLatin1String("overlap")));
        SyncClipsOptions o;
        o.sync.sync = GroupSync::Outpoints;
        const SyncClipsResult unmarked = syncClips(bin, QStringLiteral("bin"), {a, late}, o);
        QVERIFY(unmarked.error.contains(QLatin1String("OUT point")));
    }

    // Edited into a sequence, the synced subclip gives its clips' segments, picture and sound in sync.
    void editsAsItsClips() {
        Bin bin;
        const BinItem picture = clip(QStringLiteral("Take1"), 100, QStringLiteral("01:00:00:00"), QStringLiteral("V1"));
        const BinItem sound = clip(QStringLiteral("Sound1"), 200, QStringLiteral("01:00:00:10"), QStringLiteral("A1"));
        const BinItem synced = syncClips(bin, QStringLiteral("bin"), {picture, sound}, {}).subclips.first();
        Sequence s = Sequence::withDefaultTracks();
        TrackPatch p;
        p.setSourceTracks(sourceTracksOf(synced));
        applyEdit(s, EditKind::Overwrite, EditPlan{5, 0, 20, {}}, QStringLiteral("bin"), synced, p);
        QCOMPARE(only(s, QStringLiteral("V1")).clipId, picture.id);
        QCOMPARE(only(s, QStringLiteral("V1")).sourceIn, std::int64_t(15));
        QCOMPARE(only(s, QStringLiteral("A1")).clipId, sound.id);
        QCOMPARE(only(s, QStringLiteral("A1")).sourceIn, std::int64_t(5));
        // Kept in bins.
        const auto back = binItemFromJson(binItemToJson(synced));
        QVERIFY(back && isSyncedSubclip(*back));
        QVERIFY(!composedSequence(picture));
    }
};

QTEST_MAIN(TestSyncClips)
#include "tst_sync_clips.moc"
