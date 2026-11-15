// The Interface settings' colours and fonts (Editing Guide p.1201-1202; docs/spec/interface-colors.md):
// the Settings dialog's colour and font controls, the Bin Container Sidebar's background, font and
// size, bin backgrounds (allowed, on tabs only), default and override bin fonts, the viewer
// background and the Timeline background with and without blending.
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

} // namespace

class TestInterfaceColors : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void dialogControls() {
        const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Interface"));
        QVERIFY(kind);
        SettingsDialog dialog(*kind, model::SettingEntry{QStringLiteral("Interface"), {}, true, {}});
        // A colour: a swatch button holding "#rrggbb".
        auto* color = qobject_cast<QToolButton*>(dialog.editor(QStringLiteral("sidebarBackground")));
        QVERIFY(color);
        QCOMPARE(color->property("color").toString(), QStringLiteral("#2b3a4a"));
        color->setProperty("color", QStringLiteral("#ff0000"));
        // A font: Default, then the families.
        auto* font = qobject_cast<QComboBox*>(dialog.editor(QStringLiteral("sidebarFont")));
        QVERIFY(font);
        QCOMPARE(font->currentData().toString(), QString());
        QVERIFY(font->count() > 1);
        font->setCurrentIndex(1);
        const QJsonObject values = dialog.values();
        QCOMPARE(values.value(QStringLiteral("sidebarBackground")).toString(), QStringLiteral("#ff0000"));
        QCOMPARE(values.value(QStringLiteral("sidebarFont")).toString(), font->itemData(1).toString());
        // A colour that may be empty starts as None.
        auto* none = qobject_cast<QToolButton*>(dialog.editor(QStringLiteral("newBinBackground")));
        QVERIFY(none);
        QCOMPARE(none->property("color").toString(), QString());
    }

    void appliedFromTheSettings() {
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
        Settings& s = window.settings();
        const QString ui = QStringLiteral("Interface");
        BinContainer& bins = window.binContainer();

        // ToolTips Delay (p.1200), in seconds, through the style's wake-up delay.
        s.setValue(ui, QStringLiteral("toolTipDelay"), 3);
        settle();
        QCOMPARE(qApp->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay), 3000);
        s.setValue(ui, QStringLiteral("toolTipDelay"), 0);
        settle();
        QCOMPARE(qApp->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay), 0);

        // The Sidebar (p.1201).
        s.setValue(ui, QStringLiteral("customSidebarBackground"), true);
        s.setValue(ui, QStringLiteral("sidebarBackground"), QStringLiteral("#203040"));
        s.setValue(ui, QStringLiteral("overrideSidebarFontSize"), true);
        s.setValue(ui, QStringLiteral("sidebarFontSize"), 17);
        settle();
        QCOMPARE(bins.tree()->palette().color(QPalette::Base), QColor(QStringLiteral("#203040")));
        QCOMPARE(bins.tree()->font().pointSize(), 17);
        s.setValue(ui, QStringLiteral("customSidebarBackground"), false);
        settle();
        QVERIFY(bins.tree()->palette().color(QPalette::Base) != QColor(QStringLiteral("#203040")));

        // "Default background color for new bins" (p.1201).
        s.setValue(ui, QStringLiteral("newBinBackground"), QStringLiteral("#405060"));
        settle();
        const QString id = bins.newBin();
        settle();
        BinDocument* doc = bins.document(id);
        QVERIFY(doc);
        QCOMPARE(doc->bin().display.background, QStringLiteral("#405060"));
        BinView* view = bins.viewFor(doc);
        auto* table = view->findChild<QAbstractItemView*>(QStringLiteral("binTable"));
        QVERIFY(table);
        QCOMPARE(table->palette().color(QPalette::Base), QColor(QStringLiteral("#405060")));

        // On tabs only: the bin keeps the theme's background, its tab shows the colour.
        s.setValue(ui, QStringLiteral("binBackgroundTabsOnly"), true);
        settle();
        QVERIFY(table->palette().color(QPalette::Base) != QColor(QStringLiteral("#405060")));
        BinTabGroup* group = nullptr;
        for (BinDockArea* a : bins.areas()) {
            if (!group) group = a->groupOf(view);
        }
        QVERIFY(group);
        QVERIFY(!group->tabIcon(group->indexOf(view)).isNull());
        // Not allowed: neither.
        s.setValue(ui, QStringLiteral("customBinBackgrounds"), false);
        settle();
        QVERIFY(group->tabIcon(group->indexOf(view)).isNull());

        // Override all bin font sizes.
        s.setValue(ui, QStringLiteral("overrideBinFontSizes"), true);
        s.setValue(ui, QStringLiteral("overrideBinFontSize"), 19);
        settle();
        QCOMPARE(table->font().pointSize(), 19);

        // The viewer background.
        QCOMPARE(window.composer().viewerBackground(), QColor(18, 18, 18));
        s.setValue(ui, QStringLiteral("customViewerBackground"), true);
        settle();
        QCOMPARE(window.composer().viewerBackground(), QColor(QStringLiteral("#26303a")));

        // The Timeline background, blended half and half with the default grey or not (p.1202).
        const QColor plain = window.timeline().backgroundColor();
        s.setValue(ui, QStringLiteral("timelineBackground"), true);
        s.setValue(ui, QStringLiteral("timelineBackgroundColor"), QStringLiteral("#ff0000"));
        settle();
        const QColor blended = window.timeline().backgroundColor();
        QCOMPARE(blended, QColor((255 + plain.red()) / 2, plain.green() / 2, plain.blue() / 2));
        s.setValue(ui, QStringLiteral("timelineBlend"), false);
        settle();
        QCOMPARE(window.timeline().backgroundColor(), QColor(Qt::red));
    }
};

QTEST_MAIN(TestInterfaceColors)
#include "tst_interface_colors.moc"
