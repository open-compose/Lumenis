// The Script window in the application (8.3a; Editing Guide p.570-576): File > New > New Script importing a text file
// into the active bin, the window showing it, Add Scene and Add Page, the Scene and Page menus and status bar, the
// Delete dialog, Edit Mode with the numbers following the lines, Find and Replace, Text Layout, Save Script into the
// bin item, Save a Script Copy As, reopening by double-click. Slates and takes (8.3b, p.576-582): clips dropped from a
// bin, the tabs' Take numbers, selecting, Delete Takes, the arrow keys, Set Offscreen and Set Color, moving slates,
// loading and playing takes with Interpolate Position, the Script menu's options and Enlarge/Reduce Frame.
#include "lumenis/model/script.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/script_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;

class TestScriptUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void scriptWindow() {
        QTemporaryDir dir;
        const QString text = dir.filePath(QStringLiteral("Diner.txt"));
        {
            QFile f(text);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("INT. DINER - NIGHT\nMARY SUE\nCoffee?\nWAITRESS\nComing right up.\n");
        }
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        window.settings().setValue(QStringLiteral("Script"), QStringLiteral("leftMargin"), 72);
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* bin = bins.openBin(binId);
        QVERIFY(bin);
        // File > New > New Script: the text into a script item named after the file, opened.
        QVERIFY(window.newScript(text));
        QCOMPARE(bin->bin().items.size(), qsizetype(1));
        const model::BinItem item = bin->bin().items.first();
        QCOMPARE(item.name, QStringLiteral("Diner"));
        QCOMPARE(item.kind, model::BinItemKind::Script);
        ScriptWindow& w = window.scriptWindow();
        QVERIFY(w.isOpen());
        QCOMPARE(w.script().lines.size(), qsizetype(5));
        QCOMPARE(w.textLayout().leftMargin, 72); // the Script setting at import
        QCOMPARE(w.title(), QStringLiteral("Diner"));

        // Add Scene and Add Page on the selected line through the AS and AP buttons (p.574).
        w.setAskHooks([](const QString& title, const QString&) -> std::optional<QString> {
            return title == QLatin1String("Add Scene") ? QStringLiteral("33") : QStringLiteral("12");
        }, [](bool, bool, int) { return std::optional<ScriptDeleteChoice>(ScriptDeleteChoice{true, false, false}); });
        w.selectLines(0, 0);
        w.findChild<QToolButton*>(QStringLiteral("addScene"))->click();
        w.selectLines(1, 2);
        commands.action(QStringLiteral("script.add_page"))->trigger();
        QCOMPARE(w.script().scenes.value(0), QStringLiteral("33"));
        QCOMPARE(w.script().pages.value(1), QStringLiteral("12"));
        QCOMPARE(w.title(), QStringLiteral("Diner*"));
        w.selectLines(4, 4);
        QCOMPARE(w.statusText(), QStringLiteral("Scene 33   Page 12   Line 5"));
        auto* sceneMenu = w.findChild<QComboBox*>(QStringLiteral("sceneMenu"));
        QCOMPARE(sceneMenu->count(), 1);
        QVERIFY(w.goToPage(QStringLiteral("12")));
        QCOMPARE(w.selectedLines()->first, 1);

        // Edit Mode: a line typed; the numbers stay with their lines.
        w.setEditMode(true);
        QVERIFY(w.editLine(2, QStringLiteral("Coffee, please?\nAnd pie.")));
        QCOMPARE(w.script().lines.size(), qsizetype(6));
        QCOMPARE(w.script().lines.at(3), QStringLiteral("And pie."));
        QCOMPARE(w.script().pages.value(1), QStringLiteral("12"));
        // Find and Replace.
        w.setFindToolsShown(true);
        w.setFindText(QStringLiteral("pie"), QStringLiteral("cake"));
        QVERIFY(w.findNext());
        QCOMPARE(w.replaceAll(), 1);
        QCOMPARE(w.script().lines.at(3), QStringLiteral("And cake."));
        w.setEditMode(false);

