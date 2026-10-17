# Cutthulhu — Project Plan and Roadmap

Status: §2 decisions mostly settled (see each item). Phase 0 is implemented; its CI gates run on every push.

## 1. Goal and honest scope

Cutthulhu is a clean-room, cross-platform non-linear editor that works like Avid
Media Composer. Someone who knows Media Composer should be able to sit down and
work without help: the same windows, the same edit model (Source/Record, segment
and trim modes, sync locks), the same default keyboard, and existing projects,
bins and media should load and save.

"Complete in all respects" is the long-term target. Some parts of the Avid
ecosystem cannot be cloned without Avid's cooperation, and we say so up front
so that nobody is surprised later:

| Area | Can we match it? | Why / plan |
|---|---|---|
| UI, menus, commands, keyboard map, edit behaviour | Yes | Learned from public manuals and black-box observation. Menu structures and command sets are "methods of operation" (*Lotus v. Borland*). **Icons and artwork are redrawn**, never copied. |
| MXF OP-Atom / OP1a media, DNxHD/DNxHR | Yes | SMPTE standards (377M, 390M, VC-3/2019). |
| AAF interchange | Yes | Public AAF object spec. |
| Bins (`.avb`), projects (`.avp`), settings (`.avs`) | Yes, with effort | Undocumented. We use clean-room black-box analysis plus MIT-licensed prior art (pyavb). |
| Avid MediaFiles database (`msmMMOB.mdb`, `msmFMID.pmr`) | Not needed | Media Composer rebuilds these from the MXF folder, so we only have to write correct MXF. |
| OFX effect plug-ins (Boris FX, etc.) | Yes | OpenFX is an open, BSD-licensed API. Media Composer supports it too. |
| AVX plug-ins | **Only with Avid partnership** | The SDK is under an Avid licence. |
| AAX audio plug-ins | **Only with Avid partnership** | The SDK is under an Avid licence. We offer CLAP (and possibly VST3) as an alternative. |
| NEXIS / MediaCentral / Avid Link / licensing services | **No** (initially) | These are proprietary server protocols. We support shared-storage bin locking on ordinary NAS instead. |
| ScriptSync / PhraseFind | Equivalent feature | Avid licenses this technology from Nexidia. We rebuild it on open speech-to-text (e.g. whisper.cpp, MIT). |
| Avid / Avid-certified I/O hardware | Partial | AJA, Blackmagic and similar devices through their own SDKs, subject to their licences. |

Sizing: Media Composer represents decades of engineering. A daily-usable editor
for the core workflow (Phases 0–6) is a multi-year effort for a small team. Full
parity is longer. The phased plan below is built so that every phase ends with
something usable and testable.

## 2. Decisions needed now

### 2.1 Language and GUI toolkit — **decided: C++20 + Qt 6 (Widgets)**

The brief asks for "native GUI components" *and* for windows that "look the same
across platforms". Pure native toolkits (AppKit / Win32 or WinUI / GTK) conflict
with the second requirement. They would mean three separate UIs that look
different, at roughly three times the cost.

Qt 6 compiles to a native binary on each OS and uses each platform's own
windowing system, menu bar (the global menu bar on macOS), file dialogs, IME,
accessibility, HiDPI and GPU APIs (Metal, D3D11/12, Vulkan through QRhi). Its
widgets can be styled to look identical everywhere, which matches the look of
Media Composer itself. Media Composer ships with Qt libraries, so users already
know that look and feel.

Qt is LGPLv3, which is compatible with our licence when Qt is linked
dynamically.

Why C++ rather than Rust: nearly every dependency we need is C or C++. That
includes Qt, the OpenFX host API, OpenColorIO, OpenImageIO, FFmpeg, bmx (MXF)
and OpenTimelineIO. Rust–Qt bindings are immature for an application this size.
Isolated components may still be written in Rust behind a C ABI where that makes
sense, such as parsers for untrusted files.

### 2.2 Licence — **open**: GPLv3 stays; module exception / CLA still to decide

The repo is currently **GPLv3**. That works for "free build plus paid support"
(the Red Hat model): anyone can sell binaries and support. Things to know:

* Every dependency must be GPLv3-compatible. MIT, BSD, Apache-2.0, LGPL and
  MPL-2.0 are all fine. Avid's AAF SDK licence is doubtful, so we would write
  our own AAF layer, ported from or modelled on the MIT-licensed pyaaf2.
