# Building

The product is called **Cutthulhu**. Every user-visible name comes from
`cmake/Branding.cmake`; the internal code name `lumenis` (CMake targets, C++
namespaces, module ABI symbols) is not user-visible and does not change.

## Requirements

| | macOS (Apple Silicon) | Windows x64 | Linux x64 |
|---|---|---|---|
| OS | macOS 12 or later | Windows 10 21H2 or later | Ubuntu 24.04, Fedora 40, or newer |
| Compiler | Xcode 15 or later | Visual Studio 2022 (MSVC 17.8 or later) | GCC 13 or later, or Clang 17 or later |
| CMake | 3.24 or later | 3.24 or later | 3.24 or later |
| Ninja | yes | yes | yes |
| Qt | 6.8 LTS | 6.8 LTS | 6.8 LTS, or your distribution's Qt 6.4 or later |
| Installer tools | (built in) | WiX Toolset 3.14 (installed by the bootstrap script) | dpkg-dev, rpm (optional), curl, file |

Qt 6.8 LTS is the version release builds ship with. Qt 6.4 is the oldest
version supported, because that is the system Qt on Ubuntu 24.04.

## Quick start

Each platform has a bootstrap script that installs the tools and a build
script that configures, builds, tests and (with `--package` / `-Package`)
makes installers.

### macOS (Apple Silicon)

```sh
xcode-select --install             # once, if Xcode is not installed
scripts/bootstrap-macos.sh         # Homebrew cmake/ninja + Qt into ~/Qt
export QT_ROOT_DIR="$HOME/Qt/6.8.3/macos"
scripts/build.sh --package
```

Output:
* the app at `build/macos-release/bin/Cutthulhu.app`. This copy uses Qt from
  `QT_ROOT_DIR`, so it only runs on the machine that built it.
* the installer `build/macos-release/cutthulhu-<version>-macos-arm64.dmg`, made
  only with `--package`. Its app bundles Qt and runs on any Apple Silicon Mac
  with macOS 12 or later.

The build is ad-hoc signed. That is enough to run it on the machine that built
it. To distribute it, sign and notarise with a Developer ID:

```sh
DEVELOPER_ID="Developer ID Application: … (TEAMID)" NOTARY_PROFILE=… \
  cmake --install build/macos-release --prefix build/macos-release/dist   # app with Qt bundled
  packaging/macos/sign-and-notarize.sh build/macos-release/dist/Cutthulhu.app Cutthulhu.dmg
```

### Windows x64

Run in PowerShell:

```powershell
scripts\bootstrap-windows.ps1      # CMake, Ninja, WiX, VS Build Tools, Qt into C:\Qt
# open a new PowerShell window so PATH changes apply
$env:QT_ROOT_DIR = 'C:\Qt\6.8.3\msvc2022_64'
scripts\build.ps1 -Package
```

`build.ps1` enters the Visual Studio x64 developer environment itself.

