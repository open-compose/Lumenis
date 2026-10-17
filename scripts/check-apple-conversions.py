#!/usr/bin/env python3
"""Compile every source as Apple Clang on arm64 sees long double.

On Apple arm64, long double is a plain 64-bit double, so Clang's
-Wimplicit-int-float-conversion (part of -Wconversion) flags int64 to long
double conversions that x86 Linux Clang, with its 80-bit long double, lets
pass. This syntax-checks every file of a Clang build with -mlong-double-64
so such errors show up before a push instead of on the Mac.

    check-apple-conversions.py [build/clang]

Needs a Clang build directory with compile_commands.json (the presets
export it). Prints the files with errors; exit status 1 if there are any.
"""
import json
import shlex
import subprocess
import sys
from pathlib import Path


def main() -> int:
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build/clang")
    db = build / "compile_commands.json"
    if not db.exists():
        print(f"{db} not found: configure a Clang build first", file=sys.stderr)
        return 2
    bad = 0
    entries = json.loads(db.read_text())
    for e in entries:
        if "_autogen" in e["file"]:
            continue
        args = shlex.split(e["command"]) if "command" in e else list(e["arguments"])
        if "clang" not in Path(args[0]).name:
            print(f"{db} is not a Clang build", file=sys.stderr)
            return 2
        if "-o" in args:
            i = args.index("-o")
            del args[i:i + 2]
        r = subprocess.run(args + ["-mlong-double-64", "-fsyntax-only"], cwd=e["directory"],
                           capture_output=True, text=True)
        if r.returncode:
            bad += 1
            print(e["file"])
            print("\n".join(line for line in r.stderr.splitlines() if "error" in line))
    print(f"{bad} of {len(entries)} files with errors")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
