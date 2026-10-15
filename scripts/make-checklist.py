#!/usr/bin/env python3
"""Make the Phase 1 gate 5 checklist (docs/checklists/phase1-gate5.md).

Gate 5 (ROADMAP §5): an experienced Media Composer editor finds every window
and menu command without help. The checklist lists the windows (below) and
every item of the real menu bar, as the application reports it with a
project open (`--dump-menus <file> --sample-project`), with the Editing
Guide page from tests/data/menu_inventory.json.

    make-checklist.py --app build/ci/bin/cutthulhu            # write it
    make-checklist.py --app build/ci/bin/cutthulhu --check    # is it current?

--check exits 1 when the committed checklist differs, so it cannot go stale
silently (ctest: checklist_current).
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The windows of ROADMAP Phase 1, how an editor opens them, and where the
# guide describes them (EG = Editing Guide, FX = FX Guide).
WINDOWS = [
    ("Select Project", "At startup, or File > Close Project", "EG p.61-67"),
    ("Bin Container and its sidebar", "The main window; Tools > Project (Ctrl+9)", "EG p.239-243"),
    ("Bin views: Text, Frame, Script", "The buttons at the bottom of a bin; Bin View menu", "EG p.244-256"),
    ("Composer: Source and Record monitors", "The main window; the Composer menu", "EG p.378-430"),
    ("Timeline: tracks, patching, smart tools", "The main window; Tools > Timeline (Ctrl+0)", "EG p.591-645"),
    ("Trim mode", "The Trim Mode button or a lasso around a cut", "EG p.647-665"),
    ("Settings window and its dialogs", "File > Settings", "EG p.1136-1224"),
    ("Keyboard setting", "File > Settings > User > Keyboard", "EG p.1202"),
    ("Workspaces: W1-W12, Workspace View", "Windows > Workspaces; the workspace bar", "EG p.86-91"),
    ("Tool Palette", "File > Settings > User > Tool Palette", "EG p.392"),
    ("Command Palette and button mapping", "Tools > Command Palette (Ctrl+3)", "EG p.96-100"),
    ("Calculator", "Tools > Calculator (Ctrl+2)", "EG p.100"),
    ("Console", "Tools > Console (Ctrl+6)", "EG p.101"),
    ("Effect Palette", "Tools > Effect Palette (Ctrl+8)", "FX p.37-38"),
    ("Effect Editor", "Tools > Effect Editor", "FX p.83-88"),
    ("Audio Mixer", "Tools > Audio Mixer", "EG p.690-726"),
    ("Audio Tool", "Tools > Audio Tool (Ctrl+1)", "EG p.157-159"),
    ("Media Tool", "Tools > Media Tool", "EG p.344-348"),
    ("About: Configuration, Usage, Hardware", "Help > About", "EG p.94, p.102-105"),
]


def dump_menus(app):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "menus.json")
        env = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("QT_QPA_PLATFORM", "offscreen"))
        subprocess.run([app, "--dump-menus", out, "--sample-project"], check=True, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(out, encoding="utf-8") as f:
            return json.load(f)


def pages(inventory):
    """Menu path ("File/New/New Script") -> page."""
    found = {}

    def walk(items, path):
        for item in items:
            if "text" not in item:
                continue
            p = path + [item["text"]]
            if "page" in item:
                found["/".join(p)] = item["page"]
            walk(item.get("items", []), p)

    walk(inventory["menus"], [])
    return found


def cell(text):
    return text.replace("|", "\\|")


def render(menus, page_of):
    lines = []
    out = lines.append
    out("# Phase 1 gate 5: the editor's checklist")
    out("")
    out("<!-- Made by scripts/make-checklist.py from the application's menu bar")
    out("     (--dump-menus --sample-project) and tests/data/menu_inventory.json.")
    out("     Do not edit by hand: run the script; ctest checklist_current checks it. -->")
    out("")
    out("Gate 5 (ROADMAP §5): an experienced Media Composer editor finds every window")
    out("and menu command without help. Copy this file, fill it in, and send it back.")
    out("")
    out("**How to run the session**")
    out("* Install the latest build (the builds folder in Documents, BUILDING.md).")
    out("* Start with a new user and a new project; nothing else is open.")
    out("* The editor gets no help, documentation or hints during the session.")
    out("* For each entry, the editor finds it the way they would in Media Composer.")
    out("  * Tick **Found** if they found it without help.")
    out("  * In **Where**, write where they looked first, if it was not where they")
    out("    found it.")
    out("  * In **Notes**, write anything that differs from Media Composer.")
    out("* Items marked *later* are in their menu but dimmed: their feature comes in")
    out("  a later phase. They still count: the editor must find them.")
    out("* The gate passes when every entry is found.")
    out("")
    out("Editor: ______  Date: ______  Platform: ______  Build: ______")
    out("")
    out("## Windows")
    out("")
    out("| Window | Where it is | Guide | Found | Where | Notes |")
    out("|---|---|---|---|---|---|")
    for name, where, guide in WINDOWS:
        out(f"| {cell(name)} | {cell(where)} | {guide} | ☐ | | |")
    out("")

    total = 0
    working = 0
    for menu in menus:
        out(f"## {menu['text']} menu")
        out("")
        out("| Command | Shortcut | Guide | Status | Found | Where | Notes |")
        out("|---|---|---|---|---|---|---|")

        def walk(items, path):
            nonlocal total, working
            for item in items:
                if "text" not in item:
                    continue
                p = path + [item["text"]]
                if "items" in item:
                    walk(item["items"], p)
                    continue
                total += 1
                handled = item.get("handled", item.get("enabled", False))
                working += 1 if handled else 0
                page = page_of.get("/".join(p))
                guide = f"p.{page}" if page else ""
                name = " > ".join(p[1:])
                status = "works" if handled else "*later*"
                out(f"| {cell(name)} | {item.get('shortcut', '')} | {guide} | {status} | ☐ | | |")

        walk(menu.get("items", []), [menu["text"]])
        out("")

    out("## Summary")
    out("")
    out(f"* {len(WINDOWS)} windows.")
    out(f"* {total} menu commands, {working} of them working with a project open; the")
    out("  rest come in later phases.")
    out("* Found: ____ of ____.")
    out("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", help="the cutthulhu executable")
    ap.add_argument("--dump", help="a menu dump made with --dump-menus --sample-project, instead of --app")
    ap.add_argument("--inventory", default=os.path.join(ROOT, "tests", "data", "menu_inventory.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "checklists", "phase1-gate5.md"))
    ap.add_argument("--check", action="store_true", help="exit 1 if --out differs instead of writing it")
    args = ap.parse_args()
    if bool(args.app) == bool(args.dump):
        ap.error("give --app or --dump")
    if args.app:
        menus = dump_menus(args.app)
    else:
        with open(args.dump, encoding="utf-8") as f:
            menus = json.load(f)
    with open(args.inventory, encoding="utf-8") as f:
        text = render(menus, pages(json.load(f)))
    if args.check:
        try:
            with open(args.out, encoding="utf-8") as f:
                current = f.read()
        except FileNotFoundError:
            current = ""
        if current != text:
            print(f"{args.out} is out of date: run scripts/make-checklist.py --app <cutthulhu>", file=sys.stderr)
            return 1
        print("checklist: current")
        return 0
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
