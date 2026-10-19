#!/usr/bin/env python3
"""Structural diff of bins and projects (ROADMAP Phase 3, gate 1).

Compares two structure dumps (docs/spec/structure.md), such as
`cutthulhu --dump-structure` of a bin read from Media Composer and a
reference dump of the same bin:

    structdiff.py A.json B.json [--no-mob] [--fields]

What gate 1 asks to be identical:
* the bins, by name, and the clip count of each kind in each;
* each item's name, kind, edit rate, duration and Start and End timecodes;
* custom columns: each bin's list, and each item's values;
* sequences: their tracks, and on each track the segments in order (filler
  lengths; each source clip, its track, IN point and length; effect names).

Items are matched by MobID when both have one, else by kind and name (and
their order among items with the same kind and name). --no-mob matches by
kind and name only and compares segments' sources by name: for a reader whose
MobIDs differ from the other's. --fields also compares the standard columns
(Video, Tape, ...). Exits 1 when they differ, listing every difference.
"""

import argparse
import collections
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    if doc.get("format") != "cutthulhu-structure":
        raise SystemExit(f"{path}: not a structure dump (docs/spec/structure.md)")
    if doc.get("version", 0) > 1:
        raise SystemExit(f"{path}: structure version {doc['version']} is newer than this script")
    return doc


def keyed(items, use_mob):
    """Items by key: MobID, else (kind, name, n)."""
    out = {}
    seen = collections.Counter()
    for item in items:
        name_key = (item.get("kind"), item.get("name"))
        n = seen[name_key]
        seen[name_key] += 1
        mob = item.get("mobId")
        out[("mob", mob) if use_mob and mob else ("name",) + name_key + (n,)] = item
    return out


def label(item):
    return f"{item.get('kind')} '{item.get('name')}'"


def segment_text(seg, use_mob):
    if "filler" in seg:
        text = f"filler {seg['filler']}"
    else:
        source = seg.get("source") if use_mob else seg.get("name")
        text = f"{source} {seg.get('track')} in {seg.get('in')} length {seg.get('length')}"
    for k in ("effect", "transition"):
        if k in seg:
            text += f" {k} {seg[k]}"
    return text


def diff_sequences(a, b, use_mob, out, where):
    ta = {t["name"]: t["segments"] for t in a.get("tracks", [])}
    tb = {t["name"]: t["segments"] for t in b.get("tracks", [])}
    if list(ta) != list(tb):
        out.append(f"{where}: tracks {list(ta)} != {list(tb)}")
    for name in ta.keys() & tb.keys():
        sa = [segment_text(s, use_mob) for s in ta[name]]
        sb = [segment_text(s, use_mob) for s in tb[name]]
        if sa != sb:
            for i in range(max(len(sa), len(sb))):
                x = sa[i] if i < len(sa) else "(none)"
                y = sb[i] if i < len(sb) else "(none)"
                if x != y:
                    out.append(f"{where}: {name} segment {i + 1}: {x} != {y}")


def diff_items(a, b, use_mob, fields, out, where):
    for k in ("name", "kind", "editRate", "duration", "start", "end"):
        if a.get(k) != b.get(k):
            out.append(f"{where}: {k} {a.get(k)!r} != {b.get(k)!r}")
    keys = ["columns"] + (["fields", "tracks"] if fields else [])
    for k in keys:
        va, vb = a.get(k, {} if k != "tracks" else None), b.get(k, {} if k != "tracks" else None)
        if isinstance(va, dict) and isinstance(vb, dict):
            for col in sorted(va.keys() | vb.keys()):
                if va.get(col) != vb.get(col):
                    out.append(f"{where}: {k[:-1] if k.endswith('s') else k} '{col}' {va.get(col)!r} != {vb.get(col)!r}")
        elif va != vb:
            out.append(f"{where}: {k} {va!r} != {vb!r}")
    if "sequence" in a or "sequence" in b:
        diff_sequences(a.get("sequence", {}), b.get("sequence", {}), use_mob, out, where)


def diff(a, b, use_mob=True, fields=False):
    out = []
    bins_a = {x["name"]: x for x in a.get("bins", [])}
    bins_b = {x["name"]: x for x in b.get("bins", [])}
    for name in sorted(bins_a.keys() - bins_b.keys()):
        out.append(f"bin '{name}': only in the first")
    for name in sorted(bins_b.keys() - bins_a.keys()):
        out.append(f"bin '{name}': only in the second")
    for name in sorted(bins_a.keys() & bins_b.keys()):
        ba, bb = bins_a[name], bins_b[name]
        where = f"bin '{name}'"
        if sorted(ba.get("customColumns", [])) != sorted(bb.get("customColumns", [])):
            out.append(f"{where}: custom columns {ba.get('customColumns')} != {bb.get('customColumns')}")
        ca = collections.Counter(i.get("kind") for i in ba.get("items", []))
        cb = collections.Counter(i.get("kind") for i in bb.get("items", []))
        if ca != cb:
            out.append(f"{where}: counts {dict(sorted(ca.items()))} != {dict(sorted(cb.items()))}")
        ia, ib = keyed(ba.get("items", []), use_mob), keyed(bb.get("items", []), use_mob)
        if use_mob:
            # Fall back on names for items whose MobID is only on one side.
            for side, other in ((ia, ib), (ib, ia)):
                for k in [k for k in side if k[0] == "mob" and k not in other]:
                    item = side.pop(k)
                    side[("name", item.get("kind"), item.get("name"), 0)] = item
        for k in ia.keys() - ib.keys():
            out.append(f"{where}: {label(ia[k])} only in the first")
        for k in ib.keys() - ia.keys():
            out.append(f"{where}: {label(ib[k])} only in the second")
        for k in sorted(ia.keys() & ib.keys(), key=str):
            diff_items(ia[k], ib[k], use_mob, fields, out, f"{where}: {label(ia[k])}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("first")
    ap.add_argument("second")
    ap.add_argument("--no-mob", action="store_true", help="match by kind and name, sources by name")
    ap.add_argument("--fields", action="store_true", help="also compare the standard columns")
    args = ap.parse_args()
    differences = diff(load(args.first), load(args.second), not args.no_mob, args.fields)
    for line in differences:
        print(line)
    if differences:
        print(f"structdiff: {len(differences)} differences")
        return 1
    print("structdiff: the same")
    return 0


if __name__ == "__main__":
    sys.exit(main())