* If you ever want a closed "enterprise" edition, you need either full copyright
  ownership through a Contributor Licence Agreement (CLA) from day one, or a
  different licence. Decide this **before** outside contributions arrive.

### 2.3 Reference Media Composer seat — **settled**

A contact with a licensed seat will check the files we produce once we have
any to check.


Pass/fail interop tests need a legally licensed copy of Media Composer to
produce reference bins, AAFs and media, and to confirm that our output opens in
it. The person who runs it does black-box observation only: no disassembly and
no debugger. Check that the EULA allows this.

### 2.4 Target version — **decided**

Proposal: match the **Media Composer 2024.x/2025.x** UI and default keyboard,
and read bins from versions 8.x and later.

### 2.5 Name — **decided: Cutthulhu**

The owner searched the USPTO and found no conflicting mark. The closest is
"Cutethulhu", a plush toy in a different class. `cmake/Branding.cmake` is the
only place that sets user-visible names. The internal code name `lumenis`
stays in targets, namespaces and the module ABI.

### 2.6 Licensed and patented technology — **decided**

* Use a legal open-source implementation where one exists.
* Otherwise call what the OS or a driver already licenses.
* Otherwise define an extension hook now, so the support can be added as a
  module later.

[EXTENSIONS.md](EXTENSIONS.md) has the capability-by-capability plan and the
hook design.

## 3. Clean-room and IP rules

1. No decompiled, disassembled or leaked Avid code, binaries or SDK headers
   under restrictive licences may enter the repo or be consulted.
2. Allowed sources: Avid public manuals, help, marketing, training videos,
   published SMPTE/AMWA specs, and black-box observation of files that a
   licensed Media Composer produces.
3. Undocumented formats (`.avb`, `.avp`, `.avs`) are reverse-engineered into a
   written spec in `docs/spec/`. Code is written from that spec, and each spec
   entry records where the information came from.
4. MIT, BSD and Apache open-source prior art (pyavb, pyaaf2, OTIO) may be used
   and credited.
5. Patents:
   * Core NLE patents from the 1990s and early 2000s have expired.
   * Before implementing a newer feature, run a patent search for it (e.g.
     PhraseFind, Dynamic Media Folders, bin locking schemes).
   * Codec patents are the real risk. H.264, HEVC and AAC go through OS
     frameworks: VideoToolbox, Media Foundation, and VA-API or the user's own
     FFmpeg on Linux. Otherwise a licensed module plugs into the codec hook.
     See [EXTENSIONS.md](EXTENSIONS.md).
   * ProRes encode/decode needs a review by legal counsel.
   * DNxHD/VC-3 patent status needs a review by legal counsel.
