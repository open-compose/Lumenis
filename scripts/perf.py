#!/usr/bin/env python3
"""The performance check (ROADMAP 10.1; docs/spec/performance.md).

Runs the benchmarks (tests/perf/bench.cpp, `lumenis_bench`) and compares each median time with the time recorded for
this machine in tests/data/perf_baseline.json:

    perf.py --bench build/ci/bin/lumenis_bench               compare; fail when one is slower than allowed
    perf.py --bench ... --record                             write this machine's times as its baseline
    perf.py --bench ... --machine mac-m1                      the machine's name in the baseline (default: host name)
    perf.py --bench ... --only compose_blur_1080p,bin_sort_10k
    perf.py --bench ... --repeat 3 --print-times              three runs (each benchmark's median of them), and the
                                                             times in the log between "perf-times" lines (CI, 10.1c)
    perf.py --record-from a.json b.json ... --machine github-ubuntu-24.04 --margin 1.0
                                                             record from several runs' times (the hosted runners)

A benchmark fails when its median is more than the margin slower than the recorded median and also more than
`floor_ms` slower (the machine's own where its entry has one, 10 ms on the hosted runners, else 2 ms), so tiny
times do not fail on noise. The margin is the machine's own when its entry has one
(the hosted runners', whose hosts differ from run to run), else the baseline's (30%). Faster times are reported, and a
time that much faster says to record again so the baseline keeps up. A machine with no baseline only reports (exit 0):
times differ too much between machines to compare one with another's. Under GitHub Actions each slower or much faster
benchmark is also an annotation on the run.

Exit codes: 0 passed or only reported, 1 slower than allowed, 2 the benchmarks failed to run.
"""

import argparse
import json
import os
import pathlib
import socket
import statistics
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tests" / "data" / "perf_baseline.json"
# The lines around the times printed with --print-times, which --record-from also reads out of a saved CI log.
BEGIN, END = "perf-times-begin", "perf-times-end"


def run(bench, only):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "times.json")
        cmd = [bench, "--json", out]
        if only:
            cmd += ["--only", only]
        env = dict(os.environ)
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
        done = subprocess.run(cmd, env=env)
        if done.returncode != 0 or not os.path.exists(out):
            print(f"perf: {bench} failed (exit status {done.returncode})")
            return None
        with open(out, encoding="utf-8") as f:
            return json.load(f)


def combined(runs):
    """One set of times from several runs: each benchmark's median of the runs' medians."""
    names = sorted({name for r in runs for name in r["benchmarks"]})
    out = {"machine": runs[-1].get("machine", {}), "runs": sum(r.get("runs", 1) for r in runs), "benchmarks": {}}
    for name in names:
        medians = [r["benchmarks"][name]["median_ms"] for r in runs if name in r["benchmarks"]]
        out["benchmarks"][name] = {"median_ms": statistics.median(medians), "of_runs": medians}
    return out


def read_times(path):
    """Times from a JSON file lumenis_bench or --print-times wrote, or from a CI log holding --print-times' lines."""
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    if BEGIN in text:
        found = []
        for block in text.split(BEGIN)[1:]:
            body = block.split(END)[0]
            # Lines in a downloaded log start with a timestamp; the JSON is what follows it on each line.
            lines = [line.split(" ", 1)[1] if line[:4].isdigit() and " " in line else line for line in body.splitlines()]
            found.append(json.loads("\n".join(lines)))
        return found
    return [json.loads(text)]


def annotate(kind, text):
    if os.environ.get("GITHUB_ACTIONS") == "true":
        print(f"::{kind} title=Performance::{text}")


