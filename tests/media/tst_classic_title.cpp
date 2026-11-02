// Classic Title Tool titles (ROADMAP 6.8d1; FX Guide p.327-341; src/model/classic_title.*, src/media/classic_title.*):
// the title's JSON, Safe Colors, and the picture: text placed from its top-left with its justification, kerning and
// leading, boxes, ovals and lines, transparency and the Preview's hard edges.
#include "lumenis/media/classic_title.h"
#include "lumenis/model/classic_title.h"

#include <QColor>
#include <QGuiApplication>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

// A 192 x 108 picture: title pixels / 10.
const QSize kSize(192, 108);

QColor at(const QImage& image, int x, int y) {
    return QColor::fromRgba(image.pixel(x, y));
}

// The picture's inked pixels' bounds.
QRect inked(const QImage& image) {
    QRect r;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 40) r = r.united(QRect(x, y, 1, 1));
        }
    }
    return r;
}

ClassicObject text(const QString& s, const QRectF& rect) {
    ClassicObject o;
    o.id = QStringLiteral("Text 1");
    o.kind = ClassicKind::Text;
    o.text = s;
    o.rect = rect;
    o.size = 200;
    return o;
}

} // namespace

class TestClassicTitle : public QObject {
    Q_OBJECT

private slots:
    void model() {
        ClassicTitle t;
        QCOMPARE(t.newId(ClassicKind::Box), QStringLiteral("Box 1"));
        t.objects << text(QStringLiteral("Hello"), QRectF(100, 100, 800, 0));
        t.objects[0].bold = true;
        t.objects[0].justify = ClassicJustify::Center;
        t.objects[0].kerning = 10;
        t.objects[0].runs << TitleRun{0, 2, TitleCharFormat{}};
        t.objects[0].runs[0].format.size = 300;
        ClassicObject box;
        box.id = t.newId(ClassicKind::Box);
        box.kind = ClassicKind::Box;
        box.rect = QRectF(10, 20, 30, 40);
        box.color = QStringLiteral("#ff0000");
        box.transparency = 50;
        t.objects << box;
        t.videoBackground = false;
        t.backgroundColor = QStringLiteral("#0000ff");
        QCOMPARE(ClassicTitle::fromJson(t.toJson()), t);
        QCOMPARE(t.newId(ClassicKind::Box), QStringLiteral("Box 2"));
        QVERIFY(t.object(QStringLiteral("Box 1")));
        QCOMPARE(classicStyle(true, true), QStringLiteral("Bold Italic"));
        QVERIFY(styleIsBold(QStringLiteral("Bold")) && !styleIsItalic(QStringLiteral("Bold")));
        // "displays only low saturation colors" (p.333): pure red is toned down, grey stays.
        const QColor safe(safeColor(QStringLiteral("#ff0000")));
        QVERIFY(safe.saturationF() <= 0.76f && safe.valueF() <= 0.91f);
        QVERIFY(safe.red() > safe.green());
        QCOMPARE(safeColor(QStringLiteral("#808080")), QStringLiteral("#808080"));
    }

