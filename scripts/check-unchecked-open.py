#!/usr/bin/env python3
"""Opened files are checked: Qt 6.10 declares QFile::open (and QSaveFile's, QBuffer's) [[nodiscard]], so a statement
that opens a device and drops the result breaks the newest Ubuntu's build (GCC 15, Qt 6.10, warnings as errors) while
the Qt 6.4 most of us build with says nothing. Qt 6.4 cannot catch it, so this does, in src, app, tests and fuzz: a
bare `<name>.open(QIODevice::...);` statement (also `->`, QIODeviceBase, QFile) is a finding, listed as file:line. Use
the result (`if (!f.open(...))`, QVERIFY), or say on the line, in a comment, why it cannot fail. Run from anywhere; exit status 1 on a finding. ctest unchecked_open runs it.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# A trailing comment saying why the result does not matter (a read-only QBuffer cannot fail) lets the line through.
BARE = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_]*(\.|->)open\((QIODevice|QIODeviceBase|QFile)::[^;]*\);\s*$")


def findings():
    out = []
    for folder in ("src", "app", "tests", "fuzz"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.suffix not in (".cpp", ".h") or not path.is_file():
                continue
            for i, line in enumerate(path.read_text(encoding="utf-8", errors="replace").split("\n")):
                if BARE.match(line):
                    out.append(f"{path.relative_to(ROOT)}:{i + 1}: {line.strip()[:120]}")
    return out


def main():
    found = findings()
    for f in found:
        print(f)
    if found:
        print(f"unchecked open: {len(found)} open() result(s) dropped; check them (Qt 6.10 makes it an error)")
        return 1
    print("unchecked open: every open() result is used")
    return 0


if __name__ == "__main__":
    sys.exit(main())
