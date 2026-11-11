#include "lumenis/ui/tool_windows.h"

#include "lumenis/core/branding.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/ui/theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QGridLayout>
#include <QKeyEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMutexLocker>
#include <QListWidget>
#include <QProgressBar>
#include <QStorageInfo>
#include <QTabWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QPlainTextEdit>
#include <QSysInfo>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>

// Small tool windows (tool_windows.h): the Calculator, the Console log and its window, and the
// Configuration, Usage and Hardware tabs of Help > About. MainWindow (main_window.cpp) makes them and
// puts the Calculator and Console in panels; the Calculator's arithmetic is src/model/calculator.*.

namespace lumenis::ui {

// ---------------------------------------------------------------- CalculatorWindow

// The Format menu, a black display in timecode green, and a 4x4 grid of keys. The keys take no focus,
// so typing keeps going to the window.
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

// Unknown keys change nothing but still refresh the display.
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

// The handler installed before ours, which ours passes every message on to. Function statics, so they
// exist whenever the handler runs, even during static destruction.
QtMessageHandler& previousHandler() {
    static QtMessageHandler previous = nullptr;
    return previous;
}

bool& handlerInstalled() {
    static bool installed = false;
    return installed;
}

// Qt's message handler: warnings, criticals and fatals also go to the Console log; everything goes on
// to the previous handler, or to stderr as Qt would print it.
void consoleHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type != QtDebugMsg && type != QtInfoMsg) {
        const char* kind = type == QtWarningMsg ? "Warning" : type == QtCriticalMsg ? "Error" : "Fatal";
        // Console only: Qt's own warnings and errors, collected for the Console (p.101).
        ConsoleLog::instance().append(QStringLiteral("%1: %2").arg(QString::fromLatin1(kind), message));
    }
    if (previousHandler()) {
        previousHandler()(type, context, message);
    } else {
        std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
    }
}

} // namespace

ConsoleLog::ConsoleLog() : lines_(systemInfo()) {}

// A static: it goes after main() returns, while Qt can still log (its own
// statics, threads stopping). Messages go back to the handler from before.
ConsoleLog::~ConsoleLog() {
    if (handlerInstalled()) {
        qInstallMessageHandler(previousHandler());
        handlerInstalled() = false;
    }
}

ConsoleLog& ConsoleLog::instance() {
    static ConsoleLog log;
    return log;
}

void ConsoleLog::installMessageHandler() {
    if (handlerInstalled()) return;
    handlerInstalled() = true;
    // Made now, so it is destroyed (and the handler removed) after anything
    // made later that might log while it goes.
    instance();
    previousHandler() = qInstallMessageHandler(consoleHandler);
}

// Qt's message handler calls this on whatever thread logged, so lines_ is only touched under the
// mutex. The signal is emitted on the calling thread; receivers that live on another thread (the
// Console windows, on the UI thread) get it queued.
void ConsoleLog::append(const QString& line) {
    {
        const QMutexLocker lock(&mutex_);
        lines_ << line;
        // Keep the log bounded.
        if (lines_.size() > 5000) lines_.remove(0, lines_.size() - 5000);
    }
    emit lineAdded(line);
}

namespace {
FailureObserver& failureObserver() {
    static FailureObserver observer;
    return observer;
}
} // namespace

void setFailureObserver(FailureObserver observer) {
    failureObserver() = std::move(observer);
}

void reportFailure(QWidget* parent, const QString& line) {
    ConsoleLog::instance().append(line); // Console only: the failure's own Console line, shown below as well
    // "Title: text"; a title is short and has no sentence in it.
    const qsizetype colon = line.indexOf(QLatin1String(": "));
    const bool titled = colon > 0 && colon <= 60 && !line.left(colon).contains(QLatin1String(". "));
    const QString title = titled ? line.left(colon) : QString::fromUtf8(branding::kProductName);
    const QString text = titled ? line.mid(colon + 2) : line;
    showFailure(parent, title, text);
}

