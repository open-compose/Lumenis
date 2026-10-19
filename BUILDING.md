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
| FFmpeg 6.0 or later | Homebrew `ffmpeg` | LGPL shared build from `scripts\get-ffmpeg.ps1` (set `FFMPEG_ROOT`) | `libavcodec-dev` and the other FFmpeg development packages |

Qt 6.8 LTS is the version release builds ship with. Qt 6.4 is the oldest
version supported, because that is the system Qt on Ubuntu 24.04.

FFmpeg reads and writes media. The bootstrap scripts install it.
* **Development builds** use the platform's FFmpeg. They include the codecs
  that wait for legal review (`LUMENIS_PATENT_CODECS`, on by default; see
  docs/EXTENSIONS.md).
* **Official releases** turn that option off.
* **Another FFmpeg:** set `FFMPEG_ROOT` to a folder that holds `include/`
  and `lib/` (and `bin/` on Windows).
* **Checking what was found:** `cutthulhu --list-providers` prints the
  FFmpeg version, and `cutthulhu --probe <file>` shows what a media file
  holds.

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
    `CUTTHULHU_MODULE_PATH` and lists FFmpeg as a provider.
  * `checklist_current`, when Python 3 is found: the Phase 1 gate 5
    checklist (`docs/checklists/phase1-gate5.md`) still matches the menu
    bar.

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

When the account's included Actions minutes run out, GitHub refuses the
hosted jobs (they fail within seconds without running a step). Set the
repository variable `HOSTED_CI` to `off` (Settings > Secrets and variables >
Actions > Variables) until the allowance resets: the GitHub-hosted jobs are
then skipped and the self-hosted Mac still builds and tests every push.
Delete the variable, or set it to `on`, to bring them back.

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

### Self-hosted Windows and Linux (WSL) runners

A Windows x64 PC can run the Windows job, and Ubuntu distributions in its
WSL the Linux jobs, for free.

* **Windows:** the runner is a Windows service under its own hidden
  standard user `ghrunner`, started at boot; nobody logs in as it and it
  never uses the screen, keyboard or mouse. Builds use 4 parallel compile
  jobs so the PC stays responsive. The user is not an administrator and
  cannot use the Windows Installer service, so the package gate launches
  the app from the .zip; the GitHub-hosted job installs the .msi.
* **Linux in WSL:** one runner per Ubuntu distribution, labelled
  `ubuntu-<version>`, running as a systemd service under its own user at
  low priority. WSL only runs while you are logged on to Windows, so the
  Linux jobs run then; a logon task keeps the distribution running.

Windows, from an elevated PowerShell (Run as administrator) in your
checkout:

1. On GitHub, Settings > Actions > Runners > New self-hosted runner: copy
   the token from the `config` line (valid for an hour; one token can
   register several runners).
2. Run `scripts\setup-windows-runner.ps1 install <token>`. It first runs
   `bootstrap-windows.ps1` (Visual Studio Build Tools with C++, CMake,
   Ninja, Python, Qt in `C:\Qt`, WiX), which takes a while the first time;
   then it creates the user, downloads and registers the runner in
   `C:\actions-runner` and starts the service.
3. Check the runner shows as Idle, then add the repository variable
   `WINDOWS_RUNNER` = `on`.

Linux, for each Ubuntu version you want (for example 24.04, the supported
system-Qt platform, and 26.04, the newest):

1. In Windows: `wsl --install -d Ubuntu-24.04` (or `Ubuntu-26.04`) if you
   do not have it, and open it. systemd must be on (the default on current
   Ubuntu; otherwise add `[boot]` `systemd=true` to `/etc/wsl.conf` and run
   `wsl --shutdown`).
2. In the distribution, in a clone of the repository:
   `sudo scripts/setup-wsl-runner.sh install <token>`. It installs the
   build dependencies and weston, creates the user and starts the service.
3. In the elevated PowerShell: `scripts\setup-windows-runner.ps1 keep-wsl
   Ubuntu-24.04` (the name `wsl --list` shows). This starts the
   distribution when you log on and keeps it running.
4. Set the repository variable `LINUX_RUNNERS` to the versions that have a
   runner, as a JSON list: `["24.04"]`, or `["24.04","26.04"]`.

While `HOSTED_CI` is `off`, the screenshot comparison (`ui-consistency`)
runs on the WSL runner too, with its screenshots as the reference.

