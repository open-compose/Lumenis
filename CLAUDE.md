# Notes for Claude sessions working on this repository

Cutthulhu is a clean-room, cross-platform work-alike of Avid Media Composer
(C++20, Qt 6). Start with `docs/ROADMAP.md` (plan, decisions, phase gates) and
`docs/EXTENSIONS.md` (codecs, plug-ins, licensed technology, module hooks).

## Ground rules (from the project owner)

- **Clean room.** Never use decompiled, disassembled or leaked Avid code or
  restricted SDKs. Use only public Avid documentation, open standards and
  black-box observation (ROADMAP §3). Never copy Avid artwork or icons.
- **Spec before UI.** Menu, window and keyboard details come from the public
  Media Composer 2024/2025 Editing Guide, cited by page in
  `docs/spec/ui-inventory.md`. Never enter them from memory.
  - Editing Guide: https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf
  - FX Guide (effects, titles, colour correction): https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_FX_Guide.pdf
  - Supported video file formats (Phase 2): https://resources.avid.com/SupportFiles/attach/Avid_Supported_Video_File_Formats.pdf (summarised in docs/spec/media-formats.md)
  - Keyboard shortcuts, 2009, third-party (Moviola). A secondary source; mark entries ks2009: https://resources.avid.com/supportfiles/attach/keyboard%20shortcuts.pdf
  - Download it into the scratchpad only. Never commit Avid documents.
- **Licensed technology.** Use an open-source implementation if a legal one
  exists. Otherwise use what the OS or a driver licenses. Otherwise add a
  provider hook so a licensed module can be added later.
- **Dependencies.** Only licences compatible with GPLv3 and commercial
  distribution. Add each new dependency to `THIRD-PARTY-NOTICES.md` and
  install its licence text.
- **Names.** The product name is **Cutthulhu**, set in
  `cmake/Branding.cmake`. The internal code name `lumenis` (targets,
  namespaces, module ABI) stays.
- **Support.** macOS arm64 (Xcode 15 and 26), Windows x64 (MSVC 2022) and
  Linux x64 (Ubuntu 24.04 system Qt 6.4). Release builds use Qt 6.8 LTS.

## Working practice

- Build and test with `cmake --preset ci && cmake --build --preset ci && ctest --preset ci`.
  The `ci` preset turns warnings into errors.
