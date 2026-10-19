#!/usr/bin/env python3
"""Makes src/interop/aaf_model.inc, the AAF data model Cutthulhu's AAF reader
knows without a file: the baseline classes and types of the AAF Object
Specification, and the extensions Avid's applications write.

The tables come from pyaaf2's model (MIT, Mark Reid; THIRD-PARTY-NOTICES.md),
the classes and types modules of pyaaf2/src/aaf2/model and model/ext:

    make-aaf-model.py --pyaaf2 PATH/TO/pyaaf2 [--check]

PATH is a checkout of https://github.com/markreidvfx/pyaaf2 (pinned below).
--check exits 1 when the file in the repository is not what this makes.
Only the tables are read (with runpy); pyaaf2 itself is not imported.
"""

import argparse
import os
import runpy
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "interop", "aaf_model.inc")
PYAAF2_COMMIT = "08dcc3dfe823ea5781db1cb657d9c2606557fcdc"

# pyaaf2's names for the type categories, and ours (aaf::TypeKind).
KINDS = {
    "ints": "Int", "enums": "Enum", "records": "Record", "fixed_arrays": "FixedArray",
    "var_arrays": "VarArray", "renames": "Rename", "strings": "String", "streams": "Stream",
    "opaques": "Opaque", "extenums": "ExtEnum", "chars": "Character",
    "generic_chars": "GenericCharacter", "indirects": "Indirect", "sets": "Set",
    "strongrefs": "StrongRef", "weakrefs": "WeakRef",
}


def lower(auid):
    return auid.lower() if auid else auid


def merge_classes(tables):
    """{auid: [name, parent, concrete, {property auid: (name, pid, type, optional, unique)}]}"""
    classes = {}
    order = []
    for table in tables:
        for name, (auid, parent, concrete, *rest) in table["classdefs"].items():
            auid = lower(auid)
            props = rest[0] if rest else {}
            if auid not in classes:
                classes[auid] = [name, lower(parent), concrete, {}]
                order.append(auid)
            for pname, (pauid, pid, ptype, optional, unique) in (props or {}).items():
                classes[auid][3].setdefault(lower(pauid), (pname, pid, lower(ptype), optional, unique))
    return [(auid, *classes[auid]) for auid in order]


def merge_types(tables):
    """[(kind, name, auid, fields)] with enums and extensible enums merged by name."""
    types = {}
    order = []
    for table in tables:
        for category, kind in KINDS.items():
            for name, args in table.get(category, {}).items():
                if not isinstance(args, (tuple, list)):
                    args = [args]
                auid = lower(args[0])
                if name in types and kind in ("Enum", "ExtEnum"):
                    elements = args[2] if kind == "Enum" else args[1]
                    types[name][3]["elements"].update(elements)
                    continue
                if kind == "Int":
                    fields = {"size": args[1], "signed": args[2]}
                elif kind == "Enum":
                    fields = {"element": lower(args[1]), "elements": dict(args[2])}
                elif kind == "ExtEnum":
                    fields = {"elements": {lower(k): v for k, v in args[1].items()}}
                elif kind == "Record":
                    fields = {"members": [(m, lower(t)) for m, t in args[1]]}
                elif kind == "FixedArray":
                    fields = {"element": lower(args[1]), "count": args[2]}
                elif kind in ("VarArray", "Rename", "String", "Set"):
                    fields = {"element": lower(args[1])}
                elif kind == "StrongRef":
                    fields = {"element": lower(args[1])}
                elif kind == "WeakRef":
                    fields = {"element": lower(args[1]), "path": [lower(a) for a in args[2]]}
                elif kind == "GenericCharacter":
                    fields = {"size": args[1]}
                else:
                    fields = {}
                types[name] = (kind, name, auid, fields)
                order.append(name)
    return [types[n] for n in order]


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"' if s is not None else "nullptr"


def render(classes, types):
    out = ["// Made by scripts/make-aaf-model.py from pyaaf2's model tables (MIT,",
           f"// Mark Reid; commit {PYAAF2_COMMIT}). Do not edit.",
           "// clang-format off", ""]
    props = []
    out.append("const ModelClass kModelClasses[] = {")
    for auid, name, parent, concrete, properties in classes:
        first = len(props)
        for pauid, (pname, pid, ptype, optional, unique) in properties.items():
            props.append((pname, pauid, -1 if pid is None else pid, ptype, optional, unique))
        out.append(f"    {{{c_string(name)}, {c_string(auid)}, {c_string(parent)}, {str(bool(concrete)).lower()}, "
                   f"{first}, {len(props) - first}}},")
    out.append("};")
    out.append("")
    out.append("const ModelProperty kModelProperties[] = {")
    for pname, pauid, pid, ptype, optional, unique in props:
        out.append(f"    {{{c_string(pname)}, {c_string(pauid)}, {pid}, {c_string(ptype)}, "
                   f"{str(bool(optional)).lower()}, {str(bool(unique)).lower()}}},")
    out.append("};")
    out.append("")
    members = []
    out.append("const ModelType kModelTypes[] = {")
    for kind, name, auid, f in types:
        first = len(members)
        if kind == "Enum":
            for value, element in sorted(f["elements"].items()):
                members.append((element, str(value)))
        elif kind == "ExtEnum":
            for value, element in f["elements"].items():
                members.append((element, value))
        elif kind == "Record":
            members.extend(f["members"])
        elif kind == "WeakRef":
            members.extend(("", p) for p in f["path"])
        size = f.get("size", f.get("count", 0))
        signed = str(bool(f.get("signed", False))).lower()
        out.append(f"    {{TypeKind::{kind}, {c_string(name)}, {c_string(auid)}, {c_string(f.get('element'))}, "
                   f"{size}, {signed}, {first}, {len(members) - first}}},")
    out.append("};")
    out.append("")
    out.append("const ModelMember kModelMembers[] = {")
    for name, value in members:
        out.append(f"    {{{c_string(name)}, {c_string(value)}}},")
    out.append("};")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pyaaf2", required=True, help="a checkout of pyaaf2")
    ap.add_argument("--check", action="store_true", help="compare with the file in the repository")
    args = ap.parse_args()
    model = os.path.join(args.pyaaf2, "src", "aaf2", "model")
    tables = [
        runpy.run_path(os.path.join(model, "classdefs.py")),
        runpy.run_path(os.path.join(model, "ext", "classdefs.py")),
    ]
    type_tables = [
        runpy.run_path(os.path.join(model, "typedefs.py")),
        runpy.run_path(os.path.join(model, "ext", "typedefs.py")),
    ]
    text = render(merge_classes(tables), merge_types(type_tables))
    if args.check:
        with open(OUT, encoding="utf-8") as f:
            if f.read() != text:
                print(f"make-aaf-model: {OUT} is not current")
                return 1
        print("make-aaf-model: current")
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"make-aaf-model: wrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