Day to day: `status`, `stop` and `start` work on both scripts. When the PC
will be off, set `WINDOWS_RUNNER` to `off` and empty `LINUX_RUNNERS`, or CI
waits for it (for up to a day). `remove <token>` unregisters a runner (a
removal token from its page on GitHub); `unkeep-wsl <distribution>` removes
the logon task.

The runners run code from this repository's branches. Use them only while the
repository is private, or restrict it to trusted branches before making the
repository public: a pull request from a fork could otherwise run code on
the Mac.

### Trying the builds from the self-hosted runners

A runner deletes its work folder after each job, and its user cannot write in
your own folders. Run the setup script's `builds` command once on each
machine, from your own account. After that, each successful CI run keeps its
package in a builds folder, named with the date and commit. Only the newest
five are kept.

| Machine | Command | Package | Where |
|---|---|---|---|
| Mac | `sudo scripts/setup-mac-runner.sh builds` | `.dmg` | `/Users/Shared/Cutthulhu Builds`, linked as `Documents/Cutthulhu Builds` |
| Windows (elevated PowerShell) | `scripts\setup-windows-runner.ps1 builds` | `.msi` | `Documents\Cutthulhu Builds` |
| WSL Ubuntu | `sudo scripts/setup-wsl-runner.sh builds` | AppImage | `/srv/cutthulhu-builds`, linked as `~/Documents/Cutthulhu Builds` |

From Windows, the AppImages are also under
`\\wsl$\<distribution>\srv\cutthulhu-builds`. In WSL, run one with
`./Cutthulhu-*.AppImage`; WSLg shows its window.

The command restarts the runner so that it reads the new setting. A job that
is running at that moment is cut short.

Notes:

* The self-hosted Windows job builds the `.msi` without WiX's validation
  (`-sval`), and cannot install it. Installing it yourself is its first real
  test, so report any installer error.
* The `.dmg` is not notarized. A copy made on the Mac itself is not marked
  as downloaded, so it should open directly. If macOS blocks it, open it from
  Finder with Control-click > Open.

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

### "Artifact storage quota has been hit"

GitHub keeps a private repository's CI artifacts in a small storage quota.
When it is full, uploads fail with this message. The quota is for the
billing period, so deleting artifacts does not free it at once.

The jobs still build, test, package and keep their package (uploads are
`continue-on-error`). The screenshots do not use artifacts at all (next
entry), so the screenshot comparison still runs.

### CI screenshots: where they are and how to look at the differences

"UI looks the same on all platforms" compares each platform's screenshots
with Linux's. Each job stores its screenshots in the repository, not in
artifact storage:
* `scripts/ci-screenshots.py` does this on Linux and macOS, and
  `scripts/ci-screenshots.ps1` on Windows (GitHub's Git Data API, so the PC
  needs neither git nor Python).
* Each job's screenshots go, as one archive, on the branch
  `ci-screenshots/<branch>/<platform>`, replaced on every run.
* The commit message names the run, so the comparison never uses an older
  run's screenshots.

The differences found are kept on `ci-screenshots/<branch>/diffs`. To look
at them:

```
git fetch origin ci-screenshots/<branch>/diffs
mkdir diffs && git show FETCH_HEAD:shots.tar.gz | tar xz -C diffs
```

The same works for a platform's screenshots. On Windows, use `shots.zip`
instead, and unpack it with Expand-Archive.

The `ci-screenshots/*` branches hold nothing else, and you can delete them
at any time. If the comparison says "the jobs could not store them", the
job's "Pass the screenshots to ui-consistency" step failed. Check that the
workflow may write to the repository: Settings > Actions > General >
Workflow permissions must not block it.

### macOS screenshots differ: larger check boxes, sliders or dials

Qt's Fusion style scales its built-in sizes by the screen's DPI. The
baseline is 96 DPI, or 72 on macOS, so with the offscreen platform macOS drew
check boxes, radio buttons, slider handles and dials a third larger. This
made "UI looks the same on all platforms" fail for the Effect Editor, the
Audio Mixer, the Command Palette and Timeline Settings.

The application's style (`src/ui/theme.cpp`, `fixedPixelMetric`, and
`subControlRect` for the slider groove) now gives these sizes the same
values everywhere. The tab padding of Qt's common style is fixed in the
same place. `tst_theme` checks them on every
platform. If a new kind of control shows the same symptom, add its pixel
metric there.

### Sending the gate 5 checklist to an editor