void showFailure(QWidget* parent, const QString& title, const QString& text) {
    if (failureObserver()) {
        failureObserver()(title, text);
        return;
    }
    // The title is the box's main text as well as its window title: macOS shows no window title on message boxes
    // (Apple's guidelines), and the user must still see what failed. The details are the informative text. Open boxes
    // are found by the "failureTitle" property, which is the same on every platform.
    QWidget* owner = parent ? parent->window() : QApplication::activeWindow();
    if (owner) {
        for (QMessageBox* open : owner->findChildren<QMessageBox*>(QStringLiteral("failureMessage"), Qt::FindDirectChildrenOnly)) {
            if (open->isVisible() && open->property("failureTitle").toString() == title) {
                open->setInformativeText(open->informativeText() + QLatin1Char('\n') + text);
                return;
            }
        }
    }
    auto* box = new QMessageBox(QMessageBox::Warning, title, title, QMessageBox::Ok, owner);
    box->setObjectName(QStringLiteral("failureMessage"));
    box->setProperty("failureTitle", title);
    box->setInformativeText(text);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

QStringList ConsoleLog::lines() const {
    const QMutexLocker lock(&mutex_);
    return lines_;
}

void ConsoleLog::clear() {
    {
        const QMutexLocker lock(&mutex_);
        lines_.clear();
    }
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

// The log so far, then each line as it is added; the command line runs run() on Return.
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
    // `this` as the context: lines logged on other threads arrive queued, on the window's thread.
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
        log.append(tr("Commands: help, clear, sysinfo, version, HDTitleFilter, closeallofxplugins."));
    } else if (name == QLatin1String("clear")) {
        log.clear();
    } else if (name == QLatin1String("sysinfo")) {
        for (const QString& line : ConsoleLog::systemInfo()) log.append(line);
    } else if (name == QLatin1String("version")) {
        log.append(ConsoleLog::systemInfo().constFirst());
    } else if (name == QLatin1String("hdtitlefilter")) {
        // "Enter HDTitleFilter. This command works as a toggle, so the filter remains on until you type the command
        // again" (FX Guide p.376).
        media::setHdTitleFilter(!media::hdTitleFilter());
        log.append(media::hdTitleFilter() ? tr("HD title filter on.") : tr("HD title filter off."));
    } else if (name == QLatin1String("closeallofxplugins") || name == QLatin1String("closeallavxplugins")) {
        // "Type closeallavxplugins ... Media Composer closes all open AVX 1.x plug-ins. The AVX plug-ins automatically
        // open again as needed" (FX Guide p.158); ours for OFX plug-ins, under either name.
        const int closed = media::closeOfxInstances();
        log.append(tr("Closed %n OFX plug-in instance(s).", nullptr, closed));
    } else {
        log.append(tr("Unknown command: %1. Type help for a list.").arg(name));
    }
}

// ---------------------------------------------------------------- AboutInfoDialog

