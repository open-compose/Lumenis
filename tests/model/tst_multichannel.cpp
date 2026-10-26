// Multichannel audio tracks (model/multichannel.h; Editing Guide p.629, p.670-672, p.734-736;
// docs/spec/multichannel.md): track formats on clips, the source tracks they make, editing them onto
// tracks of their format, and Split Track to Mono.
#include "lumenis/model/bin.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"

#include <QtTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem clip(const char* tracks, const char* formats = "") {
    BinItem c;
    c.id = QStringLiteral("c1");
    c.name = QStringLiteral("Band");
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = 100;
    c.fields.insert(QStringLiteral("Tracks"), QString::fromLatin1(tracks));
    if (*formats) c.fields.insert(QStringLiteral("Track Formats"), QString::fromLatin1(formats));
    return c;
}

Segment material(const char* track, std::int64_t length = 10) {
    Segment s;
    s.clipId = QStringLiteral("c1");
    s.name = QStringLiteral("Band");
    s.sourceTrack = QString::fromLatin1(track);
    s.length = length;
    return s;
}

} // namespace

class TestMultichannel : public QObject {
    Q_OBJECT

private slots:
    void formats() {
        QCOMPARE(channelCount(AudioFormat::Mono), 1);
        QCOMPARE(channelCount(AudioFormat::Stereo), 2);
        QCOMPARE(channelCount(AudioFormat::Surround51), 6);
        QCOMPARE(channelCount(AudioFormat::Surround71), 8);
        // "Stereo A1A2" (p.671).
        const QList<ChannelGroup> groups{{AudioFormat::Stereo, 1}, {AudioFormat::Surround51, 3}};
        const QString text = trackFormatsText(groups);
        QCOMPARE(text, QStringLiteral("Stereo A1A2; 5.1 A3A4A5A6A7A8"));
        QCOMPARE(parseTrackFormats(text), std::optional(groups));
        QCOMPARE(parseTrackFormats(QStringLiteral("stereo a3a4")), std::optional(QList<ChannelGroup>{{AudioFormat::Stereo, 3}}));
        QVERIFY(!parseTrackFormats(QStringLiteral("Stereo A1A3")));
        QVERIFY(!parseTrackFormats(QStringLiteral("5.1 A1A2")));
        QVERIFY(!parseTrackFormats(QStringLiteral("Quad A1A2A3A4")));
        QCOMPARE(parseTrackFormats(QString()), std::optional(QList<ChannelGroup>{}));
    }

    void setFormats() {
        BinItem c = clip("V1 A1-8");
        QCOMPARE(audioChannelsOf(c), 8);
        QString error;
        QVERIFY(setTrackFormats(c, {{AudioFormat::Surround51, 3}, {AudioFormat::Stereo, 1}}, &error));
        QCOMPARE(c.fields.value(QStringLiteral("Track Formats")), QStringLiteral("Stereo A1A2; 5.1 A3A4A5A6A7A8"));
        // Overlaps, groups past the clip's tracks and stereo pairs off the pairs are refused.
        BinItem before = c;
        QVERIFY(!setTrackFormats(c, {{AudioFormat::Stereo, 1}, {AudioFormat::Stereo, 2}}, &error));
        QVERIFY(!setTrackFormats(c, {{AudioFormat::Surround71, 3}}, &error));
        QVERIFY(!setTrackFormats(c, {{AudioFormat::Stereo, 2}}, &error));
        QCOMPARE(c.fields, before.fields);
        // All mono: the column goes.
        QVERIFY(setTrackFormats(c, {}, &error));
        QVERIFY(!c.fields.contains(QStringLiteral("Track Formats")));
        // "Track formats for sequences, group clips, or subclips cannot be modified" (p.672).
        BinItem sub = clip("A1-2");
        sub.kind = BinItemKind::Subclip;
        QVERIFY(!setTrackFormats(sub, {{AudioFormat::Stereo, 1}}, &error));
    }

    void sourceTracks() {
        // With "Stereo A1A2", A1 is the pair and A2 the third channel.
        const BinItem c = clip("V1 A1-3", "Stereo A1A2");
        QCOMPARE(sourceTracksOf(c), (QStringList{"V1", "A1", "A2"}));
        const QList<SourceAudioTrack> a = sourceAudioTracks(c);
        QCOMPARE(a.size(), 2);
        QCOMPARE(a.at(0).format, AudioFormat::Stereo);
        QCOMPARE(a.at(1).first, 3);
        QCOMPARE(sourceFormat(c, QStringLiteral("A1")), AudioFormat::Stereo);
        QCOMPARE(sourceFormat(c, QStringLiteral("A2")), AudioFormat::Mono);
        QCOMPARE(sourceChannel(c, QStringLiteral("A2")), QStringLiteral("A3"));
        QCOMPARE(sourceChannel(c, QStringLiteral("V1")), QStringLiteral("V1"));
        // Without formats nothing changes.
        QCOMPARE(sourceTracksOf(clip("V1 A1-3")), (QStringList{"V1", "A1", "A2", "A3"}));
    }

