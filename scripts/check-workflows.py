#!/usr/bin/env python3
"""Checks the GitHub Actions workflows (.github/workflows) with actionlint, so that a workflow
GitHub would refuse (it then runs no jobs at all, and reports only "failure") is caught before it
is pushed (BUILDING.md, "CI: a run fails with no jobs").

    check-workflows.py [--actionlint PATH]

actionlint (MIT, github.com/rhysd/actionlint) is a single program: the pinned release for this
system is downloaded once, checked against its SHA-256, into ~/.cache/cutthulhu (or
$CUTTHULHU_TOOLS). It is not a Python package of the checks: actionlint-py has no Windows wheel,
and building it there took 20 minutes, long enough to stop the other packages' install in CI.
Its settings are in .github/actionlint.yaml. shellcheck and
pyflakes are not run on the steps.

Exits 0 when the workflows are sound, 1 with actionlint's findings, and 77 (ctest's skip) when
actionlint can be neither found nor downloaded.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = "1.7.12"
# From the release's actionlint_<VERSION>_checksums.txt.
SHA256 = {
    "linux_amd64.tar.gz": "8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8",
    "linux_arm64.tar.gz": "325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6",
    "darwin_arm64.tar.gz": "aba9ced2dee8d27fecca3dc7feb1a7f9a52caefa1eb46f3271ea66b6e0e6953f",
    "darwin_amd64.tar.gz": "5b44c3bc2255115c9b69e30efc0fecdf498fdb63c5d58e17084fd5f16324c644",
    "windows_amd64.zip": "6e7241b51e6817ea6a047693d8e6fed13b31819c9a0dd6c5a726e1592d22f6e9",
    "windows_arm64.zip": "cadcf7ea4efe3a68728893813643cebe1185e5b1d4be5b96245f65c9a4d5ea41",
}


def asset() -> str | None:
    system = {"Linux": "linux", "Darwin": "darwin", "Windows": "windows"}.get(platform.system())
    machine = {"x86_64": "amd64", "amd64": "amd64", "arm64": "arm64", "aarch64": "arm64"}.get(platform.machine().lower())
    if not system or not machine:
        return None
    name = f"{system}_{machine}." + ("zip" if system == "windows" else "tar.gz")
    return name if name in SHA256 else None


def download() -> Path | None:
    name = asset()
    if not name:
        return None
    cache = Path(os.environ.get("CUTTHULHU_TOOLS") or Path.home() / ".cache" / "cutthulhu") / f"actionlint-{VERSION}"
    program = cache / ("actionlint.exe" if name.endswith(".zip") else "actionlint")
    if program.is_file():
        return program
    url = f"https://github.com/rhysd/actionlint/releases/download/v{VERSION}/actionlint_{VERSION}_{name}"
    try:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
    except OSError as e:
        print(f"check-workflows: cannot download {url}: {e}")
        return None
    if hashlib.sha256(data).hexdigest() != SHA256[name]:
        print(f"check-workflows: {url} is not the expected file (SHA-256 differs)")
        return None
    cache.mkdir(parents=True, exist_ok=True)
    member = program.name
    if name.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            program.write_bytes(z.read(member))
    else:
        with tarfile.open(fileobj=io.BytesIO(data)) as t:
            f = t.extractfile(member)
            if f is None:
                return None
            program.write_bytes(f.read())
        program.chmod(0o755)
    return program


def find_actionlint(given: str | None) -> str | None:
    if given:
        return given
    program = download()
    if program:
        return str(program)
    return shutil.which("actionlint")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--actionlint", help="the actionlint program")
    args = parser.parse_args()
    program = find_actionlint(args.actionlint)
    if not program:
        print("check-workflows: actionlint could not be found or downloaded; skipped")
        return 77
    workflows = sorted(str(p) for p in (ROOT / ".github" / "workflows").glob("*.yml"))
    run = subprocess.run([program, "-config-file", str(ROOT / ".github" / "actionlint.yaml"),
                          "-shellcheck=", "-pyflakes=", *workflows], cwd=ROOT)
    if run.returncode == 0:
        print(f"check-workflows: {len(workflows)} workflows are sound")
    return 0 if run.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
