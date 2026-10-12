#!/usr/bin/env python3
"""Compare two widget layouts written by `--screenshot` (*.layout.json).

    scripts/layoutdiff.py A.layout.json B.layout.json [--tolerance 1]

Lists widgets that exist on only one side, and widgets whose position or
size differs by more than --tolerance pixels. Exits 0 when the layouts
match, 1 otherwise. Used by CI to explain screenshot differences between
platforms in plain text.
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as f:
        return {w["path"]: w for w in json.load(f)}


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--tolerance", type=int, default=1)
    p.add_argument("--max-lines", type=int, default=25)
    args = p.parse_args()
    a, b = load(args.a), load(args.b)
    problems = []
    for path in a.keys() - b.keys():
        problems.append(f"only in {args.a}: {path}")
    for path in b.keys() - a.keys():
        problems.append(f"only in {args.b}: {path}")
    for path in a.keys() & b.keys():
        wa, wb = a[path], b[path]
        delta = {k: wb[k] - wa[k] for k in ("x", "y", "w", "h") if abs(wb[k] - wa[k]) > args.tolerance}
        if delta:
            moved = " ".join(f"{k}{v:+d}" for k, v in delta.items())
            problems.append(f"{moved:24} {path}  ({wa['w']}x{wa['h']}@{wa['x']},{wa['y']})")
    # Size changes first: they usually cause the moves.
    problems.sort(key=lambda s: (" w" not in s and " h" not in s and not s.startswith(("w", "h")), s))
    name = args.a.rsplit("/", 1)[-1]
    if not problems:
        print(f"layoutdiff: {name}: identical")
        return 0
    print(f"layoutdiff: {name}: {len(problems)} differences")
    for line in problems[: args.max_lines]:
        print("   ", line)
    if len(problems) > args.max_lines:
        print(f"    ... and {len(problems) - args.max_lines} more")
    return 1


if __name__ == "__main__":
    sys.exit(main())