The editor gets two PDFs. To make them:

```
python3 scripts/make-editor-pdfs.py --out out-editor
```

This needs reportlab. It writes:
* `Cutthulhu-Mac-setup.pdf`, from `docs/checklists/editor-setup-macos.md`:
  install, the macOS security step, a test user, the session.
* `Cutthulhu-checklist.pdf`, from `docs/checklists/phase1-gate5.md`: a
  fillable form that Preview can tick, type into and save.

Remake them after the checklist changes.

### "FFmpeg 6.0 or later was not found"

CMake needs FFmpeg's development files.

* **Linux:**
  `sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev`.
  Fedora: `sudo dnf install ffmpeg-free-devel`.
* **macOS:** `brew install ffmpeg pkg-config`.
* **Windows:** run `scripts\get-ffmpeg.ps1`. It prints a folder; set
  `FFMPEG_ROOT` to it before configuring.

On the self-hosted runners an administrator installs it once. The CI jobs
stop with this command in their error:
* **Mac:** `brew install ffmpeg pkg-config`.
* **WSL:** `sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev`.
* **Windows PC:** nothing is needed. The job downloads the build itself
  into the runner's profile.

### A test fails in CI but its report is missing

ctest's capture of a test's output can come back without QTest's FAIL
lines (seen on Windows). Every QTest test also writes its report to
`<build>/test-logs/<name>.txt`, and the CI step "Logs of failed tests"
prints the reports that contain a failure. Locally, look in
`build/ci/test-logs/`.

Tests that move or delete media files must let go of them first
(`MediaFrames::clear()`): Windows does not move or delete open files.

### "no ffmpeg command found"

`scripts/measure-playback.py` and the `decode_corpus` test need the
`ffmpeg` program, not only FFmpeg's libraries. Up to now
`scripts/bootstrap-linux.sh` installed only the libraries. Install it with
`sudo apt install ffmpeg` (Fedora: `sudo dnf install ffmpeg-free`), or name
it with `--ffmpeg <path>`. Run the scripts as yourself, not with `sudo`:
sudo resets PATH, root usually can't reach your sound server (PipeWire or
PulseAudio), so the run reports no sound, and the test file goes into
root's cache folder. CMake now warns when it can't find `ffmpeg` and leaves
`decode_corpus` out. The self-hosted Linux CI job warns as well. In an
existing WSL runner, run `sudo apt-get install -y ffmpeg`.

On Windows, `scripts/get-ffmpeg.ps1` unpacks FFmpeg into
`%LOCALAPPDATA%\Cutthulhu\ffmpeg\<build>\bin` and prints `FFMPEG_ROOT`,
but it does not change PATH. A PATH changed with `setx` or in System
Properties only reaches terminals opened after the change. A PATH entry
that names the build folder instead of its `bin` folder doesn't help
either. The scripts now look, in order, at `--ffmpeg`, PATH,
`FFMPEG_ROOT\bin` and the newest build in that folder (`scripts/find_ffmpeg.py`).
When they find nothing, they list each place they looked.

### "measure-playback: the application failed"

The script now prints Cutthulhu's exit code and still checks the result if
one was written. Exit 2 means the application failed. On Windows a code
such as `0xC0000005 (access violation)` means it crashed.

The crash on a Windows desktop came from miniaudio before 0.11.25. When
it opened the sound device it called `CoInitializeEx` and then always
`CoUninitialize`, even when its init had failed. Its init does fail on
Qt's main thread, which is already in a single-threaded apartment. So
the uninit took Qt's own COM reference away, and closing the application
crashed in `qwindows.dll` (`QWinRTFactoryCacheRegistration::clearAllCaches`).
miniaudio 0.11.25 calls `CoUninitialize` only after a successful init.
`tst_media` (`soundLeavesComAlone`) checks that COM is still initialised
after opening and closing the sound device. Three smaller problems are
fixed too:
* The result was written without flushing, so a crash while closing down
  lost it.
* The measurement's callbacks used variables that were gone by the time
  the window closed down. The connection is now cut as soon as playback
  ends.
* Tools > Console's message handler stayed installed after its log, a
  static, was destroyed. A warning from Qt after that could crash. The handler
  is now removed with the log.

If Cutthulhu still crashes on Windows, it prints a crash report: the
exception and the call stack, with function names and lines from
`cutthulhu.pdb`, which is installed next to the executable. The script
shows Cutthulhu's messages and the report when it fails. Send the report
with the problem.

