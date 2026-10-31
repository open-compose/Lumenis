// Mixed rate clips and Motion Adapters (Editing Guide p.468-478; docs/spec/motion-effects.md, 6.5d2):
// which clips are mixed rate, their adapted copies at the project's rate and back, the Field Motion
// attribute, adapters on edited segments and in sequence JSON, Promote and Refresh Sequence.
#include "lumenis/model/marker.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"

#include <QJsonObject>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

const Rational kProject(25);

// A 50 fps master clip of 100 frames, starting at 01:00:00:00, with a marker on frame 21.
BinItem fiftyClip() {
    BinItem item;
    item.id = QStringLiteral("clip");
    item.name = QStringLiteral("playtime");
    item.editRate = Rational(50);
    item.durationFrames = 100;
    item.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
    Marker m;
    m.id = QStringLiteral("m1");
    m.color = QStringLiteral("Red");
    m.track = QStringLiteral("V1");
    m.frame = 21;
    addMarker(item, m);
    return item;
}

MotionAdapter adapterOfClip(const BinItem& clip, bool blending = true) {
    return *adapterFor(clip, kProject, false, blending);
}

} // namespace

class TestMotionAdapter : public QObject {
    Q_OBJECT

private slots:
    void mixedRate() {
        // "Clips that do not match the frame rate ... of the project are known as mixed rate clips" (p.469).
        BinItem clip = fiftyClip();
        QVERIFY(isMixedRate(clip, kProject));
        QVERIFY(!isMixedRate(clip, Rational(50)));
        // A clip linked to 59.94 media runs at the project's rate, but its own frames are the media's.
        BinItem linked;
        linked.kind = BinItemKind::MasterClip;
        linked.editRate = kProject;
        linked.link = QJsonObject{{QStringLiteral("path"), QStringLiteral("/m/a.mov")}, {QStringLiteral("rate"), QStringLiteral("60000/1001")},
                                  {QStringLiteral("frames"), 300}, {QStringLiteral("videoStream"), 0}};
        QCOMPARE(clipFrameRate(linked), Rational(60000, 1001));
        QVERIFY(isMixedRate(linked, kProject));
        // A still and a sequence of images take the clip's rate.
        BinItem still = linked;
        still.link.insert(QStringLiteral("frames"), 1);
        QVERIFY(!isMixedRate(still, kProject));
        BinItem images = linked;
        images.link.insert(QStringLiteral("path"), QStringLiteral("/m/shot.%04d.png"));
        QVERIFY(!isMixedRate(images, kProject));
        // Sequences and sound-only clips show no adapter.
        BinItem seq = clip;
        seq.kind = BinItemKind::Sequence;
        QVERIFY(!isMixedRate(seq, kProject));
        BinItem sound = linked;
        sound.link.insert(QStringLiteral("videoStream"), -1);
        QVERIFY(!isMixedRate(sound, kProject));
    }

    void adaptedCopy() {
        const BinItem clip = fiftyClip();
        const MotionAdapter a = adapterOfClip(clip);
        QCOMPARE(a.clipRate, Rational(50));
        QCOMPARE(a.sequenceRate, kProject);
        const BinItem shown = adaptedClip(clip, a);
        // The same clip at the project's rate: its time is kept.
        QCOMPARE(shown.id, clip.id);
        QCOMPARE(shown.editRate, kProject);
        QCOMPARE(shown.durationFrames, 50);
        QCOMPARE(shown.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
        QCOMPARE(markersOf(shown).at(0).frame, 10); // frame 21 at 50 fps is shown on frame 10 at 25
        QVERIFY(isAdapted(shown));
        QCOMPARE(*adapterOf(shown), a);
        // Adapting it again changes nothing.
        QCOMPARE(adaptedClip(shown, a).durationFrames, 50);
        QCOMPARE(markersOf(adaptedClip(shown, a)).at(0).frame, 10);
        // Back in the bin it is its own clip again; an unmoved marker keeps its own frame.
        BinItem renamed = shown;
        renamed.name = QStringLiteral("playtime 2");
        const BinItem back = unadaptedClip(renamed, clip);
        QVERIFY(!isAdapted(back));
        QCOMPARE(back.editRate, Rational(50));
        QCOMPARE(back.durationFrames, 100);
        QCOMPARE(back.name, QStringLiteral("playtime 2"));
        QCOMPARE(markersOf(back).at(0).frame, 21);
        QVERIFY(!back.composition.contains(QStringLiteral("adaptedFrom")));
        // A marker added in the monitor lands on the clip's frame at its time.
        BinItem marked = shown;
        Marker m;
        m.id = QStringLiteral("m2");
        m.color = QStringLiteral("Blue");
        m.track = QStringLiteral("V1");
        m.frame = 30;
        addMarker(marked, m);
        const QList<Marker> markers = markersOf(unadaptedClip(marked, clip));
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers.at(1).frame, 60);
        // Without the bin's clip it converts back as well as it can.
        QCOMPARE(unadaptedClip(shown).durationFrames, 100);
        QCOMPARE(unadaptedClip(shown).editRate, Rational(50));
        // Other items come back as they are.
        QCOMPARE(binItemToJson(unadaptedClip(clip)), binItemToJson(clip));
    }

