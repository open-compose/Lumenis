// The Intraframe drawing tools in the Effect Editor and Effect Preview (ROADMAP 6.9b1; FX Guide p.395-404, p.413-417;
// docs/spec/intraframe.md): drawing each kind of object, the tool returning to Selection, selecting, lassoing, moving,
// scaling, deleting, layers, and each object's own parameters in the editor.
#include "lumenis/media/pict.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/correction_shapes.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/tracking.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/brush_preview.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/intraframe_tool.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tracking_window.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QSlider>
#include <QTest>
#include <QToolButton>

#include <cmath>
#include <memory>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using Tool = lumenis::ui::IntraframeTool::Tool;

namespace {

BinItem sequence(const model::Effect& effect) {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    model::Segment a;
    a.clipId = QStringLiteral("a");
    a.name = QStringLiteral("A");
    a.sourceTrack = QStringLiteral("V1");
    a.length = 50;
    a.effect = effect;
    seq.splice(QStringLiteral("V1"), 0, {a});
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

} // namespace

class TestIntraframeUi : public QObject {
    Q_OBJECT

private slots:
    void drawSelectMove() {
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}}));
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        auto objects = [&] {
            const model::Segment s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
            return model::intraframeObjects(*s.effect);
        };
        IntraframeTool* tool = editor.intraframeTool();
        QVERIFY(tool && editor.intraframeEffect());
        QCOMPARE(composer.overlayInput(), static_cast<Composer::OverlayInput*>(tool));
        auto* rectangle = editor.findChild<QToolButton*>(QStringLiteral("intraframeRectangle"));
        QVERIFY(rectangle && rectangle->isVisible());
        QVERIFY(editor.findChild<QToolButton*>(QStringLiteral("intraframeBrush"))->isVisible());

        // The Rectangle tool: drag a diagonal (p.400); the tool returns to Selection with the object selected (p.396).
        rectangle->click();
        QCOMPARE(tool->tool(), Tool::Rectangle);
        QVERIFY(tool->overlayPress(QPointF(0.1, 0.1), Qt::NoModifier, false));
        tool->overlayMove(QPointF(0.3, 0.4), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.3, 0.4), Qt::NoModifier);
        settle();
        QCOMPARE(objects().size(), 1);
        QCOMPARE(objects().at(0).kind, model::IntraframeKind::Rectangle);
        QCOMPARE(objects().at(0).shapes.at(0).points.at(2).p, QPointF(0.3, 0.4));
        QCOMPARE(tool->tool(), Tool::Selection);
        QCOMPARE(tool->selection(), QStringList{objects().at(0).id});
        QCOMPARE(editor.intraframeSelection(), tool->selection());
        // One undo step.
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(objects().size(), 0);
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));
        QCOMPARE(objects().size(), 1);
        const QString box = objects().at(0).id;
        tool->select({box});
        settle();

        // The selected object's own parameters show and change in the editor (p.404): its mode, not the effect's.
        auto* mode = editor.findChild<QComboBox*>(QStringLiteral("param:Paint Effect Mode Parameters/Fast menu"));
        QVERIFY(mode);
        mode->setCurrentText(QStringLiteral("Invert"));
        settle();
        QCOMPARE(objects().at(0).parameters.value(QStringLiteral("Paint Effect Mode Parameters")).toObject().value(QStringLiteral("Fast menu")).toString(),
                 QStringLiteral("Invert"));
        const model::Segment painted = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        QVERIFY(!painted.effect->parameters.value(QStringLiteral("Paint Effect Mode Parameters")).toObject().contains(QStringLiteral("Fast menu")));

        // Moving: drag inside the outline (p.404). X is in 1920ths of the width.
        QVERIFY(tool->overlayPress(QPointF(0.2, 0.2), Qt::NoModifier, false));
        tool->overlayMove(QPointF(0.3, 0.2), Qt::NoModifier);
        QVERIFY(editor.pictureOverride()); // the Record monitor shows the drag
        tool->overlayRelease(QPointF(0.3, 0.2), Qt::NoModifier);
        settle();
        QVERIFY(!editor.pictureOverride());
        const double x = objects().at(0).parameters.value(model::kIntraframeObjectCategory).toObject().value(QStringLiteral("X")).toDouble();
        QVERIFY2(std::abs(x - 192.0) < 1e-6, qPrintable(QString::number(x)));

        // Scaling from the bottom right handle, the top left corner anchored (p.415).
        Composer::EffectPreview shown = composer.effectPreview();
        QCOMPARE(shown.handles.size(), 4);
        const QPointF corner = shown.handles.at(2).at;
        const QPointF anchor = shown.handles.at(0).at;
        QVERIFY(tool->overlayPress(corner, Qt::NoModifier, false));
        tool->overlayMove(anchor + (corner - anchor) * 2.0, Qt::NoModifier);
        tool->overlayRelease(anchor + (corner - anchor) * 2.0, Qt::NoModifier);
        settle();
        const QJsonObject place = objects().at(0).parameters.value(model::kIntraframeObjectCategory).toObject();
        QVERIFY(std::abs(place.value(QStringLiteral("Scale")).toDouble() - 200.0) < 1e-6);
        shown = composer.effectPreview();
        QVERIFY2(std::hypot(shown.handles.at(0).at.x() - anchor.x(), shown.handles.at(0).at.y() - anchor.y()) < 1e-6,
                 qPrintable(QStringLiteral("%1,%2").arg(shown.handles.at(0).at.x()).arg(shown.handles.at(0).at.y())));

        // An oval and a Brush stroke; the Brush stays the tool (p.396).
        editor.findChild<QToolButton*>(QStringLiteral("intraframeOval"))->click();
        tool->overlayPress(QPointF(0.6, 0.6), Qt::NoModifier, false);
        tool->overlayMove(QPointF(0.8, 0.8), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.8, 0.8), Qt::NoModifier);
        editor.findChild<QToolButton*>(QStringLiteral("intraframeBrush"))->click();
        tool->overlayPress(QPointF(0.1, 0.9), Qt::NoModifier, false);
        for (int i = 1; i <= 10; ++i) tool->overlayMove(QPointF(0.1 + 0.05 * i, 0.9), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.6, 0.9), Qt::NoModifier);
        settle();
        QCOMPARE(objects().size(), 3);
        QCOMPARE(objects().at(2).kind, model::IntraframeKind::Brush);
        QVERIFY(!objects().at(2).closed);
        QCOMPARE(tool->tool(), Tool::Brush);
        QVERIFY(!composer.effectPreview().lines.isEmpty());

        // A polygon: clicks, then a click on the first point closes it (p.400).
        editor.findChild<QToolButton*>(QStringLiteral("intraframePolygon"))->click();
        for (const QPointF& p : {QPointF(0.5, 0.1), QPointF(0.7, 0.1), QPointF(0.6, 0.3)}) {
            tool->overlayPress(p, Qt::NoModifier, false);
            tool->overlayRelease(p, Qt::NoModifier);
        }
        QVERIFY(tool->busy());
        tool->overlayPress(QPointF(0.5, 0.1), Qt::NoModifier, false);
        tool->overlayRelease(QPointF(0.5, 0.1), Qt::NoModifier);
        settle();
        QCOMPARE(objects().size(), 4);
        QCOMPARE(objects().at(3).kind, model::IntraframeKind::Polygon);
        QCOMPARE(objects().at(3).shapes.at(0).points.size(), 3);
        // The Curve tool: a freehand closed curve with smooth joins (p.403).
        editor.findChild<QToolButton*>(QStringLiteral("intraframeCurve"))->click();
        tool->overlayPress(QPointF(0.8, 0.1), Qt::NoModifier, false);
        for (int i = 1; i <= 12; ++i) {
            const double a = i * 3.14159 * 2.0 / 12.0;
            tool->overlayMove(QPointF(0.85 + 0.05 * std::sin(a), 0.15 - 0.05 * std::cos(a)), Qt::NoModifier);
        }
        tool->overlayRelease(QPointF(0.8, 0.1), Qt::NoModifier);
        settle();
        QCOMPARE(objects().size(), 5);
        QCOMPARE(objects().at(4).kind, model::IntraframeKind::Curve);
        QCOMPARE(objects().at(4).shapes.at(0).points.at(1).join, model::JoinType::Smooth);

        // Clicking selects the topmost object; Shift+click adds; a lasso selects what it surrounds (p.403).
        QVERIFY(tool->overlayPress(QPointF(0.7, 0.7), Qt::NoModifier, false));
        tool->overlayRelease(QPointF(0.7, 0.7), Qt::NoModifier);
        QCOMPARE(tool->selection(), QStringList{objects().at(1).id});
        tool->overlayPress(QPointF(0.6, 0.15), Qt::ShiftModifier, false);
        tool->overlayRelease(QPointF(0.6, 0.15), Qt::ShiftModifier);
        QCOMPARE(tool->selection().size(), 2);
        tool->overlayPress(QPointF(0.45, 0.02), Qt::NoModifier, false);
        tool->overlayMove(QPointF(0.95, 0.35), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.95, 0.35), Qt::NoModifier);
        QCOMPARE(tool->selection(), (QStringList{objects().at(3).id, objects().at(4).id}));

        // Layers (p.417): Send Backward with Alt to the back.
        tool->select({objects().at(4).id});
        const QString curve = objects().at(4).id;
        QVERIFY(tool->sendBackward(true));
        QCOMPARE(objects().at(0).id, curve);
        QVERIFY(tool->bringForward(false));
        QCOMPARE(objects().at(1).id, curve);
        // Go to Next Event steps through the objects (p.403).
        QVERIFY(editor.goToEvent(true));
        QCOMPARE(tool->selection(), QStringList{objects().at(2).id});

        // Delete (p.403).
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QVERIFY(tool->overlayWantsKey(&del));
        QVERIFY(tool->overlayKey(&del));
        settle();
        QCOMPARE(objects().size(), 4);
        QVERIFY(tool->selection().isEmpty());
    }

    void reshapeRotateGroup() {
        // Reshape (p.406-412), Z-Rotation (p.414-415) and the Object menu (p.417-418).
        model::Effect paint{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}};
        model::addIntraframeObject(paint, model::IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.2, 0.2, 0.2, 0.2)));
        model::addIntraframeObject(paint, model::IntraframeKind::Oval, model::ovalShape(QRectF(0.6, 0.6, 0.2, 0.2)));
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(paint));
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        auto objects = [&] {
            const model::Segment s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
            return model::intraframeObjects(*s.effect);
        };
        IntraframeTool* tool = editor.intraframeTool();
        const QString box = objects().at(0).id;
        // Double-clicking an object opens it in Reshape (p.406).
        QVERIFY(tool->overlayPress(QPointF(0.3, 0.3), Qt::NoModifier, true));
        tool->overlayRelease(QPointF(0.3, 0.3), Qt::NoModifier);
        QCOMPARE(tool->tool(), Tool::Reshape);
        QCOMPARE(tool->selection(), QStringList{box});
        QVERIFY(composer.effectPreview().handles.size() >= 4);
        // Drag a corner (p.411).
        QVERIFY(tool->overlayPress(QPointF(0.4, 0.4), Qt::NoModifier, false));
        tool->overlayMove(QPointF(0.45, 0.5), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.45, 0.5), Qt::NoModifier);
        settle();
        QVERIFY(std::hypot(objects().at(0).shapes.at(0).points.at(2).p.x() - 0.45, objects().at(0).shapes.at(0).points.at(2).p.y() - 0.5) < 1e-9);
        QCOMPARE(tool->selectedPoints(), QList<int>{2});
        // Nudge right one pixel (p.412) and step to the next point (p.411).
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QVERIFY(tool->overlayWantsKey(&right) && tool->overlayKey(&right));
        settle();
        QVERIFY(std::abs(objects().at(0).shapes.at(0).points.at(2).p.x() - (0.45 + 1.0 / 1920.0)) < 1e-9);
        QVERIFY(editor.goToEvent(true));
        QCOMPARE(tool->selectedPoints(), QList<int>{3});
        // Alt+click a point: a smooth join with direction handles shown (p.409).
        QVERIFY(tool->overlayPress(QPointF(0.2, 0.4), Qt::AltModifier, false));
        tool->overlayRelease(QPointF(0.2, 0.4), Qt::AltModifier);
        settle();
        QCOMPARE(objects().at(0).shapes.at(0).points.at(3).join, model::JoinType::Smooth);
        bool shownHandle = false;
        for (const auto& h : composer.effectPreview().handles) shownHandle = shownHandle || h.id == QLatin1String("intraframe:out:3");
        QVERIFY(shownHandle);
        // Click on an edge adds a point (p.410); Delete removes it (p.411).
        QVERIFY(tool->overlayPress(QPointF(0.3, 0.2), Qt::NoModifier, false));
        tool->overlayRelease(QPointF(0.3, 0.2), Qt::NoModifier);
        settle();
        QCOMPARE(objects().at(0).shapes.at(0).points.size(), 5);
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QVERIFY(tool->overlayKey(&del));
        settle();
        QCOMPARE(objects().at(0).shapes.at(0).points.size(), 4);
        QCOMPARE(objects().size(), 2);

        // Z-Rotation: drag the handle a quarter turn about the centre (p.415).
        editor.findChild<QToolButton*>(QStringLiteral("intraframeZRotation"))->click();
        QCOMPARE(tool->tool(), Tool::ZRotation);
        Composer::EffectPreview shown = composer.effectPreview();
        QCOMPARE(shown.handles.size(), 1);
        const QPointF centre = *shown.handles.at(0).from;
        const QPointF arm = shown.handles.at(0).at;
        const double a = editor.previewAspect();
        const QPointF quarter = centre + QPointF(-(arm.y() - centre.y()) / a, (arm.x() - centre.x()) * a);
        QVERIFY(tool->overlayPress(arm, Qt::NoModifier, false));
        tool->overlayMove(quarter, Qt::NoModifier);
        tool->overlayRelease(quarter, Qt::NoModifier);
        settle();
        const double rotation = objects().at(0).parameters.value(model::kIntraframeObjectCategory).toObject().value(QStringLiteral("Rotation")).toDouble();
        QVERIFY2(std::abs(rotation - 90.0) < 1e-6, qPrintable(QString::number(rotation)));

        // The Object menu: Group selects the group as one, Lock keeps it from moving, UnGroup (p.417-418).
        editor.findChild<QToolButton*>(QStringLiteral("intraframeSelection"))->click();
        QVERIFY(editor.findChild<QToolButton*>(QStringLiteral("intraframeObjectMenu"))->isVisible());
        tool->select({objects().at(0).id, objects().at(1).id});
        editor.findChild<QAction*>(QStringLiteral("intraframeObject:group"))->trigger();
        settle();
        QVERIFY(!objects().at(0).group.isEmpty() && objects().at(0).group == objects().at(1).group);
        tool->select({});
        QVERIFY(tool->overlayPress(QPointF(0.7, 0.7), Qt::NoModifier, false));
        tool->overlayRelease(QPointF(0.7, 0.7), Qt::NoModifier);
        QCOMPARE(tool->selection().size(), 2);
        editor.findChild<QAction*>(QStringLiteral("intraframeObject:lock"))->trigger();
        settle();
        QVERIFY(objects().at(0).locked && objects().at(1).locked);
        const QJsonObject placeBefore = objects().at(1).parameters.value(model::kIntraframeObjectCategory).toObject();
        QVERIFY(tool->overlayPress(QPointF(0.7, 0.7), Qt::NoModifier, false));
        tool->overlayMove(QPointF(0.8, 0.8), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.8, 0.8), Qt::NoModifier);
        settle();
        QCOMPARE(objects().at(1).parameters.value(model::kIntraframeObjectCategory).toObject(), placeBefore);
        editor.findChild<QAction*>(QStringLiteral("intraframeObject:unlock"))->trigger();
        editor.findChild<QAction*>(QStringLiteral("intraframeObject:ungroup"))->trigger();
        settle();
        QVERIFY(!objects().at(0).locked && objects().at(0).group.isEmpty());
        // Bring to Front from the menu.
        tool->select({box});
        editor.findChild<QAction*>(QStringLiteral("intraframeObject:bringToFront"))->trigger();
        settle();
        QCOMPARE(objects().constLast().id, box);
    }

    void brushAndEyedropper() {
        // The Brush category's Shape buttons, Brush Preview window, brush templates (p.397-399) and the eyedropper (p.89,
        // p.397).
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(10, 200, 30));
            return image;
        });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}}));
        composer.resize(800, 500);
        composer.show();
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        auto effect = [&] {
            return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0).effect;
        };
        auto brush = [&] { return effect().parameters.value(model::kBrushCategory).toObject(); };

        // A Brush Shape button: the shape (and a custom head made from another goes).
        auto* square = editor.findChild<QToolButton*>(QStringLiteral("brushShape:Square"));
        QVERIFY(square);
        square->click();
        settle();
        QCOMPARE(brush().value(QStringLiteral("Shape")).toString(), QStringLiteral("Square"));
        BrushPreview* preview = editor.brushPreview();
        QVERIFY(preview && preview->isVisible());
        QCOMPARE(preview->head(), model::brushShapePoints(QStringLiteral("Square")));

        // "click the brush head. Control points appear"; dragging one makes a custom head (p.398), one undo step.
        QVERIFY(!preview->editing());
        QTest::mouseClick(preview, Qt::LeftButton, {}, preview->rect().center());
        QVERIFY(preview->editing());
        const QPoint corner = preview->toWidget(model::brushShapePoints(QStringLiteral("Square")).at(0).p).toPoint();
        QTest::mousePress(preview, Qt::LeftButton, {}, corner);
        QCOMPARE(preview->selectedPoint(), 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(corner - QPoint(6, 6)), preview->mapToGlobal(QPointF(corner - QPoint(6, 6))), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(preview, &move);
        QTest::mouseRelease(preview, Qt::LeftButton, {}, corner - QPoint(6, 6));
        settle();
        const QList<model::ControlPoint> custom = model::brushHeadPoints(brush());
        QVERIFY(brush().contains(QStringLiteral("Head")));
        QVERIFY(custom.at(0).p.x() < -0.5 && custom.at(0).p.y() < -0.5);
        QCOMPARE(custom.at(1), model::brushShapePoints(QStringLiteral("Square")).at(1));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!brush().contains(QStringLiteral("Head")));
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));
        settle();
        QCOMPARE(preview->head(), custom);

        // The Brush Template icon makes a template named Brush (p.399); dropped on the window, its parameters return.
        QVERIFY(editor.findChild<QLabel*>(QStringLiteral("brushTemplate")));
        std::unique_ptr<QMimeData> mime(editor.brushTemplateMime());
        QVERIFY(mime && mime->hasFormat(brushTemplateMime()));
        const auto saved = preview->droppedBrush(mime.get());
        QVERIFY(saved);
        const BinItem item = model::brushTemplateItem(*saved, QString(), QDateTime::currentDateTimeUtc());
        QCOMPARE(item.name, QStringLiteral("Brush"));
        QCOMPARE(model::brushTemplateOf(item), saved);
        auto* circle = editor.findChild<QToolButton*>(QStringLiteral("brushShape:Circle"));
        circle->click();
        settle();
        QVERIFY(!brush().contains(QStringLiteral("Head")));
        QVERIFY(editor.applyBrushTemplate(*saved));
        settle();
        QCOMPARE(model::brushHeadPoints(brush()), custom);
        // A bin item dropped: resolved through the Composer.
        preview->setItemResolver([&](const QString&, const QString& id) { return id == item.id ? std::optional<BinItem>(item) : std::nullopt; });
        QMimeData items;
        items.setData(binItemsMime(), (QStringLiteral("bin\n") + item.id).toUtf8());
        QCOMPARE(preview->droppedBrush(&items), saved);

        // The eyedropper: Alt+click with the Brush tool over the Effect Preview (p.397), and a drag from a colour
        // parameter's Color Preview window to the monitor (p.89).
        editor.findChild<QToolButton*>(QStringLiteral("intraframeBrush"))->click();
        QVERIFY(editor.intraframeTool()->overlayPress(QPointF(0.5, 0.5), Qt::AltModifier, false));
        settle();
        const QString colorCategory = QStringLiteral("Paint Effect Color Parameters");
        QCOMPARE(effect().parameters.value(colorCategory).toObject().value(QStringLiteral("Color controls")).toString(), QStringLiteral("#0ac81e"));
        QVERIFY(model::intraframeObjects(effect()).isEmpty()); // nothing painted
        QWidget* monitor = composer.findChild<QWidget*>(QStringLiteral("recordMonitor"));
        QVERIFY(monitor && monitor->isVisible());
        composer.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(200, 20, 40));
            return image;
        });
        QVERIFY(editor.pickColorAt(colorCategory, QStringLiteral("Color controls"), monitor->mapToGlobal(monitor->rect().center())));
        settle();
        QCOMPARE(effect().parameters.value(colorCategory).toObject().value(QStringLiteral("Color controls")).toString(), QStringLiteral("#c81428"));
        QVERIFY(!editor.pickColorAt(colorCategory, QStringLiteral("Color controls"), editor.mapToGlobal(QPoint(5, 5))));
    }

    void markerAndMatte() {
        // The Previsualization Marker tool: only for the Paint Effect in 24-fps film projects (p.412); a click places a
        // marker, which Delete removes (p.413). Export Matte PICT writes an AniMatte matte (p.418).
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Paint Effect"), {}}));
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        auto effect = [&] {
            return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0).effect;
        };
        auto* marker = editor.findChild<QToolButton*>(QStringLiteral("intraframeMarker"));
        QVERIFY(marker);
        QVERIFY(!marker->isVisible());
        editor.setFilmProject(true);
        QVERIFY(marker->isVisible());
        marker->click();
        IntraframeTool* tool = editor.intraframeTool();
        QCOMPARE(tool->tool(), Tool::Marker);
        QVERIFY(tool->overlayPress(QPointF(0.4, 0.3), Qt::NoModifier, false));
        tool->overlayRelease(QPointF(0.4, 0.3), Qt::NoModifier);
        settle();
        QList<model::IntraframeObject> objects = model::intraframeObjects(effect());
        QCOMPARE(objects.size(), 1);
        QCOMPARE(objects.at(0).kind, model::IntraframeKind::Marker);
        QCOMPARE(objects.at(0).shapes.at(0).points.at(0).p, QPointF(0.4, 0.3));
        QCOMPARE(tool->tool(), Tool::Selection);
        QCOMPARE(tool->selection(), QStringList{objects.at(0).id});
        // Clicked again it is selected; Delete removes it.
        tool->select({});
        QVERIFY(tool->overlayPress(QPointF(0.405, 0.3), Qt::NoModifier, false));
        tool->overlayRelease(QPointF(0.405, 0.3), Qt::NoModifier);
        QCOMPARE(tool->selection(), QStringList{objects.at(0).id});
        QVERIFY(tool->removeSelected());
        settle();
        QVERIFY(model::intraframeObjects(effect()).isEmpty());
        // Not for AniMatte, even in a film project.
        timeline.setEffectMode(false);
        model::Effect animatte{QStringLiteral("Key"), QStringLiteral("AniMatte"), {}};
        model::addIntraframeObject(animatte, model::IntraframeKind::Rectangle, model::rectangleShape(QRectF(0.0, 0.0, 0.5, 1.0)));
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(animatte));
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        QVERIFY(!marker->isVisible());

        // Export Matte PICT: the left half white and opaque.
        const QImage matte = editor.aniMatteImage(QSize(32, 18));
        QCOMPARE(matte.size(), QSize(32, 18));
        QCOMPARE(matte.pixel(4, 9), qRgba(255, 255, 255, 255));
        QCOMPARE(matte.pixel(28, 9), qRgba(0, 0, 0, 0));
        const QString path = QDir::temp().filePath(QStringLiteral("tst_intraframe_ui_matte.pct"));
        QVERIFY(editor.exportMattePict(path, QSize(32, 18)));
        QCOMPARE(media::readPict(path), matte);
        QFile::remove(path);
    }

    void frameProcessing() {
        // Scratch Removal's Frame Processing button: only with two-field media; off, the Frame/Field slider is labeled
        // Field (p.421, p.547). The button is on the Tool palette by default (p.420).
        QVERIFY(ToolPalette::defaultCommands().contains(QStringLiteral("fx.scratch_removal")));
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Scratch Removal"), {}}));
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        const QString cat = QStringLiteral("Scratch Removal Effect Scratch Parameters");
        QVERIFY(!editor.findChild<QToolButton*>(QStringLiteral("param:") + cat + QStringLiteral("/Frame Processing")));
        editor.setInterlacedProject(true);
        settle();
        auto* button = editor.findChild<QToolButton*>(QStringLiteral("param:") + cat + QStringLiteral("/Frame Processing"));
        QVERIFY(button);
        QVERIFY(button->isChecked());
        auto* label = editor.findChild<QLabel*>(QStringLiteral("paramLabel:") + cat + QStringLiteral("/Frame/Field"));
        QVERIFY(label);
        QCOMPARE(label->text(), QStringLiteral("Frame"));
        button->click();
        settle();
        const model::Effect e = *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0).effect;
        QCOMPARE(e.parameters.value(cat).toObject().value(QStringLiteral("Frame Processing")).toBool(true), false);
        QCOMPARE(label->text(), QStringLiteral("Field"));
    }

    void colorCorrectionShapes() {
        // The Color Correction effect in the Effect Editor (FX Guide p.699; 6.10e3b3): the drawing tools, Layers, a
        // selected shape's Softness and its tracker.
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Color Correction"), {}}));
        EffectEditor editor{timeline};
        editor.resize(700, 900);
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        const auto effect = [&] {
            const model::Sequence seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
            return *seq.track(QStringLiteral("V1"))->segments.at(0).effect;
        };
        IntraframeTool* tool = editor.intraframeTool();
        QVERIFY(editor.intraframeEffect());
        auto* oval = editor.findChild<QToolButton*>(QStringLiteral("intraframeOval"));
        QVERIFY(oval && oval->isVisible());
        oval->click();
        QVERIFY(tool->overlayPress(QPointF(0.2, 0.2), Qt::NoModifier, false));
        tool->overlayMove(QPointF(0.4, 0.5), Qt::NoModifier);
        tool->overlayRelease(QPointF(0.4, 0.5), Qt::NoModifier);
        settle();
        QList<model::IntraframeObject> shapes = model::intraframeObjects(effect());
        QCOMPARE(shapes.size(), 1);
        const QString id = shapes.at(0).id;
        QCOMPARE(editor.intraframeSelection(), QStringList{id});
        // A shape keeps only its own categories and its place.
        for (const QString& c : shapes.at(0).parameters.keys())
            QVERIFY2(model::isShapeOwnCategory(c) || c == model::kIntraframeObjectCategory, qPrintable(c));

        // Layers is the effect's, also with a shape selected: Serial Mode on at first, Invert Alpha set on the effect.
        auto* serial = editor.findChild<QCheckBox*>(QStringLiteral("param:Layers/Serial Mode"));
        auto* invert = editor.findChild<QCheckBox*>(QStringLiteral("param:Layers/Invert Alpha"));
        QVERIFY(serial && invert && serial->isChecked() && !invert->isChecked());
        invert->click();
        settle();
        QVERIFY(model::shapeOptionsOf(effect()).invertAlpha);
        QVERIFY(!model::intraframeObjects(effect()).at(0).parameters.contains(model::kShapeOptionsCategory));
        // Softness is the selected shape's.
        auto* softness = editor.findChild<QSlider*>(QStringLiteral("param:Shapes/Softness"));
        QVERIFY(softness);
        softness->setValue(30);
        settle();
        QCOMPARE(model::shapeSoftness(model::intraframeObjects(effect()).at(0)), 30.0);
        QVERIFY(!effect().parameters.contains(model::kShapeCategory));

        // "Select the tracker from the list of created trackers" (p.699): New Tracker makes Point A for the shape.
        auto* trackers = editor.findChild<QComboBox*>(QStringLiteral("tracking:shape"));
        QVERIFY(trackers);
        QCOMPARE(trackers->currentText(), QStringLiteral("No Tracker"));
        QVERIFY(trackers->findData(QStringLiteral("new")) >= 0);
        QVERIFY(editor.chooseShapeTracker(QStringLiteral("new")));
        settle();
        const model::Effect tracked = effect();
        QCOMPARE(model::trackingOf(tracked).trackers.size(), 1);
        const QString name = model::trackingOf(tracked).trackers.at(0).name;
        QCOMPARE(model::objectTracker(model::intraframeObjects(tracked).at(0)), name);
        QCOMPARE(trackers->currentData().toString(), name);
        editor.trackingWindow()->hide();
        // No Tracker detaches it again.
        QVERIFY(editor.chooseShapeTracker(QString()));
        settle();
        QVERIFY(model::objectTracker(model::intraframeObjects(effect()).at(0)).isEmpty());
        QCOMPARE(model::trackingOf(effect()).trackers.size(), 1);
    }

    void blurTools() {
        // The Blur effect has only the Rectangle and Oval drawing tools (p.439).
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Blur Effect"), {}}));
        EffectEditor editor{timeline};
        editor.show();
        composer.moveTo(MonitorId::Record, 10);
        timeline.setEffectMode(true);
        settle();
        QVERIFY(editor.findChild<QToolButton*>(QStringLiteral("intraframeOval"))->isVisible());
        QVERIFY(!editor.findChild<QToolButton*>(QStringLiteral("intraframeBrush"))->isVisible());
        QVERIFY(!editor.findChild<QToolButton*>(QStringLiteral("intraframePolygon"))->isVisible());
        // Another effect: no drawing tools.
        timeline.setEffectMode(false);
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(model::Effect{QStringLiteral("Image"), QStringLiteral("Flop"), {}}));
        timeline.setEffectMode(true);
        settle();
        QVERIFY(!editor.intraframeEffect());
        QVERIFY(!editor.findChild<QToolButton*>(QStringLiteral("intraframeRectangle"))->isVisible());
    }
};

QTEST_MAIN(TestIntraframeUi)
#include "tst_intraframe_ui.moc"