The self-hosted Windows job runs a 20-second measurement and fails on
exit 2. It needs Python on the runner. The runner service runs as the
hidden user `ghrunner`, so Python installed for your own account doesn't
reach it. From an administrator PowerShell, run:

```
winget install --id Python.Python.3.13 --scope machine --log $env:TEMP\python-install.log
Restart-Service (Get-Service 'actions.runner.*').Name
```

Then check with `py -0p`. Exit code 1603 is the Windows Installer's
general failure; the reason is in the log. The usual causes are:
* the terminal is not elevated;
* the same Python version is already installed for your own account
  (hence 3.13 above, if you have 3.12);
* a reboot is pending.

A machine-wide install brings back `decode_corpus` on Windows as well:
CMake finds Python through the registry.

### `transcodeRoundTrips`: one platform's PSNR is far below the others

First find out whether the encoder or the decoder differs. Decode the same
file on both machines (`--framemd5`, or an MD5 of the decoded planes) and
encode the same frames on both. On the Mac, DNx 444 from the Y'CbCr chart
came back at 42 dB, against 52.6 dB on Linux. The files were the same byte
for byte, but FFmpeg's arm64 build decodes DNxHR 444 with a less exact
IDCT: x86 gives the same pictures with `-idct int`, and on arm64 no option
changes it. So on arm64 the test accepts 40 dB for DNx 444, and 49 dB
elsewhere. The RGB chart is checked first: a low value there points at
the codec, a low value only from Y'CbCr at a conversion or the decoder.
The conversions the PSNR depends on (Y'CbCr to 4:4:4, and between Y'CbCr
and RGB) are our own code (`planarYuvTo444` and the matrix in `media.cpp`),
so swscale's code for each version and processor doesn't change them.

An arm64 FFmpeg can be tried on Linux: unpack an arm64 Ubuntu with
`debootstrap --foreign --arch=arm64`, extract its python3 debs, unzip the
aarch64 PyAV and numpy wheels, and run
`qemu-aarch64 -L <root> <root>/usr/bin/python3.12`.

### macOS: "implicit conversion from 'std::int64_t' to 'long double' may lose precision"

On Apple arm64 `long double` is a plain 64-bit `double`, so Apple Clang's
`-Wimplicit-int-float-conversion` (part of `-Wconversion`, an error in the
`ci` preset) flags 64-bit integers turned into `long double`. Clang on x86
Linux has an 80-bit `long double` and says nothing. Use integer arithmetic
for frame and sample counts, or explicit casts. Before pushing, run
`scripts/check-apple-conversions.py build/clang`: it compiles every file of
the Clang build as the Mac would see `long double`.

### Packaging on Windows: "Invalid character escape '\U'"

CPack stops in `app/cmake_install.cmake` with this error, naming an FFmpeg
DLL under `C:\Users\...`. CMake took `FFMPEG_ROOT` with backslashes and
wrote it into the install script. `cmake/FindFFmpeg.cmake` converts the
path to forward slashes; configure again from a fresh build folder if an
old cache still holds the backslash path. The CI packaging step runs on
every Windows build, so the error can't come back silently.

### `checklist_current` fails

The menus or commands changed, but the gate 5 checklist was not remade.
Rebuild, then run:

```
python3 scripts/make-checklist.py --app build/ci/bin/cutthulhu
```

Commit the updated `docs/checklists/phase1-gate5.md`. Do not edit it by
hand.

### The self-hosted Windows or Linux job waits or fails

* *Queued for a long time:* the PC is off or asleep, you are logged off
  (WSL only runs while you are logged on), or a runner is stopped. Check
  `scripts\setup-windows-runner.ps1 status` and, in the distribution,
  `sudo scripts/setup-wsl-runner.sh status`; or set `WINDOWS_RUNNER` to
  `off` / take the version out of `LINUX_RUNNERS` until it is back.
* *"pwsh: command not found":* the self-hosted Windows job uses Windows
  PowerShell 5.1 (`shell: powershell`), which every Windows has; PowerShell
  7 (`pwsh`) is only on GitHub's runners. Write to `$env:GITHUB_ENV` with
  `Out-File -Encoding utf8 -Append` there, not `>>` (UTF-16 in 5.1).
