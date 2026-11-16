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
| FFmpeg 6.0 or later | Homebrew `ffmpeg` | GPL shared build from `scripts\get-ffmpeg.ps1` (set `FFMPEG_ROOT`; it has the H.264 and H.265 encoders) | `libavcodec-dev` and the other FFmpeg development packages |
| OpenColorIO 2.1 or later (optional) | Homebrew `opencolorio` | vcpkg `opencolorio` (set `OpenColorIO_ROOT`) | `libopencolorio-dev` |

Qt 6.8 LTS is the version release builds ship with. Qt 6.4 is the oldest
version supported, because that is the system Qt on Ubuntu 24.04.

OpenColorIO is optional. With it the OFX host converts images into the
colour spaces plug-ins ask for (docs/spec/ofx-host.md "Colour management");
without it they see the project's colour space only. The build uses it when
it finds it (`cmake/FindOpenColorIO.cmake`; `-DLUMENIS_OCIO=OFF` leaves it
out). The bootstrap scripts do not install it, so the packages CI builds
have none; a package built with it must carry its library, which the
packaging scripts do not check yet. `LUMENIS_NO_OCIO=1` turns conversion
off at run time, and the `OCIO` environment variable picks a config
(OpenColorIO 2.1 has no built-in one, so Ubuntu 24.04's needs it).

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

CI's `linux-asan` job runs them on every push.

The `tsan` presets build with ThreadSanitizer (RelWithDebInfo) and run the tests below the UI (label `core`: tests/core,
model, media and interop), where our own threads are (the compositor's passes, the frame cache, renders, decoding):

    cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan

Qt and FFmpeg are not built with ThreadSanitizer, and their own threads (FFmpeg's frame threads, QImage's conversion
pool, QThreadPool, QtTest's watchdog) synchronise in ways it cannot see. The preset sets
`ignore_noninstrumented_modules=1`, so only memory our code and tests touch is checked, and `tests/tsan.supp` is empty;
suppressing Qt by name hid our own races too, since every thread passes through it. Our locks are `std::mutex`
(pthreads, which ThreadSanitizer follows); a contended `QMutex` waits inside Qt (10.2). Use `std::mutex` for new
locks. Left out: the UI tests (each binary is about 750 MB with ThreadSanitizer) and `tst_dcp` and `tst_transcription`,
which start a program through `QProcess`: GCC 13's ThreadSanitizer runtime stops with `CHECK failed: tsan_rtl.cpp:253`
in the child Qt starts with `clone`. To build only the tests the preset runs, `cmake --build --preset tsan --target
<names>` with the names from `ctest --preset tsan -N`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and `claude/**`, on
pull requests and by hand, all on GitHub's hosted runners (the repository is
public, so their minutes and artifact storage cost nothing):

| Job | Runner | What it does |
|---|---|---|
| `linux` | `ubuntu-24.04` | system Qt 6.4: build, test, AppImage and .deb, the installer gate (also the AppImage on a headless Wayland compositor) |
| `linux-latest` | Ubuntu 26.04 container | GCC 15 and Qt 6.10 with warnings as errors |
| `linux-asan` | `ubuntu-24.04` | the `asan` preset: AddressSanitizer and UBSan over every test |
| `macos` | `macos-15` (Xcode 16), `macos-26` (Xcode 26) | arm64 build, test, `.dmg` and its gate |
| `windows` | `windows-2022` | MSVC 2022 build, test, `.msi` and `.zip`; installs the .msi, launches it, uninstalls it |
| `ui-consistency` | `ubuntu-24.04` | each platform's screenshots against Linux's ("CI screenshots" under Troubleshooting) |

The installer gate installs (or mounts) each package, launches the installed
program with `--smoke-test` and uninstalls it. Each job uploads its packages as
workflow artifacts, kept for GitHub's default 90 days: open the run on the
repository's Actions tab, and they are listed at the bottom of its summary page
(`gh run download <run id>` fetches them). These are development builds, with
the patent-encumbered codecs on; the packages for people to download come from
the release workflow ("Releases").

The self-hosted runners on the owner's machines were removed before the
repository went public (ROADMAP 10.0): a pull request from a fork could run
code on them.

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

### Packaging the AppImage fails with "curl: (22) ... error: 504"

`packaging/linux/make-appimage.sh` downloads the pinned linuxdeploy tools
from GitHub's release pages, and a busy server can answer 504. The tools
are kept in a per-user cache, `~/.cache/cutthulhu/appimage-tools/<tags>`
(or `$LUMENIS_APPIMAGE_TOOLS`), so they are downloaded once per version,
not once per build folder. A download goes to a `.part` file first and is
retried on server errors. If it still fails, run the script again later,
or put `linuxdeploy` and `linuxdeploy-plugin-qt` (executable) into that
folder yourself.

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

OFX video effect plug-ins are loaded at start-up too (docs/spec/ofx-host.md):
* **A plug-in effect is not in the Effect Palette.** Open Tools > Console. It
  says how many OFX plug-ins were found and added, and for each one left out
  why: its binary could not be loaded (often a supporting library it needs
  is missing), its describe action failed, or it supports none of the
  Filter, Generator, Transition and General contexts. Plug-ins are looked
  for as `.ofx.bundle` folders in the OFX folder of your system
  (`C:\Program Files\Common Files\OFX\Plugins`, `/Library/OFX/Plugins`,
  `/usr/OFX/Plugins`) and in the folders listed in `OFX_PLUGIN_PATH`. The
  bundle needs a binary for your platform (`Contents/Win64`,
  `Contents/MacOS`, `Contents/Linux-x86-64`).
* **A plug-in effect's icon in the Timeline is blank.** The plug-in it was
  made with is not installed here (a sequence from another system). Tools >
  Console names it: "Can't find effect: <label> (<identifier>)". Install the
  plug-in and restart Cutthulhu; the effect keeps its values meanwhile.
* **Cutthulhu crashes at start-up after you installed a plug-in.** Start it
  with `LUMENIS_NO_OFX=1` set, which skips the scan. Then move the bundle
  out of the OFX folders.

### Tests leave media in "Avid MediaFiles" on your drives, or crash probing it

Before 2026-11-04 some tests made managed media on the computer's real drives (the top of a
writable volume, or your home folder's `Avid MediaFiles/MXF/1`), and a test scanning a drive could
crash in FFmpeg reading a file another test was still writing (`tst_motion_adapter_ui` under
`ctest -j8`). Every test now runs with `LUMENIS_NO_SYSTEM_DRIVES=1` and a drive folder of its own in
`build/<preset>/test-media/<test>` (tests/CMakeLists.txt); `tst_media_unmount` checks that nothing
else is offered. Delete the `Avid MediaFiles` folders the tests left behind on a development machine, after checking they hold nothing of yours (their clips are named Take, Tone,
Mixdown and the like).
`build/<preset>/test-media` holds only test media and can be deleted between runs: the
`test_media_drives` ctest fixture makes the folders again before the tests that use them.

### Esc (Toggle Source/Record) does nothing, or the Record monitor stays an Effect Preview

Before 2026-11-05 the Toggle Source/Record command, on Esc and on the Command Palette's Edit tab, had
no handler; only the Timeline toolbar's Toggle Source/Record in Timeline button worked. Both now show
the Source monitor's clip in the Timeline (Editing Guide p.480, p.603); `tst_main_window` presses Esc
to check it. Neither leaves Effect mode: after Tools > Titler+ or the Effect Editor, the Record monitor
shows the Effect Preview, without markers under it, wherever the position is on an effect. Click
Source/Record Mode (or Effect Mode again) in the Timeline toolbar to get the Record monitor back.

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

### CI screenshots: where they are and how to look at the differences

"UI looks the same on all platforms" compares each platform's screenshots
with Linux's: `1x`, `2x` (QT_SCALE_FACTOR=2) and `pseudo`, the 1x windows in
the pseudo-locale (`--language pseudo`, docs/spec/localisation.md), whose
longer, accented texts show a control too small for German or French and
check that the layout stays the same everywhere with them. Each platform's job
uploads its screenshots as the artifact `screenshots-<platform>` (kept 7 days);
`ui-consistency` downloads them all and uploads the differences it finds as
`screenshot-diffs`. To look at them: `gh run download <run id> -n
screenshot-diffs`, or download the artifact from the run's summary page.

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

The first CI run on GitHub's macOS runners (2026-11-16) failed the same way
for the Effect Editor: spin box fields 4 px narrower and combo boxes and the
parameter area 2 px narrower. Fusion also scales the spin box buttons and the
combo box arrow (`subControlRect`), Qt 6.4's common style the spin box's
size hint, and QCommonStyle a set of metrics (layout margins and spacing,
title bars, dialog buttons ...). `theme.cpp` pins all of these at their
96 DPI values. Two things had hidden it: QTest's `QTEST_MAIN` sets
`Qt::AA_Use96Dpi`, which every platform but macOS obeys, and the test's loop
to `QStyle::PM_CustomBase` never ran, as that value is negative as an `int`.
`tst_theme sizesDoNotFollowTheDpi` now clears the attribute and gives the
style font metrics from a 128 DPI image, which makes Linux scale as macOS
did, and fails for any metric or rectangle that changes.

With the sizes pinned, the Effect Editor was still 2 px narrower on macOS.
That was the text, not the style. A combo box set to fit its items measures
each item's ink (`QFontMetrics::boundingRect`), and Core Text rounds that
differently from FreeType and DirectWrite, even for our bundled Inter. The
Position Tracking row's Axis menu set the editor's width, so the whole
parameter area moved. The menu is now sized by a count of characters
(`setMinimumContentsLength`), which goes by the font's advances, and those
are the same everywhere. If a window's width or a row's minimum width differs
by a pixel or two only on macOS, find the combo box that sets it. To see what
sets a window's minimum size, add `minimumSizeHint().width()` to
`describe()` in `src/ui/capture.cpp` for a local run.

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

### Export says there is no H.264 encoder

On Windows, File > Output > Export to File as MOV, MP4 or MXF with H.264 or H.265 made no file (owner's report,
2026-11-08). The FFmpeg that `scripts\get-ffmpeg.ps1` fetched was BtbN's LGPL build, which has no H.264 or H.265
encoder (libx264, libx265 are GPL), and the reason went only to the Console. Now:
* `scripts\get-ffmpeg.ps1` fetches the GPL build (`-Variant gpl`, the default) and stops if either encoder is missing.
  Run it again on a PC set up before this; it downloads the GPL build beside the old one and prints its folder. Set
  `FFMPEG_ROOT` to that folder and configure again (`cmake --preset ...`), or run `scripts\bootstrap-windows.ps1`.
* In CI the script sets `LUMENIS_REQUIRE_ENCODERS=libx264,libx265`, and `tst_movie_export requiredEncoders` fails
  when this build cannot use them (the export tests used to skip quietly).
* A failed export says why in a message as well as in the Console.

To see which encoders your FFmpeg has: `ffmpeg -hide_banner -encoders` in its `bin` folder.

### The first release run failed (2026-11-16)

None of these showed in CI, which runs with the patent codecs on. Release builds run with them off.
* **The writer refused the operating system's encoders.** `MediaWriter` checked the codec (H.264, AAC) against the
  patent-codec policy. That refused `h264_mf`, `h264_videotoolbox` and `aac_at` as well, though the policy allows
  them. A release build could not export H.264. It now uses the same rule as the movie export (`encoderAllowed`).
* **Proxies were refused outright** with the patent codecs off ("H.264 and Avid DNx are not available"). They now
  need only an H.264 encoder that works: FFmpeg's where allowed, else the operating system's.
* **An OS encoder compiled into FFmpeg was taken as present.** Windows Server has no Media Foundation H.264
  encoder. `osEncoderFor` now opens the encoder once with a small picture, and offers it only if that works.
* **Release FFmpeg fails on SVT-AV1** ("no member named 'enable_adaptive_quantization'"), and **ProRes came
  back washed out on macOS** (`tst_media transcodeProRes`). The release FFmpeg was 7.1.1. It does not build with
  Homebrew's SVT-AV1 3.x, and with it the ProRes round trip through VideoToolbox gave the wrong colours, which CI's
  macOS jobs, on Homebrew's FFmpeg 9.0.2, do not. `scripts/build-release-ffmpeg.sh` now builds 9.0.2. Keep the two
  in step: the macOS CI jobs warn when Homebrew's FFmpeg is not the release's.
* **Qt 6.8's `QTRY_COMPARE` fails GCC 13's `-Wconversion`.** The AppImage is the only build with GCC and Qt 6.8,
  and it builds the tests with warnings as errors. `tests/CMakeLists.txt` turns that warning off for the tests there.
* **aqt has no `qtwayland` module for Qt 6.8.3.** Qt 6.8's Linux package has the Wayland plug-ins in it already.
* **`tst_media provider` still treated DNxHR as a patent codec.** DNx is allowed in every build.
* **The AppImage's installer gate ran the AppImage by a relative path** from inside `build/dist` ("No such file or
  directory", exit code 127). It now takes the path with `realpath`. The third run, on FFmpeg 9.0.2, also confirmed the
  ProRes fix: the macOS package's tests passed.

The check: the Linux, macOS and Windows CI jobs run the codec-dependent tests a second time with
`LUMENIS_PATENT_CODECS_OFF=1` ("Test with the release's codec policy"). Run the same tests locally before changing
the codec policy:
`LUMENIS_PATENT_CODECS_OFF=1 ctest --preset ci -R 'tst_media$|tst_managed_media|tst_movie_export|tst_proxy|tst_render_ui|tst_copy_media|tst_send_to|tst_dcp'`.

### ProRes media fails on a Mac: "cannot open the prores_videotoolbox encoder", "Operation not permitted"

Making ProRes media (Transcode, Consolidate, renders, a ProRes Video Resolution) failed on the owner's M1 Mac mini
with "cannot open the prores_videotoolbox encoder: Generic error in an external library" (CI, 2026-11-14). FFmpeg's
VideoToolbox encoders use the hardware encoder only unless their `allow_sw` option is set, and only the Pro, Max and
Ultra chips (and later ones) have a ProRes engine. The writer (`MediaWriter`, `src/media/media.cpp`) now sets
`allow_sw` on every `*_videotoolbox` encoder, so Macs without the engine use Apple's software encoder.

Then every transcode failed with "Operation not permitted". That was not a permission: FFmpeg's MXF muxer returns -1
(EPERM) when it cannot tell the ProRes profile, which it reads from the stream's parameters, and VideoToolbox's encoder
keeps the profile in its own option only (FFmpeg's `prores_ks` sets both). The writer now puts the profile in the
stream itself. And the ProRes encoder is tried once at start-up on a few frames into MXF OP-Atom, as managed media is
written (`encodesHere` in `src/media/managed.cpp`): where VideoToolbox's still fails, FFmpeg's `prores_ks` is used in
builds with `LUMENIS_PATENT_CODECS`, and otherwise ProRes is left out of the resolution menus, with the reason in the
Console ("ProRes: prores_videotoolbox cannot encode here").
`tst_media transcodeProRes` makes ProRes on the macOS CI runners, so it fails if ProRes is offered but cannot be made.

### "FFmpeg 6.0 or later was not found"

CMake needs FFmpeg's development files.

* **Linux:**
  `sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev`.
  Fedora: `sudo dnf install ffmpeg-free-devel`.
* **macOS:** `brew install ffmpeg pkg-config`.
* **Windows:** run `scripts\get-ffmpeg.ps1`. It prints a folder; set
  `FFMPEG_ROOT` to it before configuring.

The CI jobs install it themselves (the bootstrap scripts, and
`get-ffmpeg.ps1` on Windows).

### `tst_fcpxml` "corpus(): read 0, expected 5" or `tst_avb` "!original.isEmpty()"

The interchange corpus was only partly downloaded: `interop_corpus_fetch`
shows "Skipped" because the network dropped while it ran (it happened on two
CI machines at once), and the tests then found empty folders. The corpus used
to live in the build folder, which a CI checkout cleans every run, so it was
downloaded every run. It is now kept in
the user's cache folder (one download per machine), each file is tried four
times, and `scripts/interop-corpus.py fetch` writes `.complete` only once
every file is in; the tests read the corpus only then
(`tests/interop/corpus.h`) and otherwise skip. If a build folder configured
before this still names `build/<preset>/interop-corpus`, delete
`LUMENIS_INTEROP_CORPUS` from its CMakeCache.txt or reconfigure it fresh.

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
`decode_corpus` out; the CI jobs install `ffmpeg` with the bootstrap scripts.

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

The measurement needs Python. For a Python that every account on the PC can
use (a service's own user cannot use one installed in your profile), run from
an administrator PowerShell:

```
winget install --id Python.Python.3.13 --scope machine --log $env:TEMP\python-install.log
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

Seen on the owner's Windows PC as a CI runner (runs 236 and 237), where a re-run of the job alone passed. FFmpeg's
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
segments.first();`; keep the temporary in a variable first. CI's
`linux-asan` job runs the sanitizers on every push so this is caught
there too.

### Windows: "fatal error C1001: Internal compiler error"

MSVC 14.44 (Visual Studio 2022 17.14) crashed compiling `src/media/key_effects.cpp` at the RGB
Keyer's pixel loop. Writing it out plainly was not enough (CI run 219); moving the loop's body into a
function of its own, `rgbKeyPixel`, compiled with `#pragma optimize("", off)` under MSVC only,
avoided it, until an unrelated header change made MSVC rebuild the file and crash a few lines further on,
at the matte blur (CI run 250). All of `rgbKeyerPicture` was then compiled with the optimizer off under MSVC,
and that held until two more header changes (`compositor.h` and the headers it includes) made MSVC crash
there again with the optimizer off (CI runs 317, 318 and 319). All the RGB Keyer's loops, the matte blur among them,
are now in `src/media/key_mattes.cpp`, a translation unit of its own that includes no Qt or compositor headers
and has no lambdas: widely included headers no longer rebuild it, and `key_effects.cpp` only reads the
parameters and calls it, with no `#pragma optimize` left. Keep your Visual Studio Build Tools up to
date (Visual Studio Installer > Update): an internal compiler error may be fixed in a later MSVC. Updating 17.14.41
to 17.14.44 did not help, because it kept the same compiler, cl 14.44.35207 (the job's log prints `VCToolsVersion`).
CI run 320 then crashed at the function's last statement with a list in it, the post-key correction's names,
which is now `rgbKeyCorrection`. With all of `key_effects.cpp` at `/Od /Ob0`, run 321 still crashed, at the
function's last line. So the crash is in code generation, not the optimizer. `rgbKeyerPicture` is now a few calls:
`rgbKeyControls` reads its controls into a plain struct. Under MSVC, `src/media/CMakeLists.txt` compiles the file with
`/Od /Ob0 /Z7` (debug information in the object file, not the shared PDB). Its loops are in `key_mattes.cpp`, which
keeps `/O2`. When the error keeps coming back in one function, split that function into small ones, and move its loops
to a file of their own. If it still comes back, try a newer compiler: Visual Studio 2026's cl 14.5x. An internal compiler error is the
compiler's fault, not the code's: GCC and Clang build the same file cleanly. Simplify the statement
at or just before the line the error names, keep the same result (the tests check it), and leave a
comment saying why, so nobody "tidies" it back. CI's Windows job is the check: it is the
only MSVC build, so a change in `src/media` is not done until that job is green.

### Windows: "fatal error C1128: number of sections exceeded object file format limit"

MSVC's object files hold at most 65,279 sections unless the source is compiled with `/bigobj`; each inline
function and lambda takes some. `src/ui/main_window.cpp` passed the limit with the SubCap effect's code (CI run
343), while GCC and Clang, whose objects have no such limit, built it. `lumenis_set_warnings`
(`cmake/CompilerWarnings.cmake`) now passes `/bigobj` to every target under MSVC, so a large file cannot hit it
again. A new target must call `lumenis_set_warnings`, as every target in the tree does.

### A tool window opens wider than the screen (Tools > Titler+ on Windows 11)

The owner reported that Tools > Titler+ opened a very large window on Windows 11 (2026-11-05). It was the Titler+
Dashboard, whose Font, Transform, Fill, Outline, Shadow and other sections sit in one row: 2700 pixels wide with the
default font, more with Windows' display scaling. Qt never makes a window smaller than its contents' minimum, so the
window took that width. The Dashboard now keeps its sections in a horizontal scroll area and opens at most as wide as
the screen (`TitlerDashboard::placeNear`). Every window the program opens on its own goes through `fitOnScreen` and,
once shown, `keepOnScreen` (frame included): floating panels from Tools, restored workspaces and the Dashboard.
`tst_main_window toolWindowsFitTheScreen` opens every tool panel and Titler+ and fails if one opens off the screen or
larger than it, unless the panel's own minimum needs more. A new panel or tool window with many controls in a row should
scroll, or wrap, rather than set a minimum wider than a laptop screen.

The window still opened as wide as a 1920-pixel screen, and neither it nor the Effect Editor looked like the FX Guide's
pictures: text buttons side by side where the guide has two columns of tool icons, and nine sections in a row (the
owner, 2026-11-06). The cause was the layout itself, not the window: every control the Effect Editor has was also on the
Dashboard, each section a column of labelled rows. The Dashboard is now laid out as pictured on p.302 (docs/spec/titler.md,
"Laid out as the guide pictures them"): about 900 pixels, the Effect Editor's extra controls kept off it. The test also
checks that the Dashboard asks for at most 1280 pixels (890 on Linux; the Font Family box is sized for 16
characters, not the longest installed font name, which widened it on Windows) and that its tools are two columns of icons. To compare a window
with the guide, `LUMENIS_SHOT_DIR=<folder> ctest -R tst_main_window` saves pictures of the Dashboard and the Effect
Editor there.

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

### `tst_accessibility`: "no accessible name: QComboBox ..." on Windows or macOS only

Qt 6.4 (Ubuntu 24.04, the Linux development builds) names a drop-down list after its current item; Qt 6.8 (the Windows
and macOS builds) does not, so a combo box with no name, tooltip or label was announced without one there (CI run 441).
Give the combo box a name with `setAccessibleName`, a tooltip, or a form label whose buddy it is. The test now asks for
one of those on every platform, so Linux catches it too (docs/spec/accessibility.md).

### `failure_messages` fails

A Console line in `src/ui` is neither shown to the user nor marked as information. Error messages everywhere (ROADMAP
9.7): a failure goes through `reportFailure(parent, "Title: text")` (`src/ui/tool_windows.h`), which writes the Console
line and opens a warning that does not block; information (a count, where a file went) stays in the Console with a
`// Console only: <why>` comment on its line or the line before. `scripts/check-failure-messages.py` lists each
file:line it found. The warning's title is also its main text, and the failures are the informative text below it:
macOS ignores message boxes' window titles, so a test that looked for the box by its title found none there (CI on the
Mac runner, 2026-11-11); open warnings are found by their `failureTitle` property.

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
earlier, quiet version once ran for 37 minutes on a Windows CI machine
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
network. Its settings are in `.github/actionlint.yaml`.

actionlint was first a package in `scripts/check-requirements.txt`
(actionlint-py). It has no Windows wheel, and building one on the Windows
runner took 20 minutes, so the install was stopped and every interchange
check was skipped. The check environment holds only the readers now;
tools come as pinned downloads.

### CI: a run stays "queued" with no jobs

Seen on 2026-11-11 (the owner's report): a push's run stayed queued for 50 minutes with no jobs, while the machines
that ran CI then were online and idle; cancelling it gave "409 Cannot cancel a workflow run that has not been
queued yet", and a run started by hand ended in `startup_failure` ("unknown error"). GitHub had an Actions outage
(https://www.githubstatus.com showed it some minutes later). Nothing in the repository causes this: a workflow GitHub
cannot use fails at once (above). When GitHub recovers, start a run by hand (`gh workflow run ci.yml --ref <branch>`, or Run
workflow on the Actions page); the stuck run may stay queued and can be left, but if it is
released later the concurrency group cancels the newer run, so check once more. A $0 Actions budget showing "Usage
blocked" under Billing was a red herring here. One such run (run 423) could not be deleted afterwards either: the
API answers that it cannot be; the Actions page's delete button or GitHub Support can.

### CI: the sanitizer job stops with "No space left on device" while linking

The `asan` preset's Debug build with AddressSanitizer and UBSan takes about 12 GB (each UI test binary carries
the whole application with its debug information), and GitHub's hosted Ubuntu runner has less than that left once
the dependencies are in (the first hosted run, 2026-11-16). The `linux-asan` job therefore first deletes toolchains
it does not use (.NET, Android, GHC, CodeQL ...) and prints the free space before and after. If it fills up again,
add more of the runner's preinstalled folders to that step.

### CI: the macOS job is cancelled at once: "The configuration 'macos-14-arm64-us-default' is not supported"

GitHub retired its macos-14 runner, the only one with Xcode 15 (seen 2026-11-16: the job is cancelled within a second,
with no runner and no log; the message is in the job's annotations). CI and the release workflow now use macos-15
(Xcode 16), the oldest GitHub offers, and macos-26. Built with `CMAKE_OSX_DEPLOYMENT_TARGET` 12.0 (CMakePresets.json),
the package still runs on macOS 12 and later. When GitHub retires macos-15 the same way, move to the next one.

### CI: `otio_python` and `fcpxml_python` fail on macOS with "bad any cast"

OpenTimelineIO 0.18.1 has wheels only up to Python 3.13. On the macos-26 runner the check environment was made with
Python 3.14, so pip built OpenTimelineIO from source, and that build could not read any JSON, not even its own plug-in
manifest: `RuntimeError: bad any cast` from `deserialize_json_from_string` (the first hosted run, 2026-11-16). The
macOS job now sets up Python 3.13 first (`actions/setup-python`) and makes the environment with it. On your own Mac,
make `~/.cutthulhu-venv` with a Python OpenTimelineIO has a wheel for (`brew install python@3.13`, then
`python3.13 scripts/check-python.py`), after deleting the old one.

### Windows: `soundLeavesComAlone` "first == S_OK || first == S_FALSE"

On GitHub's Windows runner the test's main thread is already in COM's multithreaded apartment when the test starts, so
asking for a single-threaded one answers `RPC_E_CHANGED_MODE` (the first hosted run, 2026-11-16). The test now takes a
reference to whichever apartment the thread has, then checks that playback leaves that reference in place.

### Fuzzing: "cannot find .../libclang_rt.fuzzer-x86_64.a"

Clang is installed without its runtime libraries (libFuzzer, the
sanitizers), which Ubuntu packages separately: `sudo apt-get install -y
libclang-rt-dev` (or `libclang-rt-<version>-dev` to match `clang++
--version`). The Fuzz workflow checks this first and says so.

### Fuzzing: "HTTP 404: Not Found (.../actions/workflows/fuzz.yml)"

`gh workflow run` only finds a workflow that is on the default branch
(`main`). Run it from there, or with `--ref <branch>` once `fuzz.yml` is on
`main`.

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
`debug` and `release` presets build anyway. Qt 6.10 declares `QFile::open`
`[[nodiscard]]`, which Qt 6.4 does not: ctest `unchecked_open`
(`scripts/check-unchecked-open.py`) finds a dropped `open()` result on any Qt.

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
* **AS-11 and AS-02 export** (7.2c-d, docs/spec/as11.md, docs/spec/as02.md) run bmx's
  `bmxtranswrap`, built with `mxf2raw` above: copy
  `bmx/build/apps/bmxtranswrap/bmxtranswrap` onto PATH, beside the
  application, or name it in `LUMENIS_BMXTRANSWRAP`. Without it those
  exports say so in the Console; `tst_movie_export as11` skips its rewrap.
* **IMF export** (11.5c2, docs/spec/imf.md) writes the picture itself (JPEG 2000 in
  MXF through FFmpeg) and runs bmx's `raw2bmx` (`-t op1a --audio-layout imf`) for each sound track file,
  built with `mxf2raw` above: copy `bmx/build/apps/raw2bmx/raw2bmx` onto PATH, beside the
  application, or name it in `LUMENIS_RAW2BMX`. Without it a sequence with sound is not
  exported and the message says why; `tst_imf_ui` uses a stand-in script (not on Windows).
* **Transcripts** (8.2, docs/spec/transcripts.md) run whisper.cpp's `whisper-cli`
  (https://github.com/ggml-org/whisper.cpp, MIT; Homebrew's `whisper-cpp`, or built with
  its CMake files) with a ggml model file from the same project (`ggml-base.bin`; a
  `*-tdrz.bin` model also marks speaker turns). Put `whisper-cli` on PATH, beside the
  application, or name it in `LUMENIS_WHISPER`; put the model in the user's application
  data folder as `Whisper/ggml-base.bin`, or name it in `LUMENIS_WHISPER_MODEL`. Without
  them nothing is transcribed; the tests use a stand-in
  engine and a stand-in script. Transcripts are kept in the application data folder's
  `Transcripts` (`LUMENIS_TRANSCRIPTS`; every ctest has its own).
* **DCP export** (7.5, docs/spec/dcp.md) runs asdcplib's `asdcp-wrap`
  (https://github.com/cinecert/asdcplib, BSD licence; Ubuntu has no package).
  Build it with its CMake files (`cmake -S asdcplib -B asdcplib/build`), then
  copy `asdcp-wrap` onto PATH, beside the application, or name it in
  `LUMENIS_ASDCP_WRAP`. Without it the export says so in the Console; the tests
  use a stand-in script instead. The picture also needs FFmpeg built with
  OpenJPEG (`libopenjpeg`, the only encoder that writes X'Y'Z'): Ubuntu's and
  the Windows FFmpeg have it, Homebrew's does not (`brew install openjpeg` and an
  FFmpeg built `--enable-libopenjpeg`). Without it the export says so and
  `tst_dcp writePackage` and `tst_movie_export_ui exportDcp` skip (CI run 347).
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
for the checks") and configures with that Python. If the environment cannot
be made, the step is marked failed with an error annotation and the job goes
on with those checks skipped. A machine with the environment made elsewhere
names it in `CUTTHULHU_CHECK_VENV` (its folder). Where a machine lacks what the
script needs:

* **Linux:** `sudo apt-get install -y python3-venv python3-dev`.
* **Mac:** nothing. With only the Command Line Tools' Python 3.9, pip
  compiles OpenTimelineIO (a few minutes, once); `brew install python`
  avoids that.
* **Windows:** a Python 3 (python.org's installer or `winget install
  Python.Python.3.12`), on PATH.

* **Gate 1, bins and projects:** dump what Cutthulhu reads with
  `cutthulhu --dump-structure <bin or project>` and compare it with a
  reference dump: `python3 scripts/structdiff.py reference.json ours.json`.
  It lists each difference in clip counts, names, timecodes, custom columns
  and sequence structure (docs/spec/structure.md). ctest `structdiff` checks
  the tool itself.
* **The interchange corpus:** AAF files and bins from pyaaf2's and pyavb's
  test suites (MIT), made with Media Composer. ctest `interop_corpus_fetch`
  downloads them at pinned commits, checked by SHA-256, into the user's
  cache folder (`~/.cache/cutthulhu/interop-corpus`, `~/Library/Caches/...`
  on macOS, `%LOCALAPPDATA%\cutthulhu\interop-corpus` on Windows), shared by
  every build folder; set `CUTTHULHU_INTEROP_CORPUS` (or the
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

## Profiling

The benchmarks (docs/spec/performance.md) time the work an editor waits for:

    build/ci/bin/lumenis_bench --list
    build/ci/bin/lumenis_bench                          # all, with their medians
    python3 scripts/perf.py --bench build/ci/bin/lumenis_bench            # compare with this machine's baseline
    python3 scripts/perf.py --bench build/ci/bin/lumenis_bench --record   # record this machine's baseline

`ctest --preset perf` runs the comparison; the other test presets leave it out. To profile one benchmark, run it long
enough to look at with `--only <name> --runs 400`, then:

* **Linux:** `perf record -g -p $(pgrep -x lumenis_bench)` and `perf report`; without perf, a few dozen
  `gdb -p <pid> -batch -ex bt` samples show where the time goes.
* **macOS:** Instruments' Time Profiler, attached to `lumenis_bench`.
* **Windows:** Windows Performance Recorder (CPU usage) and Windows Performance Analyzer, or Visual Studio's
  Performance Profiler.

Build with the `ci` preset (RelWithDebInfo) so the stacks have names.

**When times are noisy** (a shared or virtual machine, where the same build can vary by a third from run to run), compare
two builds by the instructions they run, which do not vary: `valgrind --tool=callgrind --callgrind-out-file=cg.out
build/ci/bin/lumenis_bench --only <name> --runs 1` and read the `summary:` line of `cg.out` for each build. It counts the
whole program, set-up included, so a difference is the change's; it says nothing about memory waits, so a change that
only moves less memory needs timings too (the fastest of several runs).

## Translations

The interface's languages (ROADMAP 10.4; docs/spec/localisation.md; CONTRIBUTING.md, "Translations") are Qt Linguist
files in `translations/`, compiled in when CMake finds Qt's Linguist tools:

* **Linux:** `sudo apt-get install qt6-l10n-tools qt6-tools-dev` (Fedora: `qt6-qttools-devel qt6-linguist`);
  `scripts/bootstrap-linux.sh` installs them.
* **macOS and Windows:** part of the Qt the bootstrap scripts install (aqt's qttools).

Then `cmake --preset ci` again. `python3 scripts/update-translations.py` brings the `.ts` files up to date after a text
changes (ctest `translations_current` checks them); start with `--language de` (or `fr`, `it`, `ga`, `pseudo`) to see a
language without changing the Interface setting.

**CMake says "Qt's Linguist tools were not found: Cutthulhu builds in English only":** install them as above and run
CMake again. The Interface setting then offers only English and the pseudo-locale; `tst_translation` and
`translations_current` check less. Release builds set `-DLUMENIS_REQUIRE_TRANSLATIONS=ON`, which stops instead. On
Ubuntu: `sudo apt-get install qt6-l10n-tools qt6-tools-dev`.

## Soak runs

The long-run stability check (ROADMAP 10.2; docs/spec/soak.md): `--soak <hours>` opens a sample project of its own in
a temporary folder and loops playback, scrubbing, renders, an edit and its undo and the tool windows, logging the
process's memory, handles, threads, windows and bin items and the frames played and dropped, one JSON line a minute.
Renders go to a media drive in that folder, never the computer's own. Then `scripts/soak-check.py` reads the log and
fails when a figure keeps growing after the warm-up:

    build/ci/bin/cutthulhu --soak 72 --soak-log soak-72h.jsonl            # Windows: build\ci\bin\cutthulhu.exe
    python3 scripts/soak-check.py soak-72h.jsonl
    python3 scripts/soak-check.py soak.jsonl --app build/ci/bin/cutthulhu --hours 72   # both in one

For the owner's 72-hour runs, once on each OS: a release build (the package from the builds folder, or the `ci`
preset), the machine left on with sleep off (macOS: `caffeinate -i` before the command; Windows: Power & sleep >
Never), the window may be covered or minimised but not closed. Send the log back; the check prints each figure's growth
per hour and what failed. A run that is stopped early still has a good log up to the last minute. Sanitizer soaks of a
few hours (Linux or macOS; five to fifteen times slower, so fewer cycles):

    ASAN_OPTIONS=detect_leaks=0 build/asan/bin/cutthulhu --soak 3 --soak-log soak-asan.jsonl
    TSAN_OPTIONS="halt_on_error=0:ignore_noninstrumented_modules=1:log_path=tsan" build/tsan/bin/cutthulhu --soak 3 --soak-log soak-tsan.jsonl

AddressSanitizer stops at its first report. ThreadSanitizer goes on and writes its reports to `tsan.<pid>`; read them for
a frame of ours (`lumenis::`) in both stacks. ctest `soak_smoke` runs one cycle of it on every build (up to a quarter hour under the sanitizers).

**The soak says `closedDialog` in its log:** a step opened a dialog nobody answered, and the watchdog closed it at the
next line. Find what opened it (the step number is in docs/spec/soak.md's order) and give the dialog a hook the soak
answers (as `MainWindow::setRenderHook`), or answer it in `Soak::nextStep`.

**`stalled`:** no cycle finished for ten minutes; the run ends with exit 1. Attach a debugger (or take a stack sample,
"Profiling" above) before it ends to see where it waits.

## Releases

A release is made by hand (ROADMAP 10.7, Phase 12.2b): run **Actions > Release > Run workflow** on `main` (or `gh
workflow run release.yml --ref main`). The version is worked out for you, YEAR.N: this year and the next release
number of the year (2026.1, 2026.2 ...; docs/spec/versioning.md); nothing is edited for a release. The workflow (`.github/workflows/release.yml`) runs on GitHub's hosted runners
(macOS's minutes cost ten times: run it when a release is due):

* each package is built with Qt 6.8 LTS and the `dist` or `dist-macos` preset (`LUMENIS_PATENT_CODECS` off,
  docs/spec/release-codecs.md), tested with `ctest`, packaged and checked (installed or mounted, launched with
  `--smoke-test`, no x264 or x265 inside);
* FFmpeg: on macOS and in the AppImage our LGPL build without the encoders awaiting review
  (`scripts/build-release-ffmpeg.sh`, FFmpeg's n9.0.2 tag checked against its commit), on Windows BtbN's LGPL build
  (`scripts/get-ffmpeg.ps1 -Variant lgpl`), in the .deb and .tar.gz the distribution's;
* everything goes into a **draft** GitHub Release (`v<version>`), with each file's checksum, the debug symbols
  (the macOS dSYM, the Linux `.debug`; the Windows PDB is in its packages) and `SHA256SUMS.txt`, made after the
  uploads are checked against their runners' checksums.

Read the draft on the Releases page and publish it; the update check (below) then offers it to everyone on an older
version. Nothing is signed (no certificates; INSTALL.md says how each OS lets the packages run).

If a run fails, its draft stays and takes its number: run the workflow again with that version (for example
`2026.1`) to replace the draft. A version asked for replaces only an unpublished draft. A published release
stops the run, and so does any existing release when the version is left empty.

## Update check

Cutthulhu asks GitHub for its latest release at start-up and from Help > Check for Updates (ROADMAP 10.6;
docs/spec/update-check.md). It only tells: nothing is downloaded. The Interface setting's Check for Updates at Startup
turns the start-up check off, and `LUMENIS_NO_UPDATE_CHECK=1` skips it (ctest sets it for every test). The check goes
over HTTPS through Qt's TLS plug-in (`plugins/tls`). If it says "No secure connection is available", the package lacks
that plug-in: windeployqt and macdeployqt bundle it, and `packaging/linux/make-appimage.sh` copies it and stops when
Qt has none (install Qt's TLS backends, on Debian and Ubuntu part of `libqt6network6`; the OpenSSL one needs the
system's libssl). Until the repository has a release (or while it is private), the check answers "There is no release
to compare with yet".

## Crash reports

A crash writes a report (ROADMAP 10.5; docs/spec/crash-reports.md) into "Crash Reports" in the application's local
data folder (`LUMENIS_CRASH_DIR` sets another), and the next start says where it is. `cutthulhu --crash-reports` lists
the reports not yet shown. The notice's Report on GitHub opens a new issue filled in from the newest report (no memory
map, the home folder written `~`); add what you were doing, attach the `.dmp` on Windows if you are happy to, and
submit it. Or send the `crash-*.txt`, and the `.dmp`, another way.

* **Windows:** the report names functions and lines when `cutthulhu.pdb` is next to the executable (it is in the
  packages). Open the `.dmp` in Visual Studio or WinDbg with the same build's PDB for every thread and variable.
* **Linux:** the backtrace is module + offset (`cutthulhu(+0x1e959a)`).
  `python3 scripts/symbolize-crash.py crash-....txt` gives each frame's function, file and line, using the report's
  memory map and `addr2line` (binutils) on the same build's files. When those are not where the report says, give a
  folder with them, or with the `cutthulhu.debug_<date>_<commit>` the CI keeps beside each AppImage (gunzip it first):
  `--search ~/Documents/Cutthulhu builds`.
* **macOS:** the system's own report (`~/Library/Logs/DiagnosticReports/cutthulhu-*.ips`, also in Console.app under
  Crash Reports) has every thread; the notice names it. It shows lines when the build's dSYM is found: unzip the
  `Cutthulhu-dSYM_<date>_<commit>.zip` kept beside the .dmg somewhere Spotlight indexes, or use
  `atos -o Cutthulhu.dSYM/Contents/Resources/DWARF/Cutthulhu -l <load address> <address>` with the addresses from the
  report.

The sanitizer builds leave crashes to ASan and TSan, whose reports say more; `LUMENIS_NO_CRASH_HANDLER=1` turns ours off
elsewhere (to get a core file without our report, or under a debugger that stops at signals anyway).

## Fuzzing

Phase 3 gate 4 asks that fuzzing the bin and AAF parsers for 24 hours
produces no crashes. `fuzz/` holds seven libFuzzer targets (fuzz/README.md
says what each runs): `cfb` (the compound file container), `aaf` (AAF
objects, the `--dump-aaf` dump and the import into bins), `avb` (bins, the
`--dump-avb` dump and the import), `avb_rewrite` (the bin writer: what is
read is written back and must read back the same), `otio`
(OpenTimelineIO files, imported, exported and imported again) and `fcpxml`
(FCP 7 XML, the same) and `pict` (PICT files as Export Matte PICT writes
them, read, written and read again). A crash, a hang (an
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

**In CI:** the Fuzz workflow (`.github/workflows/fuzz.yml`, run by hand
only) runs on GitHub's hosted Linux runner:

    gh workflow run fuzz.yml -f hours=5 -f jobs=2

It builds the `fuzz` preset, replays the regression inputs, seeds, and runs
every target for its share of the hours (with the default weights, 24 hours
would be 2.7 for `cfb`, 8 each for `aaf` and `avb` and 5.3 for
`avb_rewrite`). A hosted job stops after 6 hours, so one run fuzzes at most
5.5; the corpora it grows are kept in the Actions cache, and the next run
starts from them, so a longer campaign is several runs. It prints a progress
line every 10 minutes and the summary at the end. Every target runs for its
whole share even after a failure (`--keep-going`), and the job fails if
anything failed: the log lists the problems after the summary (`== problems`),
above it each one's stack and smallest reproducer as base64, and the failing
inputs are uploaded as the artifact `fuzz-artifacts`. Reproduce each, fix it,
add it to `fuzz/regressions/`, and run again.

**The 24-hour gate run** passed in October 2026, through this workflow on the
owner's Linux machine, which ran CI then (below); record any later full run's
summary here.

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
(2026-10-22 and 23, on the owner's Linux machine) each ran `cfb` clean for its 2.7
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
"Download log archive"), while GitHub keeps it. The runs made before the
repository went public were deleted with its other old runs (ROADMAP 10.0).

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
* `LUMENIS_SHARED_LUTS`: the folder of external LUTs every project lists
  (ours: the application data's `ColorManagement/LUTs`; each project's own
  are in its `LUTs` folder). docs/spec/color-adapters.md.
