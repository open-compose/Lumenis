// Volume and pan automation (lumenis/model/automation.h; Editing Guide p.714-723;
// docs/spec/automation.md): keyframe values and ramps, keyframes that follow the material through
// edits, and the Timeline's and the Audio Mixer's keyframe editing.
#include "lumenis/model/automation.h"
#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis::model;

namespace {

Segment clip(const QString& id, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = id;
    s.name = id;
    s.sourceTrack = QStringLiteral("A1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

// A1: 10 frames of filler, then 50 frames of clip "a" from its frame 100. A2: 60 frames of "b".
Sequence sequence() {
    Sequence seq = Sequence::withDefaultTracks();
    seq.splice(QStringLiteral("A1"), 0, {Segment::filler(10), clip(QStringLiteral("a"), 100, 50)});
    seq.splice(QStringLiteral("A2"), 0, {clip(QStringLiteral("b"), 0, 60)});
    return seq;
}

Segment at(const Sequence& seq, const QString& track, std::int64_t frame) {
    std::int64_t start = 0;
    const Track* t = seq.track(track);
    return t->segments.at(t->segmentAt(frame, &start));
}

} // namespace

class TestAutomation : public QObject {
    Q_OBJECT

private slots:
    void rampsAndValues() {
        // Linear between keyframes, held before the first and after the last (p.714).
        const QList<AudioKey> keys{{10, 0}, {20, -10}, {30, -10}};
        QVERIFY(!automationAt({}, 5));
        QCOMPARE(*automationAt(keys, 0), 0.0);
        QCOMPARE(*automationAt(keys, 15), -5.0);
        QCOMPARE(*automationAt(keys, 12.5), -2.5); // sub-frame (p.720)
        QCOMPARE(*automationAt(keys, 25), -10.0);
        QCOMPARE(*automationAt(keys, 99), -10.0);
        // -∞ ramps from the floor.
        const QList<AudioKey> fade{{0, 0}, {10, kGainOff}};
        QCOMPARE(*automationAt(fade, 5), kRampFloorDb / 2);

        // Volume automation adds to the clip gain (p.701); pan automation takes the clip pan's place.
        Segment s = clip(QStringLiteral("a"), 0, 50);
        s.gain = -3;
        s.pan = 40;
        QCOMPARE(volumeAt(s, 5), -3.0);
        QCOMPARE(panAt(s, 5, -100), 40.0);
        s.pan = 0;
        QCOMPARE(panAt(s, 5, -100), -100.0); // a centred clip pan follows the track's side
        s.volumeKeys = {{0, 0}, {10, -6}};
        s.panKeys = {{0, -50}, {10, 50}};
        QCOMPARE(volumeAt(s, 5), -6.0);
        QCOMPARE(panAt(s, 5, -100), 0.0);
        QCOMPARE(automationValue(s, AutomationParam::Volume, 5, 0), -3.0);
        s.volumeKeys = {{0, 0}, {10, kGainOff}};
        QCOMPARE(volumeAt(s, 10), kGainOff);
        QCOMPARE(volumeAt(s, 20), kGainOff);
        s.gain = kGainOff;
        s.volumeKeys = {{0, 6}};
        QCOMPARE(volumeAt(s, 0), kGainOff);
    }

    void addKeyframesHoldsTheSound() {
        Sequence seq = sequence();
        // On each track with material there; filler takes none.
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A1"), QStringLiteral("A2")}, 5, AutomationParam::Volume), 1);
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A1"), QStringLiteral("A2")}, 20, AutomationParam::Volume), 2);
        // In the clip's frames: record frame 20 of A1 is "a" frame 110.
        QCOMPARE(at(seq, QStringLiteral("A1"), 20).volumeKeys, (QList<AudioKey>{{110, 0}}));
        QCOMPARE(at(seq, QStringLiteral("A2"), 20).volumeKeys, (QList<AudioKey>{{5, 0}, {20, 0}}));
        // A second one there is not added.
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A1")}, 20, AutomationParam::Volume), 0);
        // A pan keyframe takes the pan that plays: A1's side, the left.
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A1")}, 30, AutomationParam::Pan), 1);
        QCOMPARE(at(seq, QStringLiteral("A1"), 30).panKeys, (QList<AudioKey>{{120, -100}}));
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A2")}, 30, AutomationParam::Pan, false), 1);
        QCOMPARE(at(seq, QStringLiteral("A2"), 30).panKeys, (QList<AudioKey>{{30, 0}}));
        // Between two keyframes it takes the ramp's value.
        setKeyframes(seq, {{QStringLiteral("A2"), 20}}, AutomationParam::Volume, -10);
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A2")}, 15.5, AutomationParam::Volume), 1);
        QCOMPARE(at(seq, QStringLiteral("A2"), 0).volumeKeys.at(1), (AudioKey{15.5, -7.0}));
        // Locked tracks are left alone.
        seq.track(QStringLiteral("A1"))->locked = true;
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A1")}, 40, AutomationParam::Volume), 0);
    }

    // Surround pan automation (p.679-680, p.715-716): keyframes on one channel's Front, Rear, Front and Rear and Center %
    // through the same operations, their values the pan that plays, kept with the sequence.
    void surroundPans() {
        QCOMPARE(surroundParam(1, SurroundAxis::Center), AutomationParam::CenterR);
        QCOMPARE(surroundParamOf(AutomationParam::FrontRearL)->axis, SurroundAxis::FrontRear);
        QVERIFY(!surroundParamOf(AutomationParam::Pan));
        QCOMPARE(automationKey(AutomationParam::RearR), QStringLiteral("R.rear"));
        QCOMPARE(automationFromKey(QStringLiteral("L.center")), AutomationParam::CenterL);
        QCOMPARE(automationFromKey(QStringLiteral("pan")), AutomationParam::Pan);
        QVERIFY(!automationFromKey(QStringLiteral("L.side")));
        QCOMPARE(clampAutomation(AutomationParam::CenterL, -5), 0.0);
        QCOMPARE(clampAutomation(AutomationParam::FrontL, -150), -100.0);

        Sequence seq = sequence();
        seq.setMixFormat(AudioFormat::Surround51);
        // A mono segment panned to the right: its default surround pan is there at the front (surroundPansOf).
        seq.track(QStringLiteral("A2"))->segments[0].pan = 60;
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A2")}, 10, AutomationParam::FrontL, false), 1);
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A2")}, 10, AutomationParam::FrontRearL, false), 1);
        Segment s = at(seq, QStringLiteral("A2"), 10);
        QCOMPARE(s.surroundKeys.size(), 1);
        QCOMPARE(s.surroundKeys.first().front, (QList<AudioKey>{{10, 60}}));
        QCOMPARE(s.surroundKeys.first().frontRear, (QList<AudioKey>{{10, 100}}));
        QVERIFY(s.hasAutomation());
        // To the rear by frame 30: halfway at 20.
        QCOMPARE(addKeyframes(seq, {QStringLiteral("A2")}, 30, AutomationParam::FrontRearL, false), 1);
        QCOMPARE(setKeyframes(seq, {{QStringLiteral("A2"), 30}}, AutomationParam::FrontRearL, -100), 1);
        s = at(seq, QStringLiteral("A2"), 10);
        QCOMPARE(surroundPanAt(s, AudioFormat::Mono, 60, 0, 20).frontRear, 0);
        QCOMPARE(surroundPanAt(s, AudioFormat::Mono, 60, 0, 40).frontRear, -100);
        QCOMPARE(surroundPanAt(s, AudioFormat::Mono, 60, 0, 40).front, 60);
        QCOMPARE(automationValue(s, AutomationParam::FrontRearL, 25, 0, AudioFormat::Mono), -50.0);
        // Center % holds 100 by default.
        QCOMPARE(automationValue(s, AutomationParam::CenterL, 25, 0, AudioFormat::Mono), 100.0);
        QCOMPARE(keyframesIn(seq, {QStringLiteral("A2")}, 0, 60, AutomationParam::FrontRearL).size(), 2);
        // Kept with the sequence.
        const Sequence back = Sequence::fromJson(seq.toJson());
        QCOMPARE(at(back, QStringLiteral("A2"), 10).surroundKeys, s.surroundKeys);
        // Removing them all leaves no empty channel behind.
        QVERIFY(removeAutomation(seq, {QStringLiteral("A2")}, 0, 60, AutomationParam::FrontRearL) > 0);
        QVERIFY(removeAutomation(seq, {QStringLiteral("A2")}, 0, 60, AutomationParam::FrontL) > 0);
        QVERIFY(at(seq, QStringLiteral("A2"), 10).surroundKeys.isEmpty());
        // A stereo segment's R channel.
        Sequence stereo;
        stereo.setMixFormat(AudioFormat::Surround51);
        stereo.ensureTrack(QStringLiteral("A1"), AudioFormat::Stereo)->segments = {clip(QStringLiteral("c"), 0, 20)};
        QCOMPARE(addKeyframes(stereo, {QStringLiteral("A1")}, 5, AutomationParam::RearR), 1);
        const Segment r = at(stereo, QStringLiteral("A1"), 5);
        QCOMPARE(r.surroundKeys.size(), 2);
        QVERIFY(r.surroundKeys.first().isEmpty());
        QCOMPARE(r.surroundKeys.at(1).rear, (QList<AudioKey>{{5, 100}}));
    }

    void keyframesFollowTheMaterial() {
        Sequence seq = sequence();
        addKeyframes(seq, {QStringLiteral("A1")}, 20, AutomationParam::Volume);
        addKeyframes(seq, {QStringLiteral("A1")}, 40, AutomationParam::Volume);
        // Splicing before it moves it later in the sequence.
        seq.splice(QStringLiteral("A1"), 0, {Segment::filler(5)});
        QCOMPARE(keyframesIn(seq, {QStringLiteral("A1")}, 0, 1000, AutomationParam::Volume),
                 (QList<KeyRef>{{QStringLiteral("A1"), 25}, {QStringLiteral("A1"), 45}}));
        // An Add Edit between them leaves each half with both keyframes, one hidden on each, and
        // the halves still join (a match frame).
        QVERIFY(seq.addEdit(QStringLiteral("A1"), 35));
        QCOMPARE(keyframesIn(seq, {QStringLiteral("A1")}, 0, 1000, AutomationParam::Volume).size(), 2);
        QCOMPARE(keyframesIn(seq, {QStringLiteral("A1")}, 0, 1000, AutomationParam::Volume, true).size(), 4);
        QCOMPARE(seq.removeMatchFrameEdits({QStringLiteral("A1")}, 0, 1000), 1);
        // Different keyframes keep an edit.
        Segment a = clip(QStringLiteral("a"), 0, 10);
        Segment b = clip(QStringLiteral("a"), 10, 10);
        b.volumeKeys = {{12, -3}};
        QVERIFY(!Sequence::isMatchFrame(a, b));
        // Saved and read back.
        const Sequence back = Sequence::fromJson(seq.toJson());
        QCOMPARE(back.toJson(), seq.toJson());
        QCOMPARE(at(back, QStringLiteral("A1"), 25).volumeKeys, (QList<AudioKey>{{110, 0}, {130, 0}}));
    }

    void moveSetAndDelete() {
        Sequence seq = sequence();
        for (double f : {15, 20, 30}) addKeyframes(seq, {QStringLiteral("A1"), QStringLiteral("A2")}, f, AutomationParam::Volume);
        const QList<KeyRef> both{{QStringLiteral("A1"), 20}, {QStringLiteral("A2"), 20}};
        // Dragging a keyframe moves the same one on another enabled track (p.716).
        QCOMPARE(setKeyframes(seq, both, AutomationParam::Volume, -12), 2);
        QCOMPARE(at(seq, QStringLiteral("A2"), 0).volumeKeys.at(1).value, -12.0);
        QCOMPARE(moveKeyframes(seq, both, AutomationParam::Volume, 0, 3), 2);
        QCOMPARE(at(seq, QStringLiteral("A1"), 20).volumeKeys.at(1).value, -9.0);
        // Alt-drag or nudge in time; it stays on the material and takes another's place.
        QCOMPARE(moveKeyframes(seq, {{QStringLiteral("A1"), 20}}, AutomationParam::Volume, 2.5, 0), 1);
        QCOMPARE(at(seq, QStringLiteral("A1"), 20).volumeKeys.at(1).frame, 112.5);
        moveKeyframes(seq, {{QStringLiteral("A1"), 22.5}}, AutomationParam::Volume, 7.5, 0);
        QCOMPARE(at(seq, QStringLiteral("A1"), 20).volumeKeys,
                 (QList<AudioKey>{{105, 0}, {120, -9}}));
        moveKeyframes(seq, {{QStringLiteral("A1"), 15}}, AutomationParam::Volume, -100, 0);
        QCOMPARE(at(seq, QStringLiteral("A1"), 20).volumeKeys.first().frame, 100.0);
        // The value is clamped.
        moveKeyframes(seq, {{QStringLiteral("A2"), 20}}, AutomationParam::Volume, 0, 100);
        QCOMPARE(at(seq, QStringLiteral("A2"), 0).volumeKeys.at(1).value, kGainMax);
        // Delete (p.717).
        QCOMPARE(deleteKeyframes(seq, {{QStringLiteral("A2"), 15}, {QStringLiteral("A2"), 30}}, AutomationParam::Volume), 2);
        QCOMPARE(at(seq, QStringLiteral("A2"), 0).volumeKeys.size(), 1);
    }

    void fastMenuCommands() {
        Sequence seq = sequence();
        // A2: keyframes at 0..20 on a straight line, and a peak at 10.
        Segment& b = seq.track(QStringLiteral("A2"))->segments[0];
        b.volumeKeys = {{0, 0}, {2, -2}, {4, -4}, {6, -6}, {8, -8}, {10, 0}, {12, -4}, {14, -8}, {16, -12}, {18, -16}, {20, -20}};
        b.panKeys = {{0, 10}};
        Sequence filtered = seq;
        // Points on straight lines go first, all of them: here all but the first, the last and the
        // turns.
        QCOMPARE(filterAutomation(filtered, {QStringLiteral("A2")}, 0, 60, AutomationParam::Volume, false), 7);
        QCOMPARE(at(filtered, QStringLiteral("A2"), 0).volumeKeys,
                 (QList<AudioKey>{{0, 0}, {8, -8}, {10, 0}, {20, -20}}));
        // On a curve, about half: every other one.
        Sequence curve = seq;
        curve.track(QStringLiteral("A2"))->segments[0].volumeKeys = {{0, 0}, {2, -1}, {4, -3}, {6, -6}, {8, -10}, {10, -15}, {12, -21}};
        QCOMPARE(filterAutomation(curve, {QStringLiteral("A2")}, 0, 60, AutomationParam::Volume, false), 3);
        QCOMPARE(at(curve, QStringLiteral("A2"), 0).volumeKeys,
                 (QList<AudioKey>{{0, 0}, {4, -3}, {8, -10}, {12, -21}}));
        // Alt: only the peaks.
        filtered = seq;
        filterAutomation(filtered, {QStringLiteral("A2")}, 0, 60, AutomationParam::Volume, true);
        QCOMPARE(at(filtered, QStringLiteral("A2"), 0).volumeKeys,
                 (QList<AudioKey>{{0, 0}, {8, -8}, {10, 0}, {20, -20}}));

        // Adjust: exactly -1 dB on those in the range (p.719).
        Sequence adjusted = seq;
        QCOMPARE(adjustAutomation(adjusted, {QStringLiteral("A2")}, 4, 9, AutomationParam::Volume, -1), 3);
        QCOMPARE(at(adjusted, QStringLiteral("A2"), 0).volumeKeys.at(2).value, -5.0);
        QCOMPARE(at(adjusted, QStringLiteral("A2"), 0).volumeKeys.at(1).value, -2.0);
        // A segment without keyframes gets one that sets it all.
        QCOMPARE(adjustAutomation(adjusted, {QStringLiteral("A1")}, 0, 60, AutomationParam::Volume, -1), 1);
        QCOMPARE(at(adjusted, QStringLiteral("A1"), 20).volumeKeys, (QList<AudioKey>{{100, -1}}));

        // Remove, in a range or everywhere; volume and pan apart.
        Sequence removedSeq = seq;
        QCOMPARE(removeAutomation(removedSeq, {QStringLiteral("A2")}, 0, 5, AutomationParam::Volume), 3);
        QCOMPARE(removeAutomation(removedSeq, {QStringLiteral("A2")}, 0, 60, AutomationParam::Pan), 1);
        QCOMPARE(at(removedSeq, QStringLiteral("A2"), 0).volumeKeys.size(), 8);
        QVERIFY(at(removedSeq, QStringLiteral("A2"), 0).panKeys.isEmpty());
    }

    void hiddenKeyframes() {
        Sequence seq = sequence();
        // A2 "b" frames 0..59 with a fade 0 -> -20 over its frames 0..20; trimmed to start at 10.
        Segment& b = seq.track(QStringLiteral("A2"))->segments[0];
        b.volumeKeys = {{0, 0}, {20, -20}, {70, 0}};
        b.sourceIn = 10;
        b.length = 50;
        QCOMPARE(keyframesIn(seq, {QStringLiteral("A2")}, 0, 100, AutomationParam::Volume).size(), 1);
        const double before = volumeAt(at(seq, QStringLiteral("A2"), 0), 10);
        const double lastBefore = volumeAt(at(seq, QStringLiteral("A2"), 0), 59);
        QCOMPARE(removeHiddenKeyframes(seq, {QStringLiteral("A2")}, 0, 100, AutomationParam::Volume), 2);
        // What plays at the edges stays, with keyframes put there.
        const Segment after = at(seq, QStringLiteral("A2"), 0);
        QCOMPARE(volumeAt(after, 10), before);
        QCOMPARE(volumeAt(after, 59), lastBefore);
        QCOMPARE(after.volumeKeys.size(), 3);
        QCOMPARE(after.volumeKeys.first().frame, 10.0);
    }

    void copyAndPaste() {
        Sequence seq = sequence();
        Segment& b = seq.track(QStringLiteral("A2"))->segments[0];
        b.volumeKeys = {{10, -3}, {14, -6}};
        b.panKeys = {{12, 30}};
        // Individual keyframes, from the earliest.
        const KeyClipboard some = copyKeyframes(seq, {{QStringLiteral("A2"), 14}}, {{QStringLiteral("A2"), 12}});
        QCOMPARE(some.tracks.size(), 1);
        QCOMPARE(some.tracks.first().volume, (QList<AudioKey>{{2, -6}}));
        QCOMPARE(some.tracks.first().pan, (QList<AudioKey>{{0, 30}}));
        // Pasted onto A1 at record frame 30: "a" frame 120 (pan) and 122 (volume).
        QCOMPARE(pasteKeyframes(seq, {QStringLiteral("A1")}, 30, some, true, true), 2);
        QCOMPARE(at(seq, QStringLiteral("A1"), 30).volumeKeys, (QList<AudioKey>{{122, -6}}));
        QCOMPARE(at(seq, QStringLiteral("A1"), 30).panKeys, (QList<AudioKey>{{120, 30}}));
        // A marked region (p.722), and only the gain chosen in the dialog; filler takes none.
        const KeyClipboard region = copyKeyframes(seq, {QStringLiteral("A2")}, 8, 20);
        QCOMPARE(region.tracks.first().volume, (QList<AudioKey>{{2, -3}, {6, -6}}));
        QCOMPARE(pasteKeyframes(seq, {QStringLiteral("A1")}, 5, region, true, false), 1); // frame 7 is filler
        QCOMPARE(at(seq, QStringLiteral("A1"), 30).volumeKeys, (QList<AudioKey>{{101, -6}, {122, -6}}));
        QVERIFY(KeyClipboard{}.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAutomation)
#include "tst_automation.moc"
