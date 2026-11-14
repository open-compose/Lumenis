#!/usr/bin/env python3
"""Turns a Linux crash report's backtrace into functions and lines (ROADMAP 10.5b; docs/spec/crash-reports.md).

A report (app/crash_report.cpp) lists each frame as module(+offset)[address] or module(function+offset)[address] and
ends with the process's memory map. For every frame this finds the module's load address in the map, and asks
addr2line (binutils) for the function, file and line at the address relative to it, inlined calls included.

    python3 scripts/symbolize-crash.py crash-20261114-020403-477.txt
    python3 scripts/symbolize-crash.py report.txt --search /opt/cutthulhu/bin    # where the same build's files are

The module files must be the same build as the one that crashed (the packages ship cutthulhu with its debug
information). A module not found, or without debug information, is shown as in the report.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

FRAME = re.compile(r"^(?P<module>[^(\s][^(]*)\((?P<symbol>[^)]*)\)\s*\[(?P<address>0x[0-9a-fA-F]+)\]")
MAPPING = re.compile(r"^(?P<start>[0-9a-f]+)-(?P<end>[0-9a-f]+)\s+\S+\s+(?P<offset>[0-9a-f]+)\s+\S+\s+\d+\s+(?P<path>/.*)$")


def parse(text):
    """The frames (module, symbol, address) and the mappings (start, end, offset, path) of a report."""
    frames, mappings, section = [], [], None
    for line in text.splitlines():
        if line.startswith("Backtrace:"):
            section = "frames"
            continue
        if line.startswith("Memory map:"):
            section = "map"
            continue
        if section == "frames":
            m = FRAME.match(line.strip())
            if m:
                frames.append((m["module"], m["symbol"], int(m["address"], 16)))
        elif section == "map":
            m = MAPPING.match(line.strip())
            if m:
                mappings.append((int(m["start"], 16), int(m["end"], 16), int(m["offset"], 16), m["path"]))
    return frames, mappings


def load_address(path, address, mappings):
    """Where the module holding <address> was loaded: its mapping at file offset 0."""
    for start, end, _, mapped in mappings:
        if start <= address < end:
            path = mapped
            break
    bases = [start for start, _, offset, mapped in mappings if mapped == path and offset == 0]
    return (path, min(bases)) if bases else (path, None)


def find_file(path, search):
    if os.path.isfile(path):
        return path
    # A folder may hold the module itself or its debug information alone (<name>.debug, as CI keeps beside each
    # AppImage: objcopy --only-keep-debug), with the build's date and commit in the name.
    for folder in search:
        name = os.path.basename(path)
        for candidate in [os.path.join(folder, name), os.path.join(folder, name + ".debug")] + sorted(
                os.path.join(folder, f) for f in (os.listdir(folder) if os.path.isdir(folder) else [])
                if f.startswith(name + ".debug") and not f.endswith(".gz")):
            if os.path.isfile(candidate):
                return candidate
    return None


def addr2line(addr2line_tool, binary, offsets):
    """{offset: [(function, file:line), ...]} with inlined frames, innermost first."""
    run = subprocess.run([addr2line_tool, "-C", "-f", "-i", "-a", "-e", binary] + [hex(o) for o in offsets],
                         capture_output=True, text=True, errors="replace")
    result, current = {}, None
    lines = run.stdout.splitlines()
    i = 0
    while i < len(lines):
        if lines[i].startswith("0x"):
            current = int(lines[i], 16)
            result[current] = []
            i += 1
            continue
        if current is not None and i + 1 < len(lines):
            result[current].append((lines[i], lines[i + 1]))
            i += 2
        else:
            i += 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("report")
    parser.add_argument("--search", action="append", default=[],
                        help="a folder with the same build's modules, where the report's paths are not found")
    args = parser.parse_args()
    tool = shutil.which("addr2line")
    if not tool:
        sys.exit("symbolize-crash: addr2line not found (install binutils)")
    text = open(args.report, encoding="utf-8", errors="replace").read()
    frames, mappings = parse(text)
    if not frames:
        sys.exit("symbolize-crash: no backtrace in the report")
    # Each frame's module file and offset, then one addr2line run a module.
    placed = []
    wanted = {}
    for module, symbol, address in frames:
        path, base = load_address(module, address, mappings)
        file = find_file(path, args.search)
        offset = address - base if base is not None else None
        if offset is None and symbol.startswith("+0x"):
            offset = int(symbol[1:], 16)  # a report without the memory map: the offset backtrace gave
        placed.append((module, symbol, address, file, offset))
        if file and offset is not None:
            # The return address: the call is the byte before it.
            wanted.setdefault(file, set()).add(max(offset - 1, 0))
    found = {file: addr2line(tool, file, sorted(offsets)) for file, offsets in wanted.items()}
    for n, (module, symbol, address, file, offset) in enumerate(placed):
        names = found.get(file, {}).get(max(offset - 1, 0)) if file and offset is not None else None
        shown = os.path.basename(module)
        if names and names[0][0] != "??":
            first = True
            for function, where in names:
                prefix = f"#{n:<3}" if first else "    "
                known = not (where.startswith("??") or where.startswith(":"))
                print(f"{prefix} {function} at {where} ({shown})" if known else f"{prefix} {function} ({shown})")
                first = False
        else:
            print(f"#{n:<3} {symbol or '?'} ({shown}, {hex(address)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