    void picture() {
        // Text from its top-left: a left-justified line begins at the object's left and goes down from its top.
        ClassicTitle t;
        t.objects << text(QStringLiteral("HHH"), QRectF(400, 300, 1200, 0));
        const QRect left = inked(media::classicTitleImage(t, kSize));
        QVERIFY2(std::abs(left.left() - 40) <= 2, qPrintable(QString::number(left.left())));
        QVERIFY2(left.top() >= 30 && left.top() <= 36, qPrintable(QString::number(left.top())));
        // Its bounds: the width given, the lines' height.
        const QRectF bounds = media::classicObjectBounds(t.objects.at(0));
        QCOMPARE(bounds.left(), 400.0);
        QCOMPARE(bounds.width(), 1200.0);
        QVERIFY(bounds.height() > 150 && bounds.height() < 320);
        // Centred within the width; right justified at its right.
        t.objects[0].justify = ClassicJustify::Center;
        const QRect centre = inked(media::classicTitleImage(t, kSize));
        QVERIFY(std::abs(centre.center().x() - 100) <= 2);
        // Right justified: the line's advance ends at the object's right; the ink stops short of it by the last
        // glyph's side bearing, which differs between the systems' default fonts.
        t.objects[0].justify = ClassicJustify::Right;
        const int right = inked(media::classicTitleImage(t, kSize)).right();
        QVERIFY2(right <= 161 && right >= 154, qPrintable(QString::number(right)));
        t.objects[0].justify = ClassicJustify::Left;
        // Kerning in percent of the point size widens the line; leading spaces the lines.
        t.objects[0].kerning = 50;
        QVERIFY(inked(media::classicTitleImage(t, kSize)).width() > left.width() + 15);
        t.objects[0].kerning = 0;
        t.objects[0].text = QStringLiteral("H\nH");
        const int twoLines = inked(media::classicTitleImage(t, kSize)).height();
        t.objects[0].leading = 100;
        QVERIFY(inked(media::classicTitleImage(t, kSize)).height() >= twoLines + 8);
        // Wrapping at the object's width.
        t.objects[0].leading = 0;
        t.objects[0].text = QStringLiteral("HH HH HH");
        t.objects[0].rect.setWidth(400);
        QVERIFY(media::classicObjectBounds(t.objects.at(0)).height() > 400);
        // A box, half transparent red, over nothing; an oval; a line.
        ClassicTitle shapes;
        ClassicObject box;
        box.kind = ClassicKind::Box;
        box.id = QStringLiteral("Box 1");
        box.rect = QRectF(100, 100, 400, 300);
        box.color = QStringLiteral("#ff0000");
        box.transparency = 50;
        shapes.objects << box;
        const QImage boxed = media::classicTitleImage(shapes, kSize).convertToFormat(QImage::Format_ARGB32);
        QVERIFY(std::abs(at(boxed, 30, 25).alpha() - 128) <= 2);
        QCOMPARE(at(boxed, 30, 25).red(), 255);
        QCOMPARE(at(boxed, 60, 25).alpha(), 0);
        ClassicObject oval = box;
        oval.kind = ClassicKind::Oval;
        oval.transparency = 0;
        oval.rect = QRectF(1000, 100, 400, 400);
        ClassicObject line = box;
        line.kind = ClassicKind::Line;
        line.transparency = 0;
        line.rect = QRectF(QPointF(100, 800), QPointF(1800, 800));
        shapes.objects << oval << line;
        const QImage drawn = media::classicTitleImage(shapes, kSize);
        QCOMPARE(at(drawn, 120, 30).alpha(), 255); // the oval's middle
        QCOMPARE(at(drawn, 101, 11).alpha(), 0);   // outside its corner
        QVERIFY(at(drawn, 100, 80).alpha() > 0);   // the line
        // Preview off: hard edges, no part-covered pixels.
        ClassicTitle round;
        round.objects << oval;
        const QImage hard = media::classicTitleImage(round, kSize, false);
        for (int y = 0; y < hard.height(); ++y) {
            for (int x = 0; x < hard.width(); ++x) {
                const int a = qAlpha(hard.pixel(x, y));
                QVERIFY2(a == 0 || a == 255, qPrintable(QStringLiteral("%1,%2: %3").arg(x).arg(y).arg(a)));
            }
        }
    }

