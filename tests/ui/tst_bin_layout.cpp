// Windows > Bin Layout (Editing Guide p.91-93; docs/spec/bin-layout.md): New Bin Layout, opening a
// layout from the menu, Save Current, Delete Bin Layout, the Project setting, and a layout linked to a
// workspace.
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QDir>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QStringList openIds(BinContainer& bins) {
    QStringList out;
    for (BinDocument* d : bins.openDocuments()) out << d->nodeId();
    out.sort();
    return out;
}

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

} // namespace

class TestBinLayout : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void binLayouts() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1100, 700);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        BinContainer& bins = window.binContainer();
        const QString a = bins.newBin();
        const QString b = bins.newBin();
        const QString c = bins.newBin();
        bins.openBin(a);
        bins.openBin(b);
        // b floats in a window of its own.
        bins.floatBin(bins.document(b), QRect(60, 60, 400, 300));
        bins.closeBin(bins.document(c));
        settle();

        // New Bin Layout: named in its dialog, then in the menu and the Settings list (p.93).
        window.setBinLayoutHook([](const QStringList&) { return std::optional<QString>(QStringLiteral("Logging")); });
        commands.action(QStringLiteral("windows.bin_layout.new_bin_layout"))->trigger();
        QCOMPARE(window.binLayouts(), QStringList{QStringLiteral("Logging")});
        QCOMPARE(window.currentBinLayout(), QStringLiteral("Logging"));
        const QJsonObject saved = window.settings().file(model::SettingsScope::Project)
                                      .activeValues(QStringLiteral("Bin Layout"))
                                      .value(QStringLiteral("layout"))
                                      .toObject();
        QCOMPARE(saved.value(QStringLiteral("main")).toArray().size(), 1);
        QCOMPARE(saved.value(QStringLiteral("floating")).toArray().size(), 1);

        // Rearranged: only c open. Opening the layout brings back a and b, b floating (p.92).
        bins.closeBin(bins.document(a));
        bins.closeBin(bins.document(b));
        bins.openBin(c);
        settle();
        QCOMPARE(openIds(bins), QStringList{c});
        QMenu* menu = window.menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_bin_layout"));
        QVERIFY(menu);
        emit menu->aboutToShow();
        QAction* entry = nullptr;
        for (QAction* act : menu->actions()) {
            if (act->text() == QLatin1String("Logging")) entry = act;
        }
        QVERIFY(entry);
        entry->trigger();
        settle();
        QStringList ab{a, b};
        ab.sort();
        QCOMPARE(openIds(bins), ab);
        QCOMPARE(bins.floatingContainers().size(), 1);
        QCOMPARE(bins.floatingContainers().first()->geometry().size(), QSize(400, 300));

        // Save Current: the layout now has c too.
        bins.openBin(c);
        settle();
        QVERIFY(window.saveCurrentBinLayout());
        bins.closeBin(bins.document(c));
        settle();
        QVERIFY(window.openBinLayout(QStringLiteral("Logging")));
        settle();
        QCOMPARE(openIds(bins).size(), 3);

        // Linked to a workspace (p.91): activating the workspace opens the layout.
        model::SettingsFile& user = window.settings().file(model::SettingsScope::User);
        QJsonObject view{{QStringLiteral("binLayout"), QStringLiteral("Logging")}};
        for (int i : user.versions(QStringLiteral("Workspace View"))) {
            if (user.entries.at(i).name == QLatin1String("Edit")) user.entries[i].values = view;
        }
        if (user.versions(QStringLiteral("Workspace View")).isEmpty() ||
            std::none_of(user.entries.begin(), user.entries.end(), [](const model::SettingEntry& e) {
                return e.kind == QLatin1String("Workspace View") && e.name == QLatin1String("Edit");
            })) {
            user.add(model::SettingEntry{QStringLiteral("Workspace View"), QStringLiteral("Edit"), false, view});
        }
        bins.closeBin(bins.document(a));
        settle();
        window.linkSettings(QStringLiteral("Edit"));
        settle();
        QVERIFY(openIds(bins).contains(a));

        // Delete Bin Layout (p.93).
        window.setBinLayoutHook([](const QStringList& choices) { return std::optional<QString>(choices.value(0)); });
        commands.action(QStringLiteral("windows.bin_layout.delete_bin_layout"))->trigger();
        QVERIFY(window.binLayouts().isEmpty());
        QVERIFY(!window.saveCurrentBinLayout());
    }
};

QTEST_MAIN(TestBinLayout)
#include "tst_bin_layout.moc"
