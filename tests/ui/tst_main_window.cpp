#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/workspace.h"

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QMenuBar>
#include <QToolButton>

#include <functional>
#include <memory>
#include <QSignalSpy>
#include <QTest>

using namespace lumenis;

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void projectInTitleAndCloseProject() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        QAction* close = commands.action(QStringLiteral("file.close_project"));
        QVERIFY(close);
        QVERIFY(!close->isEnabled()); // no project yet

        model::ProjectEntry entry;
        entry.settings.name = QStringLiteral("Feature Film");
        window.setProject(entry);
        QVERIFY(window.windowTitle().contains(QStringLiteral("Feature Film")));
        QVERIFY(close->isEnabled());
        QCOMPARE(close->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+W")));

        QSignalSpy requested(&window, &ui::MainWindow::closeProjectRequested);
        close->trigger();
        QCOMPARE(requested.count(), 1);

        window.clearProject();
        QCOMPARE(window.windowTitle(), QString::fromUtf8(branding::kProductName));
        QVERIFY(!close->isEnabled());
    }

    // Guide p.74: Bin Container top left, Composer beside it, Timeline at
    // the bottom, workspace toolbar on the right.
    void opensWithTheDefaultPanels() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.resize(1280, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::PanelManager& panels = window.panels();
        QStringList ids;
        for (const ui::PanelGroup* g : panels.mainHost()->groups()) ids << g->current()->id();
        QCOMPARE(ids, (QStringList{"BinContainer", "Composer", "Timeline"}));
        auto rect = [&](const char* id) {
            const QWidget* g = panels.panel(QString::fromLatin1(id))->group();
            return QRect(g->mapTo(&window, QPoint()), g->size());
        };
        QVERIFY(rect("BinContainer").right() < rect("Composer").left());
        QVERIFY(rect("Composer").bottom() < rect("Timeline").top());
        auto* bar = window.findChild<QWidget*>(QStringLiteral("workspaceBar"));
        QVERIFY(bar && bar->isVisible());
        QVERIFY(bar->geometry().left() >= rect("Timeline").right());
        // Same structure; sizes are pixels once laid out.
        std::function<QJsonObject(QJsonObject)> structure = [&](QJsonObject node) {
            node.remove(QStringLiteral("sizes"));
            QJsonArray children;
            for (const QJsonValue& c : node.value(QStringLiteral("children")).toArray()) {
                children.append(structure(c.toObject()));
            }
            if (!children.isEmpty()) node.insert(QStringLiteral("children"), children);
            return node;
        };
        QCOMPARE(structure(panels.saveLayout().value(QStringLiteral("main")).toObject()),
                 structure(ui::MainWindow::defaultLayout().value(QStringLiteral("main")).toObject()));
    }

    void windowsMenuCommands() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        ui::PanelManager& panels = window.panels();
        auto run = [&](const char* id) {
            QAction* a = commands.action(QString::fromLatin1(id));
            QVERIFY2(a && a->isEnabled(), id);
            a->trigger();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };

        // File > Close closes the active window; Tools > Timeline reopens it.
        panels.activate(panels.panel(QStringLiteral("Timeline")));
        run("file.close");
        QVERIFY(!panels.isOpen(panels.panel(QStringLiteral("Timeline"))));
        run("tools.timeline");
        QVERIFY(panels.isOpen(panels.panel(QStringLiteral("Timeline"))));

        run("windows.add_blank_panel");
        QVERIFY(panels.activePanel()->isBlank());
        run("windows.float_active_panel");
        QCOMPARE(panels.floatingWindows().size(), 2); // the reopened Timeline and the blank panel
        run("windows.float_all_panels");
        QVERIFY(panels.mainHost()->isEmpty());

        // Workspaces: the list in Windows > Workspaces, and the commands.
        auto* menu = window.menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_workspaces"));
        QVERIFY(menu);
        QStringList listed;
        for (QAction* a : menu->actions()) {
            if (a->property(ui::kDynamicMenuItem).toBool() && !a->isSeparator()) listed << a->text();
        }
        QCOMPARE(listed, (QStringList{"Edit", "Color", "Effects", "Audio"}));
        QVERIFY(!commands.action(QStringLiteral("windows.workspaces.delete_workspace"))->isEnabled());
        run("windows.workspaces.restore_to_saved");
        QCOMPARE(panels.mainHost()->groups().size(), 3);
        QVERIFY(panels.floatingWindows().isEmpty());

        // Hiding the toolbar shows the workspace button instead (p.88).
        QAction* showBar = commands.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"));
        QVERIFY(showBar->isCheckable() && showBar->isChecked());
        auto* button = window.findChild<QToolButton*>(QStringLiteral("workspaceButton"));
        QVERIFY(button && button->isHidden());
        showBar->trigger();
        QCOMPARE(window.workspaces().barMode(), ui::WorkspaceManager::BarMode::Hidden);
        QVERIFY(window.findChild<QWidget*>(QStringLiteral("workspaceBar"))->isHidden());
        QVERIFY(button->isVisible());
        QVERIFY(!button->menu()->actions().isEmpty());
        showBar->trigger();
        QVERIFY(button->isHidden());

        // The workspace list changes with the workspaces.
        window.workspaces().create(QStringLiteral("Logging"));
        QVERIFY(commands.action(QStringLiteral("windows.workspaces.delete_workspace"))->isEnabled());
        int dynamic = 0;
        for (QAction* a : menu->actions()) dynamic += a->property(ui::kDynamicMenuItem).toBool() ? 1 : 0;
        QCOMPARE(dynamic, 6); // five workspaces and a separator
        // ...but they are not part of the fixed inventory.
        const QByteArray dumped = QJsonDocument(ui::dumpMenus(window.menuBar())).toJson();
        QVERIFY(!dumped.contains("Logging"));
    }

    void aboutBoxShowsThePicture() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        std::unique_ptr<QMessageBox> box(window.createAboutBox());
        QVERIFY(box->text().contains(QString::fromUtf8(branding::kProductName)));
        const QPixmap picture = box->iconPixmap();
        QVERIFY(!picture.isNull());
        QCOMPARE(picture.deviceIndependentSize().toSize(),
                 QSize(ui::MainWindow::kAboutPictureSize, ui::MainWindow::kAboutPictureSize));
        QVERIFY(commands.action(QStringLiteral("help.about"))->isEnabled());
    }

    void closingTheWindowIsReported() {
        ext::ProviderRegistry providers;
        ui::CommandRegistry commands;
        commands.loadBuiltin();
        ui::MainWindow window(commands, providers);
        window.show();
        QSignalSpy closed(&window, &ui::MainWindow::closed);
        window.close();
        QCOMPARE(closed.count(), 1);
    }
};

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
