#!/usr/bin/env python3
"""OpenTimelineIO and FCP 7 XML files through Cutthulhu (Phase 3.6b and 3.6c, docs/spec/otio.md,
docs/spec/fcp-xml.md).

    otio-check.py --round-trip --app build/ci/bin/cutthulhu FILE.otio...
        Import each file and export it again (`cutthulhu --export-otio FILE OUT`), then import
        and export that: the two exports must be the same JSON. What the first pass leaves out
        (docs/spec/otio.md) may differ from the original; after it, nothing may change. Needs only
        the app.
    otio-check.py --compare --app build/ci/bin/cutthulhu FILE.otio...
        Read each file and Cutthulhu's export of it with OpenTimelineIO's own Python package
        (Apache-2.0; pip install opentimelineio): the export must load, and every track must hold
        the same clips, at the same places in the track, from the same frames of the same media,
        and the same transitions, as OpenTimelineIO finds in the original. Places are worked out by
        OpenTimelineIO itself (transformed_time_range), so this does not repeat Cutthulhu's own
        arithmetic. What Cutthulhu reads differently by design is left out of the original's side:
        generators and disabled clips (gaps), the lower tracks of a nested stack (its top track with
        clips is kept) and transitions at a track's ends. Exits 77 without opentimelineio.

    --format fcpxml
        Export as FCP 7 XML (`cutthulhu --export-fcpxml`) instead of OTIO; the files given may be
        .otio or FCP XML. --compare then reads the exports, and FCP XML originals, with
        OpenTimelineIO's FCP adapter (Apache-2.0; pip install otio-fcp-adapter), and exits 77
        without it.

Exits 1 when any file differs, 2 for wrong arguments.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import urllib.parse


FORMAT = "otio"  # what export() writes: "otio" or "fcpxml" (--format)


def suffix():
    return ".xml" if FORMAT == "fcpxml" else ".otio"


def export(app, source, out):
    option = "--export-fcpxml" if FORMAT == "fcpxml" else "--export-otio"
    run = subprocess.run([app, option, source, out], capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(f"{option} {source} failed ({run.returncode}): {run.stderr.strip()}")
    return out


def load_file(path):
    if path.endswith(".xml"):
        with open(path, encoding="utf-8") as f:
            return f.read()
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def round_trip(app, paths):
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, path in enumerate(paths):
            first = os.path.join(tmp, f"{i}-1{suffix()}")
            second = os.path.join(tmp, f"{i}-2{suffix()}")
            try:
                export(app, path, first)
                export(app, first, second)
            except RuntimeError as e:
                print(f"{path}: {e}")
                failed += 1
                continue
            a = load_file(first)
            b = load_file(second)
            if a != b:
                print(f"{path}: the second export differs from the first")
                failed += 1
            else:
                print(f"{path}: the same after a second pass")
    return 1 if failed else 0


def load_otio():
    try:
        import opentimelineio  # noqa: F401
    except ImportError:
        print("otio-check: opentimelineio is not installed (pip install opentimelineio; on Linux it may need python3-dev; BUILDING.md, \"Python packages for the checks\")", file=sys.stderr)
        sys.exit(77)
    import opentimelineio as otio
    if FORMAT == "fcpxml" and "fcp_xml" not in [a.name for a in otio.plugins.ActiveManifest().adapters]:
        print("otio-check: OpenTimelineIO's FCP adapter is not installed (pip install otio-fcp-adapter; "
              "scripts/check-python.py installs it)", file=sys.stderr)
        sys.exit(77)
    return otio


def empty_stacks(otio, path):
    """For each track of each timeline, the spans [start, end) of nested stacks with no tracks.
    OpenTimelineIO's FCP adapter (1.0.0) reads every sequence nested in a clip so (it looks for the
    tracks under <sequence> instead of its <media>), so what Cutthulhu reads there cannot be
    compared: those spans are left out of both sides."""
    loaded = otio.adapters.read_from_file(path)
    timelines = [loaded] if isinstance(loaded, otio.schema.Timeline) else list(loaded.find_children(descended_from_type=otio.schema.Timeline))
    out = []
    for timeline in timelines:
        rate = timeline.global_start_time.rate if timeline.global_start_time else 24
        spans = []
        for track in timeline.tracks:
            if not isinstance(track, otio.schema.Track):
                continue
            mine = []
            for i, child in enumerate(track):
                if isinstance(child, otio.schema.Stack) and len(child) == 0:
                    r = track.range_of_child_at_index(i)
                    mine.append((round(r.start_time.rescaled_to(rate).value), round(r.end_time_exclusive().rescaled_to(rate).value)))
            spans.append(mine)
        out.append(spans)
    return out


def without(summaries, spans):
    """The summaries less the clips and transitions in `spans` (empty_stacks()), and less tracks
    with nothing in them (an empty sequence imports with empty tracks)."""
    out = []
    for t, tracks in enumerate(summaries):
        kept = []
        for k, (kind, clips, transitions) in enumerate(tracks):
            mine = spans[t][k] if t < len(spans) and k < len(spans[t]) else []
            inside = lambda at: any(a <= at < b for a, b in mine)  # noqa: E731
            clips = [c for c in clips if not inside(c[0])]
            transitions = [x for x in transitions if not inside(x[0])]
            kept.append((kind, clips, transitions))
        while kept and not kept[-1][1] and not kept[-1][2]:
            kept.pop()
        out.append(kept)
    return out


def summary(otio, path):
    """For each track of each timeline: (kind, clips, transitions). A clip is (place in the track,
    length, first frame of its media, its media's file or name); a transition (cut, in, out). Frames
    at the timeline's rate."""
    loaded = otio.adapters.read_from_file(path)
    timelines = [loaded] if isinstance(loaded, otio.schema.Timeline) else list(loaded.find_children(descended_from_type=otio.schema.Timeline))
    out = []
    for timeline in timelines:
        rate = timeline.global_start_time.rate if timeline.global_start_time else None
        tracks = []
        for track in timeline.tracks:
            if not isinstance(track, otio.schema.Track) or track.kind not in (otio.schema.TrackKind.Video, otio.schema.TrackKind.Audio):
                continue
            if rate is None:
                rate = next((c.trimmed_range().duration.rate for c in track.find_clips()), 24)

            def frames(t):
                return round(t.rescaled_to(rate).value)

            clips = []
            for clip in track.find_clips():
                if not counts(otio, clip):
                    continue
                if not top_of_nested_stacks(otio, clip, timeline):
                    continue
                placed = clip.transformed_time_range(clip.trimmed_range(), track)
                start, end = frames(placed.start_time), frames(placed.end_time_exclusive())
                skipped = 0
                # Nested tracks and stacks show only their trimmed range: the clip is cut to it.
                ancestor = clip.parent()
                while ancestor is not None and ancestor is not track:
                    shown = ancestor.transformed_time_range(ancestor.trimmed_range(), track)
                    a, b = frames(shown.start_time), frames(shown.end_time_exclusive())
                    if a > start:
                        skipped += a - start
                    start, end = max(start, a), min(end, b)
                    ancestor = ancestor.parent()
                if start >= end:
                    continue
                ref = clip.media_reference
                key = getattr(ref, "target_url", None) or (ref.name if ref is not None else None) or clip.name
                clips.append((start, end - start, frames(clip.trimmed_range().start_time) - frames(available_start(ref)) + skipped,
                              media_key(key)))
            transitions = []
            children = list(track)
            for i, child in enumerate(children):
                if not isinstance(child, otio.schema.Transition):
                    continue
                if i == 0 or i == len(children) - 1:
                    continue
                # FCP XML holds a transition only between two clips (docs/spec/fcp-xml.md).
                if FORMAT == "fcpxml" and not all(isinstance(c, otio.schema.Clip) for c in (children[i - 1], children[i + 1])):
                    continue
                cut = track.range_of_child_at_index(i + 1).start_time
                transitions.append((frames(cut), frames(child.in_offset), frames(child.out_offset)))
            tracks.append((str(track.kind), sorted(clips), transitions))
        out.append(tracks)
    return out


def media_key(key):
    """A media reference's key as both sides write it: unquoted, and FCP's "file://localhost/" as
    "file:///"."""
    key = urllib.parse.unquote(key)
    if key.lower().startswith("file://localhost/"):
        key = "file:///" + key[len("file://localhost/"):]
    return key


def available_start(ref):
    import opentimelineio as otio
    if ref is not None and ref.available_range is not None:
        return ref.available_range.start_time
    return otio.opentime.RationalTime(0, 24)


def counts(otio, clip):
    """Whether Cutthulhu reads the clip as a clip: not disabled (in OTIO, or <enabled>FALSE</enabled>
    in FCP XML, which OpenTimelineIO's FCP adapter keeps only as metadata) and not a generator."""
    if not clip.enabled or isinstance(clip.media_reference, otio.schema.GeneratorReference):
        return False
    return str(clip.metadata.get("fcp_xml", {}).get("enabled", "TRUE")).upper() != "FALSE"


def top_of_nested_stacks(otio, clip, timeline):
    """False for a clip under a nested stack's lower layers: Cutthulhu keeps the top one with clips
    it reads (counts())."""
    child = clip
    parent = clip.parent()
    while parent is not None and parent is not timeline.tracks:
        if isinstance(parent, otio.schema.Stack):
            layers = [c for c in parent if any(counts(otio, x) for x in (c.find_clips() if hasattr(c, "find_clips") else [c] if isinstance(c, otio.schema.Clip) else []))]
            if layers and layers[-1] is not child:
                return False
        child = parent
        parent = parent.parent()
    return True


def compare(app, paths):
    otio = load_otio()
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, path in enumerate(paths):
            out = os.path.join(tmp, f"{i}{suffix()}")
            try:
                export(app, path, out)
                theirs = summary(otio, path)
                ours = summary(otio, out)
                if FORMAT == "fcpxml" and path.endswith(".xml"):
                    spans = empty_stacks(otio, path)
                    theirs, ours = without(theirs, spans), without(ours, spans)
                elif FORMAT == "fcpxml":
                    theirs, ours = without(theirs, []), without(ours, [])
            except Exception as e:  # noqa: BLE001 - any failure to read is a difference
                print(f"{path}: {e}")
                failed += 1
                continue
            if theirs != ours:
                failed += 1
                print(f"{path}: differs")
                for t, (a, b) in enumerate(zip(theirs[0] if theirs else [], ours[0] if ours else [])):
                    if a != b:
                        print(f"  track {t + 1}:\n    OpenTimelineIO: {a}\n    Cutthulhu:      {b}")
                if theirs and ours and len(theirs[0]) != len(ours[0]):
                    print(f"  {len(theirs[0])} tracks in the original, {len(ours[0])} in the export")
            else:
                print(f"{path}: the same as OpenTimelineIO reads it")
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--round-trip", action="store_true")
    mode.add_argument("--compare", action="store_true")
    ap.add_argument("--app", required=True, help="the cutthulhu executable")
    ap.add_argument("--format", choices=["otio", "fcpxml"], default="otio", help="what to export (default otio)")
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()
    global FORMAT
    FORMAT = args.format
    return round_trip(args.app, args.paths) if args.round_trip else compare(args.app, args.paths)


if __name__ == "__main__":
    sys.exit(main())
