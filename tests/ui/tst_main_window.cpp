#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"

#include <QAction>
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
