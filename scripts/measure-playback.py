#!/usr/bin/env python3
"""Real-time playback and A/V sync (ROADMAP Phase 2, gates 3 and 4).

Makes a 1080p25 DNxHD 120 QuickTime file with a tone, as long as the run,
with the FFmpeg command-line tool, then plays it with
`cutthulhu --measure-playback` and checks:

* gate 3: no frame dropped;
* gate 4: lip sync within half a frame: each time the picture moves on to a
  frame, the sound is less than half a frame into it.

    measure-playback.py --app build/ci/bin/cutthulhu [--seconds 600]

The gate runs are 10 minutes (gate 3) and 60 minutes (gate 4) on the
reference machines (M1 Mac, mid-range Windows and Linux), with a sound card;
shorter runs are for trying it. The file is kept in the user's cache folder.

Exit codes: 0 passed, 1 a gate not passed, 2 the application failed (its
exit code is printed), 77 no ffmpeg command.
"""

import argparse
import json
import os
import subprocess
import sys

import find_ffmpeg


def cache_folder():
    if os.environ.get("CUTTHULHU_DECODE_CORPUS"):
        return os.environ["CUTTHULHU_DECODE_CORPUS"]
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        base = os.environ["LOCALAPPDATA"]
    elif sys.platform == "darwin":
        base = os.path.join(os.path.expanduser("~"), "Library", "Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME", os.path.join(os.path.expanduser("~"), ".cache"))
    return os.path.join(base, "cutthulhu", "playback")


WINDOWS_CODES = {
    0xC0000005: "access violation",
    0xC0000374: "heap corruption",
    0xC0000409: "stack buffer overrun or abort",
    0xC000041D: "an exception in a callback",
    0x40000015: "abort",
    0x80000003: "breakpoint (assert)",
}


def describe(code):
    """An exit code as Windows or a POSIX system reports it."""
    if code < 0:
        return f"{code} (killed by signal {-code})"
    if os.name == "nt" and code > 0xFFFF:
        name = WINDOWS_CODES.get(code & 0xFFFFFFFF, "")
        return f"0x{code & 0xFFFFFFFF:08X}" + (f" ({name})" if name else "")
    return str(code)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True)
    ap.add_argument("--ffmpeg", default=None, help="the ffmpeg command (default: found as find_ffmpeg.py says)")
    ap.add_argument("--seconds", type=int, default=600)
    ap.add_argument("--offscreen", action="store_true", help="no window (CI); playback then paints off screen")
    args = ap.parse_args()
    explicit = args.ffmpeg
    args.ffmpeg = find_ffmpeg.find(explicit)
    if not args.ffmpeg:
        print("measure-playback: no ffmpeg command found. Looked in:\n" + find_ffmpeg.explain(explicit))
        print("measure-playback: ffmpeg makes the test file. Install it "
              "(Ubuntu: sudo apt install ffmpeg; Fedora: sudo dnf install ffmpeg-free; macOS: brew install ffmpeg; "
              "Windows: scripts/get-ffmpeg.ps1), or name it with --ffmpeg.")
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            print("measure-playback: also, do not run it with sudo. sudo resets PATH, and root usually has "
                  "no connection to your sound server.")
        return 77
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        print("measure-playback: running as root (sudo). It does not need it; the sound may then go nowhere, "
              "and the file is kept in root's cache folder.")
    folder = cache_folder()
    os.makedirs(folder, exist_ok=True)
    media = os.path.join(folder, f"dnxhd120_1080p25_{args.seconds}s.mov")
    if not os.path.exists(media):
        print(f"making {media} ...", flush=True)
        r = subprocess.run([args.ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
                            "-f", "lavfi", "-i", "testsrc2=size=1920x1080:rate=25",
                            "-f", "lavfi", "-i", "sine=f=1000:r=48000:beep_factor=4",
                            "-t", str(args.seconds + 2), "-c:v", "dnxhd", "-b:v", "120M", "-pix_fmt", "yuv422p",
                            "-c:a", "pcm_s24le", "-ac", "2", media])
        if r.returncode != 0:
            return 1
    env = dict(os.environ)
    if args.offscreen:
        env["QT_QPA_PLATFORM"] = "offscreen"
    r = subprocess.run([args.app, "--measure-playback", media, "--seconds", str(args.seconds)],
                       stdout=subprocess.PIPE, text=True, env=env)
    try:
        result = json.loads(r.stdout) if r.stdout.strip() else None
    except ValueError:
        result = None
    if r.returncode != 0:
        print(f"measure-playback: the application failed: exit code {describe(r.returncode)}, "
              + ("after writing its result" if result else "without a result"))
        if r.stdout.strip() and not result:
            print(r.stdout)
    if not result:
        return 2
    print(json.dumps(result, indent=1))
    ok = r.returncode == 0
    expected = args.seconds * 25
    if result["dropped"] != 0:
        print(f"gate 3: {result['dropped']} frames dropped of {result['shown']} shown")
        ok = False
    if result["shown"] < expected * 0.95:
        print(f"gate 3: only {result['shown']} of about {expected} frames shown")
        ok = False
    if not result["sound"]:
        print("gate 4: no sound was heard; is there an audio output?")
        ok = False
    elif result["lipSyncError"] > 0.5:
        print(f"gate 4: lip sync off by {result['lipSyncError']:.2f} frame (at most 0.5)")
        ok = False
    print("playback gates: " + ("passed" if ok else "not passed"))
    if r.returncode != 0:
        return 2
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
