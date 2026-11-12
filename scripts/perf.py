#!/usr/bin/env python3
"""The performance check (ROADMAP 10.1; docs/spec/performance.md).

Runs the benchmarks (tests/perf/bench.cpp, `lumenis_bench`) and compares each median time with the time recorded for
this machine in tests/data/perf_baseline.json:

    perf.py --bench build/ci/bin/lumenis_bench               compare; fail when one is slower than allowed
    perf.py --bench ... --record                             write this machine's times as its baseline
    perf.py --bench ... --machine mac-m1                      the machine's name in the baseline (default: host name)
    perf.py --bench ... --only compose_blur_1080p,bin_sort_10k

A benchmark fails when its median is more than `margin` (the baseline's, 30% unless set) slower than the recorded
median and also more than `floor_ms` (2 ms) slower, so tiny times do not fail on noise. Faster times are reported, and
a time 30% faster says to record again so the baseline keeps up. A machine with no baseline only reports (exit 0):
times differ too much between machines to compare one with another's.

Exit codes: 0 passed or only reported, 1 slower than allowed, 2 the benchmarks failed to run.
"""

import argparse
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tests" / "data" / "perf_baseline.json"


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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench", required=True, help="the lumenis_bench executable")
    ap.add_argument("--machine", default=os.environ.get("CUTTHULHU_PERF_MACHINE") or socket.gethostname())
    ap.add_argument("--record", action="store_true", help="write this machine's times as its baseline")
    ap.add_argument("--only", default="", help="comma-separated benchmark names")
    args = ap.parse_args()

    times = run(args.bench, args.only)
    if times is None:
        return 2
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    machines = baseline.setdefault("machines", {})

    if args.record:
        entry = machines.setdefault(args.machine, {})
        entry["machine"] = times.get("machine", {})
        recorded = entry.setdefault("benchmarks", {})
        for name, t in times["benchmarks"].items():
            recorded[name] = round(t["median_ms"], 2)
        BASELINE.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"perf: recorded {len(times['benchmarks'])} times for {args.machine} in {BASELINE.relative_to(ROOT)}")
        return 0

    entry = machines.get(args.machine)
    if not entry:
        print(f"perf: no baseline for machine '{args.machine}' in {BASELINE.relative_to(ROOT)}; only reporting "
              f"(record one with --record)")
        return 0
    margin = baseline.get("margin", 0.30)
    floor = baseline.get("floor_ms", 2.0)
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
        elif now < was * (1 - margin) and was - now > floor:
            mark = "  faster: record again"
            faster.append(name)
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
