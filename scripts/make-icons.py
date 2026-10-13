#!/usr/bin/env python3
"""Render the application icon (packaging/icon.svg) for every platform.

Writes, and these files are committed so that builds do not need an SVG
renderer:
  packaging/windows/app.ico       Windows executable and installer
  packaging/macos/app.icns        macOS application bundle
  src/ui/resources/icons/app.png  window icon (256 px)

Linux installs the SVG itself.

Needs CairoSVG and Pillow (development tools only, not shipped):
  python3 -m pip install cairosvg pillow
"""

import io
import sys
from pathlib import Path

try:
    import cairosvg
    from PIL import Image
except ImportError as e:
    sys.exit(f"make-icons: {e.name} is missing; run: python3 -m pip install cairosvg pillow")

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "packaging" / "icon.svg"


def render(size: int) -> Image.Image:
    png = cairosvg.svg2png(url=str(SVG), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main() -> int:
    master = render(1024)

    ico = ROOT / "packaging" / "windows" / "app.ico"
    sizes = [16, 24, 32, 48, 64, 128, 256]
    # Each size rendered from the vector, not scaled down from one bitmap.
    frames = {s: render(s) for s in sizes}
    frames[256].save(ico, format="ICO", sizes=[(s, s) for s in sizes],
                     append_images=[frames[s] for s in sizes if s != 256])

    icns = ROOT / "packaging" / "macos" / "app.icns"
    master.save(icns, format="ICNS")

    png = ROOT / "src" / "ui" / "resources" / "icons" / "app.png"
    png.parent.mkdir(parents=True, exist_ok=True)
    render(256).save(png, format="PNG", optimize=True)

    for path in (ico, icns, png):
        print(f"make-icons: wrote {path.relative_to(ROOT)} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
