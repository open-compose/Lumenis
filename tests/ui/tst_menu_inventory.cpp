// Phase 1 gate 2: the menu bar must match the inventory taken from the
// Media Composer documentation (tests/data/menu_inventory.json), item by
// item, including separators and default shortcuts.

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMenuBar>
#include <QTest>

namespace {

// One line per entry: "File > Exit [Ctrl+Q]", "File > ----", ...
void flatten(const QJsonArray& items, const QString& prefix, QStringList& out) {
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("separator")).toBool()) {
            out << prefix + QStringLiteral("----");
            continue;
        }
        const QString text = o.value(QStringLiteral("text")).toString();
        if (o.contains(QStringLiteral("items"))) {
            out << prefix + text + QStringLiteral(" >");
            flatten(o.value(QStringLiteral("items")).toArray(), prefix + text + QStringLiteral(" > "), out);
        } else {
            const QString keys = o.value(QStringLiteral("shortcut")).toString();
            out << prefix + text + (keys.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(keys));
        }
    }
}

int countDisabled(const QJsonArray& items) {
    int n = 0;
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("items"))) {
            n += countDisabled(o.value(QStringLiteral("items")).toArray());
        } else if (o.contains(QStringLiteral("enabled")) && !o.value(QStringLiteral("enabled")).toBool()) {
            ++n;
        }
    }
    return n;
}

} // namespace

class TestMenuInventory : public QObject {
    Q_OBJECT

private slots:
    void menuBarMatchesInventory() {
        QFile file(QStringLiteral(MENU_INVENTORY));
        QVERIFY2(file.open(QIODevice::ReadOnly), MENU_INVENTORY);
        // The inventory says %PRODUCT% where the product name appears.
        const QByteArray spec = file.readAll().replace("%PRODUCT%", lumenis::branding::kProductName);
        const QJsonArray expectedMenus =
            QJsonDocument::fromJson(spec).object().value(QStringLiteral("menus")).toArray();
        QVERIFY(!expectedMenus.isEmpty());

        lumenis::ext::ProviderRegistry providers;
        lumenis::ui::CommandRegistry commands;
        commands.loadBuiltin();
        lumenis::ui::MainWindow window(commands, providers);
        const QJsonArray actualMenus = lumenis::ui::dumpMenus(window.menuBar());

        QHash<QString, QJsonObject> actualByTitle;
        QStringList actualTitles;
        for (const QJsonValue& v : actualMenus) {
            const QJsonObject o = v.toObject();
            actualTitles << o.value(QStringLiteral("text")).toString();
            actualByTitle.insert(actualTitles.last(), o);
        }

        // Menu bar: every inventory menu exists, nothing else does, and the
        // menus whose position is confirmed appear in the confirmed order.
        QStringList expectedTitles;
        QStringList positioned;
        for (const QJsonValue& v : expectedMenus) {
            const QJsonObject o = v.toObject();
            expectedTitles << o.value(QStringLiteral("text")).toString();
            if (o.value(QStringLiteral("positionVerified")).toBool()) {
                positioned << expectedTitles.last();
            }
        }
        QStringList sortedExpected = expectedTitles;
        QStringList sortedActual = actualTitles;
        sortedExpected.sort();
        sortedActual.sort();
        QCOMPARE(sortedActual, sortedExpected);
        QStringList actualPositioned;
        for (const QString& t : actualTitles) {
            if (positioned.contains(t)) actualPositioned << t;
        }
        QCOMPARE(actualPositioned, positioned);

        int entries = 0;
        int disabled = 0;
        for (const QJsonValue& v : expectedMenus) {
            const QJsonObject menu = v.toObject();
            const QString title = menu.value(QStringLiteral("text")).toString();
            QJsonArray expectedItems = menu.value(QStringLiteral("items")).toArray();
            for (const QJsonValue& a : menu.value(QStringLiteral("additions")).toArray()) {
                expectedItems.append(QJsonObject{{QStringLiteral("text"), a.toObject().value(QStringLiteral("text"))}});
            }
            const QJsonArray actualItems = actualByTitle.value(title).value(QStringLiteral("items")).toArray();

            QStringList expected;
            QStringList actual;
            flatten(expectedItems, title + QStringLiteral(" > "), expected);
            flatten(actualItems, title + QStringLiteral(" > "), actual);
            entries += static_cast<int>(actual.size());
            disabled += countDisabled(actualItems);

            QStringList missing;
            QStringList extra;
            for (const QString& e : expected) {
                if (!actual.contains(e)) missing << e;
            }
            for (const QString& a : actual) {
                if (!expected.contains(a)) extra << a;
            }
            if (!missing.isEmpty()) {
                qWarning().noquote() << "Missing from the application:\n  " + missing.join(QStringLiteral("\n  "));
            }
            if (!extra.isEmpty()) {
                qWarning().noquote() << "Not in the inventory:\n  " + extra.join(QStringLiteral("\n  "));
            }
            QVERIFY2(missing.isEmpty() && extra.isEmpty(), qPrintable(title));
            if (menu.value(QStringLiteral("orderVerified")).toBool()) {
                QCOMPARE(actual, expected);
            }
        }
        qInfo("%d menu entries; %d not yet implemented (disabled)", entries, disabled);
    }
};

QTEST_MAIN(TestMenuInventory)
#include "tst_menu_inventory.moc"
