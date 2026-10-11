# Installing

Installers are produced by the build (see [BUILDING.md](BUILDING.md)) and by
CI, as artifacts on each workflow run.

## macOS (Apple Silicon, macOS 12 or later)

1. Open `cutthulhu-<version>-macos-arm64.dmg`.
2. Drag **Cutthulhu.app** to **Applications**.

An unsigned development build is blocked by Gatekeeper the first time it runs.
To open it, right-click the app and choose **Open**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/Cutthulhu.app
```

To uninstall, drag the app to the Bin. Per-user data lives in
`~/Library/Application Support/Open Compose/cutthulhu`.

## Windows x64 (Windows 10 21H2 or later)

**MSI:** double-click `cutthulhu-<version>-windows-x64.msi`. It installs to
`C:\Program Files\Cutthulhu` and adds a Start-menu entry.

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
