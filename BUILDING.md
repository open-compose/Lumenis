# Building

The product name is not final. Builds are called "Lumenis" until it is
decided. Every user-visible name comes from `cmake/Branding.cmake`.

## Requirements

| | macOS (Apple Silicon) | Windows x64 | Linux x64 |
|---|---|---|---|
| OS | macOS 12 or later | Windows 10 21H2 or later | Ubuntu 24.04, Fedora 40, or newer |
| Compiler | Xcode 15 or later | Visual Studio 2022 (MSVC 17.8 or later) | GCC 13 or later, or Clang 17 or later |
| CMake | 3.24 or later | 3.24 or later | 3.24 or later |
| Ninja | yes | yes | yes |
| Qt | 6.8 LTS | 6.8 LTS | 6.8 LTS, or your distribution's Qt 6.4 or later |
| Installer tools | (built in) | WiX Toolset 3.14 | dpkg-dev, rpm (optional), curl, file |

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
* the app at `build/macos-release/app/Lumenis.app`;
* the installer at `build/macos-release/lumenis-<version>-macos-arm64.dmg`.

The build is ad-hoc signed. That is enough to run it on the machine that built
it. To distribute it, sign and notarise with a Developer ID:

```sh
DEVELOPER_ID="Developer ID Application: … (TEAMID)" NOTARY_PROFILE=… \
  packaging/macos/sign-and-notarize.sh build/macos-release/app/Lumenis.app Lumenis.dmg
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
* the program at `build\release\bin\lumenis.exe`. Running it from the build
  tree needs `%QT_ROOT_DIR%\bin` on `PATH`. The installed copy bundles Qt.

### Linux x64

```sh
scripts/bootstrap-linux.sh         # apt or dnf: compiler, cmake, ninja, Qt 6
scripts/build.sh --package
```

Output, all in `build/release/`:
* `.deb` and `.tar.gz`, plus `.rpm` if `rpmbuild` is installed;
* a self-contained `Lumenis-x86_64.AppImage`.

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
    `LUMENIS_MODULE_PATH`.

The test presets set `QT_QPA_PLATFORM=offscreen`, so tests need no display.

## Continuous integration

`.github/workflows/ci.yml` builds, tests and packages on `ubuntu-24.04`,
`macos-14` (arm64) and `windows-2022`. It then installs each package,
launches the installed program with `--smoke-test`, and uninstalls it. The
installers are uploaded as workflow artifacts.

## Command-line options

```
lumenis --version          print the version and exit
lumenis --list-providers   list loaded modules and extension providers and exit
lumenis --smoke-test       open the main window, verify it, exit 0 on success
```