    // Rolling and crawling titles' pages (6.8d2, p.342-346).
    void pages() {
        QCOMPARE(classicPageLimit(ClassicMotion::Rolling), 29);
        QCOMPARE(classicPageLimit(ClassicMotion::Crawling), 4);
        QCOMPARE(classicPageLimit(ClassicMotion::Rolling, 720), 44);
        QCOMPARE(classicPageLimit(ClassicMotion::Crawling, 486), 11);
        QCOMPARE(classicPageLimit(ClassicMotion::Rolling, 576), 56);
        QCOMPARE(classicPageOrigin(ClassicMotion::Rolling, 3), QPointF(0, 2160));
        QCOMPARE(classicPageAt(ClassicMotion::Crawling, QPointF(1920, 10)), 2);
        ClassicTitle t;
        t.motion = ClassicMotion::Rolling;
        ClassicObject a;
        a.kind = ClassicKind::Box;
        a.id = QStringLiteral("Box 1");
        a.rect = QRectF(100, 100, 200, 200);
        ClassicObject b = a;
        b.id = QStringLiteral("Box 2");
        b.rect = QRectF(100, 2200, 200, 200); // page 3
        t.objects << a << b;
        QCOMPARE(media::classicContentPages(t), 3);
        t.pages = 3;
        QCOMPARE(ClassicTitle::fromJson(t.toJson()), t);
        // Insert a page at 2: Box 2 moves down a page; not at a place an object crosses.
        QVERIFY(media::insertClassicPage(t, 2));
        QCOMPARE(t.pages, 4);
        QCOMPARE(t.objects.at(1).rect.top(), 3280.0);
        t.objects[0].rect = QRectF(100, 1000, 200, 200); // across page 2's start
        QVERIFY(!media::insertClassicPage(t, 2));
        t.objects[0].rect = QRectF(100, 100, 200, 200);
        // Remove an empty page; not one with something on it.
        QVERIFY(!media::removeClassicPage(t, 4));
        QVERIFY(media::removeClassicPage(t, 2));
        QCOMPARE(t.objects.at(1).rect.top(), 2200.0);
        QCOMPARE(t.pages, 3);
        // Copy and move to a page, adding pages; not past the limit.
        QVERIFY(media::placeOnClassicPage(t, QStringLiteral("Box 1"), 5, true));
        QCOMPARE(t.objects.size(), 3);
        QCOMPARE(t.objects.at(2).rect.top(), 4420.0);
        QCOMPARE(t.pages, 5);
        QVERIFY(media::placeOnClassicPage(t, QStringLiteral("Box 1"), 2, false));
        QCOMPARE(t.objects.at(0).rect.top(), 1180.0);
        QVERIFY(!media::placeOnClassicPage(t, QStringLiteral("Box 1"), 30, false));
        // Drawn from a page down: what is on page 2 shows at the top.
        ClassicTitle shown;
        shown.motion = ClassicMotion::Rolling;
        ClassicObject box = a;
        box.rect = QRectF(0, 1080, 1920, 540);
        shown.objects << box;
        const QImage page2 = media::classicTitleImage(shown, kSize, true, QPointF(0, 1080));
        QCOMPARE(qAlpha(page2.pixel(10, 10)), 255);
        QCOMPARE(qAlpha(page2.pixel(10, 80)), 0);
        QCOMPARE(qAlpha(media::classicTitleImage(shown, kSize).pixel(10, 10)), 0);
        // Make Crawl: one line, as wide as its text, on the lower third.
        ClassicObject crawl = text(QStringLiteral("One\nTwo"), QRectF(100, 100, 500, 0));
        QVERIFY(media::makeClassicCrawl(crawl));
        QCOMPARE(crawl.text, QStringLiteral("One Two"));
        QCOMPARE(crawl.rect.top(), 720.0);
        QVERIFY(crawl.rect.width() > 500);
        QVERIFY(media::classicObjectBounds(crawl).height() < 300); // one line
    }

    // The Text tool's places: the cursor between characters, highlights over them.
    void textPlaces() {
        const ClassicObject o = text(QStringLiteral("HHHH"), QRectF(400, 300, 1200, 0));
        QCOMPARE(media::classicTextIndexAt(o, QPointF(380, 350)), 0);
        QCOMPARE(media::classicTextIndexAt(o, QPointF(1900, 350)), 4);
        const QLineF start = media::classicTextCaret(o, 0);
        QVERIFY(std::abs(start.x1() - 400) < 1.0);
        QVERIFY(std::abs(start.y1() - 300) < 1.0);
        const auto range = media::classicTextRange(o, 1, 3);
        QCOMPARE(range.size(), 1);
        QVERIFY(range.first().boundingRect().left() > 400);
        QCOMPARE(media::classicTextIndexAt(o, range.first().boundingRect().topLeft() + QPointF(2, 20)), 1);
    }
};

QTEST_MAIN(TestClassicTitle)
#include "tst_classic_title.moc"
