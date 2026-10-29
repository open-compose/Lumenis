// The compositor (ROADMAP Phase 6.1; docs/spec/compositor.md): tracks bottom to top, filler showing
// through, the dissolve family of transitions and Superimpose, effects processing precision, and
// offline media. Clips are solid colours, one per clip id, so every pixel is known.
#include "lumenis/media/compositor.h"
#include "lumenis/model/sequence.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::media;
using namespace lumenis::model;

namespace {

const QSize kSize(16, 9);

Segment clip(const QString& id, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = id;
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

QColor colourOf(const QString& id) {
    if (id == QLatin1String("red")) return QColor(255, 0, 0);
    if (id == QLatin1String("blue")) return QColor(0, 0, 255);
    if (id == QLatin1String("white")) return QColor(255, 255, 255);
    if (id == QLatin1String("grey")) return QColor(128, 128, 128);
    return QColor(0, 255, 0);
}

Composition composition(const Sequence& seq, QStringList tracks) {
    Composition c;
    c.sequence = &seq;
    c.tracks = std::move(tracks);
    c.size = kSize;
    c.precision = EffectPrecision::Bits32;
    c.source = [](const Segment& s, std::int64_t, QSize size) -> std::optional<QImage> {
        if (s.clipId == QLatin1String("offline")) return std::nullopt;
        QImage image(size, QImage::Format_RGB32);
        image.fill(colourOf(s.clipId));
        return image;
    };
    return c;
}

QColor centre(const QImage& image) {
    return image.pixelColor(image.width() / 2, image.height() / 2);
}

bool near(const QColor& a, const QColor& b, int tolerance = 2) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

Transition transition(const QString& name, std::int64_t duration) {
    Transition t;
    t.effect.category = QStringLiteral("Blend");
    t.effect.name = name;
    t.duration = duration;
    t.alignment = TransitionAlignment::CenteredOnCut;
    return t;
}

// V1: red (10) | blue (10), a transition of 4 frames centred on the cut at 10 (frames 8 to 11).
Sequence cutWith(const Transition& t, const QString& a = QStringLiteral("red"), const QString& b = QStringLiteral("blue")) {
    Sequence seq = Sequence::withDefaultTracks();
    seq.splice(QStringLiteral("V1"), 0, {clip(a, 10), clip(b, 10)});
    seq.track(QStringLiteral("V1"))->segments[1].transition = t;
    return seq;
}

} // namespace

class TestCompositor : public QObject {
    Q_OBJECT

private slots:
    void tracksBottomToTop() {
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 20)});
        seq.splice(QStringLiteral("V2"), 0, {Segment::filler(5), clip(QStringLiteral("blue"), 5)});
        const Composition c = composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")});
        // V2's filler shows V1; its clip hides it.
        QCOMPARE(centre(composeImage(c, 2)), QColor(255, 0, 0));
        QCOMPARE(centre(composeImage(c, 7)), QColor(0, 0, 255));
        // Only V1 shown (V2 not monitored).
        QCOMPARE(centre(composeImage(composition(seq, {QStringLiteral("V1")}), 7)), QColor(255, 0, 0));
        // Past every track's end: black.
        QCOMPARE(centre(composeImage(c, 30)), QColor(0, 0, 0));
        // The same frames through the full composite.
        QCOMPARE(centre(composeFrame(c, 7).toImage()), QColor(0, 0, 255));
    }

    void trackFrames() {
        const Sequence seq = cutWith(transition(QStringLiteral("Dissolve"), 4));
        const auto before = trackFrame(seq, QStringLiteral("V1"), 7);
        QVERIFY(before && !before->transition);
        QCOMPARE(before->frame, 7);
        // Frame 8 is the first of the transition: outgoing frame 8, incoming frame -2 (handle).
        const auto first = trackFrame(seq, QStringLiteral("V1"), 8);
        QVERIFY(first && first->transition);
        QCOMPARE(first->outgoing->clipId, QStringLiteral("red"));
        QCOMPARE(first->outgoingFrame, 8);
        QCOMPARE(first->segment->clipId, QStringLiteral("blue"));
        QCOMPARE(first->frame, -2);
        QCOMPARE(first->progress, 0.2);
        const auto last = trackFrame(seq, QStringLiteral("V1"), 11);
        QVERIFY(last && last->transition);
        QCOMPARE(last->progress, 0.8);
        QVERIFY(!trackFrame(seq, QStringLiteral("V1"), 12)->transition);
    }

    void dissolveFamily() {
        // Dissolve: outgoing to incoming (FX Guide p.457).
        const Sequence dissolve = cutWith(transition(QStringLiteral("Dissolve"), 4));
        const Composition c = composition(dissolve, {QStringLiteral("V1")});
        QVERIFY(near(centre(composeImage(c, 8)), QColor(204, 0, 51)));
        QVERIFY(near(centre(composeImage(c, 11)), QColor(51, 0, 204)));
        // Fade to Color: outgoing to the colour, then a cut to the incoming video.
        Transition toWhite = transition(QStringLiteral("Fade to Color"), 4);
        toWhite.effect.parameters = QJsonObject{
            {QStringLiteral("Background Color"), QJsonObject{{QStringLiteral("Background Color"), QStringLiteral("#ffffff")}}}};
        const Sequence fadeTo = cutWith(toWhite);
        QVERIFY(near(centre(composeImage(composition(fadeTo, {QStringLiteral("V1")}), 11)), QColor(255, 204, 204)));
        // Fade from Color: black (the default colour) to the incoming video.
        const Sequence fadeFrom = cutWith(transition(QStringLiteral("Fade from Color"), 4));
        QVERIFY(near(centre(composeImage(composition(fadeFrom, {QStringLiteral("V1")}), 8)), QColor(0, 0, 51)));
        // Dip to Color: through the colour at the middle.
        const Sequence dip = cutWith(transition(QStringLiteral("Dip to Color"), 3));
        // Three frames centred: 9, 10, 11 at 1/4, 2/4, 3/4; the middle is all colour (black).
        QVERIFY(near(centre(composeImage(composition(dip, {QStringLiteral("V1")}), 10)), QColor(0, 0, 0)));
        QVERIFY(near(centre(composeImage(composition(dip, {QStringLiteral("V1")}), 9)), QColor(128, 0, 0)));
        // Swap Sources (p.495): the incoming video first.
        Transition swapped = transition(QStringLiteral("Dissolve"), 4);
        swapped.effect.parameters =
            QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Swap Sources"), true}}}};
        QVERIFY(near(centre(composeImage(composition(cutWith(swapped), {QStringLiteral("V1")}), 8)), QColor(51, 0, 204)));
    }

    void filmDissolve() {
        // Ours: blended in density. Between two equal pictures nothing changes; from white to grey the
        // middle is darker than a plain dissolve's.
        const Sequence same = cutWith(transition(QStringLiteral("Film Dissolve"), 3), QStringLiteral("white"), QStringLiteral("white"));
        QVERIFY(near(centre(composeImage(composition(same, {QStringLiteral("V1")}), 10)), QColor(255, 255, 255)));
        const Sequence film = cutWith(transition(QStringLiteral("Film Dissolve"), 3), QStringLiteral("white"), QStringLiteral("grey"));
        const Sequence plain = cutWith(transition(QStringLiteral("Dissolve"), 3), QStringLiteral("white"), QStringLiteral("grey"));
        const int filmMid = centre(composeImage(composition(film, {QStringLiteral("V1")}), 10)).red();
        const int plainMid = centre(composeImage(composition(plain, {QStringLiteral("V1")}), 10)).red();
        QVERIFY(filmMid < plainMid);
        QVERIFY(filmMid > 128);
    }

    void transitionToFillerOnAnUpperTrack() {
        // A dissolve from a clip on V2 to filler fades the clip out over V1.
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 20)});
        seq.splice(QStringLiteral("V2"), 0, {clip(QStringLiteral("blue"), 10), Segment::filler(10)});
        seq.track(QStringLiteral("V2"))->segments[1].transition = transition(QStringLiteral("Dissolve"), 4);
        const QColor mid = centre(composeImage(composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")}), 9));
        // Progress 2/5: 60% blue over red.
        QVERIFY(near(mid, QColor(102, 0, 153)));
    }

    void superimpose() {
        // Superimpose on V2 at Level 25: a quarter of the top over the bottom (p.458).
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        Segment top = clip(QStringLiteral("blue"), 10);
        top.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Superimpose"),
                            QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Level"), 25}}}}};
        seq.splice(QStringLiteral("V2"), 0, {top});
        QVERIFY(near(centre(composeImage(composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")}), 5)), QColor(191, 0, 64)));
        // Without a level: half and half (ours).
        seq.track(QStringLiteral("V2"))->segments[0].effect->parameters = {};
        QVERIFY(near(centre(composeImage(composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")}), 5)), QColor(128, 0, 128)));
    }

    void notBuiltYetIsACut() {
        // A wipe comes with its step; until then it is a cut at the cut point.
        Transition box = transition(QStringLiteral("Box Wipe"), 4);
        box.effect.category = QStringLiteral("Box Wipe");
        const Sequence seq = cutWith(box);
        const Composition c = composition(seq, {QStringLiteral("V1")});
        QCOMPARE(centre(composeImage(c, 9)), QColor(255, 0, 0));
        QCOMPARE(centre(composeImage(c, 10)), QColor(0, 0, 255));
    }

    void precision() {
        // 8-bit processing rounds each result to 1/255.
        Picture p = Picture::solid(1, 1, QColor(0, 0, 0));
        p.data()[0] = 0.5004f;
        p.quantize(EffectPrecision::Bits8);
        QCOMPARE(p.data()[0], 128.0f / 255.0f);
        p.data()[0] = 0.5004f;
        p.quantize(EffectPrecision::Bits32);
        QCOMPARE(p.data()[0], 0.5004f);
    }

    void offlineMedia() {
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("offline"), 10)});
        bool missing = false;
        QVERIFY(composeImage(composition(seq, {QStringLiteral("V1")}), 3, &missing).isNull());
        QVERIFY(missing);
    }
};

QTEST_GUILESS_MAIN(TestCompositor)
#include "tst_compositor.moc"