6. Avid trademarks are used only descriptively ("opens Avid Media Composer
   bins"), never as branding.

## 4. Architecture

```
app/                 executable, startup, crash reporting
src/core/            rational time, timecode, IDs (MOB/UMID), undo, settings, threading
src/ext/             provider registry, module loader/ABI, extension interfaces (codec, ASR, plug-in hosts)
src/model/           project, bins, clips, master/sub/group clips, sequences, tracks, effects graph
src/media/           demux/decode/encode (FFmpeg LGPL build, bmx), media management, MXF writer
src/playback/        frame scheduler, caches, A/V clock, GPU display (QRhi), audio out (miniaudio/RtAudio)
src/render/          effects pipeline, OFX host, OCIO colour management
src/audio/           mixer, clip/track gain, EQ, CLAP host
src/interop/         aaf/, avb/, avp/, edl/, otio/, xml/, ale/
src/ui/              windows, palettes, keyboard mapping, workspaces, theming
tests/               unit, golden-image, interop corpus, scripted edit scenarios
docs/spec/           clean-room format specs
packaging/           macos/ (DMG, notarisation), windows/ (WiX MSI), linux/ (AppImage, deb, rpm, Flatpak)
scripts/             build.sh, build.ps1, bootstrap, test-corpus fetch
```

* Build: CMake with presets. vcpkg in manifest mode arrives with the first
  third-party library (Phase 2).
* CI: GitHub Actions on `macos-14` (arm64), `windows-2022` (x64) and
  `ubuntu-24.04` (x64).
* Every edit operation is a serialisable command. That one design gives us
  undo/redo, scripted tests and, later, a scripting/automation API.

## 5. Phases and pass/fail gates

Every gate is automated in CI unless it is marked **(M)** for manual. A phase is
done only when all of its gates pass on **all three platforms**.

### Phase 0 — Foundations (repo, build, CI, packaging) — **implemented**

Delivered:
* CMake presets and the Qt 6 app.
* Qt Test unit tests.
* The provider registry and module loader: the hooks.
* Rational time and SMPTE timecode.
* `scripts/build.sh` and `scripts/build.ps1`, plus bootstrap scripts.
* [BUILDING.md](../BUILDING.md) and [INSTALL.md](../INSTALL.md).
* Installers: DMG, MSI/ZIP, deb/rpm/tgz and AppImage.
* A CI workflow that runs gates 1–3 on all three platforms.

Gate 4 is a person following the docs on a real machine:

| Platform | Built from BUILDING.md | Installer tested by hand |
|---|---|---|
| macOS (Xcode 26) | ✅ 2026-10-12 | ✅ DMG installed and ran |
| Windows x64 | ✅ 2026-10-12 | ✅ MSI installed, ran and uninstalled |
| Linux x64 | pending | pending |

The macOS walk-through found three issues, all fixed:
* missing standard headers from a damaged Xcode install, which `build.sh` now
  detects;
* the AGL link failure with Xcode 26;
* a wrong output path in BUILDING.md.

The Windows walk-through found two issues, both fixed:
* the bootstrap script could not find the Python it had just installed;
* `cpack` could not find WiX 3 (the bootstrap script now installs WiX 3.14
  itself, and packaging falls back to `.zip` when WiX is missing).

Original plan:
* Set up the CMake/vcpkg skeleton, the Qt 6 app that opens an empty main window,
  and the unit-test framework (Catch2 or GoogleTest).
* Add `scripts/build.sh` (macOS and Linux) and `scripts/build.ps1` (Windows).
* Write `BUILDING.md` and `INSTALL.md` for each platform.
* Build installers with CPack: DMG for macOS arm64, MSI for Windows x64,
  AppImage plus .deb/.rpm for Linux x64.

**Gates**
1. CI builds all 3 targets from a clean checkout with no warnings-as-errors failures.
2. Unit tests pass on all 3.
3. The installer for each OS installs, launches to the main window, and
   uninstalls. This runs headless in CI using `QT_QPA_PLATFORM=offscreen` and
   `--smoke-test`.
4. The documented build steps work on a fresh VM for each OS **(M)**.

### Phase 1 — UI shell: every window, no media — **in progress**

Sub-steps:
* **1.1 Framework — done.**
  * a command registry: every menu item and shortcut is a command with a
    stable id, and commands without a handler show disabled;
  * menus built from `resources/menus.json`;
  * a keyboard map with user overrides;
  * named workspaces;
  * the Fusion style with the bundled Inter font for identical rendering;
  * `--dump-menus` and `--screenshot` test hooks;
  * `tst_menu_inventory` (gate 2);
  * a CI job comparing screenshots across platforms (gate 1).
* **1.2a Select Project — done.**
  * Projects and New Project pages, What's New, user profiles, and
    Private/Shared/External locations with Browse.
  * Text and frame views, a column fast menu, and Delete with confirmation.
  * Format presets for all 47 formats in guide ch. 35, plus custom raster
    rules, aspect, colour space, colour depth, stereoscopic, film and
    Choose for Me.
  * Startup flow and File > Close Project (Ctrl+Shift+W).
  * Not yet built: Save/Manage Presets, poster frames, and the fast menu's
    font option.
* **1.2b Paneled main layout — done.**
  * Our own panel framework (`src/ui/panels.*`, `panel_manager.cpp`), not
    Qt's dock widgets: vertical title bars with tabs, a tab menu and close
    buttons; green drop zones with a white preview; Alt-drag to tab;
    floating windows that can hold panels themselves.
  * The default layout of p.74, with placeholders for the Bin Container,
    Composer and Timeline until 1.3 and 1.4.
  * Windows > Float All Panels, Float Active Panel and Add Blank Panel;
    File > Close; Tools > Timeline; Ctrl+Up/Down and Ctrl+Tab.
  * Workspaces Edit, Color, Effects and Audio, up to 12 in all: New, Delete,
    Restore to Saved, Restore Current to Default and Save Current; the
    workspace toolbar and its hidden-bar button; last known or last saved
    state.
  * Not yet built: see "Implementation status (1.2b)" in the spec.
* **1.3a Bin Container and bins — done.**
  * Bins as files and the project's Sidebar tree with folders, Trash and
    Other Bins.
  * The Sidebar (filter, Flat View, rename, drag into folders), bin tabs,
    and the Text view.
  * New Sequence, Duplicate and Delete; saving and auto-save.
  * Details and what is left: "Implementation status (1.3a)" in the spec.
* **1.3b Bin views, columns, sorting and sifting — done.**
  * Text, Frame and Script views; the Bin View menu with its built-in views
    and Save As.
  * Choose Columns, moving, hiding, aligning and custom columns.
  * Sorting (double-click, multilevel Sort, Sort Again) and sifting (the
    Custom Sift dialog, Sift Selected Items, Show Sifted/Unsifted).
  * Frame sizes, Align and Fill, Snap to Grid; the Script view's text;
    clip and background colours; locks.
  * Details and what is left: "Implementation status (1.3b)" in the spec.
* **1.3c Bin docking, Project Bin Container, Favorite Bins — done.**
  * Bins docked side by side with green drop zones, and floating Bin
    Containers.
  * Items dragged between bins and onto Sidebar bins; Sidebar bins dragged
    to the bins.
  * Tools > Project; Favorite Bins; Set Bin Display; filtering on selected
    columns; Tab and Enter between cells; timecode range sifts.
  * Details and what is left: "Implementation status (1.3c)" in the spec.
* **1.4a Composer — done.**
  * Source and Record monitors with Clip Name menus, tracking displays,
    the center duration, position bars and two button rows.
  * Loading from bins, J-K-L and the other play, step and mark commands,
    Gang and Show Single/Dual Monitor; the position moves in real time
    until there is media (Phase 2).
  * Details and what is left: "Implementation status (1.4a)" in the spec.
* **1.4b Timeline — done.**
  * Sequences with video, audio and data tracks, stored in their bins.
  * Splice-in, Overwrite (three-point and single-mark editing), Lift and
    Extract, and 100 levels of Undo/Redo with the Undo/Redo List.
  * The Timeline panel: the Track Selector with patching and monitor
    buttons, the ruler and TC1, segments with clip text, IN to OUT
    highlighting, the position indicator with snapping, Focus, the scale
    bar, Zoom In/Back, Toggle Source/Record and the Fast menu.
  * Details and what is left: "Implementation status (1.4b)" in the spec.
* **1.4c Segments, locks and the Clipboard — done.**
  * The Lift/Overwrite and Extract/Splice-in segment tools: select, lasso,
    drag, copy, delete, Move Clip Up/Down, Cut, Copy and Paste.
  * Track Lock and Sync Lock; the Clipboard; Replace Edit, Top, Tail, Add
    Edit and Remove Match Frame Edits, Match Frame; deleting tracks; the
    Timeline Top toolbar and right-click menus.
  * Details and what is left: "Implementation status (1.4c)" in the spec.
* **1.4d Trim mode and markers — done.**
  * Overwrite, ripple and dual-roller trims with the trim tools, Trim
    mode, rollers, the U/A/S keys, sync locks, slip, slide and Extend; the
    Big Trim display with counters and Trim buttons.
  * Markers on clips and sequences, spanned markers, the Edit Marker
    dialog, Go to Previous/Next Marker, Mark Markers and the Markers
    window.
  * Details and what is left: "Implementation status (1.4d)" in the spec.
* **1.4e** Still to come for editing: Sync Point editing, playback-driven
  tools (Play Loop, Edit Review, J-K-L trimming) once media plays, and
  the remaining Timeline and marker details listed in the spec.
* **1.5a Settings — done.**
  * The Settings window (Format, Project, User and Site tabs), versions,
    Duplicate, naming, Delete, Restore to Default, dragging between
    lists, the Site Settings window and user profiles.
  * The Bin, Composer, General, Interface, Timeline and Trim settings
    dialogs, wired to the features built so far.
  * Details and what is left: "Implementation status (1.5a)" in the spec.
* **1.5b Palettes and tools — done.**
  * The Command Palette, with Quick Find and button mapping (Button to
    Button, Active Palette, Menu to Button) onto the Composer and the Tool
    palette, saved in the settings.
  * The Tool palette, the Calculator and the Console, as floating panels.
  * Details and what is left: "Implementation status (1.5b)" in the spec.
* **1.6 Keyboard settings, workspaces and button mapping — done.**
  * The Keyboard setting and palette: keys mapped from the Command
    Palette, the modifier key buttons, and the Shift layer; saved per user.
  * Workspaces are User settings (Workspace View), restored after a restart
    (gate 4), with Link to Named settings.
  * Workspace buttons W1–W12.
  * Gate 3 (the default keys) is `tst_keyboard`; gate 4 is
    `tst_keyboard_workspaces`.
  * Details and what is left: "Implementation status (1.6)" in the spec.
* **1.7** *Done.* The windows still missing from the list below.
  * The Effect Palette and Effect Editor. Effects can be applied to segments
    and transitions in the Timeline, removed and adjusted, and are saved
    with the sequence (1.7a).
  * The Audio Mixer and Audio Tool. Clip gain and pan are set per segment
    (1.7b).
  * The Media Tool, and Help > About's Configuration, Usage and Hardware
    (1.7c).
  * The 2025 guide has no Project window tabs: bins and volumes are in the
    Bin Container sidebar, the format is in the Settings window, and usage
    and hardware are in Help > About (spec section 14).
  * Details and what is left: "Implementation status (1.7a)" and "(1.7b and
    1.7c)" in the spec.
