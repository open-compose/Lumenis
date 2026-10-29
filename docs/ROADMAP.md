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
* **2.3a Playback — done** (spec §16).
  * Sound through miniaudio (`third_party/miniaudio`), mixed from the
    monitored tracks with clip gain and pan (`media/playback.h`).
  * The audio clock paces the picture at 1x (A/V sync). J-K-L speeds run
    on the wall clock.
  * Frames decoded ahead on a playback thread; sequences play through
    their segments.
  * The Video Quality Menu button: Full, Draft, Best Performance.
  * `tst_playback`.
* **2.3b — done** (spec §16).
  * Smooth audio scrub up to 3x either way, with the picture following
    the sound.
  * Digital audio scrub with Shift or the Digital Audio Scrub button.
  * Composer > Full Screen Playback and its setting.
  * The Audio and Video Display settings.
  * Shown and dropped frame counts for the real-time gate.
* **2.4a Managed media — done** (spec §17).
  * Import, Consolidate and Transcode into `Avid MediaFiles/MXF/<n>/` as
    MXF OP-Atom, a file per track: Avid DNx LB to 444 (DNxHR) and 48 kHz
    PCM (`media/managed.h`, `media/consolidate.h`).
  * Clip > Consolidate/Transcode: master clips whole; sequences, the parts
    used with handles, relinked to `.new.01` clips.
  * File > Input > Import Media through the Source Browser.
  * The Media Creation and Import settings.
  * The media database, read when a project opens, finds moved media.
  * `tst_managed_media`.
* **2.4b — done** (spec §17).
  * Relink > Managed Media by source name and timecode, with the relink
    methods (`media/relink.h`); sequences relink to `.relinked` copies.
  * The Media Tool's media files and Delete Media; deleting managed clips
    from a bin can delete their media.
  * Background Consolidate/Transcode and the Background Queue Window
    (`background_queue.*`).