    // Editing a stereo source track (p.629): onto a new track of its format, not onto a mono track.
    void edit() {
        const BinItem c = clip("A1-3", "Stereo A1A2");
        Sequence seq = Sequence::withDefaultTracks(); // V1, A1, A2 (mono)
        TrackPatch patch;
        patch.setSourceTracks(sourceTracksOf(c));
        // A1 (stereo) to A1 (mono) does not fit.
        QVERIFY(editFormatMismatch(seq, c, patch).contains(QLatin1String("stereo")));
        EditPlan plan;
        plan.length = 10;
        // The mismatched pair is left out; A2 (the third channel, mono) goes to A2.
        QCOMPARE(applyEdit(seq, EditKind::Overwrite, plan, QStringLiteral("bin"), c, patch), QStringList{QStringLiteral("A2")});
        QCOMPARE(seq.track(QStringLiteral("A2"))->segments.first().sourceTrack, QStringLiteral("A3"));
        // Patched to a new track A3, the pair makes it a stereo track holding channel A1 on.
        QVERIFY(patch.patch(QStringLiteral("A1"), QStringLiteral("A3")));
        patch.setSourceSelected(QStringLiteral("A2"), false);
        QCOMPARE(applyEdit(seq, EditKind::Overwrite, plan, QStringLiteral("bin"), c, patch), QStringList{QStringLiteral("A3")});
        const Track* a3 = seq.track(QStringLiteral("A3"));
        QCOMPARE(a3->format, AudioFormat::Stereo);
        QCOMPARE(a3->segments.first().sourceTrack, QStringLiteral("A1"));
        QVERIFY(editFormatMismatch(seq, c, patch).isEmpty());
    }

    // Split Track to Mono (p.734-736).
    void split() {
        Sequence seq;
        seq.ensureTrack(QStringLiteral("V1"));
        seq.ensureTrack(QStringLiteral("A1"));
        Track* stereo = seq.ensureTrack(QStringLiteral("A2"), AudioFormat::Stereo);
        stereo->segments = {Segment::filler(5), material("A3")};
        stereo->segments[1].volumeKeys = {{1, -6}};
        stereo->segments[1].panKeys = {{1, 20}};
        stereo->inserts[0] = TrackInsert{QStringLiteral("builtin.reverb"), QStringLiteral("Reverb"), {}, false};
        stereo->syncLocked = true;
        seq.ensureTrack(QStringLiteral("A3"))->segments = {material("A1", 20)};
        QString error;
        QVERIFY(!splitToMono(seq, QStringLiteral("A1"), &error)); // already mono
        QVERIFY(!error.isEmpty());
        QVERIFY(splitToMono(seq, QStringLiteral("A2"), &error));
        // A2 keeps the first channel, A3 the second, the old A3 moves to A4.
        QCOMPARE(seq.trackNames(), (QStringList{"V1", "A1", "A2", "A3", "A4"}));
        const Track* left = seq.track(QStringLiteral("A2"));
        const Track* right = seq.track(QStringLiteral("A3"));
        QCOMPARE(left->format, AudioFormat::Mono);
        QCOMPARE(right->format, AudioFormat::Mono);
        QCOMPARE(left->segments.at(1).sourceTrack, QStringLiteral("A3"));
        QCOMPARE(right->segments.at(1).sourceTrack, QStringLiteral("A4"));
        // Track effects go, volume automation stays, odd tracks pan left and even right.
        QVERIFY(!left->hasInserts());
        QCOMPARE(left->segments.at(1).volumeKeys.size(), 1);
        QVERIFY(left->segments.at(1).panKeys.isEmpty());
        QCOMPARE(left->segments.at(1).pan, kPanMax);   // A2: even
        QCOMPARE(right->segments.at(1).pan, -kPanMax); // A3: odd
        QVERIFY(left->syncLocked && right->syncLocked);
        QCOMPARE(seq.track(QStringLiteral("A4"))->segments.first().sourceTrack, QStringLiteral("A1"));

        // Split All: every multichannel track; none left is an error, with nothing changed.
        Sequence all;
        all.ensureTrack(QStringLiteral("A1"), AudioFormat::Surround51)->segments = {material("A1")};
        all.ensureTrack(QStringLiteral("A2"), AudioFormat::Stereo)->segments = {material("A7")};
        QCOMPARE(splitAllToMono(all, &error), 2);
        QCOMPARE(all.trackNames().size(), 8);
        QCOMPARE(all.track(QStringLiteral("A6"))->segments.first().sourceTrack, QStringLiteral("A6"));
        QCOMPARE(all.track(QStringLiteral("A8"))->segments.first().sourceTrack, QStringLiteral("A8"));
        const Sequence before = all;
        QCOMPARE(splitAllToMono(all, &error), 0);
        QVERIFY(!error.isEmpty());
        QCOMPARE(all.toJson(), before.toJson());
        // Segments move only between tracks of the same format (p.610-611).
        Sequence moves;
        moves.ensureTrack(QStringLiteral("A1"))->segments = {material("A1")};
        moves.ensureTrack(QStringLiteral("A2"), AudioFormat::Stereo)->segments = {material("A1")};
        moves.ensureTrack(QStringLiteral("A3"), AudioFormat::Stereo);
        QVERIFY(moveSegments(moves, {{QStringLiteral("A1"), 0}}, 0, 1, EditKind::Overwrite, false, &error).isEmpty());
        QVERIFY(error.contains(QLatin1String("formats")));
        QCOMPARE(moveSegments(moves, {{QStringLiteral("A2"), 0}}, 0, 1, EditKind::Overwrite, true, &error).size(), 1);
        // Tracks in sequences keep their format.
        Sequence back = Sequence::fromJson(seq.toJson());
        QCOMPARE(back.toJson(), seq.toJson());
    }
};

QTEST_GUILESS_MAIN(TestMultichannel)
#include "tst_multichannel.moc"
