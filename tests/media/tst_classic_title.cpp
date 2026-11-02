// Classic Title Tool titles (ROADMAP 6.8d1; FX Guide p.327-341; src/model/classic_title.*, src/media/classic_title.*):
// the title's JSON, Safe Colors, and the picture: text placed from its top-left with its justification, kerning and
// leading, boxes, ovals and lines, transparency and the Preview's hard edges; rolling and crawling titles' pages
// (6.8d2); line attributes, colour and transparency blends and shadows (6.8d3a); layers, groups, locks, alignment and
// distributing (6.8d3b).
#include "lumenis/media/classic_title.h"
#include "lumenis/model/classic_title.h"

#include <QColor>
#include <QGuiApplication>
#include <QLineF>
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
        box.fill.color = QStringLiteral("#ff0000");
        box.fill.transparency = 50;
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
        box.fill.color = QStringLiteral("#ff0000");
        box.fill.transparency = 50;
        shapes.objects << box;
        const QImage boxed = media::classicTitleImage(shapes, kSize).convertToFormat(QImage::Format_ARGB32);
        QVERIFY(std::abs(at(boxed, 30, 25).alpha() - 128) <= 2);
        QCOMPARE(at(boxed, 30, 25).red(), 255);
        QCOMPARE(at(boxed, 60, 25).alpha(), 0);
        ClassicObject oval = box;
        oval.kind = ClassicKind::Oval;
        oval.fill.transparency = 0;
        oval.rect = QRectF(1000, 100, 400, 400);
        ClassicObject line = box;
        line.kind = ClassicKind::Line;
        line.fill.transparency = 0;
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
    // Line attributes, colour and transparency blends, shadows (6.8d3, p.346-355).
    void attributes() {
        // The JSON keeps them.
        ClassicTitle t;
        ClassicObject box;
        box.id = QStringLiteral("Box 1");
        box.kind = ClassicKind::Box;
        box.rect = QRectF(100, 100, 800, 800);
        box.cornerRadius = 300;
        box.borderWidth = 20;
        box.border.color = QStringLiteral("#ff0000");
        box.border.transparency2 = 50;
        box.border.transparencyAngle = 90;
        box.fill.color2 = QStringLiteral("#0000ff");
        box.fill.colorAngle = 45;
        box.depthShadow = true;
        box.shadowDepth = 12;
        box.shadowAngle = 90;
        box.shadowSoftness = 8;
        box.shadowColor = QStringLiteral("#123456");
        box.shadowTransparency = 25;
        ClassicObject line;
        line.id = QStringLiteral("Line 1");
        line.kind = ClassicKind::Line;
        line.rect = QRectF(QPointF(200, 500), QPointF(1600, 500));
        line.lineWidth = 20;
        line.arrows = ClassicArrows::Both;
        line.arrowSize = QSizeF(4, 2);
        t.objects << box << line;
        QCOMPARE(ClassicTitle::fromJson(t.toJson()), t);
        // The shadow's offset from its depth and direction, and back.
        ClassicObject s;
        s.shadowDepth = 10;
        s.shadowAngle = 0;
        QVERIFY(QLineF(s.shadowOffset(), QPointF(10, 0)).length() < 1e-9);
        s.setShadowOffset(QPointF(0, 5));
        QCOMPARE(s.shadowDepth, 5.0);
        QCOMPARE(s.shadowAngle, 90.0);
        s.setShadowOffset(QPointF()); // the direction stays
        QCOMPARE(s.shadowAngle, 90.0);

        auto draw = [](const ClassicObject& o) {
            ClassicTitle one;
            one.objects << o;
            return media::classicTitleImage(one, kSize);
        };
        // Rounded corners: the corner's pixel is clear, the square box's is not.
        ClassicObject square;
        square.id = QStringLiteral("Box 1");
        square.kind = ClassicKind::Box;
        square.rect = QRectF(100, 100, 800, 800);
        QVERIFY(at(draw(square), 11, 11).alpha() == 255);
        ClassicObject round = square;
        round.cornerRadius = 300;
        QCOMPARE(at(draw(round), 11, 11).alpha(), 0);
        QCOMPARE(at(draw(round), 50, 50).alpha(), 255);
        // A border outside the box; the fill over it inside.
        ClassicObject bordered = square;
        bordered.borderWidth = 50;
        bordered.border.color = QStringLiteral("#ff0000");
        const QImage b = draw(bordered);
        QCOMPARE(at(b, 92, 50), QColor(Qt::red));
        QCOMPARE(at(b, 88, 50), QColor(Qt::white));
        QCOMPARE(at(draw(square), 92, 50).alpha(), 0);
        // Remove Border: none for lines either.
        QVERIFY(media::classicBorderShape(line).isEmpty());
        // An arrowhead wider than the line at its end.
        ClassicObject plain = line;
        plain.arrows = ClassicArrows::None;
        ClassicObject arrow = plain;
        arrow.arrows = ClassicArrows::End;
        arrow.arrowSize = QSizeF(3, 3);
        QCOMPARE(at(draw(plain), 155, 51).alpha(), 0);
        QVERIFY(at(draw(arrow), 155, 51).alpha() > 200);
        QVERIFY(at(draw(arrow), 100, 50).alpha() > 200);  // the line itself
        QCOMPARE(at(draw(arrow), 100, 52).alpha(), 0);     // as wide as its width
        QVERIFY(media::classicObjectShape(arrow).boundingRect().height() >= 59.0);
        // A colour blend from red at the left to blue at the right.
        ClassicObject full = square;
        full.rect = QRectF(0, 0, model::kClassicWidth, model::kClassicHeight);
        full.fill.color = QStringLiteral("#ff0000");
        full.fill.color2 = QStringLiteral("#0000ff");
        QImage g = draw(full);
        QVERIFY(at(g, 1, 50).red() > 240 && at(g, 1, 50).blue() < 15);
        QVERIFY(at(g, 190, 50).blue() > 240 && at(g, 190, 50).red() < 15);
        QVERIFY(std::abs(at(g, 96, 50).red() - at(g, 96, 50).blue()) < 12);
        // Turned to run downwards.
        full.fill.colorAngle = 90;
        g = draw(full);
        QVERIFY(at(g, 96, 1).red() > 240 && at(g, 96, 106).blue() > 240);
        // A transparency blend, opaque at the top to clear at the bottom.
        full.fill.color2.clear();
        full.fill.transparency2 = 100;
        full.fill.transparencyAngle = 90;
        g = draw(full);
        QVERIFY(at(g, 96, 1).alpha() > 245);
        QVERIFY(at(g, 96, 106).alpha() < 10);
        QVERIFY(std::abs(at(g, 96, 54).alpha() - 127) < 8);
        // A drop shadow moved by its offset; a depth shadow swept from the object to it.
        ClassicObject lit = square;
        lit.rect = QRectF(500, 500, 200, 200);
        lit.shadowDepth = std::hypot(70.0, 70.0);
        lit.shadowAngle = 45;
        QCOMPARE(at(draw(lit), 71, 53).alpha(), 0);
        QCOMPARE(at(draw(lit), 75, 75), QColor(Qt::black));
        lit.depthShadow = true;
        QCOMPARE(at(draw(lit), 71, 53), QColor(Qt::black));
        // Its colour and transparency.
        lit.shadowColor = QStringLiteral("#00ff00");
        lit.shadowTransparency = 50;
        const QColor half = at(draw(lit).convertToFormat(QImage::Format_ARGB32), 75, 75);
        QVERIFY(std::abs(half.alpha() - 128) < 4);
        QVERIFY(half.green() > 240 && half.red() < 10);
        // A glow: no depth, some softness; soft beyond the object's edge.
        ClassicObject glow = square;
        glow.rect = QRectF(500, 500, 200, 200);
        QCOMPARE(at(draw(glow), 72, 60).alpha(), 0);
        glow.shadowSoftness = 20;
        const int soft = at(draw(glow), 72, 60).alpha();
        QVERIFY2(soft > 0 && soft < 255, qPrintable(QString::number(soft)));
        // Text with a border.
        ClassicObject word = text(QStringLiteral("I"), QRectF(800, 300, 800, 0));
        word.size = 400;
        const QRect plainInk = inked(draw(word));
        word.borderWidth = 30;
        const QRect borderInk = inked(draw(word));
        QVERIFY(borderInk.contains(plainInk));
        QVERIFY(borderInk.width() >= plainInk.width() + 4);
    }

    // Layers, groups, locks, alignment and distributing (6.8d3b, p.355-357).
    void manipulating() {
        ClassicTitle t;
        auto add = [&](const QRectF& r) {
            ClassicObject o;
            o.kind = ClassicKind::Box;
            o.id = t.newId(ClassicKind::Box);
            o.rect = r;
            t.objects << o;
            return o.id;
        };
        const QString a = add(QRectF(100, 100, 100, 100)), b = add(QRectF(400, 300, 200, 100)), c = add(QRectF(900, 500, 50, 50)),
                      d = add(QRectF(1200, 200, 100, 300));
        auto order = [&] {
            QStringList ids;
            for (const ClassicObject& o : t.objects) ids << o.id;
            return ids;
        };
        // Layers: the chosen keep their order among themselves.
        QVERIFY(reorderClassicObjects(t, {a, b}, ClassicLayer::ToFront));
        QCOMPARE(order(), (QStringList{c, d, a, b}));
        QVERIFY(reorderClassicObjects(t, {b}, ClassicLayer::ToBack));
        QCOMPARE(order(), (QStringList{b, c, d, a}));
        QVERIFY(reorderClassicObjects(t, {c}, ClassicLayer::Forward));
        QCOMPARE(order(), (QStringList{b, d, c, a}));
        QVERIFY(reorderClassicObjects(t, {a}, ClassicLayer::Backward));
        QCOMPARE(order(), (QStringList{b, d, a, c}));
        QVERIFY(!reorderClassicObjects(t, {b}, ClassicLayer::ToBack));
        // Groups: one level, members found from any of them; the JSON keeps them.
        QVERIFY(!groupClassicObjects(t, {a}));
        QVERIFY(groupClassicObjects(t, {a, b}));
        QCOMPARE(t.object(a)->group, QStringLiteral("Group 1"));
        QCOMPARE(t.withGroups({b}), (QStringList{b, a}));
        QCOMPARE(t.newGroupId(), QStringLiteral("Group 2"));
        QVERIFY(groupClassicObjects(t, {b, c})); // merging
        QCOMPARE(t.withGroups({c}).size(), 3);
        QCOMPARE(ClassicTitle::fromJson(t.toJson()), t);
        QVERIFY(ungroupClassicObjects(t, {a}));
        QVERIFY(t.object(c)->group.isEmpty());
        QVERIFY(!ungroupClassicObjects(t, {a}));
        // Locks: changes and deletions put back; locking and unlocking go through.
        t.object(d)->locked = true;
        ClassicTitle before = t;
        t.object(d)->rect.translate(50, 0);
        t.object(d)->fill.color = QStringLiteral("#ff0000");
        t.objects.removeIf([&](const ClassicObject& o) { return o.id == d || o.id == a; });
        keepLockedObjects(t, before);
        QVERIFY(t.object(d));
        QCOMPARE(t.object(d)->rect, QRectF(1200, 200, 100, 300));
        QCOMPARE(t.object(d)->fill.color, QStringLiteral("#ffffff"));
        QVERIFY(!t.object(a)); // not locked: gone
        before = t;
        t.object(d)->locked = false;
        keepLockedObjects(t, before);
        QVERIFY(!t.object(d)->locked);
        QCOMPARE(ClassicTitle::fromJson(before.toJson()), before);

        // Alignment to the frame's safe title area and to the last selected object.
        ClassicTitle m;
        auto box = [&](const QRectF& r) {
            ClassicObject o;
            o.kind = ClassicKind::Box;
            o.id = m.newId(ClassicKind::Box);
            o.rect = r;
            m.objects << o;
            return o.id;
        };
        const QString p = box(QRectF(300, 300, 100, 100)), q = box(QRectF(700, 500, 200, 50)), r = box(QRectF(1000, 200, 50, 300));
        const QRectF safe(192, 108, 1536, 864);
        QVERIFY(media::alignClassicObjects(m, {p, q}, media::ClassicAlign::FrameLeft, safe));
        QCOMPARE(m.object(p)->rect.left(), 192.0);
        QCOMPARE(m.object(q)->rect.left(), 192.0);
        QVERIFY(media::alignClassicObjects(m, {q}, media::ClassicAlign::FrameBottom, safe));
        QCOMPARE(m.object(q)->rect.bottom(), 972.0);
        QVERIFY(media::alignClassicObjects(m, {r}, media::ClassicAlign::FrameCenterH, safe));
        QCOMPARE(m.object(r)->rect.center().x(), 960.0);
        QVERIFY(media::alignClassicObjects(m, {r}, media::ClassicAlign::FrameCenterV, safe));
        QCOMPARE(m.object(r)->rect.center().y(), 540.0);
        // The reference is the last selected; one object alone has nothing to align to.
        QVERIFY(!media::alignClassicObjects(m, {p}, media::ClassicAlign::ObjectsTop, safe));
        QVERIFY(media::alignClassicObjects(m, {p, q, r}, media::ClassicAlign::ObjectsTop, safe));
        QCOMPARE(m.object(p)->rect.top(), m.object(r)->rect.top());
        QCOMPARE(m.object(q)->rect.top(), m.object(r)->rect.top());
        QVERIFY(media::alignClassicObjects(m, {r, p}, media::ClassicAlign::ObjectsRight, safe));
        QCOMPARE(m.object(r)->rect.right(), m.object(p)->rect.right());
        // A group moves as one; a locked object stays.
        QVERIFY(groupClassicObjects(m, {p, q}));
        const double gap = m.object(q)->rect.left() - m.object(p)->rect.left();
        m.object(r)->locked = true;
        const QRectF stays = m.object(r)->rect;
        QVERIFY(media::alignClassicObjects(m, {p, r}, media::ClassicAlign::FrameRight, safe));
        QCOMPARE(m.object(r)->rect, stays);
        QCOMPARE(std::max(m.object(p)->rect.right(), m.object(q)->rect.right()), 1728.0);
        QCOMPARE(m.object(q)->rect.left() - m.object(p)->rect.left(), gap);

        // Distributing: "at least three objects".
        ClassicTitle n;
        auto item = [&](const QRectF& rect) {
            ClassicObject o;
            o.kind = ClassicKind::Oval;
            o.id = n.newId(ClassicKind::Oval);
            o.rect = rect;
            n.objects << o;
            return o.id;
        };
        const QString o1 = item(QRectF(800, 100, 100, 100)), o2 = item(QRectF(300, 400, 200, 100)), o3 = item(QRectF(1000, 700, 100, 100));
        QVERIFY(!media::distributeClassicObjects(n, {o1, o2}, media::ClassicDistribute::LeftToRight, safe));
        QVERIFY(media::distributeClassicObjects(n, {o1, o2, o3}, media::ClassicDistribute::LeftToRight, safe));
        // Ordered by place: o2, o1, o3; the first at the left edge, the last at the right, gaps equal.
        QCOMPARE(n.object(o2)->rect.left(), 192.0);
        QCOMPARE(n.object(o3)->rect.right(), 1728.0);
        QVERIFY(std::abs((n.object(o1)->rect.left() - n.object(o2)->rect.right()) - (n.object(o3)->rect.left() - n.object(o1)->rect.right())) < 1e-6);
        QVERIFY(media::distributeClassicObjects(n, {o1, o2, o3}, media::ClassicDistribute::TopToBottom, safe));
        QCOMPARE(n.object(o1)->rect.top(), 108.0);
        QCOMPARE(n.object(o3)->rect.bottom(), 972.0);
        // First to Last: the middle one halfway between the first and the last selected.
        QVERIFY(media::distributeClassicObjects(n, {o1, o2, o3}, media::ClassicDistribute::FirstToLast, safe));
        const QPointF mid = (n.object(o1)->rect.center() + n.object(o3)->rect.center()) / 2.0;
        QVERIFY(QLineF(n.object(o2)->rect.center(), mid).length() < 1e-6);
    }

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
