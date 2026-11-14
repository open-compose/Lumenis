#include "crash_report.h"

// Crash reports (crash_report.h; docs/spec/crash-reports.md). The handler half runs when the process has already
// failed: on Linux and macOS it uses only async-signal-safe calls on buffers prepared beforehand
// (setCrashReportFolder); on Windows the unhandled-exception filter may use DbgHelp, as before 10.5. The Qt half
// (finding, listing and marking reports) runs in a healthy process.

#include "lumenis/core/branding.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

// Sanitizers install their own handlers, whose reports say more than ours.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LUMENIS_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define LUMENIS_SANITIZED 1
#endif
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h.
#include <dbghelp.h>

#include <csignal>
#include <cstdarg>
#else
#include <csignal>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

// What the handler writes, prepared by setCrashReportFolder(): the report's path (empty: stderr only) and its
// opening lines. Fixed buffers, since the handler must not allocate.
char g_reportPath[4096] = {};
char g_header[2048] = {};
std::size_t g_headerLength = 0;
// The uncaught exception's what(), set by the terminate handler before it aborts.
char g_uncaught[512] = {};

bool handlerDisabled() {
#if defined(LUMENIS_SANITIZED)
    return true;
#else
    // Qt's reading of the environment, not getenv (which MSVC warns about); it works before the application exists.
    return qEnvironmentVariableIsSet("LUMENIS_NO_CRASH_HANDLER") && qgetenv("LUMENIS_NO_CRASH_HANDLER") != "0";
#endif
}

// std::terminate (an exception nothing caught): keeps what() for the report, then aborts, which the handlers report.
[[noreturn]] void onTerminate() {
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            std::snprintf(g_uncaught, sizeof g_uncaught, "%s", e.what());
        } catch (...) {
            std::snprintf(g_uncaught, sizeof g_uncaught, "%s", "(not a std::exception)");
        }
        std::fprintf(stderr, "%s: uncaught exception: %s\n", lumenis::branding::kExecutableName, g_uncaught);
    }
    std::abort();
}

} // namespace

#if defined(_WIN32)

namespace {

// The filter there before ours: the C++ runtime's, which turns an uncaught C++ exception into std::terminate().
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
wchar_t g_reportPathW[4096] = {};
wchar_t g_dumpPathW[4096] = {};
HANDLE g_reportFile = INVALID_HANDLE_VALUE;

// A short name for the common exception codes (0xC0000374 and 0xC0000409 have no named constant
// in every SDK; 0x40000015 is STATUS_FATAL_APP_EXIT, our abort()).
const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "division by zero";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun";
    case 0x40000015: return "abort";
    default: return "exception";
    }
}

// A line to stderr and to the text report.
void out(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n <= 0) return;
    const DWORD length = static_cast<DWORD>(std::min<std::size_t>(static_cast<std::size_t>(n), sizeof line - 1));
    std::fwrite(line, 1, length, stderr);
    if (g_reportFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_reportFile, line, length, &written, nullptr);
    }
}

// The minidump beside the text report: the threads, their stacks and the memory they point to, enough for a debugger
// with cutthulhu.pdb.
void writeMinidump(EXCEPTION_POINTERS* info) {
    if (!g_dumpPathW[0]) return;
    const HANDLE file = CreateFileW(g_dumpPathW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION exception = {};
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpWithUnloadedModules);
    if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr, nullptr)) {
        out("Minidump: %ls\n", g_dumpPathW);
    }
    CloseHandle(file);
}

