#!/usr/bin/env python3
"""Dumps a Media Composer bin (.avb) with pyavb (MIT, Mark Reid), in the form
`cutthulhu --dump-avb` prints (docs/spec/avb.md, "Dumps"), so that the two
readers can be compared:

    avb-dump.py BIN.avb [--canonical]
    avb-dump.py --compare --app build/ci/bin/cutthulhu BIN.avb...

The dump is the bin's root object (class ABIN) with everything it refers to:
* an object is {"class": fourcc, "properties": {name: value}}, named as
  pyavb names its properties (its AVBPropertyDef names); an attribute list
  (ATTR) has its attributes as properties; a parameter list (PRLS) is
  {"class": "PRLS", "items": [...]};
* a reference is the object it refers to, null for none; an object already
  being dumped further up the same path (a cycle) is {"ref": index};
* integers, Booleans and strings as they are; lists as lists; the helper
  records pyavb keeps in lists (bin items, tracks, control points...) as
  objects of their properties;
* MobIDs as their URN ("urn:smpte:umid:..."), UUIDs as lowercase hyphenated
  text, times as seconds since 1970 (UTC), bytes as {"bytes": hex}, and
  floating-point numbers as {"float": text}, the text as Python's repr()
  writes it (shortest round trip), so that both sides write them alike;
  integers outside the signed 64-bit range as {"int": decimal text}.

--compare runs Cutthulhu on each file, prints where the dumps differ, and
checks that the canonical texts are byte for byte the same; it exits 1 when
any differ. Needs pyavb (pip install pyavb); exits 77 without it.
"""

import argparse
import datetime
import json
import subprocess
import sys
import uuid


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def load_pyavb():
    try:
        import avb  # noqa: F401
        from avb import core, utils, ioctx, attributes
        from avb.mobid import MobID
    except ImportError:
        print("avb-dump: pyavb is not installed (pip install pyavb)", file=sys.stderr)
        sys.exit(77)
    # pyavb makes times as local naive datetimes; read them as UTC instead
    # (the same instant), so that the dump does not depend on the time zone.
    def read_datetime(self, f):
        return datetime.datetime.fromtimestamp(self.read_u32(f), tz=datetime.timezone.utc)
    ioctx.AVBIOContext.read_datetime = read_datetime
    return avb, core, utils, attributes, MobID


def dumper(core, utils, attributes, MobID):
    path = []

    def value(v):
        if isinstance(v, utils.AVBObjectRef):
            if v.index <= 0:
                return None
            if v.index in path:
                return {"ref": v.index}
            return obj(v.value)
        if v is None or isinstance(v, (bool, str)):
            return v
        if isinstance(v, int):
            # JSON readers keep 64-bit signed integers exactly; larger ones
            # (unsigned 64-bit fields) are written as text.
            return v if -2**63 <= v < 2**63 else {"int": str(v)}
        if isinstance(v, float):
            return {"float": repr(v)}
        if isinstance(v, MobID):
            return v.urn
        if isinstance(v, uuid.UUID):
            return str(v)
        if isinstance(v, datetime.datetime):
            return int(v.timestamp())
        if isinstance(v, (bytes, bytearray)):
            return {"bytes": bytes(v).hex()}
        if getattr(v, "class_id", None):
            return obj(v)
        if isinstance(v, core.AVBObject):  # a helper record: BinItem, Track...
            return {k: value(x) for k, x in dict.items(v.property_data)}
        if isinstance(v, dict):
            return {k: value(x) for k, x in v.items()}
        if isinstance(v, list):
            # AVBRefList dereferences on iteration; read the raw entries.
            return [value(x) for x in list.__iter__(v)]
        raise TypeError(f"cannot dump {type(v)}")

    def obj(o):
        if o is None:
            return None
        index = getattr(o, "instance_id", None)
        class_id = o.class_id.decode("latin-1")
        path.append(index)
        try:
            if isinstance(o, attributes.Attributes):
                return {"class": class_id, "properties": {k: value(x) for k, x in dict.items(o)}}
            if isinstance(o, list):
                return {"class": class_id, "items": [value(x) for x in list.__iter__(o)]}
            return {"class": class_id, "properties": {k: value(x) for k, x in dict.items(o.property_data)}}
        finally:
            path.pop()

    return obj


def dump(path):
    avb, core, utils, attributes, MobID = load_pyavb()
    obj = dumper(core, utils, attributes, MobID)
    with avb.open(path, use_ext=False) as f:
        return obj(f.content)


def differences(a, b, where="", out=None, limit=40):
    out = [] if out is None else out
    if len(out) >= limit:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(a.keys() | b.keys()):
            if k not in a:
                out.append(f"{where}/{k}: only in Cutthulhu's")
            elif k not in b:
                out.append(f"{where}/{k}: only in pyavb's")
            else:
                differences(a[k], b[k], f"{where}/{k}", out, limit)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{where}: {len(a)} items in pyavb's, {len(b)} in Cutthulhu's")
        for i, (x, y) in enumerate(zip(a, b)):
            differences(x, y, f"{where}[{i}]", out, limit)
    elif a != b or type(a) is not type(b):
        out.append(f"{where}: pyavb {json.dumps(a)[:200]} != Cutthulhu {json.dumps(b)[:200]}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--canonical", action="store_true", help="compact, sorted JSON")
    ap.add_argument("--compare", action="store_true", help="compare with cutthulhu --dump-avb")
    ap.add_argument("--app", help="the cutthulhu executable, for --compare")
    args = ap.parse_args()
    if not args.compare:
        for path in args.files:
            d = dump(path)
            print(canonical(d) if args.canonical else json.dumps(d, indent=4, sort_keys=True))
        return 0
    if not args.app:
        ap.error("--compare needs --app")
    failed = 0
    for path in args.files:
        ours = subprocess.run([args.app, "--dump-avb", path, "--canonical"], capture_output=True)
        if ours.returncode != 0:
            print(f"{path}: cutthulhu failed: {ours.stderr.decode('utf-8', 'replace').strip()}")
            failed += 1
            continue
        theirs = dump(path)
        text = ours.stdout.decode("utf-8").strip()
        found = differences(theirs, json.loads(text))
        if not found and canonical(theirs) != text:
            found = ["the canonical texts differ, though their values are the same"]
        if found:
            failed += 1
            print(f"{path}: differs")
            for line in found:
                print("  " + line)
        else:
            print(f"{path}: the same")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
