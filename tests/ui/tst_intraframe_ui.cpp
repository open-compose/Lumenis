// The Intraframe drawing tools in the Effect Editor and Effect Preview (ROADMAP 6.9b1; FX Guide p.395-404, p.413-417;
// docs/spec/intraframe.md): drawing each kind of object, the tool returning to Selection, selecting, lassoing, moving,
// scaling, deleting, layers, and each object's own parameters in the editor.
#include "lumenis/model/bin.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/intraframe_tool.h"
#include "lumenis/ui/timeline.h"

#include <QComboBox>
#include <QKeyEvent>
#include <QSlider>
#include <QTest>
#include <QToolButton>

#include <cmath>

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