* **Gate status.** Phase 1 is done when all five pass on all three platforms.

  | Gate | How | Status |
  |---|---|---|
  | 1. Screenshots match across platforms | `ui-consistency` CI job | Passes on the self-hosted Mac, Windows and Linux runners (2026-10-16); the hosted jobs wait for Actions minutes |
  | 2. Every menu entry present | `tst_menu_inventory` | Passes |
  | 3. Default keyboard | `tst_keyboard` | Passes |
  | 4. Workspaces survive a restart | `tst_keyboard_workspaces` | Passes |
  | 5. An editor finds every window and command **(M)** | [checklist](checklists/phase1-gate5.md) | Checklist ready; waiting for a session with an editor |

  * The gate 5 checklist is made from the real menu bar with a project open
    (`--dump-menus --sample-project`) by `scripts/make-checklist.py`.
  * It lists the 19 windows and every menu command, with its page and
    whether it works yet.
  * `ctest checklist_current` fails when the checklist no longer matches the
    menu bar.

The detailed inventory lives in [spec/ui-inventory.md](spec/ui-inventory.md).
It is entered from the Media Composer Editing Guide with page citations.

These windows are built using the Media Composer User Guide layout:
* Select Project dialog.
* The project's bins (Bin Container sidebar), settings (Settings window) and
  the About window's Configuration, Usage and Hardware. The 2025 guide has
  no Project window tabs.
