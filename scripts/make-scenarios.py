#!/usr/bin/env python3
"""Generates random edit scenarios for tst_scenarios (ROADMAP Phase 4 gate 1;
docs/spec/edit-scenarios.md), with their expected sequences worked out by a small reference model
of three-point editing written here, apart from the C++ one, so that each checks the other.

    make-scenarios.py --out DIR [--count N] [--seed S]

Writes DIR/generated.json: N scenarios (default 300), the same ones for the same seed. Each starts
from a sequence of clips A and B on V1, A1 and A2, and makes one to four edits: Splice-in and
Overwrite of part of clip C or D at the record position on some source tracks, and Lift and Extract
between record marks on some record tracks (Editing Guide p.466-468, p.478-479). The model knows
only this much: segments are back to back, filler is joined to the filler before it, an edit past
a track's end pads it with filler, and the end of each track's trailing filler is not material.
"""
from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

TRACKS = ["V1", "A1", "A2"]
CLIP_FRAMES = 200


def length(track: list) -> int:
    return sum(s[-1] if s[0] == "filler" else s[2] for s in track)


def seg_len(s: list) -> int:
    return s[1] if s[0] == "filler" else s[2]


def cut(track: list, at: int) -> int:
    """Splits the track at frame `at` (padding it with filler to reach it); returns the index of
    the first segment at or after `at`."""
    pos = 0
    for i, s in enumerate(track):
        n = seg_len(s)
        if at == pos:
            return i
        if at < pos + n:
            head = at - pos
            if s[0] == "filler":
                track[i:i + 1] = [["filler", head], ["filler", n - head]]
            else:
                track[i:i + 1] = [[s[0], s[1], head], [s[0], s[1] + head, n - head]]
            return i + 1
        pos += n
    if at > pos:
        track.append(["filler", at - pos])
    return len(track)


def tidy(track: list) -> list:
    out: list = []
    for s in track:
        if seg_len(s) <= 0:
            continue
        if s[0] == "filler" and out and out[-1][0] == "filler":
            out[-1] = ["filler", out[-1][1] + s[1]]
        else:
            out.append(list(s))
    return out


def splice(track: list, at: int, material: list) -> list:
    i = cut(track, at)
    track[i:i] = [list(material)]
    return tidy(track)


def remove(track: list, start: int, n: int, extract: bool) -> list:
    i = cut(track, start)
    j = cut(track, start + n)
    track[i:j] = [] if extract else [["filler", n]]
    return tidy(track)


def overwrite(track: list, at: int, material: list) -> list:
    if at < length(track):
        n = min(seg_len(material), length(track) - at)
        track = remove(track, at, n, extract=True)
    return splice(track, at, material)


def shown(track: list) -> list:
    out = [list(s) for s in track]
    while out and out[-1][0] == "filler":
        out.pop()
    return out


def scenario(rng: random.Random, index: int) -> dict:
    # Two or three segments of A and B, the same on every track.
    base = []
    for k in range(rng.randint(2, 3)):
        clip = "A" if k % 2 == 0 else "B"
        start = rng.randint(0, 100)
        base.append([clip, start, rng.randint(5, 60)])
    tracks = {t: [list(s) for s in base] for t in TRACKS}
    steps: list = []
    names = []
    for _ in range(rng.randint(1, 4)):
        duration = max(length(t) for t in tracks.values())
        op = rng.choice(["splice", "overwrite", "lift", "extract"])
        if op in ("splice", "overwrite"):
            clip = rng.choice(["C", "D"])
            src_in = rng.randint(0, 150)
            n = rng.randint(1, 30)
            src_tracks = sorted(rng.sample(TRACKS, rng.randint(1, 3)), key=TRACKS.index)
            at = rng.randint(0, max(0, duration - 1))
            steps += [
                {"load": "source", "clip": clip},
                {"source_tracks": src_tracks},
                {"record_tracks": TRACKS},
                {"mark": "source", "in": src_in, "out": src_in + n - 1},
                {"mark": "record", "at": at},
                {"command": "edit.splice_in" if op == "splice" else "edit.overwrite"},
            ]
            for t in src_tracks:
                material = [clip, src_in, n]
                tracks[t] = splice(tracks[t], at, material) if op == "splice" else overwrite(tracks[t], at, material)
            names.append(f"{op} {clip} {n}@{at} on {'+'.join(src_tracks)}")
        else:
            rec_tracks = sorted(rng.sample(TRACKS, rng.randint(1, 3)), key=TRACKS.index)
            # Within every chosen track, so that the marks cover material on each.
            end = min(length(tracks[t]) for t in rec_tracks)
            if end < 2:
                continue
            start = rng.randint(0, end - 2)
            stop = rng.randint(start, end - 1)
            steps += [
                {"record_tracks": rec_tracks},
                {"mark": "record", "in": start, "out": stop},
                {"command": "edit." + op},
            ]
            for t in rec_tracks:
                tracks[t] = remove(tracks[t], start, stop - start + 1, extract=op == "extract")
            names.append(f"{op} {start}-{stop} on {'+'.join(rec_tracks)}")
    return {
        "name": f"{index:03d} " + ", ".join(names),
        "guide": "p.466-468, p.478-479",
        "clips": [{"name": c, "frames": CLIP_FRAMES} for c in "ABCD"],
        "sequence": {"tracks": {t: base for t in TRACKS}},
        "steps": steps,
        "expect": {t: shown(tracks[t]) for t in TRACKS},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, help="the folder to write generated.json into")
    parser.add_argument("--count", type=int, default=300)
    parser.add_argument("--seed", type=int, default=20261025)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    scenarios = [scenario(rng, i) for i in range(args.count)]
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "generated.json").write_text(json.dumps(scenarios, indent=1) + "\n", encoding="utf-8")
    print(f"make-scenarios: {len(scenarios)} scenarios in {out / 'generated.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
