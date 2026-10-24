#!/usr/bin/env python3
"""Checks the GitHub Actions workflows (.github/workflows) with actionlint, so that a workflow
GitHub would refuse (it then runs no jobs at all, and reports only "failure") is caught before it
is pushed (BUILDING.md, "CI: a run fails with no jobs").

    check-workflows.py [--actionlint PATH]

actionlint comes from scripts/check-requirements.txt (the actionlint-py package puts the program
beside the virtual environment's Python), or from PATH. Its settings, the self-hosted runners'
labels, are in .github/actionlint.yaml. shellcheck and pyflakes are not run on the steps.

Exits 0 when the workflows are sound, 1 with actionlint's findings, and 77 (ctest's skip) when
actionlint is not installed.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def find_actionlint(given: str | None) -> str | None:
    if given:
        return given
    # Beside this Python: the check environment's bin (Scripts on Windows).
    here = Path(sys.executable).parent
    for name in ("actionlint", "actionlint.exe"):
        if (here / name).is_file():
            return str(here / name)
    return shutil.which("actionlint")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--actionlint", help="the actionlint program")
    args = parser.parse_args()
    program = find_actionlint(args.actionlint)
    if not program:
        print("check-workflows: actionlint is not installed (scripts/check-python.py installs it); skipped")
        return 77
    workflows = sorted(str(p) for p in (ROOT / ".github" / "workflows").glob("*.yml"))
    run = subprocess.run([program, "-config-file", str(ROOT / ".github" / "actionlint.yaml"),
                          "-shellcheck=", "-pyflakes=", *workflows], cwd=ROOT)
    if run.returncode == 0:
        print(f"check-workflows: {len(workflows)} workflows are sound")
    return 0 if run.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