    void linkedAtProjectRate() {
        // A clip linked at the project's rate keeps its frames; the copy only carries the adapter.
        BinItem linked;
        linked.id = QStringLiteral("l");
        linked.kind = BinItemKind::MasterClip;
        linked.editRate = kProject;
        linked.durationFrames = 120;
        linked.link = QJsonObject{{QStringLiteral("path"), QStringLiteral("/m/a.mov")}, {QStringLiteral("rate"), QStringLiteral("60000/1001")},
                                  {QStringLiteral("frames"), 288}, {QStringLiteral("videoStream"), 0}};
        const BinItem shown = adaptedClip(linked, adapterOfClip(linked));
        QCOMPARE(shown.durationFrames, 120);
        QCOMPARE(shown.editRate, kProject);
        QCOMPARE(clipFrameRate(shown), Rational(60000, 1001));
        const BinItem back = unadaptedClip(shown);
        QCOMPARE(binItemToJson(back), binItemToJson(linked));
    }

    void frameConversions() {
        // Project frame 3 at 25 fps is at 0.12 s: frame 6 at 50 fps, frame 2 at 24 fps.
        QCOMPARE(clipFrameAt(3, Rational(50), kProject), 6);
        QCOMPARE(clipFrameAt(3, Rational(24), kProject), 2);
        QCOMPARE(projectFrameAt(6, Rational(50), kProject), 3);
        QCOMPARE(projectFrameAt(7, Rational(50), kProject), 4);
    }

    void fieldMotion() {
        // "When you create a clip or subclip, its Field Motion attribute is set to either Interlaced or
        // Progressive, depending on the project type" (p.472).
        BinItem clip = fiftyClip();
        QCOMPARE(clipFieldMotion(clip, false), FieldMotionAttribute::Progressive);
        QCOMPARE(clipFieldMotion(clip, true), FieldMotionAttribute::Interlaced);
        clip.fields.insert(QStringLiteral("Format"), QStringLiteral("1080i/25"));
        QCOMPARE(clipFieldMotion(clip, false), FieldMotionAttribute::Interlaced);
        clip.fields.insert(QLatin1String(kFieldMotionHeading), QStringLiteral("Progressive Strobe"));
        QCOMPARE(clipFieldMotion(clip, false), FieldMotionAttribute::ProgressiveStrobe);
        QCOMPARE(cellText(clip, QLatin1String(kFieldMotionHeading)), QStringLiteral("Progressive Strobe"));
        // "2:3 Film: Only available for clips or subclips created in 30 fps projects" (p.473).
        QVERIFY(!fieldMotionChoices(clip).contains(FieldMotionAttribute::Film23));
        clip.editRate = Rational(30000, 1001);
        QVERIFY(fieldMotionChoices(clip).contains(FieldMotionAttribute::Film23));
        QCOMPARE(fieldMotionFromText(QStringLiteral("2:3 Film")), FieldMotionAttribute::Film23);
        QVERIFY(!fieldMotionFromText(QStringLiteral("Upside down")));
        // A sequence has no Field Motion.
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        QVERIFY(cellText(seq, QLatin1String(kFieldMotionHeading)).isEmpty());
    }

    void defaultType() {
        // "When this option is selected, the Blended Interpolated mode is used ... When it is not
        // selected, the Interpolated Field mode is used for interlaced clips, and Both Fields mode is used
        // for progressive clips" (p.1215).
        QCOMPARE(defaultAdapterType(FieldMotionAttribute::Progressive, true), TimewarpType::BlendedInterpolated);
        QCOMPARE(defaultAdapterType(FieldMotionAttribute::Interlaced, false), TimewarpType::InterpolatedField);
        QCOMPARE(defaultAdapterType(FieldMotionAttribute::Progressive, false), TimewarpType::BothFields);
        QVERIFY(adapterBlends(adapterOfClip(fiftyClip(), true)));
        QVERIFY(!adapterBlends(adapterOfClip(fiftyClip(), false)));
    }

