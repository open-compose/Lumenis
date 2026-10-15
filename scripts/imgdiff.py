#!/usr/bin/env python3
"""Compare two screenshots and report how many pixels differ.

    scripts/imgdiff.py A.png B.png [--max-percent 0.5] [--tolerance 16] [--diff out.png]

A pixel counts as different when any channel differs by more than
--tolerance (0-255), which absorbs tiny antialiasing differences between
platforms' font rasterisers. Exits 0 when the share of differing pixels is
at most --max-percent, 1 when it is higher, 2 when the images cannot be
compared (missing, or different sizes). On failure it also prints where
the differences are: their bounding box and the busiest 40-pixel cells, so a
failure can be read from the CI log alone. Needs Pillow.

This is the check behind Phase 1 gate 1 (the same window must look the
same on macOS, Windows and Linux).
"""
import argparse
import sys

from PIL import Image, ImageChops


def compare(a_path, b_path, tolerance, diff_path=None):
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    if a.size != b.size:
        raise ValueError(f"size differs: {a.size} vs {b.size}")
    diff = ImageChops.difference(a, b)
    # Max over channels, then threshold.
    channels = diff.split()
    worst = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2])
    mask = worst.point(lambda v: 255 if v > tolerance else 0)
    differing = mask.histogram()[255]
    if diff_path:
        highlight = Image.new("RGB", a.size, (255, 0, 255))
        Image.composite(highlight, a.convert("L").convert("RGB"), mask).save(diff_path)
    return 100.0 * differing / (a.size[0] * a.size[1]), mask


def where(mask, cell=40, top=6):
    """The bounding box of differing pixels and the cells with most of them."""
    w, h = mask.size
    counts = []
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            box = (x, y, min(x + cell, w), min(y + cell, h))
            n = mask.crop(box).histogram()[255]
            if n:
                counts.append((n, box))
    counts.sort(reverse=True)
    cells = ", ".join(f"{n}px@{b[0]},{b[1]}" for n, b in counts[:top])
    return f"bbox {mask.getbbox()}; busiest {cell}px cells: {cells}"


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--max-percent", type=float, default=0.5)
    p.add_argument("--tolerance", type=int, default=16)
    p.add_argument("--diff", help="write an image highlighting differing pixels")
    args = p.parse_args()
    try:
        percent, mask = compare(args.a, args.b, args.tolerance, args.diff)
    except (OSError, ValueError) as e:
        print(f"imgdiff: {args.a} vs {args.b}: {e}")
        return 2
    ok = percent <= args.max_percent
    print(f"imgdiff: {args.a} vs {args.b}: {percent:.3f}% differ "
          f"(limit {args.max_percent}%) {'ok' if ok else 'FAIL'}")
    if not ok:
        print(f"imgdiff:   {where(mask)}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
