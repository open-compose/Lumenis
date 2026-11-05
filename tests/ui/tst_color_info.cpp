// The Color Info tool (FX Guide p.696-697; lumenis/ui/color_info.h): the colour under the pointer in the Video and Video
// LUT columns, the scales of its shortcut menu, Active only for video monitors, and the ways it opens.
#include "lumenis/core/project_format.h"
#include "lumenis/ext/provider.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/color_info.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/correction_tool.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

using namespace lumenis;
using namespace lumenis::ui;

class TestColorInfo : public QObject {
    Q_OBJECT

private slots:
    void valuesAndScales() {
        ColorInfoTool tool;
        bool monitorsOnly = false;
        tool.setSampler([&](const QPoint&, bool only) -> std::optional<QColor> {
            monitorsOnly = only;
            return QColor(219, 218, 16);
        });
        tool.sample(QPoint(10, 10));
        QVERIFY(monitorsOnly); // "Active only for video monitors" is on at first (p.697)
        QCOMPARE(tool.columnText(false), QStringLiteral("R: 219\nG: 218\nB: 16"));
        // "RGB 10 bit Measures on a scale from 0 to 1024"; Percent; IRE with black at 16; millivolts.
        tool.setUnit(ColorInfoTool::Unit::Rgb10);
        QCOMPARE(tool.columnText(false), QStringLiteral("R: 879\nG: 875\nB: 64"));
        tool.setUnit(ColorInfoTool::Unit::Percent);
        QCOMPARE(tool.columnText(false), QStringLiteral("R: 85.9\nG: 85.5\nB: 6.3"));
        tool.setUnit(ColorInfoTool::Unit::Ire);
        QCOMPARE(ColorInfoTool::valueText(235, ColorInfoTool::Unit::Ire), QStringLiteral("100.0"));
        QCOMPARE(ColorInfoTool::valueText(0, ColorInfoTool::Unit::Ire), QStringLiteral("-7.3"));
        QCOMPARE(ColorInfoTool::valueText(235, ColorInfoTool::Unit::Millivolts), QStringLiteral("700"));
        // Nothing under the pointer: zeros.
        tool.setSampler([](const QPoint&, bool) { return std::optional<QColor>(); });
        tool.setUnit(ColorInfoTool::Unit::Rgb8);
        tool.sample(QPoint(0, 0));
        QCOMPARE(tool.columnText(false), QStringLiteral("R: 0\nG: 0\nB: 0"));
    }

    // The shortcut menu (p.697): the two options and the five scales.
    void shortcutMenu() {
        ColorInfoTool tool;
        tool.resize(240, 120);
        tool.show();
        std::unique_ptr<QMenu> menu(tool.shortcutMenu(&tool));
        QStringList texts;
        for (QAction* a : menu->actions()) {
            if (!a->isSeparator()) texts << a->text();
        }
        QCOMPARE(texts, (QStringList{"Active only for video monitors", "Show video color after applying Display Color Space", "RGB 8 bit",
                                     "RGB 10 bit", "RGB Percent", "IRE", "mVolts"}));
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("colorInfo:unit0"))->isChecked());
        QWidget* lut = tool.findChild<QWidget*>(QStringLiteral("colorInfoLut"))->parentWidget();
        QVERIFY(lut->isVisible());
        menu->findChild<QAction*>(QStringLiteral("colorInfo:lut"))->trigger();
        QVERIFY(!tool.showAfterDisplayColorSpace());
        QVERIFY(!lut->isVisible());
        menu->findChild<QAction*>(QStringLiteral("colorInfo:monitors"))->trigger();
        QVERIFY(!tool.activeOnlyForMonitors());
        menu->findChild<QAction*>(QStringLiteral("colorInfo:unit3"))->trigger();
        QCOMPARE(tool.unit(), ColorInfoTool::Unit::Ire);
        QCOMPARE(tool.findChild<QLabel*>(QStringLiteral("colorInfoUnit"))->text(), QStringLiteral("IRE"));
    }

    // Tools > Color Info Tool and the Color Correction tool's Color Info button (p.697).
    void opening() {
        Composer composer;
        Timeline timeline(composer);
        CorrectionTool tool(timeline);
        QSignalSpy spy(&tool, &CorrectionTool::colorInfoRequested);
        auto* button = tool.findChild<QToolButton*>(QStringLiteral("ccButton:colorInfo"));
        QVERIFY(button && button->isEnabled());
        button->click();
        QCOMPARE(spy.count(), 1);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        model::ProjectSettings settings;
        settings.name = QStringLiteral("Info");
        settings.format = projectFormatPresets().front();
        const model::ProjectEntry entry = *model::ProjectStore(dir.path()).create(settings);
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.setProject(entry);
        window.show();
        // Panels live outside the window until they are placed.
        const auto find = [] {
            for (QWidget* w : QApplication::allWidgets()) {
                if (auto* t = qobject_cast<ColorInfoTool*>(w)) return t;
            }
            return static_cast<ColorInfoTool*>(nullptr);
        };
        QVERIFY(find());
        QVERIFY(!find()->isVisible());
        commands.action(QStringLiteral("tools.color_info_tool"))->trigger();
        QTRY_VERIFY(find()->isVisible());
        window.clearProject();
    }
};

QTEST_MAIN(TestColorInfo)
#include "tst_color_info.moc"
