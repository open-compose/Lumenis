#!/usr/bin/env python3
"""Makes and updates the interface's translations (ROADMAP 10.4; docs/spec/localisation.md).

Each language in translations/languages.json has translations/cutthulhu_<code>.ts, a Qt Linguist file of every text
the user sees: the code's tr() texts, and the JSON resources' texts (commands, menus, settings, the Command Palette's
tabs, the built-in names of names.json, and effect names, categories, parameters and their choices). lupdate (Qt's Linguist tools) finds the first; this
script writes the second into a temporary C++ file of QT_TRANSLATE_NOOP lines for lupdate to read. Effect texts are
extracted as model::shownName shows them (the renames in src/model/shown_names.cpp applied), so a translator never
sees, and never brings back, a name we renamed.

    python3 scripts/update-translations.py            # update every .ts file (new texts added, gone ones marked)
    python3 scripts/update-translations.py --check    # fail if a .ts file is out of date (ctest translations_current)
    python3 scripts/update-translations.py --lupdate /path/to/lupdate

Translated texts are kept: lupdate only adds new source texts, and marks vanished ones obsolete.
"""

import argparse
import json
import xml.etree.ElementTree as ElementTree
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESOURCES = os.path.join(ROOT, "src", "ui", "resources")
TRANSLATIONS = os.path.join(ROOT, "translations")


def find_lupdate(given):
    if given:
        return given
    for name in ("lupdate", "lupdate-qt6"):
        path = shutil.which(name)
        if path:
            return path
    for folder in ("/usr/lib/qt6/bin", "/usr/lib/qt6/libexec", "/opt/homebrew/opt/qt/bin"):
        path = os.path.join(folder, "lupdate")
        if os.path.exists(path):
            return path
    qt = os.environ.get("QT_ROOT_DIR")
    if qt:
        for name in ("lupdate", "lupdate.exe"):
            path = os.path.join(qt, "bin", name)
            if os.path.exists(path):
                return path
    return None


def renames():
    """The (stored, shown) pairs of src/model/shown_names.cpp's kShown, in order."""
    text = open(os.path.join(ROOT, "src", "model", "shown_names.cpp"), encoding="utf-8").read()
    table = text[text.index("kShown"):]
    table = table[:table.index("};")]
    return re.findall(r'\{"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}', table)


def shown(text, pairs):
    for stored, ours in pairs:
        text = text.replace(stored, ours)
    return text


def resource_texts():
    """(context, text) pairs from the JSON resources, in a stable order."""
    load = lambda name: json.load(open(os.path.join(RESOURCES, name), encoding="utf-8"))
    out = []

    def add(context, text):
        if isinstance(text, str) and text.strip() and (context, text) not in seen:
            seen.add((context, text))
            out.append((context, text))

    seen = set()
    for c in load("commands.json")["commands"]:
        add("commands", c.get("text"))

    def menu_titles(items):
        for item in items:
            if isinstance(item, dict):
                add("menus", item.get("title"))
                menu_titles(item.get("items", []))

    menu_titles(load("menus.json")["menus"])
    for kind in load("settings.json")["settings"]:
        add("settings", kind.get("kind"))
        for v in kind.get("versions", []):
            add("settings", v.get("name"))
        for tab in kind.get("tabs", []):
            add("settings", tab.get("name"))
            for o in tab.get("options", []):
                for key in ("label", "suffix", "specialText", "resetButton"):
                    add("settings", o.get(key))
                for c in o.get("choices", []):
                    add("settings", c.get("label"))
    for tab in load("command_palette.json")["tabs"]:
        add("commandPalette", tab.get("name"))
    names = load("names.json")
    for context in ("binColumns", "binViews", "workspaces"):
        for name in names.get(context, []):
            add(context, name)
    pairs = renames()
    effects = load("effects.json")
    for tab in effects.get("tabs", []):
        add("effects", shown(tab.get("name", ""), pairs))
    for category in effects["categories"]:
        add("effects", shown(category.get("name", ""), pairs))
        for e in category.get("effects", []):
            add("effects", shown(e.get("name", ""), pairs))
    for category in load("effect_parameters.json")["categories"]:
        add("effects", shown(category.get("name", ""), pairs))
        for p in category.get("parameters", []):
            add("effects", shown(p.get("name", ""), pairs))
            for o in p.get("options", []):
                add("effects", shown(o, pairs))
    return out


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def languages():
    data = json.load(open(os.path.join(TRANSLATIONS, "languages.json"), encoding="utf-8"))
    return [l["code"] for l in data["languages"]]


def run_lupdate(lupdate, ts_files, work):
    """lupdate over the code and the resources' texts, into ts_files."""
    resources = os.path.join(work, "resource_texts.cpp")
    with open(resources, "w", encoding="utf-8") as f:
        f.write("// Written by scripts/update-translations.py for lupdate: the JSON resources' texts.\n")
        for context, text in resource_texts():
            f.write(f"QT_TRANSLATE_NOOP({cpp_string(context)}, {cpp_string(text)});\n")
    sources = [os.path.join(ROOT, "src"), os.path.join(ROOT, "app"), resources]
    command = [lupdate, "-silent", "-locations", "none", "-extensions", "cpp,h",
               "-source-language", "en"] + sources + ["-ts"] + ts_files
    run = subprocess.run(command, capture_output=True, text=True)
    if run.returncode != 0:
        sys.stderr.write(run.stdout + run.stderr)
        sys.exit(f"lupdate failed ({run.returncode})")


def messages(path):
    """The (context, source, disambiguation) of each message still in use: Qt versions write the same file differently,
    so the check compares what is in it."""
    if not os.path.exists(path):
        return None
    out = set()
    for context in ElementTree.parse(path).getroot().iter("context"):
        name = context.findtext("name")
        for message in context.iter("message"):
            translation = message.find("translation")
            if translation is not None and translation.get("type") in ("obsolete", "vanished"):
                continue
            out.add((name, message.findtext("source"), message.findtext("comment") or ""))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if a .ts file is out of date")
    parser.add_argument("--lupdate", help="the lupdate to run (default: found on PATH or in Qt's folders)")
    args = parser.parse_args()
    lupdate = find_lupdate(args.lupdate)
    if not lupdate:
        sys.exit("lupdate not found: install Qt's Linguist tools (BUILDING.md, Translations)")
    codes = languages()
    targets = [os.path.join(TRANSLATIONS, f"cutthulhu_{code}.ts") for code in codes]
    with tempfile.TemporaryDirectory() as work:
        if not args.check:
            run_lupdate(lupdate, targets, work)
            print(f"translations: updated {len(targets)} files")
            return 0
        # The check: the same update on copies, compared with the files as they are.
        copies = []
        for code, target in zip(codes, targets):
            copy = os.path.join(work, f"cutthulhu_{code}.ts")
            if os.path.exists(target):
                shutil.copy(target, copy)
            copies.append(copy)
        run_lupdate(lupdate, copies, work)
        stale = [os.path.basename(t) for t, c in zip(targets, copies) if messages(t) is None or messages(t) != messages(c)]
        if stale:
            print("translations: out of date: " + ", ".join(stale) + " (run scripts/update-translations.py)")
            return 1
        print("translations: current")
        return 0


if __name__ == "__main__":
    sys.exit(main())
