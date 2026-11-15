# Installing

Releases are on the project's GitHub Releases page: a `.dmg` for macOS, an
`.msi` and a portable `.zip` for Windows, and an AppImage, a `.deb` and a
`.tar.gz` for Linux, with `SHA256SUMS.txt` (see "Checking a download" below).
Installers are also produced by the build (see [BUILDING.md](BUILDING.md)) and
by CI.

The packages are **not signed** (Cutthulhu is free, and signing certificates
cost money), so each operating system asks once before it runs them. The
steps for each are below. Cutthulhu tells you when a newer release is out
(Help > Check for Updates, and at start-up unless the Interface setting's
"Check for Updates at Startup" is off); it never downloads or installs one
itself.

## macOS (Apple Silicon, macOS 12 or later)

1. Open `cutthulhu-<version>-macos-arm64.dmg`.
2. Drag **Cutthulhu.app** to **Applications**.

The package is not notarized by Apple (it is ad-hoc signed only), so
Gatekeeper blocks it the first time it runs:
* **macOS 15 and later:** open the app once, click **Done**, then go to
  **System Settings > Privacy & Security** and click **Open Anyway**.
* **macOS 12 to 14:** Control-click the app and choose **Open**.
* **Either way,** you can instead run:

```sh
xattr -dr com.apple.quarantine /Applications/Cutthulhu.app
```

To uninstall, drag the app to the Bin. Per-user data lives in
`~/Library/Application Support/Open Compose/cutthulhu`.

## Windows x64 (Windows 10 21H2 or later)

**MSI:** double-click `cutthulhu-<version>-windows-x64.msi`. It installs to
`C:\Program Files\Cutthulhu` and adds a Start-menu entry. The package is not
signed, so Windows SmartScreen may say "Windows protected your PC": click
**More info**, then **Run anyway**.

Unattended install:

```powershell
msiexec /i cutthulhu-<version>-windows-x64.msi /qn
```

To uninstall, use **Settings > Apps**, or run `msiexec /x <msi> /qn`.

**Portable ZIP:** unzip anywhere and run `bin\cutthulhu.exe`.

Per-user data lives in `%APPDATA%\Open Compose\cutthulhu`.

## Linux x64

* **AppImage** (any recent distribution):
  ```sh
  chmod +x Cutthulhu-x86_64.AppImage && ./Cutthulhu-x86_64.AppImage
  ```
* **Debian/Ubuntu:**
  ```sh
  sudo apt install ./cutthulhu_<version>_amd64.deb
  ```
  Remove it with `sudo apt remove cutthulhu`.
* **Fedora/RHEL:**
  ```sh
  sudo dnf install ./cutthulhu-<version>.x86_64.rpm
  ```
* **Tarball:** extract `cutthulhu-<version>-linux-x86_64.tar.gz`. It needs the
  system Qt 6 libraries.

Per-user data lives in `~/.local/share/Open Compose/cutthulhu`.

## Checking a download

`SHA256SUMS.txt` on the release page holds each file's SHA-256 checksum. In
the folder with the download:

* **macOS:** `shasum -a 256 -c SHA256SUMS.txt --ignore-missing`
* **Linux:** `sha256sum -c SHA256SUMS.txt --ignore-missing`
* **Windows (PowerShell):** `Get-FileHash .\cutthulhu-<version>-windows-x64.msi`,
  and compare the hash with the file's line in `SHA256SUMS.txt`.

## Codecs in the releases

The releases follow US patent rules by default (docs/spec/release-codecs.md):

* **DNxHD and DNxHR**, managed media's codecs, are in every release.
* **H.264, HEVC, ProRes and AAC** go through the operating system: its hardware
  decoders (VideoToolbox on macOS, Direct3D 11 on Windows, VA-API on Linux)
  and its encoders (VideoToolbox and AudioToolbox on macOS, Media Foundation on
  Windows). A computer without a hardware decoder for a stream shows the clip,
  but cannot play it, and says why. Linux has no H.264 or HEVC export in the
  releases; an MP4's sound is Opus where the system has no AAC encoder.
* **To use FFmpeg's own implementations** where the law allows it, build
  Cutthulhu yourself with `-DLUMENIS_PATENT_CODECS=ON` (the default for
  development builds; [BUILDING.md](BUILDING.md)) and an FFmpeg that has those
  codecs, such as the one your distribution or Homebrew ships.

## Modules

Optional modules add codecs, plug-in bridges and other providers (see
[docs/EXTENSIONS.md](docs/EXTENSIONS.md)). Put module files in one of these
places:

| Platform | Application modules | Per-user modules |
|---|---|---|
| macOS | `Cutthulhu.app/Contents/PlugIns/modules` | `~/Library/Application Support/Open Compose/cutthulhu/modules` |
| Windows | `C:\Program Files\Cutthulhu\bin\modules` | `%APPDATA%\Open Compose\cutthulhu\modules` |
| Linux | `/usr/lib/cutthulhu/modules` | `~/.local/share/Open Compose/cutthulhu/modules` |

You can also list extra directories in `CUTTHULHU_MODULE_PATH`, separated by
`:` (or `;` on Windows). Run `cutthulhu --list-providers` to see what loaded,
and why anything failed.
