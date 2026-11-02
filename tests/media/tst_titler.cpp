// Titler+ titles (FX Guide p.302-324; src/model/titler.*, src/media/titler.cpp): the title's JSON, layer order and
// groups, applying the effect to segments, and the picture — text, shapes, Fill, Outline, Shadow, gradients,
// Transform about the Anchor Point, the Background and the Foreground.
#include "lumenis/media/compositor.h"
#include "lumenis/media/titler.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/titler.h"
#include "lumenis/model/tracking.h"

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

    // Formatting selected characters (6.8c2, p.304-309): runs of overrides that follow their characters.
    void characters() {
        Title t;
        t.objects << textObject(t, QStringLiteral("HHHHHH"), QPointF(-300, 0));
        TitleObject& o = t.objects[0];
        o.font.size = 200;
        // Characters 2-4 red and larger; the rest as the object.
        QVERIFY(changeCharacters(o, 2, 4, [](TitleObject& v) {
            v.fill.color.color = QStringLiteral("#ff0000");
            v.font.size = 300;
        }));
        QCOMPARE(o.runs.size(), 1);
        QCOMPARE(o.runs.at(0).from, 2);
        QCOMPARE(o.runs.at(0).to, 4);
        QCOMPARE(o.font.size, 200.0);
        QCOMPARE(styleAt(o, 3).font.size, 300.0);
        QCOMPARE(styleAt(o, 1).fill.color, QStringLiteral("#ffffff"));
        QCOMPARE(characterView(o, 2).fill.color.color, QStringLiteral("#ff0000"));
        QVERIFY(!changeCharacters(o, 2, 4, [](TitleObject& v) { v.font.size = 300; }));
        // A paragraph property goes to the whole object.
        QVERIFY(changeCharacters(o, 2, 4, [](TitleObject& v) { v.font.horizontal = HorizontalJustify::Center; }));
        QCOMPARE(o.font.horizontal, HorizontalJustify::Center);
        o.font.horizontal = HorizontalJustify::Left;
        // Typing inside the run widens it; before it shifts it; the object's own value ends an override.
        replaceText(o, 3, 3, QStringLiteral("H"));
        QCOMPARE(o.runs.at(0).to, 5);
        replaceText(o, 0, 1, QString());
        QCOMPARE(o.runs.at(0).from, 1);
        QCOMPARE(o.runs.at(0).to, 4);
        QCOMPARE(o.text, QStringLiteral("HHHHHH"));
        // Kept in the effect.
        Effect e = titlerEffect();
        setTitle(e, t);
        QCOMPARE(titleOf(e), t);
        // The picture: red characters among white, taller than the white ones.
        const QImage image = media::titleImage(t, kSize);
        int redTop = 1000, whiteTop = 1000;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = at(image, x, y);
                if (c.alpha() < 250) continue;
                if (c.red() > 200 && c.green() < 60) redTop = std::min(redTop, y);
                if (c.red() > 200 && c.green() > 200) whiteTop = std::min(whiteTop, y);
            }
        }
        QVERIFY2(redTop < 1000 && whiteTop < 1000 && redTop + 3 < whiteTop, qPrintable(QStringLiteral("%1 %2").arg(redTop).arg(whiteTop)));
        // The cursor goes between characters: a click left of the text is the start, right of it the end, and the
        // larger characters are wider.
        const TitleObject& laid = t.objects.at(0);
        QCOMPARE(media::titleTextIndexAt(laid, QPointF(-50, -20)), 0);
        QCOMPARE(media::titleTextIndexAt(laid, QPointF(5000, -20)), 6);
        const auto small = media::titleTextRange(laid, 0, 1);
        const auto big = media::titleTextRange(laid, 1, 2);
        QVERIFY(small.size() == 1 && big.size() == 1);
        QVERIFY(big.first().boundingRect().width() > small.first().boundingRect().width() * 1.3);
        QVERIFY(big.first().boundingRect().height() > small.first().boundingRect().height() * 0.99);
        const QRectF second = big.first().boundingRect();
        QCOMPARE(media::titleTextIndexAt(laid, QPointF(second.left() + 2, -20)), 1);
    }

    // Templates as presets (6.8c4b, p.320): a section's properties, or all the style, from the template's object of
    // the same kind; the text stays.
    void presets() {
        Title preset;
        preset.objects << shapeObject(preset, TitleObjectKind::Rectangle, QPointF(), QSizeF(10, 10));
        preset.objects << textObject(preset, QStringLiteral("Template"), QPointF(500, 500));
        preset.objects[1].font.size = 150;
        preset.objects[1].font.family = QStringLiteral("Serif");
        preset.objects[1].fill.color.color = QStringLiteral("#0000ff");
        preset.objects[1].shadow.on = true;
        preset.objects[1].outline.on = true;
        preset.objects[0].fill.color.color = QStringLiteral("#00ff00");
        Title t;
        t.objects << textObject(t, QStringLiteral("Mine"), QPointF(-100, 0));
        TitleObject& o = t.objects[0];
        QVERIFY(changeCharacters(o, 0, 2, [](TitleObject& v) {
            v.fill.color.color = QStringLiteral("#ff0000");
            v.font.size = 40;
        }));
        // The Fill section: only the fill, and the characters' own fill goes.
        QVERIFY(applyTitlePreset(o, preset, TitlePresetPart::Fill));
        QCOMPARE(o.fill.color.color, QStringLiteral("#0000ff"));
        QCOMPARE(o.font.size, 72.0);
        QCOMPARE(styleAt(o, 0).fill.color, QStringLiteral("#0000ff"));
        QCOMPARE(styleAt(o, 0).font.size, 40.0);
        QVERIFY(!applyTitlePreset(o, preset, TitlePresetPart::Fill));
        // All: font, outline and shadow too; the text and the place stay.
        QVERIFY(applyTitlePreset(o, preset, TitlePresetPart::All));
        QCOMPARE(o.text, QStringLiteral("Mine"));
        QCOMPARE(o.transform.position, QPointF(-100, 0));
        QCOMPARE(o.font.size, 150.0);
        QCOMPARE(o.font.family, QStringLiteral("Serif"));
        QVERIFY(o.shadow.on && o.outline.on);
        QVERIFY(o.runs.isEmpty());
        // Transform only when asked.
        QVERIFY(applyTitlePreset(o, preset, TitlePresetPart::Transform));
        QCOMPARE(o.transform.position, QPointF(500, 500));
        // A shape takes the template's shape: no ellipse there, so its first object, the rectangle.
        TitleObject box = shapeObject(t, TitleObjectKind::Ellipse, QPointF(), QSizeF(5, 5));
        QVERIFY(applyTitlePreset(box, preset, TitlePresetPart::All));
        QCOMPARE(box.fill.color.color, QStringLiteral("#00ff00"));
    }

    // Templates (6.8c4a, p.319-320): the sequence a template is edited in and back; the Frame view's still.
    void templates() {
        Effect e = titlerEffect();
        Title t;
        t.objects << textObject(t, QStringLiteral("HHH"), QPointF(-200, 0));
        t.objects[0].font.size = 200;
        setTitle(e, t);
        EffectTemplate tpl;
        tpl.effect = e;
        tpl.length = 40;
        tpl.editRate = Rational(25);
        const BinItem item = effectTemplateItem(tpl, QStringLiteral("Name"), QDateTime::currentDateTimeUtc());
        QVERIFY(isTitleTemplate(item));
        QVERIFY(!isTitleTemplate(BinItem{}));
        const BinItem seq = titleTemplateSequence(tpl, Rational(25), QStringLiteral("Name"), QDateTime::currentDateTimeUtc());
        QCOMPARE(seq.kind, BinItemKind::Sequence);
        const auto back = titleTemplateFromSequence(seq, Rational(25));
        QVERIFY(back);
        QCOMPARE(back->length, 40);
        QCOMPARE(titleOf(back->effect), t);
        // Unknown length: ten seconds.
        tpl.length = 0;
        QCOMPARE(titleTemplateFromSequence(titleTemplateSequence(tpl, Rational(25), QString(), {}), Rational(25))->length, 250);
        // "displayed as a still image of the template's first frame, with titles displayed over a black background".
        const QImage still = media::titleFirstFrame(e, 40, kSize);
        QCOMPARE(at(still, 2, 2), QColor(0, 0, 0));
        bool white = false;
        for (int y = 0; y < still.height() && !white; ++y) {
            for (int x = 0; x < still.width() && !white; ++x) white = at(still, x, y) == QColor(255, 255, 255);
        }
        QVERIFY(white);
    }

    // Roll / Crawl (6.8c3, p.324-326): turning it on makes a linear Effect Position from 0 to 100; the objects go from
    // just below the frame to just above it (Crawl Left: from the right to the left).
    void rollCrawl() {
        Effect e = titlerEffect();
        Title t;
        t.objects << textObject(t, QStringLiteral("Credits\nMore\nAnd more"), QPointF(0, 0));
        setTitle(e, t);
        const EffectSpan span(31);
        QVERIFY(setRollCrawl(e, true, span));
        QVERIFY(!setRollCrawl(e, true, span));
        QCOMPARE(keyframesOf(e, titleCategory(QString()), QStringLiteral("Effect Position")).keys.size(), 2);
        QVERIFY(std::abs(titleAt(e, 0.5, span).rollCrawl.position - 50.0) < 1e-6);
        const Title start = titleAt(e, 0.0, span), end = titleAt(e, 1.0, span), middle = titleAt(e, 0.5, span);
        auto bounds = [](const Title& at) { return media::titleObjectOutline(at, QStringLiteral("Text 1"))->boundingRect(); };
        QVERIFY2(bounds(start).top() >= 539.9, qPrintable(QString::number(bounds(start).top())));
        QVERIFY2(bounds(end).bottom() <= -539.9, qPrintable(QString::number(bounds(end).bottom())));
        QVERIFY(bounds(middle).top() < 540 && bounds(middle).bottom() > -540);
        auto inked = [](const Title& at) {
            const QImage image = media::titleImage(at, kSize);
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (qAlpha(image.pixel(x, y)) > 0) return true;
                }
            }
            return false;
        };
        QVERIFY(!inked(start));
        QVERIFY(!inked(end));
        QVERIFY(inked(middle));
        // Crawl Left: from the right edge out to the left.
        Title crawl = titleOf(e);
        crawl.rollCrawl.type = RollCrawlType::CrawlLeft;
        setTitle(e, crawl);
        QCOMPARE(titleOf(e).rollCrawl.type, RollCrawlType::CrawlLeft);
        QVERIFY(bounds(titleAt(e, 0.0, span)).left() >= 959.9);
        QVERIFY(bounds(titleAt(e, 1.0, span)).right() <= -959.9);
        // Off: no movement.
        QVERIFY(setRollCrawl(e, false, span));
        QCOMPARE(media::titleScroll(titleAt(e, 0.0, span)), QPointF());
    }

    // An object attached to a tracker moves with it from the reference frame (6.8c3, p.326); edits keep its own position.
    void tracking() {
        Effect e = titlerEffect();
        Title t;
        t.objects << textObject(t, QStringLiteral("Tag"), QPointF(100, 50));
        t.objects[0].tracking = true;
        t.objects[0].tracker = QStringLiteral("Point A");
        setTitle(e, t);
        QCOMPARE(titleOf(e), t);
        TrackingSetup setup;
        Tracker tracker;
        tracker.name = QStringLiteral("Point A");
        tracker.regions << TrackerRegion{};
        tracker.setPoint(0, QPointF(0.5, 0.5));
        tracker.setPoint(10, QPointF(0.6, 0.45));
        setup.trackers << tracker;
        setTracking(e, setup);
        const EffectSpan span(11);
        const Title atStart = titleAt(e, 0.0, span), atEnd = titleAt(e, 1.0, span);
        QCOMPARE(atStart.objects.at(0).transform.position, QPointF(100, 50));
        QVERIFY(std::abs(atEnd.objects.at(0).transform.position.x() - (100 + 0.1 * 1920)) < 1e-6);
        QVERIFY(std::abs(atEnd.objects.at(0).transform.position.y() - (50 - 0.05 * 1080)) < 1e-6);
        // Dragging it at the end by 10 pixels moves its own position by 10.
        Title moved = atEnd;
        moved.objects[0].transform.position += QPointF(10, 0);
        storeTitleAt(e, moved, 1.0, span);
        QVERIFY(std::abs(titleOf(e).objects.at(0).transform.position.x() - 110) < 1e-6);
        // Not tracking: where it is.
        Title off = titleOf(e);
        off.objects[0].tracking = false;
        setTitle(e, off);
        QVERIFY(std::abs(titleAt(e, 1.0, span).objects.at(0).transform.position.x() - 110) < 1e-6);
    }

    // Keyframes on a title's numbers (6.8c1; p.320; What's New 2026.8 p.9).
    void keyframes() {
        Effect e = titlerEffect();
        Title t;
        t.objects << textObject(t, QStringLiteral("HH"), QPointF(-400, 0));
        t.objects.last().font.size = 200;
        setTitle(e, t);
        const EffectSpan span(31);
        QCOMPARE(titleParameters(TitleObjectKind::Text).size(), 21);
        QVERIFY(!titleParameters(TitleObjectKind::Rectangle).contains(QStringLiteral("Size")));
        QVERIFY(titleParameters().contains(QStringLiteral("Background Opacity")));
        // Keyframes at the start for every number of the object; then moved at the end.
        QVERIFY(addTitleKeyframes(e, {QStringLiteral("Text 1")}, 0.0, span));
        QVERIFY(!addTitleKeyframes(e, {QStringLiteral("Text 1")}, 0.0, span));
        const QString cat = titleCategory(QStringLiteral("Text 1"));
        QCOMPARE(cat, QStringLiteral("Titler:Text 1"));
        QVERIFY(keyframesOf(e, cat, QStringLiteral("Position X")).fixed);
        Title end = titleAt(e, 1.0, span);
        end.objects[0].transform.position.setX(400);
        storeTitleAt(e, end, 1.0, span);
        QCOMPARE(keyframesOf(e, cat, QStringLiteral("Position X")).keys.size(), 2);
        // Numbers that did not change get no keyframe.
        QCOMPARE(keyframesOf(e, cat, QStringLiteral("Rotation")).keys.size(), 1);
        QVERIFY(std::abs(titleAt(e, 0.5, span).objects.at(0).transform.position.x()) < 1e-6);
        QCOMPARE(titleAt(e, 0.0, span).objects.at(0).transform.position.x(), -400.0);
        // Not keyframed: stored as it is.
        Title colour = titleAt(e, 0.5, span);
        colour.objects[0].fill.color.color = QStringLiteral("#ff0000");
        storeTitleAt(e, colour, 0.5, span);
        QCOMPARE(titleOf(e).objects.at(0).fill.color.color, QStringLiteral("#ff0000"));
        QCOMPARE(keyframesOf(e, cat, QStringLiteral("Position X")).keys.size(), 2);
        // The picture follows: the text moves right over the effect.
        auto inkX = [&](double time) {
            const QImage image = media::titlePicture(e, media::Picture::solid(kSize.width(), kSize.height(), Qt::black), time, span).toImage();
            int sum = 0, n = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (at(image, x, y).red() > 128) {
                        sum += x;
                        ++n;
                    }
                }
            }
            return n ? sum / n : -1;
        };
        const int start = inkX(0.0);
        const int middle = inkX(0.5);
        const int last = inkX(1.0);
        QVERIFY2(start >= 0 && start + 20 < middle && middle + 20 < last, qPrintable(QStringLiteral("%1 %2 %3").arg(start).arg(middle).arg(last)));
        // Delete Keyframe at the end; the title's own numbers.
        QVERIFY(removeTitleKeyframes(e, {QStringLiteral("Text 1")}, 1.0, span));
        QCOMPARE(keyframesOf(e, cat, QStringLiteral("Position X")).keys.size(), 1);
        QVERIFY(addTitleKeyframes(e, {}, 0.0, span));
        QCOMPARE(keyframesOf(e, titleCategory(QString()), QStringLiteral("Crop Top")).keys.size(), 1);
        QVERIFY(setTitleValue(end, QString(), QStringLiteral("Crop Top"), 12));
        QCOMPARE(titleValue(end, QString(), QStringLiteral("Crop Top")).value_or(0), 12.0);
        QVERIFY(!titleValue(end, QStringLiteral("Nope"), QStringLiteral("Opacity")));
        // Removing the object removes its keyframes.
        Title gone = titleOf(e);
        gone.objects.clear();
        setTitle(e, gone);
        QVERIFY(keyframesOf(e, cat, QStringLiteral("Position X")).keys.isEmpty());
        QCOMPARE(keyframesOf(e, titleCategory(QString()), QStringLiteral("Crop Top")).keys.size(), 1);
    }
};

QTEST_MAIN(TestTitler)
#include "tst_titler.moc"
