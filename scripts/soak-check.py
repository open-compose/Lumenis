#!/usr/bin/env python3
"""Checks a soak log (ROADMAP 10.2; docs/spec/soak.md) for growth.

`cutthulhu --soak <hours> --soak-log soak.jsonl` writes one JSON line a minute: the process's resident memory,
handles, threads, widgets and windows, and the frames played and dropped. This script fits a straight line to each
figure after the warm-up (caches filling, the first renders) and fails when one keeps growing:

    python3 scripts/soak-check.py soak.jsonl
    python3 scripts/soak-check.py soak.jsonl --json          # the figures as JSON, for CI
    python3 scripts/soak-check.py soak.jsonl --app build/ci/bin/cutthulhu --hours 72    # run the soak first

`--smoke` checks only that the log is whole, that cycles ran and that nothing stalled (ctest `soak_smoke`: a minute
is too short to judge growth).

A figure fails when its line rises by more than its allowance per hour AND its last quarter is above its second
quarter (so a cache that filled and stayed full passes, while a steady leak fails). The allowances are options.
A "stalled" line (no cycle for ten minutes) always fails; dialogs the soak had to close are listed.
"""

import argparse
import json
import os
import subprocess
import sys

# Allowed growth per hour after the warm-up.
DEFAULTS = {
    "residentMb": 32.0,
    "heapMb": 32.0,
    "handles": 4.0,
    "threads": 2.0,
    "widgets": 20.0,
    "topLevels": 2.0,
    "binItems": 2.0,
    "gdiObjects": 20.0,
    "userObjects": 20.0,
}


def read(path):
    header, samples, notes = None, [], []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                # The last line of a run that was killed can be cut short.
                notes.append({"badLine": n})
                continue
            if "soak" in row:
                header = row["soak"]
            elif "t" in row:
                samples.append(row)
            else:
                notes.append(row)
    return header, samples, notes


def slope(xs, ys):
    """Least-squares slope of ys against xs."""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx


def mean(values):
    return sum(values) / len(values) if values else 0.0


