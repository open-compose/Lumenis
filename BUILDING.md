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

The `asan` presets (Linux and macOS, GCC or Clang) build everything in Debug
with AddressSanitizer and UBSan and stop at the first report:

    cmake --preset asan && cmake --build --preset asan && ctest --preset asan

The self-hosted Linux job runs them on every push.

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
| Windows (elevated PowerShell) | `scripts\setup-windows-runner.ps1 builds` | `.msi` | `C:\Users\ghrunner\Cutthulhu Builds` (the runner's profile), linked as `Documents\Cutthulhu Builds` |
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

### A CLAP plug-in is missing, or Cutthulhu crashes at start-up

CLAP plug-ins are loaded at start-up (docs/spec/clap-host.md):
* **A plug-in is missing.** Open Tools > Console. It says how many plug-ins
  were found, which were left out and why (an instrument, or more than two
  channels), and which files could not be loaded. Plug-ins are looked for
  in the CLAP folders of your system and in the folders listed in
  `CLAP_PATH`.
* **Cutthulhu crashes at start-up after you installed a plug-in.** Start it
  with `LUMENIS_NO_CLAP=1` set, which skips the scan. Then move the
  plug-in out of the CLAP folders.

### Audio Punch-In records silence

Each system has to let Cutthulhu use the microphone (Tools > Audio
Punch-In; docs/spec/punch-in.md):
* **macOS** asks the first time a take starts. That needs the app's
  `NSMicrophoneUsageDescription` (packaging/macos/Info.plist.in). Without it
  the input gives only silence, so CI's installer gate checks that the key
  is in the .dmg's app. A Developer ID build also needs the audio-input
  entitlement (packaging/macos/entitlements.plist, used by
  sign-and-notarize.sh). If the question was answered No, allow it again
  under System Settings > Privacy & Security > Microphone. A build run from
  the terminal asks on the terminal's behalf.
* **Windows**: Settings > Privacy & security > Microphone, "Let desktop
  apps access your microphone".
* **Linux**: the input chosen in PipeWire or PulseAudio (for example
  `pavucontrol`, Input Devices) must not be muted.

The Input Source menu lists the inputs miniaudio finds; "Default" is the
system's choice.

### The "]" button under a monitor marks an OUT point

Before this was fixed, the Composer's "]" button (left of Play) marked OUT and "[" marked IN. Media
Composer has it the other way: "]" is Mark IN and "[" is Mark OUT, and Go to IN and Go to OUT follow
the same rule (the Capture tool's labelled buttons, Editing Guide p.117). The buttons are now
assigned that way, and the I and O keys did not change. `tst_composer`'s `buttonsBelowTheMonitors`
checks the order. If a saved button mapping (Command Palette) still puts a mark on the wrong
button, map that button again.

### A marker turns the whole monitor frame its colour

Before this was fixed, a monitor showing a marked frame was painted all over in the marker's colour
in two-monitor view: the marker oval left the painter's fill set, and the active monitor's outline,
drawn next, filled the frame with it. The oval also lay on its side and the marker's comment was not
shown. Now the picture is untouched, an upright oval in the marker's colour sits at the bottom of the
frame and the comment is written under it on a dark band (Editing Guide p.413). `tst_monitor_markers`
checks the frame, the oval, the comment and clicking the oval.

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

### The .msi is not kept

The Windows job used to keep the `.msi` in the owner's
`Documents\Cutthulhu Builds`. In CI run 179, after the owner changed the
PC's security settings (Acronis was using a lot of CPU and memory), the
runner's user could no longer write there: `Copy-Item : Could not find file
'...\Documents\Cutthulhu Builds\cutthulhu-....msi'`. Security software
blocks writes to Documents this way (Controlled Folder Access, Acronis
Active Protection).

So the job now keeps it in its own profile, `C:\Users\ghrunner\Cutthulhu
Builds`, which its user can always write in. Run
`scripts\setup-windows-runner.ps1 builds` once more, elevated, from your own
account. It gives you read access to that folder and links it into your
Documents as `Cutthulhu Builds`. If the old folder is still there, the link
is `Cutthulhu Builds (CI)`; delete the old one when you no longer need its
files, then run `builds` again to take the plain name.

Keeping a copy is not a check of the build: if it fails, the step warns
instead of failing the job. The warning prints the Controlled Folder Access
setting, the folder's attributes and its permissions.

### A test fails in CI but its report is missing

ctest's capture of a test's output can come back without QTest's FAIL
lines (seen on Windows). Every QTest test also writes its report to
`<build>/test-logs/<name>.txt`, and the CI step "Logs of failed tests"
prints the reports that contain a failure. Locally, look in
`build/ci/test-logs/`.

Tests that move or delete media files must let go of them first
(`MediaFrames::clear()`): Windows does not move or delete open files.

### A WAV file does not open: "End of file" (Windows, macOS)

FFmpeg 7 and 8 (the Windows and Homebrew builds) can take some valid WAV
files for another format when they probe them, and then fail with "End of
file". Ubuntu 24.04's FFmpeg 6.1 opens them, so Linux passes. A 32 kHz
24-bit WAV with LTC on it does this (`tst_clip_audio`'s Change Sample
Rate). `openInput` in `src/media/media.cpp` therefore opens `.wav`,
`.wave` and `.bwf` files with the wav demuxer first, and probes them only
when that fails. To try a file with FFmpeg 8 on Linux, install PyAV in a
virtual environment (`pip install av`) and compare `av.open(path)` with
`av.open(path, format="wav")`.

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

### `transcodeRoundTrips`: "cannot open the dnxhd encoder: Cannot allocate memory"

Seen on the self-hosted Windows runner (runs 236 and 237), where a re-run of the job alone passed. FFmpeg's
DNx encoder sets up buffers for every thread it starts. With automatic threads on a many-core machine
that is already running other tests or jobs, those allocations can fail. `MediaWriter::open` now opens such
an encoder again with one thread when the first open fails for want of memory, so transcodes, renders and
consolidations go on, more slowly. If it still fails, the machine is out of memory: close other programs
or run fewer tests at once (`ctest -j`).

### macOS: "implicit conversion from 'std::int64_t' to 'long double' may lose precision"

On Apple arm64 `long double` is a plain 64-bit `double`, so Apple Clang's
`-Wimplicit-int-float-conversion` (part of `-Wconversion`, an error in the
`ci` preset) flags 64-bit integers turned into `long double`. Clang on x86
Linux has an 80-bit `long double` and says nothing. Use integer arithmetic
for frame and sample counts, or explicit casts. Before pushing, run
`scripts/check-apple-conversions.py build/clang`: it compiles every file of
the Clang build as the Mac would see `long double`.

### macOS: "private field 'x' is not used [-Werror,-Wunused-private-field]"

Apple's Clang (Xcode 15 and later) reports a private member that is set
but never read, which neither Ubuntu's Clang 15 or 18 nor GCC report for
the same code (the mixdown renderer's `format_`, 2026-10-25). Remove the
field, or read it. Only the Mac job catches it, and it runs on every push.

### macOS: `tst_playback` "the picture was N frames from the sound", with many frames dropped

Look for `qt.qpa.fonts: Populating font family aliases took ... ms` in the
test's log. A widget asked for a font family by a name macOS does not have
(the List Tool asked for "Monospace"), and Qt built its family aliases, a
quarter of a second with the window stalled, in the middle of playback. Use
a family that `QFontDatabase::families()` lists, as `fixedFont()` in
`src/ui/list_tool.cpp` does: even `QFontDatabase::systemFont(FixedFont)` can
name the missing generic family (the offscreen platform the tests use on
macOS), and `QFontDatabase::hasFamily` resolves aliases itself.
`tst_playback` and `tst_interchange` fail on that warning, so a new one is
named where it happens.

Without that warning, the machine held the test's own thread back: on
2026-10-25 the Mac runner twice showed 11 or 12 frames and dropped 37 or
38, which adds up to the whole of the test's two-second clip: it had played
to its end, where the picture holds the last frame, before the test
compared the picture with the sound, so the gap could not close. The clip
is now twenty seconds long, the looks at the picture and the sound go on
for up to two seconds until one lands on a tick in time (the 2-frame limit
stays), and a clip played to its end fails with that message instead. If it
still fails, see what else was running on the Mac then;
`scripts/measure-playback.py` measures sync over a longer run.

### A test crashes on Windows (0xc0000374) or macOS but passes on Linux

0xc0000374 is Windows' heap corruption: memory used after it was freed,
or written past its end. glibc's allocator often lets the same mistake
pass, so the Linux jobs stay green. Run the sanitizers on Linux
(`cmake --preset asan && cmake --build --preset asan && ctest --preset asan`,
see Tests): they name the line. The usual cause is a reference into a
temporary, e.g. `const Segment& s = Sequence::fromItem(item).track(...)->
segments.first();`; keep the temporary in a variable first. The
self-hosted Linux job runs the sanitizers on every push so this is caught
there too.

### Windows: "fatal error C1001: Internal compiler error"

MSVC 14.44 (Visual Studio 2022 17.14) crashed compiling `src/media/key_effects.cpp` at the RGB
Keyer's pixel loop. Writing it out plainly was not enough (CI run 219); moving the loop's body into a
function of its own, `rgbKeyPixel`, compiled with `#pragma optimize("", off)` under MSVC only,
avoided it, until an unrelated header change made MSVC rebuild the file and crash a few lines further on,
at the matte blur (CI run 250). Now all of `rgbKeyerPicture` is compiled with the optimizer off under MSVC;
when the error moves within one function, cover the whole function rather than the next line. An internal compiler error is the
compiler's fault, not the code's: GCC and Clang build the same file cleanly. Simplify the statement
at or just before the line the error names, keep the same result (the tests check it), and leave a
comment saying why, so nobody "tidies" it back. The self-hosted Windows job is the check: it is the
only MSVC build, so a change in `src/media` is not done until that job is green.

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
* *Tests time out ("Test function timed out"), and every test takes
  seconds:* something on the PC is scanning each new file or process
  (an antivirus or backup tool's real-time protection, such as Acronis
  Active Protection, or a Windows Defender scan). Once, every test ran
  100 times slower and the tests that write hundreds of files passed 300 s.
  The job's "File speed check" step warns when writing 200 small files
  takes over 10 s. Exclude the runner's folder (`C:\actions-runner`) and
  `ghrunner`'s temporary folder from the scanner, or wait for the scan to
  end. Tests should read and write damaged inputs in memory, not through
  hundreds of temporary files.
* *"Qt 6.8.3 is not in C:\Qt":* run `scripts\setup-windows-runner.ps1
  install <token>` again; it installs Qt and keeps the existing runner.
* *"Pillow is missing on this runner":* the screenshot comparison runs on
  the WSL runner while `HOSTED_CI` is `off` and needs Pillow: in the
  distribution, `sudo apt-get install -y python3-pil` (newer setup scripts
  install it).
* *"Only N GB free on the runner's disk", or "No space left on device"
  (CMake then calls the compiler broken):* the self-hosted jobs need about
  5 GB, in the work folder and in the temporary folder (where the compiler
  writes); the "Disk space" step stops a job with less of either. On Ubuntu
  26.04 `/tmp` is a tmpfs, in memory and small, and it once filled up with
  939 GB free on the disk: the Linux jobs now set `TMPDIR` to the runner's
  own temporary folder on the work disk, which the runner empties after
  each job. To empty `/tmp` itself, run `wsl --shutdown` in Windows (a
  tmpfs is cleared when the distribution stops). Each keeps its build
  folders (`build/ci`, `build/asan`, several GB) in the runner's work
  folder, and `keep-build.sh` the newest five packages. In WSL, see what
  is using the disk with `sudo du -xh --max-depth=2 / | sort -h | tail`,
  and the runner's folder with `sudo du -sh
  /home/ghrunner/actions-runner/_work/*`. Deleting
  `/home/ghrunner/actions-runner/_work/Lumenis/Lumenis/build` is safe (the
  next job builds again). WSL's virtual disk grows to its limit and does not
  shrink on its own: `wsl --manage <distribution> --set-sparse true` in
  Windows lets it, and Windows' own free space limits it too.
* *"systemd is not running" from setup-wsl-runner.sh:* add `[boot]` and
  `systemd=true` to `/etc/wsl.conf`, run `wsl --shutdown` in Windows, and
  open the distribution again.

### pip install opentimelineio: "Could NOT find Python (missing: Python_INCLUDE_DIRS ...)"

OpenTimelineIO has no prebuilt wheel for the newest Pythons (0.18.1 has
none for 3.14, Ubuntu 26.04's), so pip compiles it, and that needs Python's
C headers, which Ubuntu packages separately: `sudo apt-get install -y
python3-dev`, then run `scripts/check-python.py` (or the `pip install`)
again. It builds and passes `otio_python` on 3.14. Configuring prints the packages still missing
([Python packages for the checks](#python-packages-for-the-checks)).

### CI: "Python packages for the checks" takes long or stops after 20 minutes

`scripts/check-python.py` runs pip without `-q`, so the step's log shows
what pip was fetching or building when it stalled. pip asks nothing
(`--no-input`), gives up on a stalled connection after 60 s, and the
install is stopped after 20 minutes (the step after 25), so the job goes on
with those checks skipped. A first run that compiles OpenTimelineIO takes a
few minutes; later runs take under a second. If it stalls on a download,
check the machine's network or proxy for pip (`pip config list`). An
earlier, quiet version once ran for 37 minutes on the Windows runner
without a word.

### CI: a run fails with no jobs

GitHub refuses a workflow file it cannot use, and the run then shows
"failure" with no jobs and no log. Once, a job's `env:` set
`TMPDIR: ${{ runner.temp }}`; the `runner` context is not available there
(only in steps), so every job was dropped. ctest `workflows_lint`
(`scripts/check-workflows.py`)
finds such mistakes before a push; run it after changing a workflow. It
downloads the pinned actionlint release once (checked by SHA-256) into
`~/.cache/cutthulhu` (or `$CUTTHULHU_TOOLS`), and is skipped without a
network. Labels of the self-hosted runners are listed in
`.github/actionlint.yaml`.

actionlint was first a package in `scripts/check-requirements.txt`
(actionlint-py). It has no Windows wheel, and building one on the Windows
runner took 20 minutes, so the install was stopped and every interchange
check was skipped. The check environment holds only the readers now;
tools come as pinned downloads.

### Fuzzing: "cannot find .../libclang_rt.fuzzer-x86_64.a"

Clang is installed without its runtime libraries (libFuzzer, the
sanitizers), which Ubuntu packages separately: `sudo apt-get install -y
libclang-rt-dev` (or `libclang-rt-<version>-dev` to match `clang++
--version`). The Fuzz workflow checks this first and says so.

### Fuzzing: "HTTP 404: Not Found (.../actions/workflows/fuzz.yml)"

`gh workflow run` only finds a workflow that GitHub has seen run, and a
workflow only run by hand is not seen until it is on the default branch.
`fuzz.yml` therefore also runs on a push that changes it, with its job
skipped, which registers it. If the 404 persists (the registering run is
missing under the repository's Actions tab), push any change to
`.github/workflows/fuzz.yml`, or merge the branch into `main`, then run
`gh workflow run fuzz.yml --ref <branch> -f hours=24`.

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

### Python packages for the checks

Several interchange checks have another implementation read the same files:
olefile (`cfb_olefile`), pyaaf2 (`aaf_pyaaf2`, part of `aaf_round_trip`),
pyavb (`avb_pyavb`, `avb_write_pyavb`, `avb_export_pyavb`),
OpenTimelineIO (`otio_python`) and its FCP adapter (`fcpxml_python`). Without its package a check is skipped, and
configuring prints which packages the Python it found lacks.
`scripts/check-python.py` puts them, at the versions pinned in
`scripts/check-requirements.txt`, into a virtual environment
(`~/.cutthulhu-venv`, or `--venv DIR`) and prints its Python; run again, it
does nothing unless the pins changed or the environment broke. Then point
CMake at that Python:

```sh
# Linux (Ubuntu): venv, and the headers pip compiles OpenTimelineIO against
# when it has no wheel for your Python (bootstrap-linux.sh installs both)
sudo apt-get install -y python3-venv python3-dev
python3 scripts/check-python.py
cmake --preset ci -DPython3_EXECUTABLE=$HOME/.cutthulhu-venv/bin/python
```

On Windows: `py scripts\check-python.py`, then `cmake --preset ci
-DPython3_EXECUTABLE="$HOME\.cutthulhu-venv\Scripts\python.exe"`.

**CI** runs the script in every job before configuring ("Python packages
for the checks") and configures with that Python. On the self-hosted
runners the environment stays in the runner's home between jobs; if it
cannot be made, the step is marked failed with an error annotation and the
job goes on with those checks skipped. The setup scripts make it when they
install a runner. For a runner installed before that:

* **WSL:** `sudo apt-get install -y python3-venv python3-dev` in the
  distribution; the next job makes the environment.
* **Mac:** nothing; the next job makes it. With only the Command Line
  Tools' Python 3.9, pip compiles OpenTimelineIO (a few minutes, once);
  `brew install python` avoids that.
* **Windows:** nothing when a Python is installed for all users (in
  `C:\Program Files`, on the runner's PATH): the next job makes the
  environment in the runner's profile. Otherwise, in an elevated
  PowerShell, `scripts\setup-windows-runner.ps1 python`: the runner's user
  cannot use a Python installed in your profile, so this installs Python
  3.12 for all users, makes the environment in
  `C:\actions-runner-tools\check-venv` and names it in the runner's `.env`
  (`CUTTHULHU_CHECK_VENV`).

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
  `python3 scripts/interop-corpus.py manifest` (needs olefile, pyaaf2 and
  pyavb).
* **AAF objects:** `tst_aaf` reads every corpus AAF as pyaaf2 does, compared
  by the digest of a dump. ctest `aaf_pyaaf2` compares the dumps themselves
  and lists any difference; it needs pyaaf2 (`pip install pyaaf2`) and is
  skipped without it. By hand: `python3 scripts/aaf-dump.py --compare --app
  build/ci/bin/cutthulhu FILE.aaf`.
* **Bins (.avb):** `tst_avb` reads every corpus bin as pyavb does,
  compared by the digest of a dump. ctest `avb_pyavb` compares the dumps
  themselves, byte for byte, and lists any difference; it needs pyavb
  (`pip install pyavb`) and is skipped without it. By hand: `python3
  scripts/avb-dump.py --compare --app build/ci/bin/cutthulhu BIN.avb`.
* **Writing bins (.avb):** ctest `avb_round_trip` reads and writes back
  every corpus bin (`cutthulhu --rewrite-avb IN.avb OUT.avb`) and checks
  that the files are the same, byte for byte; it needs only the corpus.
  ctest `avb_write_pyavb` has pyavb read what the writer makes (a bin built
  from scratch, a bin exported from the model, and the corpus written
  big-endian), from tst_avb's
  `forPyavb`; it needs pyavb and is skipped without it. By hand: `python3
  scripts/interop-corpus.py check-avb-write --test build/ci/bin/tst_avb`.
* **Native bins written as .avb:** ctest `avb_export_round_trip` reads each
  corpus bin that has a reference dump, writes what it read as a bin and
  reads that again; the two readings must be the same (`structdiff.py
  --fields --sources`); it needs only the corpus. ctest `avb_export_pyavb`
  has pyavb read the same bins written: every object as Cutthulhu reads it
  (`avb-dump.py --compare`) and the bin as the one exported
  (`avb-structure.py`, `structdiff.py --no-effects`); it needs pyavb and is
  skipped without it. By hand: `cutthulhu --export-avb BIN.avb OUT.avb` (or
  a .cthbin), then `--import-avb` both and `structdiff.py --fields
  --sources`, or `python3 scripts/interop-corpus.py check-avb-export --app
  build/ci/bin/cutthulhu` (and `check-avb-export-pyavb`).
* **AAF import against Media Composer's bins (gate 1 on the corpus):**
  ctest `aaf_import_bins` imports each corpus AAF that has a bin of the same
  material and compares it with the bin's dump in `tests/data/aaf_reference`
  (`structdiff.py --no-effects`). Rewrite those dumps with
  `python3 scripts/interop-corpus.py reference` (needs pyavb: `pip install
  pyavb`). By hand: `cutthulhu --import-aaf FILE.aaf > ours.json` and
  `python3 scripts/avb-structure.py BIN.avb > ref.json`, then
  `structdiff.py ref.json ours.json --no-effects`.
* **Bins read into the native model (gate 1 on the corpus):** ctest
  `avb_import_bins` reads each corpus bin that has a reference dump in
  `tests/data/aaf_reference` and compares it with that dump
  (`structdiff.py --no-effects`); it needs only the corpus. By hand:
  `cutthulhu --import-avb BIN.avb > ours.json` and `structdiff.py
  tests/data/aaf_reference/BIN.json ours.json --no-effects`, or
  `python3 scripts/interop-corpus.py check-avb-import --app
  build/ci/bin/cutthulhu`.
* **AAF round trips (gate 3, Cutthulhu's half):** ctest `aaf_round_trip`
  imports, exports and imports again every corpus AAF and compares the two
  imports; with pyaaf2 installed it also has pyaaf2 read each export. By
  hand: `cutthulhu --export-aaf FILE.aaf --to OUT.aaf` (or a .cthbin), then
  `--import-aaf` both and `structdiff.py`.
* **EDLs and shot logs (3.6a):** `tst_edl` and `tst_ale` read, write and
  read again lists and logs made in the tests, and every sample EDL and ALE
  of OpenTimelineIO's CMX 3600 and ALE adapters (Apache-2.0), which the
  corpus downloads into `otio-cmx3600` and `otio-ale`; without the corpus
  those parts are skipped. `tst_interchange` drives File > Input > Import
  EDL, Tools > List Tool, shot log Import, Export Bin and Export to File.
  By hand: `cutthulhu --import-edl FILE.edl [--rate 25] > ours.json`,
  `cutthulhu --export-edl BIN.cthbin "Sequence name" OUT.edl [--edl-type
  File_129]` (an .edl in place of the bin is a round trip), `cutthulhu
  --import-ale FILE.ale` and `cutthulhu --export-ale BIN.cthbin OUT.ale`.
* **OpenTimelineIO (3.6b):** `tst_otio` reads, writes and reads again
  timelines made in the test and the sample files of OpenTimelineIO
  (Apache-2.0), which the corpus downloads into `otio`. ctest
  `otio_round_trip` exports every sample, then exports the export, and
  needs the same JSON; ctest `otio_python` has OpenTimelineIO's own Python
  package (`pip install opentimelineio`; skipped without it) compare each
  export with the original, clip by clip (docs/spec/otio.md, "Checks"). By
  hand: `cutthulhu --import-otio FILE.otio > ours.json`, `cutthulhu
  --export-otio BIN.cthbin "Sequence name" OUT.otio` (an .otio in place
  of the bin is a round trip, and then the name may be left out), and `python3 scripts/otio-check.py
  --compare --app build/ci/bin/cutthulhu FILE.otio...`.
* **FCP 7 XML (3.6c):** `tst_fcpxml` reads, writes and reads again a
  document made in the test and the FCP XML files of OpenTimelineIO's FCP
  adapter (Apache-2.0), which the corpus downloads into `otio-fcp`. ctests
  `fcpxml_round_trip` and `fcpxml_python` are the two OTIO checks with the
  exports written as FCP XML, from the FCP XML and the .otio files; the
  second reads them with OpenTimelineIO's FCP adapter (`pip install
  otio-fcp-adapter`; docs/spec/fcp-xml.md, "Checks"). By hand: `cutthulhu
  --import-fcpxml FILE.xml`, `cutthulhu --export-fcpxml BIN.cthbin
  "Sequence name" OUT.xml`, and `otio-check.py --format fcpxml`.
* **The built-in AAF model** (`src/interop/aaf_model.inc`) is made from
  pyaaf2's model tables: `python3 scripts/make-aaf-model.py --pyaaf2
  <pyaaf2 checkout>` (`--check` to compare).
* **Gate 4, fuzzing:** see [Fuzzing](#fuzzing) below.
* **Shared bins (3.7):** `tst_bin_lock` and `tst_bin_locking` run
  everywhere, on local folders with `LUMENIS_SHARED_BINS=all`. By hand, on
  a real NAS share mounted on two systems: open the project on both, open a
  bin on the first (green) and on the second (red, bold in its Bin
  Container); save on the first (yellow on the second, a click reads it);
  close it on the first (blue, a click takes it); Lock, Protect and Unlock
  Project Bin. Each system keeps the bins it has open in
  `<project>/<machine name>/open-bins.json`; locks are
  `<bin>.cthbin.lck`, and a lock left by a crash is removed by opening
  the bin again as the same user on the same machine, or by deleting that
  file.

## Fuzzing

Phase 3 gate 4 asks that fuzzing the bin and AAF parsers for 24 hours
produces no crashes. `fuzz/` holds six libFuzzer targets (fuzz/README.md
says what each runs): `cfb` (the compound file container), `aaf` (AAF
objects, the `--dump-aaf` dump and the import into bins), `avb` (bins, the
`--dump-avb` dump and the import), `avb_rewrite` (the bin writer: what is
read is written back and must read back the same), `otio`
(OpenTimelineIO files, imported, exported and imported again) and `fcpxml`
(FCP 7 XML, the same). A crash, a hang (an
input taking over 10 s), an AddressSanitizer or UBSan report, or a failed
round-trip check is a failure.

**Requirements:** Linux or macOS with Clang and its runtime libraries. On
Ubuntu, `sudo apt-get install -y clang libclang-rt-dev` (or
`libclang-rt-18-dev` for Clang 18); Xcode's Clang has them. GCC and MSVC
cannot build the fuzzers, but every build makes `lumenis_fuzz_replay`,
which runs saved inputs through the same targets.

**Every build:** ctest `fuzz_replay` runs the regression inputs
(`fuzz/regressions/`), the seeds our writers make and the downloaded
interchange corpus through all four targets, in a few seconds.

**By hand:**

    python3 scripts/fuzz.py build      # cmake --preset fuzz, then build it
    python3 scripts/fuzz.py seed       # build/fuzz/corpus/<target>/
    python3 scripts/fuzz.py run --time 1h --jobs 2
    python3 scripts/fuzz.py run --targets avb --time 30m --each

`seed` makes each target's corpus from the interchange corpus (`.aaf` for
`cfb` and `aaf`, `.avb` for the others; each AAF is also laid out again in
512-byte sectors and without its metadictionary, which the fuzzer mutates
far faster), the seeds made by our own writers, and the regression inputs,
keeping only what adds coverage. None of these are committed. `run` splits
`--time` between the targets (`--each`: that long each), runs libFuzzer in
fork mode with `--jobs` processes, adds what it finds to the corpus, and
prints a summary: seconds, executions, executions per second, coverage
(`cov`, edges; `ft`, features) and corpus size. It stops at the first
failure, prints the stack and the reproducer as base64, and exits 1; the
log and the reproducer are in `build/fuzz/artifacts/<target>/`. With
`--keep-going` (the Fuzz workflow's way) it fuzzes every target for its
whole time, keeps every failing input, then groups them into problems by
their stacks and prints each problem's stack and smallest reproducer, a
list of the problems after the summary, and exits 1 if there were any: one
run finds all it can. To
reproduce: `build/fuzz/bin/fuzz_<target> FILE`, or in any build
`lumenis_fuzz_replay --target <target> FILE` (the `asan` preset for the
sanitizer's report). fuzz/README.md, "After a crash", says how to minimise
it, fix it, and keep it as a regression input with a unit test.

**The 24-hour gate run** goes on the self-hosted Linux runner (Ubuntu in
WSL, with Clang installed as above):

    gh workflow run fuzz.yml --ref <branch> -f hours=24 -f jobs=2

The Fuzz workflow (`.github/workflows/fuzz.yml`, run by hand only) builds
the `fuzz` preset, replays the regression inputs, seeds, and runs every
target for its share of the hours: with the default weights, 24 hours are
2.7 for `cfb`, 8 each for `aaf` and `avb` and 5.3 for `avb_rewrite`. It
prints a progress line every 10 minutes and the summary at the end. Every
target runs for its whole share even after a failure (`--keep-going`), and
the job fails if anything failed: the log lists the problems after the
summary (`== problems`), and above it gives each one's stack and smallest
reproducer as base64 (nothing is uploaded: the artifact quota is used up).
Decode each (`base64 -d > input`), reproduce, fix, add it to
`fuzz/regressions/`, and run the gate again. The corpora it grows are kept in the runner's
`_work/Lumenis/cutthulhu-fuzz/`, so a later run starts from them. The gate
passes when one full 24-hour run is clean; record its summary here.
Meanwhile the runner takes no other jobs, so start it when CI can wait (or
on a second runner).

**Runs so far** (Clang 18, Ubuntu 24.04 container, 4 cores; executions are
over all processes):

| Date | Target | Processes | Time | Executions | Exec/s | Coverage (cov / ft) | Corpus | Result |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-10-22 | `aaf` | 2 (fork) | 61 min | 265,424 | 56 | 35,060 / 30,603 | 917 | clean |
| 2026-10-22 | `avb` | 1 | 60 min | 660,110 | 183 | 6,352 / 21,334 | 809 | clean |
| 2026-10-22 | `avb_rewrite` | 1 | 40 min | 382,570 | 159 | 3,628 / 17,025 | 681 | clean |
| 2026-10-22 | `cfb` | 1 | 35 min | 215,220 | 102 | 1,669 / 8,278 | 413 | clean |

In fork mode libFuzzer counts coverage over all its processes, so `aaf`'s
`cov` is not comparable with the single-process rows. Earlier, shorter runs
found the bugs listed in fuzz/README.md ("Regression inputs"): two hangs and
a division by zero in the imports, a dump that ran away on shared
references, an overflow in the compound file reader, and three ways the bin
writer did not write back what it read. Each is fixed, kept as a
regression input and a unit test. The first three 24-hour gate runs
(2026-10-22 and 23, on the WSL runner) each ran `cfb` clean for its 2.7
hours and then stopped in `aaf`, each on a new bug: a dump that grew with
the product of repeated references (a timeout), and two signed overflows in
the import's frame arithmetic, also kept as regression inputs. Runs now go
on after a failure, so one run reports all it finds. The fourth run
(2026-10-23 17:09 to 2026-10-24 17:16 UTC, run 39992718177, 2 processes a
target) was clean on every target, and the gate passed:

| Target | Seconds | Executions | Exec/s | Coverage (cov / ft) | Corpus | Result |
| --- | --- | --- | --- | --- | --- | --- |
| `cfb` | 9,632 | 3,884,980 | 192 | 1,695 / 9,069 | 510 | clean |
| `aaf` | 28,930 | 8,091,179 | 77 | 7,889 / 41,087 | 1,743 | clean |
| `avb` | 28,842 | 43,284,447 | 841 | 6,496 / 27,495 | 2,105 | clean |
| `avb_rewrite` | 19,334 | 37,310,820 | 1,031 | 3,552 / 18,916 | 1,613 | clean |

The log is on GitHub: the repository's Actions tab, the Fuzz workflow, the
run, then its job; the table above is its `== summary`. Download it with
`gh run view <run id> --log > fuzz.log` (or the job page's gear menu,
"Download log archive"). The runner also keeps each target's libFuzzer log
in `_work/Lumenis/cutthulhu-fuzz/artifacts/<target>/fuzz.log` until the
next run.

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
cutthulhu --dump-avb FILE [--canonical]
                             print a Media Composer bin (.avb) from its root object as JSON, for
                             scripts/avb-dump.py --compare (docs/spec/avb.md)
cutthulhu --import-aaf FILE   import an AAF file into a bin and print the bin as a structure dump
                             (docs/spec/aaf.md, "Import")
cutthulhu --export-aaf BIN --to FILE
                             export a bin (.cthbin) as AAF; an .aaf is imported first (a round trip)
cutthulhu --export-avb BIN OUT.avb
                             write a bin (.cthbin) as a Media Composer bin, with the clips its
                             sequences use from the other bins of its folder as reference clips;
                             an .avb is imported first (a round trip) (docs/spec/avb.md, "Export")
cutthulhu --import-edl FILE [--rate R]
                             read an EDL (CMX_3600, File_129) into a sequence and print the bin
                             as a structure dump (docs/spec/edl.md)
cutthulhu --export-edl BIN SEQUENCE OUT.edl [--edl-type CMX_3600|File_129]
                             write a sequence of a bin (.cthbin) as an EDL; an .edl is read
                             first (a round trip)
cutthulhu --import-otio FILE  read an OpenTimelineIO file (.otio) into master clips and sequences
                             and print the bin as a structure dump (docs/spec/otio.md)
cutthulhu --export-otio BIN SEQUENCE OUT.otio
                             write a sequence of a bin (.cthbin) as OpenTimelineIO; an .otio is
                             read first (a round trip; without SEQUENCE its first timeline)
cutthulhu --import-fcpxml FILE
                             read an FCP 7 XML file (xmeml) as --import-otio does (docs/spec/fcp-xml.md)
cutthulhu --export-fcpxml BIN SEQUENCE OUT.xml
                             write a sequence as FCP 7 XML, as --export-otio does; an FCP XML or .otio
                             file is read first (--export-otio also takes an FCP XML file)
cutthulhu --import-ale FILE   read an ALE shot log into master clips and print the bin as a
                             structure dump (docs/spec/ale.md)
cutthulhu --export-ale BIN OUT.ale
                             write a bin's (.cthbin) clips as an ALE shot log
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
* `CUTTHULHU_FUZZ_REGRESSIONS`: set to `fuzz/regressions` when running
  `tst_cfb`, `tst_aaf` and `tst_avb`, and the tests that build the
  fuzzers' regression inputs write them there (fuzz/README.md).
* `LUMENIS_MEDIA_DRIVES`: folders to offer as media drives, besides the
  volumes, for Import, Consolidate/Transcode and the Media Creation setting.
  Separate them as `PATH` does (`:` on macOS and Linux, `;` on Windows).
  Each one gets an `Avid MediaFiles/MXF/<n>` folder when media is made on
  it. Use it to keep test media off the system drive.
* `LUMENIS_SHARED_BINS`: `all` (or `1`) treats every folder as shared
  storage, so bins take locks and show the Bin Lock Status button; `none`
  (or `0`) treats none as shared. Without it, network file systems (SMB,
  NFS, AFP, WebDAV and the like; on Windows network drives and UNC paths)
  are shared (docs/spec/bin-locking.md). Use `all` for a share Cutthulhu
  does not recognise, and to try locking on one machine with two users
  (start each with its own User profile).
