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
  trims keeping Fixed keyframes, and 6.2b2, graph zoom and scroll, copy and paste, Bézier handles and
  the Effect Editor setting. 6.2c, Effect mode (ch.2-3) with the Effect Preview monitor and its
  toolbar (docs/spec/effect-mode.md), is done; standard keyframes wait until the guide names an effect
  that uses them. So 6.2 is done; 6.3, the effect families, is next.
* 6.3, the effect families of the Effects Reference (ch.17-18, p.454-586), in four parts (planned
  2026-10-29 from p.456-490):
  * 6.3a, the 2D transitions: Box, Edge, Shape, Matrix and Sawtooth wipes; Conceal and L-Conceal;
    Push; Squeeze; X, Y and Z Spin; with the parameters they share (Border p.493, Foreground p.495,
    Acceleration p.492, Custom Grid p.494) and the comparison of similar effects (p.490). Done
    (docs/spec/wipes.md: `src/media/wipes.cpp`, the Custom Grid's More Options); Acceleration is not
    among their parameters and comes with 6.3b-c.
  * 6.3b, Blend and Image, in two parts: 6.3b1, the geometric ones (Picture-in-Picture with Border,
    Foreground, Scaling, Position and Crop; Flip, Flop and Flip-Flop; Field Swap; Resize; Mask; the Film
    masks and Blowup; the Reformat letterboxes and 4:3 Sidebar; Scaling and Position on the Shape and
    diagonal Edge Wipes), is done (docs/spec/image-effects.md: `src/media/image_effects.cpp`); so is
    6.3b2, the Color Effect, Blur and Mosaic over the whole picture, and Acceleration
    (`src/media/color_effects.cpp`, `model::numberAt`). So 6.3b is done. Pan and Scan's tools come with the Effect Preview's
    effect tools.
  * 6.3c, Key: Luma Key, Chroma Key, RGB Keyer, Matte Key and SpectraMatte. Done (docs/spec/keys.md:
    `src/media/key_effects.cpp`, Matte Key in the compositor); SpectraMatte's SpectraGraph and Luma
    Curve wait.
  * 6.3d, Illusion FX (p.463-468) and the Generator's Timecode Burn-In (SubCap is Phase 7), in four
    parts (planned 2026-10-30 from p.463-527): 6.3d1, Timecode Burn-In (docs/spec/burn-in.md:
    `src/media/burn_in.cpp`, the Effect Editor's display tabs), is done; so is 6.3d2, the Illusion FX
    segment effects that change colour and texture or draw (Color Mix, Film Grain, Crystal, Flare,
    Pattern Generator, Lightning, Sparkler, Kaleidoscope, Motion Blur; docs/spec/illusion-fx.md:
    `src/media/illusion_fx.cpp`); and so is 6.3d3, the distortions (Pinch, Radial
    Blur, Ripple, Sphere, Swirl, Twist, Wave) with the Illusion FX Border; and so is 6.3d4, the Illusion FX
    transitions (Iris, Melt, Pagecurl, Particle Blast, Particle Orbit, Particle Wind, Random Blend,
    Rollup, Shear; `src/media/illusion_transitions.cpp`). So 6.3d is done. FluidBlur, FluidColorMap and FluidMorph need motion estimation and go with the
    FluidMotion work (6.5).
  Peel (a page curl) goes with the 3D effects (6.7); PlasmaWipe with 6.9; AniMatte, Paint, Scratch
  Removal and Stabilize with their chapters (6.7, 6.9). So 6.3 is done; 6.4 is next.
* 6.4, rendering (p.57-61, p.135-156): Render Effect, ExpertRender, partial render, render
  settings, precomputes as effect media files, render-on-the-fly, the real-time playback
  indicators, and the Timeline's Render Ranges and Show Adapters moved here from the UI pass. In four
  parts (planned 2026-10-30 from p.57-61 and p.135-156; docs/spec/rendering.md):
  * 6.4a, Render Effect: Render at Position and In/Out with the Render Effect dialog and the Effect
    Editor's button, precomputes as managed media holding the effect's track and those below, played in
    place of the effect while in date, Clear Renders with its dialog and Undo, the Timeline's dots.
    Done (`src/model/effect_render.*`, `src/media/render.*`, `render_dialogs.*`).
  * 6.4b, partial render (Ctrl+period, Keep or Discard), the Timeline's Render Ranges (None, Partial
    Only, All), the playback performance indicators (red, yellow and blue bars; Highlight Suggested Render
    Areas After Playback) and Composer > Render On-the-Fly. Done (blue is not told apart from yellow).
  * 6.4c, ExpertRender at Position and In/Out: the analysis, the Expert Render dialog (recommended
    ranges, entire selection, Prepare effects for Digital Cut) and Modify Selection. Done (In/Out, the
    menu's only one).
  * 6.4d, in two parts:
    * 6.4d1, the Render setting (Image Interpolation and Render Completion Sound; the Render Using
      menus, Effects Quality and HQ wait for motion effects (6.5) and the 3D effects (6.7)), Save after
      Render, and precompute management: precomputes as reference clips in the sequence's bin (Show
      reference clips, Reveal File, Delete), the Media Tool's Precompute Clips, and deleting a
      precompute rendered again before a save. Done. Show Adapters goes with the first adapters, motion adapters (6.5).
    * 6.4d2, Background Process rendering (the __bg folders, the Background Queue). Done, so 6.4 is.
* 6.5, motion effects and Timewarp (ch.7, p.165-205): freeze frames, variable speed, strobe, the
  Motion Effect Editor's speed and position graphs. In four parts (planned 2026-10-30 from p.165-205,
  p.476-488 and p.561-563):
  * 6.5a, traditional motion effects: playing a segment's speed (the Motion Effect of Fit to Fill and
    the imports, until now shown at normal speed) in the compositor and the sound; Composer > Freeze
    Frame (Other, the duration dialog, the two-field choices) making an "FF" clip; the Motion Effect
    dialog from the Command palette's FX tab with Active Palette and the Source monitor (Variable Speed
    by Frames, FPS, % Speed or Fit To Fill; Strobe Motion; the 2-field options, none in progressive
    projects; Create or Create and Render) making Motion Effect clips named as p.170-172 say; the
    Timeline's speed in parentheses and the green or blue dots of p.172. Done
    (docs/spec/motion-effects.md); Fit to Fill making such a clip (Alt unrendered, p.170) and rendering
    motion effects in the Timeline move to 6.5b.
  * 6.5b, Timewarp: the effect's speed and position keyframes (Source Anchor Frame, Strobe, Type),
    output frames to source frames with Duplicated Field, Both Fields and the Blended frame mixes, the
    presets (0% To 100%, 100% to 0%, Reverse Motion, Speed Boost, Speed Bump, Trim to Fill), Promote to
    Timewarp and Clip > Refresh Sequence > Motion Adapters/Timewarps. Done (docs/spec/motion-effects.md),
    but Promote's button comes with the Motion Effect Editor (6.5c) and Refresh Sequence with fields (6.5d);
    Fit to Fill keeps its segment Motion Effect, which renders in the Timeline.
  * 6.5c, the Motion Effect Editor: the Speed and Position graphs with the advanced keyframe tools of
    6.2b, Edit Graph, Type and Ignore Render Setting (with the Render setting's Render Using menus), the
    anchor, Strobe and Duration, its toolbar, and dragging its icon to a bin as a template. Done
    (docs/spec/motion-effects.md), but the icon's drag to a bin comes with effect templates (6.6).
  * 6.5d, in three parts (planned 2026-10-30 from FX p.197-205 and Editing Guide p.468-480):
    * 6.5d1, FluidMotion: our own optical flow (no new dependency) making the in-between frames of the
      FluidMotion and FluidMotion Draft Types (p.186, p.197). FluidMotion Edit's regions are drawn with
      the intraframe tools, so it goes with them (6.9). Done (`src/media/optical_flow.*`).
    * 6.5d2, motion adapters (Editing Guide p.468-478): mixed rate clips play at the project's rate in
      the monitors and sequences (until now frame for frame), the green dot and "(59.94 fps)" names in the
      Timeline, the adapter in the Motion Effect Editor (Type and Promote only), the Field Motion bin
      column, Show Adapters, Refresh Sequence > Motion Adapters/Timewarps and the Render setting's Use
      frame blending in Motion Adapters. Done (`src/model/motion_adapter.*`; adapters in AAF and `.avb`
      interchange are left for later).
    * 6.5d3, fields and format conversion, in two parts (planned 2026-10-31 from FX p.181-186,
      p.203-205 and Editing Guide p.472-473):
      * 6.5d3a, fields: the field-level Types (Duplicated Field, Interpolated Field, VTR-Style and the
        blends at field times) on interlaced sources; the Timewarp's Source and Output formats
        (Interlaced, Progressive, Film with pulldown, Progressive Repeated) with the pulldown phase and
        repeat count, the FluidFilm and Pulldown Insertion/Removal templates, Adaptive Deinterlace
        Source; motion adapters following their Field Motion (Interlaced, 2:3 Film, Progressive
        Strobe); the field order of linked media; and the two-field options of 6.5a's motion effects.
        Done (`src/media/fields.*`).
      * 6.5d3b, the FluidBlur, FluidColorMap and FluidMorph Illusion FX on 6.5d1's flow. Done
        (`src/media/fluid_fx.*`), so 6.5 is.
* 6.6, layered and nested effects (ch.8, p.206-228): nesting and stepping in, Collapse,
  Submaster, Double-Click to Show Nesting; and effect templates in bins (p.52-55), with the Motion
  Effect Editor's Timewarp icon (p.182). In two parts (planned 2026-10-31 from FX p.52-55, p.219-228):
  * 6.6a, nests: nested tracks inside a segment effect, composited as the effect's picture; Step In and
    Step Out with the nesting level (N1, N2 ...); nesting an effect inside another (Alt+drag or
    Alt+double-click); Collapse into a Submaster effect; and Timeline > Mixdown > Video. Done
    (docs/spec/nesting.md, `src/model/nest.*`).
  * 6.6b, the rest, in three parts:
    * 6.6b1, effect templates in bins: drag the effect icon to a bin, Alt for source media, apply to
      segments, transitions or one parameter group, the Effect Palette's bin icons, Promote Effects, with
      the Motion Effect Editor's Timewarp icon. Done (docs/spec/effect-templates.md,
      `src/model/effect_template.*`).
    * 6.6b2, nested effects in the Effect Editor and changing their order (p.221-222). Done
      (docs/spec/nesting.md, `model::nestedEffects`).
    * 6.6b3, expanding a nest in the Timeline by double-clicking, and the Double-Click to Show Nesting
      setting (p.220-221, Editing Guide p.1219). Done (docs/spec/nesting.md, `model::expandNest`), so 6.6
      is.