* *"Problem running WiX" in Package:* WiX checks the .msi through the
  Windows Installer service, which the runner's standard user cannot use
  (light.exe error LGHT0217). The self-hosted job configures with
  `-DLUMENIS_WIX_SKIP_VALIDATION=ON` (light `-sval`); the GitHub-hosted job
  still validates. The step prints WiX's log when it fails.
* *msiexec "Failed to connect to server. Error: 0x80070005":* the same
  limit: the runner's user may not use the Windows Installer service, not
  even for `msiexec /a`. The self-hosted job checks the .zip package (the
  same files) instead; the hosted job installs the .msi.
* *"Qt 6.8.3 is not in C:\Qt":* run `scripts\setup-windows-runner.ps1
  install <token>` again; it installs Qt and keeps the existing runner.
* *"Pillow is missing on this runner":* the screenshot comparison runs on
  the WSL runner while `HOSTED_CI` is `off` and needs Pillow: in the
  distribution, `sudo apt-get install -y python3-pil` (newer setup scripts
  install it).
* *"systemd is not running" from setup-wsl-runner.sh:* add `[boot]` and
  `systemd=true` to `/etc/wsl.conf`, run `wsl --shutdown` in Windows, and
  open the distribution again.

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

## Phase 2 gates

* **Gate 1, the decode corpus:** `ctest --preset ci -R decode_corpus`, or
  `python3 scripts/decode-corpus.py --app build/ci/bin/cutthulhu`. It needs
  the `ffmpeg` command (Windows: `$env:FFMPEG_ROOT\bin\ffmpeg.exe`, from
  `scripts/get-ffmpeg.ps1`). The first run makes the files into the user's
  cache folder (`CUTTHULHU_DECODE_CORPUS` to choose another) and takes several
  minutes; later runs about two. Codecs the local FFmpeg cannot encode (the
  Windows LGPL build has no x264 or x265) are left out; at least 100 files
  must be made. `report.json` in the folder lists each file.
* **Gate 2, MXF:** `python3 scripts/check-mxf.py --make --app <cutthulhu>`
  makes a 1080p25 test picture with a stereo tone, transcodes it into every
  DNx resolution (`cutthulhu --write-media`, as Import and Transcode write
  it), and checks each file with bmx's `mxf2raw`. A file passes when it is
  complete and readable to its last frame (every frame is read for a
  checksum), is OP-Atom with one track, and has real creation dates.
  `check-mxf.py "<drive>/Avid MediaFiles/MXF"` checks media you made in the
  application. With `mxf2raw` on PATH, ctest runs it as `mxf_check`. bmx
  (BSD licence) is a tool here, not shipped. To build it on Ubuntu:

  ```
  sudo apt install libexpat1-dev liburiparser-dev uuid-dev
  git clone --depth 1 https://github.com/bbc/bmx.git
  cmake -S bmx -B bmx/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBMX_BUILD_TESTING=OFF
  cmake --build bmx/build
  sudo cp bmx/build/apps/mxf2raw/mxf2raw /usr/local/bin/
  ```

  Then link the files into Media Composer by hand.
* **Gates 3 and 4, playback:** on a reference machine with a sound card,
  `python3 scripts/measure-playback.py --app <cutthulhu> --seconds 600` (gate
  3) and `--seconds 3600` (gate 4). Each run is judged on the gates its
  length covers. Gate 3 is no dropped frame in the first 10 minutes; drops
  later are reported with their times but don't fail it. Gate 4 is lip
  sync over 60 minutes. A shorter run is a trial of the same limits. The
  result lists when frames dropped (`dropTimes`, seconds into the run) and
  how many picture changes were over half a frame (`lipSyncOver`, with the
  99.9th percentile). Stop heavy background work, such as containers and
  backups, for gate runs. On Linux the reference is Ubuntu 26.04 in WSL.
  WSLg sends the sound to Windows over RDP, which adds some jitter of its
  own. The file is made once (about 9 GB for 10
  minutes) into the cache folder.
* **Gate 5, transcode round trips:** `tst_media` (`transcodeRoundTrips`)
  prints each resolution's PSNR.

## Phase 3 gates

* **Gate 1, bins and projects:** dump what Cutthulhu reads with
  `cutthulhu --dump-structure <bin or project>` and compare it with a
  reference dump: `python3 scripts/structdiff.py reference.json ours.json`.
  It lists each difference in clip counts, names, timecodes, custom columns
  and sequence structure (docs/spec/structure.md). ctest `structdiff` checks
  the tool itself.
