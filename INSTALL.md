# Installing

Installers are produced by the build (see [BUILDING.md](BUILDING.md)) and by
CI, as artifacts on each workflow run.

## macOS (Apple Silicon, macOS 12 or later)

1. Open `lumenis-<version>-macos-arm64.dmg`.
2. Drag **Lumenis.app** to **Applications**.

An unsigned development build is blocked by Gatekeeper the first time it runs.
To open it, right-click the app and choose **Open**, or run:

```sh
xattr -dr com.apple.quarantine /Applications/Lumenis.app
```

To uninstall, drag the app to the Bin. Per-user data lives in
`~/Library/Application Support/Open Compose/lumenis`.

## Windows x64 (Windows 10 21H2 or later)

**MSI:** double-click `lumenis-<version>-windows-x64.msi`. It installs to
`C:\Program Files\Lumenis` and adds a Start-menu entry.

Unattended install:

```powershell
msiexec /i lumenis-<version>-windows-x64.msi /qn
```

To uninstall, use **Settings > Apps**, or run `msiexec /x <msi> /qn`.

**Portable ZIP:** unzip anywhere and run `bin\lumenis.exe`.

Per-user data lives in `%APPDATA%\Open Compose\lumenis`.

## Linux x64

* **AppImage** (any recent distribution):
  ```sh
  chmod +x Lumenis-x86_64.AppImage && ./Lumenis-x86_64.AppImage
  ```
* **Debian/Ubuntu:**
  ```sh
  sudo apt install ./lumenis_<version>_amd64.deb
  ```
  Remove it with `sudo apt remove lumenis`.
* **Fedora/RHEL:**
  ```sh
  sudo dnf install ./lumenis-<version>.x86_64.rpm
  ```
* **Tarball:** extract `lumenis-<version>-linux-x86_64.tar.gz`. It needs the
  system Qt 6 libraries.

Per-user data lives in `~/.local/share/Open Compose/lumenis`.

## Modules

Optional modules add codecs, plug-in bridges and other providers (see
[docs/EXTENSIONS.md](docs/EXTENSIONS.md)). Put module files in one of these
places:

| Platform | Application modules | Per-user modules |
|---|---|---|
| macOS | `Lumenis.app/Contents/PlugIns/modules` | `~/Library/Application Support/Open Compose/lumenis/modules` |
| Windows | `C:\Program Files\Lumenis\bin\modules` | `%APPDATA%\Open Compose\lumenis\modules` |
| Linux | `/usr/lib/lumenis/modules` | `~/.local/share/Open Compose/lumenis/modules` |

You can also list extra directories in `LUMENIS_MODULE_PATH`, separated by
`:` (or `;` on Windows). Run `lumenis --list-providers` to see what loaded,
and why anything failed.
