#!/usr/bin/env python3
"""The virtual environment the interchange checks run in (BUILDING.md, "Python packages for the
checks").

    check-python.py [--venv DIR] [--github-env]

Makes DIR (default: $CUTTHULHU_CHECK_VENV, else ~/.cutthulhu-venv) a virtual environment holding
the packages in scripts/check-requirements.txt (olefile, pyaaf2, pyavb, opentimelineio), checks
that each imports, and prints the environment's Python, the one to configure with
(cmake --preset ci -DPython3_EXECUTABLE=...). Run again, it does nothing unless the requirements
changed or the environment broke (its base Python was upgraded or removed); then it is made again.

--github-env also writes CHECK_PYTHON=<that Python> to $GITHUB_ENV, for CI's configure step.

Runs on any Python 3.8 or later, the environment's own included. Exits 1, saying what to install,
when the environment cannot be made or a package cannot be installed.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REQUIREMENTS = ROOT / "scripts" / "check-requirements.txt"
STAMP = "cutthulhu-requirements.txt"  # a copy of what was installed, inside the environment
MODULES = ["olefile", "aaf2", "avb", "opentimelineio"]


def python_of(env: Path) -> Path:
    return env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def works(python: Path) -> bool:
    if not python.exists():
        return False
    run = subprocess.run([str(python), "-c", "import " + ", ".join(MODULES)], capture_output=True)
    return run.returncode == 0


def hint() -> str:
    if sys.platform.startswith("linux"):
        return ("on Ubuntu, sudo apt-get install -y python3-venv python3-dev (pip compiles "
                "opentimelineio when it has no wheel for this Python, which needs the headers)")
    if sys.platform == "darwin":
        return "install Homebrew's Python (brew install python), which opentimelineio has wheels for"
    return "install Python 3.12 for all users (winget install --id Python.Python.3.12 -e --scope machine)"


def make(env: Path) -> None:
    if env.exists():
        shutil.rmtree(env)
    print(f"check-python: making {env} with {sys.executable}")
    try:
        venv.EnvBuilder(with_pip=True, clear=True).create(env)
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"check-python: cannot make {env}: {e}; {hint()}", file=sys.stderr)
        sys.exit(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    default = os.environ.get("CUTTHULHU_CHECK_VENV") or str(Path.home() / ".cutthulhu-venv")
    ap.add_argument("--venv", default=default, help=f"the environment (default {default})")
    ap.add_argument("--github-env", action="store_true", help="write CHECK_PYTHON to $GITHUB_ENV")
    args = ap.parse_args()

    env = Path(args.venv).expanduser()
    python = python_of(env)
    wanted = REQUIREMENTS.read_text(encoding="utf-8")
    stamp = env / STAMP
    current = stamp.exists() and stamp.read_text(encoding="utf-8") == wanted
    if not (current and works(python)):
        # A broken environment (its base Python upgraded or gone) cannot even run pip: make it again.
        if not python.exists() or subprocess.run([str(python), "-c", "pass"], capture_output=True).returncode != 0:
            make(env)
        print(f"check-python: installing {', '.join(MODULES)} into {env}")
        pip = [str(python), "-m", "pip", "install", "--disable-pip-version-check", "-q", "-r", str(REQUIREMENTS)]
        if subprocess.run(pip).returncode != 0:
            print(f"check-python: pip install failed; {hint()}", file=sys.stderr)
            return 1
        if not works(python):
            print(f"check-python: installed, but {', '.join(MODULES)} do not all import; {hint()}", file=sys.stderr)
            return 1
        stamp.write_text(wanted, encoding="utf-8")
    print(python)
    if args.github_env:
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as f:
            f.write(f"CHECK_PYTHON={python}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
