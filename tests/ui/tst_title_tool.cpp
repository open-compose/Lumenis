// The classic Title Tool's window (ROADMAP 6.8d1; FX Guide p.327-341; src/ui/title_tool.*): typing text, the Selection
// tool's moves, lassos and handles, the drawing tools with Shift and sticky tools, the text formatting tools on objects,
// characters and the defaults, the drawing environment and its menus, the Edit menu, and opening the tool from the
// main window; rolling and crawling titles (6.8d2); line attributes, colours with blends and shadows (6.8d3a); layers,
// groups, locks, alignment and Duplicate (6.8d3b); saving, closing, exporting and dropped clips (6.8d4a); templates
// and styles (6.8d4b).
#include "lumenis/ext/provider.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/title_tool.h"

#include <QAction>
#include <QApplication>
#include <QLineF>
#include <QDoubleSpinBox>
#include <QMenuBar>
#include <QScrollBar>
#include <QCheckBox>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QKeySequence>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using model::ClassicKind;

namespace {

void type(TitleToolWindow& tool, const QString& text) {
    for (const QChar c : text) tool.key(c.unicode(), Qt::NoModifier, QString(c));
}

} // namespace

class TestTitleTool : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void textAndSelection() {
        TitleToolWindow tool;
        tool.setFrameSource([](QSize size) {
            QImage i(size, QImage::Format_RGB32);
            i.fill(Qt::darkGreen);
            return i;
        });
        tool.show();
        // "By default, the Text tool is active when you open the Title tool" (p.332); Safe Colors and the safe areas on.
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Text);
        QVERIFY(tool.safeColors() && tool.safeAreas() && tool.preview());
        QVERIFY(tool.title().videoBackground);
        QVERIFY(qobject_cast<QToolButton*>(tool.control(QStringLiteral("videoBackground")))->isChecked());
        // Typing: "Click the position in the frame where you want to add text ... Type the text" (p.335).
        tool.press(QPointF(300, 200), Qt::NoModifier);
        tool.release(QPointF(300, 200), Qt::NoModifier);
        QCOMPARE(tool.editing(), QStringLiteral("Text 1"));
        type(tool, QStringLiteral("Helo"));
        tool.key(Qt::Key_Left, Qt::NoModifier, QString());
        type(tool, QStringLiteral("l"));
        tool.key(Qt::Key_Return, Qt::NoModifier, QString());
        type(tool, QStringLiteral("x"));
        tool.key(Qt::Key_Backspace, Qt::NoModifier, QString());
        tool.key(Qt::Key_Backspace, Qt::NoModifier, QString());
        QCOMPARE(tool.title().objects.size(), 1);
        QCOMPARE(tool.title().objects.at(0).text, QStringLiteral("Hello"));
        QCOMPARE(tool.title().objects.at(0).rect.topLeft(), QPointF(300, 200));
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        QVERIFY(tool.editing().isEmpty());
        // One undo step for the typing.
        QVERIFY(tool.undo());
        QVERIFY(tool.title().objects.isEmpty());
        QVERIFY(tool.redo());
        QCOMPARE(tool.title().objects.size(), 1);
        // A click in empty space with nothing typed leaves nothing.
        tool.press(QPointF(1500, 900), Qt::NoModifier);
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        QCOMPARE(tool.title().objects.size(), 1);

        // The Selection tool: click selects, a drag moves, arrows move a pixel (p.332, p.336).
        tool.setTool(TitleToolWindow::Tool::Selection);
        const QRectF b = media::classicObjectBounds(tool.title().objects.at(0));
        tool.press(b.center(), Qt::NoModifier);
        tool.move(b.center() + QPointF(50, 10), Qt::NoModifier);
        tool.release(b.center() + QPointF(50, 10), Qt::NoModifier);
        QCOMPARE(tool.selection(), QStringList{QStringLiteral("Text 1")});
        QCOMPARE(tool.title().objects.at(0).rect.topLeft(), QPointF(350, 210));
        tool.key(Qt::Key_Right, Qt::NoModifier, QString());
        QCOMPARE(tool.title().objects.at(0).rect.left(), 351.0);
        QVERIFY(tool.undo());
        QCOMPARE(tool.title().objects.at(0).rect.left(), 350.0);
        // "You can resize only the width of a text object" (p.336): the right handle.
        const QRectF b2 = media::classicObjectBounds(tool.title().objects.at(0));
        const QPointF right(b2.right(), b2.center().y());
        tool.press(right, Qt::NoModifier);
        tool.move(right - QPointF(400, 0), Qt::NoModifier);
        tool.release(right - QPointF(400, 0), Qt::NoModifier);
        QCOMPARE(tool.title().objects.at(0).rect.width(), b2.width() - 400);

        // Formatting the selected object; with nothing selected, the defaults (p.337).
        tool.setPointSize(120);
        tool.setBold(true);
        tool.setJustify(model::ClassicJustify::Center);
        tool.setKerning(TitleToolWindow::kLooseKerning);
        QCOMPARE(tool.title().objects.at(0).size, 120.0);
        QVERIFY(tool.title().objects.at(0).bold);
        QCOMPARE(tool.title().objects.at(0).justify, model::ClassicJustify::Center);
        QCOMPARE(tool.title().objects.at(0).kerning, 10.0);
        QCOMPARE(qobject_cast<QDoubleSpinBox*>(tool.control(QStringLiteral("size")))->value(), 120.0);
        QVERIFY(tool.menuBar()->findChild<QAction*>(QStringLiteral("title:objectBold"))->isChecked());
        tool.select({});
        tool.setPointSize(30);
        QCOMPARE(tool.defaults().size, 30.0);
        QCOMPARE(tool.title().objects.at(0).size, 120.0);
        tool.setTool(TitleToolWindow::Tool::Text);
        tool.press(QPointF(200, 800), Qt::NoModifier);
        type(tool, QStringLiteral("Small"));
        // Characters: Shift+arrows highlight, the tools change those only (p.336).
        tool.key(Qt::Key_Home, Qt::NoModifier, QString());
        tool.key(Qt::Key_Right, Qt::ShiftModifier, QString());
        tool.key(Qt::Key_Right, Qt::ShiftModifier, QString());
        QCOMPARE(tool.anchor(), 0);
        QCOMPARE(tool.caret(), 2);
        tool.setPointSize(90);
        // Alt+Right kerns the highlighted characters (p.339).
        tool.key(Qt::Key_Right, Qt::AltModifier, QString());
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        const model::ClassicObject* small = tool.title().object(QStringLiteral("Text 2"));
        QVERIFY(small);
        QCOMPARE(small->size, 30.0);
        QCOMPARE(small->runs.size(), 1);
        QCOMPARE(small->runs.at(0).from, 0);
        QCOMPARE(small->runs.at(0).to, 2);
        QCOMPARE(*small->runs.at(0).format.size, 90.0);
        QCOMPARE(*small->runs.at(0).format.kerning, 1.0);
        // Alt+click switches between the Selection tool and the Text tool (p.332).
        tool.press(QPointF(10, 10), Qt::AltModifier);
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Selection);
        tool.press(QPointF(10, 10), Qt::AltModifier);
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Text);
        // Ctrl+Alt: the Selection tool for that press.
        tool.press(media::classicObjectBounds(*small).center(), Qt::ControlModifier | Qt::AltModifier);
        tool.release(media::classicObjectBounds(*small).center(), Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(tool.editing().isEmpty());
        QCOMPARE(tool.selection(), QStringList{QStringLiteral("Text 2")});
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Text);
    }

    void drawing() {
        TitleToolWindow tool;
        tool.show();
        // "Shift-drag to constrain the object to a square" (p.341); then back to the Selection tool.
        tool.setTool(TitleToolWindow::Tool::Box);
        tool.press(QPointF(100, 100), Qt::NoModifier);
        tool.move(QPointF(500, 300), Qt::ShiftModifier);
        tool.release(QPointF(500, 300), Qt::ShiftModifier);
        QCOMPARE(tool.title().objects.size(), 1);
        QCOMPARE(tool.title().objects.at(0).kind, ClassicKind::Box);
        QCOMPARE(tool.title().objects.at(0).rect, QRectF(100, 100, 400, 400));
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Selection);
        // A double-clicked tool stays (p.341).
        tool.setTool(TitleToolWindow::Tool::Oval, true);
        tool.press(QPointF(1000, 100), Qt::NoModifier);
        tool.release(QPointF(1200, 250), Qt::NoModifier);
        QCOMPARE(tool.tool(), TitleToolWindow::Tool::Oval);
        QCOMPARE(tool.title().objects.at(1).kind, ClassicKind::Oval);
        // Too small to be meant: nothing.
        tool.press(QPointF(1000, 600), Qt::NoModifier);
        tool.release(QPointF(1001, 601), Qt::NoModifier);
        QCOMPARE(tool.title().objects.size(), 2);
        // "Shift-drag to constrain the line to a horizontal or vertical line" (p.341).
        tool.setTool(TitleToolWindow::Tool::Line);
        tool.press(QPointF(100, 900), Qt::NoModifier);
        tool.move(QPointF(900, 940), Qt::ShiftModifier);
        tool.release(QPointF(900, 940), Qt::ShiftModifier);
        QCOMPARE(tool.title().objects.at(2).rect.bottomRight(), QPointF(900, 900));
        // The box's corner handle.
        tool.select({QStringLiteral("Box 1")});
        tool.press(QPointF(500, 500), Qt::NoModifier);
        tool.move(QPointF(700, 600), Qt::NoModifier);
        tool.release(QPointF(700, 600), Qt::NoModifier);
        QCOMPARE(tool.title().objects.at(0).rect, QRectF(100, 100, 600, 500));
        // A lasso round the line and the oval.
        tool.select({});
        tool.press(QPointF(50, 50), Qt::NoModifier);
        tool.release(QPointF(50, 50), Qt::NoModifier);
        tool.setTool(TitleToolWindow::Tool::Selection);
        tool.press(QPointF(800, 50), Qt::NoModifier);
        tool.move(QPointF(1900, 300), Qt::NoModifier);
        tool.release(QPointF(1900, 300), Qt::NoModifier);
        QCOMPARE(tool.selection(), QStringList{QStringLiteral("Oval 1")});
        // Shift+click adds.
        tool.press(QPointF(500, 900), Qt::ShiftModifier);
        tool.release(QPointF(500, 900), Qt::ShiftModifier);
        QCOMPARE(tool.selection().size(), 2);
        // Copy, paste, delete.
        tool.copy();
        tool.paste();
        QCOMPARE(tool.title().objects.size(), 5);
        QCOMPARE(tool.selection().size(), 2);
        tool.deleteSelection();
        QCOMPARE(tool.title().objects.size(), 3);
        // Align to Grid: "the object snaps to the nearest grid line" (p.334); the snap grid is a quarter of the
        // visible one.
        tool.setAlignToGrid(true);
        tool.setTool(TitleToolWindow::Tool::Box);
        tool.press(QPointF(101, 703), Qt::NoModifier);
        tool.release(QPointF(298, 905), Qt::NoModifier);
        QCOMPARE(tool.title().objects.last().rect, QRectF(90, 690, 210, 210));
        // The background: a colour (made safe) instead of video.
        tool.setBackgroundColor(QStringLiteral("#ff0000"));
        QVERIFY(!tool.title().videoBackground);
        QCOMPARE(tool.title().backgroundColor, model::safeColor(QStringLiteral("#ff0000")));
        tool.setVideoBackground(true);
        QVERIFY(tool.title().videoBackground);
        // The Object and Alignment menus.
        QAction* preview = tool.menuBar()->findChild<QAction*>(QStringLiteral("title:preview"));
        QVERIFY(preview && preview->isChecked());
        preview->trigger();
        QVERIFY(!tool.preview());
        tool.menuBar()->findChild<QAction*>(QStringLiteral("title:showGrid"))->trigger();
        QVERIFY(tool.gridShown());
        tool.menuBar()->findChild<QAction*>(QStringLiteral("title:safeTitleArea"))->trigger();
        QVERIFY(!tool.safeAreas());
        QApplication::processEvents();
        tool.repaint(); // the canvas draws with all of it
    }

    // Rolling and crawling titles (6.8d2, p.341-346).
    void rollingAndCrawling() {
        TitleToolWindow tool;
        tool.show();
        tool.setBackgroundColor(QStringLiteral("#203040"));
        QVERIFY(!tool.title().videoBackground);
        // The Rolling Title button: green, Video Background, Auto Size, the Page Number box.
        auto* roll = qobject_cast<QToolButton*>(tool.control(QStringLiteral("roll")));
        QVERIFY(roll);
        roll->click();
        QCOMPARE(tool.title().motion, model::ClassicMotion::Rolling);
        QVERIFY(roll->isChecked());
        QVERIFY(tool.title().videoBackground);
        QVERIFY(tool.title().autoSize);
        QVERIFY(!tool.control(QStringLiteral("page"))->isHidden());
        QVERIFY(!tool.findChild<QScrollBar*>(QStringLiteral("title:rollScroll"))->isHidden());
        // Typing past a page: Auto Size adds pages and the title scrolls down with the text.
        tool.setTool(TitleToolWindow::Tool::Text);
        tool.setPointSize(200);
        tool.press(QPointF(200, 150), Qt::NoModifier);
        for (int i = 0; i < 8; ++i) {
            type(tool, QStringLiteral("Line"));
            tool.key(Qt::Key_Return, Qt::NoModifier, QString());
        }
        type(tool, QStringLiteral("End"));
        QVERIFY2(tool.title().pages >= 2, qPrintable(QString::number(tool.title().pages)));
        QVERIFY(tool.scroll().y() > 0);
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        const int pages = tool.title().pages;
        // Home, End, Page Up, Page Down and the Page Number box (p.344-345).
        tool.key(Qt::Key_Home, Qt::NoModifier, QString());
        QCOMPARE(tool.page(), 1);
        tool.key(Qt::Key_PageDown, Qt::NoModifier, QString());
        QCOMPARE(tool.page(), 2);
        tool.key(Qt::Key_End, Qt::NoModifier, QString());
        QCOMPARE(tool.page(), pages);
        auto* pageBox = qobject_cast<QSpinBox*>(tool.control(QStringLiteral("page")));
        pageBox->setValue(1);
        QCOMPARE(tool.page(), 1);
        // With nothing selected the arrows go through the roll a line at a time.
        tool.select({});
        tool.key(Qt::Key_Down, Qt::NoModifier, QString());
        QCOMPARE(tool.scroll().y(), 54.0);
        // Add Page only with Auto Size off.
        QAction* addPage = tool.menuBar()->findChild<QAction*>(QStringLiteral("title:addPage"));
        QVERIFY(!addPage->isEnabled());
        QVERIFY(!tool.addPage());
        tool.setAutoSize(false);
        QVERIFY(addPage->isEnabled());
        QVERIFY(tool.addPage());
        QCOMPARE(tool.title().pages, pages + 1);
        // Auto Size again removes the extra page.
        tool.setAutoSize(true);
        QCOMPARE(tool.title().pages, pages);
        // Move to Page: the text object on to page 3 (pages added).
        tool.setTool(TitleToolWindow::Tool::Selection);
        tool.select({QStringLiteral("Text 1")});
        QVERIFY(tool.moveToPage(pages + 1));
        QVERIFY(tool.title().pages >= pages + 1);
        // The page limit: Copy to Page past 29 is refused.
        QVERIFY(!tool.copyToPage(30));
        // Crawling: the horizontal scroll bar; Make Crawl puts the text on one line on the lower third.
        tool.setMotion(model::ClassicMotion::Crawling);
        QVERIFY(qobject_cast<QToolButton*>(tool.control(QStringLiteral("crawl")))->isChecked());
        QVERIFY(!tool.findChild<QScrollBar*>(QStringLiteral("title:crawlScroll"))->isHidden());
        QVERIFY(tool.findChild<QScrollBar*>(QStringLiteral("title:rollScroll"))->isHidden());
        QVERIFY(tool.makeCrawl());
        const model::ClassicObject* text = tool.title().object(QStringLiteral("Text 1"));
        QVERIFY(text && !text->text.contains(QLatin1Char('\n')));
        QCOMPARE(text->rect.top(), 720.0);
        // "1080i and 1080p ... Crawling 4" (p.342): a fifth page is refused, with a message.
        tool.setAutoSize(false);
        while (tool.title().pages < 4) QVERIFY(tool.addPage());
        QVERIFY(!tool.limitMessage());
        QVERIFY(!tool.addPage());
        QCOMPARE(tool.title().pages, 4);
        QVERIFY(tool.limitMessage());
        // The lit button again: a still title.
        qobject_cast<QToolButton*>(tool.control(QStringLiteral("crawl")))->click();
        QCOMPARE(tool.title().motion, model::ClassicMotion::Still);
        QCOMPARE(tool.title().pages, 1);
        QVERIFY(tool.control(QStringLiteral("page"))->isHidden());
        tool.repaint();
    }

    // Line attributes, colours and transparency with blends, shadows (6.8d3, p.346-355).
    void attributes() {
        TitleToolWindow tool;
        tool.show();
        tool.setSafeColors(false);
        model::ClassicTitle t;
        model::ClassicObject box;
        box.id = QStringLiteral("Box 1");
        box.kind = ClassicKind::Box;
        box.rect = QRectF(100, 100, 400, 300);
        t.objects << box;
        tool.setTitle(t);
        tool.select({QStringLiteral("Box 1")});
        // The Box Corner button, up to 200; one undo step.
        tool.setCornerRadius(40);
        QCOMPARE(tool.title().objects.at(0).cornerRadius, 40.0);
        tool.setCornerRadius(500);
        QCOMPARE(tool.title().objects.at(0).cornerRadius, 200.0);
        QVERIFY(tool.undo());
        QCOMPARE(tool.title().objects.at(0).cornerRadius, 40.0);
        // The Border Width button gives a box a border; Remove Border takes it away.
        tool.setLineWidth(6);
        QCOMPARE(tool.title().objects.at(0).borderWidth, 6.0);
        tool.setLineWidth(0);
        QCOMPARE(tool.title().objects.at(0).borderWidth, 0.0);
        // Colours: the whole fill, then a blend to a second colour, its direction; the shadow's colour.
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#ff0000"));
        QCOMPARE(tool.title().objects.at(0).fill.color, QStringLiteral("#ff0000"));
        tool.setBlendColor(TitleToolWindow::Coat::Fill, 1, QStringLiteral("#0000ff"));
        QCOMPARE(tool.title().objects.at(0).fill.color2, QStringLiteral("#0000ff"));
        tool.setBlendAngle(TitleToolWindow::Coat::Fill, false, -90);
        QCOMPARE(tool.title().objects.at(0).fill.colorAngle, 270.0);
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#00ff00"));
        QVERIFY(tool.title().objects.at(0).fill.color2.isEmpty());
        // "You cannot blend the shadow" (p.350).
        tool.setBlendColor(TitleToolWindow::Coat::Shadow, 1, QStringLiteral("#0000ff"));
        tool.setColor(TitleToolWindow::Coat::Shadow, QStringLiteral("#202020"));
        QCOMPARE(tool.title().objects.at(0).shadowColor, QStringLiteral("#202020"));
        // With Safe Colors on, colours are made safe.
        tool.setSafeColors(true);
        tool.setColor(TitleToolWindow::Coat::Border, QStringLiteral("#ffffff"));
        QCOMPARE(tool.title().objects.at(0).border.color, model::safeColor(QStringLiteral("#ffffff")));
        // The blend tools follow the Fill and Border boxes; the slider the Transparency Level boxes.
        tool.chooseBox(TitleToolWindow::Coat::Shadow, false);
        QVERIFY(!tool.control(QStringLiteral("blendTools"))->isVisible());
        QVERIFY(!tool.control(QStringLiteral("transparencySlider"))->isVisible());
        tool.chooseBox(TitleToolWindow::Coat::Fill, true);
        QVERIFY(tool.control(QStringLiteral("blendTools"))->isVisible());
        auto* slider = qobject_cast<QSlider*>(tool.control(QStringLiteral("transparencySlider")));
        QVERIFY(slider && slider->isVisible());
        slider->setValue(30);
        QCOMPARE(tool.title().objects.at(0).fill.transparency, 30.0);
        // The second blend value from the slider.
        tool.chooseBox(TitleToolWindow::Coat::Fill, true, 1);
        slider->setValue(90);
        QCOMPARE(tool.title().objects.at(0).fill.transparency, 30.0);
        QCOMPARE(tool.title().objects.at(0).fill.transparency2.value_or(-1), 90.0);
        tool.setTransparency(TitleToolWindow::Coat::Fill, 10);
        QVERIFY(!tool.title().objects.at(0).fill.transparency2);
        // Shadows: depth, the drop or depth button, Shift+arrows and the softness.
        tool.setShadowDepth(10);
        QCOMPARE(tool.title().objects.at(0).shadowDepth, 10.0);
        QCOMPARE(qobject_cast<QSpinBox*>(tool.control(QStringLiteral("shadowDepth")))->value(), 10);
        tool.setShadowOffset(QPointF(5, 0));
        QVERIFY(tool.key(Qt::Key_Down, Qt::ShiftModifier, QString()));
        QVERIFY(QLineF(tool.title().objects.at(0).shadowOffset(), QPointF(5, 1)).length() < 1e-6);
        QCOMPARE(tool.title().objects.at(0).rect, box.rect); // the object stays
        qobject_cast<QToolButton*>(tool.control(QStringLiteral("shadowType")))->click();
        QVERIFY(tool.title().objects.at(0).depthShadow);
        tool.setShadowSoftness(2); // up from 0: 4
        QCOMPARE(tool.title().objects.at(0).shadowSoftness, 4);
        QCOMPARE(TitleToolWindow::defaultShadowSoftness(), 4);
        QVERIFY(tool.key(Qt::Key_Up, Qt::ShiftModifier | Qt::AltModifier, QString()));
        QCOMPARE(tool.title().objects.at(0).shadowSoftness, 5);
        tool.key(Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier, QString());
        tool.key(Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier, QString()); // 3: down to 0
        QCOMPARE(tool.title().objects.at(0).shadowSoftness, 0);
        // Object > Soften Shadow's dialog box.
        QTimer::singleShot(0, &tool, [&tool] {
            auto* dialog = tool.findChild<QDialog*>(QStringLiteral("title:softShadow"));
            QVERIFY(dialog);
            dialog->findChild<QSpinBox*>(QStringLiteral("title:shadowSoftness"))->setValue(12);
            dialog->accept();
        });
        tool.menuBar()->findChild<QAction*>(QStringLiteral("title:softenShadow"))->trigger();
        QCOMPARE(tool.title().objects.at(0).shadowSoftness, 12);
        // With nothing selected, the attributes become the defaults new objects take (p.347).
        tool.select({});
        tool.setLineWidth(8);
        tool.setArrows(model::ClassicArrows::Both);
        tool.setTool(TitleToolWindow::Tool::Line);
        tool.press(QPointF(100, 900), Qt::NoModifier);
        tool.release(QPointF(900, 900), Qt::NoModifier);
        const model::ClassicObject& line = tool.title().objects.last();
        QCOMPARE(line.kind, ClassicKind::Line);
        QCOMPARE(line.lineWidth, 8.0);
        QCOMPARE(line.arrows, model::ClassicArrows::Both);
        QCOMPARE(line.shadowSoftness, 12);
        // On a line, Border Width changes its width; arrowheads and their size apply to lines.
        tool.select({line.id});
        tool.setLineWidth(3);
        QCOMPARE(tool.title().objects.last().lineWidth, 3.0);
        QCOMPARE(tool.title().objects.last().borderWidth, 8.0); // a border is not a line's
        tool.setArrowSize(QSizeF(5, 4));
        QCOMPARE(tool.title().objects.last().arrowSize, QSizeF(5, 4));
        // The controls are there.
        for (const QString& name : {QStringLiteral("boxCorner"), QStringLiteral("borderWidth"), QStringLiteral("arrowhead"), QStringLiteral("fillColor"),
                                    QStringLiteral("shadowColor"), QStringLiteral("borderColor"), QStringLiteral("fillTransparency"),
                                    QStringLiteral("blend1"), QStringLiteral("blend2"), QStringLiteral("blendDirection"), QStringLiteral("shadowDirection")}) {
            QVERIFY2(tool.control(name), qPrintable(name));
        }
        QApplication::processEvents();
        tool.repaint();
    }

    // Layers, groups, locks, alignment, Duplicate (6.8d3b, p.355-357).
    void manipulating() {
        TitleToolWindow tool;
        tool.show();
        model::ClassicTitle t;
        for (int i = 0; i < 3; ++i) {
            model::ClassicObject o;
            o.kind = ClassicKind::Box;
            o.id = t.newId(ClassicKind::Box);
            o.rect = QRectF(300 + 400 * i, 300 + 100 * i, 200, 100);
            t.objects << o;
        }
        tool.setTitle(t);
        tool.setTool(TitleToolWindow::Tool::Selection);
        auto action = [&](const char* name) { return tool.menuBar()->findChild<QAction*>(QStringLiteral("title:") + QLatin1String(name)); };
        // Group: clicking one selects both; moving moves both.
        tool.select({QStringLiteral("Box 1"), QStringLiteral("Box 2")});
        action("group")->trigger();
        tool.select({});
        tool.press(QPointF(350, 350), Qt::NoModifier);
        QCOMPARE(tool.selection().size(), 2);
        tool.move(QPointF(360, 370), Qt::NoModifier);
        tool.release(QPointF(360, 370), Qt::NoModifier);
        QCOMPARE(tool.title().objects.at(1).rect.topLeft(), QPointF(710, 420));
        action("ungroup")->trigger();
        tool.select({QStringLiteral("Box 2")});
        QCOMPARE(tool.selection(), QStringList{QStringLiteral("Box 2")});
        // Layers.
        action("bringToFront")->trigger();
        QCOMPARE(tool.title().objects.last().id, QStringLiteral("Box 2"));
        action("sendBackward")->trigger();
        QCOMPARE(tool.title().objects.at(1).id, QStringLiteral("Box 2"));
        // Lock: no moves, nudges, attributes or deleting; the handles hollow (painted).
        action("lock")->trigger();
        QVERIFY(tool.title().object(QStringLiteral("Box 2"))->locked);
        const QRectF locked = tool.title().object(QStringLiteral("Box 2"))->rect;
        tool.key(Qt::Key_Right, Qt::NoModifier, QString());
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#ff0000"));
        tool.press(locked.center(), Qt::NoModifier);
        tool.move(locked.center() + QPointF(100, 100), Qt::NoModifier);
        tool.release(locked.center() + QPointF(100, 100), Qt::NoModifier);
        QCOMPARE(tool.title().object(QStringLiteral("Box 2"))->rect, locked);
        QCOMPARE(tool.title().object(QStringLiteral("Box 2"))->fill.color, QStringLiteral("#ffffff"));
        tool.select({QStringLiteral("Box 2")});
        tool.deleteSelection();
        QVERIFY(tool.title().object(QStringLiteral("Box 2")));
        tool.repaint();
        tool.select({QStringLiteral("Box 2")});
        action("unlock")->trigger();
        QVERIFY(!tool.title().object(QStringLiteral("Box 2"))->locked);
        // Align To Frame Bottom (Shift+Ctrl+Z) to the safe title area; to the last selected object.
        QCOMPARE(action("frameBottom")->shortcut(), QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_Z));
        QCOMPARE(action("centerHorizontal")->shortcut(), QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_C));
        action("frameBottom")->trigger();
        QCOMPARE(tool.title().object(QStringLiteral("Box 2"))->rect.bottom(), 972.0);
        tool.select({QStringLiteral("Box 1"), QStringLiteral("Box 3")});
        action("objectsLeft")->trigger();
        QCOMPARE(tool.title().object(QStringLiteral("Box 1"))->rect.left(), tool.title().object(QStringLiteral("Box 3"))->rect.left());
        tool.selectAll();
        action("distributeLeftToRight")->trigger();
        double left = 1e9, right = -1e9;
        for (const model::ClassicObject& o : tool.title().objects) {
            left = std::min(left, o.rect.left());
            right = std::max(right, o.rect.right());
        }
        QCOMPARE(left, 192.0);
        QCOMPARE(right, 1728.0);
        // Duplicate: copies moved down and right, selected; Deselect All.
        tool.select({QStringLiteral("Box 1")});
        const QRectF one = tool.title().object(QStringLiteral("Box 1"))->rect;
        action("duplicate")->trigger();
        QCOMPARE(tool.title().objects.size(), 4);
        QCOMPARE(tool.title().objects.last().rect, one.translated(20, 20));
        QCOMPARE(tool.selection(), QStringList{tool.title().objects.last().id});
        action("deselectAll")->trigger();
        QVERIFY(tool.selection().isEmpty());
        // Undo takes back the duplicate.
        QVERIFY(tool.undo());
        QCOMPARE(tool.title().objects.size(), 3);
    }

    // Saving (6.8d4a, p.357-360, p.364): the File menu, Save Title and Save Title As, the Close prompt, Export Title and
    // a Title Effect clip dropped on the tool.
    void saving() {
        TitleToolWindow tool;
        tool.show();
        struct Call {
            TitleToolWindow::SaveRequest request;
            QString replace;
        };
        QList<Call> calls;
        TitleToolWindow::Saver saver;
        saver.bins = [] { return QList<std::pair<QString, QString>>{{QStringLiteral("b1"), QStringLiteral("Titles")}}; };
        saver.defaults = [] {
            TitleToolWindow::SaveRequest r;
            r.bin = QStringLiteral("b1");
            r.resolution = QStringLiteral("dnx_hq");
            return r;
        };
        saver.save = [&](const model::ClassicTitle&, const TitleToolWindow::SaveRequest& r, const QString& replace, QString*) {
            calls << Call{r, replace};
            return QStringLiteral("clip%1").arg(calls.size());
        };
        tool.setSaver(saver);
        int asked = 0;
        tool.setSaveDialog([&](const TitleToolWindow::SaveRequest& suggestion) -> std::optional<TitleToolWindow::SaveRequest> {
            ++asked;
            TitleToolWindow::SaveRequest r = suggestion;
            r.name = QStringLiteral("Lower Third");
            r.fastSave = true;
            return r;
        });
        QSignalSpy saved(&tool, &TitleToolWindow::titleSaved);
        // A new title: Save Title asks first.
        tool.setTool(TitleToolWindow::Tool::Box);
        tool.press(QPointF(100, 100), Qt::NoModifier);
        tool.release(QPointF(500, 300), Qt::NoModifier);
        QVERIFY(tool.isModified());
        auto action = [&](const char* name) { return tool.menuBar()->findChild<QAction*>(QStringLiteral("title:") + QLatin1String(name)); };
        QCOMPARE(tool.menuBar()->actions().first()->text(), QStringLiteral("File"));
        action("saveTitle")->trigger();
        QCOMPARE(asked, 1);
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.at(0).request.bin, QStringLiteral("b1"));
        QVERIFY(calls.at(0).replace.isEmpty());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(tool.clipId(), QStringLiteral("clip1"));
        QVERIFY(!tool.isModified());
        // Fast Save stays on (p.358).
        QVERIFY(tool.fastSave());
        // Revised: Save Title replaces the clip without asking.
        tool.setCornerRadius(20);
        tool.saveTitle();
        QCOMPARE(asked, 1);
        QCOMPARE(calls.size(), 2);
        QCOMPARE(calls.at(1).replace, QStringLiteral("clip1"));
        QCOMPARE(calls.at(1).request.name, QStringLiteral("Lower Third"));
        // Save Title As (Shift+Ctrl+S) always asks and makes a new clip.
        QCOMPARE(action("saveTitleAs")->shortcut(), QKeySequence(Qt::SHIFT | Qt::CTRL | Qt::Key_S));
        action("saveTitleAs")->trigger();
        QCOMPARE(asked, 2);
        QVERIFY(calls.at(2).replace.isEmpty());
        // Close: a changed title asks; Cancel keeps the window, Don't Save closes it.
        tool.setCornerRadius(40);
        int prompts = 0;
        tool.setClosePrompt([&] {
            ++prompts;
            return prompts == 1 ? int(QMessageBox::Cancel) : int(QMessageBox::Discard);
        });
        QVERIFY(!tool.closeTitle());
        QVERIFY(tool.isVisible());
        QVERIFY(tool.closeTitle());
        QVERIFY(!tool.isVisible());
        QVERIFY(!tool.fastSave()); // "until you ... exit the Title tool"
        // Export Title: the frame with its alpha.
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("title.png"));
        QVERIFY(tool.exportTitle(png));
        const QImage exported(png);
        QCOMPARE(exported.size(), QSize(1920, 1080));
        QVERIFY(exported.hasAlphaChannel());
        QCOMPARE(qAlpha(exported.pixel(1800, 1000)), 0);
        QCOMPARE(qAlpha(exported.pixel(300, 200)), 255);
        // A Title Effect clip dropped from a bin opens for revising (p.360).
        QStringList opened;
        tool.setClipOpener([&](const QString& bin, const QString& id) {
            opened << bin + QLatin1Char('/') + id;
            return true;
        });
        tool.show();
        auto* data = new QMimeData;
        data->setData(binItemsMime(), QByteArrayLiteral("b1\nclip2\n"));
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tool.canvas(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tool.canvas(), &drop);
        QCOMPARE(opened, QStringList{QStringLiteral("b1/clip2")});
        delete data;
        // openClip: the clip's title, not modified, Fast Save off for a title saved without it.
        model::ClassicTitle t;
        t.objects << tool.title().objects;
        tool.openClip(t, QStringLiteral("b1"), QStringLiteral("clip9"), QStringLiteral("Credits"), false);
        QCOMPARE(tool.clipId(), QStringLiteral("clip9"));
        QVERIFY(!tool.isModified());
        tool.hide();
    }

    // Title templates and title styles (6.8d4b, p.360-364).
    void templatesAndStyles() {
        TitleToolWindow tool;
        tool.show();
        tool.setSafeColors(false);
        auto action = [&](const char* name) { return tool.menuBar()->findChild<QAction*>(QStringLiteral("title:") + QLatin1String(name)); };
        QVERIFY(action("saveTemplate") && action("includeTemplate") && action("moveableTemplates"));
        // A template: a box and a name.
        tool.setTool(TitleToolWindow::Tool::Box);
        tool.press(QPointF(200, 800), Qt::NoModifier);
        tool.release(QPointF(1000, 950), Qt::NoModifier);
        tool.setTool(TitleToolWindow::Tool::Text);
        tool.press(QPointF(1200, 300), Qt::NoModifier);
        type(tool, QStringLiteral("NAME"));
        tool.setTool(TitleToolWindow::Tool::Selection);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("lower.ttpl"));
        QVERIFY(tool.saveTemplate(path));
        // Included in a new title: fixed but for their text.
        tool.newTitle();
        QVERIFY(tool.includeTemplate(path));
        QCOMPARE(tool.title().objects.size(), 2);
        QVERIFY(tool.title().objects.at(0).fromTemplate);
        const QRectF place = tool.title().objects.at(0).rect;
        tool.setTool(TitleToolWindow::Tool::Selection);
        tool.select({tool.title().objects.at(0).id});
        tool.press(place.center(), Qt::NoModifier);
        tool.move(place.center() + QPointF(100, 0), Qt::NoModifier);
        tool.release(place.center() + QPointF(100, 0), Qt::NoModifier);
        QCOMPARE(tool.title().objects.at(0).rect, place);
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#ff0000"));
        QCOMPARE(tool.title().objects.at(0).fill.color, QStringLiteral("#ffffff"));
        tool.deleteSelection();
        QCOMPARE(tool.title().objects.size(), 2);
        // Its text can be typed in.
        const model::ClassicObject name = tool.title().objects.at(1);
        tool.setTool(TitleToolWindow::Tool::Text);
        tool.press(media::classicObjectBounds(name).center(), Qt::NoModifier);
        tool.key(Qt::Key_End, Qt::NoModifier, QString());
        type(tool, QStringLiteral("!"));
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        QCOMPARE(tool.title().objects.at(1).text, QStringLiteral("NAME!"));
        // Moveable Templates: the arrow keys move it.
        action("moveableTemplates")->trigger();
        QVERIFY(tool.moveableTemplates());
        tool.setTool(TitleToolWindow::Tool::Selection);
        tool.select({tool.title().objects.at(0).id});
        QVERIFY(tool.key(Qt::Key_Right, Qt::NoModifier, QString()));
        QCOMPARE(tool.title().objects.at(0).rect, place.translated(1, 0));
        QVERIFY(!tool.includeTemplate(dir.filePath(QStringLiteral("missing.ttpl"))));

        // Styles: Save Style Parameter > Save As through the Title Style Sheet; the Styles menu; a function key.
        tool.newTitle();
        tool.setFont(QStringLiteral("Serif"));
        tool.setPointSize(144);
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#ffcc00"));
        tool.setStyleSheetDialog([](const model::ClassicTitleStyle& suggestion) -> std::optional<model::ClassicTitleStyle> {
            model::ClassicTitleStyle s = suggestion;
            s.name = QStringLiteral("Gold");
            s.functionKey = 3;
            s.fields = {model::ClassicStyleField::Size, model::ClassicStyleField::FillColor};
            return s;
        });
        auto* saveButton = qobject_cast<QToolButton*>(tool.control(QStringLiteral("saveStyle")));
        QVERIFY(saveButton && saveButton->menu());
        QAction* saveAs = saveButton->menu()->findChild<QAction*>(QStringLiteral("title:saveStyleAs"));
        QVERIFY(saveAs);
        saveAs->trigger();
        QCOMPARE(tool.styles().size(), 1);
        QCOMPARE(tool.styles().first().values.size, 144.0);
        auto* stylesButton = qobject_cast<QToolButton*>(tool.control(QStringLiteral("styles")));
        QCOMPARE(stylesButton->menu()->actions().size(), 1);
        // A new text object with other defaults, then the style on it.
        tool.setPointSize(48);
        tool.setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#ffffff"));
        tool.setTool(TitleToolWindow::Tool::Text);
        tool.press(QPointF(300, 300), Qt::NoModifier);
        type(tool, QStringLiteral("Hi"));
        tool.key(Qt::Key_Escape, Qt::NoModifier, QString());
        tool.select({tool.title().objects.last().id});
        QVERIFY(tool.key(Qt::Key_F3, Qt::NoModifier, QString()));
        QCOMPARE(tool.title().objects.last().size, 144.0);
        QCOMPARE(tool.title().objects.last().fill.color, QStringLiteral("#ffcc00"));
        QVERIFY(!tool.key(Qt::Key_F4, Qt::NoModifier, QString()));
        // With nothing selected it becomes the defaults.
        tool.select({});
        stylesButton->menu()->actions().first()->trigger();
        QCOMPARE(tool.defaults().size, 144.0);
        // The Title Style Sheet dialog itself: Done keeps what is checked.
        QTimer::singleShot(0, &tool, [] {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w->objectName() != QLatin1String("title:styleSheet")) continue;
                w->findChild<QCheckBox*>(QStringLiteral("title:styleField0"))->setChecked(false);
                w->findChild<QLineEdit*>(QStringLiteral("title:styleName"))->setText(QStringLiteral("Plain"));
                w->findChild<QPushButton*>(QStringLiteral("title:styleDone"))->click();
            }
        });
        const auto edited = titleStyleSheet(&tool, tool.styles().first());
        QVERIFY(edited);
        QCOMPARE(edited->name, QStringLiteral("Plain"));
        tool.hide();
    }

    void fromMainWindow() {
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        QVERIFY(!window.titleTool());
        window.openTitleTool(false);
        QVERIFY(window.titleTool());
        QVERIFY(window.titleTool()->isVisible());
        window.titleTool()->hide();
    }
};

QTEST_MAIN(TestTitleTool)
#include "tst_title_tool.moc"
