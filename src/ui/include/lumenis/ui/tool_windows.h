#pragma once

#include "lumenis/model/calculator.h"

#include <QObject>
#include <QStringList>
#include <QDialog>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;

namespace lumenis::ui {

// Tools > Calculator (guide p.100): the Format menu, the display and a
// keypad; the numeric keypad and the top row of digits type too.
// The arithmetic and formats are model::Calculator (src/model/calculator.*); this is only its face.
class CalculatorWindow : public QWidget {
    Q_OBJECT

public:
    explicit CalculatorWindow(QWidget* parent = nullptr);

    // The calculator behind the window. Changes made to it directly show at the next key.
    model::Calculator& calculator() { return calc_; }
    QComboBox* formatMenu() const { return format_; }
    // The display as shown, with a trailing space as margin.
    QString displayText() const;
    // A keypad key: "0"-"9", "00", "+", "-", "=", "C" or "<" (backspace).
    void press(const QString& key);

protected:
    // Digits, + - = type as on the keypad; Return or Enter is =, Backspace is <, Esc or Delete is C.
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Shows the calculator's display and format.
    void refresh();

    model::Calculator calc_;
    QComboBox* format_ = nullptr;
    QLabel* display_ = nullptr;
};

// What the Console window shows (p.101): system information first, then
// messages and errors as they are logged.
//
// A process-wide singleton. Anything may append() to it (MainWindow logs relink, import and export
// results here); every ConsoleWindow shows it.
class ConsoleLog : public QObject {
    Q_OBJECT

public:
    // The one log, made on first use with the system information as its first lines.
    static ConsoleLog& instance();
    // Sends Qt's warnings and errors here too (and on to where they went).
    static void installMessageHandler();

    // Every line kept, oldest first; at most the last 5000.
    QStringList lines() const { return lines_; }
    // Adds a line and emits lineAdded().
    void append(const QString& line);
    // Empties the log (the system information too) and emits cleared().
    void clear();
    // "System ID: ..." (p.101) and the rest of the system information.
    static QStringList systemInfo();

signals:
    // After append(), with the line added.
    void lineAdded(const QString& line);
    // After clear().
    void cleared();

private:
    ConsoleLog();
    // Puts back the message handler from before installMessageHandler().
    ~ConsoleLog() override;
    QStringList lines_;
};

// Tools > Console (p.101-102): the log, and a command line. It shows ConsoleLog's lines and
// follows them as they are added or cleared.
class ConsoleWindow : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleWindow(QWidget* parent = nullptr);

    QPlainTextEdit* log() const { return log_; }
    QLineEdit* commandLine() const { return command_; }
    // Runs a console command; its output goes to the log. The commands are help, clear, sysinfo
    // and version; only the first word counts, ignoring case.
    void run(const QString& command);

private:
    QPlainTextEdit* log_ = nullptr;
    QLineEdit* command_ = nullptr;
};

// What the Usage tab reports for the open project (p.102-104).
struct ProjectUsage {
    QString project; // its name; empty when no project is open
    qint64 secondsOpen = 0; // "Time Project open"
    int bins = 0;
    int clips = 0;
    int sequences = 0;
};

// The Configuration, Usage and Hardware information of Help > About
// (p.94, p.102-105): the version, options, licence and installed
// plug-ins; the project's statistics; the drives with their free space
// and the system. Information only; nothing can be changed.
class AboutInfoDialog : public QDialog {
    Q_OBJECT

public:
    // `plugins`: the names listed under Installed plug-ins. The drives and system are read when
    // the dialog is made.
    AboutInfoDialog(const QStringList& plugins, const ProjectUsage& usage, QWidget* parent = nullptr);
    // The top-level tabs: Configuration, Usage and Hardware.
    QTabWidget* tabs() const { return tabs_; }
    // "Configuration", "Usage" or "Hardware".
    // Compared with the tab's (translated) text; an unknown name changes nothing.
    void showTab(const QString& name);
    // The Hardware tab's own tabs: Drives and System.
    QTabWidget* hardwareTabs() const { return hardware_; }

private:
    QTabWidget* tabs_ = nullptr;
    QTabWidget* hardware_ = nullptr;
};

} // namespace lumenis::ui