// The unhandled-exception filter: the exception, the faulting address and up to 48 stack frames to stderr and the
// text report, the minidump, then Windows carries on as it would have (EXCEPTION_CONTINUE_SEARCH). Only the first
// crash is reported, should a second thread crash meanwhile.
LONG WINAPI report(EXCEPTION_POINTERS* info) {
    // An uncaught C++ exception (MSVC's code 0xE06D7363) goes to the runtime's filter, which calls std::terminate():
    // onTerminate keeps its message and aborts, and onAbort reports that, message included.
    constexpr DWORD kCppException = 0xE06D7363;
    if (info->ExceptionRecord->ExceptionCode == kCppException && g_previousFilter) return g_previousFilter(info);
    static LONG reported = 0;
    if (InterlockedExchange(&reported, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
    if (g_reportPathW[0]) {
        g_reportFile = CreateFileW(g_reportPathW, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_reportFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(g_reportFile, g_header, static_cast<DWORD>(g_headerLength), &written, nullptr);
        }
    }
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    out("%s crashed: 0x%08lX (%s) at %p, thread %lu\n", lumenis::branding::kExecutableName,
        static_cast<unsigned long>(record->ExceptionCode), exceptionName(record->ExceptionCode),
        record->ExceptionAddress, static_cast<unsigned long>(GetCurrentThreadId()));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        out("  %s address %p\n", record->ExceptionInformation[0] == 0 ? "reading" : "writing",
            reinterpret_cast<void*>(record->ExceptionInformation[1]));
    }
    if (g_uncaught[0]) out("  uncaught exception: %s\n", g_uncaught);
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    // The executable's folder first, for cutthulhu.pdb.
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (char* slash = std::strrchr(exe, '\\')) *slash = '\0';
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    const bool symbols = SymInitialize(process, exe, TRUE) != FALSE;
// StackWalk64 starts from the faulting thread's registers, copied: it changes the context.
#if defined(_M_X64)
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Pc;
    frame.AddrStack.Offset = context.Sp;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#endif
#if defined(_M_X64) || defined(_M_ARM64)
    out("Backtrace:\n");
    for (int depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        // Each frame as module!function +offset (file:line) when symbols are found, else module +offset.
        const DWORD64 address = frame.AddrPC.Offset;
        char moduleName[MAX_PATH] = "?";
        const DWORD64 base = SymGetModuleBase64(process, address);
        if (base) {
            GetModuleFileNameA(reinterpret_cast<HMODULE>(base), moduleName, MAX_PATH);
            if (const char* slash = std::strrchr(moduleName, '\\')) std::memmove(moduleName, slash + 1, std::strlen(slash));
        }
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (symbols && SymFromAddr(process, address, &displacement, symbol)) {
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
                out("  #%d %s!%s +0x%llx (%s:%lu)\n", depth, moduleName, symbol->Name,
                    static_cast<unsigned long long>(displacement), line.FileName,
                    static_cast<unsigned long>(line.LineNumber));
            } else {
                out("  #%d %s!%s +0x%llx\n", depth, moduleName, symbol->Name,
                    static_cast<unsigned long long>(displacement));
            }
        } else {
            out("  #%d %s +0x%llx\n", depth, moduleName, static_cast<unsigned long long>(base ? address - base : address));
        }
    }
#endif
    writeMinidump(info);
    std::fflush(stderr);
    if (g_reportFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_reportFile);
        g_reportFile = INVALID_HANDLE_VALUE;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// abort() (qFatal, std::terminate) raises SIGABRT, which never reaches the exception filter: the same report from
// here, with the registers of this point as the stack's start.
void onAbort(int) {
    CONTEXT context = {};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = 0x40000015; // STATUS_FATAL_APP_EXIT
#if defined(_M_X64)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
#elif defined(_M_ARM64)
    record.ExceptionAddress = reinterpret_cast<void*>(context.Pc);
#endif
    EXCEPTION_POINTERS pointers = {&record, &context};
    report(&pointers);
}

} // namespace

