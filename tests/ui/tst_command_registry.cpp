#include "lumenis/ui/command_registry.h"

#include <QAction>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using lumenis::ui::CommandRegistry;

namespace {

const QByteArray kCommands = R"({"commands": [
    {"id": "a.one", "text": "&One", "shortcut": "Ctrl+1"},
    {"id": "a.two", "text": "Two %PRODUCT%"}
]})";

} // namespace

class TestCommandRegistry : public QObject {
    Q_OBJECT

private slots:
    void loadsDeclarations() {
        CommandRegistry r;
        QVERIFY(r.load(kCommands));
        QCOMPARE(r.ids(), (QStringList{"a.one", "a.two"}));
        QCOMPARE(r.action("a.one")->text(), QStringLiteral("&One"));
        QVERIFY(!r.action("a.two")->text().contains(QStringLiteral("%PRODUCT%")));
        QCOMPARE(r.shortcut("a.one"), QKeySequence(Qt::CTRL | Qt::Key_1));
        QVERIFY(!r.action("nope"));
    }

    void rejectsBadInput() {
        CommandRegistry r;
        QString error;
        QVERIFY(!r.load("not json", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!r.load(R"({"commands": [{"id": "x", "text": "X"}, {"id": "x", "text": "Y"}]})", &error));
        QVERIFY(error.contains(QStringLiteral("duplicate")));
        // All or nothing: the commands before the error are not registered.
        QVERIFY(!r.contains(QStringLiteral("x")));
        QVERIFY(r.ids().isEmpty());
        // Ids are QSettings keys of saveShortcuts(): '/' would make a group
        // that loadShortcuts() never reads.
        QVERIFY(!r.load(R"({"commands": [{"id": "a", "text": "A"}, {"id": "file/open", "text": "Open"}]})", &error));
        QVERIFY(error.contains(QStringLiteral("file/open")));
        QVERIFY(r.ids().isEmpty());
        QVERIFY(r.load(R"({"commands": [{"id": "a", "text": "A"}]})", &error));
        // Against those already registered too.
        QVERIFY(!r.load(R"({"commands": [{"id": "b", "text": "B"}, {"id": "a", "text": "A"}]})", &error));
        QCOMPARE(r.ids(), QStringList{QStringLiteral("a")});
    }

    void disabledUntilHandled() {
        CommandRegistry r;
        QVERIFY(r.load(kCommands));
        QVERIFY(!r.action("a.one")->isEnabled());

        int calls = 0;
        r.setHandler("a.one", [&] { ++calls; });
        QVERIFY(r.action("a.one")->isEnabled());
        r.action("a.one")->trigger();
        QCOMPARE(calls, 1);

        r.setHandler("a.one", {});
        QVERIFY(!r.action("a.one")->isEnabled());
    }

    void savesOnlyChangedShortcuts() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);

        CommandRegistry r;
        QVERIFY(r.load(kCommands));
        r.setShortcut("a.two", QKeySequence(Qt::Key_F5));
        r.saveShortcuts(settings);
        settings.sync();
        QCOMPARE(settings.allKeys(), QStringList{"keyboard/a.two"});

        CommandRegistry fresh;
        QVERIFY(fresh.load(kCommands));
        fresh.loadShortcuts(settings);
        QCOMPARE(fresh.shortcut("a.two"), QKeySequence(Qt::Key_F5));
        QCOMPARE(fresh.shortcut("a.one"), fresh.defaultShortcut("a.one"));

        fresh.resetShortcuts();
        QVERIFY(fresh.shortcut("a.two").isEmpty());
    }

    void builtinDeclarationsLoad() {
        CommandRegistry r;
        r.loadBuiltin(); // qFatal on failure
        QVERIFY(r.contains("file.exit"));
    }
};

QTEST_MAIN(TestCommandRegistry)
#include "tst_command_registry.moc"
