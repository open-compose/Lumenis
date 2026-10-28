// File > Media > Unmount (Editing Guide p.343-344; docs/spec/media-unmount.md), File > Input > Link
// (p.328) and Windows > Center Current / Send Current Home (p.601).
#include "lumenis/ext/provider.h"
#include "lumenis/media/managed.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/unmount_dialog.h"

#include <QAction>
#include <QDir>
#include <QListWidget>
#include <QScreen>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool hasDrive(const QString& root) {
    for (const media::MediaDrive& d : media::mediaDrives()) {
        if (d.root == QDir::cleanPath(root)) return true;
    }
    return false;
}

} // namespace

class TestMediaUnmount : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void unmount() {
        QTemporaryDir dir;
        const QString a = dir.filePath("DriveA");
        const QString b = dir.filePath("DriveB");
        QDir().mkpath(a);
        QDir().mkpath(b);
        media::setExtraMediaDrives({a, b});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        QVERIFY(hasDrive(a) && hasDrive(b));
        // "The list displays all drives currently available. ... Select a drive to unmount ... Click
        // Unmount. The drives are no longer available" (p.344).
        window.setUnmountHook([&](UnmountDialog& d) {
            bool listed = false;
            for (int i = 0; i < d.list()->count(); ++i) {
                QListWidgetItem* item = d.list()->item(i);
                if (item->data(Qt::UserRole).toString() == QDir::cleanPath(a)) {
                    item->setSelected(true);
                    listed = true;
                }
            }
            return listed;
        });
        commands.action(QStringLiteral("file.media.unmount"))->trigger();
        QVERIFY(!hasDrive(a));
        QVERIFY(hasDrive(b));
        QCOMPARE(media::unmountedDrives(), QStringList{QDir::cleanPath(a)});
        // Mount All (ours) brings them back.
        window.setUnmountHook([](UnmountDialog& d) {
            auto* mountAll = d.findChild<QPushButton*>(QStringLiteral("mountAll"));
            if (!mountAll || !mountAll->isEnabled()) return false;
            mountAll->click();
            return d.mountAll();
        });
        window.unmountDrives();
        QVERIFY(hasDrive(a));
        QVERIFY(media::unmountedDrives().isEmpty());
        // Cancel changes nothing.
        window.setUnmountHook([](UnmountDialog& d) {
            d.list()->selectAll();
            return false;
        });
        window.unmountDrives();
        QVERIFY(hasDrive(a) && hasDrive(b));
        media::setExtraMediaDrives({});
    }

    // File > Input > Link opens the Source Browser to link (p.315, p.328), Import Media to import.
    void inputLink() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        commands.action(QStringLiteral("file.input.import_media"))->trigger();
        auto* browser = window.findChild<SourceBrowser*>();
        QVERIFY(browser);
        QVERIFY(browser->isImportMode());
        commands.action(QStringLiteral("file.input.link"))->trigger();
        QVERIFY(!browser->isImportMode());
    }

    // Center Current and Send Current Home (p.601) move the active panel's floating window.
    void placeWindows() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        window.resize(1000, 700);
        window.show();
        PanelManager& panels = window.panels();
        Panel* timeline = panels.panel(QStringLiteral("Timeline"));
        QVERIFY(timeline);
        panels.showPanel(timeline);
        panels.activate(timeline);
        // Docked: nothing to move.
        if (!panels.isFloating(timeline)) QVERIFY(!window.placeCurrentWindow(false));
        panels.floatPanel(timeline, QRect(5, 5, 300, 200));
        panels.activate(timeline);
        QVERIFY(panels.isFloating(timeline));
        QWidget* w = timeline->window();
        QVERIFY(window.placeCurrentWindow(false));
        const QPoint screenCenter = w->screen()->availableGeometry().center();
        QVERIFY((w->frameGeometry().center() - screenCenter).manhattanLength() <= 2);
        commands.action(QStringLiteral("windows.send_current_home"))->trigger();
        QVERIFY((w->frameGeometry().center() - window.frameGeometry().center()).manhattanLength() <= 2);
    }
};

QTEST_MAIN(TestMediaUnmount)
#include "tst_media_unmount.moc"
