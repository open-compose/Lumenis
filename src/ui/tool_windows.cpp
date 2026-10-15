#include "lumenis/ui/tool_windows.h"

#include "lumenis/core/branding.h"
#include "lumenis/ui/theme.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <cstdio>
#include <QDateTime>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumenis::ui {

// ---------------------------------------------------------------- CalculatorWindow

CalculatorWindow::CalculatorWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("calculator"));
    setFocusPolicy(Qt::StrongFocus);

    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("calculatorFormat"));
    for (const model::CalculatorFormat& f : model::calculatorFormats()) format_->addItem(f.name);
    format_->setCurrentIndex(calc_.format());
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int index) {
        calc_.setFormat(index);
        refresh();
    });

    display_ = new QLabel(this);
    display_->setObjectName(QStringLiteral("calculatorDisplay"));
    display_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    display_->setAutoFillBackground(true);
    display_->setFixedHeight(32);
    QFont f = display_->font();
    f.setPixelSize(18);
    display_->setFont(f);
    QPalette pal = display_->palette();
    pal.setColor(QPalette::Window, Qt::black);
    pal.setColor(QPalette::WindowText, QColor(0xff4cd98c)); // green, like the timecode displays
    display_->setPalette(pal);

    // The keypad: digits on the left, operations on the right (our layout;
    // the guide has no picture of it).
    auto* keys = new QGridLayout;
    keys->setSpacing(2);
    const char* digits[4][3] = {{"7", "8", "9"}, {"4", "5", "6"}, {"1", "2", "3"}, {"0", "00", "<"}};
    const char* ops[4] = {"C", "-", "+", "="};
    auto key = [this, keys](const QString& label, int row, int column) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("calculatorKey:") + label);
        b->setText(label == QLatin1String("<") ? QStringLiteral("←") : label);
        b->setFixedSize(40, kControlHeight + 4);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, label] { press(label); });
        keys->addWidget(b, row, column);
    };
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 3; ++c) key(QString::fromLatin1(digits[r][c]), r, c);
        key(QString::fromLatin1(ops[r]), r, 3);
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(format_);
    layout->addWidget(display_);
    layout->addLayout(keys);
    layout->addStretch(1);
    refresh();
}

QString CalculatorWindow::displayText() const {
    return display_->text();
}

void CalculatorWindow::press(const QString& key) {
    if (key == QLatin1String("00")) {
        calc_.doubleZero();
    } else if (key.size() == 1 && key.at(0).isDigit()) {
        calc_.digit(key.at(0).digitValue());
    } else if (key == QLatin1String("+")) {
        calc_.add();
    } else if (key == QLatin1String("-")) {
        calc_.subtract();
    } else if (key == QLatin1String("=")) {
        calc_.equals();
    } else if (key == QLatin1String("C")) {
        calc_.clear();
    } else if (key == QLatin1String("<")) {
        calc_.backspace();
    }
    refresh();
}

void CalculatorWindow::keyPressEvent(QKeyEvent* event) {
    // The numeric keypad and the top row of digits (p.100).
    const QString t = event->text();
    if (t.size() == 1 && (t.at(0).isDigit() || t == QLatin1String("+") || t == QLatin1String("-") ||
                          t == QLatin1String("="))) {
        press(t);
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        press(QStringLiteral("="));
    } else if (event->key() == Qt::Key_Backspace) {
        press(QStringLiteral("<"));
    } else if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Delete) {
        press(QStringLiteral("C"));
    } else {
        QWidget::keyPressEvent(event);
    }
}

void CalculatorWindow::refresh() {
    if (format_->currentIndex() != calc_.format()) format_->setCurrentIndex(calc_.format());
    display_->setText(calc_.display() + QLatin1Char(' '));
}

// ---------------------------------------------------------------- ConsoleLog

namespace {

QtMessageHandler& previousHandler() {
    static QtMessageHandler previous = nullptr;
    return previous;
}

void consoleHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type != QtDebugMsg && type != QtInfoMsg) {
        const char* kind = type == QtWarningMsg ? "Warning" : type == QtCriticalMsg ? "Error" : "Fatal";
        ConsoleLog::instance().append(QStringLiteral("%1: %2").arg(QString::fromLatin1(kind), message));
    }
    if (previousHandler()) {
        previousHandler()(type, context, message);
    } else {
        std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
    }
}

} // namespace

ConsoleLog::ConsoleLog() {
    lines_ = systemInfo();
}

ConsoleLog& ConsoleLog::instance() {
    static ConsoleLog log;
    return log;
}

void ConsoleLog::installMessageHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    previousHandler() = qInstallMessageHandler(consoleHandler);
}

void ConsoleLog::append(const QString& line) {
    lines_ << line;
    // Keep the log bounded.
    if (lines_.size() > 5000) lines_.remove(0, lines_.size() - 5000);
    emit lineAdded(line);
}

void ConsoleLog::clear() {
    lines_.clear();
    emit cleared();
}

QStringList ConsoleLog::systemInfo() {
    // A stable ID for this computer, derived from the operating system's
    // machine ID, which it does not reveal.
    const QByteArray machine = QSysInfo::machineUniqueId();
    const QString id = machine.isEmpty()
                           ? QStringLiteral("unknown")
                           : QString::fromLatin1(
                                 QCryptographicHash::hash(machine, QCryptographicHash::Sha256).toHex().left(12).toUpper());
    return {QStringLiteral("%1 %2").arg(QString::fromUtf8(branding::kProductName), QString::fromUtf8(branding::kVersion)),
            QStringLiteral("System ID: %1").arg(id),
            QStringLiteral("Operating system: %1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()),
            QStringLiteral("Qt: %1").arg(QString::fromLatin1(qVersion())),
            QStringLiteral("Started: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate))};
}

// ---------------------------------------------------------------- ConsoleWindow

ConsoleWindow::ConsoleWindow(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("console"));
    log_ = new QPlainTextEdit(this);
    log_->setObjectName(QStringLiteral("consoleLog"));
    log_->setReadOnly(true);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setPlainText(ConsoleLog::instance().lines().join(QLatin1Char('\n')));
    command_ = new QLineEdit(this);
    command_->setObjectName(QStringLiteral("consoleCommand"));
    command_->setPlaceholderText(tr("Command (type help)"));
    connect(command_, &QLineEdit::returnPressed, this, [this] {
        const QString text = command_->text().trimmed();
        command_->clear();
        if (!text.isEmpty()) run(text);
    });
    connect(&ConsoleLog::instance(), &ConsoleLog::lineAdded, this, [this](const QString& line) {
        log_->appendPlainText(line);
    });
    connect(&ConsoleLog::instance(), &ConsoleLog::cleared, log_, &QPlainTextEdit::clear);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(log_, 1);
    layout->addWidget(command_);
}

void ConsoleWindow::run(const QString& command) {
    ConsoleLog& log = ConsoleLog::instance();
    log.append(QStringLiteral("> ") + command);
    const QString name = command.section(QLatin1Char(' '), 0, 0).toLower();
    if (name == QLatin1String("help")) {
        log.append(tr("Commands: help, clear, sysinfo, version."));
    } else if (name == QLatin1String("clear")) {
        log.clear();
    } else if (name == QLatin1String("sysinfo")) {
        for (const QString& line : ConsoleLog::systemInfo()) log.append(line);
    } else if (name == QLatin1String("version")) {
        log.append(ConsoleLog::systemInfo().constFirst());
    } else {
        log.append(tr("Unknown command: %1. Type help for a list.").arg(name));
    }
}

} // namespace lumenis::ui