- Also build once with `-DCMAKE_CXX_COMPILER=clang++`, because CI's macOS
  jobs use Clang, and run `scripts/check-apple-conversions.py build/clang`
  (Apple arm64's 64-bit `long double`).
- Actions minutes are limited (private repository; macOS costs 10×). Pushes
  to `claude/**` run Linux and Windows only; macOS runs on `main`, pull
  requests and by hand. Run it by hand (`gh workflow run ci.yml --ref
  <branch>`) once at the end of each phase, and batch fixes into few pushes.
  The owner's M1 Mac runs the macOS job on every push as a self-hosted
  runner (`scripts/setup-mac-runner.sh`, BUILDING.md: hidden user
  `ghrunner`, launchd daemon, no window server, Qt in its home). The
  `macos-local` job runs while the repository variable `MAC_RUNNER` is
  `on`. The hosted macOS jobs (Xcode 15 and 26) still need the by-hand run
  at the end of each phase; `-f hosted_macos=false` skips them. While the
  repository variable `HOSTED_CI` is `off` (minutes used up), only the Mac
  runs: build and test Linux locally (ci preset, clang, GCC 15 check) with
  extra care. The owner's Windows PC can run the Windows job
  (`scripts/setup-windows-runner.ps1`, variable `WINDOWS_RUNNER` = `on`) and
  Ubuntu in its WSL the Linux jobs (`scripts/setup-wsl-runner.sh`, variable
  `LINUX_RUNNERS`, e.g. `["24.04","26.04"]`); BUILDING.md has the steps.
  Currently all three self-hosted runners are on (Linux: 26.04 only) and
  `HOSTED_CI` is off; the screenshot comparison runs on the WSL runner.
  Screenshots travel between jobs on `ci-screenshots/<branch>/<platform>`
  branches (`scripts/ci-screenshots.py`, `.ps1`), not artifacts: the
  artifact quota is used up. The
  .msi install, Ubuntu 24.04 and Xcode 15 wait until minutes reset. Each
  self-hosted job keeps its package (.dmg, .msi, AppImage) in a builds
  folder linked from the owner's Documents (`scripts/keep-build.sh`; the
  setup scripts' `builds` command, BUILDING.md).
- Push only after a local build and test pass, then check CI. CI covers every
  platform, also builds on the newest Ubuntu (26.04: GCC 15, Qt 6.10) with
  warnings as errors, and checks:
  - the installers (install, launch, uninstall), and that the AppImage
    opens natively on a headless Wayland compositor;
  - that PowerShell scripts parse;
  - cross-platform screenshot consistency (`ui-consistency`).
- Comment the code for contributors (the project will be public; the map
  is docs/ARCHITECTURE.md): each file starts with what it implements and
  how it fits; public functions and members say their contract; non-obvious
  code says why. Keep guide page citations; never invent one.
- Everything the user can invoke is a command declared in
  `src/ui/resources/commands.json` and placed in `menus.json`.
  `tests/data/menu_inventory.json` is the spec the menu bar is tested against.
- Qt 6.4's moc cannot parse GCC 13's C++20 headers when given compiler
  predefines. The top-level CMakeLists turns them off for Qt < 6.5.
- The owner tests on real Mac and Windows machines and reports problems.
  Fix the root cause, add a troubleshooting entry to `BUILDING.md`, and add a
  check to the scripts or CI so it can't recur silently.

## Current state

See the status lines in `docs/ROADMAP.md`. Phase 0 is done, except the manual
Linux check. Phase 1.1 (the UI framework) is done. The menu inventory and the window
specs for Select Project, panels, workspaces and the Command Palette are
entered in `docs/spec/ui-inventory.md`, with page citations. Phase 1.2a, the Select Project window (`src/ui/select_project_dialog.*`, `src/model/`), is
done. Phase 1.2b, the paneled main layout and workspaces (`src/ui/panels.*`,
`panel_manager.cpp`, `workspace*`, `main_window.*`), is done. Phase 1.3a,
the Bin Container (`src/model/bin.*`, `project_bins.*`, `src/ui/bin_*`), is
done. Phase 1.3b, the bin views, columns, sorting and sifting
(`bin_view.*`, `bin_dialogs.*`), is done. Phase 1.3c, bin docking
(`bin_dock.*`), the Project Bin Container and Favorite Bins, is done. Phase 1.4a, the Composer
(`src/model/monitor.*`, `src/ui/composer.*`), is done. Phase 1.4b, the
Timeline and editing (`src/model/sequence.*`, `src/ui/timeline.*`), is
done. Phase 1.4c, segment editing, locks and the Clipboard, is done.
Phase 1.4d, Trim mode and markers (`src/model/marker.*`,
`src/ui/marker_dialogs.*`), is done. Phase 1.5a, Settings
(`src/model/settings.*`, `src/ui/settings*`, options in
`src/ui/resources/settings.json`), is done. Phase 1.5b, the Command
Palette and button mapping (`command_palette.*`, `button_mapping.*`,
`resources/command_palette.json`), the Tool palette, Calculator
(`src/model/calculator.*`) and Console (`tool_windows.*`), is done. Phase
1.6, the Keyboard setting (`keyboard_palette.*`), workspaces as User
settings and the workspace buttons, is done. Phase 1.7 is done:
  - the Effect Palette and Effect Editor (`effect_palette.*`,
    `effect_editor.*`, `resources/effects.json`,
    `resources/effect_parameters.json`, with effects in `sequence.*`);
  - the Audio Mixer and Audio Tool (`audio_tools.*`);
  - the Media Tool (`media_tool.*`);
  - Help > About's Configuration, Usage and Hardware (`tool_windows.*`).

Gate 5's checklist is ready: `docs/checklists/phase1-gate5.md`.
  - It is made by `scripts/make-checklist.py` from
    `--dump-menus --sample-project`.
  - Rerun the script after changing menus or commands; ctest
    `checklist_current` fails otherwise.
  - Phase 1 waits on the editor's session (ROADMAP gate status). Gate 1
    passes on the self-hosted runners; `fixedPixelMetric` in `theme.cpp`
    keeps macOS sizes the same as Linux.

Phase 2 (media I/O and playback) has started. 2.1, the media library, is
done:
  - `src/media`: FFmpeg probe, frame-exact video and audio readers, and a
    writer; FFmpeg as a codec provider; `--probe`.
  - FFmpeg comes from the platform for development: the bootstrap scripts,
    `cmake/FindFFmpeg.cmake`, `scripts/get-ffmpeg.ps1` on Windows.
  - `LUMENIS_PATENT_CODECS` is on for dev and test builds, off for official
    releases (owner's decision).

2.2, linking, is done:
  - the Source Browser (`source_browser.*`);
  - linked clips and image sequences (`src/media/link.*`,
    `ImageSequence` in `media.h`);
  - frames in the monitors, bins and Timeline (`media_frames.*`);
  - Relink > Linked Media and Unlink (`relink_dialog.*`);
  - the Link setting.
2.3a, playback, is done:
  - sound through miniaudio (`third_party/miniaudio`, its own target without
    our warning flags) from `src/media/playback.*`;
  - the audio clock paces the Composer; frames are decoded ahead
    (`MediaFrames::prefetch`);
  - the Video Quality Menu button.
2.3b is done: smooth and digital audio scrub, Full Screen Playback
(`full_screen.*`), the Audio, Full Screen Playback and Video Display
settings, dropped-frame counts (`Composer::playStats`).
2.4a, managed media, is done:
  - MXF OP-Atom media in `Avid MediaFiles/MXF/<n>/` and the media database
    (`src/media/managed.*`);
  - Consolidate/Transcode (`src/media/consolidate.*`,
    `consolidate_dialog.*`) and Import through the Source Browser;
  - the Media Creation and Import settings. Test drives come from
    `media::setExtraMediaDrives` or `LUMENIS_MEDIA_DRIVES`.
2.4b is done:
  - Relink > Managed Media (`src/media/relink.*`, `ManagedRelinkDialog` in
    `relink_dialog.*`);
  - the Media Tool's media files and Delete Media (`media_tool.*`);
  - the Background Queue (`background_queue.*`).
2.5, the Phase 2 gates, is automated where it can be (ROADMAP gate
status, BUILDING.md "Phase 2 gates"):
  - `decode_corpus` (ctest, `scripts/decode-corpus.py`, `--framemd5`);
  - `transcodeRoundTrips` in `tst_media` (PSNR, timecode, tape);
  - `--measure-playback`, `scripts/measure-playback.py` and
    `scripts/check-mxf.py` for the runs done by hand.
Gates 3 and 4 (playback) pass on the owner's Windows PC, M1 Mac mini and
Ubuntu 26.04 in WSL, the reference Linux. Gate 2's bmx half passes
(`scripts/check-mxf.py --make`, `cutthulhu --write-media`, ctest
`mxf_check` with mxf2raw installed); `MediaWriter` relabels FFmpeg's
OP-Atom essence as clip-wrapped. Media Composer linking (M) is deferred
until the owner has the licensed seat again; Phase 3 goes ahead meanwhile
(ROADMAP gate status).

Phase 3 (projects, bins and interchange) has started. 3.1, the native
model, is done (docs/spec/native-model.md):
  - MobIDs (`src/core/mob_id.*`, `BinItem::mobId`, `Segment::sourceMob`);
    bins are version 2.
  - Source chains (`BinItem::sources`), subclips (`model::subclipItem`,
    `media::makeSubclip`), and package UMIDs in managed MXF
    (`fixOpAtom` in `media.cpp`).
  - Structure dumps (`--dump-structure`, `src/model/structure.*`) and
    `scripts/structdiff.py` (docs/spec/structure.md).
3.2, AAF (docs/spec/aaf.md), has started. 3.2a, the compound file
container (`src/interop/cfb.*`, target `lumenis_interop`), is done; the
corpus comes from `scripts/interop-corpus.py` (pyaaf2's and pyavb's test
files, MIT). 3.2b, AAF's object layer (`src/interop/aaf*`, the built-in
model `aaf_model.inc` from `scripts/make-aaf-model.py`), is done; `--dump-aaf`
and `scripts/aaf-dump.py --compare` check it against pyaaf2. 3.2c, AAF
import into bins (`aaf_import.*`, `--import-aaf`), is done; ctest
`aaf_import_bins` compares it with Media Composer's bins of the same
material (`tests/data/aaf_reference`, from `scripts/avb-structure.py`).
3.2d, AAF export (`aaf_writer.*` builds any AAF objects, `aaf_export.*`
lays out bins as Media Composer does, `--export-aaf`), is done; ctest
`aaf_round_trip` checks it. Gate 3's Media Composer halves are deferred.
3.2e is done: AAF Import through the Source Browser (`MainWindow::importAaf`;
clips find managed media by MobID) and File > Output > Export to File
(`export_dialog.*`, the Export setting). Next is 3.3, `.avb` bin read.

Spec sections 3, 4, 7, 11, 12, 13, 14, 15 and 17 list details left for later.

Panels are our own framework, not QDockWidget, because Qt cannot draw
vertical title bars, green drop zones or Alt-drag tabbing. Custom-painted
widgets draw text with palette colours (`WindowText`, `BrightText`) so the
text-free screenshots hide it.

The application icon is a ray-marched scene, `packaging/icon/scene.frag`.
`scripts/render-icon.js` renders it and `scripts/make-icons.py` makes every
platform's icon and the About box picture from the render; see `BUILDING.md`.

To read the guide, download it into the scratchpad and run `pdftotext`. To see
a page's layout, render it with `pdftoppm -r 80 -f N -l N`.
