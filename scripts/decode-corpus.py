#!/usr/bin/env python3
"""The decode corpus (ROADMAP Phase 2, gate 1).

Makes about 150 short files with the FFmpeg command-line tool: every codec,
frame rate and raster the media library reads (docs/spec/media-formats.md)
whose encoder this FFmpeg has. For each file it compares the MD5 of every
decoded frame, read by Cutthulhu in a shuffled order (so every frame is
reached by seeking), with FFmpeg's own framemd5 of a straight decode.

    decode-corpus.py --app build/ci/bin/cutthulhu [--ffmpeg ffmpeg] [--dir DIR]

The files are kept in DIR (default: $CUTTHULHU_DECODE_CORPUS, else the
user's cache folder) and made again only when this FFmpeg's version changes:
the first run takes minutes, later ones about two. Exits 1 when a frame differs,
or when fewer than --min files could be made.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys

import find_ffmpeg

FRAMES = 10

# Raster and rate pairs: (name, width, height, rate).
HD = [
    ("1080p23976", 1920, 1080, "24000/1001"),
    ("1080p24", 1920, 1080, "24"),
    ("1080p25", 1920, 1080, "25"),
    ("1080p2997", 1920, 1080, "30000/1001"),
    ("1080p50", 1920, 1080, "50"),
    ("1080p5994", 1920, 1080, "60000/1001"),
    ("720p25", 1280, 720, "25"),
    ("720p5994", 1280, 720, "60000/1001"),
]
SD = [
    ("486p2997", 720, 486, "30000/1001"),
    ("480p2997", 720, 480, "30000/1001"),
    ("576p25", 720, 576, "25"),
]
LARGE = [
    ("2k24", 2048, 1080, "24"),
    ("uhd23976", 3840, 2160, "24000/1001"),
    ("uhd25", 3840, 2160, "25"),
]
SOME = [HD[0], HD[2], HD[3], HD[6], SD[2]]
HDV = [("1440p25", 1440, 1080, "25"), ("1440p2997", 1440, 1080, "30000/1001")]


def entries():
    """(name, encoder, container, ffmpeg output options, rasters, frames)."""
    out = []
    for profile, fmt in [("dnxhr_lb", "yuv422p"), ("dnxhr_sq", "yuv422p"), ("dnxhr_hq", "yuv422p"),
                         ("dnxhr_hqx", "yuv422p10le"), ("dnxhr_444", "yuv444p10le")]:
        out.append((profile, "dnxhd", "mxf", ["-profile:v", profile, "-pix_fmt", fmt], HD + LARGE[:2], FRAMES))
    # DNxHD at its fixed bit rates (1080p, 25 and 23.976).
    for rate, kbps in [("25", ["36M", "120M", "185M"]), ("24000/1001", ["36M", "115M", "175M"])]:
        for b in kbps:
            out.append((f"dnxhd_{b}", "dnxhd", "mov", ["-b:v", b, "-pix_fmt", "yuv422p"],
                        [(f"1080p{rate.replace('/', '_')}", 1920, 1080, rate)], FRAMES))
    for profile in range(5):
        fmt = "yuva444p10le" if profile == 4 else "yuv422p10le"
        out.append((f"prores{profile}", "prores_ks", "mov", ["-profile:v", str(profile), "-pix_fmt", fmt],
                    HD[:6] + [SD[2], LARGE[0]], FRAMES))
    gop = ["-g", "4", "-bf", "2"]
    out.append(("mpeg2", "mpeg2video", "mxf", gop + ["-pix_fmt", "yuv422p", "-b:v", "20M"], HD[:4] + [HD[7]] + SD[1:] + HDV, FRAMES))
    out.append(("mpeg2ts", "mpeg2video", "ts", gop + ["-b:v", "8M"], [HD[2]], FRAMES))
    out.append(("h264", "libx264", "mp4", gop + ["-pix_fmt", "yuv420p"], HD + [LARGE[0]], FRAMES))
    out.append(("h264intra", "libx264", "mov", ["-g", "1", "-pix_fmt", "yuv420p"], [HD[2], HD[6]], FRAMES))
    out.append(("hevc", "libx265", "mp4", gop + ["-pix_fmt", "yuv420p10le", "-x265-params", "log-level=error"],
                HD[:3] + [LARGE[2]], FRAMES))
    out.append(("mpeg4", "mpeg4", "avi", gop + ["-q:v", "4"], [HD[2], SD[2], HD[6]], FRAMES))
    out.append(("mjpeg", "mjpeg", "mov", ["-pix_fmt", "yuvj422p", "-q:v", "3"], SOME, FRAMES))
    out.append(("dv_ntsc", "dvvideo", "dv", ["-pix_fmt", "yuv411p"], [SD[1]], FRAMES))
    out.append(("dv_pal", "dvvideo", "dv", ["-pix_fmt", "yuv420p"], [SD[2]], FRAMES))
    out.append(("dv50", "dvvideo", "mov", ["-pix_fmt", "yuv422p"], [SD[2]], FRAMES))
    out.append(("ffv1", "ffv1", "mkv", ["-pix_fmt", "yuv422p10le"], SOME[:4], FRAMES))
    out.append(("uyvy", "rawvideo", "mov", ["-pix_fmt", "uyvy422"], [HD[2], SD[0], SD[2]], FRAMES))
    out.append(("v210", "v210", "mov", [], [HD[2], HD[3], SD[0]], FRAMES))
    out.append(("cfhd", "cfhd", "mov", ["-pix_fmt", "yuv422p10le"], [HD[2], HD[6]], FRAMES))
    out.append(("vp9", "libvpx-vp9", "webm", ["-g", "4", "-deadline", "realtime", "-cpu-used", "8"],
                [HD[2], HD[6]], FRAMES))
    out.append(("av1", "libsvtav1", "mkv", ["-g", "4", "-preset", "12"], [HD[6]], FRAMES))
    out.append(("av1aom", "libaom-av1", "mkv", ["-g", "4", "-cpu-used", "8", "-row-mt", "1"], [SD[2]], 6))
    # Still sequences, read as one clip.
    for codec, ext, fmt in [("png", "png", "rgb24"), ("tiff", "tif", "rgb48le"), ("dpx", "dpx", "rgb48le"),
                            ("exr", "exr", "gbrpf32le")]:
        out.append((f"seq_{codec}", codec, "seq:" + ext, ["-pix_fmt", fmt], [HD[0], LARGE[0]], 6))
    return out


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)


def ffmpeg_hashes(ffmpeg, source):
    """FFmpeg's framemd5 of a straight decode, in presentation order."""
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error"]
    if source.get("seq"):
        cmd += ["-framerate", source["rate"], "-start_number", "1", "-i", source["pattern"]]
    else:
        cmd += ["-i", source["path"]]
    cmd += ["-map", "0:v:0", "-fps_mode", "passthrough", "-f", "framemd5", "-"]
    r = run(cmd)
    if r.returncode != 0:
        return None
    return [line.split(",")[-1].strip() for line in r.stdout.splitlines() if line and not line.startswith("#")]