* **The interchange corpus:** AAF files and bins from pyaaf2's and pyavb's
  test suites (MIT), made with Media Composer. ctest `interop_corpus_fetch`
  downloads them at pinned commits, checked by SHA-256, into
  `build/<preset>/interop-corpus`; set `CUTTHULHU_INTEROP_CORPUS` (or the
  CMake variable `LUMENIS_INTEROP_CORPUS`) to keep them elsewhere. By hand:
  `python3 scripts/interop-corpus.py fetch`. Without a network the download
  and the tests that read the files are skipped.
* **Compound files (AAF's container):** `tst_cfb` reads every corpus AAF the
  way olefile does (`tests/data/interop_corpus.json`). ctest `cfb_olefile`
  reads what Cutthulhu writes with olefile and needs it installed
  (`pip install olefile`, or the distribution's `python3-olefile`); without
  it, it is skipped. After changing the corpus, rewrite the manifest with
  `python3 scripts/interop-corpus.py manifest` (needs olefile and pyaaf2).
* **AAF objects:** `tst_aaf` reads every corpus AAF as pyaaf2 does, compared
  by the digest of a dump. ctest `aaf_pyaaf2` compares the dumps themselves
  and lists any difference; it needs pyaaf2 (`pip install pyaaf2`) and is
  skipped without it. By hand: `python3 scripts/aaf-dump.py --compare --app
  build/ci/bin/cutthulhu FILE.aaf`.
* **AAF import against Media Composer's bins (gate 1 on the corpus):**
  ctest `aaf_import_bins` imports each corpus AAF that has a bin of the same
  material and compares it with the bin's dump in `tests/data/aaf_reference`
  (`structdiff.py --no-effects`). Rewrite those dumps with
  `python3 scripts/interop-corpus.py reference` (needs pyavb: `pip install
  pyavb`). By hand: `cutthulhu --import-aaf FILE.aaf > ours.json` and
  `python3 scripts/avb-structure.py BIN.avb > ref.json`, then
  `structdiff.py ref.json ours.json --no-effects`.
* **The built-in AAF model** (`src/interop/aaf_model.inc`) is made from
  pyaaf2's model tables: `python3 scripts/make-aaf-model.py --pyaaf2
  <pyaaf2 checkout>` (`--check` to compare).

## Command-line options

```
cutthulhu --version          print the version and exit
cutthulhu --list-providers   list loaded modules and extension providers and exit
cutthulhu --smoke-test       open the main window, verify it, exit 0 on success
cutthulhu --probe FILE       print what a media file holds (streams, codecs, timecode) as JSON
cutthulhu --framemd5 FILE    print each decoded frame's MD5 (as FFmpeg's framemd5); with --shuffle,
                             read them in a shuffled order, seeking
cutthulhu --measure-playback FILE [--seconds N]
                             play FILE at 1x with sound and print frames shown and dropped and
                             the picture's offset from the sound as JSON
cutthulhu --dump-structure PATH
                             print a bin (.cthbin) or project (.cthproj or its folder) as a
                             structure dump, for scripts/structdiff.py (docs/spec/structure.md)
cutthulhu --dump-aaf FILE [--canonical]
                             print every object of an AAF file below its Header as JSON, for
                             scripts/aaf-dump.py --compare (docs/spec/aaf.md)
cutthulhu --import-aaf FILE   import an AAF file into a bin and print the bin as a structure dump
                             (docs/spec/aaf.md, "Import")
cutthulhu --write-media FILE --drive FOLDER [--resolution ID|all]
                             transcode FILE into managed MXF under FOLDER/Avid MediaFiles/MXF
                             and print the files made as JSON (for scripts/check-mxf.py)
cutthulhu --dump-menus FILE  write the menu bar as JSON ('-' for stdout) and exit;
                             with --sample-project, open a temporary project first
                             and add each command's id and whether it works yet
```

## Environment variables

* `CUTTHULHU_MODULE_PATH`: folders of extra modules (see `--list-providers`).
* `CUTTHULHU_INTEROP_CORPUS`: where the interchange corpus is kept (Phase 3
  gates).
* `LUMENIS_MEDIA_DRIVES`: folders to offer as media drives, besides the
  volumes, for Import, Consolidate/Transcode and the Media Creation setting.
  Separate them as `PATH` does (`:` on macOS and Linux, `;` on Windows).
  Each one gets an `Avid MediaFiles/MXF/<n>` folder when media is made on
  it. Use it to keep test media off the system drive.
