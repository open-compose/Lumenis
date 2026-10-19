#!/usr/bin/env python3
"""MXF we write against an MXF validator (ROADMAP Phase 2, gate 2).

Runs bmx's mxf2raw (https://github.com/bbc/bmx, BSD licence; a tool, not
shipped with Cutthulhu) on MXF files, or on every .mxf under folders:

    check-mxf.py "<drive>/Avid MediaFiles/MXF" [more files or folders]

or makes the media first: a 1080p25 test picture with a stereo tone (with
the ffmpeg command), transcoded by Cutthulhu into every DNx resolution, as
Import and Transcode write it:

    check-mxf.py --make --app build/ci/bin/cutthulhu [--keep DIR]

Each file must open, be complete, read to its last frame (every frame is
read for a checksum), be OP-Atom with one track, and carry real creation
and modified dates. With --make, a clip's track files must also share their
material package (the master clip) and each have its own file package. The other half of the gate, linking the files
into a real Media Composer, is done by hand (M). Exits 77 (skipped) without
mxf2raw (or, with --make, without ffmpeg), 1 when a file fails.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

import find_ffmpeg


def files(args):
    for a in args:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                for n in sorted(names):
                    if n.lower().endswith(".mxf"):
                        yield os.path.join(root, n)
        else:
            yield a


def make(app, ffmpeg, folder):
    """The source and the media; returns the media drive."""
    source = os.path.join(folder, "Check.mov")
    r = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
                        "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=25",
                        "-f", "lavfi", "-i", "sine=f=1000:r=48000",
                        "-t", "2", "-c:v", "ffv1", "-c:a", "pcm_s24le", "-ac", "2", source])
    if r.returncode != 0:
        raise RuntimeError("ffmpeg could not make the source")
    drive = os.path.join(folder, "drive")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    r = subprocess.run([app, "--write-media", source, "--drive", drive], stdout=subprocess.PIPE, text=True, env=env)
    if r.returncode != 0:
        raise RuntimeError("cutthulhu --write-media failed")
    made = json.loads(r.stdout)
    print("made: " + ", ".join(f"{m['resolution']} ({len(m['files'])} files)" for m in made))
    return drive, made


def check(tool, path):
    """mxf2raw's verdict and what is wrong, if anything."""
    # --check-end: the last frame reads; --track-chksum: so does every other.
    r = subprocess.run([tool, "--info", "--check-complete", "--check-end", "--track-chksum", "md5", path],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.returncode != 0:
        return r.stdout.strip()[-800:]
    out = r.stdout
    # Media Composer's managed media: OP-Atom, one essence track a file.
    problems = []
    if "OP-Atom" not in out and "OPAtom" not in out:
        problems.append("not OP-Atom")
    tracks = sum(1 for line in out.splitlines() if line.strip().startswith("Track #") or line.strip().startswith("track #"))
    if tracks > 1:
        problems.append(f"{tracks} tracks")
    # FFmpeg writes zeros without a creation time (Media Composer's media
    # database keys on the dates).
    if "0-00-00 00:00:00" in out:
        problems.append("a creation or modified date is 0-00-00")
    return "; ".join(problems) if problems else None


def packages(tool, path):
    """The file's material and file source package UIDs, as mxf2raw prints them."""
    r = subprocess.run([tool, "--info", path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    found = {}
    section = None
    for line in r.stdout.splitlines():
        s = line.strip()
        if s.startswith("MaterialPackage #"):
            section = "material"
        elif s.startswith("FileSourcePackage #"):
            section = "file"
        elif s.startswith("package_uid") and section and section not in found:
            found[section] = s.split(":", 1)[1].strip()
    return found.get("material"), found.get("file")


def grouped(tool, made, folder):
    """A clip's track files share its material package (Media Composer makes
    one master clip of them) and each has a file source package of its own."""
    problems = []
    for clip in made:
        ids = [packages(tool, os.path.join(folder, f) if not os.path.isabs(f) else f) for f in clip["files"]]
        materials = {m for m, _ in ids}
        files = [f for _, f in ids]
        if len(materials) != 1 or None in materials:
            problems.append(f"{clip['resolution']}: its files have {len(materials)} material packages")
        if len(set(files)) != len(files) or None in files:
            problems.append(f"{clip['resolution']}: its files share file source packages")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--make", action="store_true", help="make the media with Cutthulhu first")
    ap.add_argument("--app", help="cutthulhu, for --make")
    ap.add_argument("--ffmpeg", default=None)
    ap.add_argument("--mxf2raw", default=None, help="bmx's mxf2raw (default: on PATH)")
    ap.add_argument("--keep", help="with --make: make the media in DIR and keep it")
    args = ap.parse_args()
    tool = args.mxf2raw or shutil.which("mxf2raw")
    if not tool:
        print("check-mxf: mxf2raw (bmx) is not installed; skipped (BUILDING.md, Phase 2 gates)")
        return 77
    paths = list(args.paths)
    work = None
    made = None
    if args.make:
        if not args.app:
            ap.error("--make needs --app")
        ffmpeg = find_ffmpeg.find(args.ffmpeg)
        if not ffmpeg:
            print("check-mxf: no ffmpeg command found; skipped. Looked in:\n" + find_ffmpeg.explain(args.ffmpeg))
            return 77
        if args.keep:
            os.makedirs(args.keep, exist_ok=True)
            folder = args.keep
        else:
            work = tempfile.TemporaryDirectory(prefix="check-mxf-")
            folder = work.name
        drive, made = make(args.app, ffmpeg, folder)
        paths.append(drive)
    if not paths:
        ap.error("no files or folders (or --make)")
    failed = 0
    checked = 0
    for path in files(paths):
        checked += 1
        problem = check(tool, path)
        if problem:
            failed += 1
            print(f"{path}: FAILED\n{problem}")
        else:
            print(f"{path}: ok")
    if checked == 0:
        print("check-mxf: no MXF files found")
        return 1
    if made is not None:
        for problem in grouped(tool, made, folder):
            failed += 1
            print(f"FAILED: {problem}")
    print(f"check-mxf: {checked} files, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
