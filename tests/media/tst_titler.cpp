// Titler+ titles (FX Guide p.302-324; src/model/titler.*, src/media/titler.cpp): the title's JSON, layer order and
// groups, applying the effect to segments, and the picture — text, shapes, Fill, Outline, Shadow, gradients,
// Transform about the Anchor Point, the Background and the Foreground.
#include "lumenis/media/compositor.h"
#include "lumenis/media/titler.h"
#include "lumenis/model/titler.h"

#include <QGuiApplication>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::model;

namespace {

// A 192 x 108 picture: title pixels / 10.
const QSize kSize(192, 108);

QColor at(const QImage& image, int x, int y) {
    return QColor::fromRgba(image.pixel(x, y));
}

// The picture's (x, y) for title pixels (tx, ty) from the centre.
QPoint where(double tx, double ty) {
    return {static_cast<int>(96 + tx / 10.0), static_cast<int>(54 + ty / 10.0)};
}

} // namespace

class TestTitler : public QObject {
    Q_OBJECT

private slots:
    void model() {
        Effect e = titlerEffect();
        QVERIFY(isTitlerEffect(e));
        QVERIFY(titleOf(e).objects.isEmpty());
        Title t;
        t.objects << textObject(t, QStringLiteral("Hello\nWorld"), QPointF(10, 20));
        t.objects << shapeObject(t, TitleObjectKind::Rectangle, QPointF(0, 0), QSizeF(100, 50));
        t.objects << shapeObject(t, TitleObjectKind::Ellipse, QPointF(5, 5), QSizeF(10, 10));
        QCOMPARE(t.objects.at(0).id, QStringLiteral("Text 1"));
        QCOMPARE(t.objects.at(1).id, QStringLiteral("Rectangle 1"));
        t.objects[1].fill.color.gradient = true;
        t.objects[1].fill.color.topRight = QStringLiteral("#ff0000");
        t.objects[1].outline.on = true;
        t.objects[1].outline.type = OutlineType::Inner;
        t.objects[0].font.horizontal = HorizontalJustify::Center;
        t.objects[0].transform.rotation = 30;
        t.background.on = true;
        t.foreground.cropLeft = 10;
        setTitle(e, t);
        QCOMPARE(titleOf(e), t);
        // "dynamically labels segments based on their topmost Titler+ text content" (p.305).
        QCOMPARE(t.topText(), QStringLiteral("Hello"));
        // Layer order (p.318): the ellipse is on top; send it back, bring it forward.
        QVERIFY(!bringForward(t, QStringLiteral("Ellipse 1")));
        QVERIFY(sendBackward(t, QStringLiteral("Ellipse 1")));
        QCOMPARE(t.objects.at(1).id, QStringLiteral("Ellipse 1"));
        QVERIFY(bringForward(t, QStringLiteral("Ellipse 1")));
        QCOMPARE(t.objects.last().id, QStringLiteral("Ellipse 1"));
        // Groups: one object where the topmost was, ungrouped in place.
        const auto group = groupObjects(t, {QStringLiteral("Text 1"), QStringLiteral("Ellipse 1")});
        QVERIFY(group);
        QCOMPARE(t.objects.size(), 2);
        QCOMPARE(t.objects.last().id, *group);
        QCOMPARE(t.objects.last().children.size(), 2);
        QVERIFY(t.object(QStringLiteral("Ellipse 1")));
        QCOMPARE(t.topText(), QStringLiteral("Hello"));
        setTitle(e, t);
        QCOMPARE(titleOf(e), t);
        QVERIFY(ungroupObject(t, *group));
        QCOMPARE(t.objects.size(), 3);
        QCOMPARE(t.objects.at(1).id, QStringLiteral("Text 1"));
        QVERIFY(removeObject(t, QStringLiteral("Text 1")));
        QVERIFY(t.topText().isEmpty());
    }