def check(samples, allowances, warmup):
    """Each figure's growth per hour and whether it passes."""
    results = {}
    if not samples:
        return results
    end = samples[-1]["t"]
    # The warm-up: the given seconds, but never more than a fifth of the run.
    start = min(warmup, end / 5.0)
    kept = [s for s in samples if s["t"] >= start]
    for key, allowed in allowances.items():
        points = [(s["t"], s[key]) for s in kept if isinstance(s.get(key), (int, float)) and s[key] >= 0]
        if len(points) < 4:
            continue  # not given on this system (GDI objects off Windows), or too short a run
        xs = [p[0] / 3600.0 for p in points]
        ys = [float(p[1]) for p in points]
        per_hour = slope(xs, ys)
        q = len(ys) // 4
        second, last = mean(ys[q:2 * q]), mean(ys[3 * q:])
        growing = per_hour > allowed and last > second
        results[key] = {
            "first": ys[0],
            "last": ys[-1],
            "perHour": round(per_hour, 2),
            "allowedPerHour": allowed,
            "secondQuarter": round(second, 2),
            "lastQuarter": round(last, 2),
            "ok": not growing,
        }
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log")
    parser.add_argument("--warmup", type=float, default=600.0, help="seconds left out at the start (default 600)")
    parser.add_argument("--json", action="store_true", help="print the results as JSON")
    parser.add_argument("--min-cycles", type=int, default=1, help="cycles the run must have done (default 1)")
    parser.add_argument("--app", help="run this cutthulhu with --soak first, writing the log")
    parser.add_argument("--hours", type=float, default=1.0, help="with --app: how long (default 1)")
    parser.add_argument("--interval", type=int, default=60, help="with --app: seconds between lines (default 60)")
    parser.add_argument("--cycles", type=int, default=0, help="with --app: also end after this many cycles")
    parser.add_argument("--smoke", action="store_true", help="check the log's form, cycles and stalls, not growth")
    for key, allowed in DEFAULTS.items():
        parser.add_argument("--max-" + key, type=float, default=allowed, dest=key,
                            help=f"allowed growth of {key} per hour (default {allowed:g})")
    args = parser.parse_args()

    if args.app:
        command = [args.app, "--soak", str(args.hours), "--soak-interval", str(args.interval), "--soak-log", args.log]
        if args.cycles > 0:
            command += ["--soak-cycles", str(args.cycles)]
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        run = subprocess.run(command, env=env, timeout=args.hours * 3600 + 900)
        if run.returncode != 0:
            print(f"soak: {args.app} exited with {run.returncode}")
            return 1
    header, samples, notes = read(args.log)
    allowances = {key: getattr(args, key) for key in DEFAULTS}
    results = {} if args.smoke else check(samples, allowances, args.warmup)
    stalled = [n for n in notes if "stalled" in n]
    dialogs = [n["closedDialog"] for n in notes if "closedDialog" in n]
    cycles = samples[-1].get("cycles", 0) if samples else 0
    shown = samples[-1].get("shown", 0) if samples else 0
    dropped = samples[-1].get("dropped", 0) if samples else 0
    # Where the heap in use is known (Linux, macOS), it is what a leak shows in: resident memory also counts what the
    # allocator keeps after it was freed (glibc's per-thread arenas, macOS's zones), which can rise in steps as threads
    # come and go and then stay. Resident memory is then shown but not judged.
    if "heapMb" in results and "residentMb" in results:
        results["residentMb"]["ok"] = True
        results["residentMb"]["note"] = "not judged: heapMb is"
    problems = [key for key, r in results.items() if not r["ok"]]
    if stalled:
        problems.append("stalled")
    if cycles < args.min_cycles:
        problems.append("cycles")
    if not samples:
        problems.append("no samples")
    if header is None:
        problems.append("no header")
    expected = ("t", "residentMb", "handles", "threads", "widgets", "topLevels", "cycles", "shown", "dropped")
    if samples and any(key not in samples[-1] for key in expected):
        problems.append("fields")
    if any("badLine" in n for n in notes[:-1]):
        problems.append("bad lines")  # only the last line may be cut short (a killed run)
    # How the run ended: its closing line ({"ended": {"code": ...}}, written since 2026-11-17), or, in a log without one,
    # whether the samples reach the hours asked for. A run that crashed, was killed or lost its computer stops without
    # it; the owner's first Linux soak stopped at 64.6 of 72 hours and passed unnoticed.
    ended = next((n["ended"] for n in notes if "ended" in n), None)
    stoppedEarly = None
    if ended is not None:
        if ended.get("code", 0) != 0 and not stalled:
            problems.append("ended")
    elif args.app:
        problems.append("no closing line")  # the app run here writes one (ctest soak_smoke)
    # Failures the application reported (reportFailure), each with its step, cycle and time; the closing line counts
    # them all, the log keeps the first 500. Any failure fails the run: a soak of what an editor does should fail at
    # nothing (the owner's first 72-hour runs ended with a write failure that no log had).
    failures = [n for n in notes if "failure" in n]
    failureCount = max(len(failures), (ended or {}).get("failures", 0))
    if failureCount:
        problems.append("failures")
    elif header and samples and not header.get("cycles"):
        wanted = float(header.get("hours", 0)) * 3600.0
        interval = float(header.get("interval", 60))
        if wanted > 0 and samples[-1]["t"] < wanted - 2 * interval:
            stoppedEarly = (samples[-1]["t"] / 3600.0, wanted / 3600.0)
            problems.append("stopped early")
    summary = {
        "header": header,
        "hours": round(samples[-1]["t"] / 3600.0, 3) if samples else 0,
        "cycles": cycles,
        "shown": shown,
        "dropped": dropped,
        "droppedShare": round(dropped / (shown + dropped), 4) if shown + dropped else 0.0,
        "closedDialogs": sorted(set(dialogs)),
        "figures": results,
        "ended": ended,
        "failures": failureCount,
        "problems": problems,
    }
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"{summary['hours']} hours, {cycles} cycles; frames shown {shown}, dropped {dropped} "
              f"({summary['droppedShare'] * 100:.1f}%)")
        for key, r in results.items():
            mark = "ok  " if r["ok"] else "FAIL"
            note = f"; {r['note']}" if "note" in r else ""
            print(f"  {mark} {key:12} {r['first']:10.1f} -> {r['last']:10.1f}   {r['perHour']:+9.2f}/hour "
                  f"(allowed {r['allowedPerHour']:g}; quarters {r['secondQuarter']:.1f} -> {r['lastQuarter']:.1f}{note})")
        if dialogs:
            print("  dialogs the soak closed: " + ", ".join(sorted(set(dialogs))))
        if stoppedEarly:
            print(f"  FAIL stopped at {stoppedEarly[0]:.1f} of {stoppedEarly[1]:g} hours without its closing line: it crashed, "
                  "was killed or its computer stopped (look in Crash Reports, BUILDING.md \"Crash reports\")")
        if ended is not None and "ended" in problems:
            why = f" ({ended['why']})" if ended.get("why") else ""
            print(f"  FAIL ended with exit code {ended.get('code')}{why}")
        if failureCount:
            print(f"  FAIL {failureCount} failures reported; by text (count, first at hours, step, cycle):")
            first = {}
            for n in failures:
                entry = first.setdefault(n["failure"], [0, n.get("seconds", 0) / 3600.0, n.get("step"), n.get("cycle")])
                entry[0] += 1
            for text, (count, at, step, cycle) in sorted(first.items(), key=lambda kv: kv[1][1]):
                print(f"       {count:5} x  {at:7.2f} h  step {step}  cycle {cycle}  {text}")
        if stalled:
            print("  FAIL stalled at step " + ", ".join(str(n["stalled"]) for n in stalled))
        if "cycles" in problems:
            print(f"  FAIL only {cycles} cycles (at least {args.min_cycles})")
    if problems and not args.json:
        print("soak: FAIL (" + ", ".join(problems) + ")")
    elif not args.json:
        print("soak: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
