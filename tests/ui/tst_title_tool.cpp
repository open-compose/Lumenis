// The classic Title Tool's window (ROADMAP 6.8d1; FX Guide p.327-341; src/ui/title_tool.*): typing text, the Selection
// tool's moves, lassos and handles, the drawing tools with Shift and sticky tools, the text formatting tools on objects,
// characters and the defaults, the drawing environment and its menus, the Edit menu, and opening the tool from the
// main window.
#include "lumenis/ext/provider.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/title_tool.h"

#include <QAction>
#include <QDoubleSpinBox>
#include <QMenuBar>
#include <QScrollBar>
#include <QSpinBox>
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
