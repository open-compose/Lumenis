// The Intraframe effects' objects and pictures (ROADMAP 6.9a; FX Guide ch.15; docs/spec/intraframe.md): the model's
// objects, shapes and places (model/intraframe.h) and the Paint, Blur, Mosaic and AniMatte pictures (intraframe.cpp).
#include "lumenis/media/compositor.h"
#include "lumenis/media/pict.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/intraframe.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QPainterPath>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;
using model::ControlPoint;
using model::Effect;
using model::EffectSpan;
using model::IntraframeKind;

namespace {

QColor at(const Picture& p, int x, int y) {
    return p.toImage().pixelColor(x, y);
}

bool near(const QColor& a, const QColor& b, int tolerance = 3) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance && std::abs(a.blue() - b.blue()) <= tolerance;
}

void setParameter(Effect& e, const QString& id, const QString& category, const QString& name, const QJsonValue& value) {
    QList<model::IntraframeObject> objects = model::intraframeObjects(e);
    for (model::IntraframeObject& o : objects) {
        if (o.id != id) continue;
        QJsonObject c = o.parameters.value(category).toObject();
        c.insert(name, value);
        o.parameters.insert(category, c);
    }
    model::setIntraframeObjects(e, objects);
}

} // namespace

class TestIntraframe : public QObject {
    Q_OBJECT

private slots:
    void objects() {
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        QVERIFY(model::isIntraframeEffect(paint));
        QVERIFY(model::isIntraframeEffect(Effect{QStringLiteral("Key"), QStringLiteral("AniMatte"), {}}));
        QVERIFY(!model::isIntraframeEffect(Effect{QStringLiteral("Image"), QStringLiteral("Resize"), {}}));
        // A new object takes the effect's parameters as its own.
        paint.parameters.insert(QStringLiteral("Paint Effect Mode Parameters"), QJsonObject{{QStringLiteral("Fast menu"), QStringLiteral("Invert")}});
        const QString a = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.1, 0.1, 0.2, 0.2)));
        const QString b = model::addIntraframeObject(paint, IntraframeKind::Oval, model::ovalShape(QRectF(0.5, 0.5, 0.2, 0.2)));
        const QString c = model::addIntraframeObject(paint, IntraframeKind::Brush, model::polylineShape({QPointF(0, 0), QPointF(1, 1)}));
        auto objects = model::intraframeObjects(paint);
        QCOMPARE(objects.size(), 3);
        QCOMPARE(objects.at(0).id, a);
        QCOMPARE(objects.at(0).shapes.at(0).points.size(), 4);
        QCOMPARE(objects.at(1).shapes.at(0).points.at(0).join, model::JoinType::Smooth);
        QVERIFY(!objects.at(2).closed);
        QCOMPARE(model::objectEffect(paint, objects.at(0)).parameters.value(QStringLiteral("Paint Effect Mode Parameters")).toObject().value(QStringLiteral("Fast menu")).toString(),
                 QStringLiteral("Invert"));
        QVERIFY(!objects.at(0).parameters.contains(model::kIntraframeCategory));
        // JSON round trip.
        QCOMPARE(model::IntraframeObject::fromJson(objects.at(1).toJson()), objects.at(1));
        // Layers (p.417).
        QVERIFY(model::reorderIntraframeObject(paint, a, model::IntraframeLayer::Front));
        QCOMPARE(model::intraframeObjects(paint).constLast().id, a);
        QVERIFY(!model::reorderIntraframeObject(paint, a, model::IntraframeLayer::Forward));
        QVERIFY(model::reorderIntraframeObject(paint, a, model::IntraframeLayer::Backward));
        QCOMPARE(model::intraframeObjects(paint).at(1).id, a);
        // Groups and locks (p.417-418): locked objects are not deleted; ungrouping one ungroups all.
        model::groupIntraframeObjects(paint, {a, b});
        objects = model::intraframeObjects(paint);
        QVERIFY(!objects.at(0).group.isEmpty());
        QCOMPARE(objects.at(0).group, objects.at(1).group);
        model::lockIntraframeObjects(paint, {a}, true);
        QVERIFY(model::intraframeObjects(paint).at(0).locked && model::intraframeObjects(paint).at(1).locked); // the group
        QCOMPARE(model::deleteIntraframeObjects(paint, {a, c}), 1);
        model::ungroupIntraframeObjects(paint, {b});
        QVERIFY(model::intraframeObjects(paint).at(0).group.isEmpty());
        QCOMPARE(model::intraframeObjects(paint).size(), 2);
    }

    void shapesAndPlaces() {
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        const QString id = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 0.2, 0.2)));
        // A second shape at the end: halfway, the corners are halfway (p.416).
        auto objects = model::intraframeObjects(paint);
        objects[0].shapes << model::IntraframeShape{1.0, model::rectangleShape(QRectF(0.4, 0.0, 0.2, 0.2))};
        model::setIntraframeObjects(paint, objects);
        const auto halfway = model::shapeAt(model::intraframeObjects(paint).at(0), 0.5);
        QVERIFY(std::abs(halfway.at(0).p.x() - 0.2) < 1e-9);
        QCOMPARE(model::shapeAt(model::intraframeObjects(paint).at(0), -1.0).at(0).p, QPointF(0.0, 0.0));
        // X 960 of 1920 moves it half the width; Scale 200 doubles it about its centre.
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("X"), 960);
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("Scale"), 200);
        const auto placed = model::placedShapeAt(paint, model::intraframeObjects(paint).at(0), 0.0, EffectSpan(10), 16.0 / 9.0);
        QVERIFY(std::abs(placed.at(0).p.x() - (0.5 - 0.1)) < 1e-9);
        QVERIFY(std::abs(placed.at(2).p.x() - (0.5 + 0.3)) < 1e-9);
        QVERIFY(std::abs(placed.at(2).p.y() - 0.3) < 1e-9);
        // Z rotation by 90 degrees keeps the shape square in pixels.
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("Scale"), 100);
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("X"), 0);
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("Rotation"), 90);
        const auto turned = model::placedShapeAt(paint, model::intraframeObjects(paint).at(0), 0.0, EffectSpan(10), 2.0);
        const QRectF b = model::shapeBounds(turned);
        // 0.2 by 0.2 of a frame twice as wide as tall is 0.2 by 0.1 widths; turned, 0.1 by 0.2 widths: 0.1 of the width
        // by 0.4 of the height.
        QVERIFY(std::abs(b.width() - 0.1) < 1e-9 && std::abs(b.height() - 0.4) < 1e-9);
    }

    void reshaping() {
        // Control points (p.405-411): a point added on a straight edge splits it; one added on a curve keeps its shape;
        // removing keeps at least three; Alt+click joins; direction handles move in unison (p.409).
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 0.4, 0.4)));
        model::IntraframeObject box = model::intraframeObjects(paint).at(0);
        model::insertControlPoint(box, 0, 0.5);
        QCOMPARE(box.shapes.at(0).points.size(), 5);
        QCOMPARE(box.shapes.at(0).points.at(1).p, QPointF(0.2, 0.0));
        const auto near1 = model::nearestSegment(box, 0.0, QPointF(0.4, 0.21), 0.02);
        QVERIFY(near1 && near1->first == 2 && std::abs(near1->second - 0.5) < 0.05);
        QVERIFY(model::removeControlPoints(box, {1}));
        QVERIFY(!model::removeControlPoints(box, {0, 1}));
        QCOMPARE(box.shapes.at(0).points.size(), 4);
        model::toggleSmooth(box, 0, 0, true);
        QCOMPARE(box.shapes.at(0).points.at(0).join, model::JoinType::Smooth);
        QVERIFY(!box.shapes.at(0).points.at(0).out.isNull());
        model::toggleCorner(box, 0, 0);
        QCOMPARE(box.shapes.at(0).points.at(0).join, model::JoinType::Corner);
        model::toggleCorner(box, 0, 0);
        QCOMPARE(box.shapes.at(0).points.at(0).join, model::JoinType::Smooth);
        model::toggleSmooth(box, 0, 0, true);
        QCOMPARE(box.shapes.at(0).points.at(0).join, model::JoinType::Straight);
        // Handles: smooth moves the other bar in unison with its length; fixedLength keeps the other length; corner
        // leaves it.
        model::ControlPoint c{QPointF(0.5, 0.5), QPointF(-0.1, 0.0), QPointF(0.1, 0.0), model::JoinType::Smooth, false};
        model::setDirectionHandle(c, true, QPointF(0.0, 0.2));
        QVERIFY(std::abs(c.in.x()) < 1e-12 && std::abs(c.in.y() + 0.2) < 1e-12);
        c.fixedLength = true;
        model::setDirectionHandle(c, true, QPointF(0.3, 0.0));
        QVERIFY(std::abs(c.in.x() + 0.2) < 1e-12);
        c.join = model::JoinType::Corner;
        model::setDirectionHandle(c, false, QPointF(0.0, 0.1));
        QVERIFY(std::abs(c.out.x() - 0.3) < 1e-12);
        // A curve split at its middle passes through the same point.
        model::IntraframeObject oval = box;
        oval.shapes[0].points = model::ovalShape(QRectF(0.0, 0.0, 0.4, 0.4));
        const QPainterPath before = intraframePath(oval.shapes.at(0).points, true, QSizeF(100, 100));
        model::insertControlPoint(oval, 0, 0.5);
        const QPainterPath after = intraframePath(oval.shapes.at(0).points, true, QSizeF(100, 100));
        QVERIFY(std::abs(before.boundingRect().width() - after.boundingRect().width()) < 0.5);
        // Editing a keyframed shape between keyframes adds a shape keyframe there (p.411).
        model::IntraframeObject moving = model::intraframeObjects(paint).at(0);
        moving.shapes << model::IntraframeShape{1.0, model::rectangleShape(QRectF(0.5, 0.0, 0.4, 0.4))};
        const int shape = model::editableShape(moving, 0.5, EffectSpan(10));
        QCOMPARE(moving.shapes.size(), 3);
        QCOMPARE(moving.shapes.at(shape).at, 0.5);
        // unplacedPoint undoes the place.
        setParameter(paint, model::intraframeObjects(paint).at(0).id, model::kIntraframeObjectCategory, QStringLiteral("Rotation"), 30);
        setParameter(paint, model::intraframeObjects(paint).at(0).id, model::kIntraframeObjectCategory, QStringLiteral("X"), 100);
        const model::IntraframeObject turned = model::intraframeObjects(paint).at(0);
        const auto placed = model::placedShapeAt(paint, turned, 0.0, EffectSpan(10), 16.0 / 9.0);
        const QPointF back = model::unplacedPoint(paint, turned, 0.0, EffectSpan(10), 16.0 / 9.0, placed.at(2).p);
        QVERIFY(std::hypot(back.x() - 0.4, back.y() - 0.4) < 1e-9);
    }

    void paintModes() {
        // A grey picture with a Solid red rectangle over its left half (p.538).
        const Picture grey = Picture::solid(64, 36, QColor(128, 128, 128));
        const Picture blue = Picture::solid(64, 36, QColor(0, 0, 255));
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        QVERIFY(near(at(intraframePicture(paint, grey, blue, 0.0, EffectSpan(10)), 10, 10), QColor(128, 128, 128))); // no objects
        paint.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#ff0000")}});
        const QString box = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 0.5, 1.0)));
        Picture shown = intraframePicture(paint, grey, blue, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 10, 10), QColor(255, 0, 0)));
        QVERIFY(near(at(shown, 50, 10), QColor(128, 128, 128)));
        // Opacity 50.
        setParameter(paint, box, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Opac."), 50);
        QVERIFY(near(at(intraframePicture(paint, grey, blue, 0.0, EffectSpan(10)), 10, 10), QColor(191, 64, 64), 4));
        setParameter(paint, box, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Opac."), 100);
        // Object Visible off: nothing.
        setParameter(paint, box, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Object Visible"), false);
        QVERIFY(near(at(intraframePicture(paint, grey, blue, 0.0, EffectSpan(10)), 10, 10), QColor(128, 128, 128)));
        setParameter(paint, box, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Object Visible"), true);
        // Erase over its top half takes the paint off (p.538).
        const QString eraser = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 1.0, 0.5)));
        setParameter(paint, eraser, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Fast menu"), QStringLiteral("Erase"));
        shown = intraframePicture(paint, grey, blue, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 10, 5), QColor(128, 128, 128)));
        QVERIFY(near(at(shown, 10, 30), QColor(255, 0, 0)));
        // Invert (p.542).
        setParameter(paint, eraser, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Fast menu"), QStringLiteral("Invert"));
        shown = intraframePicture(paint, grey, blue, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 10, 5), QColor(0, 255, 255)));
        QVERIFY(near(at(shown, 50, 5), QColor(127, 127, 127)));
        // Darken by Amt 50 (p.541).
        setParameter(paint, eraser, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Fast menu"), QStringLiteral("Darken"));
        setParameter(paint, eraser, QStringLiteral("Paint Effect Mode Parameters"), QStringLiteral("Amt"), 50);
        QVERIFY(near(at(intraframePicture(paint, grey, blue, 0.0, EffectSpan(10)), 50, 5), QColor(78, 78, 78)));
    }

    void cloneAndMagicMask() {
        // A picture red on top, green below; an oval drawn over the red half and moved down in Clone mode carries the
        // red with it (p.538).
        Picture halves = Picture::solid(64, 36, QColor(0, 255, 0));
        const Picture red = Picture::solid(64, 18, QColor(255, 0, 0));
        for (int y = 0; y < 18; ++y) {
            for (int x = 0; x < 64; ++x) std::copy(red.pixel(x, y), red.pixel(x, y) + 4, halves.pixel(x, y));
        }
        const Picture none(64, 36);
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        paint.parameters.insert(QStringLiteral("Paint Effect Mode Parameters"), QJsonObject{{QStringLiteral("Fast menu"), QStringLiteral("Clone")}});
        const QString id = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.25, 0.1, 0.25, 0.2)));
        // 18 rows down: Y is in 1920ths of the width, 18 of 64 is 540.
        setParameter(paint, id, model::kIntraframeObjectCategory, QStringLiteral("Y"), 540);
        Picture shown = intraframePicture(paint, halves, none, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 20, 26), QColor(255, 0, 0)));
        QVERIFY(near(at(shown, 50, 26), QColor(0, 255, 0)));
        // Magic Mask on green with a whole-frame Solid blue rectangle: only the green turns blue (p.497).
        Effect masked{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        masked.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#0000ff")}});
        masked.parameters.insert(QStringLiteral("Magic Mask"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#00ff00")}, {QStringLiteral("Gain"), 5}});
        model::addIntraframeObject(masked, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 1.0, 1.0)));
        shown = intraframePicture(masked, halves, none, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 20, 30), QColor(0, 0, 255)));
        QVERIFY(near(at(shown, 20, 5), QColor(255, 0, 0)));
    }

    void featherAndBrush() {
        const Picture black = Picture::solid(64, 36, Qt::black);
        const Picture none(64, 36);
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        paint.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#ffffff")}});
        const QString box = model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 0.5, 1.0)));
        // Without feathering the edge is hard; Hor 63 (of 1920: 2 pixels of 64) softens it on both sides.
        QCOMPARE(at(intraframePicture(paint, black, none, 0.0, EffectSpan(10)), 31, 18).red(), 255);
        setParameter(paint, box, QStringLiteral("Feathering"), QStringLiteral("Hor"), 63);
        setParameter(paint, box, QStringLiteral("Feathering"), QStringLiteral("Vert"), 0);
        const int soft = at(intraframePicture(paint, black, none, 0.0, EffectSpan(10)), 31, 18).red();
        QVERIFY2(soft > 60 && soft < 250, qPrintable(QString::number(soft)));
        // Bias 100 feathers inside the edge, 0 outside it (p.495).
        setParameter(paint, box, QStringLiteral("Feathering"), QStringLiteral("Bias"), 100);
        const int inside = at(intraframePicture(paint, black, none, 0.0, EffectSpan(10)), 31, 18).red();
        setParameter(paint, box, QStringLiteral("Feathering"), QStringLiteral("Bias"), 0);
        const int outside = at(intraframePicture(paint, black, none, 0.0, EffectSpan(10)), 31, 18).red();
        QVERIFY2(inside < soft && outside > soft, qPrintable(QStringLiteral("%1 %2 %3").arg(inside).arg(soft).arg(outside)));
        // A Brush stroke across the middle, Size 300 (10 pixels of 64); Path 50 shows only its first half (p.493).
        Effect brush{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        brush.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#ffffff")}});
        brush.parameters.insert(QStringLiteral("Brush"), QJsonObject{{QStringLiteral("Size"), 300}});
        const QString stroke = model::addIntraframeObject(brush, IntraframeKind::Brush, model::polylineShape({QPointF(0.1, 0.5), QPointF(0.9, 0.5)}));
        Picture shown = intraframePicture(brush, black, none, 0.0, EffectSpan(10));
        QCOMPARE(at(shown, 10, 18).red(), 255);
        QCOMPARE(at(shown, 54, 18).red(), 255);
        QCOMPARE(at(shown, 32, 5).red(), 0);
        setParameter(brush, stroke, QStringLiteral("Brush"), QStringLiteral("Path"), 50);
        shown = intraframePicture(brush, black, none, 0.0, EffectSpan(10));
        QCOMPARE(at(shown, 10, 18).red(), 255);
        QCOMPARE(at(shown, 54, 18).red(), 0);
    }

    void pictFiles() {
        // Export Matte PICT's file (p.418): a 32-bit PICT with alpha; our reader takes it back unchanged.
        QImage image(80, 7, QImage::Format_ARGB32); // 320 row bytes: the packed rows' counts are words
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, x < 40 ? qRgba(x * 3, y * 30, 200, 255) : qRgba(0, 0, 0, 0));
        }
        const QByteArray data = pictData(image);
        QCOMPARE(data.size() > 512, true);
        QCOMPARE(static_cast<unsigned char>(data.at(512 + 10)), static_cast<unsigned char>(0x00)); // VersionOp 0x0011
        QCOMPARE(static_cast<unsigned char>(data.at(512 + 11)), static_cast<unsigned char>(0x11));
        QCOMPARE(readPictData(data), image);
        // Without the 512-byte file header too.
        QCOMPARE(readPictData(data.mid(512)), image);
        // A small one (byte counts) without alpha reads back opaque.
        QImage small(6, 3, QImage::Format_ARGB32);
        small.fill(qRgba(10, 20, 30, 40));
        const QImage opaque = readPictData(pictData(small, false));
        QCOMPARE(opaque.size(), small.size());
        QCOMPARE(opaque.pixel(2, 1), qRgba(10, 20, 30, 255));
        // Files.
        const QString path = QDir::temp().filePath(QStringLiteral("tst_intraframe_matte.pct"));
        QVERIFY(writePict(image, path));
        QCOMPARE(readPict(path), image);
        QFile::remove(path);
        // Not PICT, or cut short: nothing.
        QVERIFY(readPictData(QByteArray(600, 'x')).isNull());
        QVERIFY(readPictData(data.left(data.size() / 2)).isNull());
    }

    void exportedMatte() {
        // An AniMatte matte as Export Matte PICT writes it (p.418): white and opaque within Key In objects, black and
        // transparent outside; Invert Key turns it over.
        Effect matte{QStringLiteral("Key"), QStringLiteral("AniMatte"), {}};
        model::addIntraframeObject(matte, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.25, 0.25, 0.5, 0.5)));
        const Picture layer = Picture::solid(64, 36, QColor(30, 60, 90));
        QImage image = aniMatteImage(matte, layer, 0.0, EffectSpan(10));
        QCOMPARE(image.size(), QSize(64, 36));
        QCOMPARE(image.pixel(32, 18), qRgba(255, 255, 255, 255));
        QCOMPARE(image.pixel(2, 2), qRgba(0, 0, 0, 0));
        matte.parameters.insert(QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Invert Key"), true}});
        image = aniMatteImage(matte, layer, 0.0, EffectSpan(10));
        QCOMPARE(image.pixel(32, 18), qRgba(0, 0, 0, 0));
        QCOMPARE(image.pixel(2, 2), qRgba(255, 255, 255, 255));
    }

    void previsualizationMarkers() {
        // The Previsualization Marker (p.412-413): a marker where it was clicked, in its colour; each keyframe of its
        // position is an optical event.
        Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        paint.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#ff0000")}});
        const QString marker = model::addPrevisualizationMarker(paint, QPointF(0.5, 0.5));
        QCOMPARE(model::intraframeObjects(paint).at(0).kind, IntraframeKind::Marker);
        const Picture black = Picture::solid(64, 36, Qt::black);
        const Picture shown = intraframePicture(paint, black, Picture(64, 36), 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 32, 18), QColor(255, 0, 0)));
        QVERIFY(near(at(shown, 5, 5), QColor(0, 0, 0)));
        QList<model::PrevisualizationEvent> events = model::previsualizationEvents(paint, EffectSpan(10), 16.0 / 9.0);
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.at(0).marker, marker);
        QCOMPARE(events.at(0).keyframe, 1);
        QCOMPARE(events.at(0).position, QPointF(0.5, 0.5));
        QCOMPARE(events.at(0).color, QStringLiteral("#ff0000"));
        // Moved by keyframes of its shape: one event at each.
        QList<model::IntraframeObject> objects = model::intraframeObjects(paint);
        objects[0].shapes = {model::IntraframeShape{0.0, model::polylineShape({QPointF(0.2, 0.2)})},
                             model::IntraframeShape{1.0, model::polylineShape({QPointF(0.8, 0.6)})}};
        model::setIntraframeObjects(paint, objects);
        events = model::previsualizationEvents(paint, EffectSpan(10), 16.0 / 9.0);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.at(1).keyframe, 2);
        QCOMPARE(events.at(1).at, 1.0);
        QCOMPARE(events.at(1).position, QPointF(0.8, 0.6));
        // Other objects are not markers.
        model::addIntraframeObject(paint, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.1, 0.1, 0.2, 0.2)));
        QCOMPARE(model::previsualizationEvents(paint, EffectSpan(10), 16.0 / 9.0).size(), 2);
    }

    void scratchRemoval() {
        // Scratch Removal's replacement material (p.419-428, p.546-547).
        using model::ScratchOffset;
        // The zero point and the slider; never outside the segment, nor beyond 5 frames or 10 fields.
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::FromStart, -1, false, 1, 2), 0);
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::FromStart, 0, false, 1, 2), 0);
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::Relative, 2, false, 3, 10), 5);
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::Relative, 9, false, 0, 20), 5);
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::FromEnd, -1, false, 0, 10), 8);
        // "positioned six fields from the end ... Field slider to 10 and the Offset to Relative, you reference the last
        // field" (p.428).
        QCOMPARE(model::scratchSourceIndex(ScratchOffset::Relative, 10, true, 14, 20), 19);
        QCOMPARE(model::scratchOffsetFrom(QStringLiteral("From End")), ScratchOffset::FromEnd);
        QCOMPARE(model::scratchOffsetFrom(QString()), ScratchOffset::FromStart);
        // The Scratch Removal button's frames (p.422, p.426).
        QCOMPARE(model::scratchRemovalFrames(20, std::nullopt, std::nullopt), (std::pair<std::int64_t, std::int64_t>(19, 20)));
        QCOMPARE(model::scratchRemovalFrames(12, 10, 14), (std::pair<std::int64_t, std::int64_t>(9, 14)));
        QCOMPARE(model::scratchRemovalFrames(9, 10, 14), (std::pair<std::int64_t, std::int64_t>(9, 14)));
        QCOMPARE(model::scratchRemovalFrames(30, 10, 14), (std::pair<std::int64_t, std::int64_t>(29, 30)));

        // Frames of the segment, each a colour of its own: frame n is red n * 20.
        auto frameColour = [](std::int64_t n) { return QColor(static_cast<int>(n) * 20, 0, 0); };
        IntraframeFrames frames;
        frames.offset = 3;
        frames.length = 6;
        frames.pictureAt = [&](std::int64_t n) { return Picture::solid(16, 8, frameColour(n)); };
        const Picture shown = Picture::solid(16, 8, frameColour(3));
        Effect scratch{QStringLiteral("Image"), QStringLiteral("Scratch Removal"), {}};
        const QString cat = QStringLiteral("Scratch Removal Effect Scratch Parameters");
        // The defaults, -1 From Start: the first frame.
        QVERIFY(near(at(scratchRemovalSource(scratch, cat, shown, &frames, 0.5, EffectSpan(6)), 4, 4), frameColour(0)));
        // Relative +2: two frames on.
        scratch.parameters.insert(cat, QJsonObject{{QStringLiteral("Frame/Field"), 2}, {QStringLiteral("Offset Fast Menu"), QStringLiteral("Relative")}});
        QVERIFY(near(at(scratchRemovalSource(scratch, cat, shown, &frames, 0.5, EffectSpan(6)), 4, 4), frameColour(5)));
        // Without other frames: this one.
        QVERIFY(near(at(scratchRemovalSource(scratch, cat, shown, nullptr, 0.5, EffectSpan(6)), 4, 4), frameColour(3)));

        // Field processing on two-field media: Relative +1 takes field 2 of this frame for field 1 and field 1 of the next
        // for field 2. Frames whose fields differ: even lines red n * 20, odd lines green n * 20.
        frames.interlaced = true;
        frames.pictureAt = [&](std::int64_t n) {
            Picture p = Picture::solid(16, 8, frameColour(n));
            const Picture odd = Picture::solid(16, 1, QColor(0, static_cast<int>(n) * 20, 0));
            for (int y = 1; y < 8; y += 2) std::copy(odd.pixel(0, 0), odd.pixel(0, 0) + 16 * 4, p.pixel(0, y));
            return p;
        };
        const Picture interlacedShown = frames.pictureAt(3);
        scratch.parameters.insert(cat, QJsonObject{{QStringLiteral("Frame/Field"), 1}, {QStringLiteral("Offset Fast Menu"), QStringLiteral("Relative")},
                                                   {QStringLiteral("Frame Processing"), false}});
        Picture fields = scratchRemovalSource(scratch, cat, interlacedShown, &frames, 0.5, EffectSpan(6));
        QVERIFY(near(at(fields, 4, 2), QColor(0, 60, 0)));  // field 1 from field 2 of frame 3
        QVERIFY(near(at(fields, 4, 3), QColor(80, 0, 0)));  // field 2 from field 1 of frame 4
        // Frame processing copies field to field.
        scratch.parameters.insert(cat, QJsonObject{{QStringLiteral("Frame/Field"), 1}, {QStringLiteral("Offset Fast Menu"), QStringLiteral("Relative")}});
        fields = scratchRemovalSource(scratch, cat, interlacedShown, &frames, 0.5, EffectSpan(6));
        QVERIFY(near(at(fields, 4, 2), QColor(80, 0, 0)));
        QVERIFY(near(at(fields, 4, 3), QColor(0, 80, 0)));

        // In the picture: an object over the flaw shows the material, from the first frame by default.
        frames.interlaced = false;
        frames.pictureAt = [&](std::int64_t n) { return Picture::solid(16, 8, frameColour(n)); };
        Effect fix{QStringLiteral("Image"), QStringLiteral("Scratch Removal"), {}};
        model::addIntraframeObject(fix, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.25, 0.25, 0.5, 0.5)));
        const Picture fixed = intraframePicture(fix, shown, Picture(16, 8), 0.5, EffectSpan(6), &frames);
        QVERIFY(near(at(fixed, 8, 4), frameColour(0)));
        QVERIFY(near(at(fixed, 1, 1), frameColour(3)));

        // The Scratch Removal button on a sequence: a two-frame segment ending at the position, nested over an effect
        // already there (p.422).
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.sourceTrack = QStringLiteral("V1");
        a.length = 50;
        seq.splice(QStringLiteral("V1"), 0, {a});
        QVERIFY(model::applyScratchRemoval(seq, QStringLiteral("V1"), 20, std::nullopt, std::nullopt));
        const model::Track* v1 = seq.track(QStringLiteral("V1"));
        QCOMPARE(v1->segments.size(), 3);
        QCOMPARE(v1->segments.at(1).length, 2);
        QCOMPARE(v1->segments.at(1).effect->name, QStringLiteral("Scratch Removal"));
        QVERIFY(!v1->segments.at(1).hasNest());
        QVERIFY(model::applyScratchRemoval(seq, QStringLiteral("V1"), 20, std::nullopt, std::nullopt));
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.size(), 3);
        QVERIFY(seq.track(QStringLiteral("V1"))->segments.at(1).hasNest());
        QVERIFY(!model::applyScratchRemoval(seq, QStringLiteral("A1"), 20, std::nullopt, std::nullopt));
    }

    void brushHeads() {
        // The Brush Shape buttons' heads and a custom head from the Brush Preview window (p.397-398), and brush
        // templates (p.399).
        QCOMPARE(model::brushShapes(), (QStringList{QStringLiteral("Circle"), QStringLiteral("Oval"), QStringLiteral("Ellipse"), QStringLiteral("Square")}));
        QCOMPARE(model::brushHeadPoints({}), model::brushShapePoints(QStringLiteral("Circle")));
        QCOMPARE(model::brushShapePoints(QStringLiteral("Square")).size(), 4);
        QCOMPARE(model::brushShapePoints(QStringLiteral("Square")).at(0).join, model::JoinType::Straight);
        QCOMPARE(model::brushShapePoints(QStringLiteral("Oval")).at(0).join, model::JoinType::Smooth);
        QJsonObject brushCategory{{QStringLiteral("Size"), 600}};
        // A flat head: a bar across, a tenth as high as it is wide.
        const QList<model::ControlPoint> flat = model::rectangleShape(QRectF(-0.5, -0.05, 1.0, 0.1));
        model::setBrushHead(brushCategory, flat);
        QCOMPARE(model::brushHeadPoints(brushCategory), flat);
        QJsonObject shaped = brushCategory;
        model::setBrushShape(shaped, QStringLiteral("Oval"));
        QVERIFY(!shaped.contains(QStringLiteral("Head")));
        QCOMPARE(model::brushHeadPoints(shaped), model::brushShapePoints(QStringLiteral("Oval")));
        QJsonObject cleared = brushCategory;
        model::setBrushHead(cleared, {});
        QVERIFY(!cleared.contains(QStringLiteral("Head")));

        // One stamp in the middle, Size 600 (20 pixels of 64): the circle reaches below, the flat head only across.
        const Picture black = Picture::solid(64, 36, Qt::black);
        const Picture none(64, 36);
        auto stamped = [&](const QJsonObject& b) {
            Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
            paint.parameters.insert(QStringLiteral("Paint Effect Color Parameters"), QJsonObject{{QStringLiteral("Color controls"), QStringLiteral("#ffffff")}});
            paint.parameters.insert(model::kBrushCategory, b);
            model::addIntraframeObject(paint, IntraframeKind::Brush, model::polylineShape({QPointF(0.5, 0.5), QPointF(0.5001, 0.5)}));
            return intraframePicture(paint, black, none, 0.0, EffectSpan(10));
        };
        const Picture round = stamped(QJsonObject{{QStringLiteral("Size"), 600}});
        QCOMPARE(at(round, 32, 24).red(), 255);
        const Picture bar = stamped(brushCategory);
        QCOMPARE(at(bar, 32, 24).red(), 0);
        QCOMPARE(at(bar, 40, 18).red(), 255);
        // Spin 90 stands the bar up.
        QJsonObject turned = brushCategory;
        turned.insert(QStringLiteral("Spin"), 90);
        const Picture upright = stamped(turned);
        QCOMPARE(at(upright, 32, 24).red(), 255);
        QCOMPARE(at(upright, 40, 18).red(), 0);

        // A brush template: an Effect item named Brush by default.
        const model::BinItem item = model::brushTemplateItem(brushCategory, QString(), QDateTime::currentDateTimeUtc());
        QCOMPARE(item.kind, model::BinItemKind::Effect);
        QCOMPARE(item.name, QStringLiteral("Brush"));
        QCOMPARE(model::brushTemplateOf(item), std::optional<QJsonObject>(brushCategory));
        QCOMPARE(model::brushTemplateOf(model::BinItem{}), std::nullopt);
    }

    void blurWithinObjects() {
        // The Blur effect blurs only inside its objects (p.439-441); without objects nothing changes.
        Picture halves = Picture::solid(64, 36, QColor(0, 0, 255));
        const Picture left = Picture::solid(32, 36, QColor(255, 0, 0));
        for (int y = 0; y < 36; ++y) {
            for (int x = 0; x < 32; ++x) std::copy(left.pixel(x, y), left.pixel(x, y) + 4, halves.pixel(x, y));
        }
        const Picture none(64, 36);
        Effect blur{QStringLiteral("Image"), QStringLiteral("Blur Effect"),
                    QJsonObject{{QStringLiteral("Blur Effect Blur Parameters"), QJsonObject{{QStringLiteral("Hor"), 300}, {QStringLiteral("Vert"), 0}}}}};
        QCOMPARE(at(intraframePicture(blur, halves, none, 0.0, EffectSpan(10)), 31, 5), QColor(255, 0, 0));
        // An object over the bottom half: the edge softens there only.
        model::addIntraframeObject(blur, IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.5, 1.0, 0.5)));
        const Picture shown = intraframePicture(blur, halves, none, 0.0, EffectSpan(10));
        QCOMPARE(at(shown, 31, 5), QColor(255, 0, 0));
        QVERIFY(at(shown, 31, 30).blue() > 35);
    }

    void aniMatte() {
        // Key In shows the higher track inside the matte and the lower one outside; Key Out the reverse (p.548).
        const Picture top = Picture::solid(64, 36, QColor(255, 0, 0));
        const Picture bottom = Picture::solid(64, 36, QColor(0, 0, 255));
        Effect matte{QStringLiteral("Key"), QStringLiteral("AniMatte"), {}};
        const QString id = model::addIntraframeObject(matte, IntraframeKind::Oval, model::ovalShape(QRectF(0.25, 0.25, 0.5, 0.5)));
        Picture shown = intraframePicture(matte, top, bottom, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 32, 18), QColor(255, 0, 0)));
        QVERIFY(near(at(shown, 2, 2), QColor(0, 0, 255)));
        setParameter(matte, id, QStringLiteral("AniMatte Effect Mode Parameters"), QStringLiteral("Fast menu"), QStringLiteral("Key Out"));
        shown = intraframePicture(matte, top, bottom, 0.0, EffectSpan(10));
        QVERIFY(near(at(shown, 32, 18), QColor(0, 0, 255)));
        QVERIFY(near(at(shown, 2, 2), QColor(255, 0, 0)));
        // As a transition (span of a transition).
        QVERIFY2(near(at(intraframePicture(matte, top, bottom, 0.5, EffectSpan(4, true)), 32, 18), QColor(0, 0, 255)),
                 qPrintable(at(intraframePicture(matte, top, bottom, 0.5, EffectSpan(4, true)), 32, 18).name()));
        // Swap Sources and Invert Key (p.394, p.495).
        matte.parameters.insert(QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Invert Key"), true}});
        QVERIFY(near(at(intraframePicture(matte, top, bottom, 0.0, EffectSpan(10)), 32, 18), QColor(255, 0, 0)));
        matte.parameters.insert(QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Swap Sources"), true}});
        QVERIFY(near(at(intraframePicture(matte, top, bottom, 0.0, EffectSpan(10)), 32, 18), QColor(255, 0, 0)));
    }
};

QTEST_MAIN(TestIntraframe)
#include "tst_intraframe.moc"
