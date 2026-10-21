#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// Drops a command from the Command Palette onto a button (p.98).
void dropCommand(QWidget* target, const QString& commandId) {
    std::unique_ptr<QMimeData> mime(ButtonMapper::mimeFor(commandId));
    const QPointF at(target->width() / 2.0, target->height() / 2.0);
    QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDropEvent drop(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
}

model::BinItem sequence() {
    model::BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = model::BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("Interview");
        a.sourceTrack = t;
        a.sourceIn = 100;
        a.length = 50;
        seq.splice(t, 0, {a});
    }
    seq.storeIn(s);
    return s;
}

} // namespace

class TestTools : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }
    void init() { ButtonMapper::instance().setMode(ButtonMapper::Mode::ButtonToButton); }

    void commandPaletteTabs() {
        // The tabs in the picture on p.96; the Move tab as pictured.
        CommandRegistry commands;
        commands.loadBuiltin();
        CommandPalette palette(commands);
        QCOMPARE(palette.tabNames(),
                 (QStringList{"Move", "Play", "Edit", "Trim", "FX", "3D", "CC", "MCam", "Tracks", "Smart Tools", "Other",
                              "More", "Workspaces", "Plug-ins"}));
        const QStringList move = palette.commandsOn(QStringLiteral("Move"));
        QCOMPARE(move.size(), 18);
        QVERIFY(move.contains("move.go_to_next_marker"));
        QVERIFY(move.contains("move.step_forward_one_field"));
        // The Blank button is on the Other tab (p.97).
        QVERIFY(palette.commandsOn(QStringLiteral("Other")).contains(QString()));
        QVERIFY(palette.button(QString()));
        // The three modes at the bottom; Button to Button first.
        QVERIFY(palette.findChild<QRadioButton*>(QStringLiteral("modeButtonToButton"))->isChecked());
        palette.findChild<QRadioButton*>(QStringLiteral("modeActive"))->click();
        QCOMPARE(ButtonMapper::instance().mode(), ButtonMapper::Mode::Active);
        ButtonMapper::instance().setMode(ButtonMapper::Mode::MenuToButton);
        QVERIFY(palette.findChild<QRadioButton*>(QStringLiteral("modeMenuToButton"))->isChecked());
        // Closing the palette leaves Menu to Button mode.
        palette.show();
        palette.hide();
        QCOMPARE(ButtonMapper::instance().mode(), ButtonMapper::Mode::ButtonToButton);
    }

    void quickFind() {
        // p.99-100: matching buttons only; tabs without matches close.
        CommandRegistry commands;
        commands.loadBuiltin();
        CommandPalette palette(commands);
        const QStringList found = palette.matches(QStringLiteral("add"));
        QVERIFY(found.contains("edit.add_edit"));
        QVERIFY(found.contains("more.add_marker"));
        QVERIFY(!found.contains("edit.lift"));
        palette.setQuickFind(QStringLiteral("add"));
        const QStringList names = palette.tabNames();
        QVERIFY(palette.tabs()->isTabVisible(static_cast<int>(names.indexOf("Edit"))));
        QVERIFY(!palette.tabs()->isTabVisible(static_cast<int>(names.indexOf("Move"))));
        palette.setQuickFind(QString());
        for (int i = 0; i < palette.tabs()->count(); ++i) QVERIFY(palette.tabs()->isTabVisible(i));
        QVERIFY(palette.showCommand(QStringLiteral("trim.slip_left")));
        QCOMPARE(palette.tabs()->tabText(palette.tabs()->currentIndex()), QStringLiteral("Trim"));
    }

    void activePalette() {
        // p.99: in Active Palette mode a click runs the command.
        CommandRegistry commands;
        commands.loadBuiltin();
        int played = 0;
        commands.setHandler(QStringLiteral("play.play"), [&] { ++played; });
        CommandPalette palette(commands);
        palette.refresh();
        PaletteButton* play = palette.button(QStringLiteral("play.play"));
        QVERIFY(play->isEnabled());
        // Commands without a handler are dimmed.
        QVERIFY(!palette.button(QStringLiteral("edit.fit_to_fill"))->isEnabled());
        play->click();
        QCOMPARE(played, 0); // Button to Button: clicking does nothing
        ButtonMapper::instance().setMode(ButtonMapper::Mode::Active);
        play->click();
        QCOMPARE(played, 1);
    }

    void buttonToButton() {
        // p.98: drag a palette button onto a Composer button.
        Composer composer;
        QSignalSpy changed(&composer, &Composer::buttonsChanged);
        auto* blank = composer.findChildren<MappableButton*>(QStringLiteral("blankButton")).first();
        const QPoint slot = blank->property("slot").toPoint();
        dropCommand(blank, QStringLiteral("other.match_frame"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(blank->command(), QStringLiteral("other.match_frame"));
        QCOMPARE(blank->objectName(), QStringLiteral("other.match_frame"));
        const QJsonObject layout = composer.buttonLayout();
        const QString side = composer.button(MonitorId::Source, QStringLiteral("other.match_frame"))
                                 ? QStringLiteral("source")
                                 : QStringLiteral("record");
        QCOMPARE(layout.value(side).toArray().at(slot.x()).toArray().at(slot.y()).toString(),
                 QStringLiteral("other.match_frame"));
        // Blank replaces a button (p.97).
        QToolButton* splice = composer.button(MonitorId::Source, QStringLiteral("edit.splice_in"));
        dropCommand(splice, QString());
        QVERIFY(!composer.button(MonitorId::Source, QStringLiteral("edit.splice_in")));
        // The layout comes back from the settings; {} is the default.
        Composer other;
        other.setButtonLayout(composer.buttonLayout());
        QVERIFY(other.button(MonitorId::Source, QStringLiteral("other.match_frame")) ||
                other.button(MonitorId::Record, QStringLiteral("other.match_frame")));
        QVERIFY(!other.button(MonitorId::Source, QStringLiteral("edit.splice_in")));
        other.setButtonLayout({});
        QVERIFY(other.button(MonitorId::Source, QStringLiteral("edit.splice_in")));
        QCOMPARE(other.buttonLayout(), Composer::defaultButtonLayout());
    }

    void menuToButton() {
        // p.98: pick a target button, then a menu command; the button shows
        // the command's initials.
        CommandRegistry commands;
        commands.loadBuiltin();
        int ran = 0;
        commands.setHandler(QStringLiteral("composer.load_filler"), [&] { ++ran; });
        ButtonMapper::instance().setRegistry(&commands);
        commands.setCapture([](const QString& id) {
            ButtonMapper& m = ButtonMapper::instance();
            return m.mode() == ButtonMapper::Mode::MenuToButton && m.target() && m.mapMenuCommand(id);
        });
        ToolPalette tools;
        QSignalSpy changed(&tools, &ToolPalette::commandsChanged);
        ButtonMapper::instance().setMode(ButtonMapper::Mode::MenuToButton);
        MappableButton* target = tools.slot(0);
        QTest::mouseClick(target, Qt::LeftButton);
        QCOMPARE(ButtonMapper::instance().target(), target);
        if (QAction* a = commands.action(QStringLiteral("composer.load_filler"))) {
            a->trigger();
            QCOMPARE(ran, 0); // mapped, not run
            QCOMPARE(target->command(), QStringLiteral("composer.load_filler"));
            QCOMPARE(target->toolTip(), QStringLiteral("Load Filler"));
            QCOMPARE(changed.count(), 1);
            QVERIFY(!ButtonMapper::instance().target());
            // Without a target the command runs.
            a->trigger();
            QCOMPARE(ran, 1);
        }
        QCOMPARE(commandInitials(QStringLiteral("Load Filler")), QStringLiteral("LF"));
        QCOMPARE(commandInitials(QStringLiteral("Lift/Overwrite")), QStringLiteral("LO"));
        ButtonMapper::instance().setRegistry(nullptr);
    }

    void toolPalette() {
        ToolPalette tools;
        QCOMPARE(tools.commands(), ToolPalette::defaultCommands());
        QSignalSpy clicked(&tools, &ToolPalette::commandClicked);
        tools.slot(0)->click();
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.first().first().toString(), QStringLiteral("edit.mark_in"));
        tools.setCommands({QStringLiteral("edit.lift")});
        QCOMPARE(tools.commands().first(), QStringLiteral("edit.lift"));
        QCOMPARE(tools.commands().at(1), ToolPalette::defaultCommands().at(1));
        // Show Labels in Tool Palette (p.392).
        tools.setShowLabels(true);
        QCOMPARE(tools.slot(0)->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
        QVERIFY(!tools.slot(0)->text().isEmpty());
    }

    void calculator() {
        // p.100: digits without colons, the Format menu converts.
        CalculatorWindow calc;
        calc.formatMenu()->setCurrentText(QStringLiteral("25 Timecode"));
        for (const char* k : {"1", "0", "00", "+", "5", "00", "="}) calc.press(QString::fromLatin1(k));
        QCOMPARE(calc.displayText().trimmed(), QStringLiteral("00:00:15:00"));
        calc.formatMenu()->setCurrentText(QStringLiteral("30 Timecode"));
        QCOMPARE(calc.displayText().trimmed(), QStringLiteral("00:00:15:00"));
        QCOMPARE(calc.calculator().value(), 450);
        // Typing on the keyboard.
        calc.show();
        QTest::keyClick(&calc, Qt::Key_Escape);
        QTest::keyClicks(&calc, QStringLiteral("2"));
        QTest::keyClick(&calc, Qt::Key_Return);
        QCOMPARE(calc.displayText().trimmed(), QStringLiteral("00:00:00:02"));
    }

    void console() {
        // p.101: system information, logged messages, the command line.
        ConsoleWindow console;
        const QStringList info = ConsoleLog::systemInfo();
        QVERIFY(info.at(1).startsWith(QStringLiteral("System ID: ")));
        console.commandLine()->setText(QStringLiteral("help"));
        QTest::keyClick(console.commandLine(), Qt::Key_Return);
        QVERIFY(console.log()->toPlainText().contains(QStringLiteral("Commands:")));
        console.run(QStringLiteral("nonsense"));
        QVERIFY(ConsoleLog::instance().lines().constLast().startsWith(QStringLiteral("Unknown command")));
        console.run(QStringLiteral("clear"));
        QVERIFY(console.log()->toPlainText().isEmpty());
        ConsoleLog::instance().append(QStringLiteral("Error: test"));
        QCOMPARE(console.log()->toPlainText(), QStringLiteral("Error: test"));
    }

    void consoleFromThreads() {
        // Qt's message handler runs on the thread that logged: every line arrives, and the
        // window gets them on its own thread.
        ConsoleWindow console;
        ConsoleLog::instance().clear();
        constexpr int kThreads = 8;
        constexpr int kLines = 300;
        QList<QThread*> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads << QThread::create([t] {
                for (int i = 0; i < kLines; ++i) ConsoleLog::instance().append(QStringLiteral("thread %1 line %2").arg(t).arg(i));
            });
        }
        for (QThread* t : threads) t->start();
        for (QThread* t : threads) {
            QVERIFY(t->wait(10000));
            delete t;
        }
        QCOMPARE(ConsoleLog::instance().lines().size(), kThreads * kLines);
        QTRY_COMPARE(console.log()->blockCount(), kThreads * kLines);
        ConsoleLog::instance().clear();
    }

    void mainWindowTools() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        window.show();
        // Tools open as floating panels (p.94).
        for (const auto& [command, panel] : {std::pair{"tools.command_palette", "CommandPalette"},
                                             std::pair{"tools.calculator", "Calculator"},
                                             std::pair{"tools.console", "Console"},
                                             std::pair{"other.tool_palette", "ToolPalette"}}) {
            QAction* a = commands.action(QString::fromLatin1(command));
            QVERIFY2(a->isEnabled(), command);
            a->trigger();
            Panel* p = window.panels().panel(QString::fromLatin1(panel));
            QVERIFY2(window.panels().isOpen(p), panel);
            QVERIFY(window.panels().isFloating(p));
        }
        QCOMPARE(commands.action("tools.command_palette")->shortcut(), QKeySequence(QStringLiteral("Ctrl+3")));

        // Mapped Composer buttons are saved with the Composer settings, and
        // the Tool palette's in its own (p.97, p.1221).
        auto* blank = window.composer().findChildren<MappableButton*>(QStringLiteral("blankButton")).first();
        dropCommand(blank, QStringLiteral("edit.lift"));
        QVERIFY(window.settings().value("Composer", "buttons").toObject() == window.composer().buttonLayout());
        dropCommand(window.toolPalette().slot(2), QStringLiteral("trim.trim_mode"));
        QCOMPARE(window.settings().value("Tool Palette", "buttons").toArray().at(2).toString(),
                 QStringLiteral("trim.trim_mode"));
        Settings again;
        again.setUser(dir.filePath("Users"), QStringLiteral("ed"));
        QCOMPARE(again.value("Tool Palette", "buttons").toArray().at(2).toString(), QStringLiteral("trim.trim_mode"));
        window.settings().setValue("Interface", "toolPaletteLabels", true);
        QVERIFY(window.toolPalette().showsLabels());
        // Restoring the default Composer setting restores the buttons.
        window.settings().setValue("Composer", "buttons", QJsonObject());
        QCOMPARE(window.composer().buttonLayout(), Composer::defaultButtonLayout());
    }

    void positionInfo() {
        // File > Get Position Info (p.101) writes to the Console.
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        model::ProjectEntry entry;
        entry.folder = dir.path();
        entry.settings.name = QStringLiteral("Film");
        window.setProject(entry);
        window.composer().load(MonitorId::Record, QStringLiteral("bin"), sequence());
        window.composer().moveTo(MonitorId::Record, 10);
        ConsoleLog::instance().clear();
        commands.action(QStringLiteral("file.get_position_info"))->trigger();
        const QString log = ConsoleLog::instance().lines().join(QLatin1Char('\n'));
        QVERIFY2(log.contains(QStringLiteral("Position Info: Cut")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("V1: Interview, clip frame 110")), qPrintable(log));
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("Console"))));
    }
};

QTEST_MAIN(TestTools)
#include "tst_tools.moc"
