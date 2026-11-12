#!/usr/bin/env python3
"""The trademark check (ROADMAP 9.8; docs/spec/trademarks.md): Avid's marks may not name our menus, commands, windows,
settings or effects.

Reads tests/data/trademarks.json and looks through every user-visible string: the values in src/ui/resources/*.json
(but "comment", "source", "page" and similar notes) and the tr() strings in src/. A mark whose status is "renamed" or
"absent" fails the check when it is found; a "planned" one is listed with the step that renames it; a "nominative" one
is allowed, and its name is taken out of a string before the other marks are looked for (so "Avid DNx HQ" does not
count as "Avid"). Exit status 1 on a failure. ctest trademark_names runs it.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NOTES = {"comment", "source", "page", "pages", "note", "notes", "guide", "id", "command", "key", "icon", "kind"}


def json_strings(value, key=None):
    if isinstance(value, dict):
        for k, v in value.items():
            if k not in NOTES:
                yield from json_strings(v, k)
    elif isinstance(value, list):
        for v in value:
            yield from json_strings(v, key)
    elif isinstance(value, str) and key not in NOTES:
        yield value


TR = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"')


def visible_strings():
    for path in sorted((ROOT / "src" / "ui" / "resources").glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        for s in json_strings(data):
            yield path, s
    for path in sorted((ROOT / "src").rglob("*.cpp")) + sorted((ROOT / "src").rglob("*.h")):
        for s in TR.findall(path.read_text(encoding="utf-8", errors="replace")):
            yield path, s


def pattern(name):
    return re.compile(r"(?<![A-Za-z])" + re.escape(name) + r"(?![a-z])")


def main():
    marks = json.loads((ROOT / "tests" / "data" / "trademarks.json").read_text(encoding="utf-8"))["marks"]
    nominative = sorted((m["name"] for m in marks if m["status"] == "nominative"), key=len, reverse=True)
    checked = [(m, pattern(m["name"])) for m in marks if m["status"] != "nominative"]
    failures, planned = [], {}
    for path, s in visible_strings():
        rest = s
        for n in nominative:
            rest = pattern(n).sub(" ", rest)
        for m, p in checked:
            if not p.search(rest):
                continue
            where = f"{path.relative_to(ROOT)}: \"{s[:80]}\""
            if m["status"] == "planned":
                planned.setdefault(m["name"], []).append(where)
            else:
                failures.append(f"{m['name']} ({m['status']}; ours: {m.get('ours', '-')}): {where}")
    for name, places in sorted(planned.items()):
        step = next(m["step"] for m in marks if m["name"] == name)
        print(f"planned for {step}: {name}, {len(places)} string(s), e.g. {places[0]}")
    for f in failures:
        print("FAIL", f)
    if failures:
        print(f"trademark names: {len(failures)} user-visible string(s) use a mark that is renamed or must not appear")
        return 1
    print(f"trademark names: no renamed or absent mark in a user-visible string ({sum(len(v) for v in planned.values())} planned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