void installCrashReport() {
    std::set_terminate(onTerminate);
    if (handlerDisabled()) return;
    g_previousFilter = SetUnhandledExceptionFilter(report);
    std::signal(SIGABRT, onAbort);
    // No "abort() has been called" box or Windows Error Reporting dialog: the report is ours.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

#else

namespace {

// Async-signal-safe output: write(2) only, numbers formatted by hand.
void writeAll(int fd, const char* text, std::size_t length) {
    while (length > 0) {
        const ssize_t n = ::write(fd, text, length);
        if (n <= 0) return;
        text += n;
        length -= static_cast<std::size_t>(n);
    }
}
void writeText(int fd, const char* text) { writeAll(fd, text, std::strlen(text)); }
void writeNumber(int fd, unsigned long long value, unsigned base) {
    char digits[32];
    std::size_t i = sizeof digits;
    do {
        digits[--i] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value > 0 && i > 0);
    if (base == 16) writeText(fd, "0x");
    writeAll(fd, digits + i, sizeof digits - i);
}

const char* signalName(int signal) {
    switch (signal) {
    case SIGSEGV: return "SIGSEGV, invalid memory access";
    case SIGBUS: return "SIGBUS, bad memory access";
    case SIGFPE: return "SIGFPE, arithmetic error";
    case SIGILL: return "SIGILL, illegal instruction";
    case SIGABRT: return "SIGABRT, abort";
    case SIGTRAP: return "SIGTRAP, trap";
    default: return "signal";
    }
}

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP};
// Its own stack, so a stack overflow can still be reported (on the thread that installed it, the main thread).
alignas(16) char g_alternateStack[1 << 16];

void report(int fd, int signal, const siginfo_t* info, void** frames, int frameCount) {
    if (fd != STDERR_FILENO) writeAll(fd, g_header, g_headerLength);
    writeText(fd, lumenis::branding::kExecutableName);
    writeText(fd, " crashed: signal ");
    writeNumber(fd, static_cast<unsigned long long>(signal), 10);
    writeText(fd, " (");
    writeText(fd, signalName(signal));
    writeText(fd, ")");
    if (signal == SIGSEGV || signal == SIGBUS || signal == SIGFPE || signal == SIGILL) {
        writeText(fd, " at address ");
        writeNumber(fd, reinterpret_cast<std::uintptr_t>(info->si_addr), 16);
    }
    writeText(fd, "\n");
    if (g_uncaught[0]) {
        writeText(fd, "  uncaught exception: ");
        writeText(fd, g_uncaught);
        writeText(fd, "\n");
    }
    writeText(fd, "Backtrace:\n");
    backtrace_symbols_fd(frames, frameCount, fd);
#if defined(__linux__)
    // Where each library was loaded, to turn the addresses into functions and lines later (BUILDING.md, "Crash
    // reports"); only into the report, it is long.
    if (fd != STDERR_FILENO) {
        writeText(fd, "Memory map:\n");
        const int maps = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
        if (maps >= 0) {
            char buffer[4096];
            ssize_t n = 0;
            while ((n = ::read(maps, buffer, sizeof buffer)) > 0) writeAll(fd, buffer, static_cast<std::size_t>(n));
            ::close(maps);
        }
    }
#endif
}

void onSignal(int signal, siginfo_t* info, void*) {
    static volatile sig_atomic_t entered = 0;
    if (!entered) {
        entered = 1;
        void* frames[64];
        const int frameCount = backtrace(frames, 64);
        report(STDERR_FILENO, signal, info, frames, frameCount);
        if (g_reportPath[0]) {
            const int fd = ::open(g_reportPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (fd >= 0) {
                report(fd, signal, info, frames, frameCount);
                ::close(fd);
                writeText(STDERR_FILENO, "Crash report: ");
                writeText(STDERR_FILENO, g_reportPath);
                writeText(STDERR_FILENO, "\n");
            }
        }
    }
    // The usual course: the default action once the handler returns (the signal stays blocked until then), with a
    // core file where they are enabled and, on macOS, the system's crash report.
    struct sigaction usual = {};
    usual.sa_handler = SIG_DFL;
    sigemptyset(&usual.sa_mask);
    sigaction(signal, &usual, nullptr);
    raise(signal);
}

} // namespace

void installCrashReport() {
    std::set_terminate(onTerminate);
    if (handlerDisabled()) return;
    // backtrace() loads its unwinder on first use, which allocates: once here, not in the handler.
    void* frame[1];
    backtrace(frame, 1);
    stack_t stack = {};
    stack.ss_sp = g_alternateStack;
    stack.ss_size = sizeof g_alternateStack;
    sigaltstack(&stack, nullptr);
    struct sigaction action = {};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : kSignals) sigaction(signal, &action, nullptr);
}

#endif

void setCrashReportFolder(const QString& folder) {
    if (folder.isEmpty() || !QDir().mkpath(folder)) return;
    const QDateTime started = QDateTime::currentDateTime();
    const QString base = QDir(folder).filePath(QStringLiteral("crash-%1-%2")
                                                   .arg(started.toString(QStringLiteral("yyyyMMdd-HHmmss")))
                                                   .arg(QCoreApplication::applicationPid()));
    const QByteArray report = QFile::encodeName(QDir::toNativeSeparators(base + QStringLiteral(".txt")));
    std::snprintf(g_reportPath, sizeof g_reportPath, "%s", report.constData());
#if defined(_WIN32)
    const std::wstring reportW = QDir::toNativeSeparators(base + QStringLiteral(".txt")).toStdWString();
    const std::wstring dumpW = QDir::toNativeSeparators(base + QStringLiteral(".dmp")).toStdWString();
    wcsncpy_s(g_reportPathW, reportW.c_str(), _TRUNCATE);
    wcsncpy_s(g_dumpPathW, dumpW.c_str(), _TRUNCATE);
#endif
    // The opening lines: what crashed, on what. No user data (no file names, no command line).
    const QByteArray header = QStringLiteral("%1 %2 crash report\nSystem: %3 (%4)\nQt: %5\nStarted: %6\n\n")
                                  .arg(QString::fromUtf8(lumenis::branding::kProductName),
                                       QString::fromUtf8(lumenis::branding::kVersion), QSysInfo::prettyProductName(),
                                       QSysInfo::currentCpuArchitecture(), QString::fromLatin1(qVersion()),
                                       started.toString(Qt::ISODate))
                                  .toUtf8();
    g_headerLength = std::min<std::size_t>(static_cast<std::size_t>(header.size()), sizeof g_header - 1);
    std::memcpy(g_header, header.constData(), g_headerLength);
}

QString defaultCrashReportFolder() {
    const QString given = qEnvironmentVariable("LUMENIS_CRASH_DIR");
    if (!given.isEmpty()) return given;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("Crash Reports"));
}

