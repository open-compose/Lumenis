#!/usr/bin/env python3
"""Make every platform's application icon from the master image.

The master, packaging/icon/icon-1024.png, is rendered from
packaging/icon/scene.frag by scripts/render-icon.js. This script writes, and
these files are committed so that builds need neither tool:
  packaging/windows/app.ico          Windows executable and installer
  packaging/macos/app.icns           macOS application bundle
  packaging/linux/icon-256.png       Linux desktop icon (hicolor 256x256)
  packaging/linux/icon-512.png       Linux desktop icon (hicolor 512x512)
  src/ui/resources/icons/app.png     window icon (256 px)
  src/ui/resources/icons/about.png   About box picture (512 px)

Needs Pillow (a development tool only, not shipped):
  python3 -m pip install pillow
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("make-icons: Pillow is missing; run: python3 -m pip install pillow")

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "packaging" / "icon" / "icon-1024.png"


def main() -> int:
    if not MASTER.exists():
        sys.exit(f"make-icons: {MASTER.relative_to(ROOT)} is missing; run: node scripts/render-icon.js")
    master = Image.open(MASTER).convert("RGBA")

    def scaled(size: int) -> Image.Image:
        return master.resize((size, size), Image.LANCZOS)

    written = []

    ico = ROOT / "packaging" / "windows" / "app.ico"
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = {s: scaled(s) for s in sizes}
    frames[256].save(ico, format="ICO", sizes=[(s, s) for s in sizes],
                     append_images=[frames[s] for s in sizes if s != 256])
    written.append(ico)

    icns = ROOT / "packaging" / "macos" / "app.icns"
    master.save(icns, format="ICNS")
    written.append(icns)

    for size, path in [(256, ROOT / "packaging" / "linux" / "icon-256.png"),
                       (512, ROOT / "packaging" / "linux" / "icon-512.png"),
                       (256, ROOT / "src" / "ui" / "resources" / "icons" / "app.png"),
                       (512, ROOT / "src" / "ui" / "resources" / "icons" / "about.png")]:
        path.parent.mkdir(parents=True, exist_ok=True)
        scaled(size).save(path, format="PNG", optimize=True)
        written.append(path)

    for path in written:
        print(f"make-icons: wrote {path.relative_to(ROOT)} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
