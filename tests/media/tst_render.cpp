// Render Effect (FX Guide p.57-61; docs/spec/rendering.md): an effect rendered into a precompute that
// holds its track and those below, the Composer playing it in place of the effect while it is in date,
// and computing the effect again once the render is out of date.
#include "lumenis/media/media.h"
#include "lumenis/media/render.h"

#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::media;
using namespace lumenis::model;

namespace {

const QSize kSize(64, 36);

Segment clip(const QString& id, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = id;
    s.name = id;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

Composition composition(const Sequence& seq) {
    Composition c;
    c.sequence = &seq;
    c.tracks = {QStringLiteral("V1"), QStringLiteral("V2")};
    c.size = kSize;
    c.precision = EffectPrecision::Bits32;
    c.source = [](const Segment& s, std::int64_t, QSize size) -> std::optional<QImage> {
        QImage image(size, QImage::Format_RGB32);
        image.fill(s.clipId == QLatin1String("red") ? QColor(255, 0, 0) : QColor(0, 0, 255));
        return image;
    };
    return c;
}

// V1 red for 20 frames; V2 blue over frames 5-15 with a Picture-in-Picture (half size, centred).
Sequence sample() {
    Sequence seq = Sequence::withDefaultTracks();
    seq.addTrack(TrackKind::Video);
    seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("red"), 20)});
    seq.splice(QStringLiteral("V2"), 0, {Segment::filler(5), clip(QStringLiteral("blue"), 10)});
    seq.track(QStringLiteral("V2"))->segments[1].effect = Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
    return seq;
}

bool near(const QColor& a, const QColor& b, int tolerance) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

} // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:
    void playsRendersInDate() {
        // A render in date shows in place of its track and those below; out of date, the effect is
        // computed again. (The render's pictures here: green.)
        Sequence seq = sample();
        const RenderTarget pip = *renderTargetAt(seq, QStringLiteral("V2"), 8);
        QVERIFY(setRender(seq, pip, EffectRender{QJsonObject{{QStringLiteral("name"), QStringLiteral("p")}}, renderKey(seq, pip)}));
        Composition c = composition(seq);
        QList<std::int64_t> asked;
        c.rendered = [&asked](const EffectRender&, std::int64_t frame, QSize size) -> std::optional<QImage> {
            asked << frame;
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(0, 255, 0));
            return image;
        };
        QCOMPARE(composeFrame(c, 8).toImage().pixelColor(2, 2), QColor(0, 255, 0));
        QCOMPARE(asked, QList<std::int64_t>{3}); // the render's own frame: 8 - 5
        QCOMPARE(composeFrame(c, 2).toImage().pixelColor(2, 2), QColor(255, 0, 0)); // before the effect
        // Changed: computed (the PIP's corner shows V1's red).
        seq.track(QStringLiteral("V2"))->segments[1].effect->parameters =
            QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Level"), 50}}}};
        QVERIFY(composeFrame(c, 8).toImage().pixelColor(2, 2) != QColor(0, 255, 0));
        // Offline: computed too.
        Sequence again = sample();
        QVERIFY(setRender(again, pip, EffectRender{QJsonObject{{QStringLiteral("name"), QStringLiteral("p")}}, renderKey(again, pip)}));
        Composition offline = composition(again);
        offline.rendered = [](const EffectRender&, std::int64_t, QSize) -> std::optional<QImage> { return std::nullopt; };
        QCOMPARE(composeFrame(offline, 8).toImage().pixelColor(2, 2), QColor(255, 0, 0));
    }

    void rendersIntoManagedMedia() {
        // A real render: the composite of V1 and V2 over the PIP's frames, as a precompute clip of
        // managed media that reads back as the computed picture.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Sequence seq = sample();
        const RenderTarget pip = *renderTargetAt(seq, QStringLiteral("V2"), 8);
        const Composition c = composition(seq);
        MediaCreation creation;
        creation.videoDrive = dir.path();
        creation.raster = QSize(256, 144);
        QString error;
        const auto render = renderEffect(c, pip, QStringLiteral("Seq"), Rational{25, 1}, creation, &error);
        QVERIFY2(render, qPrintable(error));
        QCOMPARE(render->key, renderKey(seq, pip));
        const auto item = binItemFromJson(render->clip);
        QVERIFY(item);
        QCOMPARE(item->kind, BinItemKind::RenderedEffect);
        QCOMPARE(item->name, QStringLiteral("Seq - Picture-in-Picture"));
        QCOMPARE(item->durationFrames, 10);
        VideoReader reader;
        QVERIFY(reader.open(item->link.value(QStringLiteral("path")).toString()));
        const QImage first = reader.frame(0, kSize);
        const QImage computed = composeFrame(c, 5).toImage();
        QVERIFY(near(first.pixelColor(2, 2), computed.pixelColor(2, 2), 12));                 // V1 red round the PIP
        QVERIFY(near(first.pixelColor(32, 18), computed.pixelColor(32, 18), 12));             // V2 blue inside
        // Kept with the effect, it is in date.
        QVERIFY(setRender(seq, pip, *render));
        QVERIFY(isRendered(seq, pip));
    }
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