* 6.7, 3D effects, camera effects, and tracking and stabilization (ch.9-11, p.229-300). In four parts
  (planned 2026-10-31 from the FX Guide's contents, p.229-300):
  * 6.7a, 3D effects (ch.11, p.287-300), in two parts:
    * 6.7a1, the 3D Warp renderer (the parameter hierarchy, axis, rotation, position, scaling, skew, crop,
      perspective, target, Corner Pin, border, shadow, highlight, defocus, opacity and Swap Sources), the
      Xpress 3D effects through it, and the 3D Promote button for Picture-in-Picture. Done
      (docs/spec/3d-effects.md, `src/media/warp3d.cpp`, `src/model/promote3d.*`).
    * 6.7a2, the shapes (Ball, Page Fold, Slats) drawn as a mesh, the Foreground's key types and the other
      border types. Done (docs/spec/3d-effects.md).
    * 6.7a3a, Trail, Erode/Expand and Spill Suppression. Done (docs/spec/3d-effects.md).
    * 6.7a3b, Stamp: stamps kept by the compositor across segments until cleared, with the Effect Editor's
      Stamp Image and Clear buttons (p.298-299, p.584-585). Done (docs/spec/3d-effects.md).
    * 6.7a4, direct manipulation in the Effect Preview monitor for 2D (p.92) and 3D (p.292-293) effects, with
      Outline/Path. Done (docs/spec/3d-effects.md, `src/media/effect_handles.*`), so 6.7a is.
  * 6.7b, camera effects (ch.9, p.229-247), in two parts:
    * 6.7b1, Pan and Scan (source and target aspect ratios, origin, subdividing, the wire frame, with
      transitions). Done (docs/spec/pan-scan.md, `src/media/pan_scan.cpp`, `src/model/pan_scan.*`).
    * 6.7b2, Avid Pan & Zoom, in two parts:
      * 6.7b2a, the image files, field of view, display modes, filtering, background, pixel aspect and colour
        levels. Done (docs/spec/pan-zoom.md, `src/media/pan_zoom.cpp`).
      * 6.7b2b, camera speed (Velocity In and Out) and path changes at keyframes (Linear, Spline). Done
        (docs/spec/pan-zoom.md, `media::panZoomMotion`), so 6.7b is.
  * 6.7c, motion tracking (ch.10, p.248-275): our own tracking engine, the Tracking window, trackers mapped
    to effect parameters, offset tracking and regions, tracking data editing, SteadyGlide and smoothing. In
    three parts:
    * 6.7c1, the tracking data (trackers, regions, offset tracking, SteadyGlide, Smoothing, the reference frame,
      mappings to Position, Scaling, Rotation and Corner Tracking) and the engines. Done (docs/spec/tracking.md,
      `src/model/tracking.*`, `src/media/tracking.*`).
    * 6.7c2, the Tracking window, the Effect Editor's tracker controls, the Effect Preview's tracking boxes and
      generating tracking data. Done (docs/spec/tracking.md, `src/ui/tracking_window.*`,
      `src/ui/effect_editor_tracking.cpp`).
    * 6.7c3, editing tracking data (points, regions, the reference frame, the display range). Done
      (docs/spec/tracking.md).
    So 6.7c is done.
  * 6.7d, stabilization (p.275-284): Stabilize and Region Stabilize, editing segments that use tracking
    data, reusing and copying tracking data, the examples. In three parts:
    * 6.7d1, tracking data through segment edits (trims, splits) and copying and pasting trackers
      (p.281-284). Done (docs/spec/tracking.md, `model::shiftTrackingData`, `model::pasteTrackers`).
    * 6.7d2, the Stabilize effect: AutoStabilize, Stabilize Tracking (also in 3D Warp and on promotion),
      Auto-Zoom (p.275-279). Done (docs/spec/tracking.md, `model::stabilizes`, `model::applyAutoZoom`,
      `EffectEditor::autoStabilize`).
    * 6.7d3, the Region Stabilize effect: its region of interest, Model menu, Auto Zoom and Progressive
      Source (p.279-281). Done (docs/spec/tracking.md, `src/media/region_stabilize.cpp`).
    So 6.7d, and with it 6.7, is done.
* 6.8, titles (ch.12-13, p.301-377): the Title Tool and Titler+, with keyframes kept through trims,
  splits and duplicates (2026.8). SubCap (ch.14) goes to Phase 7 with the captions. Marquee (p.327) and
  NewBlue Titler Express (p.301) are other companies' tools: not ours. In five parts:
  * 6.8a, Avid Titler+'s title model and renderer: text, rectangle and ellipse objects with Font,
    Transform (anchor points), Fill, Outline and Shadow, z-order and groups, the Background and
    Foreground; the title effect on a segment or between marks (Tools > Avid Titler+), the "Title Text"
    Clip Text label (p.302-319, p.321-324). Done (docs/spec/titler.md, `src/model/titler.*`,
    `src/media/titler.cpp`).
  * 6.8b, editing titles in the Record monitor and the Dashboard (p.303-319, p.320-324), in two parts:
    * 6.8b1, the Record monitor's tools: Text, Selection, Rectangle and Ellipse, typing and selections of
      text, moving, scaling and rotating with their modifier keys, layer order, groups and the keyboard
      shortcuts. Done (docs/spec/titler.md, `src/ui/titler_tool.*`, `Composer::OverlayInput`).
    * 6.8b2, the Dashboard and the Effect Editor's selected object parameters (Font, Transform with
      alignment and anchor presets, Fill, Outline, Shadow, Background, Foreground), the Grid button. Done
      (docs/spec/titler.md, `src/ui/titler_panel.*`). Formatting selected characters moves to 6.8c.
  * 6.8c, Titler+ animation, in four parts:
    * 6.8c1, keyframes on title objects and the title, Fixed so that they stay with the title through
      trims, splits and duplicates (p.320, 2026.8 p.325; docs/spec/titler.md). Done.
    * 6.8c2, formatting selected characters (p.304, p.309; docs/spec/titler.md). Done.
    * 6.8c3, Roll and Crawl with the Effect Position, and tracking data on objects (p.324-326;
      docs/spec/titler.md). Done.
    * 6.8c4, in two parts: 6.8c4a, templates (the Frame view's still, Edit Template and dropping on the
      Record monitor, the Dashboard's Save and Save As, the save prompt; p.319-320; docs/spec/titler.md),
      done; 6.8c4b, templates as presets on parameters (p.320) and titles in EDLs (p.327;
      docs/spec/edl.md), done, so 6.8c is. Promote Titles
      from the classic Title Tool (p.326) moves to 6.8d, which makes the classic titles; titles in Find are
      dropped (neither guide says so).
  * 6.8d, the classic Title Tool (p.326-364), in four parts:
    * 6.8d1, the window: Clip > New Title and the New Title dialog, the drawing environment (video or
      colour background, Preview, Safe Colors, the safe areas, the alignment grid), the Selection, Text
      and Video Placement tools, text and its formatting, boxes, ovals and lines (p.327-341;
      docs/spec/title-tool.md). Done.
    * 6.8d2, rolling and crawling titles (p.341-346; docs/spec/title-tool.md). Done.
    * 6.8d3, modifying and manipulating objects (p.346-357), in two parts: 6.8d3a, corners, lines and
      borders, colour and transparency with blends, drop and depth shadows (p.346-355;
      docs/spec/title-tool.md), done; 6.8d3b, layers, grouping, locking, alignment and distributing
      (p.355-357), done.
    * 6.8d4, saving and reusing titles (p.357-364, p.326), in three parts: 6.8d4a, Title Effect clips with
      their fill and matte precomputes, the Title Tool's File menu (Save Title, Save Title As, Close, Export
      Title), the Save Title dialog with Fast Save, revising a title in a bin, Bin > Select > Unrendered
      Titles (docs/spec/title-tool.md), done; 6.8d4b, title templates (the Templates menu) and title styles
      (the Styles and Save Style Parameter buttons, the Title Style Sheet dialog, function keys, the Title
      Style settings), done; 6.8d4c, Promote Titles > Avid Titler+ (p.326), done. Place in Timeline (In -> Out) comes
      with 6.8e.
  * 6.8e, editing with titles (ch.13, p.365-377), in three parts:
    * 6.8e1, editing Title Effect clips into sequences: splicing, overwriting and dragging them in
      (marked and unmarked), replacing and removing, Place in Timeline (In -> Out) from the Save Title
      dialog, marks and trims of rolling and crawling titles, fading (p.365-371). Done.
    * 6.8e2, revising a title in a sequence (the Effect Editor's Edit Title button, the Timeline's Edit
      Title), the Title effect's parameters (Foreground, Scaling, Position, Crop) and replacing a title's
      fill track (p.371-374). Done.
    * 6.8e3, in two parts: 6.8e3a, rendering titles, Clip > Recreate Title Media and Timeline > Create
      Unrendered Title Media, the troubleshooting options (HDTitleFilter, Use Classic Character Mapping,
      Wrong Format; p.374-377), done; 6.8e3b, promoting titles to 3D (p.371), moved here from 6.8e2,
      done. So 6.8e and 6.8 are done.
