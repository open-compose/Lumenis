#!/usr/bin/env python3
"""Fuzzing the interchange parsers (ROADMAP Phase 3 gate 4; fuzz/README.md, BUILDING.md "Fuzzing").

  fuzz.py build                 configure and build the `fuzz` preset (Clang, libFuzzer, ASan, UBSan)
  fuzz.py seed                  make the seed corpora in build/fuzz/corpus/<target>/
  fuzz.py run --time 30m        fuzz every target, the time split between them
  fuzz.py replay PATH...        run saved inputs through every target (no libFuzzer needed)

Seeds come from three places: the interchange corpus (scripts/interop-corpus.py downloads pyaaf2's
and pyavb's test files, MIT; they are not committed), the small inputs our own writers make
(lumenis_fuzz_replay --make-seeds) and the regression inputs in fuzz/regressions/. With a fuzz
build, `seed` keeps only the inputs that add coverage (libFuzzer's -merge=1).

`run` stops at the first crash, hang (-timeout) or sanitizer report, prints the reproducer as base64
(the CI job uploads nothing) and exits 1. With --keep-going it fuzzes every target for its whole
time instead, keeping each failing input, then groups them into problems by their stacks (a
problem is the first frame in our code and the kind of failure) and prints each problem's stack and
smallest reproducer, and exits 1 if there were any. Each target's statistics (executions,
coverage, corpus) are printed at the end, and written as JSON with --stats.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ["cfb", "aaf", "avb", "avb_rewrite", "otio", "fcpxml", "pict", "scc", "lut", "psd"]
# Which corpus files seed which target.
EXTENSIONS = {"cfb": (".aaf",), "aaf": (".aaf",), "avb": (".avb",), "avb_rewrite": (".avb",), "otio": (".otio",), "fcpxml": (".xml",), "pict": (".pct", ".pict"), "scc": (".scc",), "lut": (".cube", ".3dl", ".lut", ".itx"), "psd": (".psd",)}
# The largest input the fuzzer makes or keeps (-max_len), and the largest corpus file used as a
# seed: libFuzzer mutates small inputs far faster. AAF files are compound files of a few hundred
# kilobytes, so `seed` also lays each out again smaller; bins are a few tens of kilobytes.
MAX_LEN = {"cfb": 262144, "aaf": 262144, "avb": 131072, "avb_rewrite": 131072, "otio": 131072, "fcpxml": 131072, "pict": 65536, "scc": 65536, "lut": 262144, "psd": 131072}
# How a total time is split between the targets: the AAF and bin parsers (and their imports) have
# the most code.
WEIGHTS = {"cfb": 1, "aaf": 3, "avb": 3, "avb_rewrite": 2, "otio": 2, "fcpxml": 2, "pict": 1, "scc": 1, "lut": 1, "psd": 1}


def default_corpus() -> Path:
    env = os.environ.get("CUTTHULHU_INTEROP_CORPUS")
    if env:
        return Path(env)
    # The user's cache folder, where tests/interop/CMakeLists.txt keeps it, then older build folders.
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        cache = Path(os.environ["LOCALAPPDATA"])
    elif sys.platform == "darwin":
        cache = Path.home() / "Library" / "Caches"
    else:
        cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    if (cache / "cutthulhu" / "interop-corpus").is_dir():
        return cache / "cutthulhu" / "interop-corpus"
    for build in ("ci", "clang", "asan", "debug", "release"):
        p = ROOT / "build" / build / "interop-corpus"
        if p.is_dir():
            return p
    return ROOT / "build" / "ci" / "interop-corpus"


def parse_duration(text: str) -> int:
    """Seconds from "90", "90s", "30m", "2h" or "1d"."""
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([smhd]?)", text.strip())
    if not m:
        raise argparse.ArgumentTypeError(f"not a duration: {text}")
    return int(float(m.group(1)) * {"": 1, "s": 1, "m": 60, "h": 3600, "d": 86400}[m.group(2)])


def binary(build: Path, name: str) -> Path:
    exe = build / "bin" / (name + (".exe" if os.name == "nt" else ""))
    if not exe.exists():
        sys.exit(f"{exe} is missing: run `scripts/fuzz.py build` first")
    return exe


def env() -> dict:
    e = dict(os.environ)
    e.setdefault("QT_QPA_PLATFORM", "offscreen")
    # Leaks are not the gate's concern, and Qt's caches look like them at exit.
    e.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    e.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    return e


def cmd_build(args) -> int:
    subprocess.run(["cmake", "--preset", "fuzz"], cwd=ROOT, check=True)
    subprocess.run(["cmake", "--build", "--preset", "fuzz"], cwd=ROOT, check=True)
    # The fuzz build preset lists its targets by name: one left out there builds nothing for it.
    missing = [t for t in TARGETS if not (ROOT / "build" / "fuzz" / "bin" / f"fuzz_{t}").exists()]
    if missing:
        print(f"fuzz: no fuzz_{', fuzz_'.join(missing)} after the build: add them to the fuzz build "
              "preset's targets in CMakePresets.json", file=sys.stderr)
        return 1
    return 0


def cmd_seed(args) -> int:
    out = Path(args.out)
    build = Path(args.build)
    corpus = Path(args.corpus)
    out.mkdir(parents=True, exist_ok=True)
    raw = Path(tempfile.mkdtemp(prefix="seed-", dir=out.parent))
    try:
        # The built-in seeds, our writers' output (needs the replay driver of any build).
        replay = build / "bin" / ("lumenis_fuzz_replay" + (".exe" if os.name == "nt" else ""))
        if replay.exists():
            # The built-in seeds, and each corpus AAF laid out smaller (512-byte sectors, and without
            # its metadictionary), which mutates far faster than the original.
            aafs = sorted(str(f) for f in corpus.rglob("*.aaf")) if corpus.is_dir() else []
            subprocess.run([str(replay), "--make-seeds", str(raw)] + aafs, check=True, env=env())
        else:
            print(f"no {replay}: built-in seeds left out", file=sys.stderr)
        for t in TARGETS:
            d = raw / t
            d.mkdir(parents=True, exist_ok=True)
            for f in d.iterdir():
                if f.stat().st_size > MAX_LEN[t]:
                    f.unlink()
            if corpus.is_dir():
                for f in sorted(corpus.rglob("*")):
                    if f.is_file() and f.suffix.lower() in EXTENSIONS[t] and f.stat().st_size <= MAX_LEN[t]:
                        shutil.copy(f, d / "_".join(f.relative_to(corpus).parts))
            reg = ROOT / "fuzz" / "regressions" / t
            if reg.is_dir():
                for f in reg.iterdir():
                    if f.is_file() and f.name != "README.md":
                        shutil.copy(f, d / ("regression_" + f.name))
        if not corpus.is_dir():
            print(f"no interchange corpus at {corpus} (scripts/interop-corpus.py fetch --dir {corpus})", file=sys.stderr)
        for t in TARGETS:
            dest = out / t
            dest.mkdir(parents=True, exist_ok=True)
            fuzzer = build / "bin" / f"fuzz_{t}"
            if fuzzer.exists() and not args.no_merge:
                # Only what adds coverage, added to what is there. An input that crashes the merge is
                # written to the scratch folder (libFuzzer's default is the current folder), and the
                # merge goes on without it; `run` and `replay` report it.
                subprocess.run([str(fuzzer), "-merge=1", f"-max_len={MAX_LEN[t]}", f"-artifact_prefix={raw}{os.sep}",
                                str(dest), str(raw / t)],
                               check=True, env=env(), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=raw)
            else:
                for f in (raw / t).iterdir():
                    if f.stat().st_size <= MAX_LEN[t]:
                        shutil.copy(f, dest / f.name)
            print(f"{t}: {len(list(dest.iterdir()))} inputs in {dest}")
    finally:
        shutil.rmtree(raw, ignore_errors=True)
    return 0


# libFuzzer's status lines: "#1234\tNEW    cov: ..." from one process, "#1234: cov: ..." from the parent in
# fork mode, whose counts are those of all its processes (its children's lines are in the log too).
STATS = re.compile(r"#(\d+)\s*:?\s*(?:\w+\s+)?cov: (\d+) ft: (\d+) corp: (\d+)(?:/(\S+))?.*?exec/s: (\d+)")
FORK_STATS = re.compile(r"#(\d+): cov: (\d+) ft: (\d+) corp: (\d+)()\s.*?exec/s: (\d+)")


def run_target(t: str, seconds: int, args) -> dict:
    build = Path(args.build)
    fuzzer = binary(build, f"fuzz_{t}")
    corpus = Path(args.corpus_dir) / t
    corpus.mkdir(parents=True, exist_ok=True)
    artifacts = Path(args.artifacts) / t
    if artifacts.exists():
        shutil.rmtree(artifacts)
    artifacts.mkdir(parents=True)
    log = artifacts / "fuzz.log"
    command = [str(fuzzer), str(corpus), f"-max_total_time={seconds}", f"-timeout={args.timeout}",
               f"-max_len={args.max_len or MAX_LEN[t]}", f"-rss_limit_mb={args.rss_limit}",
               f"-artifact_prefix={artifacts}{os.sep}", "-print_final_stats=1"]
    if args.jobs > 1:
        # Fork mode: N processes share the corpus. Hangs and running out of memory are failures like
        # crashes; without --keep-going the first stops the run, with it each is saved and the fuzzer
        # goes on (libFuzzer's -ignore_*: the child that failed is replaced).
        ignore = "1" if args.keep_going else "0"
        command += [f"-fork={args.jobs}", f"-ignore_timeouts={ignore}", f"-ignore_ooms={ignore}",
                    f"-ignore_crashes={ignore}"]
    print(f"== fuzz_{t} for {seconds} s: {' '.join(command)}", flush=True)
    start = time.time()

    def latest():
        found = None
        for m in (FORK_STATS if args.jobs > 1 else STATS).finditer(log.read_text(errors="replace")):
            found = m
        return found

    log.write_text("")
    code = 0
    seen: set = set()
    while True:
        with open(log, "a") as out:
            process = subprocess.Popen(command, stdout=out, stderr=subprocess.STDOUT, env=env())
            # A line of progress every few minutes, so that a long run's log shows it is alive.
            while True:
                try:
                    code = process.wait(timeout=args.progress)
                    break
                except subprocess.TimeoutExpired:
                    m = latest()
                    if m:
                        print(f"   {round(time.time() - start)} s: {m.group(1)} executions, cov {m.group(2)}, "
                              f"ft {m.group(3)}, corpus {m.group(4)}, {m.group(6)} exec/s", flush=True)
        # One process stops at its first failure: with --keep-going it is started again for the time
        # left (fork mode goes on by itself). The failing input is in the artifacts already; when it is
        # one of the corpus's own files, that file is moved out, or the next start would fail on it
        # again. A start that saves nothing new ends the loop.
        left = seconds - round(time.time() - start)
        saved = {f.name for f in artifacts.iterdir() if f.name != "fuzz.log"}
        if args.jobs > 1 or not args.keep_going or code == 0 or left < 10 or saved == seen:
            break
        seen = saved
        failing = {f.read_bytes() for f in artifacts.iterdir() if f.name in saved}
        sizes = {len(b) for b in failing}
        for c in list(corpus.iterdir()):
            if c.is_file() and c.stat().st_size in sizes and c.read_bytes() in failing:
                c.rename(artifacts / f"corpus-{c.name}")
        print(f"   {round(time.time() - start)} s: stopped (exit {code}); again for the {left} s left", flush=True)
        command = [a if not a.startswith("-max_total_time=") else f"-max_total_time={left}" for a in command]
    elapsed = time.time() - start
    text = log.read_text(errors="replace")
    last = latest()
    stats = {"target": t, "seconds": round(elapsed), "exit": code,
             "executions": int(last.group(1)) if last else 0,
             "coverage": int(last.group(2)) if last else 0, "features": int(last.group(3)) if last else 0,
             "corpus": int(last.group(4)) if last else 0, "exec_per_s": int(last.group(6)) if last else 0}
    m = re.search(r"stat::number_of_executed_units:\s*(\d+)", text)
    if m and args.jobs <= 1:
        stats["executions"] = max(stats["executions"], int(m.group(1)))
    found = sorted((f for f in artifacts.iterdir() if f.name != "fuzz.log" and not f.name.startswith("corpus-")),
                   key=lambda f: f.name)
    stats["artifacts"] = [f.name for f in found]
    stats["problems"] = []
    if args.keep_going and found:
        stats["problems"] = report_problems(t, fuzzer, found, args)
    elif code != 0 or found:
        print("\n".join(text.splitlines()[-80:]))
        for f in found:
            print_reproducer(t, f)
    return stats


def print_reproducer(t: str, f: Path) -> None:
    data = f.read_bytes()
    print(f"\n== reproducer {f.name} ({len(data)} bytes), base64; decode with `base64 -d > input`, then "
          f"build/fuzz/bin/fuzz_{t} input (or lumenis_fuzz_replay --target {t} input):")
    print(base64.encodebytes(data).decode(), end="", flush=True)


# A stack frame in our code ("#3 0x... in lumenis::interop::aaf::...(...) /path/src/interop/aaf.cpp:520:65"),
# and the sanitizer's or libFuzzer's one-line summary of what went wrong.
OUR_FRAME = re.compile(r"#\d+ 0x[0-9a-f]+ in (lumenis::(?:[^(\s]|\(anonymous namespace\))+).*?((?:src|fuzz)/[^\s:]+:\d+)")
SUMMARY = re.compile(r"^SUMMARY: (.*)$", re.M)
# Problems whose stack and reproducer are printed, by target; the rest are counted.
MAX_PRINTED = 20


def report_problems(t: str, fuzzer: Path, found: list, args) -> list:
    """Each failing input run once more, alone, for its stack, and grouped by where it fails.

    The same bug is usually saved many times (each process, each mutation that reaches it). A problem
    is the kind of failure (the artifact's prefix: crash, timeout, oom, leak, slow-unit) and the first
    frame of the stack in our code; failures with no such frame are told apart by the summary line.
    """
    groups: dict = {}
    for f in found:
        kind = f.name.rsplit("-", 1)[0]  # crash, timeout, oom, leak, slow-unit
        try:
            run = subprocess.run([str(fuzzer), f"-timeout={args.timeout}", f"-rss_limit_mb={args.rss_limit}", str(f)],
                                 capture_output=True, text=True, errors="replace", env=env(),
                                 timeout=args.timeout * 3 + 60)
            output = run.stdout + run.stderr
        except subprocess.TimeoutExpired as e:
            # What it printed before it was stopped (bytes, even in text mode, on some Pythons).
            output = "".join(x.decode(errors="replace") if isinstance(x, bytes) else (x or "") for x in (e.stdout, e.stderr))
        frame = OUR_FRAME.search(output)
        summary = SUMMARY.search(output)
        where = (f"{frame.group(1)} ({frame.group(2)})" if frame else summary.group(1) if summary
                 else "nothing when run alone (slow, or it depends on what ran before)")
        key = (kind, where)
        g = groups.setdefault(key, {"kind": kind, "where": where, "files": [], "output": output})
        g["files"].append(f)
    problems = sorted(groups.values(), key=lambda g: (g["kind"], g["where"]))
    print(f"\n== fuzz_{t}: {len(found)} failing inputs, {len(problems)} problems", flush=True)
    for i, g in enumerate(problems, 1):
        smallest = min(g["files"], key=lambda f: f.stat().st_size)
        print(f"\n== problem {i} of {len(problems)}: {g['kind']} in {g['where']} ({len(g['files'])} inputs)")
        if i > MAX_PRINTED:
            continue
        lines = [l for l in g["output"].splitlines() if l.strip()]
        start = next((n for n, l in enumerate(lines) if "ERROR:" in l or "runtime error" in l or "ALARM" in l), 0)
        print("\n".join(lines[start:start + 40]))
        print_reproducer(t, smallest)
    if len(problems) > MAX_PRINTED:
        print(f"\n(the stacks and reproducers of problems past {MAX_PRINTED} are not printed: they are in {found[0].parent})")
    return [{"kind": g["kind"], "where": g["where"], "inputs": len(g["files"]),
             "reproducer": min(g["files"], key=lambda f: f.stat().st_size).name} for g in problems]


def cmd_run(args) -> int:
    targets = args.targets or TARGETS
    total = args.time
    weight = sum(WEIGHTS[t] for t in targets)
    results = []
    failed = False
    for t in targets:
        seconds = max(10, total * WEIGHTS[t] // weight) if not args.each else args.time
        r = run_target(t, seconds, args)
        results.append(r)
        if r["exit"] != 0 or r["artifacts"]:
            failed = True
            if not args.keep_going:
                break
    print("\n== summary")
    print(f"{'target':<12} {'seconds':>8} {'executions':>12} {'exec/s':>8} {'cov':>6} {'ft':>7} {'corpus':>7}  result")
    for r in results:
        if r["exit"] == 0 and not r["artifacts"]:
            result = "clean"
        elif r["problems"]:
            result = f"FAILED ({len(r['problems'])} problems, {len(r['artifacts'])} inputs)"
        else:
            result = f"FAILED ({', '.join(r['artifacts']) or 'exit ' + str(r['exit'])})"
        print(f"{r['target']:<12} {r['seconds']:>8} {r['executions']:>12} {r['exec_per_s']:>8} {r['coverage']:>6} "
              f"{r['features']:>7} {r['corpus']:>7}  {result}")
    problems = [(r["target"], p) for r in results for p in r["problems"]]
    if problems:
        print("\n== problems")
        for t, p in problems:
            print(f"{t:<12} {p['kind']:<9} {p['where']}  ({p['inputs']} inputs; {p['reproducer']})")
    if args.stats:
        Path(args.stats).write_text(json.dumps(results, indent=2) + "\n")
    return 1 if failed else 0


def cmd_replay(args) -> int:
    replay = binary(Path(args.build), "lumenis_fuzz_replay")
    command = [str(replay)] + [a for t in (args.targets or []) for a in ("--target", t)]
    if args.seeds:
        command.append("--seeds")
    return subprocess.run(command + args.paths, env=env()).returncode


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build", help="configure and build the fuzz preset")
    b.set_defaults(func=cmd_build)

    s = sub.add_parser("seed", help="make the seed corpora")
    s.add_argument("--corpus", default=str(default_corpus()), help="the interchange corpus (default: %(default)s)")
    s.add_argument("--out", default=str(ROOT / "build" / "fuzz" / "corpus"), help="default: %(default)s")
    s.add_argument("--build", default=str(ROOT / "build" / "fuzz"), help="default: %(default)s")
    s.add_argument("--no-merge", action="store_true", help="copy every seed, not only those adding coverage")
    s.set_defaults(func=cmd_seed)

    r = sub.add_parser("run", help="fuzz the targets")
    r.add_argument("--targets", nargs="+", choices=TARGETS, help="default: all")
    r.add_argument("--time", type=parse_duration, default=parse_duration("10m"),
                   help="total time, split between the targets by weight (30m, 24h); default 10m")
    r.add_argument("--each", action="store_true", help="--time is each target's, not the total")
    r.add_argument("--jobs", type=int, default=1, help="processes per target (libFuzzer -fork)")
    r.add_argument("--timeout", type=int, default=10, help="seconds one input may take (default 10)")
    r.add_argument("--max-len", type=int, default=0, help="largest input (default per target)")
    r.add_argument("--rss-limit", type=int, default=2048, help="MB (default 2048)")
    r.add_argument("--build", default=str(ROOT / "build" / "fuzz"))
    r.add_argument("--corpus-dir", default=str(ROOT / "build" / "fuzz" / "corpus"),
                   help="the corpora, which the run adds to (default: %(default)s)")
    r.add_argument("--artifacts", default=str(ROOT / "build" / "fuzz" / "artifacts"),
                   help="logs and reproducers, by target (default: %(default)s)")
    r.add_argument("--keep-going", action="store_true",
                   help="fuzz every target for its whole time, keep every failing input, and report them "
                        "grouped into problems at the end")
    r.add_argument("--progress", type=int, default=600, help="seconds between progress lines (default 600)")
    r.add_argument("--stats", help="write the statistics as JSON here")
    r.set_defaults(func=cmd_run)

    y = sub.add_parser("replay", help="run inputs through the targets")
    y.add_argument("paths", nargs="*")
    y.add_argument("--targets", nargs="+", choices=TARGETS)
    y.add_argument("--seeds", action="store_true", help="also the built-in seeds")
    y.add_argument("--build", default=str(ROOT / "build" / "fuzz"),
                   help="any build with lumenis_fuzz_replay (default: %(default)s)")
    y.set_defaults(func=cmd_replay)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
