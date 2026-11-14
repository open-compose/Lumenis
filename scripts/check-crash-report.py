#!/usr/bin/env python3
"""Checks the crash reports (ROADMAP 10.5; app/crash_report.h, docs/spec/crash-reports.md): ctest crash_report.

Crashes the application on purpose three ways (cutthulhu --crash-test segv|abort|exception) with LUMENIS_CRASH_DIR set
to a temporary folder, and checks for each crash:
  * that the process still ended as a crash (not a clean exit), so the system's own handling (core file, macOS crash
    report, Windows Error Reporting) is kept;
  * that a text report was written with the product's opening lines and what happened, and on Windows a minidump
    beside it ("MDMP");
then that --crash-reports lists the three, newest first.

    python3 scripts/check-crash-report.py --app build/ci/bin/cutthulhu
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time

KINDS = {
    # kind: what the report says on Windows, and elsewhere
    "segv": ("access violation", "SIGSEGV"),
    "abort": ("(abort)", "SIGABRT"),
    "exception": ("uncaught exception: crash test", "uncaught exception: crash test"),
}


def no_core_files():
    # A crash on purpose needs no core file.
    try:
        import resource
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    except (ImportError, ValueError, OSError):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--app", required=True)
    args = parser.parse_args()
    windows = sys.platform.startswith("win")
    failures = []
    with tempfile.TemporaryDirectory() as folder:
        env = dict(os.environ, LUMENIS_CRASH_DIR=folder)
        env.pop("LUMENIS_NO_CRASH_HANDLER", None)
        for kind, (on_windows, elsewhere) in KINDS.items():
            before = set(os.listdir(folder))
            run = subprocess.run([args.app, "--crash-test", kind], env=env, capture_output=True, text=True,
                                 errors="replace", timeout=120, preexec_fn=None if windows else no_core_files)
            if run.returncode == 0:
                failures.append(f"{kind}: the process exited cleanly (code 0)")
            reports = sorted(f for f in set(os.listdir(folder)) - before if f.endswith(".txt"))
            if len(reports) != 1:
                failures.append(f"{kind}: {len(reports)} new reports, expected 1; stderr:\n{run.stderr}")
                continue
            text = open(os.path.join(folder, reports[0]), encoding="utf-8", errors="replace").read()
            expected = on_windows if windows else elsewhere
            for needed in ("crash report\n", "System: ", "Backtrace:", expected):
                if needed not in text:
                    failures.append(f"{kind}: the report lacks {needed!r}:\n{text[:2000]}")
            if windows:
                dump = os.path.join(folder, reports[0][:-4] + ".dmp")
                if not os.path.exists(dump) or open(dump, "rb").read(4) != b"MDMP":
                    failures.append(f"{kind}: no minidump {dump}")
            print(f"crash report: {kind}: {reports[0]} ({len(text.splitlines())} lines)")
            # The next report's name differs by its start time (to the second) or its process id.
            time.sleep(1.1)
        listed = subprocess.run([args.app, "--crash-reports"], env=env, capture_output=True, text=True,
                                errors="replace", timeout=120).stdout.split()
        reports = [line for line in listed if line.endswith(".txt")]
        if len(reports) != 3:
            failures.append(f"--crash-reports listed {len(reports)} reports, expected 3: {listed}")
        elif [os.path.getmtime(r) for r in reports] != sorted((os.path.getmtime(r) for r in reports), reverse=True):
            failures.append(f"--crash-reports is not newest first: {reports}")
    for f in failures:
        print("FAIL: " + f)
    if failures:
        return 1
    print("crash reports: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