    void apply() {
        // V1: A 0-50, filler 50-80, B 80-120.
        Sequence seq = Sequence::withDefaultTracks();
        Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = QStringLiteral("V1");
        a.length = 50;
        Segment b = a;
        b.clipId = QStringLiteral("b");
        b.name = QStringLiteral("B");
        b.length = 40;
        seq.splice(QStringLiteral("V1"), 0, {a, Segment::filler(30), b});
        // "Select the clip or an empty track segment" (p.303).
        Sequence one = seq;
        QCOMPARE(applyTitler(one, QStringLiteral("V1"), std::nullopt, {SegmentRef{QStringLiteral("V1"), 50}}), 1);
        QVERIFY(isTitlerEffect(*segmentFor(one, SegmentRef{QStringLiteral("V1"), 50})->effect));
        // A clip with an effect keeps it under the title.
        Sequence nested = seq;
        QVERIFY(applySegmentEffect(nested, QStringLiteral("V1"), 0, Effect{QStringLiteral("Image"), QStringLiteral("Flop"), {}}));
        QCOMPARE(applyTitler(nested, QStringLiteral("V1"), std::nullopt, {SegmentRef{QStringLiteral("V1"), 0}}), 1);
        const Segment titled = *segmentFor(nested, SegmentRef{QStringLiteral("V1"), 0});
        QVERIFY(isTitlerEffect(*titled.effect));
        QVERIFY(titled.hasNest());
        // "set in and out marks ... to highlight a portion of the track": 40-90 becomes its own segments.
        Sequence marked = seq;
        QCOMPARE(applyTitler(marked, QStringLiteral("V1"), std::pair<std::int64_t, std::int64_t>{40, 90}, {}), 3);
        const Track& v1 = *marked.track(QStringLiteral("V1"));
        std::int64_t start = 0;
        for (const Segment& s : v1.segments) {
            const bool inside = start >= 40 && start < 90;
            QCOMPARE(s.effect.has_value() && isTitlerEffect(*s.effect), inside);
            start += s.length;
        }
        // Past the end of the track: filler up to the out mark.
        Sequence beyond = seq;
        QCOMPARE(applyTitler(beyond, QStringLiteral("V1"), std::pair<std::int64_t, std::int64_t>{130, 150}, {}), 1);
        QCOMPARE(beyond.track(QStringLiteral("V1"))->length(), std::int64_t(150));
    }

