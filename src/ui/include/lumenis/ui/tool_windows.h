#pragma once

#include "lumenis/model/calculator.h"

#include <QObject>
#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace lumenis::ui {

// Tools > Calculator (guide p.100): the Format menu, the display and a
// keypad; the numeric keypad and the top row of digits type too.
class CalculatorWindow : public QWidget {
    Q_OBJECT

public:
    explicit CalculatorWindow(QWidget* parent = nullptr);

    model::Calculator& calculator() { return calc_; }
    QComboBox* formatMenu() const { return format_; }
    QString displayText() const;
    // A keypad key: "0"-"9", "00", "+", "-", "=", "C" or "<" (backspace).
    void press(const QString& key);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refresh();

    model::Calculator calc_;
    QComboBox* format_ = nullptr;
    QLabel* display_ = nullptr;
};

// What the Console window shows (p.101): system information first, then
// messages and errors as they are logged.
class ConsoleLog : public QObject {
    Q_OBJECT

public:
    static ConsoleLog& instance();
    // Sends Qt's warnings and errors here too (and on to where they went).
    static void installMessageHandler();

    QStringList lines() const { return lines_; }
    void append(const QString& line);
    void clear();
    // "System ID: ..." (p.101) and the rest of the system information.
    static QStringList systemInfo();

signals:
    void lineAdded(const QString& line);
    void cleared();

private:
    ConsoleLog();
    QStringList lines_;
};

// Tools > Console (p.101-102): the log, and a command line.
class ConsoleWindow : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleWindow(QWidget* parent = nullptr);

    QPlainTextEdit* log() const { return log_; }
    QLineEdit* commandLine() const { return command_; }
    // Runs a console command; its output goes to the log.
    void run(const QString& command);

private:
    QPlainTextEdit* log_ = nullptr;
    QLineEdit* command_ = nullptr;
};

} // namespace lumenis::ui
