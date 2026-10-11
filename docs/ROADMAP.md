# Lumenis — Project Plan and Roadmap

Status: **proposal**. The decisions in §2 need sign-off before Phase 0 code lands.

## 1. Goal and honest scope

Lumenis is a clean-room, cross-platform non-linear editor that works like Avid
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

### 2.1 Language and GUI toolkit — recommendation: C++20 + Qt 6 (Widgets)

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

### 2.2 Licence

The repo is currently **GPLv3**. That works for "free build plus paid support"
(the Red Hat model): anyone can sell binaries and support. Things to know:

* Every dependency must be GPLv3-compatible. MIT, BSD, Apache-2.0, LGPL and
  MPL-2.0 are all fine. Avid's AAF SDK licence is doubtful, so we would write
  our own AAF layer, ported from or modelled on the MIT-licensed pyaaf2.
* If you ever want a closed "enterprise" edition, you need either full copyright
  ownership through a Contributor Licence Agreement (CLA) from day one, or a
  different licence. Decide this **before** outside contributions arrive.

### 2.3 Reference Media Composer seat

Pass/fail interop tests need a legally licensed copy of Media Composer to
produce reference bins, AAFs and media, and to confirm that our output opens in
it. The person who runs it does black-box observation only: no disassembly and
no debugger. Check that the EULA allows this.

### 2.4 Target version

Proposal: match the **Media Composer 2024.x/2025.x** UI and default keyboard,
and read bins from versions 8.x and later.

### 2.5 Name

"Lumenis" is a registered trademark of a medical-device company. That is a
different trade class, but run a trademark search before public release.

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
   * Codec patents are the real risk. H.264, HEVC and AAC encoding go through
     OS frameworks: VideoToolbox, Media Foundation, and VA-API or the user's
     own FFmpeg on Linux. Otherwise we take a pool licence for the paid
     edition.
   * ProRes encode/decode needs a review by legal counsel.
   * DNxHD/VC-3 patent status needs a review by legal counsel.
6. Avid trademarks are used only descriptively ("opens Avid Media Composer
   bins"), never as branding.

## 4. Architecture

```
app/                 executable, startup, crash reporting
src/core/            rational time, timecode, IDs (MOB/UMID), undo, settings, threading
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

* Build: CMake with presets, and vcpkg in manifest mode for pinned dependencies.
* CI: GitHub Actions on `macos-14` (arm64), `windows-2022` (x64) and
  `ubuntu-24.04` (x64).
* Every edit operation is a serialisable command. That one design gives us
  undo/redo, scripted tests and, later, a scripting/automation API.

## 5. Phases and pass/fail gates

Every gate is automated in CI unless it is marked **(M)** for manual. A phase is
done only when all of its gates pass on **all three platforms**.

### Phase 0 — Foundations (repo, build, CI, packaging)
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

### Phase 1 — UI shell: every window, no media
These windows are built using the Media Composer User Guide layout:
* Select Project dialog.
* Project window: Bins, Settings, Format, Usage, Info and Volumes tabs.
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
2. A window/menu inventory, generated from the user guide, has 100% of entries
   present. A test walks the menu tree and diffs it against
   `tests/data/menu_inventory.json`.
3. Every key in the default keyboard map matches the published Media Composer
   default. This is a table-driven test.
4. Workspaces and window layouts save, restore and survive a restart.
5. An experienced Media Composer editor finds every window and menu command
   without help **(M)**, using a checklist.

### Phase 2 — Media I/O and playback
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
   8.x to current) opens in Lumenis. Clip count, names, timecodes, custom
   columns and sequence structure must be identical. This is checked with a
   structural diff tool.
2. Bins saved by Lumenis open in Media Composer without errors, with the same
   checks in reverse **(M → automated by corpus)**.
3. AAF round-trips (MC→Lumenis→MC and Lumenis→MC→Lumenis) have no structural
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

1. Sign off on §2. The critical decisions are the toolkit, the licence/CLA and
   the reference seat.
2. Phase 0 scaffold: CMake/vcpkg, the Qt app, CI for 3 platforms, build
   scripts, BUILDING/INSTALL docs and installers.
3. Start `docs/spec/ui-inventory.md`, a window-by-window and
   menu-by-menu inventory taken from the public Media Composer User Guide.
   It drives Phase 1 gate 2.
4. Start assembling the media and bin test corpus.