Output:
* the `.msi` and `.zip` installers in `build\release\`;
* the program at `build\release\bin\cutthulhu.exe`. Running it from the build
  tree needs `%QT_ROOT_DIR%\bin` on `PATH`. The installed copy bundles Qt.

### Linux x64

```sh
scripts/bootstrap-linux.sh         # apt or dnf: compiler, cmake, ninja, Qt 6
scripts/build.sh --package
```

Output, all in `build/release/`:
* `.deb` and `.tar.gz`, plus `.rpm` if `rpmbuild` is installed;
* a self-contained `Cutthulhu-x86_64.AppImage`.

The `.deb`/`.rpm` use the distribution's Qt. The AppImage bundles Qt and runs
on any recent distribution.

To use a Qt 6.8 you installed yourself instead of the system Qt:

```sh
scripts/build.sh --qt ~/Qt/6.8.3/gcc_64 --package
```

## Using CMake directly

```sh
cmake --preset release            # or: debug, macos-release, macos-debug
cmake --build --preset release
ctest --preset release
cd build/release && cpack
```

Point CMake at Qt with `QT_ROOT_DIR` (the presets pass it as
`CMAKE_PREFIX_PATH`), or pass `-DCMAKE_PREFIX_PATH=...` yourself.

Useful options:

| Option | Default | Meaning |
|---|---|---|
| `LUMENIS_BUILD_TESTS` | `ON` | Build the unit and app tests |
| `LUMENIS_WARNINGS_AS_ERRORS` | `OFF` (`ON` in CI) | Fail the build on any compiler warning |
| `PRODUCT_NAME`, `PRODUCT_EXE_NAME`, `PRODUCT_BUNDLE_ID`, ... | see `cmake/Branding.cmake` | Rebrand the build |

## Tests

`ctest` runs:

* unit tests (Qt Test): rational time, SMPTE timecode, the provider registry
  and the module loader;
* app tests:
  * `--version`;
  * `--smoke-test`, which opens the main window offscreen and checks it;
  * `--list-providers`, which loads a sample module through
    `CUTTHULHU_MODULE_PATH`.

The test presets set `QT_QPA_PLATFORM=offscreen`, so tests need no display.

## Continuous integration

`.github/workflows/ci.yml` builds, tests and packages on `ubuntu-24.04`
(and Ubuntu 26.04 in a container), `macos-14` and `macos-26` (arm64) and
`windows-2022`. It then installs each package, launches the installed
program with `--smoke-test`, and uninstalls it. The installers are uploaded
as workflow artifacts.

GitHub bills macOS minutes at ten times the Linux rate on a private
repository. The GitHub-hosted macOS jobs therefore run only for `main`, pull
requests and manual runs (`gh workflow run ci.yml --ref <branch>`). Pushes
to other branches build on macOS only through the self-hosted runner below.

### Self-hosted macOS runner

An Apple Silicon Mac can run the macOS job for free. The runner works
headless: it runs as its own hidden standard user, is started by launchd at
boot, and needs nobody to log in as that user. It never opens windows (the
tests and screenshots use Qt's offscreen platform) or takes the keyboard,
mouse or screen, so you keep using the Mac as usual. Builds run in the
background QoS class at low priority, mostly on the efficiency cores, so
your own work comes first.

1. In your administrator account, install the build tools for all users:
   Xcode (or `xcode-select --install`) and `brew install cmake ninja`.
2. On GitHub, open the repository's Settings > Actions > Runners > New
   self-hosted runner, and copy the token from the `./config.sh` line. It
   is valid for an hour.
3. From your checkout, run:

   ```sh
   sudo scripts/setup-mac-runner.sh install <token>
   ```

   This creates the hidden user `ghrunner`, downloads and registers the
   runner (labels `self-hosted`, `macOS`, `ARM64`, `cutthulhu`), and
   installs `/Library/LaunchDaemons/com.open-compose.actions-runner.plist`.
4. Check that the runner shows as Idle under Settings > Actions > Runners.
   Then add the repository variable `MAC_RUNNER` with the value `on`
   (Settings > Secrets and variables > Actions > Variables). The
   `macOS arm64 (self-hosted)` job then runs on every push.

Day to day:

* `sudo scripts/setup-mac-runner.sh status` shows whether it is running and
  the end of its log (`/Users/ghrunner/runner.log`).
* `sudo scripts/setup-mac-runner.sh stop` and `start` pause and resume it.
  While the Mac is off, asleep or stopped, set `MAC_RUNNER` to `off`;
  otherwise CI runs wait for the Mac (for up to a day).
* The Mac must be awake for jobs to run. The display can sleep; to keep the
  Mac itself awake on power, turn on System Settings > Battery (or Energy) >
  Options > Prevent automatic sleeping when the display is off, or run
  `sudo pmset -c sleep 0`.
* Fewer or more parallel compile jobs: `sudo RUNNER_JOBS=2
  scripts/setup-mac-runner.sh install <token>` (the default is 4).
* To remove it: `sudo scripts/setup-mac-runner.sh remove <token>` with a
  removal token from the runner's page on GitHub, then optionally `sudo
  sysadminctl -deleteUser ghrunner`.

The runner runs code from this repository's branches. Use it only while the
repository is private, or restrict it to trusted branches before making the
repository public: a pull request from a fork could otherwise run code on
the Mac.

## Application icon

The icon is a 3D scene, `packaging/icon/scene.frag`, rendered to
`packaging/icon/icon-1024.png`. The Windows (`.ico`), macOS (`.icns`), Linux,
window and About box images are made from that picture. All of them are
committed, so a build needs none of these tools. After changing the scene:

```sh
npm install -g playwright && npx playwright install chromium   # once
NODE_PATH=$(npm root -g) node scripts/render-icon.js           # the 1024 px master
python3 -m pip install pillow                                  # once
python3 scripts/make-icons.py                                  # every platform's icon
```

## Troubleshooting

### The self-hosted macOS job waits or fails

* *Queued for a long time:* the Mac is off or asleep, or the runner is
  stopped. Run `sudo scripts/setup-mac-runner.sh status`, or set the
  repository variable `MAC_RUNNER` to `off` until the Mac is back.
* *"mkdir: /Users/runner: Permission denied":* `actions/setup-python` (used
  by `install-qt-action`) only installs Python under `/Users/runner`, the
  GitHub-hosted runners' home. The self-hosted job therefore installs Qt
  itself, once, into `/Users/ghrunner/Qt` with aqtinstall in a virtual
  environment. Delete that folder to make it reinstall.
* *"Could not find the Qt platform plugin "offscreen"" in the installer
  gate:* the runner has no window server, so the installed app must start
  with Qt's offscreen plugin. The bundle gets it at install time
  (`app/CMakeLists.txt`); the gate checks that it is there.
* *"command not found: ninja" (or cmake):* install them from an
  administrator account with `brew install cmake ninja`. The runner's user
  cannot run `brew`.
* *`hdiutil attach` fails in the installer gate:* the runner has no login
  session. Restart it with `sudo scripts/setup-mac-runner.sh stop` and
  `start`; if it keeps failing, report it with the job log.

**macOS: `fatal error: 'cstdint' file not found`** (or any other standard
header). The Xcode or Command Line Tools installation is incomplete. This
often happens after a macOS upgrade. `scripts/build.sh` checks for it and
stops with the fix. To repair it:

* If Xcode is installed:
  `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
