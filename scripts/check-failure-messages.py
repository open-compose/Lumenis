#!/usr/bin/env python3
"""Error messages everywhere (ROADMAP 9.7; the owner's request, 2026-11-08): a failure tells the user in a message, not
only in the Console.

Every Console line written in src/ui goes through ui::reportFailure() (src/ui/tool_windows.h), which also shows it in
a warning, or is marked as information with a comment holding "Console only" on its line or the line before, with the
reason. This fails, listing file:line, for a ConsoleLog::instance().append( that is neither, so a new failure that
only reaches the Console is caught. Run from anywhere; exit status 1 on a finding. ctest failure_messages runs it.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
APPEND = re.compile(r"ConsoleLog::instance\(\)\.append\(")


def findings():
    out = []
    for path in sorted((ROOT / "src" / "ui").rglob("*.cpp")):
        lines = path.read_text(encoding="utf-8").split("\n")
        for i, line in enumerate(lines):
            if not APPEND.search(line):
                continue
            near = line + (lines[i - 1] if i > 0 else "")
            if "Console only" not in near:
                out.append(f"{path.relative_to(ROOT)}:{i + 1}: {line.strip()[:120]}")
    return out


def main():
    found = findings()
    for f in found:
        print(f)
    if found:
        print(f"failure messages: {len(found)} Console line(s) neither through reportFailure() nor marked "
              "\"Console only\" (scripts/check-failure-messages.py)")
        return 1
    print("failure messages: every Console line is a message or marked Console only")
    return 0


if __name__ == "__main__":
    sys.exit(main())
