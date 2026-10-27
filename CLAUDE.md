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
- Run the sanitizers before pushing changes to object lifetimes or
  ownership: `cmake --preset asan && cmake --build --preset asan && ctest
  --preset asan` (AddressSanitizer and UBSan). glibc tolerates
  use-after-free that crashes Windows (0xc0000374); the self-hosted Linux
  job runs them too.
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
(`export_dialog.*`, the Export setting). 3.3, `.avb` bins (docs/spec/avb.md), has started: 3.3a, the object reader
(`src/interop/avb.*`, `--dump-avb`, ported from pyavb), is done; ctest
`avb_pyavb` compares it with pyavb (`scripts/avb-dump.py`). 3.3b, bins
into the native model (`avb_import.*`, `--import-avb`; helpers shared with
the AAF import in `import_common.h`), is done; ctest `avb_import_bins`
compares it with `tests/data/aaf_reference`. File > Open Bin takes a `.avb`
and lists a native copy under Other Bins (`MainWindow::openBinFile`). 3.4,
`.avb` bin write, has started: 3.4a, the object writer (`avb_writer.*`,
`avb::Builder`, ported from pyavb's write()), is done; every corpus bin
rewrites byte for byte (`--rewrite-avb`, ctest `avb_round_trip`) and pyavb
reads what we build (ctest `avb_write_pyavb`). 3.4b, native bins out as
`.avb` (`avb_export.*`, `--export-avb`, File > Output > Export to File's
"Media Composer Bin"), is done; ctests `avb_export_round_trip` and
`avb_export_pyavb`. Gate 2 (Media Composer opens our bins) waits for the
owner's seat. 3.5, `.avp` projects and `.avs` settings, is deferred (no
prior art, samples or seat; ROADMAP). 3.6a, EDL and ALE (`edl.*`,
`ale.*`, `--import-edl`/`--export-edl`/`--import-ale`/`--export-ale`; Import
EDL, the List Tool `list_tool.*`, Export Bin, shot logs through Import), is
done. Gate 4 (fuzzing, `fuzz/`, `scripts/fuzz.py`) passed: the 24-hour
run on the WSL runner was clean (BUILDING.md "Fuzzing"). 3.6b, OTIO, is
done: the reader and writer (`otio.*`, docs/spec/otio.md,
`--import-otio`/`--export-otio`, ctests `otio_round_trip` and
`otio_python` against OpenTimelineIO's Python package, fuzz target `otio`),
Import Media and bin drops (`MainWindow::importOtio`), and the List Tool's
OTIO output with the Default OpenTimelineIO setting (p.969). The checks'
Python packages come from `scripts/check-python.py` (BUILDING.md). 3.6c, FCP
7 XML (ours: `fcp_xml.*`, docs/spec/fcp-xml.md, through the OTIO import and
export; `--import-fcpxml`/`--export-fcpxml`, Import Media, Export to File's
"Final Cut Pro 7 XML"; ctests `fcpxml_round_trip`, `fcpxml_python`, fuzz
target `fcpxml`), is done, so 3.6 is. 3.7, shared-storage bin locking
(docs/spec/bin-locking.md: `src/model/bin_lock.*`, the Bin Lock Status
button in `bin_view.*`, locks in `bin_container.*`, Lock/Protect/Unlock
Project Bin, `LUMENIS_SHARED_BINS`), is done. Left in Phase 3: 3.5
(deferred) and the gates that wait for the owner's seat.

Phase 4 (core editing) has started; its features came in 1.4. 4.1 added
Fit to Fill, Reverse Match Frame and the edit-scenario harness
(docs/spec/edit-scenarios.md: `tests/scenarios/*.json`, `tst_scenarios`,
`scripts/make-scenarios.py`, ctest `edit_scenarios_generated`). Add a
scenario for each editing change. 4.2 took in the
2026.8 guides' changes: Change Source Track with More
(`Timeline::addChangeSourceTrack`, `model::changeSourceTrack`), Choose
Columns' Search field and keys (`bin_dialogs.*`), 32-bit Effects
Processing.

Guide editions: citations are the 2025.12 guides' pages. Material new in
2026.8 is cited "2026.8 guide p.N" or "What's New 2026.8 p.N". The 2026.8
Editing Guide is at
https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf
and the FX Guide at the same folder's `Media_Composer_v2026.x_FX_Guide.pdf`
(same pages as 2025.12). The page offsets between the editions are in
docs/spec/ui-inventory.md, "Editions". When new Avid documents arrive,
compare them with the last edition (as in "Editions") and recheck the
"Watch list" there: ours where the guides are silent. The owner wants
these changed if Avid's documents turn out to differ. Avid's 2026.8 shared projects on
third-party storage are undocumented, so our bin locking stays
(docs/spec/bin-locking.md).

Phase 5 (audio) has started. 5.1, Audio Mixdown (docs/spec/audio-mixdown.md:
`src/media/mixdown.*`, `media::renderedAudioClip`, `mixdown_dialog.*`,
`MainWindow::audioMixdown`), is done; `tst_mixdown` is gate 1's null test.
5.2 is done: volume and pan automation (docs/spec/automation.md):
  - keyframes on segments (`src/model/automation.*`);
  - the Timeline's Audio Data and keyframe editing (`timeline_automation.cpp`);
  - the Audio Mixer's Auto mode and recording (`audio_tools.*`);
  - sample-by-sample gains in `media::AudioMix`.
  Tests: `tst_automation`, `tst_automation_ui`, and the null test's
  automated cases.
5.3 is done: the Audio EQ tool (docs/spec/audio-eq.md):
  - three bands on whole segments (`src/model/audio_eq.*`, `Segment::eq`);
  - the filters in `media::AudioMix`;
  - the tool (`src/ui/audio_eq.*`), with Set EQ and Remove EQs, Remove
    Effect and Bypass RT EQ.
  Tests: `tst_audio_eq`, `tst_audio_eq_ui`, scenario `08-audio-eq.json`.
5.4 is done: Audio Punch-In (docs/spec/punch-in.md):
  - recording through miniaudio, simulated in tests (`src/media/audio_input.*`);
  - the tool (`src/ui/audio_punch_in.*`) and `Timeline::punchIn`;
  - the macOS microphone permission (packaging/macos/Info.plist.in, checked
    by the installer gate).
  Tests: `tst_audio_punch_in`, scenario `09-audio-punch-in.json`.
5.5a is done: Audio Track Effects (docs/spec/track-effects.md):
  - the audio effect interface and built-in effects (`src/media/audio_fx.*`;
    a plug-in host adds to it with `media::registerAudioEffects`);
  - inserts a-e on tracks (`model::Track::inserts`), mixed in `media::AudioMix`;
  - the tool (`src/ui/audio_track_effect.*`), the track button's menu, the
    Effect Palette's Audio Track tab.
  Tests: `tst_audio_fx`, `tst_track_effects`, scenario `10-track-effects.json`.
5.5b is done: the CLAP host (docs/spec/clap-host.md): CLAP 1.2.9 headers in
  `third_party/clap`, `src/media/clap_host.*` (scan at start-up in
  `app/main.cpp`, `LUMENIS_NO_CLAP=1` skips it), plug-ins as audio effects.
  Test: `tst_clap_host` with our own `tests/media/clap_test_plugin.cpp`.
5.5c1 is done: AudioSuite on Timeline segments (docs/spec/audiosuite.md):
  - our processes and every audio effect offline, rendering into managed
    media (`src/media/audio_suite.*`); the effect on segments and when a
    render is out of date (`src/model/audio_suite.*`, `Segment::audioSuite`);
  - playback plays renders; Audio Mixdown renders first;
  - the tool (`src/ui/audio_suite.*`).
  Tests: `tst_audio_suite`, `tst_audio_suite_ui`, scenario `11-audiosuite.json`.
5.5c2 is done: AudioSuite on master clips (`media::audioSuiteClip`, the
  tool's master clip controls, `MainWindow::makeAudioSuiteClip`) and effect
  templates (`model::AudioSuiteTemplate`, Effect items in bins; dropped from
  the effect icon through `audioSuiteTemplateMime()`).
5.6a is done: multichannel tracks (docs/spec/multichannel.md):
  - Track Formats on clips and Set Multichannel Audio (`src/model/multichannel.*`,
    `modify_dialog.*`, `MainWindow::modifySelection`);
  - multichannel segments name their first channel; patching, editing and
    moves only between tracks of one format; Split Track to Mono;
  - playback places each channel for stereo speakers (`media::audioPieces`),
    inserts per channel (`media::channelTrackKey`).
  Tests: `tst_multichannel`, `tst_multichannel_audio`, `tst_multichannel_ui`,
  scenario `12-multichannel.json`.
5.6b1 is done: surround mixing (docs/spec/surround.md):
  - the Sequence Mix Format (`model::Sequence::mixFormat`) and the Mix Mode
    Selection's monitor formats (`src/media/surround.*`);
  - `media::AudioMix` mixes onto eight speakers and folds them into the
    output (`setOutput`); the player opens that many channels;
  - Stereo, 5.1 and 7.1 Audio Mixdowns (`media::mixdownTrackFormat`).
  Tests: `tst_surround`, `tst_multichannel_ui`, scenario `13-surround.json`.
  Clip colours in the Timeline (docs/spec/timeline-colors.md: the Clip Color
  dialog `clip_color_dialog.*`, `model::ClipColorOptions` in
  `src/model/clip_coloring.*`, Set Local Clip Color, the Select menu's colour
  choices, Show Source/Record colors in Timeline) were added after 5.6b1;
  test `tst_clip_colors`, scenario `14-local-clip-color.json`. So were slow
  play (K+L, K+J, p.401) and the Play button's speed display, the owner's
  own design (`Composer::playDisplay`, docs/spec/ui-inventory.md section 10).
5.6b2 is done: surround panning (docs/spec/surround.md):
  - pans on mono and stereo segments (`model::SurroundPan`,
    `src/model/surround_pan.*`) and the pan law (`media::surroundGains`);
  - the mixer's Pan grids, Stereo Link/Mirror and the Advanced Panner
    (`src/ui/surround_panner.*`, `audio_tools.*`);
  - LFE-only tracks (`Track::lfeOnly`).
  Tests: `tst_surround`, `tst_surround_ui`, scenario `15-surround-pan.json`.
  5.7a1 is done: the Audio Mixer's Audio Loop Play (`Composer::playLoop`,
  `Timeline::audioLoopRange`), insert buttons, Render Effect, the Clip mode
  Fast menu with a segment's own centred pan (`Segment::hasPan`), Set
  Display Options and Live Mix to/as Automation (docs/spec/audio-mixer.md;
  test `tst_audio_mixer`, scenario `16-mixer-clip-pan.json`). 5.7a2 is
  done: Show Track Sidebar and Audio Grouping (`src/ui/audio_groups.cpp`).
  5.7b1 is done: Apply Gain and Center Pan on bin clips (`BinItem::audioGain`,
  `audioPan`, carried into edits) and Auto VO (`src/model/clip_audio.*`,
  `clip_audio_dialogs.*`, docs/spec/clip-audio.md; test `tst_clip_audio`,
  scenario `17-clip-gain-pan.json`). 5.7b2 is done: Read Audio Timecode
  (our LTC decoder `src/media/ltc.*`, test `tst_ltc`) and Change Sample
  Rate (`media::resampleClipAudio`; 32/44.1 kHz media is WAV, MXF takes only
  48 kHz).
  Next: 5.7, the audio items still dimmed (ROADMAP Phase 5 status: 5.7b Clip/Timeline audio commands, 5.7c tone, EQ icon and
  audio settings); then the UI completion pass (U1 Timeline, U2 Composer and
  trim, U3 bins, windows and interface); Phase 9 "Left over stuff" (printing,
  Batch Import, AutoSync ...) comes before Phase 10, hardening.

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
