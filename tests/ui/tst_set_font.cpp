// Windows > Set Font (Editing Guide p.81-82; docs/spec/set-font.md): the active Bin's font stored in
// the bin, the Composer's clip name font as a Composer setting, the Timeline's clip text font in its
// display (a Timeline View setting). Also Help > Shortcuts (p.97).
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/set_font_dialog.h"
#include "lumenis/ui/shortcuts_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFontComboBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QTableWidget>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;

class TestSetFont : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void setFont() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString family = QApplication::font().family();
        int asked = 0;
        window.setFontHook([&](SetFontDialog& d) {
            ++asked;
            d.sizeBox()->setValue(17);
            return true;
        });
        PanelManager& panels = window.panels();

        // Bin: kept in the bin and shown by its views.
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* doc = bins.openBin(binId);
        panels.activate(panels.panel(QStringLiteral("BinContainer")));
        QVERIFY(window.chooseFont());
        QCOMPARE(doc->bin().display.fontSize, 17);
        QVERIFY(!doc->bin().display.fontFamily.isEmpty());
        BinView* view = bins.viewFor(doc);
        QCOMPARE(view->findChild<QAbstractItemView*>(QStringLiteral("binTable"))->font().pointSize(), 17);
        QVERIFY(doc->save());
        const auto reread = model::BinFile::read(doc->path());
        QVERIFY(reread);
        QCOMPARE(reread->display.fontSize, 17);

        // Composer: the clip name font, saved as a Composer setting.
        Panel* composer = panels.panel(QStringLiteral("Composer"));
        panels.showPanel(composer);
        panels.activate(composer);
        QVERIFY(window.chooseFont());
        QCOMPARE(window.composer().clipNameButton(MonitorId::Source)->font().pointSize(), 17);
        QCOMPARE(window.settings().value(QStringLiteral("Composer"), QStringLiteral("fontSize")).toInt(), 17);

        // Timeline: the clip text font, kept with the display a Timeline View saves.
        Panel* timeline = panels.panel(QStringLiteral("Timeline"));
        panels.showPanel(timeline);
        panels.activate(timeline);
        commands.action(QStringLiteral("windows.set_font"))->trigger();
        QCOMPARE(asked, 3);
        QCOMPARE(window.timeline().display().clipFontSize, 17);
        QCOMPARE(window.timeline().clipFont().pointSize(), 17);
        const QJsonObject saved = Timeline::displayToJson(window.timeline().display());
        QCOMPARE(Timeline::displayFromJson(saved).clipFontSize, 17);
        Q_UNUSED(family);
    }

    // Help > Shortcuts: every command with a shortcut, with its keys; the Find box filters.
    void shortcuts() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        commands.action(QStringLiteral("help.shortcuts"))->trigger();
        ShortcutsWindow* w = window.shortcutsWindow();
        QVERIFY(w && w->isVisible());
        const int all = w->table()->rowCount();
        QVERIFY(all > 20);
        // Tape Capture's menu shortcut (Ctrl+7, ks2009).
        w->filter()->setText(QStringLiteral("Tape Capture"));
        QCOMPARE(w->table()->rowCount(), 1);
        QCOMPARE(w->table()->item(0, 1)->text(), QKeySequence(QStringLiteral("Ctrl+7")).toString(QKeySequence::NativeText));
        w->filter()->clear();
        QCOMPARE(w->table()->rowCount(), all);
    }
};

QTEST_MAIN(TestSetFont)
#include "tst_set_font.moc"
