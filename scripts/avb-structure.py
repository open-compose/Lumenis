#!/usr/bin/env python3
"""A reference structure dump of a Media Composer bin (.avb), read with pyavb
(MIT, Mark Reid), for scripts/structdiff.py (docs/spec/structure.md).

    avb-structure.py BIN.avb [BIN.avb...] > reference.json

It dumps the bin's master clips, subclips and sequences as Cutthulhu's
structure dumps do, so that what Cutthulhu reads from another file of the
same material (an AAF export: `cutthulhu --import-aaf`) can be compared
with what Media Composer's own bin holds:

    structdiff.py reference.json ours.json --no-effects

Effects are named as the bin names them (Media Composer's effect IDs), so
--no-effects leaves them out of the comparison. The columns are the bin's
user columns (_USER) with a value: AAF carries only those. Needs pyavb (pip install
pyavb); exits 77 without it.
"""

import fractions
import json
import os
import sys


# The standard bin headings that can be typed into (Editing Guide p.274-278,
# "Modifiable after capture with no restrictions"; model::isEditableHeading):
# the ones a bin's _USER attributes carry besides custom columns.
EDITABLE_HEADINGS = {"Name", "Comments", "Journalist", "Production", "Shoot date", "Start", "Take", "TapeID",
                     "Frame Count Start", "TC 24", "TC 25", "TC 25PD", "TC 30", "TC 30NP", "TC 60"} | \
                    {f"Auxiliary TC{i}" for i in range(1, 6)}


def rate_text(rate):
    """NTSC rates as their exact fractions (23.976 is 24000/1001)."""
    r = float(rate)
    n = round(r * 1.001)
    if abs(r - round(r)) > 0.001 and abs(r * 1.001 - n) < 0.002:
        return f"{n * 1000}/1001"
    f = fractions.Fraction(r).limit_denominator(1001)
    return f"{f.numerator}/{f.denominator}"


def timecode(frames, rate):
    fps = round(float(rate))
    frames %= fps * 86400
    s, f = divmod(frames, fps)
    m, s = divmod(s, 60)
    h, m = divmod(m, 60)
    return f"{h:02d}:{m:02d}:{s:02d}:{f:02d}"


def track_list(video, audio):
    parts = []
    for letter, numbers in (("V", video), ("A", audio)):
        numbers = sorted(set(numbers))
        i = 0
        while i < len(numbers):
            j = i
            while j + 1 < len(numbers) and numbers[j + 1] == numbers[j] + 1:
                j += 1
            parts.append(f"{letter}{numbers[i]}" if i == j else f"{letter}{numbers[i]}-{numbers[j]}")
            i = j + 1
    return " ".join(parts)


