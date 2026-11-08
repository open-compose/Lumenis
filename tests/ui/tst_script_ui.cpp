// The Script window in the application (8.3a; Editing Guide p.570-576): File > New > New Script importing a text file
// into the active bin, the window showing it, Add Scene and Add Page, the Scene and Page menus and status bar, the
// Delete dialog, Edit Mode with the numbers following the lines, Find and Replace, Text Layout, Save Script into the
// bin item, Save a Script Copy As, reopening by double-click.
#include "lumenis/model/script.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/script_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
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
        }, [](bool, bool) { return std::optional<std::pair<bool, bool>>(std::pair{true, false}); });
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
};

QTEST_MAIN(TestScriptUi)
#include "tst_script_ui.moc"