* 6.9, intraframe effects (Paint, AniMatte, Blur, Scratch Removal; ch.15) and PlasmaWipe (ch.16), in five parts
  (docs/spec/intraframe.md):
  * 6.9a, the objects and their pictures: rectangles, ovals, polygons, curves and Brush strokes as Bézier control
    points with keyframed shapes and places, each with its own parameters; the Paint Effect's modes, Feathering with
    Bias, Magic Mask, Brush Size/Soft/Spin/Path, Object Visible and opacity; the Blur and Mosaic effects within their
    objects; AniMatte's Key In and Key Out on segments and transitions (p.391-399, p.416, p.439-441, p.493-497,
    p.532-548). Done.
  * 6.9b, the drawing tools in the Effect Editor and Effect Preview (p.395-418), in two parts: 6.9b1, the Selection,
    Bring Forward/Send Backward, Rectangle, Oval, Polygon, Curve and Brush tools; selecting (Shift, lasso), moving,
    scaling and deleting objects; Go to Next/Previous Event through objects; each object's parameters in the editor.
    Done. 6.9b2, the Z-Rotation and Reshape tools: rotating objects, control points and direction handles with the
    modifier keys, adding, removing, nudging and stepping through control points; the Object menu's layers, Group,
    UnGroup, Lock and Unlock. Done.
  * 6.9c, in two parts: 6.9c1, the Brush Shape buttons, the Brush Preview window and custom brush heads, brush
    templates in bins and the eyedropper (p.88-89, p.396-399, p.493). Done. 6.9c2, Export Matte PICT and the
    Previsualization Marker tool (p.412-413, p.418-419). Done: our own PICT writer and reader (`src/media/pict.*`, fuzz
    target `pict`), the markers and their optical events (`model::previsualizationEvents`). Importing the PICT back
    as a Matte Key clip goes with graphics import's alpha options (Phase 9), the optical list with film cut lists.
  * 6.9d, Scratch Removal: replacement material from nearby frames or fields, the Offset menu, frame and field
    processing, and the Scratch Removal button (p.419-429). Done: `media::scratchRemovalSource` with the segment's
    frames from the compositor (`IntraframeFrames`), `model::applyScratchRemoval` behind `fx.scratch_removal` (Tool
    palette, Command Palette FX tab), the Frame Processing button; scenario `37-scratch-removal.json`.
  * 6.9e, PlasmaWipe effects (ch.16, p.442-455). Done (docs/spec/plasma-wipe.md: `src/media/plasma_wipe.*`, our own
    generated gradients and the user's PlasmaWipes folders in the Effect Palette; test `tst_plasma_wipe`), so 6.9 is.
* 6.10, colour correction (ch.19-24, p.587-733), in five parts:
  * 6.10a, the corrections and their pictures: the colour correction effect's groups in their order with their
    enable buttons (p.598-605), in three parts: 6.10a1, the HSL group's Controls, Hue Offsets (ChromaWheels) and
    Luma Ranges tabs (p.631-644), done (docs/spec/color-correction.md: `src/model/color_correction.*`,
    `src/media/color_correction.*`, the Color Correction effect in the compositor; test `tst_color_correction`); 6.10a2, the Channels, Levels and Curves groups (p.644-667), done (the same files: `model::levelsValue`, `colorCurve`,
    `curveValue`; the groups applied in the tool's order); 6.10a3, the Secondary
    group's vectors (p.668-683), done (`model::secondaryVector`, `vectorMembership`, `syringeVector`,
    `addToVector`/`subtractFromVector`, `media::wheelHueSaturation`).
  * 6.10b, Color Correction mode (p.587-609), in four parts (planned 2026-11-03 from p.587-609):
    * 6.10b1, the mode and its monitors: Source and Program Segment corrections kept on segments and applied
      before other effects but Timewarps (p.597); entering and leaving the mode (p.589); the three monitors with
      their Source menus (Empty, Entire Sequence, Reference, Current, Previous, Next, Second Previous/Next, p.591;
      the waveform and vectorscope entries wait for 6.10c), Reference Current and the Escape key (p.593-594), Dual
      Split with its handles, Go to Previous/Next Uncorrected Shot and Remove Effect (p.592-593), Play Loop and
      Edit Review in the mode (p.594). Done (docs/spec/color-correction.md: `Segment::sourceCorrection`/
      `programCorrection`, `Timeline::setCorrectionMode`, `src/ui/correction_monitors.*`; tests `tst_correction_mode`,
      scenario `38-color-correction.json`).
    * 6.10b2, the Color Correction tool and the HSL group's panes: the Correction Type button (Source and Program
      Segment; the other relationships with 6.10d, the effect with 6.10e), the group and subdividing tabs with their
      Enable buttons, Alt+click resets and Page Up/Down (p.598-605), the Controls sliders, the ChromaWheels and the
      Luma Ranges graphs (p.631-644). Done (`src/ui/correction_tool.*`; test `tst_correction_tool`).
    * 6.10b3, the Channels, Levels, Curves and Secondary panes, in two parts (planned 2026-11-04): 6.10b3a, Channel
      Blending and previews (p.645-647), histograms and the Levels ChromaCurve (p.653-658), the Curves graphs with
      added and dragged points (p.659-663), done (`src/ui/correction_tool_panes.cpp`); 6.10b3b, the vector wheels, Selected Vectors panel, sliders, Lock Vectors
      and the Color Selection buttons (p.668-682), done (`src/ui/correction_tool_secondary.cpp`).
    * 6.10b4, in two parts (planned 2026-11-04): 6.10b4a, the Correction Mode settings (Tabs, Units, Levels,
      Features, p.606-609; Editing Guide p.1164-1167), done (the "Correction" User setting, `CorrectionTool::Options`,
      `UnitSpinBox`, `model::toCorrectionUnit`); 6.10b4b, the tool's buttons (p.605), the Command Palette's CC tab with
      its twelve Tab buttons (p.601-602), and Comments (p.609-610), done (`CorrectionTool::button`/`editComment`/
      `showPath`, `Segment::correctionComment`, Clip Text > Comments, `cc.tab_1`-`cc.tab_12`). So 6.10b is done.
  * 6.10c, in three parts (planned 2026-11-04 from p.618-631 and p.686-692): 6.10c1, the Waveform and Vectorscope
    monitors: the Source menu's Quad Display, RGB Histogram, RGB Parade, Vectorscope, Y Waveform (and HDR 1000/10000),
    YC Waveform, YCbCr Histogram and YCbCr Parade, for the active monitor (p.686-692; the safe colour marks come with
    6.10e), done (`src/media/scopes.*`; tests `tst_scopes`, `tst_correction_mode`); 6.10c2, the automatic corrections: Auto Balance, Black, Contrast and White in the HSL group, Auto Balance and
    Contrast in the Curves tab, the Remove Color Cast buttons, and the CC tab's buttons (p.618-622), done
    (`src/media/auto_correct.*`; tests `tst_auto_correct`, `tst_correction_tool`); 6.10c3, in two parts:
    6.10c3a, the Color Match control in each group with its Match Types, NaturalMatch and Show Eyedropper Info (p.624-629,
    p.631, p.608), done (`src/media/color_match.*`, `ColorMatchControl`; tests `tst_color_match`, `tst_correction_tool`);
    6.10c3b, custom colours in bins with Saved Color Labels and Eyedropper Picks from Anywhere in Application (p.630-631,
    p.608), done (`src/media/custom_color.*`, `customColorMime()`; tests `tst_color_match`, `tst_correction_tool`). So 6.10c
    is done.
  * 6.10d, in four parts (planned 2026-11-04 from p.596-598, p.610-616, p.683-686 and p.700-706): 6.10d1, the
    relationship corrections: the Correction Type menu's Src Tape, Src Clip Name, Master Clip, Sub Clip and Prog Track
    applying a correction to every related segment, the button following the segment's type, converting between
    relationships of one side (p.596-598), and the Timeline Fast menu's Color Correction with its indicator lines
    (p.616), done (`src/model/correction_relations.*`; tests `tst_correction_relations`, `tst_correction_tool`, scenario
    `39-correction-relationships.json`); 6.10d2, Color Correction effect templates, in two parts (2026-11-04): 6.10d2a, the Color Correction Effect
    Template button and Save Correction, the buckets C1-C8, applying by dropping on the monitor, a tab or a segment, by
    double-click and Enter, and which relationship each way uses (p.610-615), done (`src/model/correction_template.*`,
    `src/ui/timeline_correction.cpp`; tests `tst_correction_relations`, `tst_correction_tool`, scenario
    `40-correction-templates.json`); 6.10d2b, Color Correction Presets and the Effect Palette's automatic templates
    (p.611-612), done (our presets in `resources/effects.json`, `Timeline::automaticCorrections`; tests `tst_effects`,
    `tst_correction_tool`). So 6.10d2 is done;
    6.10d3, keyframing corrections, in three parts (2026-11-05): 6.10d3a, keyframes on corrections (which controls take
    them, p.685), played per frame, the tool following them and adjustments adding keyframes at the position (p.683,
    p.686), done (`src/model/correction_keyframes.*`, `Timeline::storedCorrection`; tests `tst_correction_keyframes`,
    `tst_compositor`, `tst_correction_tool`); 6.10d3b, the correction parameters in the Effect Editor (the HSL Controls
    and Hue Offsets, the Secondary sliders, ganged with the tool), where the first keyframes are added (p.683-686), done
    (`src/model/correction_effect.*`, `Timeline::setStoredCorrection`; tests `tst_correction_keyframes`,
    `tst_correction_tool`); 6.10d3c, a position bar under the Current monitor with the keyframe markers: select,
    delete, Alt-drag (p.686), and the Curves and Levels as keyframe tracks only (p.685), done (`CorrectionKeyframeBar` in
    `correction_monitors.cpp`, `Timeline::correctionKeyframeMarkers`; tests `tst_correction_keyframes`,
    `tst_correction_mode`, `tst_correction_tool`). So 6.10d3 is done; 6.10d4, Update, Merge, Flatten and Remove Correction from the tool's
    shortcut menu with their dialogs (ch.21, p.700-706), done (`src/model/correction_manage.*`,
    `correction_manage_dialog.*`, `Timeline::manageCorrections`; tests `tst_correction_manage`, `tst_correction_tool`,
    `tst_main_window`, scenario `41-update-correction.json`). So 6.10d is done. Correcting a whole sequence (p.692) uses the Color Correction
    effect and comes with 6.10e.
  * 6.10e, in five parts (planned 2026-11-05 from p.692-699 and ch.23-24): 6.10e1, the Color Correction effect as a
    correction: CC Effect in the Correction Type menu, the tool's adjustments going to the segment's effect (added at
    the first one, without the AutoCorrect corrections), the relationship correction applied first and its result the
    effect's input (p.693-694, p.696); the effect from the Effect Palette making the AutoCorrect tab's three corrections
    (the Correction setting's First/Second/Third Correction, p.695-696, p.622); a whole sequence corrected on an empty
    top track, filled with filler carrying the effect (p.692-693, p.695), done (`Timeline::setCorrectionOnEffect`,
    `putEffectCorrection`, `setAutoCorrection`; tests `tst_correction_tool`, `tst_compositor`, scenario
    `42-correction-effect-top-track.json`); 6.10e2, the Color Info tool (Tools > Color Info
    Tool, its shortcut menu's options and the tool's Color Info button, p.696-697), done (`src/ui/color_info.*`; test
    `tst_color_info`); 6.10e3, shape-based correction in the
    CC Effect: Shapes and Base layers, Show Alpha, Serial Mode, Show Result, Invert Alpha, Softness and trackers on
    shapes (p.697-699), in two parts: 6.10e3a, the shapes and the shape layer's correction on the effect and its
    picture, done (`src/model/correction_shapes.*`, `media::colorCorrectedShapesPicture`; test `tst_compositor`);
    6.10e3b, in three parts: 6.10e3b1, the tool's shape panel (Shapes/Base, Show Alpha, Serial Mode, Show Result,
    Invert Alpha, Softness), done (`src/ui/correction_tool_shapes.cpp`, `Timeline::setCorrectionLayer`,
    `Composition::shapeView`; tests `tst_correction_tool`, `tst_compositor`); 6.10e3b2, the shape tools drawing and
    selecting in the Current monitor, done (`src/ui/correction_shape_host.*`, the Intraframe tool's `IntraframeHost`,
    `CorrectionMonitors::setOverlayInput`; test `tst_correction_tool`); 6.10e3b3, the Color Correction effect in
    the Effect Editor and trackers on shapes (p.699), done (`EffectEditor::chooseShapeTracker`, `model::objectTracker`
    in `placedShapeAt`; tests `tst_intraframe`, `tst_intraframe_ui`), so 6.10e3 is done; 6.10e4, safe colours (ch.23, p.721-730), in three parts:
    6.10e4a, the Safe Colors project setting (limits with two decimals, units, Actions), its graph, the Color Correction
    tool's Safe Color Settings button (orange with Warn) and the limit checks, done (`src/model/safe_colors.*`,
    `src/ui/safe_color_graph.*`; tests `tst_safe_colors`, `tst_settings_window`); 6.10e4b, the warnings in the monitors
    (triangle and indicator bars), the Color Match swatches (RGB Gamut) and the Y Waveform, YC Waveform and RGB Parade
    colours, done (`media::pictureViolations`, `CorrectionMonitors::setSafeColors`, `ColorMatchControl::rgbWarnings`;
    tests `tst_scopes`, `tst_correction_mode`, `tst_correction_tool`); 6.10e4c, in two parts: 6.10e4c1, the Safe Color
    Limiter effect (p.471, p.545-546, p.728), its limiting and its parameters seeded from the Safe Colors setting, done
    (`src/model/safe_color_limiter.*`, `Timeline::setEffectSeed`; tests `tst_safe_colors`, `tst_compositor`,
    `tst_effect_mode`); 6.10e4c2, its Source monitor analysis display (p.729-730), done (`media::safeColorAnalysisImage`,
    `Composer::setMonitorOverride`, `EffectEditor::safeColorAnalysis`; tests `tst_scopes`, `tst_effect_mode`), so 6.10e4
    is done; 6.10e5, the Spot Color Correction effect and the Paint Effect's colour modes (ch.24, p.731-733), done
    (`spotCorrected`/`colorMatched` in `src/media/intraframe.cpp`; test `tst_intraframe`), so 6.10e is. Avid
    Artist Color (ch.25) needs Avid's EuControl software: a control surface provider hook only (EXTENSIONS.md), later.
  * 6.10f, after 6.10e (owner's report, 2026-11-05, Windows 11), done for the window sizes: the large window was the
    Titler+ Dashboard, 2700 pixels wide with its sections in one row; it now scrolls across and opens within the screen,
    and every floating panel and tool window is fitted to its screen (`fitOnScreen`, `keepOnScreen`; test
    `tst_main_window toolWindowsFitTheScreen`; BUILDING.md). The Effect Editor itself opened at its 340x520. The report:
    Tools > Titler+ and Tools > Effect Editor open as very
    large windows where a panel of ordinary size (or a page of a paneled window) is expected. Find why their panels open
    at that size (size hints, floating geometry, the workspace they open in), fix the root cause for every tool panel,
    add a BUILDING.md troubleshooting entry and a check (a test of each tool panel's opening size), and check Mac and
    Linux too. That Titler+ opens the Effect Editor with it is the guide's: "The Avid Titler+ Dashboard and the Effect
    Editor open" (FX Guide p.303). Also reported: after Tools > Titler+ the Record monitor changes size, and the
    Timeline's markers no longer show under the Record monitor (they stay in the Timeline and show in the picture at
    their frame) until the program is restarted. First look: Titler+ puts the Timeline in Effect mode
    (`MainWindow::openTitleEditing`, ours), whose Effect Preview resizes the monitor and whose position bar draws only
    the effect and its keyframes (`PositionBar::paintEvent`); check how Effect mode is left after titling, and what the
    guide shows under the Effect Preview (p.76-78) and on leaving Titler+. Found so far: Effect mode stays on after
    Titler+ and its Effect Editor close, so the Record monitor shows the Effect Preview (and no markers) whenever the
    position is on an effect; Source/Record Mode or Edit Mode gives the monitor back, as the owner confirmed. Toggle
    Source/Record in Timeline only shows the source clip in the Timeline (Editing Guide p.603, "Click the Toggle
    Source/Record in Timeline button"), so it leaves Effect mode on. The owner's Esc did nothing: the
    `edit.toggle_source_record` command (Esc, the Command Palette) had no handler; it now toggles as the button does
    (test `toggleSourceRecordCommandShowsTheSource`). Leaving Effect mode now always clears the Effect Preview and the overlay (`Timeline::setEffectMode`;
    tests `leavingTitlerRestoresTheRecordMonitor`, `titlerAndBackToSourceRecord`). Still to decide: whether closing
    Titler+ should leave Effect mode (the guide says the title is "directly viewed and manipulated in the Record
    monitor", p.303).
* 6.11, the OFX host in place of AVX (ch.6, p.157-164), with OCIO colour management, in three parts (planned
  2026-11-06 from p.157-164; AVX needs Avid's SDK, so OpenFX, BSD-3, takes its place as EXTENSIONS.md says):
  * 6.11a, the host's core: the OpenFX 1.5.1 headers in `third_party/openfx`; finding bundles in the standard OFX
    folders and `OFX_PLUGIN_PATH` (and `LUMENIS_OFX_PATH` for tests; `LUMENIS_NO_OFX=1` skips the scan) at start-up
    ("Do not add or remove plug-ins while Media Composer is running", p.158); loading them and the host's property,
    parameter, image effect, memory, multithread and message suites; the describe actions; plug-in effects in the
    Effect Palette "in their own category (usually the name of the plug-in vendor)" with a plug-in icon (p.157); the
    Console's reasons when one does not load (p.163); our own test plug-in. Done (docs/spec/ofx-host.md:
    `src/media/ofx_host.*`, `OfxHostProvider`, `EffectInfo::ofxPlugin` and the plug-in icon; tests `tst_ofx_host`,
    `tst_ofx_palette` with `tests/media/ofx_test_plugin.cpp`).
  * 6.11b, in two parts (2026-11-06): 6.11b1, rendering: a plug-in's parameters as the Effect Editor's controls (double,
    integer, boolean, choice, colour, 2D), keyframed like ours ("AVX 2.0 plug-ins automatically use advanced
    keyframes", p.157); the Filter, Generator and Transition contexts; images in and out at the project's size. Done
    (docs/spec/ofx-host.md: `ofxSegmentPicture`/`ofxTransitionPicture` from the compositor, `ofxEditorParameters`,
    instances and `closeOfxInstances`; tests `tst_ofx_host`, `tst_ofx_palette`). 6.11b2: a missing plug-in's effect
    blank in the Timeline and named in the Console (p.163); effect templates of plug-in effects (p.157); the Console's
    `closeallofxplugins` after `closeallavxplugins` (p.158). Done (the "OFX Plug-In" tag through `markOfxEffect`,
    `isMissingOfxEffect`; tests `tst_ofx_host`, `tst_ofx_palette`), so 6.11b is.
  * 6.11c, in three parts (2026-11-06, from p.161-162): 6.11c1, the General context's several inputs with the
    Optional Inputs dialog (p.161) and its inputs as the effect's nest tracks, the Angle control (p.162) and push
    buttons telling the plug-in (its own dialogs, "Custom dialog box only", p.161). Done (docs/spec/ofx-host.md:
    `OfxPluginInfo::inputs`/`connectedInputs`, `ofxInputCount`, `ofxPushButton`, `nestTrackPicture` in the compositor,
    `OfxOptionalInputsDialog`, `AngleDial`, `Timeline::setOptionalInputsChooser`; tests `tst_ofx_host`,
    `tst_ofx_palette`). 6.11c2, overlays through the Interact suite and the Draw suite with Outline/Path in the Effect
    Preview (p.161-162). Done (`ofxOverlayDraw`/`ofxOverlayPen`, `Composer::EffectPreview::pluginShapes`,
    `PluginOverlayInput` and `EffectEditor::pluginPen`; the project's size and render scale through
    `setOfxProjectSize`; tests `tst_ofx_host`, `tst_ofx_palette`). 6.11c3, colour management with OCIO (the OFX 1.5 colour suite) where the library is installed,
    else the plug-in sees the project's colour space only. Done (docs/spec/ofx-host.md "Colour management": OpenColorIO
    optional through `cmake/FindOpenColorIO.cmake` and `LUMENIS_OCIO`, `src/media/ocio_colour.*`; `setOfxColourSpace`,
    `ofxNativeColourspace`, `ofxColourStyle`, the clip preferences and output colour space actions; test `tst_ofx_host`
    with the halve plug-in), so 6.11c and 6.11 are.

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

**Steps** (planned 2026-11-06 from the Editing Guide's ch.25 "Exporting Frames, Clips, or Sequences", p.861-908, and
ch.26 "Generating Output", p.909-985, and the FX Guide's ch.14, p.378-390):
* 7.1, exporting pictures and sound through FFmpeg (ch.25):
  * 7.1a, File > Output > Export to File for QuickTime Movie, MOV and MP4 (p.869-870, p.879-881, p.893-899): the
    Export setting's dialogs for them, the sequence's frames through the compositor and its sound through the mix,
    Use Marks and Use Selected Tracks. Done (docs/spec/movie-export.md: `src/media/movie_export.*`,
    `MainWindow::exportMovieFile`, the Export setting's MOV and MP4 tab; tests `tst_movie_export`,
    `tst_movie_export_ui`).
  * 7.1b, stills, audio and image sequences: Graphic (frames), Audio (WAV, AIFF), DPX (p.890) and OpenEXR (p.900),
    Export with Mask Margins (p.900), H.265 (p.898). Done (docs/spec/movie-export.md: `media::writeStill`,
    `exportAudioFile`, `MainWindow::exportStills`, the Export setting's Graphic, Audio, DPX and OpenEXR tabs; H.265 came
    with 7.1a; Mask Margins waits for the mask regions, p.550; tests `tst_movie_export`, `tst_movie_export_ui`).
  * 7.1c, in three parts (2026-11-06): 7.1c1, AAF export's Export Methods: Copy All Media, Consolidate Media and Video
    Mixdown into the Media Destinations folder (p.870-872, p.1175-1177). Done (docs/spec/aaf.md "Export Methods":
    `MainWindow::prepareAafMedia`, `media::copyClipMedia`; test `tst_managed_media aafExportMethods`). 7.1c2, the Send To
    templates (p.863-867) with the Pro Tools options, Make New and .stt templates; export by dragging (p.868-869). Done
    but dragging (docs/spec/send-to.md: `src/model/send_to.*`, `send_to_dialog.*`, `MainWindow::sendTo`/`makeNewSendTo`,
    `exportOverride_`; `writeExport` split out of Export to File; test `tst_send_to`); dragging needs file promises. 7.1c3,
    Simplified AAF (p.884) and Export Media Difference (p.885). Done (docs/spec/aaf.md "Simplified AAF":
    `src/model/simplify_export.*`, the shipped Link To Effects Mixdown version through `SettingKind::versions`,
    `copyClipMedia`'s `exclude`; tests `tst_sequence`, `tst_managed_media aafExportMethods`), so 7.1c is but dragging. A Pro Tools Session (.ptx, p.872-878) needs Avid's
    unpublished format: a provider hook only (docs/EXTENSIONS.md).
* 7.2, MXF OP1a and AS-11 (p.886-892), XDCAM (p.883), AS-02 export volumes (p.901-904); gate 1 with bmx's checkers.
  In four parts (2026-11-06):
  * 7.2a, MXF OP1a, XDCAM MXF OP1a and MXF OP1a (legacy) through FFmpeg's OP1a muxer: DNxHR, ProRes, XDCAM-50/35/25/17,
    AVC Long-GOP G6-G50 and JPEG 2000 (p.886-890, p.1174), with DPX's options left from 7.1b (p.891). Done
    (docs/spec/movie-export.md "MXF OP1a": `MovieContainer::Mxf`/`Xdcam`/`MxfLegacy`, `media::setDpxHeaderInfo`; tests
    `tst_movie_export`, `tst_movie_export_ui`).
  * 7.2b, Export to Device > XDCAM and P2 (p.883-884, p.895, p.1187): a disk or card as a folder with the devices' own
    layout and naming (Cxxxx.MXF), the audio track rule. Done for XDCAM (docs/spec/export-device.md:
    `ext::ExportDeviceProvider`, `media::XdcamDiskProvider`, `ExportDeviceDialog`, `MainWindow::exportToDevice`; test
    `tst_movie_export_ui`); P2 cards wait for a module, since Panasonic's clip metadata is not published.
  * 7.2c, AS-11 (p.891-893): shims, descriptive metadata and segmentation from spanned markers. Done through bmx's
    bmxtranswrap where it is installed (docs/spec/as11.md: `src/media/as11.*`, the Export setting's AS-11 tab; test
    `tst_movie_export as11`).
  * 7.2d, AS-02 export volumes (p.901-904): the Export Volume bin, Commit, the bundle (manifest, shim, media, extra).
    Done (docs/spec/as02.md: `model::ExportVolume` in `Bin::exportVolume`, `ExportVolumeDialog`, the bin's volume bar,
    `MainWindow::commitExportVolume` through `media::writeAs02`; test `tst_movie_export_ui`), so 7.2 is; one bundle per
    sequence for now.
  AS-11 and AS-02 need DM and OP1b writing FFmpeg does not have: bmx's bmxtranswrap (BSD) rewraps our OP1a.
* 7.3, output to devices (ch.26, p.909-938): the output device and sync source, the Video Output tool, audio output
  options, the Digital Cut tool, output and timecode formats; a device provider interface with a file "device" of our
  own for tests, AJA (NTV2, MIT) and Blackmagic (the driver the user installs) behind it. Gate 2 waits for hardware (M).
  In three parts (2026-11-06):
  * 7.3a, decks and the Digital Cut tool (p.926-938): `ext::DeckProvider` with a tape kept in a folder of our own
    (the "file device" for tests and for laying a cut onto a file), the Digital Cut tool with Remote and Local, the Deck
    Selection menu, Entire Sequence, Safe Mode, Stop on Dropped Frames, Add Black at Tail, Insert/Assemble/Crash,
    Sequence Time/Record Deck Time/Mark In Time, Custom Preroll and Frame Offset, the track buttons, Preview, Play and
    Halt. Done (docs/spec/digital-cut.md: `ext::DeckProvider`, `media::FolderTapeDeckProvider`, `DigitalCutTool`,
    `MainWindow::digitalCut`; test `tst_digital_cut`).
  * 7.3b, the Video Output tool and audio output (p.910-924): video output devices (`ext::VideoOutputProvider`), Sync
    Lock, the output signal, calibration with the factory presets and test patterns, Recording Bars and Tone, the audio
    output options, Generate LTC On Playback. Done but LTC and direct bars and tone (docs/spec/video-output.md:
    `ext::VideoOutputProvider`, `VideoOutputTool`, `media::testPattern`, the project's Video Output setting, Output Gain;
    test `tst_video_output`); LTC needs an LTC OUT connector, so it goes with 7.3c.
  * 7.3c, hardware modules: AJA NTV2 (MIT) and Blackmagic Desktop Video behind the 7.3a-b interfaces, built where their
    SDKs are installed; gate 2 waits for hardware (M).
* 7.4, captions: the SubCap effect (FX ch.14) with EBU N19 STL and Avid DS caption files, Scenarist SCC import
  (p.232) and caption data tracks, SCC and TTML export. In two parts (2026-11-06):
  * 7.4a, the SubCap effect (FX p.378-388, p.501-504): its picture, Import and Export Caption Data (Avid DS and
    EBU N19), the Master Caption List, Global Properties with stylesheets and Synchronize. Done
    (docs/spec/subcap.md: `src/model/subcap.*`, `media::subcapPicture`, `src/ui/subcap_dialogs.*`,
    `MainWindow::subcapAction`; tests `tst_subcap`, `tst_subcap_ui`).
  * 7.4b, closed captions (p.232, p.973-985). "SRT" in the Editing Guide is the streaming protocol, not SubRip
    subtitles. In two parts:
    * 7.4b1, SCC import through the AncData Import dialog into D-track only clips, CEA-608 decoding, the D1 row's
      loading and playing phases and broken captions, Show Captions in the monitors, SCC and TTML export. Done
      (docs/spec/closed-captions.md: `src/model/closed_captions.*`, `MainWindow::importScc`, `Timeline::captionTrack`,
      `Composer::shownCaption`; tests `tst_closed_captions`, `tst_closed_captions_ui`, fuzz target `scc`).
    * 7.4b2, the Timeline Quick Find (p.645-646) with its search options, closed caption text among the Visible
      Timeline Text (p.985). Done (docs/spec/timeline-find.md: `src/ui/timeline_find.cpp`; test `tst_timeline_find`),
      so 7.4 is.
* 7.5, DCP ("Output to DCP requirements for theatrical releases", p.519) through an open library, IMF export later.
  Done (docs/spec/dcp.md: Export As DCP with its tab, ours; `media::writeDcp` with JPEG 2000 frames from FFmpeg's
  OpenJPEG, asdcplib's asdcp-wrap for the SMPTE MXF track files and our CPL, PKL and Asset Map; tests `tst_dcp`,
  `tst_movie_export_ui exportDcp`). Validating with a DCP checker (gate 1) waits for asdcplib on a runner.
* 7.6 (the owner's report, 2026-11-06, after 7.5): the Titler+ window and the Effect Editor do not look like the FX
  Guide's pictures. Titler+ in the guide has two columns of tool icons on the left; ours has text buttons, some side by
  side, which make it as wide as a 1920-pixel screen on Windows. Lay both out as the guide's figures show them (read
  the figures with `pdftoppm`), check their widths on a 1920 x 1080 screen in a test, and add a BUILDING.md entry.
  Done (docs/spec/titler.md: `TitlerPanel` laid out as p.302 in the Dashboard and as p.321 in the Effect Editor, our own
  icons as `GlyphIcon`s, the Effect Editor's side buttons as icons; test `tst_main_window toolWindowsFitTheScreen`,
  pictures through `LUMENIS_SHOT_DIR`). Still different from the pictures: the Effect Editor's parameters are fields, not
  the guide's sliders with numbers, and the keyframe area beside them is not shown for Titler+.

**Gates**
1. Exports validate against the relevant spec checkers (AS-11 DPP, DCP checker
   and others).
2. Broadcast output frame-accuracy is confirmed on hardware **(M)**.

### Phase 8 — Advanced workflows
* Multicam (group/multigroup), ScriptSync/PhraseFind equivalents, Dynamic Media
  Folders/background services, FrameFlex, stereoscopic, Symphony-level colour,
  and multi-user bin locking.
* In steps (planned 2026-11-07, from the Editing Guide's chapters):
  * 8.1, MultiCamera Editing (ch.32, p.1123-1136). 8.1a, group clips, in two parts. 8.1a1 (done,
    docs/spec/multicamera.md): Group Clips by IN points, OUT points, Source Timecode or Auxiliary TC1-5 (p.1123-1125),
    the group in the Source monitor full size and in Quad and Nine Split views with Swap Cam Bank (p.1126-1129),
    groups edited into sequences with "(G)", Previous In Group and Next In Group in both monitors (p.1131). 8.1a2 (done):
    Waveform Sync and Waveform Analysis grouping (p.1124, p.1126), Edit Group, Update Group, Create Group from a
    sequence and Refresh Sequence > Group Clips (p.478, p.1125-1126). So 8.1a is done. 8.1b, MultiCamera mode, in two
    parts. 8.1b1 (done): the mode, switching angles with the numeric keypad and mouse, live cuts while playing, banks
    (p.1129-1132), the Composer settings' MultiCam tab (p.1160-1161). 8.1b2 (done): the linecut in the split, the
    Group menu with Audio Follow Video and the Multi-angle View menus (p.1132-1134), Match Frame (p.1134), Commit
    Multicam Edits (p.1135). 8.1b3 (done): multigroups (p.1125), so 8.1b is done. 8.1c (done): the Group menu's patching of any angle's tracks (p.1133) and Change Source Track for group and multigroup
    clips (2026.8 guide p.507). Left: stereoscopic groups (p.1124) with stereoscopic projects in 8.6 (AutoCut from
    transcript speakers came with 8.2e).
  * 8.2 (done), transcripts and PhraseFind (ch.14, p.436-453; 2026.8 guide p.446-447, p.451-452), in five parts (planned
    2026-11-07):
    * 8.2a (done, docs/spec/transcripts.md), transcripts and the engine: transcripts of master clips (words with times, lines with Speaker IDs, the
      original kept beside edits) in the project's transcript store, keyed by the clip's MobID; speech to text through
      `ext::TranscriptionProvider`, built in as whisper.cpp's `whisper-cli` found like bmxtranswrap
      (`LUMENIS_WHISPER`, its model file), speaker turns from its tinydiarize option; background transcription on the
      Background Queue with Include/Exclude Bin in Transcription and "Transcription On/Off" in the sidebar (p.446-447),
      pausing while editing (p.447); the Transcript Project setting: Include all new bins, Pause background
      transcription, Language Hint (also on the New Project tab, p.448), Subclips/Groups (p.449-450), Manage Speakers.
    * 8.2b, the Transcript Tool (p.442-450), in two parts: 8.2b1 (done) the tool itself (what is shown for clips, subclips,
      groups and sequences, the playhead word, clicks and selections, Shift+arrows, search, the fast menu's display
      items, Transcribe, the Source/Record and track buttons); 8.2b2a (done) editing text, Speaker IDs, Find and Replace,
      Group Speaker Lines, New Line After, Start New Line, Revert; 8.2b2b (done) Manage Speaker Names and the New Project
      tab's Language Hint. In full: ganged to the Source or Record monitor, the playhead word, clicks and
      selections setting the position and marks, Shift+arrows, edits with V and B, copy, the fast menu (Timecode,
      Speaker ID, Show Not Transcribed, Set Font, Transcribe Remaining Clips), search, the Source/Record button, the
      audio track button for sequences and groups; Speaker IDs (rename, Identify Speaker, Correct Speaker ID, Manage
      Speaker Names); editing text with word timing kept (p.445-446), Find and Replace; 2026.8's Group Speaker Lines,
      New Line After, Start New Line, Revert and Revert Transcript (2026.8 guide p.446-447).
    * 8.2c (done), PhraseFind AI in the Find window (p.436-441): the index of the project's transcripts, the PhraseFind AI
      button, Score and Transcription columns, the index status, Load into monitor with the mark IN before the
      dialogue, the Search Settings (indexing on/off, Manage Transcripts).
    * 8.2d (done), transcripts out (p.450-453; 2026.8 guide p.455-456): the bin's Transcript menu (Export Transcript as .txt,
      Share Transcripts, Delete Transcript, Create SubCap and Export SubCap with the Transcribe window's tracks, Use
      Marks and Limit characters per caption), Manage Transcripts (delete, import). Shared transcript files are ours
      (Avid's .avt is undocumented); peer-to-peer sharing over NEXIS waits for 8.7.
    * 8.2e (done), AutoCut from transcript speakers (2026.8 guide p.451-452): speakers assigned to a group's angles, AutoCut
      on a group segment.
  * 8.3 (done), ScriptSync (ch.18, p.566-590; the Script setting p.1217), in five parts (planned 2026-11-07):
    * 8.3a (done), scripts and the Script window (p.570-576): File > New > New Script importing a text file into a script item
      in the active bin, the script kept in the project folder (ours: `.cthscript`; Avid's .avc is undocumented) with
      the attic and auto-save as bins (p.570); the Script window (a panel) with Save Script, Save a Script Copy As,
      Close, the asterisk; the Script setting (font, size, left margin, Word Wrap, Show Line Number, background,
      pre-roll, the slate options stored for 8.3b); Script > Text Layout (left margin, text width, word wrap); Add Scene
      and Add Page with the Delete dialog's scene and page choices, the Scene and Page menus and the status bar; Edit
      Mode with cut, copy, paste, typing and Undo Edit Script Text, and the Show Find Tools find and replace.
    * 8.3b (done), slates and takes (p.576-582): clips dragged onto selected lines become slates of takes (frames, clip names,
      Take column numbers); selecting, lassoing, moving slates (Ctrl+drag with their lines), changing a take's length,
      deleting takes; Show Frames, Show All Takes, Hold Slates Onscreen, Enlarge/Reduce Frame; the representative
      frame stepped with the arrow keys; double-click and the Play button loading and playing takes; Interpolate
      Position; off-screen and colour indicators (Set Offscreen, Script > Color, Set Color); Alt+click for the Info
      window.
    * 8.3c (done), script marks (p.582-588): Add Script Mark (the Other tab), double-click at a take and line, the Record
      button's real-time screening and marking (Tab/Shift+Tab, marks extending takes), loading a marked segment with
      its IN and the pre-roll, moving and deleting marks, Find Script and Find Bin, splicing a script range
      (Ctrl+Alt+double-click), the rough cut workflow.
    * 8.3d (done), ScriptSync AI (p.584-585) and Create Script from Clip (p.569-570): the dialog (tracks, the skip options,
      Overwrite existing marks, Sync between first and last mark) aligning script lines with the words of 8.2's
      transcripts; a script made from clips' transcripts, its lines already marked.
    * 8.3e (done), shared scripts (p.589-590): the script lock icons and refresh on shared storage through 3.7's locks, the
      Cancel choice when closing a locked script; the Find window's Script Text tab and Scripts in Project (p.428-430).
  * 8.4 (done), Dynamic Media Folders and the background services (p.371-377), on the Background Queue, in three parts (planned
    2026-11-08; docs/spec/background-services.md):
    * 8.4a (done), the background services: Tools > Background Services (Start, Stop, Pause for a time, Always Start at
      Launch, Always Stop upon Exit, Enable Pausing; p.376-377), the Background Queue starting jobs only while they are
      active and pausing during playback, the Timeline's progress indicator with its states and Generate Error Report in
      Console (p.375-376).
    * 8.4b (done), Dynamic Media Folders and profiles (p.371-375): folders with a profile and Enable, profiles of Link Settings
      (Link Using, the bin choices and naming, Multichannel Audio, the AIS options) and ordered Actions (Copy to Folder
      with Auto Relink, Consolidate, Transcode with their options), files added to an enabled folder linked and processed
      on the Background Queue.
    * 8.4c (done), the Dynamic Media Folders window and the Profile Editor (p.372-376): + to add a folder, the Profile Editor's
      menu bars and actions reordered by dragging, Save and naming, the profile menu, Enable, the indicator's green dot
      and the Acquire column putting the assets into bins (offline while processing, .new for consolidated or transcoded
      clips); DPX folders as clips.
  * 8.5, FrameFlex and colour adapters (p.527-545): Source Settings, reframing and panning, Reformat, LUTs and CLF
    through OpenColorIO, CDLs, Remove Color Adapters, ACES projects (p.557). In five parts (planned 2026-11-08;
    docs/spec/frameflex.md):
    * 8.5a (done), Reformat and spatial adapters (p.475-478, p.528-535): the clips' Reformatting Options column and the
      Link Options' default (Center Keep Size, p.475, p.1204), FrameFlex boxes kept with clips, the spatial adapter on
      segments drawn from whole frames (Stretch, Pillarbox/Letterbox, Center Crop, Center Keep Size; the box's position,
      size, rotation, Image Size and aspect ratios), the green dot, Refresh Sequence > Aspect Ratio and Reformatting
      Options and All FrameFlex including creative framing.
    * 8.5b (done), the Source Settings dialog and panning, in three parts:
      * 8.5b1 (done), the dialog's FrameFlex tab (p.527-535): from the bin's and the Source monitor's right-click Source
        Settings (p.316, p.529); Show Viewers with the top viewer's box, moved, resized and rotated by dragging, and the
        bottom viewer's result; the fields, Size matches project raster, Image Size = raster, Rotate Left/Right, Reset,
        Revert, Apply and OK on every selected clip.
      * 8.5b2 (done), the Timeline's right-click Source Settings (p.542, p.545) and the Playback Rate tab (p.545: the
        clip's original rate or the project's, with Refresh Sequence > Motion Adapters/Timewarps dropping the adapters).
      * 8.5b3 (done), panning: the FrameFlex effect in the Effect Editor with keyframed boxes in the Record monitor, the
        Layout button's bypass and Alt+Layout reset (p.533-534), creative framing for All FrameFlex.
    * 8.5c (done), colour adapters, in two parts (8.5c1 done: the encoding on clips, the built-in transformations, the
      Color Adapter on segments, the column and the two Refresh Sequence commands; 8.5c2 done: the Color Encoding tab
      and Transcode's option; docs/spec/color-adapters.md; p.536-541, p.544): the Color Encoding tab (Source color space,
      Auto, the transformation list with Add, Delete and dragging, Bypass all color transformations, Revert), the built-in transformations
      (levels scaling, gamma 2.6, linear, Cineon, sRGB, Log-C, C-Log; the Sony looks through a provider unless public
      curves exist), the Color Adapter on segments, the Color Transformations column, Refresh Sequence > Color
      Adapters and Remove Color Adapters, Transcode's Apply source transformations > Color encoding.
    * 8.5d (done), LUTs, CLF and CDLs (p.539-544), in two parts (8.5d1 done: our LUT readers, the installed LUTs as External
      transformations, the CDL transformation and the fuzz target `lut`; 8.5d2 done: the setting and CLF): the Color Management setting (Insert color transformations automatically,
      Use CDL values from ASC_SOP and ASC_SAT, Installed LUTs Project/Shared/Both, Select LUT file), our readers for
      the listed 1D and 3D LUT formats, CLF through OpenColorIO where it is installed, the ASC_SOP and ASC_SAT columns
      and the CDL transformation.
    * 8.5e, in four parts: 8.5e1 (done), Display Color Space on the monitors (p.544); 8.5e2 (done), the proxy mode (the Format
      tab's Proxy 1/4 and 1/16, p.526-527, p.549-550: pictures and renders at the proxy raster, renders kept per proxy
      mode); 8.5e3 (done), the Format tab's Mask Margins and the monitors' Target Mask (p.550-552, the Composer setting's Target
      Mask p.1191, Export's Enable Mask Regions); 8.5e4, in two parts: 8.5e4a (done), the Composer setting's Viewer
      tab Color Space options (p.1161: Project, Rec709, Rec709 (full range), SRGB, DCI P3, Rec2020, ACESproxy) as Display
      Color Space's choices; 8.5e4b, ACES projects, in two parts: 8.5e4b1 (done), the ACES encodings and
      colour adapter types, the ACEScct working space and the monitors' ACESproxy and output transform (p.557-559, built
      in from the published formulas; OpenColorIO 2.1 has no built-in ACES config); 8.5e4b2 (done), the Format tab's
      ACES version and Output Color Space on export, the blue Video Quality Menu, Transcode in the working space
      (p.558-560); the Project tab's video output colour space waits for the Video Output modules (7.3c). So 8.5e and
      8.5 are done.
  * 8.6, MultiRez and Dynamic Relink (ch.31, p.1095-1121) and stereoscopic projects, in four parts. Our managed media
    stand in for the Media Indexer: a clip's media at other resolutions are the files that name its MobID.
    8.6a (done), MultiRez media and the Dynamic Relink Project setting (p.1099-1110, p.1120-1121, p.1169-1171): a master
      clip's media at several resolutions, Transcode without Create new clips (p.354-355), the setting's Working and
      Target tabs, quality matching, relinking clips loaded in the Source monitor and the Timeline, Override Working
      Settings with Target Settings, Allow Mixed Frame Rate Media.
    8.6b (done), the MultiRez button and its menu (p.1111-1116): Show Mismatches and Show Target Availability, Enable Clip
      Coloring with Resolution Tracking, Find Next Mismatched or Unavailable Clip (Ctrl+/), Update Media Status, Show
      Mismatched Render Ranges, Clip Text > Clip Resolutions (p.1104).
    8.6c (done), partial and shared media (p.1100-1102, p.1116-1120): the MultiRez bin headings with full, half and empty
      circles, clips sharing a source and timecode span, partially online media, the Delete dialog's affinity
      resolutions, Cross Rate Dynamic Relink (p.1109-1110).
    8.6d, stereoscopic projects, in three parts. The Editing Guide leaves "Working with Stereoscopic Clips" to the Help
      (p.213), so how stereoscopic clips are made is ours; the project's options, the displays and the settings are in the
      guide (p.65-66, p.243, p.403, p.478, p.562, p.959, p.1124, p.1162-1163, p.1190-1191, p.1216, p.1227).
      8.6d1 (done, docs/spec/stereoscopic.md), stereoscopic clips and the project's Stereoscopic option: Stereo Master Clips made from a left and a right
      eye master clip (p.213, p.243) with the S3D Leading Eye attribute, and pictures by the project's mode (Off,
      Leading Eye, Left Eye Only, Right Eye Only, Side by Side, Over / Under, Full; p.65-66).
      8.6d2 (done), the S3D views and overlays: the Composer setting's Source and Record Monitor S3D View and S3D Overlay
      (Mix, Difference, Frame Compatible, Mono, Anaglyph, B/W Anaglyph, Checkerboard, Compare; parallax guides,
      p.1162-1163), Full Screen Playback's (p.1190-1191), the Video Output tool's (p.1227), the S3D Settings Project
      setting with its parallax worked out (p.1216).
      8.6d3 (done), the rest: stereoscopic clips in group clips (p.1124), the List Tool's S3D Contributor (p.959), Refresh
      Sequence > Stereo Correction Effects (p.478), the 2048 x 1080 S3D preset (p.562), interlaced stereoscopic
      playback at Full Quality only (p.403).
  * 8.7, what is left of Symphony-level colour and multi-user work after 6.10 and 3.7, in four parts (planned
    2026-11-10, from the Editing Guide; 3.7 has the shared bins and projects of p.300-304, 6.10 the Symphony Option colour
    correction):
    * 8.7a, proxy media (ch.16, p.508-513), in two parts, done (docs/spec/proxy-media.md): 8.7a1 the proxies, their
      playback and the bin's columns; 8.7a2 Copy Media (`src/media/copy_media.*`, `CopyMediaDialog`,
      `MainWindow::copyMedia`) and the provider hook for shared workspaces and Distributed Processing
      (`ext::SharedStorageProvider`). In full: Create Proxies from the bin's right-click and Fast menus with its window (a
      local drive or a shared workspace; Application or Background Process), proxy media at Avid DNx LB and H.264 by the
      Timeline's video quality (p.508-510), the orange clip icon, the Play button's High-Resolution Only, Proxy Preferred
      and High-Resolution Preferred with the orange Play button (p.509), the bin's Proxy Offline, Proxy Video, Proxy Audio
      and Proxy Path columns (p.509-510), proxies in sequences (p.512) and Copy Media gathering managed media onto one
      drive (p.512-513). Distributed Processing and Avid NEXIS | EDGE (p.510-511, p.514-516) need Avid's servers: a
      provider hook only.
    * 8.7b, peer-to-peer transcript sharing (p.451-452), done (docs/spec/transcripts.md): Host Shared Transcripts and Use
      Shared Transcripts with our transcript store on shared storage (as LUMENIS_SHARED_BINS finds it), entries locked
      while written with the override prompt, deletes for everyone, the host's store read when the host is off.
    * 8.7c, sequence format changes and Universal Mastering (ch.36, p.1287, p.1294-1298, Symphony Option): Changing the
      Project and Sequence Format, 24p NTSC to 1080p/23.976 and 23.976p NTSC to 720p/23.976 (p.1294-1295), Editing at 60
      fps and showing 24p and 25p media (p.1295-1296), HD Universal Mastering with its audio conversion and Digital Cut
      (p.1297-1298).
    * 8.7d, the Symphony Option items left in both guides after an audit (redefining a title's font replacement,
      p.994, among them); control surfaces (Avid Artist Color, ch.25 of the FX Guide) stay a provider hook
      (EXTENSIONS.md).
* From 2026.8:
  * the Transcript Tool's versions (Revert, Revert Transcript), Start New
    Line, Group Speaker Lines and New Line After (2026.8 guide p.446);
  * Multicam Auto Cut from transcript speakers (2026.8 guide p.451,
    p.1134);
  * Change Source Track for group and multigroup clips (2026.8 guide
    p.507): built in 8.1c.

### Phase 9 — Left over stuff
Items that fit no other area or phase, done before hardening (owner's
decision, 2026-10-27):
* printing: Page Setup, Print Bin, Print Frame and Print Timeline
  (p.288-292, p.645);
* Batch Import and Batch Re-import (p.1098, p.230);
* AutoSync (p.223);
* importing graphics with the Alpha options Use Existing, Invert Existing and Ignore, making Matte Key clips (FX Guide
  p.418-419, Editing Guide "Importing Media Files"), and reading PICT files there (`media::readPict`, 6.9c2);
* film cut lists and optical lists in the List Tool, with Effect Grid coordinates and the Previsualization Markers'
  events (FX Guide p.79, p.412; Editing Guide "Optical Lists");
* importing from audio CDs, with the Import setting's Apply
  attenuation/gain effect on import and its CD only choice (p.1198);
* a check for unconnected items (owner's request, 2026-11-05): commands with no handler, buttons, menu items and
  keys that reach nothing, and places where the guides use slightly different names for one item (Esc's "Toggle
  Source/Record" and the Timeline's "Toggle Source/Record in Timeline" button, p.480 and p.603, were one button but
  only the button worked). Go through commands.json, menus.json, keyboard.json and command_palette.json against the
  handlers, list the guides' names that differ for one item in docs/spec/ui-inventory.md, and add a test that fails
  when a command reaches nothing unless it is listed as dimmed on purpose;
* Export to File of a sequence as MOV or MP4 made no file on Windows 11 (owner's report, 2026-11-08). Found: the
  Windows FFmpeg (BtbN's LGPL build) had no H.264 or H.265 encoder and the reason went only to the Console. Fixed:
  a failed export says why in a message; `scripts/get-ffmpeg.ps1` fetches the GPL build with libx264 and libx265 and
  checks them; CI requires them (`LUMENIS_REQUIRE_ENCODERS`, `tst_movie_export requiredEncoders`); BUILDING.md "Export
  says there is no H.264 encoder". Left: check the Options button (it opens the Settings window) against the guide
  (p.865-869);
* error messages everywhere (owner's request, 2026-11-08): whatever fails, is missing or cannot be done tells the
  user in a message, not only the Console. Go through every place that writes a failure to the Console (`ConsoleLog`)
  or returns quietly (commands that do nothing, missing media, files that cannot be read or written, missing encoders,
  tools and plug-ins), show a message there (non-modal where a test or script may run it), and add a test that a
  failed command with no message is caught;
* anything else later found not to fit another area;
* last, a trademark check (owner's request, 2026-11-09): Avid marks some feature names as trademarks (AutoSync,
  for one, carries a ™ in places in the Editing Guide), so we must not use them as our menu, command, window or
  function names. Go through every user-visible name (commands.json, menus.json, command_palette.json, settings.json,
  window and panel titles, the specs and BUILDING.md) against Avid's list at
  https://www.avid.com/legal/trademarks-and-other-notices and against the names the guides mark ™ or ®. Rename what
  is marked to a generic name (AutoSync becomes "Sync Clips"; others such as ScriptSync, PhraseFind, MultiRez,
  FrameFlex, Titler+ and Media Composer itself to be checked). Keep the guide's name in the spec as the source the
  item follows, with our name beside it; update tests/data/menu_inventory.json and the checklist; and add a test that
  fails when a listed trademark appears in a user-visible string. Do the same for other companies' editing and
  finishing products, so no name is one another company owns: Adobe (Premiere Pro, After Effects, Audition; e.g.
  Warp Stabilizer, Morph Cut, Lumetri), Apple (Final Cut Pro, Motion, Compressor; e.g. Magnetic Timeline), Blackmagic
  Design (DaVinci Resolve, Fusion, Fairlight; e.g. Speed Warp, Magic Mask), and others the check turns up (Grass
  Valley EDIUS, Magix Vegas, Autodesk Flame, Boris FX). Use each company's published trademark list and the USPTO and
  EUIPO trademark searches; keep the lists checked, with their dates, in docs/spec/trademarks.md, and put their names
  in the test too. Names of open standards and formats we read or write (AAF, MXF, OpenTimelineIO, EDL, DNxHD/DNxHR
  as codec names, ProRes, H.264) are used to name the format only, as the owners allow, and the doc records why each
  is allowed.

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
