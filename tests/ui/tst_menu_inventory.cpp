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

        QStringList expected;
        QStringList actual;
        flatten(expectedMenus, {}, expected);
        flatten(actualMenus, {}, actual);

        QStringList missing;
        QStringList extra;
        for (const QString& e : expected) {
            if (!actual.contains(e)) missing << e;
        }
        for (const QString& a : actual) {
            if (!expected.contains(a)) extra << a;
        }
        if (!missing.isEmpty() || !extra.isEmpty()) {
            qWarning().noquote() << "Missing from the application:\n  " + missing.join(QStringLiteral("\n  "));
            qWarning().noquote() << "Not in the inventory:\n  " + extra.join(QStringLiteral("\n  "));
        }
        QVERIFY(missing.isEmpty());
        QVERIFY(extra.isEmpty());
        // Same entries in the same order.
        QCOMPARE(actual, expected);

        qInfo("%lld menu entries; %d not yet implemented (disabled)",
              static_cast<long long>(actual.size()), countDisabled(actualMenus));
    }
};

QTEST_MAIN(TestMenuInventory)
#include "tst_menu_inventory.moc"
