// Titler+ from the Timeline (FX Guide p.303-305; Timeline::applyTitler): Tools > Titler+ and the Timeline's button
// put an empty title on the segment at the position, on the selected segments or between the marks, open the
// Effect Editor, undo in one step, and the "Title Text" Clip Text label shows a title's topmost text. Then the Record
// monitor's tools (6.8b1, p.303-319): typing, the Selection tool's moves, scaling and rotation, drawing shapes, and the
// keyboard shortcuts.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/titler.h"
#include "lumenis/media/titler.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/titler_panel.h"
#include "lumenis/ui/titler_tool.h"
#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// V1: A (50 frames), filler (30), B (40).
BinItem sequence() {
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
    model::Segment b = a;
    b.clipId = QStringLiteral("b");
    b.name = QStringLiteral("B");
    b.length = 40;
    seq.splice(QStringLiteral("V1"), 0, {a, model::Segment::filler(30), b});
    seq.storeIn(s);
    return s;
}

} // namespace

class TestTitlerUi : public QObject {
    Q_OBJECT

private slots:
    void apply() {
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        auto v1 = [&] { return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1")); };
        QSignalSpy opened(&timeline, &Timeline::titlerApplied);

        // On the empty track segment at the position (p.303).
        composer.moveTo(MonitorId::Record, 60);
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        QCOMPARE(opened.count(), 1);
        QVERIFY(v1().segments.at(1).effect && model::isTitlerEffect(*v1().segments.at(1).effect));
        QVERIFY(!v1().segments.at(0).effect);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!v1().segments.at(1).effect);

        // The Timeline's button does the same, here on the clip A.
        auto* button = timeline.findChild<QToolButton*>(QStringLiteral("tools.titler"));
        QVERIFY(button);
        composer.moveTo(MonitorId::Record, 10);
        button->click();
        QCOMPARE(opened.count(), 2);
        QVERIFY(v1().segments.at(0).effect && model::isTitlerEffect(*v1().segments.at(0).effect));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));

        // "set in and out marks in the Timeline to highlight a portion of the track": frames 40-89 become their own
        // segments with titles.
        composer.state(MonitorId::Record).setPosition(40);
        composer.state(MonitorId::Record).markIn();
        composer.state(MonitorId::Record).setPosition(89);
        composer.state(MonitorId::Record).markOut();
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        std::int64_t start = 0;
        int titled = 0;
        for (const model::Segment& s : v1().segments) {
            const bool title = s.effect && model::isTitlerEffect(*s.effect);
            QCOMPARE(title, start >= 40 && start < 90);
            titled += title ? 1 : 0;
            start += s.length;
        }
        QCOMPARE(titled, 3);

        // The "Title Text" label (p.305): on by default, the topmost text.
        model::Track track = v1();
        model::Segment& segment = track.segments[1];
        model::Title title = model::titleOf(*segment.effect);
        title.objects << model::textObject(title, QStringLiteral("Opening\nCredits"), QPointF());
        title.objects << model::shapeObject(title, model::TitleObjectKind::Rectangle, QPointF(), QSizeF(10, 10));
        model::setTitle(*segment.effect, title);
        QVERIFY(timeline.clipText(track, segment).contains(QStringLiteral("Opening")));
        QVERIFY(!timeline.clipText(track, segment).contains(QStringLiteral("Credits")));
        Timeline::Display d = timeline.display();
        QVERIFY(d.titleText);
        d.titleText = false;
        timeline.setDisplay(d);
        QVERIFY(!timeline.clipText(track, segment).contains(QStringLiteral("Opening")));

        // Nowhere to put it: no sequence.
        Composer empty;
        Timeline none{empty};
        QVERIFY(!none.applyTitler());
        QVERIFY(!none.lastMessage().isEmpty());
    }

    void tools() {
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        editor.resize(700, 900);
        editor.show();
        auto settle = [] {
            for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        };
        auto title = [&] {
            const model::Segment s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(1);
            return model::titleOf(*s.effect);
        };
        composer.moveTo(MonitorId::Record, 60);
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        timeline.setEffectMode(true);
        settle();
        TitlerTool* tool = editor.titlerTool();
        QVERIFY(tool);
        QCOMPARE(composer.overlayInput(), static_cast<Composer::OverlayInput*>(tool));
        QCOMPARE(tool->tool(), TitlerTool::Tool::Text);
        auto key = [&](int k, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& text = {}) {
            QKeyEvent e(QEvent::KeyPress, k, m, text);
            return tool->overlayKey(&e);
        };

        // "Click where you want to create the title in the Record monitor ... and begin typing" (p.303).
        QVERIFY(tool->overlayPress(tool->toFraction(QPointF(-300, 0)), Qt::NoModifier, false));
        tool->overlayRelease(tool->toFraction(QPointF(-300, 0)), Qt::NoModifier);
        QVERIFY(!tool->editing().isEmpty());
        for (const QChar c : QStringLiteral("Hi")) QVERIFY(key(c.toUpper().unicode(), Qt::NoModifier, QString(c)));
        // Typing shows before the undo step: the preview, and the overlay's caret.
        QCOMPARE(editor.shownTitle()->objects.first().text, QStringLiteral("Hi"));
        QVERIFY(title().objects.isEmpty());
        QVERIFY(composer.effectPreview().caret);
        QVERIFY(editor.pictureOverride());
        QVERIFY(key(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r")));
        for (const QChar c : QStringLiteral("there")) key(c.toUpper().unicode(), Qt::NoModifier, QString(c));
        // Shift+Left selects, typing replaces the selection, Backspace removes the character before the caret.
        key(Qt::Key_Left, Qt::ShiftModifier);
        key(Qt::Key_Left, Qt::ShiftModifier);
        QVERIFY(!composer.effectPreview().highlights.isEmpty());
        key(Qt::Key_I, Qt::NoModifier, QStringLiteral("i"));
        key(Qt::Key_Backspace);
        QVERIFY(key(Qt::Key_Escape));
        QVERIFY(tool->editing().isEmpty());
        QCOMPARE(title().objects.size(), 1);
        QCOMPARE(title().objects.first().text, QStringLiteral("Hi\nthe"));
        QCOMPARE(title().objects.first().transform.position, QPointF(-300, 0));
        // One undo step for the typing.
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(title().objects.isEmpty());
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));
        settle();
        // "If you click in an empty area but do not type anything, that empty layer gets deleted" (p.303).
        tool->overlayPress(tool->toFraction(QPointF(400, 300)), Qt::NoModifier, false);
        tool->overlayRelease(tool->toFraction(QPointF(400, 300)), Qt::NoModifier);
        key(Qt::Key_Escape);
        QCOMPARE(title().objects.size(), 1);

        // The Selection tool (M): click to select, drag to move; Shift keeps to one axis (p.311).
        QVERIFY(key(Qt::Key_M));
        QCOMPARE(tool->tool(), TitlerTool::Tool::Selection);
        const QString text = title().objects.first().id;
        const QPointF inside = media::titleObjectOutline(title(), text)->boundingRect().center();
        QVERIFY(tool->overlayPress(tool->toFraction(inside), Qt::NoModifier, false));
        QCOMPARE(tool->selection(), QStringList{text});
        tool->overlayMove(tool->toFraction(inside + QPointF(100, 30)), Qt::ShiftModifier);
        tool->overlayRelease(tool->toFraction(inside + QPointF(100, 30)), Qt::ShiftModifier);
        QCOMPARE(title().objects.first().transform.position, QPointF(-200, 0));
        settle();
        QCOMPARE(composer.effectPreview().boxes.size(), 1);
        QCOMPARE(composer.effectPreview().handles.size(), 4);
        QCOMPARE(composer.effectPreview().anchors.size(), 1);

        // Rotation just outside a corner, Shift in 15 degree steps (p.312).
        const QPolygonF box = *media::titleObjectOutline(title(), text);
        const QPointF anchor = media::titleObjectPlacement(title(), text)->map(title().objects.first().transform.anchor);
        const QPointF corner = box.at(1);
        const QPointF out = corner + (corner - anchor) / std::hypot(corner.x() - anchor.x(), corner.y() - anchor.y()) * 20.0;
        QVERIFY(tool->overlayPress(tool->toFraction(out), Qt::NoModifier, false));
        // A quarter turn about the anchor (y down: clockwise).
        const QPointF turned(anchor.x() - (out.y() - anchor.y()), anchor.y() + (out.x() - anchor.x()));
        tool->overlayRelease(tool->toFraction(turned + QPointF(3, 2)), Qt::ShiftModifier);
        QVERIFY2(std::abs(title().objects.first().transform.rotation - 90.0) < 1e-6, qPrintable(QString::number(title().objects.first().transform.rotation)));

        // The Rectangle tool: drag from corner to corner; Shift makes a square (p.306).
        tool->setTool(TitlerTool::Tool::Rectangle);
        QVERIFY(tool->overlayPress(tool->toFraction(QPointF(100, 100)), Qt::NoModifier, false));
        tool->overlayMove(tool->toFraction(QPointF(300, 150)), Qt::NoModifier);
        tool->overlayRelease(tool->toFraction(QPointF(300, 150)), Qt::ShiftModifier);
        QCOMPARE(title().objects.size(), 2);
        QCOMPARE(title().objects.last().kind, model::TitleObjectKind::Rectangle);
        QCOMPARE(title().objects.last().size, QSizeF(200, 200));
        // A click without a drag makes none.
        tool->overlayPress(tool->toFraction(QPointF(0, -300)), Qt::NoModifier, false);
        tool->overlayRelease(tool->toFraction(QPointF(0, -300)), Qt::NoModifier);
        QCOMPARE(title().objects.size(), 2);
        // An ellipse from its centre (Ctrl+Shift).
        tool->setTool(TitlerTool::Tool::Ellipse);
        tool->overlayPress(tool->toFraction(QPointF(-500, -300)), Qt::NoModifier, false);
        tool->overlayRelease(tool->toFraction(QPointF(-450, -280)), Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(title().objects.last().kind, model::TitleObjectKind::Ellipse);
        QCOMPARE(title().objects.last().transform.position, QPointF(-500, -300));
        QCOMPARE(title().objects.last().size, QSizeF(100, 100));
        const QString ellipse = title().objects.last().id;

        // Scaling a shape from its corner handle: wider and taller about its anchor (its centre).
        tool->setTool(TitlerTool::Tool::Selection);
        const QString rect = title().objects.at(1).id;
        tool->select({rect});
        const QPointF handle = media::titleObjectOutline(title(), rect)->at(2);
        QVERIFY(tool->overlayPress(tool->toFraction(handle), Qt::NoModifier, false));
        tool->overlayRelease(tool->toFraction(handle + QPointF(100, 0)), Qt::NoModifier);
        QCOMPARE(title().object(rect)->size, QSizeF(400, 200));

        // Layer order with - and + (p.318); Page Up selects the next object (p.319).
        tool->select({ellipse});
        QVERIFY(key(Qt::Key_Minus));
        QCOMPARE(title().objects.at(1).id, ellipse);
        QVERIFY(key(Qt::Key_Plus, Qt::NoModifier, QStringLiteral("+")));
        QCOMPARE(title().objects.last().id, ellipse);
        QVERIFY(key(Qt::Key_PageUp));
        QCOMPARE(tool->selection(), QStringList{text});
        // Ctrl+G groups the Shift-clicked objects, Ctrl+Shift+G ungroups (p.318).
        tool->select({text, rect});
        QKeyEvent ctrlG(QEvent::ShortcutOverride, Qt::Key_G, Qt::ControlModifier);
        QVERIFY(tool->overlayWantsKey(&ctrlG));
        QVERIFY(key(Qt::Key_G, Qt::ControlModifier));
        QCOMPARE(title().objects.size(), 2);
        QCOMPARE(title().objects.first().kind, model::TitleObjectKind::Group);
        QVERIFY(key(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier));
        QCOMPARE(title().objects.size(), 3);
        // Delete removes the selection; the space bar is left to playback when nothing is selected (p.305).
        tool->select({ellipse});
        QVERIFY(key(Qt::Key_Delete));
        QCOMPARE(title().objects.size(), 2);
        QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
        QVERIFY(!tool->overlayWantsKey(&space));
        // Double-clicking a text enters text mode (p.303).
        const QPointF onText = media::titleObjectOutline(title(), text)->boundingRect().center();
        QVERIFY(tool->overlayPress(tool->toFraction(onText), Qt::NoModifier, true));
        tool->overlayRelease(tool->toFraction(onText), Qt::NoModifier);
        QCOMPARE(tool->editing(), text);
        QCOMPARE(tool->tool(), TitlerTool::Tool::Text);
        key(Qt::Key_Escape);
        timeline.setEffectMode(false);
        settle();
        QVERIFY(!composer.overlayInput());
    }

    // The Dashboard and the Effect Editor's controls (6.8b2, p.306-324).
    void panel() {
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        editor.resize(700, 900);
        editor.show();
        auto settle = [] {
            for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        };
        auto title = [&] {
            const model::Segment s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(1);
            return model::titleOf(*s.effect);
        };
        composer.moveTo(MonitorId::Record, 60);
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        timeline.setEffectMode(true);
        settle();
        TitlerTool* tool = editor.titlerTool();
        // A text object, typed and selected.
        tool->overlayPress(tool->toFraction(QPointF(0, 0)), Qt::NoModifier, false);
        tool->overlayRelease(tool->toFraction(QPointF(0, 0)), Qt::NoModifier);
        for (const QChar c : QStringLiteral("Title")) {
            QKeyEvent e(QEvent::KeyPress, c.toUpper().unicode(), Qt::NoModifier, QString(c));
            tool->overlayKey(&e);
        }
        tool->setTool(TitlerTool::Tool::Selection);
        const QString text = title().objects.first().id;
        tool->select({text});
        settle();
        TitlerPanel* panel = editor.titlerPanel();
        QVERIFY(panel);
        QVERIFY(panel->control(QStringLiteral("section:Font"))->isEnabled());
        auto number = [&](const QString& name, double v) {
            auto* box = qobject_cast<QDoubleSpinBox*>(panel->control(name));
            QVERIFY(box);
            box->setValue(v);
            emit box->editingFinished();
            settle();
        };

        // Font: "click the current number in the Size field and enter a new number" (p.307); one undo step.
        number(QStringLiteral("size"), 120);
        QCOMPARE(title().objects.first().font.size, 120.0);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(title().objects.first().font.size, 72.0);
        QCOMPARE(qobject_cast<QDoubleSpinBox*>(panel->control(QStringLiteral("size")))->value(), 72.0);
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));
        qobject_cast<QToolButton*>(panel->control(QStringLiteral("justify:center")))->click();
        QCOMPARE(title().objects.first().font.horizontal, model::HorizontalJustify::Center);
        // Transform: Scale with Fixed Aspect keeps both (p.322); Opacity; Rotation.
        number(QStringLiteral("scaleX"), 200);
        QCOMPARE(title().objects.first().transform.scaleY, 200.0);
        number(QStringLiteral("rotation"), 10);
        QCOMPARE(title().objects.first().transform.rotation, 10.0);
        number(QStringLiteral("rotation"), 0);
        number(QStringLiteral("scaleX"), 100);
        // Style: Fill colour, Outline, Shadow (p.315-317).
        QVERIFY(panel->chooseColor(QStringLiteral("fill"), QColor(Qt::red)));
        QCOMPARE(title().objects.first().fill.color.color, QStringLiteral("#ff0000"));
        qobject_cast<QCheckBox*>(panel->control(QStringLiteral("outline")))->setChecked(true);
        QVERIFY(title().objects.first().outline.on);
        number(QStringLiteral("outlineSize"), 9);
        QCOMPARE(title().objects.first().outline.size, 9.0);
        qobject_cast<QCheckBox*>(panel->control(QStringLiteral("shadow")))->setChecked(true);
        number(QStringLiteral("shadowAngle"), 90);
        QVERIFY(title().objects.first().shadow.on);
        QCOMPARE(title().objects.first().shadow.angle, 90.0);

        // Align Left puts the object's left edge on the frame's (p.312).
        auto* alignH = qobject_cast<QComboBox*>(panel->control(QStringLiteral("alignH")));
        alignH->setCurrentIndex(1);
        emit alignH->activated(1);
        settle();
        const double width = 1080.0 * editor.previewSize().width() / editor.previewSize().height();
        QVERIFY(std::abs(media::titleObjectOutline(title(), text)->boundingRect().left() + width / 2.0) < 0.5);
        // With the Grid on, first to the Safe Title area (ours: 80%), then to the frame again (p.312).
        qobject_cast<QToolButton*>(panel->control(QStringLiteral("grid")))->click();
        settle();
        QVERIFY(tool->gridShown());
        QCOMPARE(composer.effectPreview().guides.size(), 2);
        emit alignH->activated(1);
        settle();
        QVERIFY(std::abs(media::titleObjectOutline(title(), text)->boundingRect().left() + width * 0.4) < 0.5);
        emit alignH->activated(1);
        settle();
        QVERIFY(std::abs(media::titleObjectOutline(title(), text)->boundingRect().left() + width / 2.0) < 0.5);
        // Anchor Point presets (p.313), and Custom after the sliders.
        auto* anchor = qobject_cast<QComboBox*>(panel->control(QStringLiteral("anchor")));
        emit anchor->activated(5); // Top Left
        settle();
        const QRectF bounds = media::titleObjectBounds(title().objects.first());
        QCOMPARE(title().objects.first().transform.anchor, bounds.topLeft());
        QCOMPARE(anchor->currentIndex(), 5);
        number(QStringLiteral("anchorX"), bounds.left() + 3);
        QCOMPARE(anchor->currentIndex(), anchor->count() - 1);

        // The Background and Foreground of the whole title (p.317, p.324).
        qobject_cast<QCheckBox*>(panel->control(QStringLiteral("background")))->setChecked(true);
        QVERIFY(panel->chooseColor(QStringLiteral("background"), QColor(Qt::blue)));
        QVERIFY(title().background.on);
        QCOMPARE(title().background.color.color, QStringLiteral("#0000ff"));
        number(QStringLiteral("cropTop"), 25);
        QCOMPARE(title().foreground.cropTop, 25.0);

        // The Title Text box (p.321).
        auto* box = qobject_cast<QPlainTextEdit*>(panel->control(QStringLiteral("text")));
        QVERIFY(box);
        QCOMPARE(box->toPlainText(), QStringLiteral("Title"));
        box->setFocus();
        box->setPlainText(QStringLiteral("Credits\nRoll"));
        QFocusEvent out(QEvent::FocusOut);
        QCoreApplication::sendEvent(box, &out);
        settle();
        QCOMPARE(title().objects.first().text, QStringLiteral("Credits\nRoll"));

        // A rectangle has no Font; the arrange buttons work on it.
        tool->setTool(TitlerTool::Tool::Rectangle);
        tool->overlayPress(tool->toFraction(QPointF(-100, -100)), Qt::NoModifier, false);
        tool->overlayRelease(tool->toFraction(QPointF(100, 100)), Qt::NoModifier);
        settle();
        QVERIFY(!panel->control(QStringLiteral("section:Font"))->isEnabled());
        qobject_cast<QToolButton*>(panel->control(QStringLiteral("backward")))->click();
        QCOMPARE(title().objects.first().kind, model::TitleObjectKind::Rectangle);
        QVERIFY(qobject_cast<QToolButton*>(panel->control(QStringLiteral("tool:rectangle")))->isChecked());

        // The Dashboard shows the same.
        TitlerDashboard* dashboard = editor.titlerDashboard();
        QVERIFY(dashboard && dashboard->panel());
        dashboard->panel()->refresh();
        QVERIFY(!dashboard->panel()->control(QStringLiteral("section:Font"))->isEnabled());
        QVERIFY(!dashboard->panel()->control(QStringLiteral("text")));
        timeline.setEffectMode(false);
    }
};

QTEST_MAIN(TestTitlerUi)
#include "tst_titler_ui.moc"
