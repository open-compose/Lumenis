#include "lumenis/ext/provider.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/keyboard_palette.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/workspace.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void dropCommand(QWidget* target, const QString& commandId) {
    std::unique_ptr<QMimeData> mime(ButtonMapper::mimeFor(commandId));
    const QPointF at(target->width() / 2.0, target->height() / 2.0);
    QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDropEvent drop(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
}

QKeySequence key(const char* text) {
    return QKeySequence::fromString(QString::fromLatin1(text), QKeySequence::PortableText);
}

// A main window for a user in `users`, as after Select Project.
struct App {
    ext::ProviderRegistry providers;
    CommandRegistry commands;
    std::unique_ptr<MainWindow> window;
    App(const QString& users, const QString& user) {
        commands.loadBuiltin();
        window = std::make_unique<MainWindow>(commands, providers);
        window->resize(1200, 760);
        window->show();
        window->settings().setUser(users, user);
    }
};

} // namespace

class TestKeyboardWorkspaces : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void keyboardDifferences() {
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap map;
        map.loadBuiltin(commands);
        QVERIFY(map.differences().isEmpty());
        map.assign(key("Q"), QStringLiteral("edit.lift"));
        map.assign(key("J"), QString());
        const QJsonObject d = map.differences();
        QCOMPARE(d.value("Q").toString(), QStringLiteral("edit.lift"));
        QCOMPARE(d.value("J").toString(), QString());
        KeyboardMap other;
        other.loadBuiltin(commands);
        other.setDifferences(d);
        QCOMPARE(other.commandFor(key("Q")), QStringLiteral("edit.lift"));
        QVERIFY(other.commandFor(key("J")).isEmpty());
        QCOMPARE(other.commandFor(key("K")), QStringLiteral("play.pause"));
        other.setDifferences({});
        QCOMPARE(other.commandFor(key("J")), QStringLiteral("play.play_reverse"));
    }

    void keyboardPalette() {
        // p.1202: the keys show their commands; buttons dragged onto keys
        // map them (p.98).
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap map;
        map.loadBuiltin(commands);
        KeyboardPalette palette(map);
        QCOMPARE(palette.key("I")->command(), QStringLiteral("edit.mark_in"));
        QCOMPARE(palette.key("Space")->command(), QStringLiteral("play.play"));
        QVERIFY(palette.key("Q")->isBlank());
        QSignalSpy changed(&palette, &KeyboardPalette::keyboardChanged);
        dropCommand(palette.key("Q"), QStringLiteral("edit.lift"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(map.commandFor(key("Q")), QStringLiteral("edit.lift"));
        QCOMPARE(palette.key("Q")->command(), QStringLiteral("edit.lift"));
        // Blank unassigns a key.
        dropCommand(palette.key("J"), QString());
        QVERIFY(map.commandFor(key("J")).isEmpty());
        // The Add Alt Key button (p.97): I becomes Go to IN Point (Alt+I).
        dropCommand(palette.key("I"), QStringLiteral("other.add_alt_key"));
        QCOMPARE(map.commandFor(key("I")), QStringLiteral("edit.go_to_in_point"));
        // ... and the key's own function is kept under the modifier (p.97).
        QCOMPARE(map.commandFor(key("Alt+I")), QStringLiteral("edit.mark_in"));
        // With nothing on the modified key, the function still moves there
        // rather than being lost.
        dropCommand(palette.key("O"), QStringLiteral("other.add_alt_key"));
        QVERIFY(map.commandFor(key("O")).isEmpty());
        QCOMPARE(map.commandFor(key("Alt+O")), QStringLiteral("edit.mark_out"));
        // The Shift layer.
        palette.setShiftLayer(true);
        QVERIFY(palette.key("Q")->isBlank());
        dropCommand(palette.key("Q"), QStringLiteral("edit.extract"));
        QCOMPARE(map.commandFor(key("Shift+Q")), QStringLiteral("edit.extract"));
        QCOMPARE(map.commandFor(key("Q")), QStringLiteral("edit.lift"));
        palette.setShiftLayer(false);
        QCOMPARE(palette.key("Q")->command(), QStringLiteral("edit.lift"));
        // Map Foreign Keyboard is not available yet.
        QVERIFY(!palette.findChild<QWidget*>(QStringLiteral("keyboardForeign"))->isEnabled());
    }

    void keyboardSurvivesRestart() {
        // The Keyboard setting is a User setting (p.1202): saved with the
        // user, back after a restart.
        QTemporaryDir dir;
        {
            App app(dir.filePath("Users"), QStringLiteral("ed"));
            dropCommand(app.window->keyboardPalette().key("Q"), QStringLiteral("edit.lift"));
            QCOMPARE(app.window->keyboard().commandFor(key("Q")), QStringLiteral("edit.lift"));
            // Opening the Keyboard setting opens its palette.
            app.window->setProject([&] {
                model::ProjectEntry e;
                e.folder = dir.filePath("Project");
                e.settings.name = QStringLiteral("Film");
                return e;
            }());
            SettingsWindow* sw = app.window->showSettings();
            SettingsList* list = sw->list(model::SettingsScope::User);
            list->openEntry(app.window->settings().file(model::SettingsScope::User).activeIndex("Keyboard"));
            QVERIFY(app.window->panels().isOpen(app.window->panels().panel(QStringLiteral("Keyboard"))));
            QVERIFY(app.window->panels().panel(QStringLiteral("Keyboard"))->title().startsWith(QStringLiteral("Keyboard - ")));
        }
        App again(dir.filePath("Users"), QStringLiteral("ed"));
        QCOMPARE(again.window->keyboard().commandFor(key("Q")), QStringLiteral("edit.lift"));
        QCOMPARE(again.window->keyboardPalette().key("Q")->command(), QStringLiteral("edit.lift"));
        // Another user has the default keyboard.
        App other(dir.filePath("Users"), QStringLiteral("jo"));
        QVERIFY(other.window->keyboard().commandFor(key("Q")).isEmpty());
    }

    void workspacesSurviveRestart() {
        // Gate 4: workspaces and layouts save, restore and survive a
        // restart. They are User settings (p.86), one Workspace View each
        // (picture on p.1136).
        QTemporaryDir dir;
        QJsonObject logging;
        {
            App app(dir.filePath("Users"), QStringLiteral("ed"));
            WorkspaceManager& ws = app.window->workspaces();
            QVERIFY(ws.create(QStringLiteral("Logging")));
            PanelManager& panels = app.window->panels();
            panels.floatPanel(panels.panel(QStringLiteral("Composer")), QRect(100, 100, 400, 300));
            ws.saveCurrent();
            logging = panels.saveLayout();
            const model::SettingsFile& f = app.window->settings().file(model::SettingsScope::User);
            QStringList views;
            for (int i : f.versions("Workspace View")) views << f.entries.at(i).name;
            QCOMPARE(views, ws.names());
            QCOMPARE(f.entries.at(f.activeIndex("Workspace View")).name, QStringLiteral("Logging"));
            ws.activate(QStringLiteral("Edit"));
            ws.activate(QStringLiteral("Logging"));
            app.window->close(); // where it was left is saved
        }
        App again(dir.filePath("Users"), QStringLiteral("ed"));
        WorkspaceManager& ws = again.window->workspaces();
        QVERIFY(ws.names().contains(QStringLiteral("Logging")));
        QCOMPARE(ws.active(), QStringLiteral("Logging"));
        QVERIFY(again.window->panels().isFloating(again.window->panels().panel(QStringLiteral("Composer"))));
        // Choosing another Workspace View in the Settings list switches.
        model::SettingsFile& f = again.window->settings().file(model::SettingsScope::User);
        for (int i : f.versions("Workspace View")) {
            if (f.entries.at(i).name == QLatin1String("Edit")) f.setActive(i);
        }
        again.window->settings().commit(model::SettingsScope::User);
        QTRY_COMPARE(ws.active(), QStringLiteral("Edit"));
        QVERIFY(!again.window->panels().isFloating(again.window->panels().panel(QStringLiteral("Composer"))));

        // Each user has their own workspaces.
        App other(dir.filePath("Users"), QStringLiteral("jo"));
        QVERIFY(!other.window->workspaces().names().contains(QStringLiteral("Logging")));
    }

    void linkToNamedSettings() {
        // p.89-90: a workspace linked to settings named like it makes them
        // active when it opens.
        QTemporaryDir dir;
        App app(dir.filePath("Users"), QStringLiteral("ed"));
        Settings& settings = app.window->settings();
        WorkspaceManager& ws = app.window->workspaces();
        QVERIFY(ws.create(QStringLiteral("Logging")));
        model::SettingsFile& f = settings.file(model::SettingsScope::User);
        const int timeline = f.activeIndex("Timeline");
        const int named = f.duplicate(timeline);
        f.entries[named].name = QStringLiteral("Logging");
        f.entries[named].values.insert(QStringLiteral("showToolbar"), false);
        for (int i : f.versions("Workspace View")) {
            if (f.entries.at(i).name == QLatin1String("Logging")) {
                f.entries[i].values.insert(QStringLiteral("linkNamed"), true);
                f.entries[i].values.insert(QStringLiteral("linkName"), QStringLiteral("Logging"));
            }
        }
        settings.commit(model::SettingsScope::User);
        QVERIFY(settings.flag("Timeline", "showToolbar"));
        ws.activate(QStringLiteral("Edit"));
        ws.activate(QStringLiteral("Logging"));
        QVERIFY(!settings.flag("Timeline", "showToolbar"));
        const model::SettingsFile& g = settings.file(model::SettingsScope::User);
        QCOMPARE(g.entries.at(g.activeIndex("Timeline")).name, QStringLiteral("Logging"));
    }

    void workspaceButtons() {
        // p.90-91: W1-W12 switch to their workspaces; the Workspaces tab's
        // menus assign them.
        QTemporaryDir dir;
        App app(dir.filePath("Users"), QStringLiteral("ed"));
        QVERIFY(app.window->workspaces().create(QStringLiteral("Logging")));
        CommandPalette& palette = app.window->commandPalette();
        QComboBox* w5 = palette.workspaceMenu(4);
        QVERIFY(w5);
        QCOMPARE(w5->itemText(0), QStringLiteral("None"));
        QVERIFY(w5->findText(QStringLiteral("Logging")) > 0);
        w5->setCurrentIndex(w5->findText(QStringLiteral("Logging")));
        QCOMPARE(app.window->workspaceButtons().value(4), QStringLiteral("Logging"));
        app.window->workspaces().activate(QStringLiteral("Edit"));
        app.commands.action(QStringLiteral("workspace.w5"))->trigger();
        QCOMPARE(app.window->workspaces().active(), QStringLiteral("Logging"));
        // W1 is Edit by default (ours).
        app.commands.action(QStringLiteral("workspace.w1"))->trigger();
        QCOMPARE(app.window->workspaces().active(), QStringLiteral("Edit"));
        // A workspace button mapped onto a key switches workspaces too.
        dropCommand(app.window->keyboardPalette().key("F5"), QStringLiteral("workspace.w5"));
        QCOMPARE(app.window->keyboard().commandFor(key("F5")), QStringLiteral("workspace.w5"));
        QVERIFY(!app.window->keyboardPalette().key("F5")->icon().isNull());
    }
};

QTEST_MAIN(TestKeyboardWorkspaces)
#include "tst_keyboard_workspaces.moc"
