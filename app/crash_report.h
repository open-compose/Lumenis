#pragma once

// Crash reports (ROADMAP 10.5; docs/spec/crash-reports.md). The operating systems' own means, no crash reporting
// library (the owner's decision, 2026-11-14):
//   * Windows: the exception, the faulting address and the call stack (functions and lines from cutthulhu.pdb next to
//     the executable, else module + offset) on stderr and in a text report, and a minidump (DbgHelp's
//     MiniDumpWriteDump) beside it.
//   * Linux and macOS: a signal handler writes the signal, the faulting address and the backtrace (and on Linux the
//     process's memory map, so the addresses can be turned into lines later) to stderr and the text report, then lets
//     the signal take its usual course: a core file where they are enabled, and on macOS the system's crash report
//     (~/Library/Logs/DiagnosticReports).
// Nothing leaves the computer. At the next start the main window says where the report is (app/main.cpp).
// Sanitizer builds (ASan, TSan) and LUMENIS_NO_CRASH_HANDLER=1 keep the sanitizer's or the system's handling.

#include <QString>
#include <QStringList>
#include <QUrl>

// Installs the handlers; called once, first thing in main(). Until setCrashReportFolder() reports go to stderr only.
void installCrashReport();

// From now on reports are also written into <folder> as crash-<date>-<time>-<pid>.txt (and .dmp on Windows). The
// folder is made and the names and the report's opening lines (product, version, system) prepared now, because the
// handler cannot allocate memory.
void setCrashReportFolder(const QString& folder);

// LUMENIS_CRASH_DIR, else "Crash Reports" in the application's local data folder.
QString defaultCrashReportFolder();

// The text reports in <folder> not yet shown to the user, newest first.
QStringList unseenCrashReports(const QString& folder);

// Remembers every report now in <folder> as shown (in <folder>/shown.txt).
void markCrashReportsShown(const QString& folder);

// On macOS, the system's crash report written for the same crash (in ~/Library/Logs/DiagnosticReports, named after
// the executable, no more than two minutes after <report>); empty elsewhere or when there is none.
QString systemCrashReportFor(const QString& report);

// A new GitHub issue for the report at <homepage>/issues/new (the owner's decision, ROADMAP 10.5), filled in: a title
// naming what happened, and a body with the report's opening lines, what happened and the backtrace, without the
// memory map and with the home folder's path written "~" (no user name). Opened in the browser, it is the user who
// reviews and submits it, and attaches the minidump if they choose; nothing is sent by the application. Kept under
// GitHub's URL length.
QUrl crashIssueUrl(const QString& report, const QString& homepage);

// Crashes on purpose, for the crash report's test (--crash-test): "segv" reads address 0, "abort" calls abort(),
// "exception" throws a std::runtime_error nothing catches.
[[noreturn]] void crashForTest(const QString& kind);