        // The Delete dialog on the first line's scene (p.574).
        w.selectLines(0, 1);
        w.askDelete();
        QVERIFY(w.script().scenes.isEmpty());
        QCOMPARE(w.script().pages.value(1), QStringLiteral("12")); // page breaks were not chosen

        // Text Layout (p.571-572).
        model::ScriptLayout layout = w.textLayout();
        layout.textWidth = 420;
        layout.wordWrap = false;
        QVERIFY(window.scriptTextLayout(layout));
        QCOMPARE(w.textLayout().textWidth, 420);

        // Save Script: the bin item holds it; reopened by double-click it comes back the same.
        commands.action(QStringLiteral("file.save_script"))->trigger();
        QCOMPARE(w.title(), QStringLiteral("Diner"));
        const auto saved = model::scriptOf(*bin->bin().item(item.id));
        QVERIFY(saved);
        QCOMPARE(saved->lines.at(3), QStringLiteral("And cake."));
        QCOMPARE(saved->pages.value(1), QStringLiteral("12"));
        QVERIFY(!saved->layout.wordWrap);
        w.close();
        emit bins.itemActivated(binId, item.id);
        QVERIFY(w.isOpen());
        QCOMPARE(w.script(), *saved);

        // Save a Script Copy As: a second item, now shown.
        QVERIFY(window.saveScriptCopyAs(QStringLiteral("Diner v2")));
        QCOMPARE(bin->bin().items.size(), qsizetype(2));
        QCOMPARE(w.name(), QStringLiteral("Diner v2"));
        QVERIFY(w.itemId() != item.id);
    }

    void slatesAndTakes() {
        QTemporaryDir dir;
        const QString text = dir.filePath(QStringLiteral("Scene 33.txt"));
        {
            QFile f(text);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("INT. DINER - NIGHT\nMARY SUE\nCoffee?\nWAITRESS\nComing right up.\nMARY SUE\nThanks.\nEND\n");
        }
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1400, 800);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* bin = bins.openBin(binId);
        QVERIFY(bin);
        // Three takes of the master shot, with the Take column filled in (p.580).
        QStringList clipIds;
        for (int i = 1; i <= 3; ++i) {
            model::BinItem clip;
            clip.id = model::newItemId();
            clip.kind = model::BinItemKind::MasterClip;
            clip.name = QStringLiteral("33/%1").arg(i);
            clip.durationFrames = 100;
            clip.fields.insert(QStringLiteral("Take"), QString::number(i));
            bin->addItem(clip);
            clipIds << clip.id;
        }
        QVERIFY(window.newScript(text));
        ScriptWindow& w = window.scriptWindow();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* view = w.findChild<QWidget*>(QStringLiteral("scriptText"));
        QVERIFY(view);
        auto* textView = qobject_cast<QAbstractScrollArea*>(view);
        QVERIFY(textView);

        // "Select the portion of the script that is covered by the first clip or clips ... and drag them to the highlighted
        // text" (p.576): a drop of two bin items on lines 2-5 makes one slate with two takes.
        w.selectLines(1, 4);
        auto drop = [&](const QStringList& ids, QPoint at) {
            QMimeData mime;
            mime.setData(binItemsMime(), (QStringList{binId} + ids).join(QLatin1Char('\n')).toUtf8());
            QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(textView->viewport(), &enter);
            QDropEvent dropped(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(textView->viewport(), &dropped);
        };
        drop({clipIds[0], clipIds[1]}, QPoint(100, 30));
        QCOMPARE(w.script().slates.size(), qsizetype(1));
        const model::ScriptSlate slate = w.script().slates.first();
        QCOMPARE(slate.takes.size(), qsizetype(2));
        QCOMPARE(slate.takes[0].clipName, QStringLiteral("33/1"));
        QCOMPARE(slate.takes[1].take, QStringLiteral("2"));
        QCOMPARE(slate.takes[0].first, 1);
        QCOMPARE(slate.takes[0].last, 4);
        QVERIFY(w.title().endsWith(QLatin1Char('*')));
        // "Drag the clip to the slate. The new take appears in the slate" (p.580): onto the slate's box.
        w.selectLines(1, 6);
        const QRect box = w.slateRect(slate.id);
        QVERIFY(!box.isEmpty());
        drop({clipIds[2]}, box.center());
        QCOMPARE(w.script().slates.size(), qsizetype(1));
        QCOMPARE(w.script().slates.first().takes.size(), qsizetype(3));
        const QString take1 = slate.takes[0].id;
        const QString take2 = slate.takes[1].id;
        const QString take3 = w.script().slates.first().takes[2].id;
        QCOMPARE(model::takeOf(w.script(), take3)->last, 6);

        // "Click any take tab to select it ... Ctrl+click ... additional takes" (p.580).
        const QRect tab1 = w.takeTabRect(take1);
        QVERIFY(!tab1.isEmpty());
        QTest::mouseClick(textView->viewport(), Qt::LeftButton, Qt::NoModifier, tab1.center());
        QCOMPARE(w.selectedTakes(), QStringList{take1});
        QTest::mouseClick(textView->viewport(), Qt::LeftButton, Qt::ControlModifier, w.takeTabRect(take2).center());
        QCOMPARE(w.selectedTakes(), (QStringList{take1, take2}));
        // A lasso through the slate area takes in every take (p.578).
        const QRect all = w.slateRect(slate.id).united(w.takeTabRect(take3));
        QTest::mousePress(textView->viewport(), Qt::LeftButton, Qt::NoModifier, all.topRight() + QPoint(30, -2));
        QTest::mouseMove(textView->viewport(), all.bottomLeft() + QPoint(-4, 4));
        QTest::mouseRelease(textView->viewport(), Qt::LeftButton, Qt::NoModifier, all.bottomLeft() + QPoint(-4, 4));
        QCOMPARE(w.selectedTakes().size(), qsizetype(3));

        // The arrow keys step the selected takes' frames (p.581).
        w.selectTakes({take1, take2});
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_Right);
        QTest::keyClick(view, Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(model::takeOf(w.script(), take1)->frame, 0); // not before 0
        QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(model::takeOf(w.script(), take2)->frame, 10);
        QCOMPARE(model::takeOf(w.script(), take3)->frame, 0);

        // Set Offscreen and Set Color on lines 3-4 of the selected takes; "switch the indicators on or off by clicking the
        // button repeatedly" (p.581-582).
        w.selectLines(2, 3);
        auto* offscreen = w.findChild<QToolButton*>(QStringLiteral("setOffscreen"));
        offscreen->click();
        QVERIFY(model::isOffscreen(*model::takeOf(w.script(), take1), 3));
        QVERIFY(!model::isOffscreen(*model::takeOf(w.script(), take3), 3));
        QVERIFY(offscreen->isChecked());
        offscreen->click();
        QVERIFY(!model::isOffscreen(*model::takeOf(w.script(), take1), 3));
        commands.action(QStringLiteral("script.color.green"))->trigger();
        QVERIFY(commands.action(QStringLiteral("script.color.green"))->isChecked());
        w.findChild<QToolButton*>(QStringLiteral("setColor"))->click();
        QCOMPARE(model::colorAt(*model::takeOf(w.script(), take2), 2), QStringLiteral("#3cb446"));

        // Show All Takes off: a slate with no selected take shows only its active take (p.578).
        QVERIFY(commands.action(QStringLiteral("script.show_all_takes"))->isChecked());
        commands.action(QStringLiteral("script.show_all_takes"))->trigger();
        QVERIFY(!w.slateOptions().showAllTakes);
        w.selectTakes({});
        QVERIFY(w.takeTabRect(take3).isEmpty() != w.takeTabRect(take1).isEmpty());
        commands.action(QStringLiteral("script.show_all_takes"))->trigger();
        // Show Frames off: only the clip names; the slate is smaller.
        const int tall = w.slateRect(slate.id).height();
        commands.action(QStringLiteral("script.show_frames"))->trigger();
        QVERIFY(w.slateRect(slate.id).height() < tall);
        commands.action(QStringLiteral("script.show_frames"))->trigger();
        // Enlarge Frame with the Script panel active (p.578).
        window.panels().activate(window.panels().panel(QStringLiteral("Script")));
        const int wide = w.slateRect(slate.id).width();
        commands.action(QStringLiteral("edit.enlarge_frame"))->trigger();
        QVERIFY(w.slateRect(slate.id).width() > wide);

        // Moving the slate alone keeps the lines (p.579); Ctrl+drag moves them with it.
        w.moveSlate(slate.id, 1, 20, false);
        QCOMPARE(w.script().slates.first().line, slate.line + 1);
        QCOMPARE(model::takeOf(w.script(), take1)->first, 1);
        QTest::mousePress(textView->viewport(), Qt::LeftButton, Qt::ControlModifier, w.slateRect(slate.id).center());
        const auto line0 = w.slateRect(slate.id).center();
        // A drag of about one line down.
        const int lineHeight = textView->fontMetrics().lineSpacing();
        QTest::mouseMove(textView->viewport(), line0 + QPoint(0, lineHeight + 2));
        QTest::mouseRelease(textView->viewport(), Qt::LeftButton, Qt::ControlModifier, line0 + QPoint(0, lineHeight + 2));
        QCOMPARE(model::takeOf(w.script(), take1)->first, 2);
        QCOMPARE(model::takeOf(w.script(), take1)->last, 5);
        // A take's end changed (p.581).
        w.setTakeLines(take1, 2, 7);
        QCOMPARE(model::takeOf(w.script(), take1)->last, 7);

        // Double-click a take's tab: its clip in the Source monitor at its representative frame (p.581); on its line with
        // Interpolate Position, where that line falls in the clip (p.577).
        QTest::mouseDClick(textView->viewport(), Qt::LeftButton, Qt::NoModifier, w.takeTabRect(take2).center());
        Composer& composer = window.composer();
        QVERIFY(composer.state(MonitorId::Source).current());
        QCOMPARE(composer.state(MonitorId::Source).current()->item.id, clipIds[1]);
        QCOMPARE(composer.state(MonitorId::Source).current()->position, 10);
        commands.action(QStringLiteral("script.interpolate_position"))->trigger();
        QVERIFY(w.slateOptions().interpolatePosition);
        const model::ScriptTake t1 = *model::takeOf(w.script(), take1);
        QVERIFY(window.loadScriptTake(t1, 4, 0.5));
        QCOMPARE(composer.state(MonitorId::Source).current()->position, model::interpolatedFrame(t1, 4, 0.5, 100));
        // The Play button plays the selected takes one after another (p.581).
        w.selectTakes({take1, take3});
        w.findChild<QToolButton*>(QStringLiteral("playTakes"))->click();
        QVERIFY(composer.isClipLooping());
        composer.stop();

        // Delete Takes (p.579-580); the clips stay in the bin. The slate goes with its last take.
        w.setAskHooks({}, [](bool, bool, int takes) {
            return takes > 0 ? std::optional<ScriptDeleteChoice>(ScriptDeleteChoice{false, false, true}) : std::nullopt;
        });
        w.selectTakes({take1});
        w.askDelete();
        QCOMPARE(w.script().slates.first().takes.size(), qsizetype(2));
        w.selectSlate(slate.id);
        QCOMPARE(w.selectedTakes().size(), qsizetype(2));
        w.askDelete();
        QVERIFY(w.script().slates.isEmpty());
        QCOMPARE(bin->bin().items.size(), qsizetype(4));
    }
};

QTEST_MAIN(TestScriptUi)
#include "tst_script_ui.moc"