* If you only have the Command Line Tools:
  `sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install`.
* If neither works, reinstall Xcode.

Then delete `build/` and build again. CMake remembers the compiler it found
the first time, so the old build folder must go.

**macOS: `ld: framework 'AGL' not found`.** Xcode 26 removed the obsolete AGL
framework, but Qt 6.8.3 still asks to link it. The project removes that link
request itself (top-level `CMakeLists.txt`). If you still see this error, you
are building an older checkout: update it, delete `build/`, and rebuild.

**Windows: `Could not find the WiX candle executable`** (or the warning
"WiX Toolset 3.x not found"). The `.msi` needs WiX Toolset **3.x**. Run
`scripts\bootstrap-windows.ps1` again: it installs WiX 3.14 into
`%USERPROFILE%\.wix\wix314` and sets `WIX`. Then delete `build\release`,
open a new PowerShell window and build again. Without WiX, `-Package` still
produces the portable `.zip`.

**Linux (including WSL): `qt.qpa.plugin: Could not find the Qt platform
plugin "wayland"`, or `qt.qpa.wayland: No shell integration named
"xdg-shell" found` followed by `Could not load the Qt platform plugin
"wayland" ... even though it was found`.** Qt's Wayland support is missing
or incomplete. It comes in parts: the platform plugin and the shell
integrations (`xdg-shell`) that windows need. Ubuntu 26.04 installs the
platform plugin with the Qt development packages (`libqt6waylandclient6`)
but not the shell integrations (`libqt6wlshellintegration6`), which gives
the second message. Install the whole of it:

* Ubuntu/Debian: `sudo apt install qt6-wayland` (it pulls in both parts);
* Fedora: `sudo dnf install qt6-qtwayland`.

Until then Cutthulhu notices and says in one line what is missing. If X11
(XWayland) is there it runs through it, which works but responds more
slowly; WSLg normally provides it and sets `DISPLAY=:0`.

**The AppImage** carries its own Qt, so the system's packages do not matter
to it. AppImages made before this was fixed (October 2026) lack the
`xdg-shell` plugin and show the second message on every Wayland desktop,
WSLg included; Cutthulhu then says the package's own Qt is missing it. Make
the AppImage again (`scripts/build.sh --package`, or
`packaging/linux/make-appimage.sh build/<preset>`), or run the built
`build/<preset>/bin/cutthulhu` directly. CI starts every AppImage on a
headless Wayland compositor (`scripts/check-appimage-wayland.sh`).

If the messages stay after installing it, the parts are probably from
different updates: `apt` says "Not Upgrading: N" when it holds some back
(Ubuntu phases updates in), and a plugin from one build does not load into
the libraries of another. Cutthulhu then says "installed but does not load"
with Qt's reason. Bring them level and rebuild:

```
sudo apt update && sudo apt full-upgrade
apt list --upgradable 2>/dev/null | grep -i qt6   # should print nothing
```

To see what Qt sees:

```
echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY DISPLAY=$DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
dpkg -l qt6-wayland libqt6wlshellintegration6 libqt6waylandclient6 | tail -n 3
ls "$(qtpaths6 --query QT_INSTALL_PLUGINS)/wayland-shell-integration"
QT_DEBUG_PLUGINS=1 cutthulhu 2>&1 | grep -i -A3 "xdg-shell"
```

The last line shows why Qt could not load `libxdg-shell.so`, for example a
Qt built elsewhere (`QT_ROOT_DIR`) looking in its own plugin folder.
`scripts/bootstrap-linux.sh` installs it, configuring the build warns when
it is missing, and the `.deb` package recommends it.

**Linux, newer distributions (Ubuntu 26.04: GCC 15, Qt 6.10): warnings
such as `potential null pointer dereference` or `'invalidateFilter()' is
deprecated`.** Newer compilers and Qt versions warn about things older ones
accept, and the `ci` preset turns warnings into errors. Update your checkout:
these are fixed, and CI now builds on Ubuntu 26.04 too, so new ones are
caught there. If you meet one first, report it with the full message; the
`debug` and `release` presets build anyway.

**WSL: windows respond slowly.** WSLg draws Linux windows on Windows through
a remote desktop connection, with software rendering, so every repaint
costs more than on a Linux desktop. Install the Wayland plugin (above) to
avoid the extra XWayland step. To compare, start it with
`QT_QPA_PLATFORM=xcb cutthulhu` or `QT_QPA_PLATFORM=wayland cutthulhu`. For
real use and for judging speed, build natively on Windows
(`scripts\build.ps1`).

**Any platform: strange errors after changing compilers, Qt or Xcode.** Delete
the build folder (`build/<preset>`) and configure again.

## Command-line options

```
cutthulhu --version          print the version and exit
cutthulhu --list-providers   list loaded modules and extension providers and exit
cutthulhu --smoke-test       open the main window, verify it, exit 0 on success
```
