#!/usr/bin/env python3
"""MXF we write against an MXF validator (ROADMAP Phase 2, gate 2).

Runs bmx's mxf2raw (https://github.com/bbc/bmx, BSD licence; a tool, not
shipped with Cutthulhu) on MXF files, or on every .mxf under folders:

    check-mxf.py "<drive>/Avid MediaFiles/MXF" [more files or folders]

Each file must open, have every essence track complete and an index. The
other half of the gate, linking the files into a real Media Composer, is done
by hand (M). Exits 77 (skipped) without mxf2raw, 1 when a file fails.
"""

import os
import shutil
import subprocess
import sys


def files(args):
    for a in args:
        if os.path.isdir(a):
            for root, _, names in os.walk(a):
                for n in sorted(names):
                    if n.lower().endswith(".mxf"):
                        yield os.path.join(root, n)
        else:
            yield a


def main():
    tool = shutil.which("mxf2raw")
    if not tool:
        print("check-mxf: mxf2raw (bmx) is not installed; skipped")
        return 77
    failed = 0
    checked = 0
    for path in files(sys.argv[1:]):
        checked += 1
        r = subprocess.run([tool, "--info", "--check-complete", path], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        if r.returncode != 0:
            failed += 1
            print(f"{path}: FAILED\n{r.stdout.strip()[-800:]}")
        else:
            print(f"{path}: ok")
    print(f"check-mxf: {checked} files, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