// Builds the three tabs from labels and values; nothing in them can be edited. Values we do not have
// yet ("GPU", the bytes used) are shown as such.
AboutInfoDialog::AboutInfoDialog(const QStringList& plugins, const ProjectUsage& usage, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("aboutInfo"));
    setWindowTitle(tr("About %1").arg(QString::fromUtf8(branding::kProductName)));
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("aboutTabs"));

    // Configuration (p.104-105): version, product options, licensing type,
    // GPU and the installed plug-ins.
    auto* config = new QWidget;
    auto* cl = new QVBoxLayout(config);
    auto* form = new QGridLayout;
    const std::pair<QString, QString> rows[] = {
        {tr("Version"), QString::fromUtf8(branding::kVersion)},
        {tr("Product Options"), tr("None")},
        {tr("Licensing Type"), tr("GNU General Public License v3")},
        {tr("GPU"), tr("Not reported yet")},
        {tr("System"), QSysInfo::prettyProductName()},
    };
    int r = 0;
    for (const auto& [label, value] : rows) {
        form->addWidget(new QLabel(label, config), r, 0);
        auto* v = new QLabel(value, config);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addWidget(v, r++, 1);
    }
    cl->addLayout(form);
    cl->addWidget(new QLabel(tr("Installed plug-ins:"), config));
    auto* list = new QListWidget(config);
    list->setObjectName(QStringLiteral("aboutPlugins"));
    list->addItems(plugins);
    cl->addWidget(list, 1);
    config->setObjectName(QStringLiteral("Configuration"));
    tabs_->addTab(config, tr("Configuration"));

    // Usage (p.102-104): the project's statistics.
    auto* use = new QWidget;
    auto* ul = new QGridLayout(use);
    const qint64 h = usage.secondsOpen / 3600;
    const qint64 m = (usage.secondsOpen / 60) % 60;
    const std::pair<QString, QString> stats[] = {
        {tr("Project"), usage.project.isEmpty() ? tr("No project open") : usage.project},
        {tr("Time Project open"), QStringLiteral("%1:%2").arg(h).arg(m, 2, 10, QLatin1Char('0'))},
        {tr("Bins"), QString::number(usage.bins)},
        {tr("Clips"), QString::number(usage.clips)},
        {tr("Sequences"), QString::number(usage.sequences)},
        {tr("Captured Media bytes used"), tr("0")},
        {tr("Rendered Effects bytes used"), tr("0")},
    };
    r = 0;
    for (const auto& [label, value] : stats) {
        ul->addWidget(new QLabel(label, use), r, 0);
        auto* v = new QLabel(value, use);
        v->setObjectName(QStringLiteral("usage:") + label);
        ul->addWidget(v, r++, 1);
    }
    ul->setRowStretch(r, 1);
    use->setObjectName(QStringLiteral("Usage"));
    tabs_->addTab(use, tr("Usage"));

    // Hardware (p.102): a Drives tab with a bar of each online drive's
    // filled space and its free space, and a System tab.
    hardware_ = new QTabWidget;
    hardware_->setObjectName(QStringLiteral("hardwareTabs"));
    auto* drives = new QWidget;
    auto* dl = new QGridLayout(drives);
    r = 0;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.bytesTotal() <= 0) continue;
        const QString name = volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName();
        dl->addWidget(new QLabel(name, drives), r, 0);
        auto* bar = new QProgressBar(drives);
        bar->setObjectName(QStringLiteral("drive"));
        bar->setRange(0, 1000);
        const double used = 1.0 - static_cast<double>(volume.bytesAvailable()) / static_cast<double>(volume.bytesTotal());
        bar->setValue(static_cast<int>(used * 1000));
        // The number is the free space (p.102).
        bar->setFormat(freeSpaceText(volume.bytesAvailable()));
        dl->addWidget(bar, r++, 1);
    }
    dl->setRowStretch(r, 1);
    hardware_->addTab(drives, tr("Drives"));
    auto* system = new QWidget;
    auto* sl = new QGridLayout(system);
    const std::pair<QString, QString> sys[] = {
        {tr("Operating System"), QSysInfo::prettyProductName()},
        {tr("Version"), QSysInfo::productVersion()},
        {tr("Kernel"), QSysInfo::kernelVersion()},
        {tr("Architecture"), QSysInfo::currentCpuArchitecture()},
    };
    r = 0;
    for (const auto& [label, value] : sys) {
        sl->addWidget(new QLabel(label, system), r, 0);
        sl->addWidget(new QLabel(value, system), r++, 1);
    }
    sl->setRowStretch(r, 1);
    hardware_->addTab(system, tr("System"));
    tabs_->addTab(hardware_, tr("Hardware"));
    // hardwareTabs keeps its own object name; showTab() maps "Hardware" to it.

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_, 1);
    layout->addWidget(buttons);
    resize(520, 420);
}

// Matches the page's object name, which is not translated; the tab's (translated) text is
// accepted as well, for callers that pass what the user sees.
void AboutInfoDialog::showTab(const QString& name) {
    for (int i = 0; i < tabs_->count(); ++i) {
        const QWidget* page = tabs_->widget(i);
        const QString key = page == hardware_ ? QStringLiteral("Hardware") : page->objectName();
        if (key == name || tabs_->tabText(i) == name) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }
}

// Decimal gigabytes (10^9 bytes), as macOS's Finder and drive makers count them; the guide does not
// say how Media Composer counts.
QString freeSpaceText(qint64 bytes) {
    return QCoreApplication::translate("lumenis::ui", "%1 GB free")
        .arg(QString::number(static_cast<double>(std::max<qint64>(bytes, 0)) / 1e9, 'f', 1));
}

} // namespace lumenis::ui
