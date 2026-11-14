# Crash reports (10.5)

Ours: the guides say nothing about crashes. The ROADMAP first named Crashpad; no package manager ships it and it builds
only with Chromium's tools, so the owner chose the operating systems' own means instead (2026-11-14). Nothing leaves
the computer: sending reports waits on the owner's decision about collecting them, and then on the user's consent.

## What is written

`app/crash_report.*`, installed first thing in `main()`. Reports go to stderr from the start and, once the application
has its name (`setCrashReportFolder`), also into the reports folder:

* the folder: `LUMENIS_CRASH_DIR`, else "Crash Reports" in the application's local data folder
  (`QStandardPaths::AppLocalDataLocation`: `~/.local/share/<organisation>/cutthulhu` on Linux,
  `~/Library/Application Support/...` on macOS, `%LOCALAPPDATA%\...` on Windows);
* `crash-<date>-<time>-<pid>.txt`: the product and version, the system, Qt, the start time (no file names, no command
  line), then what happened (the signal or exception and the faulting address, an uncaught exception's message) and
  the backtrace;
  * Windows: functions and lines from `cutthulhu.pdb` next to the executable, through DbgHelp, and a minidump
    `crash-....dmp` beside the report (`MiniDumpWriteDump`: threads, stacks and the memory they point to);
  * Linux: `backtrace_symbols_fd` (module + offset) and the process's memory map, so the offsets can be turned into
    functions and lines later (BUILDING.md, "Crash reports");
  * macOS: the same backtrace; the system's own crash report (`~/Library/Logs/DiagnosticReports/cutthulhu-*.ips`), with
    every thread symbolicated, is written as usual and named in the notice (`systemCrashReportFor`).
* After the report the crash takes its usual course: the process ends with the signal or exception (a core file where
  they are enabled, Windows Error Reporting, the macOS report).

What is caught: SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT and SIGTRAP (POSIX, on an alternate stack, so a stack
overflow on the main thread is caught too); unhandled exceptions and `abort()` (qFatal, `std::terminate`) on Windows;
an uncaught C++ exception everywhere (`std::set_terminate` keeps its message, then aborts; on Windows the exception
first reaches the unhandled-exception filter as code 0xE06D7363, which ours hands to the C++ runtime's filter so that
`std::terminate` runs). Windows' fail-fast heap
corruption (0xc0000374) ends the process without running the filter; Windows Error Reporting has it.

The POSIX handler uses only async-signal-safe calls on buffers prepared beforehand (the path and the opening lines);
`backtrace()` is called once at start-up so its first use in the handler does not allocate. Sanitizer builds (ASan,
TSan) and `LUMENIS_NO_CRASH_HANDLER=1` keep the sanitizer's or the system's handling.

## At the next start

A normal start (not the tooling modes) with reports not yet shown opens a message that does not block: "Cutthulhu
closed unexpectedly last time ...", the newest report's path (and on macOS the system's), how many earlier ones there
are, and Show Folder. The reports are then remembered as shown in `shown.txt` in the folder. `cutthulhu
--crash-reports` lists the ones not yet shown, newest first.

## Tests

ctest `crash_report` (`scripts/check-crash-report.py`): `cutthulhu --crash-test segv|abort|exception` (a hidden
option) into a temporary folder; each ends as a crash, writes one report with its opening lines and what happened
(and on Windows a minidump starting "MDMP"), and `--crash-reports` lists the three, newest first. Not in the sanitizer
builds.

## Parts

* 10.5a, the reports and the notice above, is done.
* 10.5b: symbols for the release builds (the PDB is installed already; Linux debug files and macOS dSYMs kept with
  each package) and a script that turns a Linux report's offsets into functions and lines.
