// The editing keyboard map, and the check that its defaults match the keys
// the Media Composer documentation states (tests/data/keyboard_inventory.json).

#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/keyboard_map.h"

#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::ui;

class TestKeyboard : public QObject {
    Q_OBJECT

private slots:
    void defaultsMatchTheInventory() {
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap map;
        map.loadBuiltin(commands);

        QFile file(QStringLiteral(KEYBOARD_INVENTORY));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonArray expected = QJsonDocument::fromJson(file.readAll()).object().value("keys").toArray();
        QVERIFY(!expected.isEmpty());

        QStringList problems;
        QSet<QString> expectedKeys;
        for (const QJsonValue& v : expected) {
            const QJsonObject o = v.toObject();
            const QKeySequence key = QKeySequence::fromString(o.value("key").toString(), QKeySequence::PortableText);
            expectedKeys.insert(key.toString(QKeySequence::PortableText));
            const QString id = map.commandFor(key);
            const QString text = id.isEmpty() ? QString() : commands.action(id)->text();
            if (text != o.value("command").toString()) {
                problems << QStringLiteral("%1: expected '%2', got '%3'")
                                .arg(o.value("key").toString(), o.value("command").toString(), text);
            }
        }
        for (const QKeySequence& k : map.keys()) {
            if (!expectedKeys.contains(k.toString(QKeySequence::PortableText))) {
                problems << QStringLiteral("%1 is mapped but not in the inventory").arg(k.toString());
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("\n"))));
        QCOMPARE(map.keysFor("play.play").size(), 4);
    }

    void rejectsBadDocuments() {
        CommandRegistry commands;
        QVERIFY(commands.load(R"({"commands":[{"id":"x.go","text":"Go"}]})"));
        KeyboardMap map;
        QString error;
        QVERIFY(map.load(R"({"keys":[{"key":"G","command":"x.go"}]})", commands, &error));
        QVERIFY(!map.load(R"({"keys":[{"key":"G","command":"nope"}]})", commands, &error));
        QVERIFY(error.contains("unknown command"));
        QVERIFY(!map.load(R"({"keys":[{"key":"G","command":"x.go"},{"key":"G","command":"x.go"}]})", commands, &error));
        QVERIFY(error.contains("twice"));
    }

    void savesOnlyDifferences() {
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap map;
        map.loadBuiltin(commands);
        QVERIFY(map.isDefault());

        map.assign(QKeySequence(Qt::Key_F5), QStringLiteral("edit.mark_in"));
        map.assign(QKeySequence(Qt::Key_B), QString()); // unassign Overwrite
        QTemporaryDir dir;
        QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
        map.save(settings);
        settings.sync();
        QCOMPARE(settings.allKeys().size(), 2);

        KeyboardMap fresh;
        fresh.loadBuiltin(commands);
        fresh.restore(settings);
        QCOMPARE(fresh.commandFor(QKeySequence(Qt::Key_F5)), QStringLiteral("edit.mark_in"));
        QVERIFY(fresh.commandFor(QKeySequence(Qt::Key_B)).isEmpty());
        QCOMPARE(fresh.commandFor(QKeySequence(Qt::Key_I)), QStringLiteral("edit.mark_in"));

        fresh.resetToDefaults();
        QVERIFY(fresh.isDefault());
    }
};

QTEST_MAIN(TestKeyboard)
#include "tst_keyboard.moc"