    void picture() {
        // A rectangle 400 x 200 at the centre, filled red: red at the centre, nothing outside.
        Title t;
        TitleObject box = shapeObject(t, TitleObjectKind::Rectangle, QPointF(0, 0), QSizeF(400, 200));
        box.fill.color.color = QStringLiteral("#ff0000");
        t.objects << box;
        QImage image = media::titleImage(t, kSize);
        QCOMPARE(at(image, 96, 54), QColor(255, 0, 0));
        QCOMPARE(at(image, 96 + 25, 54).alpha(), 0);
        // Moved by Position and rotated a quarter turn about its anchor (its centre): now 200 wide and 400 high.
        t.objects[0].transform.position = QPointF(300, 0);
        t.objects[0].transform.rotation = 90;
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, where(300, 150).x(), where(300, 150).y()), QColor(255, 0, 0));
        QCOMPARE(at(image, where(300 + 150, 0).x(), where(300 + 150, 0).y()).alpha(), 0);
        // About an anchor at its left edge: rotation swings it round that point (p.313).
        t.objects[0].transform.position = QPointF(0, 0);
        t.objects[0].transform.anchor = QPointF(-200, 0);
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, where(-200, 150).x(), where(-200, 150).y()), QColor(255, 0, 0));
        QCOMPARE(at(image, where(100, 0).x(), where(100, 0).y()).alpha(), 0);
        t.objects[0].transform = TitleTransform{};
        // Outer outline: black just outside the box; Inner: black just inside, red further in.
        t.objects[0].outline.on = true;
        t.objects[0].outline.size = 40;
        t.objects[0].outline.shape = OutlineShape::Square;
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, where(220, 0).x(), where(220, 0).y()), QColor(0, 0, 0));
        QCOMPARE(at(image, where(150, 0).x(), where(150, 0).y()), QColor(255, 0, 0));
        t.objects[0].outline.type = OutlineType::Inner;
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, where(180, 0).x(), where(180, 0).y()), QColor(0, 0, 0));
        QCOMPARE(at(image, where(220, 0).x(), where(220, 0).y()).alpha(), 0);
        t.objects[0].outline.on = false;
        // Shadow: Distance 100 at Angle 0 (to the right).
        t.objects[0].shadow.on = true;
        t.objects[0].shadow.distance = 100;
        t.objects[0].shadow.angle = 0;
        t.objects[0].shadow.softness = 0;
        t.objects[0].shadow.opacity = 100;
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, where(250, 0).x(), where(250, 0).y()), QColor(0, 0, 0));
        QCOMPARE(at(image, where(320, 0).x(), where(320, 0).y()).alpha(), 0);
        t.objects[0].shadow.on = false;
        // A gradient: red at the top left, blue at the bottom right.
        t.objects[0].fill.color.gradient = true;
        t.objects[0].fill.color.topLeft = QStringLiteral("#ff0000");
        t.objects[0].fill.color.topRight = QStringLiteral("#800080");
        t.objects[0].fill.color.bottomLeft = QStringLiteral("#800080");
        t.objects[0].fill.color.bottomRight = QStringLiteral("#0000ff");
        image = media::titleImage(t, kSize);
        QVERIFY(at(image, where(-190, -90).x(), where(-190, -90).y()).red() > 200);
        QVERIFY(at(image, where(190, 90).x(), where(190, 90).y()).blue() > 200);
        t.objects[0].fill.color.gradient = false;
        // Fill Opacity and Object Opacity.
        t.objects[0].fill.opacity = 50;
        image = media::titleImage(t, kSize);
        QVERIFY(std::abs(at(image, 96, 54).alpha() - 128) <= 2);
        t.objects[0].fill.opacity = 100;
        t.objects[0].transform.opacity = 25;
        image = media::titleImage(t, kSize);
        QVERIFY(std::abs(at(image, 96, 54).alpha() - 64) <= 2);
        t.objects[0].transform.opacity = 100;
        // Softness blurs the edge.
        t.objects[0].fill.softness = 40;
        image = media::titleImage(t, kSize);
        const int edge = at(image, where(200, 0).x(), where(200, 0).y()).alpha();
        QVERIFY2(edge > 40 && edge < 220, qPrintable(QString::number(edge)));
        t.objects[0].fill.softness = 0;

        // The Background under it all, the Foreground's Crop and Opacity over it.
        t.background.on = true;
        t.background.color.color = QStringLiteral("#00ff00");
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, 2, 2), QColor(0, 255, 0));
        QCOMPARE(at(image, 96, 54), QColor(255, 0, 0));
        t.foreground.cropLeft = 25;
        image = media::titleImage(t, kSize);
        QCOMPARE(at(image, 10, 54).alpha(), 0);
        QCOMPARE(at(image, 60, 54), QColor(0, 255, 0));
        t.foreground.cropLeft = 0;
        t.foreground.opacity = 50;
        image = media::titleImage(t, kSize);
        QVERIFY(std::abs(at(image, 2, 2).alpha() - 128) <= 2);
    }

    void text() {
        // Left justified text from its baseline left (p.307, p.313); Center and Right move it.
        Title t;
        TitleObject text = textObject(t, QStringLiteral("HHHH"), QPointF(0, 0));
        text.font.size = 200;
        t.objects << text;
        auto inked = [&](const QImage& image) {
            QRect r;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (at(image, x, y).alpha() > 128) r = r.united(QRect(x, y, 1, 1));
                }
            }
            return r;
        };
        const QRect left = inked(media::titleImage(t, kSize));
        QVERIFY(!left.isEmpty());
        QVERIFY(left.left() >= 95 && left.left() <= 99);
        QVERIFY(left.bottom() <= 54); // above the baseline
        t.objects[0].font.horizontal = HorizontalJustify::Right;
        const QRect right = inked(media::titleImage(t, kSize));
        QVERIFY(right.right() <= 97 && right.right() >= 93);
        t.objects[0].font.horizontal = HorizontalJustify::Center;
        const QRect centred = inked(media::titleImage(t, kSize));
        QVERIFY(std::abs(centred.center().x() - 96) <= 2);
        // Kerning widens it; a second line goes below.
        t.objects[0].font.kerning = 40;
        QVERIFY(inked(media::titleImage(t, kSize)).width() > centred.width() + 8);
        t.objects[0].font.kerning = 0;
        t.objects[0].text = QStringLiteral("HHHH\nHHHH");
        QVERIFY(inked(media::titleImage(t, kSize)).bottom() > 60);
        // Bottom: the last line's baseline at the origin, so all above it.
        t.objects[0].font.vertical = VerticalJustify::Bottom;
        QVERIFY(inked(media::titleImage(t, kSize)).bottom() <= 54);
        // The outline of a text object for the Record monitor (6.8b).
        const auto outline = media::titleObjectOutline(t, QStringLiteral("Text 1"));
        QVERIFY(outline && outline->boundingRect().width() > 100);

        // Over a segment's picture: the title over it.
        Effect e = titlerEffect();
        setTitle(e, t);
        const media::Picture under = media::Picture::solid(kSize.width(), kSize.height(), Qt::blue);
        const QImage over = media::titlePicture(e, under).toImage();
        QCOMPARE(at(over, 2, 2), QColor(0, 0, 255));
        const QImage alone = media::titleImage(t, kSize);
        QPoint ink(-1, -1);
        for (int y = 0; y < alone.height() && ink.x() < 0; ++y) {
            for (int x = 0; x < alone.width() && ink.x() < 0; ++x) {
                if (at(alone, x, y).alpha() == 255) ink = QPoint(x, y);
            }
        }
        QVERIFY(ink.x() >= 0);
        QCOMPARE(at(over, ink.x(), ink.y()), QColor(255, 255, 255));
    }
};

QTEST_MAIN(TestTitler)
#include "tst_titler.moc"