class Dumper:
    def __init__(self, avb, f):
        self.avb = avb
        self.mobs = {str(m.mob_id): m for m in f.content.mobs}
        self.custom = []

    def tracks(self, mob):
        return [t for t in mob.tracks if t.media_kind in ("picture", "sound")]

    def first_clip(self, c, depth=0):
        if c is None or depth > 16:
            return None
        name = type(c).__name__
        if name == "SourceClip":
            return c
        for child in getattr(c, "components", []) or []:
            found = self.first_clip(child, depth + 1)
            if found is not None:
                return found
        for t in getattr(c, "tracks", []) or []:
            found = self.first_clip(getattr(t, "component", None), depth + 1)
            if found is not None:
                return found
        return None

    def timecode_of(self, mob):
        for t in mob.tracks:
            c = t.component
            for _ in range(8):
                if c is None:
                    break
                if type(c).__name__ == "Timecode":
                    return c.start, c.fps
                kids = list(getattr(c, "tracks", []) or [])
                if kids:
                    c = kids[0].component
                elif getattr(c, "components", None):
                    c = c.components[0]
                else:
                    break
        return None

    def start_of(self, mob, track, rate):
        clip = self.first_clip(track.component)
        offset = 0
        for _ in range(8):
            if clip is None:
                return None
            offset += clip.start_time
            source = self.mobs.get(str(clip.mob_id))
            if source is None:
                return None
            tc = self.timecode_of(source)
            if tc:
                start, fps = tc
                nominal = round(float(rate))
                if fps and fps != nominal:
                    start = start * nominal // fps
                return start + offset
            nxt = [t for t in source.tracks if t.index == clip.track_id and t.media_kind == track.media_kind]
            clip = self.first_clip(nxt[0].component) if nxt else None
        return None

    def item(self, mob, kind):
        tracks = self.tracks(mob)
        pics = [t for t in tracks if t.media_kind == "picture"]
        first = pics[0] if pics else (tracks[0] if tracks else None)
        rate = mob.edit_rate
        out = {"kind": kind, "name": mob.name, "mobId": str(mob.mob_id), "editRate": rate_text(rate)}
        # The bin's columns (_USER); empty ones do not travel in AAF. Standard
        # headings that can be typed into (Comments, Take, ...) are fields, as
        # Cutthulhu's dumps have them; the others are custom columns.
        user = mob.attributes.get("_USER") if hasattr(mob.attributes, "get") else None
        values = {k: str(v) for k, v in (user.items() if user is not None else []) if str(v)}
        columns = {k: v for k, v in values.items() if k not in EDITABLE_HEADINGS}
        fields = {k: v for k, v in values.items() if k in EDITABLE_HEADINGS}
        if columns:
            out["columns"] = columns
            for k in columns:
                if k not in self.custom:
                    self.custom.append(k)
        if fields:
            out["fields"] = fields
        if kind == "sequence":
            out.update(self.sequence(mob, rate))
        else:
            out["duration"] = max([t.component.length for t in tracks if first and t.media_kind == first.media_kind] or [0])
            out["tracks"] = track_list([t.index for t in pics], [t.index for t in tracks if t.media_kind == "sound"])
            start = self.start_of(mob, first, rate) if first else None
            out["start"] = timecode(start, rate) if start is not None else ""
            out["end"] = timecode(start + out["duration"], rate) if start is not None else ""
        return out

    def segments(self, c, out, state, depth=0):
        name = type(c).__name__
        if depth > 32:
            return
        if name == "Sequence":
            for x in c.components:
                self.segments(x, out, state, depth + 1)
        elif name == "SourceClip":
            mob = self.mobs.get(str(c.mob_id))
            if c.mob_id is None or str(c.mob_id).endswith("00000000.00000000.00000000.00000000"):
                self.push(out, state, {"filler": c.length})
            else:
                track = f"{'V' if c.media_kind == 'picture' else 'A'}{c.track_id}"
                self.push(out, state, {"source": str(c.mob_id), "name": mob.name if mob else "",
                                       "track": track, "in": c.start_time, "length": c.length})
        elif name in ("Transition", "TransitionEffect"):
            if out:
                last = out[-1]
                key = "filler" if "filler" in last else "length"
                last[key] = max(0, last[key] - (c.length - c.cutpoint))
            state["pending"] = c.cutpoint
        elif getattr(c, "tracks", None) is not None and name != "Timecode":
            inner = []
            for t in c.tracks:
                inner = []
                if getattr(t, "component", None) is None:
                    continue
                self.segments(t.component, inner, {"pending": None}, depth + 1)
                if any("source" in s for s in inner):
                    break
            inner = self.tidy(inner)
            if not inner:
                inner = [{"filler": c.length}]
            total = sum(s.get("filler", s.get("length", 0)) for s in inner)
            while total > c.length and inner:
                last = inner[-1]
                key = "filler" if "filler" in last else "length"
                cut = min(last[key], total - c.length)
                last[key] -= cut
                total -= cut
                if last[key] == 0 and len(inner) > 1:
                    inner.pop()
            if total < c.length:
                last = inner[-1]
                last["filler" if "filler" in last else "length"] += c.length - total
            for s in inner:
                self.push(out, state, s)
        else:
            self.push(out, state, {"filler": c.length})

    def push(self, out, state, s):
        if state.get("pending") is not None:
            cut = state["pending"]
            if "source" in s:
                s["in"] += cut
            key = "filler" if "filler" in s else "length"
            s[key] = max(0, s[key] - cut)
            state["pending"] = None
        out.append(s)

    @staticmethod
    def tidy(segments):
        """Join filler and drop empty segments, as the model does."""
        out = []
        for s in segments:
            if s.get("filler", s.get("length", 0)) == 0:
                continue
            if "filler" in s and out and "filler" in out[-1]:
                out[-1]["filler"] += s["filler"]
            else:
                out.append(s)
        return out

    def sequence(self, mob, rate):
        tracks = []
        video, audio = [], []
        has_tc = any(t.media_kind == "timecode" for t in mob.tracks)
        start = None
        for t in mob.tracks:
            if t.media_kind == "timecode" and start is None and type(t.component).__name__ == "Timecode":
                start = t.component.start
                nominal = round(float(rate))
                if t.component.fps and t.component.fps != nominal:
                    start = start * nominal // t.component.fps
        for t in sorted(self.tracks(mob), key=lambda t: (t.media_kind != "picture", t.index)):
            out = []
            self.segments(t.component, out, {"pending": None})
            letter = "V" if t.media_kind == "picture" else "A"
            (video if letter == "V" else audio).append(t.index)
            tracks.append({"name": f"{letter}{t.index}", "segments": self.tidy(out)})
        duration = max([sum(s.get("filler", s.get("length", 0)) for s in t["segments"]) for t in tracks] or [0])
        text = track_list(video, audio) + (" TC1" if has_tc else "")
        return {"duration": duration, "tracks": text.strip(),
                "start": timecode(start, rate) if start is not None else "",
                "end": timecode(start + duration, rate) if start is not None else "",
                "sequence": {"tracks": tracks}}


def main():
    try:
        import avb
    except ImportError:
        print("avb-structure: pyavb is not installed (pip install pyavb)", file=sys.stderr)
        return 77
    bins = []
    for path in sys.argv[1:]:
        with avb.open(path) as f:
            d = Dumper(avb, f)
            items = []
            for m in f.content.mobs:
                usage = getattr(m, "usage_code", None)
                if m.mob_type == "MasterMob":
                    items.append(d.item(m, "masterClip" if any(t.media_kind == "picture" for t in d.tracks(m)) or not d.tracks(m) else "audioClip"))
                elif m.mob_type == "CompositionMob" and usage in (None, 0):
                    items.append(d.item(m, "sequence"))
                elif m.mob_type == "CompositionMob" and usage == 2:
                    # A subclip (usage code 2): its Start is its master's, moved by its offset.
                    items.append(d.item(m, "subclip"))
            bins.append({"name": os.path.splitext(os.path.basename(path))[0], "customColumns": sorted(d.custom),
                         "items": items})
    json.dump({"format": "cutthulhu-structure", "version": 1, "bins": bins}, sys.stdout, indent=1)
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