* **2.5 The gates below — automated where they can be** (BUILDING.md,
  "Phase 2 gates").
  * Gate 1: `decode_corpus` (ctest, `scripts/decode-corpus.py`): about 150
    files made by the FFmpeg command for every codec, rate and raster it can
    encode. Every frame, read by seeking in a shuffled order
    (`cutthulhu --framemd5 --shuffle`), matches FFmpeg's framemd5. It found
    and fixed: frames counted from the first one shown (B-frames in AVI and
    MPEG-TS), a sane frame rate and counted length for files without an
    index (raw DV), and a reliable rewind when a seek overshoots.
  * Gate 2: `scripts/check-mxf.py --make` makes media in every DNx
    resolution with `cutthulhu --write-media` and checks it with bmx's
    mxf2raw (ctest `mxf_check` when installed); linking into Media Composer
    is by hand **(M)**. bmx found two faults. FFmpeg's OP-Atom muxer
    clip-wraps the essence but labels VC-3 and BWF essence containers
    frame-wrapped, so bmx could not read any frame; `MediaWriter` now
    relabels them in the partition packs and header metadata, found through
    the random index pack. And the packages had zero dates; the writer now
    passes the creation time. The audio essence key still says
    frame-wrapped (FFmpeg's); bmx accepts it. A third fault came up in
    Phase 3.1: FFmpeg gives every track file a different, random material
    package, so Media Composer would see one clip per file. The writer now
    puts the master clip's MobID in every track file and a file MobID in
    each (docs/spec/native-model.md), and `check-mxf.py --make` checks it.
  * Gates 3 and 4: `cutthulhu --measure-playback` and
    `scripts/measure-playback.py` (DNxHD 120 1080p25 with a tone): dropped
    frames, and lip sync as how far the sound already is into a frame when
    the picture moves on to it. The Composer steps every 5 ms on a precise
    timer (macOS coalesced the 20 ms one: the picture fell up to 1.2 frames
    behind). The self-hosted Mac reports a 30-second run in CI; the 10- and
    60-minute runs on the reference machines are by hand **(M)**.
  * Gate 5: `tst_media` `transcodeRoundTrips`: each DNx resolution's
    Y'CbCr PSNR (expected values are ours, measured), timecode and tape.
    Transcoding now goes straight from the decoded picture to the encoder
    at full depth (10-bit DNx keeps 10 bits; RGB 444 rose from 33 to 53 dB, its 4:4:4 step and matrix done by hand as swscale's results differ between FFmpeg versions; FFmpeg's arm64 build decodes DNxHR 444 less exactly, about 42 dB, so arm64 accepts 40 dB).

**Gate status (Phase 2):** gates 1 and 5 pass on the Linux, Windows and
Mac CI runners on every push (156 files; FFmpeg 6.1 to 9). Gates 3 and 4
pass on all three reference machines: the owner's Windows PC, M1 Mac mini
and Ubuntu 26.04 in WSL, with 10 minutes without a dropped frame and 60
minutes of lip sync within half a frame. The owner's other Linux machines
are servers without a GUI, so Ubuntu 26.04 in WSL is the reference Linux
(owner's decision). One earlier WSL run had one picture change 0.53 frame
late. An earlier Mac run, with Docker stopped, dropped 3 frames, all in the
first half second. The measurement had pressed Play before the window was
drawn, so its first paint fell in the first ticks. It now waits a second
first, as an editor would, and drops are timed from the start. The Mac's
rerun then passed 60 minutes with no warning of any kind. The "±1 audio
sample block" part of gate 4 has no measurement of its own: the picture is
slaved to the audio clock, so there is no drift to measure, and lip sync
covers the offset. Gate 2's validator half passes: all 15 files (5 DNx
resolutions, picture and two audio tracks each) pass bmx 1.6.0's mxf2raw.
Its other half, linking into a real Media Composer, is **deferred** until the
owner has access to the licensed seat again (M): run
`scripts/check-mxf.py --make --app <cutthulhu> --keep <folder>` and copy
`<folder>/drive/Avid MediaFiles` to a drive Media Composer watches. Phase 3
goes ahead meanwhile (owner's decision).

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

**Status (Phase 3):** 3.1, the native model for interchange, is done
(docs/spec/native-model.md):
  * MobIDs (SMPTE ST 330 UMIDs) for clips, sequences and segments. Bins are
    version 2; version 1 bins get stable derived MobIDs.
  * Source chains: file, tape and import sources, and subclips as references
    to their master. Managed MXF carries the master clip's MobID in every
    track file and a file MobID in each.
  * Structure dumps (`cutthulhu --dump-structure`) and `scripts/structdiff.py`
    for gate 1 (docs/spec/structure.md).

3.2, AAF, has started (docs/spec/aaf.md). Without a Media Composer seat for
now, the first reference corpus is the MIT-licensed bins and AAFs in pyavb's
and pyaaf2's test suites, made with real Media Composer
(`scripts/interop-corpus.py`, downloaded at pinned commits, not committed).
  * 3.2a, the compound file container (`src/interop/cfb.*`), is done: a
    reader for version 3 and 4 files that reads streams on demand and
    refuses damaged files, and a writer. Every corpus AAF reads as olefile
    reads it, and olefile reads what the writer makes.
  * 3.2b, AAF's objects, properties and metadictionary (`src/interop/aaf*`),
    is done. Every corpus AAF dumps byte for byte as pyaaf2 reads it
    (`cutthulhu --dump-aaf`, `scripts/aaf-dump.py --compare`).
  * 3.2c, AAF import into bins (`src/interop/aaf_import.*`), is done:
    master clips with their source chains, subclips and sequences. Every
    corpus AAF with a Media Composer bin of the same material (13) imports
    the same as that bin (ctest `aaf_import_bins`): gate 1 on the corpus at
    hand, effect names aside.
  * 3.2d, AAF export (`src/interop/aaf_writer.*`, `aaf_export.*`), is done.
    Every corpus AAF imported, exported and imported again is the same, and
    pyaaf2 reads each export as Cutthulhu does (ctest `aaf_round_trip`):
    gate 3's Cutthulhu→Cutthulhu half. The halves through Media Composer
    (MC→Cutthulhu→MC, Cutthulhu→MC→Cutthulhu) are DEFERRED with gate 2's
    Media Composer check until the owner has the seat again.
  * 3.2e, AAF in the application, is done: Import through the Source
    Browser (clips online by MobID, p.547), File > Output > Export to File
    with the Export As dialog and the Export setting (p.868-869,
    p.1173-1177).
  * 3.3, reading Media Composer's `.avb` bins (docs/spec/avb.md), has
    started. 3.3a, the object reader (`src/interop/avb.*`), is done:
    every class in the corpus bins is read as pyavb reads it, and all 14
    bins dump byte for byte the same (`cutthulhu --dump-avb`,
    `scripts/avb-dump.py --compare`, ctest `avb_pyavb`, tst_avb).
  * 3.3b, bins into the native model (`src/interop/avb_import.*`), is
    done: master and audio clips with their source chains, subclips and
    sequences, with reference clips marked (`userPlaced`). All 13 corpus
    bins with a reference dump read the same as pyavb reads them
    (`cutthulhu --import-avb`, ctest `avb_import_bins`), effect names
    aside. File > Open Bin reads an `.avb` into a bin under Other Bins
    (p.296), its clips online by MobID.
  * 3.4, writing `.avb` bins (docs/spec/avb.md, "Writing" and "Export"),
    is done. 3.4a, the object writer (`src/interop/avb_writer.*`, ported
    from pyavb's write methods): all 14 corpus bins read and written back
    are the same files, byte for byte (`cutthulhu --rewrite-avb`, ctest
    `avb_round_trip`); written big-endian they read the same in Cutthulhu
    and pyavb; a bin built from scratch reads in pyavb as Cutthulhu reads
    it (ctest `avb_write_pyavb`, tst_avb).
  * 3.4b, native bins written as `.avb` (`src/interop/avb_export.*`,
    `cutthulhu --export-avb`), is done: master and audio clips with their
    source chains (managed and linked files, tapes, imports), subclips,
    sequences with filler, transitions and effects (by Media Composer's
    effect IDs), reference clips, user columns and the bin view. All 13
    corpus bins with a reference read, written and read again are the same,
    columns, sources and effects included (ctest `avb_export_round_trip`);
    pyavb reads every bin written as Cutthulhu does and as the bin exported
    (ctest `avb_export_pyavb`); a bin made in the application's terms reads
    back the same (tst_avb `exportsBin`). In the application: File > Output
    > Export to File with the Export setting's Export As at Media Composer
    Bin (ours: the guide's Export As menu has no bin type; docs/spec/avb.md,
    "In the application"). Gate 2 (bins saved by Cutthulhu open in Media
    Composer) is DEFERRED with the other Media Composer checks until the
    owner has the seat again.
  * 3.5, Media Composer projects (`.avp`) and settings (`.avs`), is
    DEFERRED: neither pyavb nor any other open reader covers them, no public
    sample files exist, and black-box observation needs a Media Composer
    seat. It resumes when the owner can supply sample files (used for tests
    only, not committed) or has the seat again.
  * 3.6, EDL, ALE, OTIO and FCP XML, has started. 3.6a is done: EDLs
    (`src/interop/edl.*`, docs/spec/edl.md: CMX_3600 and File_129 read into
    a sequence with a tape source per reel, written from one; `--import-edl`,
    `--export-edl`) and ALE shot logs (`src/interop/ale.*`, docs/spec/ale.md;
    `--import-ale`, `--export-ale`). In the application: File > Input >
    Import EDL (p.951-952), Tools > List Tool (`list_tool.*`, p.939-960),
    shot logs through the Source Browser's Import (p.129-132), File >
    Output > Export Bin and Export to File as Avid Log Exchange (p.130,
    p.1174). Tests `tst_edl`, `tst_ale` (with OpenTimelineIO's sample files,
    Apache-2.0, in the downloaded corpus) and `tst_interchange`. File_129's
    exact layout is to be checked against Media Composer when the seat is
    back. 3.6b, OpenTimelineIO, has started: the reader and writer
    (`src/interop/otio.*`, docs/spec/otio.md: timelines into offline master
    clips and sequences, a sequence out as a Timeline; `--import-otio`,
    `--export-otio`) are done, checked against OpenTimelineIO's own reader
    (ctests `otio_round_trip`, `otio_python`, `tst_otio`) and fuzzed (target
    `otio`). In the application (p.969): Import Media and drops into a bin
    take `.otio` files (`MainWindow::importOtio`), and the List Tool has the
    OTIO output format with the Default OpenTimelineIO setting (test
    `tst_interchange`). 3.6b is done. 3.6c, FCP 7 XML, is done
    (`src/interop/fcp_xml.*`, docs/spec/fcp-xml.md; ours, as Media Composer
    has none): xmeml read and written through the OTIO import and export,
    `--import-fcpxml`, `--export-fcpxml`, Import Media and bin drops, and
    Export to File's "Final Cut Pro 7 XML"; checked against OpenTimelineIO's
    FCP adapter (ctests `fcpxml_round_trip`, `fcpxml_python`, `tst_fcpxml`)
    and fuzzed (target `fcpxml`). 3.6 is done.
  * Gate 4 (fuzzing): libFuzzer targets for the compound file, AAF and
    bin readers, importers and the bin writer (`fuzz/`, `scripts/fuzz.py`,
    BUILDING.md "Fuzzing") found and fixed 9 bugs (hangs, a division by
    zero, an overflow, three writer round-trip slips); saved inputs replay
    on every platform (ctest `fuzz_replay`). The 24-hour run is by hand on
    the self-hosted Linux runner (`.github/workflows/fuzz.yml`, needs
    `libclang-rt-dev`). The first three found three more bugs in the AAF
    dump and import (fixed, with regression inputs); the fourth
    (2026-10-23/24, run 39992718177) was clean on every target: **passed**.
  * 3.7, shared-storage bin locking (docs/spec/bin-locking.md), is done:
    lock and history files beside each bin on a network share
    (`src/model/bin_lock.*`), the Bin Lock Status button (green, red,
    yellow, blue), read-only bins, bold locked bins in the Bin Container,
    Lock, Protect and Unlock Project Bin, Alt+double-click, and each
    machine's open bins kept apart (ctests `tst_bin_lock`,
    `tst_bin_locking`). Media Composer 2026.8 Enterprise adds shared
    projects on third-party NAS/SAN with "the same ... bin-locking
    workflows" (2026.8 Editing Guide p.301). Avid does not document its
    lock files, so ours stay. Whether the two can share a bin folder is
    checked when the seat is back (docs/spec/bin-locking.md).
  * Left in Phase 3: 3.5 (deferred), and the gates' Media Composer halves,
    which wait for the owner's seat.

### Phase 4 — Core editing
Features in this phase:
* Mark in/out, Splice-in, Overwrite, Lift, Extract, Replace and Fit-to-Fill.
* Segment mode, red and yellow (Lift/Overwrite and Extract/Splice).
* Trim mode: single/dual-roller, ripple, slip/slide, asymmetric trim and trim
  loop.
* Add Edit, Match Frame, Reverse Match Frame, sync locks, track patching,
  subclips and locators.
* Undo: the guide gives 100 levels (p.466), which Cutthulhu keeps.

**Gates**
1. More than 300 scripted edit scenarios, each written as a command sequence.
   The resulting sequence must equal the expected sequence. The expected
   sequences are recorded from real Media Composer and exported as AAF/EDL.
2. Undo/redo of every scenario restores identical state, checked by hash.
3. Keyboard-only editing works for everything in the user guide's editing
   chapters **(M)**.

**Status (Phase 4):** the editing features were built in Phase 1.4
(1.4a-1.4d). 4.1 adds what was missing, Fit to Fill (FX Guide p.170) and
Reverse Match Frame (p.433-434), and the gates' harness
(docs/spec/edit-scenarios.md): `tst_scenarios` runs the scenarios in
`tests/scenarios` (from the guide's rules, each with its page) and 300
random ones whose results `scripts/make-scenarios.py` works out with a
reference model of its own (ctest `edit_scenarios_generated`), and undoes
and redoes each by hash (gate 2: **passes**). Gate 1's expected sequences
from real Media Composer wait for the owner's seat; gate 3 is by hand.
4.2 brings in what the 2026.8 guides changed (docs/spec/ui-inventory.md,
"Editions"):
* Change Source Track on audio segments, with More for a subclip's master
  clip tracks (2026.8 guide p.507);
* the Bin Column Selection dialog's Search field, keys and vertical resize
  (2026.8 guide p.246);
* 32-bit Effects Processing in the Media Creation setting.

### Phase 5 — Audio
* Audio Mixer, clip/track gain and pan, keyframes, Audio EQ, AudioSuite
  equivalents, track effects (CLAP), audio punch-in, mixdown and 5.1/7.1
  routing.

**Gates**
1. Sample-exact mixdown against reference renders: null test ≤ -120 dBFS.
2. Real-time mix of 24 tracks with 2 effects each on the reference hardware.

**Status (Phase 5):** 5.1, Audio Mixdown (docs/spec/audio-mixdown.md), is
done: Timeline > Mixdown > Audio > To Sequence, Mono and Dual Mono, into
managed media and the target tracks (`src/media/mixdown.*`,
`mixdown_dialog.*`). Gate 1's null test (`tst_mixdown`) passes against a
reference worked out from the sources at about -137 dBFS; the reference
renders from real Media Composer wait for the owner's seat.
5.2, volume and pan automation (docs/spec/automation.md), is done:
* keyframes on segments, counted in the clip's frames (`src/model/automation.*`);
* the Timeline's Audio Data graph and keyframe editing, copy and paste
  (`timeline_automation.cpp`);
* the Audio Mixer's Auto mode faders, recording and Fast menu;
* playback and mixdown following the keyframes sample by sample.
The null test has automated cases at about -137 dBFS.
5.3, the Audio EQ tool (docs/spec/audio-eq.md), is done:
* three-band EQ on whole segments (`src/model/audio_eq.*`, `Segment::eq`);
* the filters in playback and mixdown (`media::AudioMix`), checked against
  a double-precision reference at about -144 dBFS;
* the tool (`src/ui/audio_eq.*`): sliders, 3-band buttons, EQ Range,
  graph, In, Bypass RT EQ, Track Selection and the Fast menu's Set EQ and
  Remove EQs; Remove Effect and the Timeline's EQ icon.
The twelve predefined templates are in the Fast menu; their values are in
`src/ui/resources/eq_templates.json` (one observed, the rest mostly
guesses). Templates in bins, Audio Loop Play and Render Effect come later.
5.4, Audio Punch-In (docs/spec/punch-in.md), is done:
* recording from the system's inputs through miniaudio, with monitoring
  (`src/media/audio_input.*`);
* the tool (`src/ui/audio_punch_in.*`): Play In/Out with looping, Record
  with preroll, postroll and handles, Stop at end or at mark out, the input
  channels and Timeline Track menus, target drive and bin;
* each take one "Voice Over" master clip of managed media, each punch-in an
  undo step (`Timeline::punchIn`).
On macOS the bundle asks for the microphone (Info.plist, entitlement).
5.5a, Audio Track Effects (docs/spec/track-effects.md), is done:
* the audio effect interface and nine built-in effects (`src/media/audio_fx.*`),
  which the CLAP host (5.5b) will add to;
* five inserts a-e on each audio track (`model::Track::inserts`), mixed
  through in real time and in Audio Mixdown (`media::AudioMix`);
* Tools > Audio Track Effect, the track button's menu, the Effect
  Palette's Audio Track tab and Select Insert, Bypass and Ctrl+Bypass.
5.5b, the CLAP plug-in host (docs/spec/clap-host.md), is done: CLAP 1.2.9
headers in `third_party/clap`; `src/media/clap_host.*` finds `.clap` files in
the standard folders and `CLAP_PATH` at start-up, and the effect plug-ins
that fit (mono or stereo) join the Audio Track Effects. A CLAP file of our
own tests it (`tst_clap_host`). Plug-in windows, latency compensation and
state come later.
5.5c1, AudioSuite on Timeline segments (docs/spec/audiosuite.md), is done:
* our own processes (Gain, Normalize, Invert, Reverse, DC Offset Removal,
  Signal Generator) and every audio effect run offline
  (`src/media/audio_suite.*`); Avid's own AudioSuite plug-ins wait for the
  AAX bridge;
* the effect on segments, rendered into managed media that plays in the
  segment's place, unrendered by the changes the render order says
  (p.700); Audio Mixdown renders first (`src/model/audio_suite.*`);
* Tools > AudioSuite (`src/ui/audio_suite.*`): Track Selection and Plug-In
  Selection menus, the plug-in dialog, Render Effect, the Fast menu's Set,
  Render and Remove over the marks, the Target Drive menu.
5.5c2, AudioSuite on master clips (docs/spec/audiosuite.md), is done: clips
dropped on the tool become new master clips of managed media with their
templates beside them (`media::audioSuiteClip`, the tool's master clip
controls), templates find their clips again (Find Source From Effect),
the effect icon saves templates to bins, the Fast menu applies them;
Duplicate.
5.6a, multichannel tracks (docs/spec/multichannel.md), is done: Track
Formats on clips and Clip > Modify > Modify Clip's Set Multichannel Audio
(`src/model/multichannel.*`, `modify_dialog.*`); stereo, 5.1 and 7.1
segments edited, patched and moved only onto tracks of their format; their
channels played for stereo speakers as the guide's table has them, each
through its own copy of the track's effects; Split Track to Mono and Split
All Tracks.
5.6b1, surround mixing (docs/spec/surround.md), is done: the Sequence Mix
Format (`model::Sequence::mixFormat`) and the Mix Mode Selection's monitor
formats (`src/media/surround.*`); the mix on an eight-speaker bus, folded
as the guide's table has it into the speakers listening, in each of their
channel orders; multichannel output; Stereo, 5.1 and 7.1 Audio Mixdowns.
5.6b2, surround panning (docs/spec/surround.md), is done: surround pans on
mono and stereo segments (`model::SurroundPan`, `src/model/surround_pan.*`)
with our pan law (`media::surroundGains`), the Audio Mixer's Pan grids,
Stereo Link and Mirror, the Advanced Panner (`src/ui/surround_panner.*`),
and LFE-only tracks.
5.7 finishes the audio items still dimmed in the UI, found by the audit of
2026-10-27 (every dimmed command, button and setting sorted by whether its
area is built):
* 5.7a, the Audio Mixer: the strips' Insert button, Audio Loop Play (also
  in the Audio EQ and AudioSuite tools), Render Effect, Show Track Sidebar,
  and the Fast menu's Adjust Pan/Vols on Track, Remove/Commit Default Clip
  Pan, Disable Track Monitoring, Set Display Options and Set Live Mix
  to/as Automation;
  5.7a1 is done (docs/spec/audio-mixer.md): Audio Loop Play
  (`Composer::playLoop`, `Timeline::audioLoopRange`, command
  `play.audio_loop_play`), the insert buttons, Render Effect, a segment's
  own centred pan (`Segment::hasPan`), the Clip mode items, Set Display
  Options, Live Mix heard in playback and Set Live Mix to/as Automation
  (`model::setAutomationTo`). 5.7a2, Show Track Sidebar and Audio
  Grouping (p.691, p.696-698; `src/ui/audio_groups.cpp`), is done. Calibrate Hardware Sliders stays dimmed
  until external controllers (p.757);
* 5.7b, the Clip and Timeline commands (docs/spec/clip-audio.md; 5.7b1,
  Apply Gain, Center Pan and Auto VO, and 5.7b2, Read Audio Timecode with
  our own LTC decoder and Change Sample Rate, are done; 5.7b3, Multiple
  Mixes with the Multi-Mix Tool (docs/spec/multi-mix.md), is done): Clip > Audio > Apply Gain (p.216),
  Center Pan (p.714) and Change Sample Rate (p.731), Auto VO (p.754), Read
  Audio Timecode (p.668), Mixdown > Audio > Multiple Mixes (p.732);
* 5.7c, the Audio Tool's calibration tone and Create Tone Media, the Audio
  EQ's effect icon, and the Audio, Audio Project and Import settings' audio
  options. 5.7c1, Play Calibration Tone, Create Tone Media and the Audio
  Tool's meters (docs/spec/tone.md), is done; 5.7c2, the Audio EQ's
  effect icon and EQ templates in bins, the Timeline and Site_EQs_Bin
  (docs/spec/audio-eq.md), is done; 5.7c3, the Audio, Audio Project and
  Import settings' audio options (docs/spec/audio-settings.md), is done but
  for Display Channel Order (with U1's waveforms), Remove Extra Filler
  After Punchin (with Extended Audio Punch-In) and Apply attenuation/gain
  effect on import (Phase 9, with CD import).

### The UI completion pass (after Phase 5, before Phase 6)
The same audit found items of areas built in Phases 1-4 left dimmed with no
phase of their own. They are done in three steps:
* U1, the Timeline: audio waveforms (p.677) and Show Marked Waveforms, with
  the Audio setting's Display Channel Order for 5.1 and 7.1 (p.1148) (U1a,
  docs/spec/waveforms.md, is done); the
  Fast menu's Show Markers, Track Color, Segment and Mark IO Selection
  colours, Track Control panel (U1b1, docs/spec/track-control.md, with per
  track settings and Enlarge/Reduce Track, is done), View Type and Timeline Views (p.592-604)
  (U1b2, docs/spec/timeline-views.md, is done; Show Adapters and Render Ranges move to Phase 6),
  Sync Breaks, Dupe Detection, Show Every Frame and the remaining Clip Text
  items (U1c1, docs/spec/sync-dupes.md, is done but for Group Clip Name,
  Source Track Name and Show Every Frame, which wait for groups, sound
  metadata and the Film track; U1c2, Timeline Clip Notes, docs/spec/clip-notes.md, is done); Show Sequence Map (p.606; U1d2, docs/spec/sequence-map.md, is done), Find Black Hole/Flash Frame (p.645), Add
  Filler at Start (p.462), Rename Track, Create Sequence Based on Selection
  (p.613) (U1d1, docs/spec/timeline-commands.md, is done); the Timeline settings still dimmed (U1e, docs/spec/timeline-settings.md, is done but for Segment Drag Snap, which waits for bin editing; Display During Segment Drag and Dynamic Play move to U2; Double-Click to Show Nesting and Highlight Suggested Render Areas to Phase 6).
* U2, the Composer, editing and trim: Load Filler (p.99), Sync Point
  Overwrite (p.496) and the Composer settings' Edit tab (U2a,
  docs/spec/composer-edit.md, is done but for Undo Only Record Events and
  Color Framing), AutoSequence (p.499) (U2d, docs/spec/autosequence.md, is done), Loop Selected Clips (p.393), the
  Timecode Window (p.389) (U2b, docs/spec/timecode-window.md, is done), Edit > Find (p.417) (U2c, docs/spec/find.md, is done); the Composer and Trim
  settings still dimmed (the trim loop, Edit Review and J-K-L Trim, U2e1,
  and the trim frames, Small Trim, Dual Image Play and Sync Rollers, U2e2,
  docs/spec/trim-settings.md, are done; so is the Composer Window tab, U2e3,
  docs/spec/composer-display.md, but for multicamera icons, and Dynamic Play
  and the four-frame display, U2e4; Undo Only Record Events and Color
  Framing stay dimmed). So U2 is done.
* U3, bins, media, windows and interface: Bin > Select's Offline Items,
  Media Relatives, Sources and Unreferenced Clips (p.266-267), Reveal File
  (p.360) and Close All But Active (U3a, docs/spec/bin-select.md, is done;
  Enlarge and Reduce HeadFrames, p.1016, are the NRCS Tool's); Input > Link (p.328),
  Media > Unmount (p.344) and Center Current/Send Current Home (p.601) (U3b,
  docs/spec/media-unmount.md, is done); the bin attic and auto-save (U3e, docs/spec/bin-attic.md, is
  done), the Frame view's borders and icons, Show local media icons and Save single floating Bin
  Containers (U3h, docs/spec/bin-settings.md, is done) and the other Bin settings (bin editing
  into the Timeline, p.623-624, and the Timeline's Segment Drag Snap: U3f,
  docs/spec/bin-editing.md, is done); Bin Layout (p.93; U3d, docs/spec/bin-layout.md, is done but for its
  buttons), Set Font (p.82) and Help > Shortcuts (p.97) (U3c,
  docs/spec/set-font.md, is done), the Interface colour
  and font settings (U3g, docs/spec/interface-colors.md, is done but for the brightness sliders and
  the Default Timeline track options).

### Phase 6 — Effects and rendering
* Transitions, motion effects, timewarps, Color Correction (HSL and curves),
  3D Warp, Picture-in-Picture, AniMatte, Paint, Blur, SpotColor and titles.
* Keyframing in the Effect Editor. Titler+ keeps a title's keyframes through
  trims, splits and duplicates (FX p.325, 2026.8).
* Render and expert render, at 8-, 16- or 32-bit (FX p.151-152, 2026.8).
* The Timeline Fast menu's Render Ranges (FX guide) and Show Adapters (Editing Guide p.599), moved
  here from the UI completion pass.
* OFX host, with OCIO colour management covering Rec.709, Rec.2020, PQ/HLG and
  ACES.

**Steps** (planned 2026-10-29 from the FX Guide's chapters; pages are the FX Guide's):
* 6.1, the compositor: a sequence frame made from every monitored video track, bottom to top, each
  segment's picture through its effect and transitions blended across cuts, in floating point and
  at the 8-, 16- or 32-bit processing the setting picks; the monitors, Full Screen Playback and
  output take their frames from it. With it the dissolve family (Dissolve, Fade to/from Color, Dip
  to Color, Film Dissolve and Fade, Superimpose). Tests check the pixels; gate 1's reference-frame
  comparison waits for Media Composer's frames (M). (6.1, docs/spec/compositor.md, is done.)
* 6.2, keyframes on effect parameters: standard and advanced keyframes and the Effect Editor's
  graphs (ch.4, p.94-134), with the Fade Effect button (p.46-47) and Reverse Animation. 6.2a, the
  model, its evaluation in the compositor and the Fade Effect button (docs/spec/effect-keyframes.md),
  is done; so is 6.2b1, the Effect Editor's keyframe tracks, graphs, menus and Add Keyframe modes, and
  trims keeping Fixed keyframes. 6.2b2 (graph zoom and scroll, copy and paste, Bézier handles, the
  Effect Editor settings, standard keyframes) is next.
* 6.3, the effect families of the Effects Reference (ch.17-18, p.454-586): wipes and pushes,
  squeezes, Blend (Picture-in-Picture, Superimpose), Image (Blur, Color Effect, Flip-Flop, Mask,
  Mosaic, Resize, Letterbox and the rest), Key (chroma, luma, matte keys).
* 6.4, rendering (p.57-61, p.135-156): Render Effect, ExpertRender, partial render, render
  settings, precomputes as effect media files, render-on-the-fly, the real-time playback
  indicators, and the Timeline's Render Ranges and Show Adapters moved here from the UI pass.
* 6.5, motion effects and Timewarp (ch.7, p.165-205): freeze frames, variable speed, strobe, the
  Motion Effect Editor's speed and position graphs.
* 6.6, layered and nested effects (ch.8, p.206-228): nesting and stepping in, Collapse,
  Submaster, Double-Click to Show Nesting.
* 6.7, 3D effects, camera effects, and tracking and stabilization (ch.9-11, p.229-300).
* 6.8, titles (ch.12-13, p.301-377): the Title Tool and Titler+, with keyframes kept through trims,
  splits and duplicates (2026.8). SubCap (ch.14) goes to Phase 7 with the captions.
* 6.9, intraframe effects (Paint, AniMatte, Blur, Scratch Removal; ch.15) and PlasmaWipe (ch.16).
* 6.10, colour correction (ch.19-24, p.587-733): Color Correction mode, HSL and curves, safe
  colours, and the Symphony option's spot colour.
* 6.11, the OFX host in place of AVX (ch.6, p.157-164), with OCIO colour management.

**Gates**
1. Each effect's output matches the reference frames from Media Composer with
   ΔE2000 ≤ 1.0 mean and ≤ 3.0 max.
2. The OFX host passes the OpenFX test plug-ins, and Boris FX Continuum/Sapphire
   load and render **(M)**.

### Phase 7 — Output and finishing
* Export (QuickTime, MXF OP1a, AS-11, DCP via an open library, IMF later).
* EDL Manager and Send-To templates.
* Output to AJA/Blackmagic devices.
* Captions (SCC, SRT, STL) and the SubCap effect (FX Guide ch.14, p.378-390); Titler+ is in Phase 6.8.

**Gates**
1. Exports validate against the relevant spec checkers (AS-11 DPP, DCP checker
   and others).
2. Broadcast output frame-accuracy is confirmed on hardware **(M)**.

### Phase 8 — Advanced workflows
* Multicam (group/multigroup), ScriptSync/PhraseFind equivalents, Dynamic Media
  Folders/background services, FrameFlex, stereoscopic, Symphony-level colour,
  and multi-user bin locking.
* From 2026.8:
  * the Transcript Tool's versions (Revert, Revert Transcript), Start New
    Line, Group Speaker Lines and New Line After (2026.8 guide p.446);
  * Multicam Auto Cut from transcript speakers (2026.8 guide p.451,
    p.1134);
  * Change Source Track for group and multigroup clips (2026.8 guide
    p.507).

### Phase 9 — Left over stuff
Items that fit no other area or phase, done before hardening (owner's
decision, 2026-10-27):
* printing: Page Setup, Print Bin, Print Frame and Print Timeline
  (p.288-292, p.645);
* Batch Import and Batch Re-import (p.1098, p.230);
* AutoSync (p.223);
* importing from audio CDs, with the Import setting's Apply
  attenuation/gain effect on import and its CD only choice (p.1198);
* anything else later found not to fit another area.

### Phase 10 — Hardening
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
