#!/usr/bin/env python3
"""Dumps an AAF file with pyaaf2 (MIT), in the form `cutthulhu --dump-aaf`
prints (docs/spec/aaf.md, "Dumps"), so that the two readers can be compared:

    aaf-dump.py FILE.aaf [--canonical]
    aaf-dump.py --compare --app build/ci/bin/cutthulhu FILE.aaf...

Every object below the Header, with every property:
    {"class": name, "properties": {name: value}}
* integers, Booleans and strings as they are; enumerations by element name;
* AUIDs as "0d010101-0101-0100-060e-2b3402060101", MobIDs as their URN;
* records as objects of their members, arrays as lists, data sets sorted;
* strong references as the objects, weak references as {"weakref": key};
* streams as {"stream": size}; what cannot be decoded as {"bytes": hex}.
A property without a definition is named by its pid, "0x" and 4 hex digits.

--compare runs Cutthulhu on each file and prints where the dumps differ;
it exits 1 when any do. Needs pyaaf2 (pip install pyaaf2); exits 77
without it.
"""

import argparse
import datetime
import json
import subprocess
import sys
import uuid


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def load_pyaaf2():
    try:
        import aaf2  # noqa: F401
        from aaf2 import properties, core
        from aaf2.auid import AUID
        from aaf2.mobid import MobID
        from aaf2.rational import AAFRational
    except ImportError:
        print("aaf-dump: pyaaf2 is not installed (pip install pyaaf2)", file=sys.stderr)
        sys.exit(77)
    return aaf2, properties, core, AUID, MobID, AAFRational


def dumper(aaf2, P, core, AUID, MobID, AAFRational):
    def value(v):
        if isinstance(v, bool) or v is None:
            return v
        if isinstance(v, int):
            return v
        if isinstance(v, str):
            return v
        if isinstance(v, (AUID, uuid.UUID)):
            return str(v)
        if isinstance(v, MobID):
            return v.urn
        if isinstance(v, AAFRational):
            return {"Numerator": v.numerator, "Denominator": v.denominator}
        if isinstance(v, datetime.datetime):
            return {"date": value(v.date()), "time": value(v.time())}
        if isinstance(v, datetime.date):
            return {"year": v.year, "month": v.month, "day": v.day}
        if isinstance(v, datetime.time):
            return {"hour": v.hour, "minute": v.minute, "second": v.second, "fraction": v.microsecond}
        if isinstance(v, dict):
            return {k: value(x) for k, x in v.items()}
        if isinstance(v, (set, frozenset)):
            return sorted((value(x) for x in v), key=canonical)
        if isinstance(v, (list, tuple)):
            return [value(x) for x in v]
        if isinstance(v, (bytes, bytearray)):
            return {"bytes": bytes(v).hex()}
        if isinstance(v, core.AAFObject):
            return obj(v)
        raise TypeError(f"cannot dump {type(v)}")

    def key_text(ref):
        return ref.urn if isinstance(ref, MobID) else str(ref)

    def prop(p):
        f = p.format
        if f == P.SF_STRONG_OBJECT_REFERENCE:
            return obj(p.value)
        if f == P.SF_STRONG_OBJECT_REFERENCE_VECTOR:
            return [obj(o) for o in p]
        if f == P.SF_STRONG_OBJECT_REFERENCE_SET:
            return [obj(o) for o in p.values()]
        if f == P.SF_WEAK_OBJECT_REFERENCE:
            return {"weakref": key_text(p.ref)}
        if f in (P.SF_WEAK_OBJECT_REFERENCE_VECTOR, P.SF_WEAK_OBJECT_REFERENCE_SET):
            return [{"weakref": key_text(r)} for r in p.references]
        if f == P.SF_DATA_STREAM:
            entry = p.parent.dir.get(p.stream_name)
            return {"stream": entry.byte_size if entry is not None else -1}
        if p.propertydef is None or p.typedef is None:
            return {"bytes": bytes(p.data).hex()}
        try:
            return value(p.value)
        except Exception:
            return {"bytes": bytes(p.data).hex()}

    def obj(o):
        if o is None:
            return None
        props = {}
        for pid, p in o.property_entries.items():
            pdef = p.propertydef
            name = pdef.property_name if pdef is not None else "0x%04x" % pid
            props[name] = prop(p)
        classdef = o.classdef
        return {"class": classdef.class_name if classdef else str(o.class_id), "properties": props}

    return obj


def dump(path):
    aaf2, P, core, AUID, MobID, AAFRational = load_pyaaf2()
    obj = dumper(aaf2, P, core, AUID, MobID, AAFRational)
    with aaf2.open(path, "r") as f:
        return obj(f.header)


def differences(a, b, where="", out=None, limit=40):
    out = [] if out is None else out
    if len(out) >= limit:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(a.keys() | b.keys()):
            if k not in a:
                out.append(f"{where}/{k}: only in Cutthulhu's")
            elif k not in b:
                out.append(f"{where}/{k}: only in pyaaf2's")
            else:
                differences(a[k], b[k], f"{where}/{k}", out, limit)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{where}: {len(a)} items in pyaaf2's, {len(b)} in Cutthulhu's")
        for i, (x, y) in enumerate(zip(a, b)):
            differences(x, y, f"{where}[{i}]", out, limit)
    elif a != b:
        out.append(f"{where}: pyaaf2 {json.dumps(a)[:200]} != Cutthulhu {json.dumps(b)[:200]}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--canonical", action="store_true", help="compact, sorted JSON")
    ap.add_argument("--compare", action="store_true", help="compare with cutthulhu --dump-aaf")
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
        ours = subprocess.run([args.app, "--dump-aaf", path, "--canonical"], capture_output=True, text=True)
        if ours.returncode != 0:
            print(f"{path}: cutthulhu failed: {ours.stderr.strip()}")
            failed += 1
            continue
        theirs = dump(path)
        found = differences(theirs, json.loads(ours.stdout))
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
