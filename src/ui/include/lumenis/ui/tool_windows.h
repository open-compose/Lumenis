#pragma once

#include "lumenis/model/calculator.h"

#include <QObject>
#include <QStringList>
#include <QDialog>
#include <QWidget>

#include <functional>
#include <mutex>

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
// results here); every ConsoleWindow shows it. append(), lines() and clear() may be called from any
// thread (Qt's message handler runs on the thread that logged).
class ConsoleLog : public QObject {
    Q_OBJECT

public:
    // The one log, made on first use with the system information as its first lines.
    static ConsoleLog& instance();
    // Sends Qt's warnings and errors here too (and on to where they went).
    static void installMessageHandler();

    // Every line kept, oldest first; at most the last 5000. A copy, taken under the lock.
    QStringList lines() const;
    // Adds a line and emits lineAdded() on the calling thread.
    void append(const QString& line);
    // Empties the log (the system information too) and emits cleared().
    void clear();
    // "System ID: ..." (p.101) and the rest of the system information.
    static QStringList systemInfo();

signals:
    // After append(), with the line added, on the thread that appended: connect with a receiver
    // as context so it arrives on the receiver's thread.
    void lineAdded(const QString& line);
    // After clear().
    void cleared();

private:
    ConsoleLog();
    // Puts back the message handler from before installMessageHandler().
    ~ConsoleLog() override;
    mutable std::mutex mutex_; // guards lines_
    QStringList lines_;
};

// Error messages everywhere (9.7; the owner's request, 2026-11-08): whatever fails, is missing or cannot be done tells
// the user in a message, not only in the Console. `line` is written to the Console as it is and shown in a warning that
// does not block (QMessageBox::open: modal to its window only, the call returns at once), so tests and scripts go on: "Title: text" shows "Title" as the warning's title and main text (macOS
// shows no window titles on message boxes) with `text` below it (the product name when the line has no "Title: " part). While a warning with that title is still open, a new failure adds its text to it
// instead of opening another. On the UI thread only. `parent` (or the active window when null) owns the warning.
// scripts/check-failure-messages.py (ctest failure_messages) fails for a Console line in src/ui written any other
// way, unless it is marked "Console only" with the reason (information, not a failure).
void reportFailure(QWidget* parent, const QString& line);
// The warning alone, for failures whose lines are already in the Console (an export's, gathered by its caller).
void showFailure(QWidget* parent, const QString& title, const QString& text);
// Tests: called with the title and text in place of the warning (the Console line is still written); empty to show
// warnings again.
using FailureObserver = std::function<void(const QString& title, const QString& text)>;
void setFailureObserver(FailureObserver observer);

// Tools > Console (p.101-102): the log, and a command line. It shows ConsoleLog's lines and
// follows them as they are added or cleared.
class ConsoleWindow : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleWindow(QWidget* parent = nullptr);

    QPlainTextEdit* log() const { return log_; }
    QLineEdit* commandLine() const { return command_; }
    // Runs a console command; its output goes to the log. The commands are help, clear, sysinfo,
    // version and HDTitleFilter (FX Guide p.376); only the first word counts, ignoring case.
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
    // "Configuration", "Usage" or "Hardware": stable keys, not translated (the tab's translated
    // text is accepted too). An unknown name changes nothing.
    void showTab(const QString& name);
    // The Hardware tab's own tabs: Drives and System.
    QTabWidget* hardwareTabs() const { return hardware_; }

private:
    QTabWidget* tabs_ = nullptr;
    QTabWidget* hardware_ = nullptr;
};

// A drive's free space as "12.3 GB free", in decimal gigabytes (10^9 bytes), for every place that
// shows one (Help > About's Drives, the Consolidate/Transcode drive lists).
QString freeSpaceText(qint64 bytes);

} // namespace lumenis::ui