def record(baseline, machine, times, margin, floor=None):
    machines = baseline.setdefault("machines", {})
    entry = machines.setdefault(machine, {})
    entry["machine"] = times.get("machine", {})
    if times.get("runs"):
        entry["runs"] = times["runs"]
    if margin is not None:
        entry["margin"] = margin
    if floor is not None:
        entry["floor_ms"] = floor
    recorded = entry.setdefault("benchmarks", {})
    for name, t in times["benchmarks"].items():
        recorded[name] = round(t["median_ms"], 2)
    BASELINE.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"perf: recorded {len(times['benchmarks'])} times for {machine} in {BASELINE.relative_to(ROOT)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", help="the lumenis_bench executable")
    ap.add_argument("--machine", default=os.environ.get("CUTTHULHU_PERF_MACHINE") or socket.gethostname())
    ap.add_argument("--record", action="store_true", help="write this machine's times as its baseline")
    ap.add_argument("--record-from", nargs="+", metavar="FILE",
                    help="record this machine's baseline from saved times (JSON files or CI logs with --print-times)")
    ap.add_argument("--margin", type=float, help="with --record or --record-from: the machine's own margin (1.0: 100%%)")
    ap.add_argument("--floor", type=float, help="with --record or --record-from: the machine's own floor in ms")
    ap.add_argument("--only", default="", help="comma-separated benchmark names")
    ap.add_argument("--repeat", type=int, default=1, help="run the benchmarks this many times (default 1)")
    ap.add_argument("--print-times", action="store_true", help="print the times as JSON between perf-times lines")
    args = ap.parse_args()

    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    if args.record_from:
        runs = [t for path in args.record_from for t in read_times(path)]
        if not runs:
            print("perf: no times in " + ", ".join(args.record_from))
            return 2
        record(baseline, args.machine, combined(runs), args.margin, args.floor)
        return 0
    if not args.bench:
        ap.error("--bench is needed unless --record-from is given")

    runs = []
    for _ in range(max(args.repeat, 1)):
        times = run(args.bench, args.only)
        if times is None:
            return 2
        runs.append(times)
    times = combined(runs) if len(runs) > 1 else runs[0]
    if args.print_times:
        print(BEGIN)
        print(json.dumps(times, indent=1, sort_keys=True))
        print(END)

    if args.record:
        record(baseline, args.machine, times, args.margin, args.floor)
        return 0

    entry = baseline.get("machines", {}).get(args.machine)
    if not entry:
        print(f"perf: no baseline for machine '{args.machine}' in {BASELINE.relative_to(ROOT)}; only reporting "
              f"(record one with --record)")
        return 0
    margin = entry.get("margin", baseline.get("margin", 0.30))
    # The machine's own floor where it has one: on the hosted runners a benchmark of a few milliseconds varies by more
    # than 2 ms between runs of one job (macOS: bin_show_10k at 6.2 and 14.3 ms, 2026-11-17).
    floor = entry.get("floor_ms", baseline.get("floor_ms", 2.0))
    slower, faster = [], []
    for name, t in sorted(times["benchmarks"].items()):
        was = entry["benchmarks"].get(name)
        now = t["median_ms"]
        if was is None:
            print(f"  {name:32} {now:9.2f} ms (new: no recorded time)")
            continue
        change = (now - was) / was if was > 0 else 0.0
        mark = ""
        if now > was * (1 + margin) and now - was > floor:
            mark = "  SLOWER than allowed"
            slower.append(name)
            annotate("error", f"{name} took {now:.1f} ms on {args.machine}, recorded {was:.1f} ms ({change:+.0%})")
        elif now < was / (1 + margin) and was - now > floor:
            mark = "  faster: record again"
            faster.append(name)
            annotate("notice", f"{name} took {now:.1f} ms on {args.machine}, recorded {was:.1f} ms ({change:+.0%}): "
                               "record again")
        print(f"  {name:32} {now:9.2f} ms, recorded {was:9.2f} ms ({change:+.0%}){mark}")
    if faster:
        print(f"perf: {len(faster)} benchmark(s) much faster than recorded; run with --record to keep the baseline current")
    if slower:
        print(f"perf: {len(slower)} benchmark(s) more than {margin:.0%} slower than recorded on {args.machine}: "
              + ", ".join(slower))
        return 1
    print(f"perf: every benchmark within {margin:.0%} of {args.machine}'s recorded times")
    return 0


if __name__ == "__main__":
    sys.exit(main())