* Bin views: Frame, Script, Text/Column and the bin map.
* Composer (Source/Record, single/dual monitor) and the Timeline (tracks,
  patching, track selectors, smart tools).
* Tool Palette, Command Palette, Effect Palette and Effect Editor.
* Audio Mixer and Audio Tool.
* Media Tool, Settings dialogs, Keyboard settings, workspaces and the
  button-to-button mapping.
* Menus (all items present, unimplemented items disabled), and the default
  keyboard map.

**Gates**
1. Golden screenshot tests compare every window at 1× and 2× scale. The pixel
   difference between platforms must be ≤ 0.5%, excluding OS text antialiasing
   masks.
   * How it's implemented: each window is also captured with its text drawn
     transparent (`*.notext.png`), and those images must match.
   * For diagnosis, CI also logs the full images' differences and a
     widget-by-widget layout comparison (`scripts/layoutdiff.py`).
2. A window/menu inventory, generated from the user guide, has 100% of entries
   present. A test walks the menu tree and diffs it against
   `tests/data/menu_inventory.json`.
3. Every key in the default keyboard map matches the published Media Composer
   default. This is a table-driven test.
4. Workspaces and window layouts save, restore and survive a restart.
5. An experienced Media Composer editor finds every window and menu command
   without help **(M)**, using a checklist.

