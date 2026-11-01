// The compositor (ROADMAP Phase 6.1; docs/spec/compositor.md): tracks bottom to top, filler showing
// through, the dissolve family of transitions and Superimpose, the 2D transitions (wipes.cpp), effects
// processing precision, and offline media. Clips are solid colours, one per clip id, so every pixel is known.
#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"

#include <QJsonArray>
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

    void keyframedLevels() {
        // A transition's keyframed Foreground Level is its progress (FX Guide p.94), keyframes placed in
        // the transition's progress (ours): held at 0 for its first half, then up to 100. Frames 8 to 11
        // are at 0.2, 0.4, 0.6 and 0.8.
        Transition held = transition(QStringLiteral("Dissolve"), 4);
        setKeyframe(held.effect, QStringLiteral("Foreground"), QStringLiteral("Level"), 0.0, 4, 0);
        setKeyframe(held.effect, QStringLiteral("Foreground"), QStringLiteral("Level"), 0.5, 4, 0);
        setKeyframe(held.effect, QStringLiteral("Foreground"), QStringLiteral("Level"), 1.0, 4, 100);
        const Sequence seq = cutWith(held);
        const Composition c = composition(seq, {QStringLiteral("V1")});
        QVERIFY(near(centre(composeImage(c, 9)), QColor(255, 0, 0)));
        QVERIFY(near(centre(composeImage(c, 10)), QColor(204, 0, 51)));
        QVERIFY(near(centre(composeImage(c, 11)), QColor(102, 0, 153)));
        // A segment effect's Level, as the Fade Effect button keys it (p.46): Superimpose fading up
        // over V1 across its 11 frames.
        Sequence two = Sequence::withDefaultTracks();
        two.addTrack(TrackKind::Video);
        two.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 11)});
        Segment top = clip(QStringLiteral("blue"), 11);
        top.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Superimpose"), {}};
        fadeEffect(*top.effect, 11, 10, 0);
        two.splice(QStringLiteral("V2"), 0, {top});
        const Composition both = composition(two, {QStringLiteral("V1"), QStringLiteral("V2")});
        QVERIFY(near(centre(composeImage(both, 0)), QColor(255, 0, 0)));
        QVERIFY(near(centre(composeImage(both, 5)), QColor(128, 0, 128)));
        QVERIFY(near(centre(composeImage(both, 10)), QColor(0, 0, 255)));
    }

    void notBuiltYetIsACut() {
        // A Peel comes with its step (6.7); until then it is a cut at the cut point.
        Transition peel = transition(QStringLiteral("Bottom to Top"), 4);
        peel.effect.category = QStringLiteral("Peel");
        const Sequence seq = cutWith(peel);
        const Composition c = composition(seq, {QStringLiteral("V1")});
        QCOMPARE(centre(composeImage(c, 9)), QColor(255, 0, 0));
        QCOMPARE(centre(composeImage(c, 10)), QColor(0, 0, 255));
    }

    void wipeShapes() {
        // The 2D transitions (docs/spec/wipes.md) at given progress, red going out and blue coming in.
        const Picture red = Picture::solid(64, 36, QColor(255, 0, 0));
        const Picture blue = Picture::solid(64, 36, QColor(0, 0, 255));
        auto at = [&](const QString& category, const QString& name, double p, int x, int y, QJsonObject params = {}) {
            const Effect e{category, name, std::move(params)};
            return wipePicture(e, red, blue, p, p, EffectSpan(10, true)).toImage().pixelColor(x, y);
        };
        const QColor r(255, 0, 0), b(0, 0, 255), black(0, 0, 0);
        // Edge Wipe Horizontal, left to right (p.460).
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 10, 18), b);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 50, 18), r);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Vertical Open"), 0.5, 5, 18), b);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Vertical Open"), 0.5, 5, 2), r);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Upper Left Diagonal"), 0.5, 5, 5), b);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Upper Left Diagonal"), 0.5, 60, 32), r);
        // Box Wipe from its corner (p.459).
        const QString box = QStringLiteral("Box Wipe");
        QCOMPARE(at(box, QStringLiteral("Top Left to Bottom Right"), 0.5, 10, 5), b);
        QCOMPARE(at(box, QStringLiteral("Top Left to Bottom Right"), 0.5, 50, 5), r);
        QCOMPARE(at(box, QStringLiteral("Top Left to Bottom Right"), 0.5, 50, 30), r);
        QCOMPARE(at(box, QStringLiteral("Bottom Box"), 0.5, 32, 30), b);
        QCOMPARE(at(box, QStringLiteral("Bottom Box"), 0.5, 5, 30), r);
        QCOMPARE(at(box, QStringLiteral("Bottom Box"), 1.0, 0, 0), b);
        QCOMPARE(at(box, QStringLiteral("Bottom Box"), 0.0, 32, 35), r);
        // Shape Wipes (p.481-482).
        const QString shape = QStringLiteral("Shape Wipe");
        QCOMPARE(at(shape, QStringLiteral("Circle"), 0.5, 32, 18), b);
        QCOMPARE(at(shape, QStringLiteral("Circle"), 0.5, 1, 1), r);
        QCOMPARE(at(shape, QStringLiteral("Clock"), 0.25, 48, 9), b);
        QCOMPARE(at(shape, QStringLiteral("Clock"), 0.25, 16, 27), r);
        QCOMPARE(at(shape, QStringLiteral("4 Corners"), 0.5, 3, 3), b);
        QCOMPARE(at(shape, QStringLiteral("4 Corners"), 0.5, 60, 33), b);
        QCOMPARE(at(shape, QStringLiteral("4 Corners"), 0.5, 32, 18), r);
        QCOMPARE(at(shape, QStringLiteral("Diamond"), 0.5, 32, 18), b);
        QCOMPARE(at(shape, QStringLiteral("Diamond"), 0.5, 2, 2), r);
        QCOMPARE(at(shape, QStringLiteral("Horizontal Bands"), 0.25, 3, 2), b);  // the first band from the left
        QCOMPARE(at(shape, QStringLiteral("Horizontal Bands"), 0.25, 3, 8), r);  // the second from the right
        QCOMPARE(at(shape, QStringLiteral("Horizontal Bands"), 0.25, 60, 8), b);
        // Matrix One-WayRow on the default 8 x 6 grid (p.494): half way, the top three rows are in.
        const QString matrix = QStringLiteral("Matrix Wipe");
        QCOMPARE(at(matrix, QStringLiteral("One-WayRow"), 0.5, 60, 15), b);
        QCOMPARE(at(matrix, QStringLiteral("One-WayRow"), 0.5, 4, 20), r);
        // A Custom Grid of 2 x 2: half way, the top two cells.
        const QJsonObject grid{{QStringLiteral("Custom Grid"), QJsonObject{{QStringLiteral("Columns"), 2}, {QStringLiteral("Rows"), 2}}}};
        QCOMPARE(at(matrix, QStringLiteral("One-WayRow"), 0.5, 40, 5, grid), b);
        QCOMPARE(at(matrix, QStringLiteral("One-WayRow"), 0.5, 40, 30, grid), r);
        // Zig-Zag: the second row from the right.
        QCOMPARE(at(matrix, QStringLiteral("Zig-Zag"), 0.75, 40, 30, grid), b);
        QCOMPARE(at(matrix, QStringLiteral("Zig-Zag"), 0.75, 10, 30, grid), r);
        // Speckle: every cell in at the end, none at the start.
        QCOMPARE(at(matrix, QStringLiteral("Speckle"), 1.0, 30, 20), b);
        QCOMPARE(at(matrix, QStringLiteral("Speckle"), 0.0, 30, 20), r);
        // Sawtooth (p.480): Horizontal moves left to right.
        QCOMPARE(at(QStringLiteral("Sawtooth Wipe"), QStringLiteral("Horizontal Sawtooth"), 0.5, 5, 18), b);
        QCOMPARE(at(QStringLiteral("Sawtooth Wipe"), QStringLiteral("Horizontal Sawtooth"), 0.5, 60, 18), r);
        // X Spin (p.482): the outgoing squeezed to a line, black beside it; then the incoming opens out.
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("X Spin"), 0.25, 32, 18), r);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("X Spin"), 0.25, 4, 18), black);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("X Spin"), 0.75, 32, 18), b);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("X Spin"), 0.75, 4, 18), black);
        // Z Spin (p.483): from a dot to the whole picture.
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("Z Spin"), 0.0, 32, 18), r);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("Z Spin"), 1.0, 1, 1), b);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("Z Spin"), 0.5, 32, 18), b);
        QCOMPARE(at(QStringLiteral("Spin"), QStringLiteral("Z Spin"), 0.5, 1, 1), r);
        // Border (p.493): Width 100 is 64/16 = 4 pixels outside the incoming edge, white by default.
        const QJsonObject bordered{{QStringLiteral("Border"), QJsonObject{{QStringLiteral("Width"), 100}}}};
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 33, 18, bordered), QColor(255, 255, 255));
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 38, 18, bordered), r);
        // Blend Color from the inner to the outer edge, unless Fixed Border.
        QJsonObject blended = bordered;
        blended[QStringLiteral("Border")] = QJsonObject{{QStringLiteral("Width"), 100},
                                                        {QStringLiteral("Color"), QStringLiteral("#ffffff")},
                                                        {QStringLiteral("Blend Color"), QStringLiteral("#000000")}};
        QVERIFY(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 34, 18, blended).red() < 200);
        QJsonObject fixed = blended;
        QJsonObject b2 = fixed.value(QStringLiteral("Border")).toObject();
        b2[QStringLiteral("Fixed Border")] = true;
        fixed[QStringLiteral("Border")] = b2;
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.5, 34, 18, fixed), QColor(255, 255, 255));
        // Reverse Animation (p.495): the effect backwards with the sources the other way round.
        const QJsonObject reversed{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Reverse Animation"), true}}}};
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.25, 10, 18, reversed), r);
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), 0.25, 60, 18, reversed), b);
    }

    void movingWipes() {
        // Conceal, Push, Squeeze and L-Conceal move or scale the pictures: halves of two colours show
        // which part is where.
        auto halves = [](const QColor& left, const QColor& right) {
            Picture p = Picture::solid(64, 36, right);
            const Picture l = Picture::solid(32, 36, left);
            for (int y = 0; y < 36; ++y) {
                for (int x = 0; x < 32; ++x) std::copy(l.pixel(x, y), l.pixel(x, y) + 4, p.pixel(x, y));
            }
            return p;
        };
        const QColor yellow(255, 255, 0), r(255, 0, 0), g(0, 255, 0), b(0, 0, 255);
        const Picture out = halves(yellow, r);
        const Picture in = halves(g, b);
        auto at = [&](const QString& category, const QString& name, double p, int x, int y) {
            return wipePicture(Effect{category, name, {}}, out, in, p, p, EffectSpan(10, true)).toImage().pixelColor(x, y);
        };
        // Conceal Left to Right half way: the incoming picture's right half on the left (p.459); the
        // outgoing picture stays where it is.
        QCOMPARE(at(QStringLiteral("Conceal"), QStringLiteral("Left to Right"), 0.5, 10, 18), b);
        QCOMPARE(at(QStringLiteral("Conceal"), QStringLiteral("Left to Right"), 0.5, 50, 18), r);
        // Push: the outgoing picture pushed right, its left half now on the right (p.479).
        QCOMPARE(at(QStringLiteral("Push"), QStringLiteral("Left to Right"), 0.5, 10, 18), b);
        QCOMPARE(at(QStringLiteral("Push"), QStringLiteral("Left to Right"), 0.5, 50, 18), yellow);
        // Squeeze Centered Zoom: the whole incoming picture at half size in the middle (p.484).
        QCOMPARE(at(QStringLiteral("Squeeze"), QStringLiteral("Centered Zoom"), 0.5, 20, 18), g);
        QCOMPARE(at(QStringLiteral("Squeeze"), QStringLiteral("Centered Zoom"), 0.5, 44, 18), b);
        QCOMPARE(at(QStringLiteral("Squeeze"), QStringLiteral("Centered Zoom"), 0.5, 2, 2), yellow);
        // Squeeze Right to Left: the full height, squeezed from the right.
        QCOMPARE(at(QStringLiteral("Squeeze"), QStringLiteral("Right to Left"), 0.5, 40, 2), g);
        QCOMPARE(at(QStringLiteral("Squeeze"), QStringLiteral("Right to Left"), 0.5, 60, 33), b);
        // L-Conceal Bottom Right (p.475): first up the right half, showing the incoming left half...
        QCOMPARE(at(QStringLiteral("L-Conceal"), QStringLiteral("Bottom Right"), 0.5, 40, 4), g);
        QCOMPARE(at(QStringLiteral("L-Conceal"), QStringLiteral("Bottom Right"), 0.5, 10, 4), yellow);
        // ...then across to the left.
        QCOMPARE(at(QStringLiteral("L-Conceal"), QStringLiteral("Bottom Right"), 1.0, 10, 4), g);
        QCOMPARE(at(QStringLiteral("L-Conceal"), QStringLiteral("Bottom Right"), 1.0, 50, 4), b);
    }

    void wipesInSequences() {
        // A Horizontal edge wipe as a transition: the left of the picture shows the incoming clip first.
        Transition edge = transition(QStringLiteral("Horizontal"), 4);
        edge.effect.category = QStringLiteral("Edge Wipe");
        const Sequence seq = cutWith(edge);
        const Composition c = composition(seq, {QStringLiteral("V1")});
        // Frame 9 is at 2/5.
        const QImage frame = composeImage(c, 9);
        QCOMPARE(frame.pixelColor(1, 4), QColor(0, 0, 255));
        QCOMPARE(frame.pixelColor(14, 4), QColor(255, 0, 0));
        // On a segment of V2 ("multilayer segments", p.459): from V1 to V2 across the segment.
        Sequence two = Sequence::withDefaultTracks();
        two.addTrack(TrackKind::Video);
        two.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 11)});
        Segment top = clip(QStringLiteral("blue"), 11);
        top.effect = Effect{QStringLiteral("Edge Wipe"), QStringLiteral("Horizontal"), {}};
        two.splice(QStringLiteral("V2"), 0, {top});
        const Composition both = composition(two, {QStringLiteral("V1"), QStringLiteral("V2")});
        QCOMPARE(composeImage(both, 0).pixelColor(8, 4), QColor(255, 0, 0));
        QCOMPARE(composeImage(both, 5).pixelColor(2, 4), QColor(0, 0, 255));
        QCOMPARE(composeImage(both, 5).pixelColor(13, 4), QColor(255, 0, 0));
        QCOMPARE(composeImage(both, 10).pixelColor(15, 8), QColor(0, 0, 255));
        // Swap Sources (p.495) on the segment: from V2 to V1.
        two.track(QStringLiteral("V2"))->segments[0].effect->parameters =
            QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Swap Sources"), true}}}};
        QCOMPARE(composeImage(both, 5).pixelColor(2, 4), QColor(255, 0, 0));
        QCOMPARE(composeImage(both, 5).pixelColor(13, 4), QColor(0, 0, 255));
    }

    void imageEffects() {
        // The geometric segment effects (docs/spec/image-effects.md) on a picture red on the left and
        // blue on the right.
        Picture halves = Picture::solid(64, 36, QColor(0, 0, 255));
        const Picture left = Picture::solid(32, 36, QColor(255, 0, 0));
        for (int y = 0; y < 36; ++y) {
            for (int x = 0; x < 32; ++x) std::copy(left.pixel(x, y), left.pixel(x, y) + 4, halves.pixel(x, y));
        }
        auto at = [&](const QString& category, const QString& name, int x, int y, QJsonObject params = {}) {
            const Effect e{category, name, std::move(params)};
            return imageEffectPicture(e, halves, 0.0, EffectSpan(10)).toImage().pixelColor(x, y);
        };
        auto alphaAt = [&](const QString& category, const QString& name, int x, int y, QJsonObject params = {}) {
            const Effect e{category, name, std::move(params)};
            return imageEffectPicture(e, halves, 0.0, EffectSpan(10)).pixel(x, y)[3];
        };
        const QColor r(255, 0, 0), b(0, 0, 255), black(0, 0, 0);
        // Flop flips across, Flip up and down (p.469).
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Flop"), 5, 5), b);
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Flip"), 5, 5), r);
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Flip-Flop"), 60, 30), r);
        // Picture-in-Picture (p.458): half size in the middle by default (ours), transparent around it.
        const QString blend = QStringLiteral("Blend");
        const QString pip = QStringLiteral("Picture-in-Picture");
        QCOMPARE(at(blend, pip, 24, 18), r);
        QCOMPARE(at(blend, pip, 40, 18), b);
        QCOMPARE(alphaAt(blend, pip, 4, 4), 0.0f);
        // Scaling 100 with Position X 960 (half of 1920): the picture's left half lands on the right.
        const QJsonObject moved{{QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 100}, {QStringLiteral("Y"), 100}}},
                                {QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 960}}}};
        QCOMPARE(at(blend, pip, 40, 18, moved), r);
        QCOMPARE(alphaAt(blend, pip, 10, 18, moved), 0.0f);
        // Crop L at 999/2: only the right half is kept (p.494).
        const QJsonObject cropped{{QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 100}, {QStringLiteral("Y"), 100}}},
                                  {QStringLiteral("Crop"), QJsonObject{{QStringLiteral("L"), 500}}}};
        QCOMPARE(alphaAt(blend, pip, 10, 18, cropped), 0.0f);
        QCOMPARE(at(blend, pip, 50, 18, cropped), b);
        // Foreground Level: the picture's opacity.
        const QJsonObject half{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Level"), 50}}}};
        QCOMPARE(alphaAt(blend, pip, 32, 18, half), 0.5f);
        // Border (p.493) around it: Width 100 is 4 pixels, white.
        const QJsonObject bordered{{QStringLiteral("Border"), QJsonObject{{QStringLiteral("Width"), 100}}}};
        QCOMPARE(at(blend, pip, 32, 7, bordered), QColor(255, 255, 255));
        // Resize (p.470): the Background Color where there is no picture.
        const QJsonObject green{{QStringLiteral("Background Color"), QJsonObject{{QStringLiteral("Background Color"), QStringLiteral("#00ff00")}}},
                                {QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 50}, {QStringLiteral("Y"), 50}}}};
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Resize"), 2, 2, green), QColor(0, 255, 0));
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Resize"), 2, 2), r);
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Resize"), 24, 18, green), r);
        // Mask (p.470): the picture inside the rectangle, black outside.
        const QJsonObject narrow{{QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 50}, {QStringLiteral("Y"), 100}}}};
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Mask"), 4, 18, narrow), black);
        QCOMPARE(at(QStringLiteral("Image"), QStringLiteral("Mask"), 20, 18, narrow), r);
        // The 1.85 Mask (p.461): 64 / 1.85 = 34.6 lines of 36, so the first line is partly masked.
        QVERIFY(at(QStringLiteral("Film"), QStringLiteral("1.85 Mask"), 10, 0).red() < 128);
        QCOMPARE(at(QStringLiteral("Film"), QStringLiteral("1.85 Mask"), 10, 5), r);
        // The Anamorphic Mask: 64 / 2.35 = 27 lines, 4.5 black at each side.
        QCOMPARE(at(QStringLiteral("Film"), QStringLiteral("Anamorphic Mask"), 10, 2), black);
        QCOMPARE(at(QStringLiteral("Film"), QStringLiteral("Anamorphic Mask"), 10, 30), r);
        // Blowup (p.500): Field 12 is three times the size (ours); X Pos -24 shows the left edge.
        const QJsonObject blown{{QStringLiteral("Film Effects Blowup Parameter"),
                                 QJsonObject{{QStringLiteral("Field"), 12}, {QStringLiteral("X Pos"), -24}}}};
        QCOMPARE(at(QStringLiteral("Film"), QStringLiteral("Blowup"), 60, 18, blown), r);
        QCOMPARE(at(QStringLiteral("Film"), QStringLiteral("Blowup"), 60, 18), b);
        // Field Swap (p.469): each pair of lines changes places.
        Picture lines = Picture::solid(2, 2, QColor(0, 0, 0));
        lines.pixel(0, 0)[0] = 1.0f;
        const Picture swapped = imageEffectPicture(Effect{QStringLiteral("Image"), QStringLiteral("Field Swap"), {}}, lines, 0.0, EffectSpan(10));
        QCOMPARE(swapped.pixel(0, 1)[0], 1.0f);
        QCOMPARE(swapped.pixel(0, 0)[0], 0.0f);
    }

    void colorEffect() {
        // The Color Effect (FX Guide p.533-536) on one pixel.
        auto apply = [](const QColor& in, QJsonObject params) {
            const Effect e{QStringLiteral("Image"), QStringLiteral("Color Effect"),
                           QJsonObject{{QStringLiteral("Color Effect Parameters"), std::move(params)}}};
            return imageEffectPicture(e, Picture::solid(1, 1, in), 0.0, EffectSpan(10)).toImage().pixelColor(0, 0);
        };
        const QColor grey(128, 128, 128);
        // At its defaults it changes nothing.
        QVERIFY(near(apply(grey, {}), grey, 1));
        QVERIFY(near(apply(QColor(200, 30, 90), {}), QColor(200, 30, 90), 1));
        // Bright and Cont (p.533).
        QVERIFY(apply(grey, {{QStringLiteral("Bright"), 50}}).red() > 170);
        QVERIFY(near(apply(QColor(250, 250, 250), {{QStringLiteral("Cont"), -100}}), apply(QColor(5, 5, 5), {{QStringLiteral("Cont"), -100}}), 1));
        // Luma Adjust's Invert: white to black.
        QVERIFY(near(apply(QColor(255, 255, 255), {{QStringLiteral("Invert"), true}}), QColor(0, 0, 0), 1));
        // Sat -100: grey (p.535).
        const QColor flat = apply(QColor(255, 0, 0), {{QStringLiteral("Sat"), -100}});
        QVERIFY(std::abs(flat.red() - flat.green()) <= 1 && std::abs(flat.green() - flat.blue()) <= 1);
        // The second Invert, Chroma Adjust's, is kept apart from Luma Adjust's: red's complement.
        const QColor comp = apply(QColor(255, 0, 0), {{QStringLiteral("Chroma Adjust Parameters: Invert"), true}});
        QVERIFY(comp.red() < comp.green() && comp.red() < comp.blue());
        // Color Gain (p.536): Red 0 takes red out.
        QCOMPARE(apply(QColor(255, 0, 0), {{QStringLiteral("Red"), 0}}).red(), 0);
        // Solar 255 inverts everything; Post 25 leaves two steps (p.535-536).
        QVERIFY(near(apply(QColor(255, 255, 255), {{QStringLiteral("Solar"), 255}}), QColor(0, 0, 0), 1));
        const int posted = apply(QColor(100, 100, 100), {{QStringLiteral("Post"), 25}}).red();
        QVERIFY(posted == 0 || posted == 255);
        // Luma Clip High at 128: white comes down to code 128 (p.535).
        QVERIFY(near(apply(QColor(255, 255, 255), {{QStringLiteral("High"), 128}}), QColor(130, 130, 130), 2));
        // Luma Range 0 to 255 with the white point at 200: light grey becomes white (p.534).
        QVERIFY(near(apply(QColor(210, 210, 210), {{QStringLiteral("Fast Menu"), QStringLiteral("0 to 255")}, {QStringLiteral("W Point"), 200}}),
                     QColor(255, 255, 255), 1));
    }

    void blurAndMosaic() {
        // Blur and Mosaic (p.468, p.532-536) over the whole picture (ours until Intraframe objects).
        Picture halves = Picture::solid(64, 36, QColor(0, 0, 255));
        const Picture left = Picture::solid(32, 36, QColor(255, 0, 0));
        for (int y = 0; y < 36; ++y) {
            for (int x = 0; x < 32; ++x) std::copy(left.pixel(x, y), left.pixel(x, y) + 4, halves.pixel(x, y));
        }
        // Hor 300 of 1920 is 10 pixels of 64: the edge softens, the far sides stay.
        const Effect blur{QStringLiteral("Image"), QStringLiteral("Blur Effect"),
                          QJsonObject{{QStringLiteral("Blur Effect Blur Parameters"), QJsonObject{{QStringLiteral("Hor"), 300}, {QStringLiteral("Vert"), 0}}}}};
        const QImage blurred = imageEffectPicture(blur, halves, 0.0, EffectSpan(10)).toImage();
        QVERIFY(blurred.pixelColor(31, 18).red() < 220 && blurred.pixelColor(31, 18).blue() > 35);
        QCOMPARE(blurred.pixelColor(0, 18), QColor(255, 0, 0));
        // Opac. 0: no blur.
        Effect none = blur;
        QJsonObject params = none.parameters.value(QStringLiteral("Blur Effect Blur Parameters")).toObject();
        params.insert(QStringLiteral("Opac."), 0);
        none.parameters.insert(QStringLiteral("Blur Effect Blur Parameters"), params);
        QCOMPARE(imageEffectPicture(none, halves, 0.0, EffectSpan(10)).toImage().pixelColor(31, 18), QColor(255, 0, 0));
        // Mosaic tiles of 24 pixels (Hor and Vert 720): the tile across the middle (x 24 to 47, a third
        // red) is the average.
        const Effect mosaic{QStringLiteral("Image"), QStringLiteral("Mosaic Effect"),
                            QJsonObject{{QStringLiteral("Mosaic Effect Mosaic Parameters"), QJsonObject{{QStringLiteral("Hor"), 720}, {QStringLiteral("Vert"), 720}}}}};
        const QImage tiled = imageEffectPicture(mosaic, halves, 0.0, EffectSpan(10)).toImage();
        QCOMPARE(tiled.pixelColor(0, 0), QColor(255, 0, 0));
        QVERIFY(near(tiled.pixelColor(30, 0), QColor(85, 0, 170), 1));
        QCOMPARE(tiled.pixelColor(30, 0), tiled.pixelColor(47, 23));
    }

    void keys() {
        // The Key effects (docs/spec/keys.md) on single pixels: their alpha says what of the tracks below
        // shows through.
        auto keyed = [](const QString& name, const QColor& in, QJsonObject params = {}) {
            const Effect e{QStringLiteral("Key"), name, std::move(params)};
            return imageEffectPicture(e, Picture::solid(1, 1, in), 0.0, EffectSpan(10));
        };
        auto alpha = [&](const QString& name, const QColor& in, QJsonObject params = {}) {
            return keyed(name, in, std::move(params)).pixel(0, 0)[3];
        };
        const QColor blue(0, 0, 255), red(255, 0, 0), green(0, 255, 0), black(0, 0, 0), white(255, 255, 255);
        const QString chroma = QStringLiteral("Chroma Key");
        const QString luma = QStringLiteral("Luma Key");
        // Until set, Chroma Key keys blue and Luma Key black (ours).
        QCOMPARE(alpha(chroma, blue), 0.0f);
        QCOMPARE(alpha(chroma, red), 1.0f);
        QCOMPARE(alpha(luma, black), 0.0f);
        QCOMPARE(alpha(luma, white), 1.0f);
        // Gain "0 (shows only the foreground) to 63 (replaces all the foreground video ...)" (p.548).
        const QString keyCat = QStringLiteral("Key Effects Key Parameters");
        QCOMPARE(alpha(chroma, blue, {{keyCat, QJsonObject{{QStringLiteral("Gain"), 0}}}}), 1.0f);
        QCOMPARE(alpha(chroma, red, {{keyCat, QJsonObject{{QStringLiteral("Gain"), 63}}}}), 0.0f);
        // A green key colour keys green, not blue.
        const QJsonObject greenKey{{keyCat, QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#00ff00")}}}};
        QCOMPARE(alpha(chroma, green, greenKey), 0.0f);
        QCOMPARE(alpha(chroma, blue, greenKey), 1.0f);
        // The Secondary Key keys a second colour (p.549).
        const QString more = QStringLiteral("Chroma Key Profile, Secondary Key, and Spill Suppression Parameters");
        QJsonObject both = greenKey;
        both.insert(more, QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#0000ff")}, {QStringLiteral("Gain"), 20}});
        QCOMPARE(alpha(chroma, blue, both), 0.0f);
        // Spill Suppression greys colours near its own (p.550).
        const QJsonObject spill{{keyCat, QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#00ff00")}}},
                                {more, QJsonObject{{QStringLiteral("Spill Suppression Parameters: Color controls"), QStringLiteral("#0000ff")},
                                                   {QStringLiteral("Spill Suppression Parameters: Gain"), 30}}}};
        const QColor spilt = keyed(chroma, QColor(60, 60, 200), spill).toImage().pixelColor(0, 0);
        QVERIFY(std::abs(spilt.red() - spilt.blue()) < 5);
        // Foreground: Invert Key, Level and Show Alpha (p.495).
        const QString fg = QStringLiteral("Foreground");
        QCOMPARE(alpha(chroma, blue, {{fg, QJsonObject{{QStringLiteral("Invert Key"), true}}}}), 1.0f);
        QCOMPARE(alpha(chroma, red, {{fg, QJsonObject{{QStringLiteral("Level"), 50}}}}), 0.5f);
        QCOMPARE(keyed(chroma, red, {{fg, QJsonObject{{QStringLiteral("Show Alpha"), true}}}}).toImage().pixelColor(0, 0), white);
        // The RGB Keyer (p.550-551): blue keyed, Reverse and Level.
        const QString rgb = QStringLiteral("RGB Keyer");
        const QString rgbCat = QStringLiteral("RGB Keyer Parameters");
        QCOMPARE(alpha(rgb, blue), 0.0f);
        QCOMPARE(alpha(rgb, red), 1.0f);
        QCOMPARE(alpha(rgb, blue, {{rgbCat, QJsonObject{{QStringLiteral("Reverse"), true}}}}), 1.0f);
        QCOMPARE(alpha(rgb, red, {{rgbCat, QJsonObject{{QStringLiteral("Level"), 25}}}}), 0.25f);
        // SpectraMatte (p.551-556): blue keyed by its wedge; Bypass shows the foreground; Opacity.
        const QString spectra = QStringLiteral("SpectraMatte");
        const QString spectraCat = QStringLiteral("SpectraMatte Parameters");
        QCOMPARE(alpha(spectra, blue), 0.0f);
        QCOMPARE(alpha(spectra, red), 1.0f);
        QVERIFY(alpha(spectra, QColor(40, 40, 255)) < 0.2f); // near the key colour: mostly keyed
        QCOMPARE(alpha(spectra, blue, {{spectraCat, QJsonObject{{QStringLiteral("Bypass"), true}}}}), 1.0f);
        QCOMPARE(alpha(spectra, red, {{spectraCat, QJsonObject{{QStringLiteral("Opacity"), 50}}}}), 0.5f);
        // A narrow wedge (Tolerance 10) leaves a purple-blue alone that the default 90 keys.
        const QColor violet(90, 0, 255);
        QVERIFY(alpha(spectra, violet) < 0.5f);
        QCOMPARE(alpha(spectra, violet, {{spectraCat, QJsonObject{{QStringLiteral("Tolerance"), 10}}}}), 1.0f);
        // Alpha Offset +100 brings the foreground back.
        QCOMPARE(alpha(spectra, blue, {{spectraCat, QJsonObject{{QStringLiteral("Alpha Offset"), 100}}}}), 1.0f);
    }

    void keysInSequences() {
        // Chroma Key on V2: the blue clip keyed out, V1's red shows.
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        Segment top = clip(QStringLiteral("blue"), 10);
        top.effect = Effect{QStringLiteral("Key"), QStringLiteral("Chroma Key"), {}};
        seq.splice(QStringLiteral("V2"), 0, {top});
        QCOMPARE(centre(composeImage(composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")}), 3)), QColor(255, 0, 0));
        // Matte Key (p.474): background V1 red, foreground V2 blue, matte V3. A white matte shows the
        // foreground; inverted, the background; grey, half of each.
        Sequence three = Sequence::withDefaultTracks();
        three.addTrack(TrackKind::Video);
        three.addTrack(TrackKind::Video);
        three.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        three.splice(QStringLiteral("V2"), 0, {clip(QStringLiteral("blue"), 10)});
        Segment matte = clip(QStringLiteral("white"), 10);
        matte.effect = Effect{QStringLiteral("Key"), QStringLiteral("Matte Key"), {}};
        three.splice(QStringLiteral("V3"), 0, {matte});
        const Composition c = composition(three, {QStringLiteral("V1"), QStringLiteral("V2"), QStringLiteral("V3")});
        QCOMPARE(centre(composeImage(c, 3)), QColor(0, 0, 255));
        three.track(QStringLiteral("V3"))->segments[0].effect->parameters =
            QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Invert Key"), true}}}};
        QCOMPARE(centre(composeImage(c, 3)), QColor(255, 0, 0));
        three.track(QStringLiteral("V3"))->segments[0].effect->parameters = {};
        three.track(QStringLiteral("V3"))->segments[0].clipId = QStringLiteral("grey");
        QVERIFY(near(centre(composeImage(c, 3)), QColor(127, 0, 128), 2));
    }

    void reformat() {
        // "A Reformat effect works on all tracks below the track to which you apply it" (p.229): on
        // filler on V2, 16:9 Letterbox squeezes V1 to three quarters of the height, black bands above
        // and below.
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        Segment vertical = Segment::filler(10);
        vertical.effect = Effect{QStringLiteral("Reformat"), QStringLiteral("16:9 Letterbox"), {}};
        seq.splice(QStringLiteral("V2"), 0, {vertical});
        const Composition c = composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")});
        const QImage frame = composeImage(c, 3);
        QCOMPARE(frame.pixelColor(8, 0), QColor(0, 0, 0));
        QCOMPARE(frame.pixelColor(8, 4), QColor(255, 0, 0));
        QCOMPARE(frame.pixelColor(8, 8), QColor(0, 0, 0));
        // 4:3 Sidebar: bands at the sides.
        seq.track(QStringLiteral("V2"))->segments[0].effect->name = QStringLiteral("4:3 Sidebar");
        const QImage side = composeImage(c, 3);
        QCOMPARE(side.pixelColor(0, 4), QColor(0, 0, 0));
        QCOMPARE(side.pixelColor(8, 4), QColor(255, 0, 0));
        // Pan and Scan (FX Guide p.230-234): until set, the 4:3 middle of the 16:9 picture, pillarboxed; the Effect
        // Preview's original (Composition::effectPreview) leaves the picture as it is.
        seq.track(QStringLiteral("V2"))->segments[0].effect->name = QStringLiteral("Pan and Scan");
        QCOMPARE(composeImage(c, 3).pixelColor(0, 4), QColor(0, 0, 0));
        QCOMPARE(composeImage(c, 3).pixelColor(8, 4), QColor(255, 0, 0));
        Composition original = c;
        original.effectPreview = true;
        QCOMPARE(composeImage(original, 3).pixelColor(0, 4), QColor(255, 0, 0));
        // A Picture-in-Picture on V2 over V1, and as a transition (the incoming inside the outgoing).
        Sequence pip = Sequence::withDefaultTracks();
        pip.addTrack(TrackKind::Video);
        pip.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        Segment top = clip(QStringLiteral("blue"), 10);
        top.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
        pip.splice(QStringLiteral("V2"), 0, {top});
        const QImage inset = composeImage(composition(pip, {QStringLiteral("V1"), QStringLiteral("V2")}), 3);
        QCOMPARE(inset.pixelColor(8, 4), QColor(0, 0, 255));
        QCOMPARE(inset.pixelColor(1, 1), QColor(255, 0, 0));
        Transition t = transition(QStringLiteral("Picture-in-Picture"), 4);
        const QImage across = composeImage(composition(cutWith(t), {QStringLiteral("V1")}), 9);
        QCOMPARE(across.pixelColor(8, 4), QColor(0, 0, 255));
        QCOMPARE(across.pixelColor(1, 1), QColor(255, 0, 0));
    }

    void shapeScalingAndPosition() {
        // The Shape Wipes' Scaling and Position (p.482) and the diagonal Edge Wipes' Scaling (p.461).
        const Picture red = Picture::solid(64, 36, QColor(255, 0, 0));
        const Picture blue = Picture::solid(64, 36, QColor(0, 0, 255));
        auto at = [&](const QString& category, const QString& name, double p, int x, int y, QJsonObject params = {}) {
            return wipePicture(Effect{category, name, std::move(params)}, red, blue, p, p, EffectSpan(10, true)).toImage().pixelColor(x, y);
        };
        const QColor r(255, 0, 0), b(0, 0, 255);
        const QString shape = QStringLiteral("Shape Wipe");
        // A circle moved to the left (Position X -480 of 1920 is 16 pixels of 64) starts there.
        const QJsonObject left{{QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), -480}}}};
        QCOMPARE(at(shape, QStringLiteral("Circle"), 0.1, 16, 18, left), b);
        QCOMPARE(at(shape, QStringLiteral("Circle"), 0.1, 32, 18, left), r);
        // It still covers the whole picture at the end.
        QCOMPARE(at(shape, QStringLiteral("Circle"), 1.0, 63, 0, left), b);
        // A box made narrow by Scaling X 25.
        const QJsonObject narrow{{QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 25}, {QStringLiteral("Y"), 100}}}};
        QCOMPARE(at(shape, QStringLiteral("Center Box"), 0.25, 32, 4, narrow), b);
        QCOMPARE(at(shape, QStringLiteral("Center Box"), 0.25, 44, 18, narrow), r);
        QCOMPARE(at(shape, QStringLiteral("Center Box"), 1.0, 0, 0, narrow), b);
        // The diagonal at the default scaling reaches the far corner at the end.
        QCOMPARE(at(QStringLiteral("Edge Wipe"), QStringLiteral("Upper Left Diagonal"), 1.0, 63, 35, narrow), b);
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

    void imageInterpolation() {
        // The Render setting's Image Interpolation (Editing Guide p.1214): Draft (Nearest Neighbor)
        // takes the nearest pixel, so an enlarged black and white checkerboard stays black and white;
        // Advanced (Polyphase) filters, giving greys between.
        QImage checker(2, 2, QImage::Format_RGB32);
        checker.setPixel(0, 0, qRgb(0, 0, 0));
        checker.setPixel(1, 0, qRgb(255, 255, 255));
        checker.setPixel(0, 1, qRgb(255, 255, 255));
        checker.setPixel(1, 1, qRgb(0, 0, 0));
        auto greys = [&](ImageInterpolation mode) {
            const QImage out = Picture::fromImage(checker, 16, 16, mode).toImage();
            int n = 0;
            for (int y = 0; y < out.height(); ++y) {
                for (int x = 0; x < out.width(); ++x) {
                    const int v = qRed(out.pixel(x, y));
                    if (v > 8 && v < 247) ++n;
                }
            }
            return n;
        };
        QCOMPARE(greys(ImageInterpolation::Draft), 0);
        QVERIFY(greys(ImageInterpolation::Advanced) > 0);
    }

    void fluidEffects() {
        // FluidColorMap of a still picture: no motion, black (FX Guide p.467).
        Sequence seq = Sequence::withDefaultTracks();
        Segment s = clip(QStringLiteral("white"), 10);
        s.effect = Effect{QStringLiteral("Illusion FX"), QStringLiteral("FluidColorMap"), {}};
        seq.splice(QStringLiteral("V1"), 0, {s});
        QCOMPARE(centre(composeImage(composition(seq, {QStringLiteral("V1")}), 4)), QColor(0, 0, 0));
        // FluidBlur of a still picture leaves it as it is.
        seq.track(QStringLiteral("V1"))->segments[0].effect = Effect{QStringLiteral("Illusion FX"), QStringLiteral("FluidBlur"), {}};
        QVERIFY(near(centre(composeImage(composition(seq, {QStringLiteral("V1")}), 4)), QColor(255, 255, 255)));
        // FluidMorph from red to blue: half way, some of each (p.467, p.511).
        Transition t = transition(QStringLiteral("FluidMorph"), 4);
        t.effect.category = QStringLiteral("Illusion FX");
        const Sequence morph = cutWith(t);
        const QColor mid = centre(composeImage(composition(morph, {QStringLiteral("V1")}), 9));
        QVERIFY(mid.red() > 40 && mid.blue() > 40);
        // Still->Still: the transition's last outgoing frame and first incoming one, the same here.
        Transition still = t;
        still.effect.parameters.insert(QStringLiteral("FluidMorph Effect Source and Input Parameters"),
                                       QJsonObject{{QStringLiteral("Source"), QStringLiteral("Still->Still")}});
        const QColor stillMid = centre(composeImage(composition(cutWith(still), {QStringLiteral("V1")}), 9));
        QVERIFY(near(stillMid, mid, 3));
    }

    void stamps() {
        // Stamp (FX Guide p.298-299, p.584-585): A, a half-size red picture stamped at its last frame, stays on
        // screen over the next segment until C's Clear.
        auto warpOf = [](int scale) {
            Effect e{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
            e.parameters.insert(QStringLiteral("Scaling (3D)"), QJsonObject{{QStringLiteral("X"), scale}, {QStringLiteral("Y"), scale}});
            return e;
        };
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("blue"), 20)});
        Segment a = clip(QStringLiteral("red"), 10);
        a.effect = warpOf(50);
        QJsonObject stamp{{QStringLiteral("Stamps"), QJsonArray{1.0}}};
        a.effect->parameters.insert(QStringLiteral("Stamp"), stamp);
        Segment cc = clip(QStringLiteral("green"), 10);
        cc.effect = warpOf(25);
        cc.effect->parameters.insert(QStringLiteral("Stamp"), QJsonObject{{QStringLiteral("Clears"), QJsonArray{0.5}}});
        seq.overwrite(QStringLiteral("V2"), 0, {a, cc});
        QCOMPARE(stampTimes(*a.effect), QList<double>{1.0});
        const Composition c = composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")});
        auto colour = [&](std::int64_t frame, int x, int y) { return composeImage(c, frame, nullptr).pixelColor(x, y); };
        // In C before its Clear: C's quarter-size green under the stamp ("the Stamp buffer is always in the
        // foreground" without Foregnd. On Top).
        QCOMPARE(colour(12, 8, 4), QColor(255, 0, 0));
        QCOMPARE(colour(12, 5, 4), QColor(255, 0, 0));
        QCOMPARE(colour(12, 0, 0), QColor(0, 0, 255));
        // After the Clear: C alone.
        QCOMPARE(colour(17, 8, 4), QColor(0, 255, 0));
        QCOMPARE(colour(17, 5, 4), QColor(0, 0, 255));
        // Foregnd. On Top: the stamp goes under the track's own picture.
        stamp.insert(QStringLiteral("Foregnd. On Top"), true);
        seq.track(QStringLiteral("V2"))->segments[0].effect->parameters.insert(QStringLiteral("Stamp"), stamp);
        const Composition under = composition(seq, {QStringLiteral("V1"), QStringLiteral("V2")});
        QCOMPARE(composeImage(under, 12, nullptr).pixelColor(8, 4), QColor(0, 255, 0));
        QCOMPARE(composeImage(under, 12, nullptr).pixelColor(5, 4), QColor(255, 0, 0));
    }

    void nests() {
        // A Submaster on filler whose nest holds red under blue at a quarter (Superimpose 25): the nest's
        // composite is the picture (FX Guide p.219, p.224).
        Sequence nest = Sequence::withDefaultTracks();
        nest.addTrack(TrackKind::Video);
        nest.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 10)});
        Segment top = clip(QStringLiteral("blue"), 10);
        top.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Superimpose"), {}};
        nest.overwrite(QStringLiteral("V2"), 0, {top});
        Segment sub = Segment::filler(10);
        sub.effect = Effect{QStringLiteral("Image"), QStringLiteral("Submaster"), {}};
        setNest(sub, nest);
        Sequence seq = Sequence::withDefaultTracks();
        seq.overwrite(QStringLiteral("V1"), 0, {sub});
        const QColor nested = centre(composeImage(composition(seq, {QStringLiteral("V1")}), 4));
        const QColor direct = centre(composeImage(composition(nest, {QStringLiteral("V1"), QStringLiteral("V2")}), 4));
        QVERIFY(near(nested, direct));
        // The nest's frames follow nestIn: a nest of red (0-4) then blue, shown from its frame 5.
        Sequence two = Sequence::withDefaultTracks();
        two.overwrite(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 5), clip(QStringLiteral("blue"), 5)});
        Segment later = Segment::filler(5);
        later.effect = sub.effect;
        setNest(later, two);
        later.nestIn = 5;
        Sequence outer = Sequence::withDefaultTracks();
        outer.overwrite(QStringLiteral("V1"), 0, {later});
        QCOMPARE(centre(composeImage(composition(outer, {QStringLiteral("V1")}), 1)), QColor(0, 0, 255));
    }
};

QTEST_GUILESS_MAIN(TestCompositor)
#include "tst_compositor.moc"
