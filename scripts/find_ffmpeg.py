"""Finds the ffmpeg command for the Phase 2 gate scripts.

In order: the path given (--ffmpeg), PATH, FFMPEG_ROOT/bin, then on Windows
the newest build scripts/get-ffmpeg.ps1 unpacked into
%LOCALAPPDATA%\\Cutthulhu\\ffmpeg (it prints FFMPEG_ROOT but does not
change PATH).
"""

import glob
import os
import re
import shutil

EXE = "ffmpeg.exe" if os.name == "nt" else "ffmpeg"


def _version(folder):
    m = re.match(r"ffmpeg-n([\d.]+)-", os.path.basename(os.path.dirname(folder)))
    return tuple(int(p) for p in m.group(1).split(".") if p) if m else ()


def candidates():
    """(where, path) for each place looked in, in order."""
    found = []
    on_path = shutil.which("ffmpeg")
    found.append(("PATH", on_path))
    root = os.environ.get("FFMPEG_ROOT")
    found.append(("FFMPEG_ROOT", os.path.join(root, "bin", EXE) if root else None))
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        base = os.path.join(os.environ["LOCALAPPDATA"], "Cutthulhu", "ffmpeg")
        bins = sorted(glob.glob(os.path.join(base, "ffmpeg-n*", "bin")), key=_version, reverse=True)
        found.append((f"{base} (scripts/get-ffmpeg.ps1)", os.path.join(bins[0], EXE) if bins else None))
    return found


def find(explicit=None):
    if explicit:
        return explicit if os.path.isfile(explicit) or shutil.which(explicit) else None
    for _, path in candidates():
        if path and os.path.isfile(path):
            return path
    return None


def explain(explicit=None):
    """Why nothing was found: each place looked in."""
    if explicit:
        return f"--ffmpeg {explicit}: no such file"
    lines = []
    for where, path in candidates():
        if not path:
            lines.append(f"  {where}: not set" if where == "FFMPEG_ROOT" else f"  {where}: no ffmpeg")
        else:
            lines.append(f"  {where}: {path} does not exist")
    return "\n".join(lines)