### Phase 2 — Media I/O and playback — **in progress**

Sub-steps:
* **2.1 Media library — done.**
  * `src/media`, on FFmpeg 6.0 or later. FFmpeg is the platform's for
    development builds: distribution packages, Homebrew, or an LGPL shared
    build on Windows (`scripts/get-ffmpeg.ps1`).
  * `probe()` reports the container, streams, codecs, profiles, sizes,
    rates, aspect, lengths, start timecode and reel.
  * Frame-exact `VideoReader` (seeks to the keyframe before and decodes
    forward) and sample-exact `AudioReader` (float).
  * `MediaWriter` for any FFmpeg muxer and encoder: QuickTime, MXF, MXF
    OP-Atom, WAV; DNxHD/DNxHR, ProRes, MPEG-2, FFV1, PCM. Colour space is
    declared consistently (Rec. 709 for HD and DNx).
  * FFmpeg is registered as a codec provider; `--probe <file>`.
  * `LUMENIS_PATENT_CODECS`: development builds include the codecs awaiting
    counsel; official releases leave them out (EXTENSIONS.md).
  * `tst_media` writes its own media and reads it back, with lossless,
    Long GOP, 24-bit audio and DNxHR in MXF OP-Atom.
* **2.2a Linking — done.** File > Input > Source Browser and Link, per
  Editing Guide ch. 11 (spec §15).
  * The Source Browser panel: navigation, Explore, Favorites and Recent,
    Text and Frame views with hover preview, Media Folder View, Target Bin,
    Link, Add Volume Bin, and its settings.
  * Linked master clips and audio clips (`media/link.h`) with their real
    metadata in the bin columns; the link is kept in the bin file.
  * Frames in the monitors (sequences too), Frame view and Script view;
    "Media Offline" when a file is gone. A frame cache shared between
    threads; thumbnails decode off the UI thread.
  * Files dragged to a bin link; dragged to the Source monitor they load
    without a bin and are committed when cut in.
  * The Link setting; `tst_source_browser`.
* **2.2b — done.** Clip Frames in the Timeline (p.592); Clip > Modify >
  Unlink Media and Clip > Relink > Linked Media with Locate Media
  (p.364–368); image sequences (DPX, EXR, TIFF, PNG, ...) as one clip,
  grouped as p.325–326 describe (spec §15).
* **2.3 Playback:**
  * A decode thread and frame cache, and an audio clock with audio output.
  * J-K-L at real speeds.
  * Playing sequences through their segments, with full and draft quality.
  * The client monitor.
* **2.4 Avid MediaFiles:**
  * Import, Consolidate and Transcode into `Avid MediaFiles/MXF/<n>/` as MXF
    OP-Atom (DNx and PCM).
  * The media database scanned when a project opens, and relinking.
* **2.5 The gates below:**
  * The decode corpus with frame MD5s.
  * Real-time playback.
  * A/V sync.
  * Transcode round trips.

* Linking (AMA-style) and Import/Consolidate/Transcode into an Avid MediaFiles
  layout: `Avid MediaFiles/MXF/<n>/`.
* MXF OP-Atom and OP1a read/write. Codecs: DNxHD, DNxHR, DNxUncompressed, XDCAM,
  AVC-Intra, ProRes (pending legal review), H.264/HEVC through OS codecs, and
  PCM/WAV/AIFF audio.
* Playback engine: frame cache, J-K-L shuttle, A/V sync, full/draft quality,
  and a client monitor window.

**Gates**
1. A decode corpus of about 150 files covering every listed codec, frame rate
   and raster. Frame MD5s must match reference decodes.
2. MXF we write passes the MXF validators (bmx/MXFDump) **and** links into a
   real Media Composer with no errors **(M)**, and then into an automated
   corpus.
3. 1080p25 DNxHD 120 plays in real time for 10 minutes with 0 dropped frames on
   reference hardware: M1 Mac, mid-range Windows and mid-range Linux.
4. A/V offset stays within ±1 audio sample block and lip-sync holds within
   ±0.5 frame over 60 minutes.
5. Transcode round-trips: Y'CbCr PSNR ≥ the codec's expected value, and
   timecode and reel/tape metadata are preserved.