    void editsCarryTheAdapter() {
        // "Media Composer applies Motion Adapter effects ... when they are edited into a sequence" (p.469).
        const BinItem shown = adaptedClip(fiftyClip(), adapterOfClip(fiftyClip()));
        Sequence seq = Sequence::withDefaultTracks();
        EditMarks source;
        source.duration = shown.durationFrames;
        source.in = 10;
        source.out = 19;
        EditMarks record;
        record.duration = 0;
        const EditPlan plan = planEdit(source, record);
        TrackPatch patch;
        patch.setSourceTracks({QStringLiteral("V1")});
        applyEdit(seq, EditKind::Overwrite, plan, QStringLiteral("bin"), shown, patch);
        const Segment& s = seq.track(QStringLiteral("V1"))->segments.at(0);
        QVERIFY(s.adapter);
        QCOMPARE(s.adapter->clipRate, Rational(50));
        QCOMPARE(s.sourceIn, 10);
        QCOMPARE(s.length, 10);
        // Kept in the sequence's JSON.
        const Sequence again = Sequence::fromJson(seq.toJson());
        QCOMPARE(again.track(QStringLiteral("V1"))->segments.at(0).adapter, s.adapter);
        // The segment plays its clip's adapted copy; one without an adapter plays the clip.
        QCOMPARE(clipForSegment(fiftyClip(), s).editRate, kProject);
        Segment plain = s;
        plain.adapter.reset();
        QCOMPARE(clipForSegment(fiftyClip(), plain).editRate, Rational(50));
        // An edit from a clip at the sequence's rate has none.
        BinItem same = fiftyClip();
        same.editRate = kProject;
        Sequence other = Sequence::withDefaultTracks();
        applyEdit(other, EditKind::Overwrite, plan, QStringLiteral("bin"), same, patch);
        QVERIFY(!other.track(QStringLiteral("V1"))->segments.at(0).adapter);
    }

    void promote() {
        // "The motion adapter is promoted to a Timewarp effect" (p.472); removing the Timewarp leaves the
        // adapter ("Media Composer re-applies a Motion Adapter effect", p.469).
        Segment s;
        s.clipId = QStringLiteral("clip");
        s.length = 40;
        MotionAdapter a = adapterOfClip(fiftyClip());
        a.type = timewarpTypeName(TimewarpType::BlendedVtr);
        s.adapter = a;
        QVERIFY(promoteAdapter(s));
        QVERIFY(s.effect && isTimewarp(*s.effect));
        QCOMPARE(timewarpOf(*s.effect).type, TimewarpType::BlendedVtr);
        QCOMPARE(segmentSourceFrame(s, 7), 7); // real time: the segment counts the project's frames
        QVERIFY(!promoteAdapter(s));           // already an effect
        s.effect.reset();
        QVERIFY(s.adapter);
        Segment none;
        none.clipId = QStringLiteral("clip");
        QVERIFY(!promoteAdapter(none));
        QCOMPARE(adapterSpeed(a), 100.0);
    }

    void refreshSequence() {
        // "Refreshes the sequence so that media with Motion Adapters or Timewarps use the current Field
        // Motion attribute value from their source clip" (p.478).
        BinItem clip = fiftyClip();
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = clip.id;
        s.binId = QStringLiteral("bin");
        s.name = clip.name;
        s.sourceTrack = QStringLiteral("V1");
        s.length = 10;
        s.adapter = adapterOfClip(clip, false); // Progressive, Both Fields
        Segment chosen = s;
        chosen.adapter->type = timewarpTypeName(TimewarpType::VtrStyle);
        seq.overwrite(QStringLiteral("V1"), 0, {s, chosen});
        clip.fields.insert(QLatin1String(kFieldMotionHeading), QStringLiteral("Interlaced"));
        auto resolve = [&](const QString&, const QString&) -> std::optional<BinItem> { return clip; };
        QCOMPARE(refreshMotionAdapters(seq, resolve, false, false), 2);
        const QList<Segment>& segs = seq.track(QStringLiteral("V1"))->segments;
        QCOMPARE(segs.at(0).adapter->fieldMotion, QStringLiteral("Interlaced"));
        // The default Type follows; a chosen one stays.
        QCOMPARE(segs.at(0).adapter->type, timewarpTypeName(TimewarpType::InterpolatedField));
        QCOMPARE(segs.at(1).adapter->type, timewarpTypeName(TimewarpType::VtrStyle));
        QCOMPARE(refreshMotionAdapters(seq, resolve, false, false), 0);
    }

    void names() {
        // "the clip name displays as playtime (59.94 fps)" (p.470).
        QCOMPARE(mixedRateName(QStringLiteral("playtime"), Rational(60000, 1001)), QStringLiteral("playtime (59.94 fps)"));
        QCOMPARE(mixedRateName(QStringLiteral("a"), Rational(50)), QStringLiteral("a (50 fps)"));
    }
};

QTEST_GUILESS_MAIN(TestMotionAdapter)
#include "tst_motion_adapter.moc"