namespace {

QString shownList(const QString& folder) { return QDir(folder).filePath(QStringLiteral("shown.txt")); }

QSet<QString> shownReports(const QString& folder) {
    QSet<QString> names;
    QFile file(shownList(folder));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (!line.isEmpty()) names.insert(line);
        }
    }
    return names;
}

QFileInfoList reportFiles(const QString& folder) {
    return QDir(folder).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
}

} // namespace

QStringList unseenCrashReports(const QString& folder) {
    const QSet<QString> shown = shownReports(folder);
    QStringList out;
    for (const QFileInfo& f : reportFiles(folder)) {
        if (!shown.contains(f.fileName())) out << f.absoluteFilePath();
    }
    return out;
}

void markCrashReportsShown(const QString& folder) {
    QSet<QString> shown = shownReports(folder);
    for (const QFileInfo& f : reportFiles(folder)) shown.insert(f.fileName());
    QFile file(shownList(folder));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QStringList names(shown.begin(), shown.end());
    names.sort();
    QTextStream(&file) << names.join(QLatin1Char('\n')) << '\n';
}

QString systemCrashReportFor(const QString& report) {
#if defined(Q_OS_MACOS)
    const QDateTime when = QFileInfo(report).lastModified();
    const QDir reports(QDir::home().filePath(QStringLiteral("Library/Logs/DiagnosticReports")));
    const QString prefix = QString::fromUtf8(lumenis::branding::kExecutableName);
    for (const QFileInfo& f : reports.entryInfoList({prefix + QStringLiteral("*")}, QDir::Files, QDir::Time)) {
        const qint64 seconds = when.secsTo(f.lastModified());
        if (seconds >= -10 && seconds <= 120) return f.absoluteFilePath();
    }
#else
    Q_UNUSED(report);
#endif
    return {};
}

QUrl crashIssueUrl(const QString& report, const QString& homepage) {
    QFile file(report);
    QString text;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) text = QString::fromUtf8(file.readAll());
    // Not the memory map: long, and full of paths.
    const qsizetype map = text.indexOf(QLatin1String("\nMemory map:"));
    if (map >= 0) text.truncate(map);
    // The home folder as "~", in either separator, so no user name goes into the issue.
    const QString home = QDir::homePath();
    if (!home.isEmpty() && home != QLatin1String("/")) {
        text.replace(home, QStringLiteral("~"), Qt::CaseInsensitive);
        text.replace(QDir::toNativeSeparators(home), QStringLiteral("~"), Qt::CaseInsensitive);
    }
    // The title: the line saying what happened ("cutthulhu crashed: signal 11 (SIGSEGV, ...)").
    QString what;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String(" crashed: "))) {
            what = line.mid(line.indexOf(QLatin1String(" crashed: ")) + 10).trimmed();
            break;
        }
    }
    // GitHub refuses very long addresses; the backtrace's first lines are the useful ones.
    constexpr qsizetype kMaxReport = 5000;
    if (text.size() > kMaxReport) text = text.left(kMaxReport) + QStringLiteral("\n...");
    const bool windows = QFileInfo::exists(QFileInfo(report).path() + QLatin1Char('/') +
                                           QFileInfo(report).completeBaseName() + QStringLiteral(".dmp"));
    QString body = QStringLiteral("**What I was doing when it crashed:**\n\n\n**The crash report** (%1):\n\n```\n%2\n```\n")
                       .arg(QFileInfo(report).fileName(), text.trimmed());
    if (windows) {
        body += QStringLiteral("\nThe minidump beside the report (%1.dmp) helps most: drag it here if you are happy to share it "
                               "(it holds the program's memory at the crash).\n")
                    .arg(QFileInfo(report).completeBaseName());
    }
    // Each value percent-encoded in full: QUrlQuery leaves "+" as it is, which GitHub reads as a space (the
    // backtrace's offsets are "+0x...").
    const QString title = QStringLiteral("Crash: %1").arg(what.isEmpty() ? QStringLiteral("unknown") : what);
    QUrl url(homepage + QStringLiteral("/issues/new"));
    url.setQuery(QStringLiteral("title=") + QString::fromLatin1(QUrl::toPercentEncoding(title)) + QStringLiteral("&body=") +
                     QString::fromLatin1(QUrl::toPercentEncoding(body)),
                 QUrl::StrictMode);
    return url;
}

void crashForTest(const QString& kind) {
#if defined(_WIN32)
    // No Windows Error Reporting dialog waiting for a click on a test machine.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (kind == QLatin1String("abort")) std::abort();
    if (kind == QLatin1String("exception")) throw std::runtime_error("crash test");
    // "segv": a write to address 0 through a volatile access, which the compiler may neither drop nor turn into a trap.
    volatile std::uintptr_t address = 0;
    *reinterpret_cast<volatile int*>(address) = 1;
    std::abort();
}