def our_hashes(app, target):
    r = run([app, "--framemd5", target, "--shuffle"], env=dict(os.environ, QT_QPA_PLATFORM="offscreen"))
    if r.returncode not in (0, 2):
        return None, r.stderr.strip()
    return [line.split()[1] for line in r.stdout.splitlines() if line.strip()], ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True)
    ap.add_argument("--ffmpeg", default=None)
    ap.add_argument("--dir", default=None)
    ap.add_argument("--min", type=int, default=100, help="fewest files that must be made (default 100)")
    args = ap.parse_args()

    ffmpeg = find_ffmpeg.find(args.ffmpeg)
    if not ffmpeg:
        print("decode corpus: no ffmpeg command found; skipped. Looked in:\n" + find_ffmpeg.explain(args.ffmpeg))
        return 77  # ctest's SKIP_RETURN_CODE
    version = run([ffmpeg, "-version"]).stdout.splitlines()[0]
    encoders = run([ffmpeg, "-hide_banner", "-encoders"]).stdout
    have = {line.split()[1] for line in encoders.splitlines()[10:] if len(line.split()) > 1}

    folder = args.dir or os.environ.get("CUTTHULHU_DECODE_CORPUS")
    if not folder:
        cache = os.environ.get("LOCALAPPDATA") if os.name == "nt" else None
        if not cache:
            cache = os.path.join(os.path.expanduser("~"), "Library", "Caches") if sys.platform == "darwin" \
                else os.environ.get("XDG_CACHE_HOME", os.path.join(os.path.expanduser("~"), ".cache"))
        folder = os.path.join(cache, "cutthulhu", "decode-corpus")
    stamp = os.path.join(folder, "ffmpeg-version.txt")
    if os.path.exists(stamp) and open(stamp).read() != version:
        shutil.rmtree(folder)
    os.makedirs(folder, exist_ok=True)
    with open(stamp, "w") as f:
        f.write(version)

    made = skipped = failed = same = 0
    unavailable = set()
    report = []
    for name, encoder, container, options, rasters, frames in entries():
        if encoder not in have:
            unavailable.add(encoder)
            continue
        for raster, width, height, rate in rasters:
            stem = f"{name}_{raster}"
            source = {"rate": rate}
            if container.startswith("seq:"):
                sub = os.path.join(folder, stem)
                os.makedirs(sub, exist_ok=True)
                pattern = os.path.join(sub, f"{stem}_%04d.{container[4:]}")
                source.update(seq=True, pattern=pattern)
                first = pattern % 1
                target = f"seq:{pattern}?first=1&count={frames}&rate={rate if '/' in rate else rate + '/1'}"
                out = pattern
            else:
                out = os.path.join(folder, f"{stem}.{container}")
                source["path"] = out
                first = out
                target = out
            if not os.path.exists(first):
                cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                       f"testsrc2=size={width}x{height}:rate={rate}", "-frames:v", str(frames), "-c:v", encoder]
                cmd += options
                if container.startswith("seq:"):
                    cmd += ["-start_number", "1"]
                cmd.append(out)
                r = run(cmd)
                if r.returncode != 0:
                    report.append({"file": stem, "result": "not made", "why": r.stderr.strip()[-300:]})
                    skipped += 1
                    continue
            made += 1
            want = ffmpeg_hashes(ffmpeg, source)
            got, why = our_hashes(args.app, target)
            if want is None:
                report.append({"file": stem, "result": "ffmpeg cannot decode it"})
                skipped += 1
                continue
            if got is None:
                # A codec this build leaves out (LUMENIS_PATENT_CODECS off).
                report.append({"file": stem, "result": "not read", "why": why})
                failed += 1
                continue
            bad = [i for i, (a, b) in enumerate(zip(want, got)) if a != b]
            if len(want) != len(got) or bad:
                failed += 1
                report.append({"file": stem, "result": "differs", "frames": len(want), "ours": len(got),
                               "first_bad": bad[0] if bad else min(len(want), len(got))})
            else:
                same += 1
                report.append({"file": stem, "result": "same", "frames": len(want)})

    with open(os.path.join(folder, "report.json"), "w") as f:
        json.dump({"ffmpeg": version, "report": report}, f, indent=1)
    for r in report:
        if r["result"] != "same":
            print(f"{r['file']}: {r['result']} {r.get('why', '') or ''}".rstrip())
    print(f"decode corpus: {made} files, {same} the same as FFmpeg frame for frame, {failed} not, "
          f"{skipped} not made or not decodable by FFmpeg ({version})")
    if unavailable:
        print("encoders this FFmpeg lacks: " + ", ".join(sorted(unavailable)))
    if failed:
        return 1
    if made < args.min:
        print(f"decode corpus: only {made} files; at least {args.min} are needed")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