### Phase 3 — Projects, bins and interchange
Formats come in this order:
1. Our own native model.
2. AAF import/export.
3. `.avb` bin read.
4. `.avb` bin write.
5. `.avp`/`.avs`.
6. EDL (CMX3600, File_129), ALE, OTIO and FCP XML.

Shared-storage bin locking (`.lck`-style) comes after that.

**Gates**
1. A corpus of 100+ bins and projects made with real Media Composer (versions
   8.x to current) opens in Cutthulhu. Clip count, names, timecodes, custom
   columns and sequence structure must be identical. This is checked with a
   structural diff tool.
2. Bins saved by Cutthulhu open in Media Composer without errors, with the same
   checks in reverse **(M → automated by corpus)**.
3. AAF round-trips (MC→Cutthulhu→MC and Cutthulhu→MC→Cutthulhu) have no structural
   differences.
4. Fuzzing the bin and AAF parsers for 24 hours produces no crashes.

### Phase 4 — Core editing
Features in this phase:
* Mark in/out, Splice-in, Overwrite, Lift, Extract, Replace and Fit-to-Fill.
* Segment mode, red and yellow (Lift/Overwrite and Extract/Splice).
* Trim mode: single/dual-roller, ripple, slip/slide, asymmetric trim and trim
  loop.
* Add Edit, Match Frame, Reverse Match Frame, sync locks, track patching,
  subclips and locators.
* 32-level undo.

**Gates**
1. More than 300 scripted edit scenarios, each written as a command sequence.
   The resulting sequence must equal the expected sequence. The expected
   sequences are recorded from real Media Composer and exported as AAF/EDL.
2. Undo/redo of every scenario restores identical state, checked by hash.
3. Keyboard-only editing works for everything in the user guide's editing
   chapters **(M)**.

### Phase 5 — Audio
* Audio Mixer, clip/track gain and pan, keyframes, Audio EQ, AudioSuite
  equivalents, track effects (CLAP), audio punch-in, mixdown and 5.1/7.1
  routing.

**Gates**
1. Sample-exact mixdown against reference renders: null test ≤ -120 dBFS.
2. Real-time mix of 24 tracks with 2 effects each on the reference hardware.

### Phase 6 — Effects and rendering
* Transitions, motion effects, timewarps, Color Correction (HSL and curves),
  3D Warp, Picture-in-Picture, AniMatte, Paint, Blur, SpotColor and titles.
* Keyframing in the Effect Editor.
* Render and expert render.
* OFX host, with OCIO colour management covering Rec.709, Rec.2020, PQ/HLG and
  ACES.

**Gates**
1. Each effect's output matches the reference frames from Media Composer with
   ΔE2000 ≤ 1.0 mean and ≤ 3.0 max.
2. The OFX host passes the OpenFX test plug-ins, and Boris FX Continuum/Sapphire
   load and render **(M)**.

### Phase 7 — Output and finishing
* Export (QuickTime, MXF OP1a, AS-11, DCP via an open library, IMF later).
* EDL Manager and Send-To templates.
* Output to AJA/Blackmagic devices.
* Captions (SCC, SRT, STL) and the Titler+ equivalent.

**Gates**
1. Exports validate against the relevant spec checkers (AS-11 DPP, DCP checker
   and others).
2. Broadcast output frame-accuracy is confirmed on hardware **(M)**.

### Phase 8 — Advanced workflows
* Multicam (group/multigroup), ScriptSync/PhraseFind equivalents, Dynamic Media
  Folders/background services, FrameFlex, stereoscopic, Symphony-level colour,
  and multi-user bin locking.

### Phase 9 — Hardening
* Performance profiling, accessibility audit, localisation (the languages
  Media Composer ships in), crash reporting, auto-update, code signing, and
  long-run stability tests (72-hour playback soak).

## 6. Immediate next steps

1. Decide §2.2 (module exception and/or CLA) before accepting outside
   contributions.
2. Do Phase 0 gate 4: a person follows BUILDING.md on a fresh VM for each OS.
3. Start `docs/spec/ui-inventory.md`, a window-by-window and
   menu-by-menu inventory taken from the public Media Composer User Guide.
   It drives Phase 1 gate 2.
4. Start assembling the media and bin test corpus.
