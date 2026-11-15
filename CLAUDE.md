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
  If the WSL runner goes offline (its job stays queued or is cancelled),
  go on when the Windows and Mac jobs pass, with Linux checked locally (ci
  preset, clang, GCC 15 check); come back to it when WSL is back (owner's
  decision, 2026-11-10). ui-consistency waits for it too.
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
- Tests never touch the computer's real drives: every ctest runs with
  `LUMENIS_NO_SYSTEM_DRIVES=1` and its own drive folder `test-media/<test>`
  (`lumenis_isolate_media_drives` in tests/CMakeLists.txt; a new tests/
  subdirectory calls it last).
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
  48 kHz; `.wav` files open with FFmpeg's wav demuxer first, BUILDING.md).
  5.7b3 is done: Multiple Mixes, the Multi-Mix Tool (`media::MultiMix` in
  `mixdown.*`, `multi_mix_dialog.*`, `MainWindow::multipleMixes`, Project
  setting "Audio Multi-Mix", docs/spec/multi-mix.md; test `tst_multi_mix`).
  5.7c1 is done: Play Calibration Tone, Create Tone Media and the Audio
  Tool's meters following the output (`src/media/tone.*`, `AudioPiece::tone`,
  `AudioPlayer::takePeaks`, `CreateToneMediaDialog`, docs/spec/tone.md; tests
  `tst_tone`, `tst_audio_media`).
  5.7c2 is done: EQ templates (`model::eqTemplateItem`, `eqTemplateMime()`,
  `Timeline::dropEq`, Site_EQs_Bin through `MainWindow::siteEqBinPath`;
  test `tst_audio_eq_ui`, scenario `18-eq-drop.json`).
  5.7c3 is done: the audio settings' options (docs/spec/audio-settings.md:
  buffer sizes, Pitch Correction, the project Sample Rate for new media,
  Convert Sample Rates When Playing `AudioMix::setRequiredRate`, mismatched
  rate colours, the Import Audio tab with `media::groupMonoWavs` and
  `alignToFrame`; tests `tst_audio_project`, `tst_audio_media`). Display
  Channel Order, Remove Extra Filler and gain on import stay dimmed.
  So 5.7 is done. The UI completion pass has started: U1a, audio waveforms in
  the Timeline (docs/spec/waveforms.md: `src/media/waveform.*` with the
  project's WaveformCache, `TimelineView::paintWaveform`, Waveform Options,
  Ctrl+Alt+L/K, Show Marked Waveforms, mismatched rates white, Display
  Channel Order; tests `tst_waveform`, `tst_waveform_ui`), is done. U1b1,
  the Track Control panel (docs/spec/track-control.md: per track settings
  and Inactive on `model::Track`, kept with the sequence without undo steps;
  Solo/Mute shared with the Audio Mixer; insert buttons; Enlarge/Reduce
  Track with Ctrl+L/K in the Timeline; Track Color, Segment/Mark IO
  Selection, Show Markers; test `tst_track_control`), is done. U1b2,
  Timeline views and View Type (docs/spec/timeline-views.md: Timeline View
  User settings from the View Menu button, `Timeline::displayToJson`,
  `MainWindow::saveTimelineView`, TV1-TV8 on the More tab; Heads and Heads
  Tails with `Sequence::headsSpans`/`moveSpan`; test `tst_timeline_views`,
  scenario `19-heads-move.json`), is done. U1c1, Sync Breaks, Dupe
  Detection and Clip Text (docs/spec/sync-dupes.md: `src/model/sync_dupes.*`,
  `Timeline::clipText`, the Dupe Detection Handles setting; test
  `tst_sync_dupes`), is done. U1c2, Timeline Clip Notes
  (docs/spec/clip-notes.md: `Segment::note`, `Timeline::setClipNote`, the
  window `src/ui/clip_notes.*`; test `tst_clip_notes`, scenario
  `20-clip-notes.json`), is done. U1d1, Add Filler at Start, Find Black
  Hole/Flash Frame, Rename Track and Create Sequence Based on Selection
  (docs/spec/timeline-commands.md: `Sequence::addFillerAtStart`,
  `findBlackHole`, `findFlashFrame`, `selectionCopy`, `Track::customName`;
  test `tst_timeline_commands`, scenario `21-filler-at-start.json`), is done.
  U1d2, the Sequence Map (docs/spec/sequence-map.md: `src/ui/sequence_map.*`,
  `Timeline::showSequenceMap`, the Timeline settings' Sequence Map options;
  test `tst_sequence_map`), is done. U1e1, the Timeline settings' Edit tab
  (docs/spec/timeline-settings.md: Auto-Patching, Auto-Monitoring, Segment
  Drag Sync Locks `moveSegments(..., keepSync)`, Position Bar Snap, Default
  Sync Locks On, Select Filler, Effect Editor on apply, TC/ruler disables
  Smart Tools, Segment Mode lassos; test `tst_timeline_settings`, scenario
  `22-segment-drag-sync-locks.json`), is done. U1e2, the Display tab (Timeline
  Text Color, Wireframe Dragging, Use Fast Scrub), is done; so is U1. Display
  During Segment Drag and Dynamic Play move to U2. U2a, the Composer
  settings' Edit tab, Sync Point Overwrite and Load Filler
  (docs/spec/composer-edit.md: `planSyncPoint`, `planEdit(..., singleMark)`,
  phantom marks through `Composer::setPhantomResolver`, the orange mark
  `ButtonMapper::setMarked`, `Timeline::loadFiller`; test `tst_composer_edit`,
  scenario `23-composer-edit.json`), is done. U2b, the Timecode window and Loop
  Selected Clips (docs/spec/timecode-window.md: `src/ui/timecode_window.*`,
  `Composer::playClipLoop`; test `tst_timecode_window`), is done. U2c, the Find
  window (docs/spec/find.md: `src/model/text_find.*`, `src/ui/find_window.*`,
  Ctrl+G `edit.find_again`; test `tst_find_window`), is done. U2d, AutoSequence
  (docs/spec/autosequence.md: `model::autoSequence`, `MainWindow::autoSequence`;
  test `tst_autosequence`), is done. U2e1, the trim Play Loop, Edit Review and J-K-L Trim
  (docs/spec/trim-settings.md: `Timeline::playTrimLoop`, `editReview`, the Trim
  settings' Play Loop tab, `Composer::playLoop` with a `LoopResolver`; test
  `tst_trim_settings`, scenario `24-jkl-trim.json`), is done. U2e2, the trim
  monitors' outgoing and incoming frames, Small Trim display, Dual Image Play
  and Sync Rollers (`Composer::TrimDisplay`, `trimFrame`, the PictureResolver
  taking a frame, `Timeline::syncRollers`), is done. U2e3, the Composer settings'
  Window tab (docs/spec/composer-display.md: two rows of data with a second
  tracking display, Flow Data, position bar tick marks, the middle button panel
  under "middle" in the button layout; test `tst_composer_display`), is done.
  U2e4, Dynamic Play Forward/Reverse (`Composer::playDynamic`, fractional
  speeds) and Display During Segment Drag (`Timeline::showSegmentDrag`,
  `Composer::setDragDisplay`, the four-frame display), is done. So U2 is.
  U3a, Bin > Select's Offline Items, Media Relatives, Sources and
  Unreferenced Clips, Reveal File and Close All But Active
  (docs/spec/bin-select.md: `src/model/bin_relations.*`, `MainWindow::binSelect`,
  `revealFile`; test `tst_bin_select`), is done. U3b, Input > Link, Media >
  Unmount and Center Current/Send Current Home (docs/spec/media-unmount.md:
  `media::unmountDrives`, `unmount_dialog.*`, `MainWindow::placeCurrentWindow`;
  test `tst_media_unmount`), is done. U3c, Set Font and Help > Shortcuts
  (docs/spec/set-font.md: `set_font_dialog.*`, `MainWindow::chooseFont`, fonts
  in `BinDisplay`, the Composer setting and `Timeline::Display`;
  `shortcuts_window.*`; test `tst_set_font`), is done. U3d, Bin Layouts
  (docs/spec/bin-layout.md: `BinContainer::binLayout`/`applyBinLayout`, the
  "Bin Layout" Project setting, `MainWindow::openBinLayout` and the Workspace
  View link; test `tst_bin_layout`), is done. U3e, the attic and auto-save
  (docs/spec/bin-attic.md: `src/model/bin_attic.*`, `Locations::attic`,
  `BinContainer::setAutoSaveTiming`; tests `tst_bin_attic`, `tst_bin_attic_ui`),
  is done. U3f, bin editing into the Timeline (docs/spec/bin-editing.md:
  `Timeline::editClip`/`droppedClip`/`binEditKind`, `MainWindow::editFromBin`,
  Enable edit from bin and Segment Drag Snap; test `tst_bin_editing`, scenario
  `25-bin-editing.json`), is done. U3g, the Interface settings' colours and
  fonts (docs/spec/interface-colors.md: "color" and "font" options in
  `SettingsDialog`, `BinAppearance`, `BinContainer::setSidebarAppearance`,
  `Composer::setViewerBackground`, `Timeline::backgroundColor`; test
  `tst_interface_colors`), is done. U3h, the last Bin settings
  (docs/spec/bin-settings.md: Frame view borders and icons, Show local media
  icons with `MediaDatabase::hasMixedResolution`, single floating Bin
  Containers in workspaces through `WorkspaceManager::setExtras`; test
  `tst_bin_settings`), is done, so U3 is.
  The monitor marker fix (an upright oval and the comment over the frame,
  no colour over the whole picture; `markerOval` in `composer.cpp`, test
  `tst_monitor_markers`) is done.
  Phase 6 (effects and rendering) has started; its steps (6.1 the compositor
  and dissolves to 6.11 the OFX host) are in docs/ROADMAP.md. 6.1, the
  compositor (docs/spec/compositor.md: `src/media/compositor.*`,
  `media::composeImage` in the Composer's picture resolver,
  `MainWindow::shownVideoTracks`; the dissolve family and Superimpose; test
  `tst_compositor`), is done. 6.2a, keyframes on effect parameters
  (docs/spec/effect-keyframes.md: `src/model/effect_keyframes.*`, read by the
  compositor; the Fade Effect button `Timeline::fadeEffects`; tests
  `tst_effect_keyframes`, `tst_compositor`, `tst_effects`), is done. 6.2b1,
  the Effect Editor's keyframe tracks and graphs (`effect_editor_keyframes.cpp`,
  `KeyframeTrack`; keyframe operations and `model::EffectSpan`; Fixed keyframes
  through trims; `"keyframeable": false` in effect_parameters.json; test
  `tst_effect_keyframes_ui`, scenario `26-fixed-keyframes-trim.json`), is done.
  6.2b2, graph views, copy/paste, Bézier handles and the "Effect Editor"
  User setting (`EffectEditor::Options`), is done. 6.2c, Effect mode
  (docs/spec/effect-mode.md: `Timeline::setEffectMode`, the Source/Record Mode
  and Effect Mode buttons, `Composer::EffectPreview` driven by
  `EffectEditor::syncPreview`; test `tst_effect_mode`), is done, so 6.2 is.
  6.3a, the 2D transitions (docs/spec/wipes.md: `src/media/wipes.cpp`, Box, Edge, Shape, Matrix and
  Sawtooth wipes, Conceal, L-Conceal, Push, Squeeze and Spin with Border and Foreground; the Custom
  Grid's More Options; tests `tst_compositor`, `tst_effects`), is done.
  6.3b1, the geometric image effects (docs/spec/image-effects.md: `src/media/image_effects.cpp`,
  `effect_drawing.*`; Picture-in-Picture, Flip/Flop, Field Swap, Resize, Mask, Film masks, Blowup, the
  Reformat letterboxes; `media::parameterDefault` shown by the Effect Editor; Fixed Aspect), is done.
  6.3b2, the Color Effect, Blur and Mosaic (`src/media/color_effects.cpp`) and Acceleration
  (easing in `model::numberAt`; test `tst_effect_keyframes`), is done; repeated parameter names in a
  category are stored as "<subcategory>: <name>" (`EffectParameter::name`).
  6.3c, the Key effects (docs/spec/keys.md: `src/media/key_effects.cpp` for Luma/Chroma Key, RGB
  Keyer and SpectraMatte; Matte Key's three layers in `compositor.cpp`; tests `tst_compositor`), is done.
  6.3d1, Timecode Burn-In (docs/spec/burn-in.md: `src/media/burn_in.cpp`, `media::BurnInInfo` filled
  by the Composer's picture resolver, the Effect Editor's Display/Notes tabs; tests `tst_burn_in`,
  `tst_effects`), is done.
  6.3d2, the Illusion FX colour, texture and drawing effects (docs/spec/illusion-fx.md:
  `src/media/illusion_fx.cpp`; slider ranges the guide omits as `"ours"` in effect_parameters.json;
  test `tst_illusion_fx`), is done.
  6.3d3, the Illusion FX distortions (Pinch, Radial Blur, Ripple, Sphere, Swirl, Twist, Wave; the
  same file, `warp` and `SourceBox` for the Illusion FX Border), is done.
  6.3d4, the Illusion FX transitions (Iris, Melt, Pagecurl, Particle Blast/Orbit/Wind, Random Blend,
  Rollup, Shear: `src/media/illusion_transitions.cpp`, `media::illusionTransitionPicture` from the
  compositor's `transitionPicture`; helpers shared in `illusion_common.h`), is done, so 6.3d is.
  So 6.3, the effect families, is done.
  6.4a, Render Effect (docs/spec/rendering.md: `EffectRender` on segments in `sequence.h`,
  `src/model/effect_render.*`, `src/media/render.*` with `media::renderedVideoClip`, renders played
  through `Composition::rendered`, `MainWindow::renderEffects`/`clearRenders`, `render_dialogs.*`; tests
  `tst_effect_render`, `tst_render`, `tst_render_ui`), is done. 6.4b, partial render (Keep/Discard,
  `EffectRender::clips`), Render Ranges (`Timeline::RenderRanges`), the playback indicators
  (`Composer::playbackDifficulties`, `Timeline::setPlaybackIndicators`) and Render On-the-Fly
  (`Composition::effects`), is done. 6.4c, ExpertRender In/Out (`model::expertRender`,
  `ExpertRenderDialog`, `MainWindow::expertRender`, the Timeline's expert selection), is done.
  6.4d1, the Render setting (Image Interpolation `media::ImageInterpolation`, Render Completion Sound
  `MainWindow::playRenderCompletionSound`), Save after Render and precomputes as reference clips in the
  sequence's bin with the Media Tool's Precompute Clips (`MainWindow::keepPrecomputes`), is done.
  6.4d2, Background Process rendering (`MainWindow::renderInBackground`, `applyBackgroundRenders`; media in
  `MXF/__bg.1` through `MediaCreation::folder`; `BackgroundQueue::add`'s `done`), is done, so 6.4 is.
  6.5a, traditional motion effects (docs/spec/motion-effects.md: Motion Effect clips in
  `src/model/motion_effect.*` followed by `MainWindow::followMotion`; `model::segmentSourceFrame` playing a
  segment's Motion Effect; Composer > Freeze Frame > Other and the Motion Effect dialog,
  `motion_effect_dialogs.*`; tests `tst_motion_effect`, `tst_motion_effect_ui`), is done.
  6.5b, Timewarp (`src/model/timewarp.*`: speed and position graphs as keyframed parameters, anchor,
  strobe, Type, templates set by `applySegmentEffect`, `promoteToTimewarp`; `model::segmentSourcePosition`
  and blended frames `media::segmentBlend`), is done.
  6.5c, the Motion Effect Editor (`src/ui/motion_effect_editor.*`, `MotionGraph`; Timewarps Render
  Using applied by `MainWindow::applyTimewarpRenderTypes`), is done.
  6.5d1, FluidMotion (our optical flow `src/media/optical_flow.*`: block matching and in-between frames;
  test `tst_optical_flow`), is done.
  6.5d2, motion adapters (`src/model/motion_adapter.*`: mixed rate clips seen as adapted copies at the
  project's rate, `Segment::adapter`, blends in `media::FrameCache::frame`, the green dot and Show
  Adapters, the Field Motion column, Refresh Sequence; tests `tst_motion_adapter`,
  `tst_motion_adapter_ui`, scenario `27-motion-adapter.json`), is done.
  6.5d3a, fields and format conversion (`src/media/fields.*`: field-level Types, the Timewarp's Source and
  Output formats with 2:3 pulldown and repeated frames, Adaptive Deinterlace Source, the FluidFilm and
  Pulldown templates, adapters following Field Motion, two-field motion effects; tests `tst_fields`,
  `tst_motion_adapter_ui`), is done.
  6.5d3b, FluidBlur, FluidColorMap and FluidMorph (docs/spec/illusion-fx.md: `src/media/fluid_fx.*` on the
  optical flow, from the compositor; test `tst_fluid_fx`), is done, so 6.5 is.
  6.6a, nests (docs/spec/nesting.md: `Segment::nest`/`nestIn` and `src/model/nest.*`, the compositor's
  nest, `Timeline::stepIn`/`stepOut` with N1 in the Timecode display, Alt-nesting in
  `applySegmentEffect`, `Timeline::collapse`, `MainWindow::videoMixdown`; tests `tst_nest`, `tst_nest_ui`,
  scenario `28-collapse.json`), is done.
  6.6b1, effect templates (docs/spec/effect-templates.md: `src/model/effect_template.*`, Effect items under
  "videoEffect"; `effectTemplateMime()`, the Effect Editor's effect icon and parameter group drops, the Motion
  Effect Editor's Timewarp icon, `Timeline::dropTemplate`, the Effect Palette's template bins,
  `BinView::promoteEffects`; tests `tst_effect_template`, `tst_effect_template_ui`, scenario
  `29-effect-template.json`), is done.
  6.6b2, nested effects in the Effect Editor (docs/spec/nesting.md: `model::nestedEffects`,
  `setNestedEffect`, `moveNestedEffect`; the editor's title rows, `EffectEditor::setNestedLevel`,
  `Timeline::changeNestedEffect`; nests in render keys; tests `tst_nest`, `tst_nest_ui`, scenario
  `30-nested-order.json`), is done.
  6.6b3, expanding nests in the Timeline (docs/spec/nesting.md: `model::expandNest`/`collapseExpansion`,
  `Timeline::expandNest`/`closeNest`/`doubleClickTrack` with the expanded sequence as a Record item of its
  own, Double-Click to Show Nesting; `Timeline::storeDetached` for undo after leaving a nest; tests
  `tst_nest`, `tst_nest_ui`, scenario `31-expanded-nest.json`), is done, so 6.6 is.
  6.7 is in four parts (ROADMAP). 6.7a1, the 3D Warp renderer (docs/spec/3d-effects.md: `src/media/warp3d.cpp`,
  `media::is3DEffect`/`warp3dPicture` from the compositor; Corner Pin's per-corner "key"s in
  effect_parameters.json; 3D Promote with `src/model/promote3d.*` and `EffectEditor::promoteTo3D`; tests
  `tst_warp3d`, `tst_effect_keyframes_ui`), is done.
  6.7a2, the 3D shapes (Page Fold, Ball, Slats bent and drawn as a depth-buffered mesh, `drawMesh`), the
  Foreground (3D) key types and the Border (3D) types (same file; test `tst_warp3d`), is done.
  6.7a3a, Trail, Erode/Expand and Spill Suppression (`placedLayer` renders the effect at any time; test
  `tst_warp3d`), is done.
  6.7a3b, Stamp (the compositor's per-track stamp buffer `stampsOf`, `media::warp3dLayer`/`stampTimes`, the
  Effect Editor's Stamp Image and Clear; tests `tst_compositor`, `tst_effect_keyframes_ui`), is done.
  6.7a4, direct manipulation in the Effect Preview (docs/spec/3d-effects.md: outlines, handles, drags solved
  numerically and the motion path in `src/media/effect_handles.*`; the 3D handle buttons and Outline/Path in
  `effect_editor_handles.cpp`; the overlay and `Composer::effectHandleDragged`; tests `tst_effect_handles`,
  `tst_effect_keyframes_ui`), is done, so 6.7a is.
  6.7b1, Pan and Scan (docs/spec/pan-scan.md: `src/media/pan_scan.cpp` and `media::panScanFrames`, the Effect
  Preview's original through `Composition::effectPreview`, the wire frame's drags in `effect_handles.cpp`,
  Establish/Reset Origin and Subdivide Effect in `src/model/pan_scan.*` and `effect_editor_handles.cpp`; tests
  `tst_pan_scan`, scenario `32-pan-scan-subdivide.json`), is done.
  6.7b2a, Avid Pan & Zoom's image and view (docs/spec/pan-zoom.md: `src/media/pan_zoom.cpp` with its image cache,
  `Composition::effectPreview` for Display Source, the crosshair and Safe Action in `effect_handles.cpp`,
  `EffectEditor::importImage` and the locate dialog; sliders with `"decimals"`; test `tst_pan_zoom`), is done.
  6.7b2b, Pan & Zoom's Velocity In/Out and Path (`media::panZoomMotion`; the Velocity menus kept as keyframes of
  the menu at the selected keyframes' times; test `tst_pan_zoom`), is done, so 6.7b is.
  6.7c1, motion tracking's data and engines (docs/spec/tracking.md: `src/model/tracking.*` kept under the effect's
  "Trackers", `model::trackedChange`; `src/media/tracking.*` with `media::trackTarget`, `trackedEffect` used by the
  compositor and Corner Tracking in `warp3d.cpp`; test `tst_tracking`), is done.
  6.7c2, the Tracking window and the tracking controls (`src/ui/tracking_window.*`, `effect_editor_tracking.cpp`:
  the Effect Editor's On boxes and tracker selectors, tracking boxes and data points in the Effect Preview through
  `Composer::EffectPreview::trackers`, `EffectEditor::startTracking` over frames from
  `MainWindow`'s `setTrackingFrames`; test `tst_tracking_ui`), is done.
  6.7c3, editing tracking data (selection, lasso and double-click in the Effect Preview, Delete through
  `Timeline::setDeleteHook`, moving and Stretch Points Mode, the tracker timeline's region and reference frame
  menu, Go to Previous/Next Region, the Display Markers; `model::deletePoints` and friends; tests `tst_tracking`,
  `tst_tracking_ui`), is done, so 6.7c is.
  6.7d is in three parts (ROADMAP). 6.7d1, tracking data through trims and splits (`model::shiftTrackingData`
  from `Sequence::cut` and the trims) and copying and pasting trackers (`model::pasteTrackers`,
  `EffectEditor::copyTrackers`; scenario `33-tracking-through-edits.json`), is done.
  6.7d2, the Stabilize effect (`model::stabilizes`/`applyAutoZoom` in `src/model/tracking.*`, rendered as Resize;
  AutoStabilize through `Timeline::autoStabilizeRequested` and `EffectEditor::autoStabilize`, Auto-Zoom kept by
  `EffectEditor::keepAutoZoom`; promotion to 3D with Stabilize Tracking; tests `tst_tracking`, `tst_tracking_ui`),
  is done.
  6.7d3, the Region Stabilize effect (`src/media/region_stabilize.cpp`: the region followed frame to frame and kept
  per segment, the Model menu, Auto Zoom; its wire frame in `effect_handles.cpp`; categories from p.280-281 in
  effect_parameters.json), is done, so 6.7d and 6.7 are.
  6.8, titles, is in five parts (ROADMAP). 6.8a, Titler+'s title model and picture (docs/spec/titler.md:
  `src/model/titler.*` kept under the effect's "Titler", `src/media/titler.cpp`, `Timeline::applyTitler` from Tools >
  Titler+ and the Timeline's button, the Title Text Clip Text label; tests `tst_titler`, `tst_titler_ui`, scenario
  `34-titler-marks.json`), is done.
  6.8b1, the Record monitor's Titler+ tools (`src/ui/titler_tool.*` through `Composer::OverlayInput`, previews via
  `EffectEditor::previewTitle`/`pictureOverride` in `effect_editor_titler.cpp`; test `tst_titler_ui`), is done.
  6.8b2, the Dashboard and the Effect Editor's Titler+ controls (`src/ui/titler_panel.*`, alignment, anchor presets
  and the grid in `titler_tool.*`; test `tst_titler_ui`), is done, so 6.8b is.
  6.8c is in four parts (ROADMAP). 6.8c1, keyframes on Titler+ objects and the title (`Titler:<id>` effect keyframes,
  Fixed; `model::titleAt`/`storeTitleAt`/`addTitleKeyframes`, `media::titlePicture` at a time, the panel's Add/Delete
  Keyframe through `EffectEditor::setTitleKeyframes`; splits move Fixed keyframes in `Sequence::cut`; tests
  `tst_titler`, `tst_titler_ui`, scenario `35-title-keyframes-split.json`), is done.
  6.8c2, formatting selected characters (`TitleObject::runs`, `model::changeCharacters`/`replaceText`/`styleAt`,
  layout by characters in `src/media/titler.cpp` with `media::titleTextIndexAt`, drag highlighting and
  `TitlerTool::highlighted`/`shownObject`; tests `tst_titler`, `tst_titler_ui`), is done.
  6.8c3, Roll / Crawl and tracking on title objects (`Title::rollCrawl`, `model::setRollCrawl`, the keyframeable
  "Effect Position", `media::titleScroll`; `TitleObject::tracking`/`tracker` applied in `model::titleAt`; tests
  `tst_titler`, `tst_titler_ui`), is done.
  6.8c4a, Titler+ templates (`model::isTitleTemplate`/`titleTemplateSequence`, `media::titleFirstFrame` in the bin
  views, Edit Template and `Composer::titleTemplateDropped`, `MainWindow::editTitleTemplate`/`saveTitleTemplate` with
  the Dashboard's Save and Save As and the save prompt; tests `tst_titler`, `tst_titler_templates`), is done.
  6.8c4b, templates as presets (`model::applyTitlePreset`, drops on `TitlerPanel` by section, `TitlerTool::applyPreset`)
  and titles in EDLs (`titlePieces` in `src/interop/edl.cpp`; tests `tst_titler`, `tst_titler_ui`, `tst_edl`), is done,
  so 6.8c is.
  6.8d is in four parts (ROADMAP). 6.8d1, the classic Title Tool's window (docs/spec/title-tool.md:
  `src/model/classic_title.*`, `src/media/classic_title.*` through Titler+'s layout, `src/ui/title_tool.*` with its own
  Edit/Object/Alignment menus, `MainWindow::openTitleTool` from Clip > New Title and Tools > Title Tool Application;
  tests `tst_classic_title`, `tst_title_tool`), is done.
  6.8d2, classic rolling and crawling titles (`ClassicTitle::motion`/`pages`/`autoSize`, `model::classicPageLimit`,
  the page operations and Make Crawl in `src/media/classic_title.*`, the window's buttons, Page Number box, scroll bars
  and Object menu page commands; tests `tst_classic_title`, `tst_title_tool`), is done.
  6.8d3a, classic line attributes, colour and transparency blends and shadows (`ClassicPaint`, the line, border, corner,
  arrow and shadow fields of `ClassicObject`; `media::classicObjectShape`/`classicBorderShape` with Titler+'s
  `titleTextPath`/`softenImage`; the window's attribute row and Object > Soften Shadow; tests `tst_classic_title`,
  `tst_title_tool`), is done.
  6.8d3b, layers, groups, locks, alignment and distributing (`ClassicObject::group`/`locked`,
  `model::reorderClassicObjects`/`groupClassicObjects`/`keepLockedObjects`, `media::alignClassicObjects`/
  `distributeClassicObjects`, Edit > Duplicate), is done, so 6.8d3 is.
  6.8d4a, Title Effect clips (`src/model/title_clip.*`: a Title item with the effect and a nest of fill and matte
  precomputes; `media::renderClassicTitleMedia`, the compositor's `classicTitleLayer`), the Title Tool's File menu and
  Save Title dialog, Fast Save, revising (`MainWindow::saveClassicTitle`/`reviseTitle`, Ctrl+double-click, drops) and
  Bin > Select > Unrendered Titles (tests `tst_classic_title`, `tst_title_tool`, `tst_title_clips`), is done.
  6.8d4b, title templates (`ClassicObject::fromTemplate`, `model::includeClassicTemplate`/`keepTemplateObjects`, the
  Templates menu, `.ttpl` files in the user's Title_Templates) and title styles (`model::ClassicTitleStyle`,
  `applyClassicStyle`, the Styles and Save Style Parameter buttons, `titleStyleSheet()`, F-keys, "Title Style" User
  settings through `MainWindow::titleStyles`/`saveTitleStyle`), is done.
  6.8d4c, Promote Titles > Avid Titler+ (`media::promoteClassicTitle`, `Timeline::promoteTitles` from the Timeline's
  context menu; tests `tst_classic_title`, `tst_title_clips`), is done, so 6.8d is.
  6.8e1, editing Title Effect clips into sequences (Title items as edit sources in `model::applyEdit`, bin drags,
  `model::titleRollStart` for rolls and crawls, `MainWindow::placeTitle` for Place in Timeline; tests
  `tst_title_clips`, scenario `36-title-clip.json`), is done; `shownVideoTracks` now shows every track under the
  monitored one (Editing Guide p.627).
  6.8e2, revising titles in sequences (`MainWindow::editTitleInSequence` from the Effect Editor's Edit Title button and
  the Timeline's Edit Title, `TitleToolWindow::openInSequence`, `model::titleSourceOf`/`reviseTitleSegment`; the Title
  effect's Scaling/Position/Crop and replaced fill tracks in `classicTitleLayer`; test `tst_title_clips`), is done.
  6.8e3a, title media and troubleshooting (`MainWindow::recreateTitleMedia`/`createUnrenderedTitleMedia` with
  `askTitleMediaChoice`, `model::setTitleMedia`, the resolution and raster in `model::TitleMedia`, Wrong Format through
  `Composition::raster`, the console's HDTitleFilter `media::setHdTitleFilter`, Use Classic Character Mapping dimmed;
  test `tst_title_clips`), is done.
  6.8e3b, titles promoted to 3D (`model::canPromoteTo3D`/`promoteTo3D` for classic titles, `model::is3DClassicTitle`,
  `warp3dPicture` in `classicTitleLayer`; test `tst_title_clips`), is done, so 6.8 is.
  6.9 is in five parts (ROADMAP). 6.9a, the Intraframe objects and their pictures (docs/spec/intraframe.md:
  `src/model/intraframe.*` under the effect's "Intraframe" category, each object with its own parameters and keyframed
  shapes; `src/media/intraframe.cpp` with the Paint modes, Blur and Mosaic within objects and AniMatte on segments and
  transitions; tests `tst_intraframe`, `tst_compositor`), is done.
  6.9b1, the drawing and selection tools (`src/ui/intraframe_tool.*` through `Composer::OverlayInput`, the Effect
  Editor's lettered tool buttons, `EffectEditor::setIntraframeSelection` showing one object's parameters,
  `previewIntraframe`/`changeIntraframe`; test `tst_intraframe_ui`), is done.
  6.9b2, Reshape, Z-Rotation and the Object menu (`src/ui/intraframe_reshape.cpp`; control point operations in
  `src/model/intraframe.*`; the Effect Editor's "intraframeObjectMenu"; tests `tst_intraframe`, `tst_intraframe_ui`), is
  done, so 6.9b is.
  6.9c1, the Brush Shape buttons, Brush Preview window and custom heads (`src/ui/brush_preview.*`,
  `effect_editor_brush.cpp`, `model::brushHeadPoints`), brush templates (`model::brushTemplateItem`,
  `brushTemplateMime()`) and the eyedropper (`ColorSwatch` in `effect_editor.cpp`, `Composer::colorAt`; tests
  `tst_intraframe`, `tst_intraframe_ui`), is done.
  6.9c2, the Previsualization Marker (`model::addPrevisualizationMarker`/`previsualizationEvents`,
  `EffectEditor::setFilmProject`) and Export Matte PICT (`media::aniMatteImage`, our PICT writer and reader
  `src/media/pict.*` with fuzz target `pict`, `MainWindow::exportMattePict`), is done, so 6.9c is.
  6.9d, Scratch Removal (`media::scratchRemovalSource` with the segment's frames through `IntraframeFrames`,
  `model::applyScratchRemoval` behind `fx.scratch_removal` on the Tool palette, the Frame Processing button; tests
  `tst_intraframe`, `tst_intraframe_ui`, scenario `37-scratch-removal.json`), is done.
  6.9e, PlasmaWipe (docs/spec/plasma-wipe.md: `src/media/plasma_wipe.*` with our own generated gradients and the
  user's PlasmaWipes folders, `LUMENIS_PLASMAWIPES`, added to `EffectCatalog`; test `tst_plasma_wipe`), is done, so 6.9 is.
  6.10 is in five parts (ROADMAP). 6.10a1, the HSL group's corrections (docs/spec/color-correction.md:
  `model::ColorCorrection` with controls by path and Enable buttons, `model::lumaRangeWeights`,
  `media::colorCorrectedPicture` for the Color Correction effect; test `tst_color_correction`), is done.
  6.10a2, the Channels, Levels and Curves groups (paths "channels/...", "levels/...", "curves/...";
  `model::levelsValue`, `colorCurve`/`curveValue`; groups applied in the tab order in `colorCorrectedPixel`), is done.
  6.10a3, the Secondary group's vectors ("secondary/vectors/<vector>/input|output/...", `model::vectorMembership`,
  `syringeVector`, `addToVector`/`subtractFromVector`, `media::wheelHueSaturation`), is done, so 6.10a is.
  6.10b is in four parts (ROADMAP). 6.10b1, Color Correction mode and its monitors (`Segment::sourceCorrection`/
  `programCorrection` merged by `model::segmentCorrection` and applied in the compositor's `segmentPicture`;
  `Timeline::setCorrectionMode`, `correctionShot`, `setCurrentCorrection`, `removeCorrection`, `goToUncorrectedShot`;
  `Composer::setCorrectionMode`/`playOnce` and `src/ui/correction_monitors.*`; the `cc.*` commands on the Command
  Palette's CC tab; tests `tst_correction_mode`, scenario `38-color-correction.json`), is done.
  6.10b2, the Color Correction tool and the HSL group's panes (`src/ui/correction_tool.*`: `CorrectionTool` as the
  "ColorCorrection" panel opened by the mode, its tabs and Enable buttons, `ChromaWheel`, `LumaRangesGraph`, the Luma
  Ranges previews through `CorrectionMonitors::setPreview`; test `tst_correction_tool`), is done.
  6.10b3a, the Channels, Levels and Curves panes (`src/ui/correction_tool_panes.cpp`: Channel Blending and channel
  previews, the Levels subdividing tabs with `Histogram` and `ChromaCurveGraph`, the Curves graphs and Master sliders;
  test `tst_correction_tool`), is done.
  6.10b3b, the Secondary pane (`src/ui/correction_tool_secondary.cpp`: the vector sliders per vector, `VectorWheel`,
  the Selected Vectors panel, Lock Vectors, the Color Selection buttons sampling through `CorrectionMonitors::
  setSampling`/`colorsSampled`; test `tst_correction_tool`), is done, so 6.10b3 is.
  6.10b4a, the Correction setting (Tabs, Units with `UnitSpinBox` and `model::toCorrectionUnit`, the histogram options,
  Secondary Vectors, backgrounds, 3 x 3 averaging, Dual-Split during playback, Use Marks; `CorrectionTool::Options`;
  test `tst_correction_tool`), is done.
  6.10b4b, the tool's buttons, Comments (`Segment::correctionComment`, Clip Text > Comments) and the CC tab's Tab 1-12
  buttons (`CorrectionTool::showPath`, kept as the Correction setting's tabButton1-12), is done, so 6.10b is.
  6.10c is in three parts (ROADMAP). 6.10c1, the Waveform and Vectorscope monitors (`src/media/scopes.*` with
  `media::scopeImage`, the scope entries of `CorrectionSource`; tests `tst_scopes`, `tst_correction_mode`), is done.
  6.10c2, the automatic corrections and Remove Color Cast (`src/media/auto_correct.*`, `CorrectionTool::autoCorrect`/
  `startRemoveColorCast`, the CC tab's `cc.auto_*`; tests `tst_auto_correct`, `tst_correction_tool`), is done.
  6.10c3a, the Color Match control (`src/media/color_match.*`, `ColorMatchControl` in `correction_color_match.cpp`,
  Show Eyedropper Info on by default; tests `tst_color_match`, `tst_correction_tool`), is done.
  6.10c3b, custom colours in bins (`src/media/custom_color.*`, `customColorMime()`, Saved Color Labels, Eyedropper Picks
  from Anywhere through `ColorMatchControl::colorAt`), is done, so 6.10c is.
  6.10d is in four parts (ROADMAP). 6.10d1, relationship corrections (`src/model/correction_relations.*`: Src Tape,
  Src Clip Name, Master Clip, Sub Clip and Prog Track through `Segment::sourceRelationship`/`programRelationship`,
  `Timeline::setCorrectionRelationship` converting, the Correction Type button following the segment, the Timeline's
  Color Correction indicator lines; tests `tst_correction_relations`, `tst_correction_tool`, scenario
  `39-correction-relationships.json`), is done. 6.10d2a, Color Correction effect templates (`src/model/correction_template.*`
  as Effect items, `correctionTemplateMime()`, `src/ui/timeline_correction.cpp`; the tool's template button and buckets
  C1-C8 with `cc.save_correction`/`cc.bucket_1`-`8`, drops on a tab, the monitors and Timeline segments, the bin's
  Enter/double-click; tests `tst_correction_relations`, `tst_correction_tool`, scenario `40-correction-templates.json`),
  is done. 6.10d2b, our Color Correction Presets (`EffectInfo::correctionPreset`) and the Effect Palette's automatic
  templates (`Timeline::automaticCorrections`, listed by `MainWindow::refreshTemplateBins`), is done, so 6.10d2 is.
  6.10d3a, keyframes on corrections (`src/model/correction_keyframes.*`, `ColorCorrection::keyframes`, `correctionAt` in
  the compositor, `adjustedAt` behind `Timeline::setCurrentCorrection`; tests `tst_correction_keyframes`,
  `tst_compositor`, `tst_correction_tool`), is done. 6.10d3b, the correction in the Effect Editor in Color Correction
  mode (`src/model/correction_effect.*`: generated categories HSL Master ... Secondary <vector>, -360 to 360 hues;
  `Timeline::setStoredCorrection`; tests `tst_correction_keyframes`, `tst_correction_tool`), is done. 6.10d3c, the
  keyframe markers below the Current monitor (`CorrectionKeyframeBar` in `correction_monitors.cpp`,
  `Timeline::correctionKeyframeMarkers` and its selection, which adjustments change through `model::adjustedAt`) and
  Curves and Levels as track-only rows in the Effect Editor (`CorrectionParameter::trackOnly`; tests
  `tst_correction_keyframes`, `tst_correction_mode`, `tst_correction_tool`), is done, so 6.10d3 is. 6.10d4, Update,
  Merge, Flatten and Remove Correction (`src/model/correction_manage.*`, `correction_manage_dialog.*`, the tool's
  `shortcutMenu`, `Timeline::manageCorrections`, `cc.update_correction` ...; tests `tst_correction_manage`,
  `tst_correction_tool`, `tst_main_window`, scenario `41-update-correction.json`), is done, so 6.10d is.
  6.10e is in five parts (ROADMAP). 6.10e1, the Color Correction effect as a correction (CC Effect in the Correction
  Type menu: `Timeline::setCorrectionOnEffect`, `putEffectCorrection`; filler and empty top tracks; the AutoCorrect
  corrections through `Timeline::setAutoCorrection`; the effect on filler in the compositor; tests `tst_correction_tool`,
  `tst_compositor`, scenario `42-correction-effect-top-track.json`), is done. 6.10e2, the Color Info tool
  (`src/ui/color_info.*` as the "ColorInfo" panel, `tools.color_info_tool`, the Color Correction tool's Color Info
  button, the Effect Editor's eyedropper opening it; test `tst_color_info`), is done. 6.10e3a, shapes on the Color
  Correction effect (`src/model/correction_shapes.*`: Intraframe objects with Softness, the "Shape Correction" layer,
  Serial Mode and Invert Alpha; `media::colorCorrectedShapesPicture`/`correctionShapesMask`; test `tst_compositor`), is
  done.
  6.10e3b1, the Color Correction tool's shape panel (`src/ui/correction_tool_shapes.cpp`: Shapes/Base through
  `Timeline::setCorrectionLayer`, Serial Mode, Invert Alpha and Softness as undo steps through
  `Timeline::editCorrectionEffect`, Show Alpha and Show Result as `Composition::shapeView`; tests `tst_correction_tool`,
  `tst_compositor`), is done.
  6.10e3b2, drawing shapes in the Current monitor (`src/ui/correction_shape_host.*`: the Intraframe tool behind
  `IntraframeHost`, which the Effect Editor also implements; `CorrectionMonitors::setOverlayInput`/`setOverlay`,
  `Timeline::setCorrectionShapeObjects`; test `tst_correction_tool`), is done.
  6.10e3b3, the Color Correction effect in the Effect Editor (drawing tools, Layers, Shapes, Shape Tracking;
  `EffectEditor::drawsObjects`/`chooseShapeTracker`, `model::correctionShapeView`/`keepCorrectionShapeChange`) and shapes
  on trackers (`model::objectTracker` applied in `placedShapeAt`; tests `tst_intraframe`, `tst_intraframe_ui`), is done,
  so 6.10e3 is.
  6.10e4a, the Safe Colors Project setting (`src/model/safe_colors.*`: limits, units, Rec. 601 composite levels,
  `safeViolations`; the settings' `number` and `safeColorGraph` option types, `src/ui/safe_color_graph.*`,
  `MainWindow::safeColorLimits`, the tool's orange Safe Color Settings button; tests `tst_safe_colors`,
  `tst_settings_window`), is done.
  6.10e4b, safe colour warnings (`model::pictureLevel`/`warnedViolations`, `media::pictureViolations` and the scopes'
  legal and illegal colours through `scopeImage`'s `limits`, the triangle and indicators in Color Correction mode's
  monitors through `CorrectionMonitors::setSafeColors`, the Color Match swatches' ⚠ through
  `ColorMatchControl::setSafeColors`; tests `tst_safe_colors`, `tst_scopes`, `tst_correction_mode`,
  `tst_correction_tool`), is done.
  6.10e4c1, the Safe Color Limiter effect (`src/model/safe_color_limiter.*`: its parameters, Rec. 601 limiting and 422
  Safe; `detail::safeColorLimiterPicture`; seeded from the Safe Colors setting through `Timeline::setEffectSeed`; tests
  `tst_safe_colors`, `tst_compositor`, `tst_effect_mode`), is done.
  6.10e4c2, the limiter's analysis display in the Source monitor (`media::safeColorAnalysisImage`,
  `Composition::limitSafeColors`, `EffectEditor::safeColorAnalysis`, `Composer::setMonitorOverride`; tests `tst_scopes`,
  `tst_effect_mode`), is done, so 6.10e4 is.
  6.10e5, the Spot Color Correction effect and the Paint Effect's Color Match mode (Spot Color Correction among the
  Intraframe effects, `spotCorrected`/`colorMatched` in `src/media/intraframe.cpp`, the Color Match Input/Output
  swatches; test `tst_intraframe`), is done, so 6.10e is.
  6.10f, the owner's report of huge windows on Windows 11, is done for the sizes: it was the Titler+ Dashboard (2700
  pixels of sections in a row), now scrolling and fitted to the screen; every floating panel and tool window goes through
  `fitOnScreen`/`keepOnScreen` in `panel_manager.cpp` (test `tst_main_window toolWindowsFitTheScreen`, BUILDING.md).
  Whether closing Titler+ should leave Effect mode is still open (ROADMAP 6.10f).
  6.11, the OFX host in place of AVX, is in three parts (ROADMAP). 6.11a, the host's core (docs/spec/ofx-host.md: the
  OpenFX 1.5.1 headers in `third_party/openfx`, `src/media/ofx_host.*` with our suites, the bundle scan at start-up in
  `app/main.cpp` with `LUMENIS_NO_OFX=1`, the describe actions, plug-ins in the Effect Palette by vendor through
  `EffectInfo::ofxPlugin` with the plug-in icon, reasons in the Console; our test bundle `tests/media/ofx_test_plugin.cpp`;
  tests `tst_ofx_host`, `tst_ofx_palette`), is done. 6.11b1, rendering (`ofxSegmentPicture`/`ofxTransitionPicture`
  from the compositor, one instance per plug-in with `closeOfxInstances`, values and keyframes through the parameter
  suite, bottom-up float RGBA clip images; the parameters as Effect Editor controls through `ofxEditorParameters` under
  "<label> Plug-In Parameters"; `setOfxFrameRate` from `MainWindow`), is done. 6.11b2, missing plug-ins (the "OFX
  Plug-In" tag written by `Timeline::seededEffect` through `markOfxEffect`, `isMissingOfxEffect`, the blank Timeline icon,
  "Can't find effect" in the Console), templates of plug-in effects and the Console's `closeallofxplugins`, is done, so
  6.11b is. 6.11c is in three parts (ROADMAP). 6.11c1, several inputs (General described too, `OfxPluginInfo::inputs`,
  the OFX Optional Inputs dialog `ofx_inputs_dialog.*` through `Timeline::setOptionalInputsChooser`, `ofxInputCount` in
  the tag, the inputs as the effect's nest tracks through the compositor's `nestTrackPicture`), the Angle control
  (`angle_dial.*`) and push buttons (`ofxPushButton`, `EffectEditor::pressPluginButton`), is done. 6.11c2, overlays (V2
  interacts through our Draw suite, `ofxOverlayDraw`/`ofxOverlayPen`, `Composer::EffectPreview::pluginShapes`, the
  Effect Editor's `PluginOverlayInput` with Outline/Path) and the project's size as canonical coordinates
  (`setOfxProjectSize`, render scale), is done. 6.11c3, colour management (ofxColour.h through `src/media/ocio_colour.*`
  with OpenColorIO where it is installed, optional: `cmake/FindOpenColorIO.cmake`, `LUMENIS_OCIO`; `setOfxColourSpace`
  from `MainWindow`, `ofxNativeColourspace`, `ofxColourStyle`; without it plug-ins see the project's colour space;
  `LUMENIS_NO_OCIO=1`; test `tst_ofx_host` with the halve plug-in), is done, so 6.11 and Phase 6's steps are.
  Phase 7 (output and finishing) is planned in five steps (ROADMAP, 2026-11-06). 7.1a, MOV and MP4 export
  (docs/spec/movie-export.md: `src/media/movie_export.*` with `media::exportMovie` and the compression table,
  `MainWindow::exportMovieFile` composing frames and mixing sound, the Export setting's MOV and MP4 tab; `MediaWriter`
  checks the codec behind an encoder, `WriterSettings::fullRange`; tests `tst_movie_export`, `tst_movie_export_ui`),
  is done. 7.1b, Graphic, DPX, OpenEXR and Audio export (`media::writeStill`, `exportAudioFile`,
  `MainWindow::exportStills`; `WriterSettings::videoOptions`/`quality`), is done. 7.1c1, AAF's Export Methods (Copy All
  Media, Consolidate Media, Video Mixdown: `MainWindow::prepareAafMedia`, `media::copyClipMedia`; docs/spec/aaf.md), is
  done. 7.1c2, Send To (docs/spec/send-to.md: `.stt` templates in `src/model/send_to.*`, `send_to_dialog.*`,
  `MainWindow::sendTo`/`makeNewSendTo` exporting through `writeExport` with `exportOverride_`, the Pro Tools options;
  test `tst_send_to`), is done. 7.1c3, the Simplified AAF and Export Media Difference (docs/spec/aaf.md: `src/model/
  simplify_export.*`, shipped setting versions `SettingKind::versions`, `MainWindow::setExcludeSequenceHook`), is done.
  7.2 is in four parts (ROADMAP). 7.2a, MXF OP1a, XDCAM MXF OP1a and MXF OP1a (legacy) through FFmpeg (docs/spec/
  movie-export.md "MXF OP1a": `MovieContainer::Mxf`/`Xdcam`/`MxfLegacy`, the XDCAM and AVC Long-GOP entries of
  `movieCompressions()`) and DPX's options (`media::setDpxHeaderInfo`), is done. 7.2b, Export to Device (docs/spec/export-device.md: `ext::ExportDeviceProvider`,
  XDCAM disks as folders `media::XdcamDiskProvider` with `LUMENIS_XDCAM_DISKS`, `ExportDeviceDialog`,
  `MainWindow::exportToDevice`; P2 through a module), is done. 7.2c, AS-11 through bmx's bmxtranswrap
  (docs/spec/as11.md: `src/media/as11.*`, `LUMENIS_BMXTRANSWRAP`, segments from spanned markers), is done. 7.2d, AS-02 Export
  Volumes (docs/spec/as02.md: `model::ExportVolume` kept in `Bin::exportVolume`, `ExportVolumeDialog`, the bin's volume
  bar, `MainWindow::commitExportVolume`, `media::writeAs02`), is done, so 7.2 is. 7.3 is in three parts (ROADMAP). 7.3a, decks and the Digital Cut tool
  (docs/spec/digital-cut.md: `ext::DeckProvider`, tapes kept in folders `media::FolderTapeDeckProvider` with
  `LUMENIS_TAPE_DECKS`, `DigitalCutTool`, `MainWindow::digitalCut` through exportMovieFile's "sink"), is done. 7.3b, the Video Output tool (docs/spec/video-output.md: `ext::VideoOutputProvider`,
  `VideoOutputTool` with the project's Video Output setting, `media::testPattern`) and Output Gain, is done. 7.3c, the
  AJA and Blackmagic modules, needs their SDKs and hardware. 7.4a, the SubCap
  effect (docs/spec/subcap.md: `src/model/subcap.*` with Avid DS and EBU N19 files, `media::subcapPicture`,
  `src/ui/subcap_dialogs.*`, `MainWindow::subcapAction`; tests `tst_subcap`, `tst_subcap_ui`), is done. 7.4b1, closed
  captions on D1 (docs/spec/closed-captions.md: `src/model/closed_captions.*` with SCC, CEA-608 decoding and TTML,
  `MainWindow::importScc` with `AncDataImportDialog`, `Timeline::captionTrack`/`TimelineView::paintCaptions`, Show
  Captions through `Composer::shownCaption`; tests `tst_closed_captions`, `tst_closed_captions_ui`, fuzz target `scc`),
  is done. 7.4b2, the Timeline Quick Find (docs/spec/timeline-find.md: `src/ui/timeline_find.cpp`, `Timeline::findNext`
  and `findOptionsMenu`; test `tst_timeline_find`), is done, so 7.4 is. 7.5, DCP export (docs/spec/dcp.md, ours: Export
  As DCP, `src/media/dcp.*` with asdcplib's asdcp-wrap found like bmxtranswrap, `LUMENIS_ASDCP_WRAP`; tests `tst_dcp`,
  `tst_movie_export_ui exportDcp`), is done. 7.6, the Titler+ Dashboard and Effect Editor laid out as the FX Guide's
  pictures (p.302, p.321; `TitlerPanel`, `GlyphIcon`s in theme.h; test `tst_main_window toolWindowsFitTheScreen`,
  `LUMENIS_SHOT_DIR` for pictures), is done. Phase 7 is done but 7.3c (hardware modules). Phase 8 has started (steps in ROADMAP).
  8.1a1, group clips (docs/spec/multicamera.md: `src/model/group_clip.*` with each angle's clip kept in the group,
  `Segment::groupAngle` played through `model::clipForSegment`; Group Clips with `group_clips_dialog.*` and
  `MainWindow::groupClips`; Quad and Nine Split through `Composer::setGroupSplit` and `MainWindow::groupSplitImage`;
  Previous/Next In Group with `MainWindow::stepInGroup`, `Timeline::stepGroupAngle` and the arrow keys' hook; tests
  `tst_group_clip`, `tst_multicam_ui`, scenario `43-group-clip.json`), is done. 8.1a2, syncing by sound and editing
  groups (`src/media/waveform_sync.*` with `media::waveformOffset`, Waveform Analysis through `GroupOptions::waveform`,
  `Timeline::waveformSync`; `model::groupEditSequence`/`groupFromSequence`/`refreshGroupEdits` with
  `Segment::groupOffsets` and `Sequence::editedGroup`; `MainWindow::editGroup`/`updateGroup`/`createGroupFromSequence`/
  `refreshGroupEdits`; tests `tst_waveform_sync`, `tst_group_clip`, `tst_multicam_ui`, scenario
  `44-waveform-sync.json`), is done, so 8.1a is. 8.1b1, MultiCamera mode (`Composer::setMultiCameraMode`,
  `MainWindow::multiCamPicture`/`splitClicked`/`multiCamKey` through the Composer's picture click and keypad handlers,
  `model::groupSegmentAt`/`setGroupAngleAt`/`keypadSplitPosition`, `Timeline::setGroupAngle` cutting while playing; the
  Composer settings' MultiCam tab; tests `tst_group_clip`, `tst_multicam_ui`, scenario `45-multicam-cut.json`), is done. 8.1b2,
  the Group menu with Audio Follow Video (`Composer::setGroupMenu`/`audioFollowVideo`), the Multi-angle View menus
  (`MainWindow::multiAngleMenu`, `Composer::setSplitCell` with the sequence as `kSplitSequence`), Match Frame in groups,
  Commit Multicam Edits (`model::commitMulticamEdits`, the bin's right-click menu through `BinView::commandRequested`;
  scenario `46-audio-follow-video.json`), is done. 8.1b3, MultiGroup (`model::multiGroup`, multigroups as Group items
  holding a sequence of group segments, `multigroupSequence`; `MainWindow::multiGroup`/`sourceGroup`/
  `switchMultigroupAngle`; the bin's plus icon; tests `tst_group_clip`, `tst_multicam_ui`), is done, so 8.1b is. 8.1c, the
  Group menu's patching (`GroupClip::trackAngles`, `model::groupTrackAngle`/`anglesWithTrack`, `MainWindow::groupMenuInfo`/
  `chooseGroupTrackAngle`, `Timeline::setGroupTrackAngle`) and Change Source Track on group segments, is done (scenario
  `47-group-track-angles.json`); stereoscopic groups wait for 8.6.
  8.2, transcripts and PhraseFind, is in five parts (ROADMAP). 8.2a, transcripts and the engine (docs/spec/transcripts.md:
  `src/model/transcript.*` with the `TranscriptStore` outside the project (`LUMENIS_TRANSCRIPTS`) and
  `TranscriptionBins` in it; `src/media/transcription.*` with `WhisperProvider` running whisper.cpp's `whisper-cli`
  (`LUMENIS_WHISPER`, `LUMENIS_WHISPER_MODEL`); `main_window_transcripts.cpp` queuing clips on the Background Queue while
  idle; Include/Exclude Bin in Transcription and the Sidebar's Transcription column; the Transcript Project setting; tests
  `tst_transcript`, `tst_transcription`, `tst_transcripts_ui`), is done. 8.2b1, the Transcript Tool (`src/ui/transcript_tool.*`
  as the "Transcript" panel, `model::shownTranscript` in `src/model/transcript_view.*`, `MainWindow::refreshTranscriptTool`/
  `transcribeNow`; tests `tst_transcript`, `tst_transcripts_ui`), is done. 8.2b2a, editing transcripts (`model::editWords`/
  `replaceWords`/`renameSpeaker`/`identifySpeaker`/`correctSpeaker`/`startNewLine`/`revertLine`/`revertTranscript`,
  `model::groupedLines`, the tool's in-place editor and right-click menus, `MainWindow::editTranscript`), is done. 8.2b2b,
  Manage Speaker Names (`src/ui/speaker_names_dialog.*` from the Transcript setting's "button" option, `model::ProjectSpeakers`
  in `<project>/Speakers.json`, `renameSpeakerName`/`mergeSpeakerNames`/`clearSpeakerName`, `MainWindow::manageSpeakers`)
  and the New Project tab's Language Hint (`SelectProjectDialog`), is done. 8.2c, PhraseFind AI in the Find window
  (`src/model/phrase_find.*` over the transcripts, `FindWindow::phraseFind` with Score and Transcription, the PhraseFind
  AI Index and Settings menu, `MainWindow::connectPhraseFind`/`phraseIndexStatus`/`setTranscriptionIndexing`; tests
  `tst_transcript phraseFind`, `tst_transcripts_ui phraseFindWindow`), is done. 8.2d, transcripts out (the bin's and
  Timeline's Transcript menus, `src/model/transcript_export.*` with the exported text as Avid DS captions, captions,
  `createSubCapTrack` and our `.cttr` shared file; `src/ui/transcript_dialogs.*` with the Transcribe window and Manage
  Transcripts; `MainWindow::exportTranscripts`/`shareTranscripts`/`deleteTranscripts`/`exportSubCap`/`createSubCap`/
  `manageTranscripts`; tests `tst_transcript transcriptsOut`, `tst_transcripts_ui transcriptsOut`), is done. 8.2e,
  AutoCut (`GroupClip::speakerAngles` from the Transcript Tool's Assign to Camera Angle, `model::autoCut`, the Timeline's
  AutoCut through `MainWindow::autoCut`; tests `tst_group_clip autoCutFromSpeakers`, `tst_transcripts_ui
  autoCutFromTranscript`, scenario `48-autocut.json`), is done, so 8.2 is.
  8.3, ScriptSync, is in five parts (ROADMAP). 8.3a, scripts and the Script window (docs/spec/scriptsync.md:
  `src/model/script.*` with the script kept in its bin item `BinItemKind::Script`; `src/ui/script_window.*` as the
  "Script" panel; `main_window_script.cpp` with New Script, Save Script, Save a Script Copy As and Text Layout; the
  "Script" User setting; tests `tst_script`, `tst_script_ui`), is done. 8.3b, slates and takes (`model::ScriptSlate`/
  `ScriptTake` with `addSlate`, `moveSlate`, `toggleOffscreen` ...; the window's slates drawn over the text, drops of bin
  items, selection, drags, Delete Takes, Set Offscreen/Set Color, `takeLoadRequested`/`takesPlayRequested` handled by
  `MainWindow::loadScriptTake` and `playClipLoop`; Script > Color, Show Frames, Show All Takes; tests `tst_script
  slatesAndTakes`, `tst_script_ui slatesAndTakes`), is done. 8.3c, script marks (`ScriptTake::marks`, `model::addScriptMark`/
  `scriptLinesAt`/`markedFrames`; the window's marks, Record button screening through `playClipLoop`, Find Bin; `MainWindow::
  addScriptMark`/`findScript`/`findScriptBins`/`spliceScriptRange`, `other.add_script_mark`/`other.find_script`, the
  Pre-roll; tests `tst_script scriptMarks`, `tst_script_ui scriptMarks`), is done. 8.3d, ScriptSync AI and Create Script from Clip
  (`src/model/script_sync.*` aligning script words with the transcript's, `src/ui/script_sync_dialog.*`, `MainWindow::
  scriptSyncAI`/`createScriptFromClips`, `Script::fromClips`; tests `tst_script scriptSync`, `tst_script_ui
  scriptSyncAndCreateScript`), is done. 8.3e, shared scripts and the Find window's Script Text (the script lock icon from
  the bin's lock through `MainWindow::updateScriptLock`/`scriptLockClicked`, `ScriptLock`, `MainWindow::closeScript` with
  Save Script As / Don't Save / Cancel; `model::findScriptText`/`scriptTakeBins`, `FindWindow::setScriptSource`; tests
  `tst_script findInScripts`, `tst_script_ui sharedScripts`), is done, so 8.3 is.
  8.4, Dynamic Media Folders and the background services, is in three parts (ROADMAP). 8.4a, the background services
  (docs/spec/background-services.md: `BackgroundQueue::ServiceState` with start/stop/pause and the pause during playback,
  `src/ui/background_services.*` with the Timeline's `BackgroundIndicator` and `BackgroundServicesDialog`,
  `main_window_services.cpp`; test `tst_background_services`), is done. 8.4b, Dynamic Media Folders and profiles
  (`src/media/dynamic_media_folders.*` with `processDmfEntry` linking, filling the AIS columns and running Copy to
  Folder, Consolidate and Transcode; `main_window_dmf.cpp` watching enabled folders and queuing entries, the assets
  for the green dot; tests `tst_dmf`, `tst_background_services dynamicMediaFolders`), is done. 8.4c, the Dynamic Media
  Folders window and the Profile Editor (`src/ui/dynamic_media_folders_window.*` as the "DynamicMediaFolders" panel,
  `MainWindow::editDmfProfile`/`acquireDmfAssets` with offline clips while processing; test `tst_background_services
  dmfWindowAndAcquire`), is done, so 8.4 is.
  8.5, FrameFlex and colour adapters, is in five parts (ROADMAP). 8.5a, Reformat and spatial adapters
  (docs/spec/frameflex.md: `src/model/frame_flex.*` with the Reformatting Options column, FrameFlex boxes in
  `BinItem::sourceSettings` and `Segment::spatial`; `src/media/frame_flex.*` drawing them through
  `MainWindow::adaptedFrame`; the Link Options' Reformatting option; Refresh Sequence > Aspect Ratio and Reformatting
  Options and All FrameFlex; tests `tst_frame_flex`, `tst_frameflex_ui`, scenario `49-spatial-adapter.json`), is done.
  8.5b1, the Source Settings dialog's FrameFlex tab (`src/ui/source_settings_dialog.*` with `FramingViewer`,
  `MainWindow::sourceSettings` from the bin's and Source monitor's right-click `bin.source_settings`; test
  `tst_frameflex_ui sourceSettingsDialog`), is done. 8.5b2, the Timeline's Source Settings and the Playback Rate tab
  (`Timeline::sourceSettingsRequested`, `MainWindow::sourceSettingsFor`; `Link::conformed` through
  `media::setPlaybackAtProjectRate`, Refresh Sequence dropping adapters; test `tst_frameflex_ui playbackRate`), is done.
  8.5b3, panning (the FrameFlex effect: `SpatialAdapter::framing` with `model::framingEffect`/`framingAt`/`adapterAt`,
  `EffectTarget::Spatial` in `effectAt`/`effectFor`, `model::Effect` now in `effect.h`; the box over the whole image in the
  Effect Preview through `media::framingPreviewAdapter`/`framingHandles`/`dragFraming` and `EffectEditor::framingSegment`;
  the Layout button `EffectEditor::setFramingBypassed`/`resetFraming`; tests `tst_frame_flex framingEffect`,
  `tst_frameflex_ui panningShot`), is done, so 8.5b is. 8.5c1, colour adapters (docs/spec/color-adapters.md:
  `model::ColorEncoding` in `src/model/color_adapter.*` kept in the clip's Source Settings with the Color Transformations
  column, `Segment::color`, `refreshColorAdapters`/`removeColorAdapters` behind Refresh Sequence > Color Adapters and Remove
  Color Adapters; the transformations in `src/media/color_transform.*` with `media::registerColorTransformation` for
  providers; `MainWindow::adaptedFrame`/`refreshSequences`; tests `tst_color_adapter`, `tst_frameflex_ui colorAdapters`), is
  done. 8.5c2, the Source Settings' Color Encoding tab (`SourceSettingsDialog::setColorEncoding`/`autoTransformations`/
  `moveTransformation`/`shownFrame`, the Applier's encoding) and Transcode's Apply source transformations > Color encoding
  (`ConsolidateOptions::applyColorEncoding`, `MediaCreation::colorTransformations`; test `tst_media
  transcodeColorEncoding`), is done, so 8.5c is. 8.5d1, external LUTs and CDLs (`src/media/lut_file.*` reading .cube/.itx,
  .3dl and Autodesk tables, `registerLutFolders` from the project's `LUTs` and `sharedLutFolder()` with
  `LUMENIS_SHARED_LUTS`, "External <name>"; `media::cdlTransformationName` and the CDL formulas in `color_transform.cpp`;
  `SourceSettingsDialog::setClipCdl`; fuzz target `lut`; tests `tst_color_adapter cdl`/`lutFiles`), is done. 8.5d2, the
  Color Management User setting (Insert color transformations through `LinkOptions::insertColorTransformations` and
  `media::sourceSpaceOfTransfer` from `StreamInfo::colorTransfer`, Use CDL values for Auto, Installed LUTs and Select LUT
  file `MainWindow::installLutFile`, the tab's Color Management Settings button `openColorManagementSetting`) and CLF
  files baked through OpenColorIO (`detail::ocioBakeFile`; tests `tst_color_adapter clfAndTransfers`, `tst_frameflex_ui
  colorAdapters`), is done, so 8.5d is. 8.5e is in four parts (ROADMAP). 8.5e1, the monitors' Display Color Space
  (`Composer::setDisplayColorSpace`/`displayed`, `src/media/display_color.*`; tests `tst_color_adapter displayColorSpace`,
  `tst_composer_display displayColorSpace`), is done. 8.5e2, the proxy mode (the Format tab's Proxy `formatProxy`,
  `ProjectSettings::proxy` with `model::proxyModes`/`proxyRaster`, `MainWindow::setProxyMode`; renders per mode through
  `model::setRenderProxy` in the render key and `EffectRender::kept`; tests `tst_effect_render proxyModes`,
  `tst_project_store proxyModes`, `tst_render_ui proxyMode`), is done. 8.5e3, mask margins and Target Mask (the Format
  tab's Mask Margins with `src/ui/target_settings_dialog.*`, `ProjectSettings::mask` and `src/model/mask_margins.*`,
  `MainWindow::setMaskMargins`; `Composer::setTargetMask`, the Composer setting's Viewer tab, Full Screen Playback's
  Target Mask, `media::paintTargetMask`/`burnMaskMargins` in `src/media/target_mask.*` for Export's Enable Mask Regions;
  tests `tst_project_store maskMargins`, `tst_composer_display targetMask`, `tst_render_ui proxyMode`), is done. 8.5e4
  is in two parts (ROADMAP). 8.5e4a, the Viewer tab's Source and Record Monitor Color Space as Display Color Space's
  choices (Project, Rec709, Rec709 (full range), SRGB, DCI P3, Rec2020, ACESproxy; `media::displayColorSpaceOfValue`;
  tests `tst_color_adapter displayColorSpace`, `tst_composer_display displayColorSpace`), is done. 8.5e4b1, ACES's
  encodings and colour adapters (`src/media/aces.*`; the Color Adapter Type menu through `media::colorAdapterTypes`/
  `colorTransformationsOfType`, `SourceSettingsDialog::setAcesProject`; the ACEScct working space through
  `media::withAcesWorkingSpace`; ACESproxy and the output transform on the monitors in `display_color.cpp`; tests
  `tst_color_adapter acesProjects`, `tst_frameflex_ui acesColorAdapterTypes`), is done. 8.5e4b2, ACES projects' Format tab
  (ACES version, Output Color Space `ProjectSettings::outputColorSpace` through `MainWindow::setOutputColorSpace`, exported
  frames through `MainWindow::outputFrame`), the blue Video Quality Menu `Timeline::setFloatingPointPrecision` and Transcode
  in the working space (`ConsolidateOptions::acesWorkingSpace`; tests `tst_render_ui acesProject`, `tst_project_store
  acesOutputColorSpace`), is done, so 8.5e and 8.5 are. 8.6, MultiRez and Dynamic Relink and stereoscopic projects, is in
  four parts (ROADMAP). 8.6a, MultiRez media and the Dynamic Relink Project setting (docs/spec/dynamic-relink.md:
  `src/media/dynamic_relink.*` with a clip's media as the sets naming its MobID, `MediaDatabase::setsOf`, quality matching
  `media::chooseMedia`, the affinity link kept under "multiRez"; `src/ui/main_window_relink.cpp` relinking through
  `Composer::setLoadFilter` and when the setting changes; Transcode's Create new clips `ConsolidateOptions::newClips`;
  tests `tst_dynamic_relink`, `tst_render_ui dynamicRelink`), is done. 8.6b, the MultiRez button (`src/ui/timeline_multirez.cpp`:
  `Timeline::MultiRezDisplay`, `multiRezMenu`, `findNextMismatched` with Ctrl+/ `timeline.find_next_mismatched`,
  `updateMediaStatus`; `MainWindow::clipResolution`/`renderAtOtherResolution`; Resolution Tracking in
  `model::ClipColorOptions` and the Clip Color dialog; Clip Text > Clip Resolution following the media; tests
  `tst_render_ui multiRezButton`, `tst_clip_colors resolutionTracking`), is done. 8.6c, partial and shared media
  (`media::associatedMedia`/`mediaCoverage` by timecode span and source, the MultiRez bin headings through
  `BinAppearance::multiRezHeadings`/`multiRezCell` and `MainWindow::multiRezCell`, the Video column following the media,
  Delete's affinity files `media::affinityMediaFiles` by resolution, Cross Rate Dynamic Relink in `chooseMedia`, Transcode
  of sequences without new clips; tests `tst_dynamic_relink crossRate`/`partialAndShared`/`sequenceWithoutNewClips`,
  `tst_render_ui multiRezButton`), is done. 8.6d is in three parts (ROADMAP). 8.6d1, stereoscopic clips and the
  project's Stereoscopic option (docs/spec/stereoscopic.md: Stereo Master Clips in `src/model/stereo_clip.*` with both
  eyes' links under the clip's Source Settings "stereo" and the S3D Leading Eye column, `MainWindow::createStereoscopicClip`/
  `setStereoLeadingEye`/`setProjectStereoscopic` from the bin's right-click menu, the modes in `MainWindow::adaptedFrame`
  with `media::frameCompatible` in `src/media/stereo.*`; tests `tst_stereo`, `tst_render_ui stereoscopic`), is done.
  8.6d2, the S3D Views and Overlays and the S3D Settings (`model::S3DView`/`S3DSettings` in `src/model/stereo_view.*`,
  `media::stereoView`/`drawParallaxGuides`; the Composer setting's Viewer tab, Full Screen Playback through
  `FullScreenView::setPictureSource`, the Video Output tool's `s3dView`/`s3dOverlay`; `MainWindow::stereoPicture` composing
  each eye through `eyeOverride_`; the "S3D" Project setting with the parallax worked out in `applySettings`; tests
  `tst_stereo`, `tst_render_ui stereoViews`), is done. 8.6d3, the rest (stereo clips in groups shown by the Stereoscopic
  option in the Source monitor, the List Tool's S3D Contributor through `interop::edl::ExportOptions::s3dContributor`,
  Refresh Sequence > Stereo Correction Effects dimmed, the "2K DCI Full S3D" preset, Full Quality only through
  `Composer::setFullQualityOnly`; tests `tst_edl s3dContributor`, `tst_render_ui interlacedStereoFullQuality`), is done,
  so 8.6d and 8.6 are. 8.7 is in four parts (ROADMAP). 8.7a1, proxy media (docs/spec/proxy-media.md: `src/model/proxy.*`
  kept in the clip's Source Settings "proxy", `media::makeProxyMedia` writing H.264 and DNx LB with AAC through the movie
  export, `MainWindow::createProxies`/`makeProxies`/`playedClip`/`proxyCell` in `main_window_proxy.cpp`, the Create
  Proxies window `proxy_dialogs.*`, the Play button's proxy menu `Composer::setProxyPlayback`, the orange icon and the
  proxy columns through `BinAppearance::proxyCell`; tests `tst_proxy`, `tst_render_ui proxyMedia`), is done. 8.7a2,
  Copy Media (`src/media/copy_media.*` with `media::planCopyMedia`, `CopyMediaDialog` in `proxy_dialogs.*`,
  `MainWindow::copyMediaRequest`/`copyMedia`/`runCopyMedia` from the bin's and the Sidebar's right-click menus) and the hook
  for shared workspaces and Distributed Processing (`ext::SharedStorageProvider` in `src/ext/.../shared_storage.h`,
  `MainWindow::mediaTargets`/`submitDistributedProxies`/`pollDistributedJobs`; tests `tst_copy_media`, `tst_render_ui
  copyMedia`/`distributedProcessing`), is done, so 8.7a is. 8.7b, peer-to-peer transcript sharing (docs/spec/transcripts.md:
  the Transcript setting's Transcript Sharing and Shared Transcripts Folder through `MainWindow::applyTranscriptSharing`,
  `model::setTranscriptSharing`/`hostTranscripts`, entry locks `TranscriptStore::lock`/`lockOf` with `LUMENIS_STATION`,
  the take-over question in `MainWindow::saveTranscript`/`editTranscript`; tests `tst_transcript peerToPeerSharing`,
  `tst_transcripts_ui peerToPeerSharing`), is done. 8.7c is in four parts (ROADMAP). 8.7c1a, Clip > Modify > Set Format
  on sequences (docs/spec/sequence-format.md: `model::setSequenceFormat` in `src/model/sequence_format.*`, the Modify
  dialog's Format menu, `MainWindow::modifySelection`; tests `tst_sequence_format`, `tst_multichannel_ui
  setFormatOnSequences`), is done. 8.7c1b, changing the project format on the Format tab (the Project Type menu through
  `SettingsWindow::projectFormatChanged` and `MainWindow::setProjectFormat`, new sequences' Format through
  `BinContainer::setSequenceFormat`; test `tst_settings_window changingProjectFormat`), is done. 8.7c2a, Universal
  Mastering's remastered sample rates in Change Sample Rate (`model::masteringConversions` in
  `src/model/universal_mastering.*`; tests `tst_sequence_format universalMastering`, `tst_clip_audio`), is done.
  8.7c2b, the Digital Cut tool's Output Format menu (`model::outputFormats` in `src/model/output_format.*` with the
  2:3 cadences and Pullin B, `DigitalCutTool::setOutputFormats`, `MainWindow::digitalCut` laying frames and sound onto
  the tape's rate, `updateDigitalCutDuration`; tests `tst_sequence_format outputFormatsAndPulldown`, `tst_digital_cut
  outputFormats`), is done, so 8.7c2 is. 8.7c3, editing at 60 fps and 24p/25p display (single-field steps
  `Composer::parkedField` beeping in progressive projects, Draft at 30 fps `Composer::shownFrame`, the 30 fps deck's odd
  Mark IN in `MainWindow::digitalCut`; tests `tst_composer_display fieldStepsAndSixtyFps`, `tst_digital_cut
  sixtyFpsMarkIn`), is done, so 8.7c is. 8.7d1, the Symphony Option audit (docs/spec/symphony-audit.md) and font
  replacement for classic titles (`model::FontMap`/`replaceFont` in `src/model/font_replacement.*`,
  `ClassicTitle::fontReplacements`, `TitleToolWindow::replaceUnknownFonts`/`redefineFontReplacement`, the "Font
  Replacement" Site setting through `MainWindow::fontReplacements`; tests `tst_classic_title fontReplacement`,
  `tst_title_clips fontReplacement`), is done. 8.7d2, Mouse Jog and Mouse Shuttle (`Composer::startMouseControl`/
  `mouseControlMoved`/`shuttleFaster` with an application event filter; test `tst_composer_display mouseJogAndShuttle`),
  is done. 8.7d3, the Frame view's represented frame (p.251: `BinFrameView::roll` for K+L/K+J, `representEnd` for
  Home/End, its own Mouse Jog/Shuttle, kept in `BinDisplay::representedFrames`; test `tst_bin_views
  frameIdentifyingTheClip`), is done, so 8.7d and 8.7 are, and so are Phase 8's steps. Phase 9 is planned in
  eight steps (ROADMAP, 2026-11-11). 9.1, printing (docs/spec/printing.md: `src/ui/printing.*` with `PrintDocument`,
  `main_window_print.cpp` with Page Setup, Print Bin, Print Frame, Print Timeline, File > Print for the Markers window and
  the Console, the Media Tool's table, `MainWindow::setPrintDialogHook`; Qt Print Support; test `tst_printing`), is done.
  9.2a, Batch Import and Batch Re-import (docs/spec/batch-import.md: `src/media/batch_import.*`, `BatchImportDialog` in
  `src/ui/batch_import_dialog.*`, `main_window_batch_import.cpp` with `MainWindow::batchImport`/`runBatchImport`, the
  Import settings kept with each clip through `importSettingsSnapshot`/`importClipMedia`; test `tst_batch_import`), is done.
  9.2b, graphics with alpha as Matte Key clips and PICT files (docs/spec/graphics-import.md: `src/media/graphic_import.*`,
  `src/model/matte_key_clip.*` as Title items with fill and matte precomputes, `matteKeyClipLayer` in the compositor,
  `main_window_graphics.cpp` with `MainWindow::importMatteKeyClip`/`importPictFile`, the Image tab's Alpha Channel and
  Dilate Fill; test `tst_graphics_import`), is done.
  9.2c, the Sequence Report and the Popup Report on AAF Import (docs/spec/sequence-report.md: `src/media/sequence_report.*`
  with `media::reportedEffects`/`sequenceReport`, `SequenceReportDialog` in `src/ui/sequence_report_dialog.*`,
  `main_window_report.cpp` with `MainWindow::sequenceReport`/`popupAafReports` from `bin.sequence_report` and
  `composer.sequence_report`, the Import setting's OMFI/AAF report options; test `tst_sequence_report`), is done.
  9.2d, audio CDs (docs/spec/audio-cd.md: `ext::AudioCdProvider` in `src/ext/include/lumenis/ext/audio_cd.h`, `src/media/audio_cd.*`
  with ".cda" files, `isOnDisc` and the Windows CD-ROM driver provider, `MainWindow::readCdTracks`; Apply attenuation/gain
  effect on import with CD only in `importClipMedia`; test `tst_audio_cd`), is done.
  9.2e1, multilayer Photoshop files (docs/spec/graphics-import.md: our layer reader `src/media/psd.*` with fuzz target `psd`,
  `MainWindow::importPsdLayers` with Sequence of Layers, Flattened Image and Select Layers through
  `askPsdLayersChoice`/`askPsdLayers`; test `tst_psd_import`), is done. 9.2e2, linked files with alpha as Matte Key
  clips by the Link setting's Alpha Channel (`MainWindow::linkedMatteKeyClip`, `MatteKeyMedia::linkedAlpha` keyed in
  `matteKeyClipLayer`; test `tst_graphics_import linking`), is done, so 9.2 is. 9.3, Clip > Sync Clips, the guide's
  AutoSync under our own name (docs/spec/sync-clips.md: `model::syncClips` and synced subclips holding a sequence,
  `model::composedSequence`; `SyncClipsDialog`, `MainWindow::syncClips`; tests `tst_sync_clips`, `tst_sync_clips_ui`),
  is done. 9.4 is in three parts (ROADMAP). 9.4a, film cut lists in Columnar and TabbedLists (docs/spec/cut-lists.md:
  `interop::cutlist::exportText` in `src/interop/cut_list.*` with key and ink numbers, the assemble, scene assemble,
  optical, dupe and pull lists; the List Tool's Cut List options and `ListTool::setFilmType`; tests `tst_cut_list`,
  `tst_cut_list_ui`), is done. 9.4b, the XML and WebLists templates (`cutlist::Format::Xml`/`WebLists` with
  `Result::files` and `writeFiles`, CutAsm.htm linked to CutOpt/CutDupe/CutPull.htm, `ListTool::openPage`), is done. 9.4c, change lists
  (`cutlist::exportChangeList` with insertions, deletions, head and tail trims and moves, the change pull and discard
  lists and reels by Reel #; the List Tool's Change List mode, `setOldSequence`/`setChangeSequences` and
  `loadOldRequested`), is done, so 9.4 is. 9.5, titles saved in more than one aspect ratio (the Save Title dialog's 16:9
  and 4:3 through `SaveRequest::aspects`, `ClassicTitle::aspect` cut to the 4:3 centre in `media::renderClassicTitleMedia`,
  `MainWindow::saveClassicTitle` saving one clip per aspect; test `tst_title_clips aspectRatios`), is done. 9.6a, the check for unconnected items (docs/spec/ui-inventory.md
  "Unconnected items": test `tst_command_wiring` fails for a command, key or button that reaches nothing unless it is in
  `tests/data/dimmed_commands.json` with its reason; fixed MultiCamera Mode, Digital Audio Scrub and menu keys in the
  Timeline; To the Left/Right, Select In/Out in `Timeline::perform`, Make Subclip; scenario `50-select-left-right.json`),
  is done. Remove a command from dimmed_commands.json when it gets a handler. 9.6b, audio marks and split edits
  (docs/spec/audio-marks.md: `MonitorState::markAudioIn`/`markAudioOut`, Shift+Clear, green brackets, `Timeline::edit`
  splitting through `model::withAudioMarks`/`patchOfKind`; Go to Audio Mark IN/OUT and Go to Previous/Next Trim; scenario
  `51-audio-marks.json`), is done, so 9.6 is. 9.7a, failures in messages (`reportFailure(parent, "Title: text")` in
  `src/ui/tool_windows.*` writes the Console line and opens a warning that does not block; information is marked
  `// Console only: <why>`; ctest `failure_messages` checks every Console line in src/ui; test `tst_command_wiring
  failedCommandsSayWhy`), is done. Use `reportFailure` for any new failure. 9.7b, the Export As dialog's Options as
  the guide's Export Settings dialog box with Save and Save As (`SettingsDialog::useSaveButtons`, p.869-870; test
  `tst_interchange`) and the Timeline's refusals in the Console, is done, so 9.7 is. 9.8a, the trademark audit (docs/spec/trademarks.md: each Avid mark in
  `tests/data/trademarks.json` with its source, our name and status; ctest `trademark_names` fails when a renamed or
  absent mark names something the user sees), is done. 9.8b, the texts renamed (Script Alignment, Phrase Search, Multi-Resolution, Framing, Title Designer,
  Speaker Cut, Group of Groups, Audio Processing, Smart Render, Asset Transfer ...; docs/spec/trademarks.md), is done;
  9.8c, effects and parameter categories shown under our names over their stored ones (`model::shownName` in
  `src/model/shown_names.*`: show every effect or category name through it), is done. 9.8d, other companies' marks (Adobe, Apple, Blackmagic Design, Autodesk, Boris FX, Grass Valley, MAGIX and Avid's
  own list, with their owners and USPTO serials in `tests/data/trademarks.json`; docs/spec/trademarks.md), is done,
  so 9.8 and Phase 9's steps are.
  Phase 10's step plan (10.0-10.8) and the owner's answers are in ROADMAP; the repository goes public after Phase 10.
  10.1a, the benchmarks (docs/spec/performance.md: `tests/perf/bench.cpp` as `lumenis_bench`, `scripts/perf.py` with
  `tests/data/perf_baseline.json`, ctest `perf_benchmarks_run`, `ctest --preset perf`), is done. 10.1b, the slow paths,
  has started (`media::CorrectionPlan`, `detail::parallelRange` in `src/media/parallel.h`; BUILDING.md "Profiling";
  in-place `over`/`mix` taking `Picture&&` and `Picture::uninitialized`: use them in new blends and effects).
  10.2, long-run stability (docs/spec/soak.md: `--soak <hours>` in `src/ui/soak.*`, `scripts/soak-check.py`, ctest
  `soak_smoke`; the `tsan` presets on the tests labelled `core`), is done but the owner's 72-hour runs (BUILDING.md
  "Soak runs"). Use `std::mutex`, not `QMutex`, for new locks (ThreadSanitizer follows it); a dialog that blocks needs a
  hook the soak can answer (as `MainWindow::setRenderHook`).
  10.4, localisation, is in four parts (ROADMAP; docs/spec/localisation.md). 10.4a, the infrastructure (`translations/`
  with `languages.json` and `cutthulhu_<code>.ts`, `scripts/update-translations.py`, `src/ui/translation.*` with the
  Interface setting's Language, `--language`, the pseudo-locale; tests `tst_translation`, `translations_current`), is
  done. Show a JSON resource's text through `translatedResource(context, text)` (effects through `model::shownName`),
  never translate it where it is read (it is often the identifier), and run `scripts/update-translations.py` after
  changing any text (CONTRIBUTING.md). 10.4b, the check (test `tst_untranslated` reads every text shown in the
  pseudo-locale with the Tools windows open; built-in names in `src/ui/resources/names.json`, kept complete by the
  same test; model texts as `QT_TRANSLATE_NOOP`, shown through `translatedResource`), is done. 10.4c, the
  pseudo-locale's screenshots (`shots/pseudo` in each CI job, compared by ui-consistency), is done. 10.4d1, the
  German, French and Italian drafts (every text translated, left unfinished for a native speaker's review; terms in
  `translations/glossary_<code>.md`), is done; so is 10.4d2, Irish, so 10.4 is, but the native speakers' reviews.
  10.5a, crash reports (docs/spec/crash-reports.md: `app/crash_report.*` with the OS's own means, the owner's choice
  over Crashpad; a minidump and text report on Windows, a signal handler's report on Linux and macOS, in "Crash
  Reports" or `LUMENIS_CRASH_DIR`; the notice at the next start with Report on GitHub through `crashIssueUrl`, `--crash-reports`; ctest
  `crash_report`), is done. 10.5b, symbols (the Linux `.debug` and macOS dSYM kept beside each
  self-hosted package, the PDB in the Windows packages, `scripts/symbolize-crash.py` for Linux reports), is done.
  10.3a, accessible names (docs/spec/accessibility.md: `installAccessibleNames` in `src/ui/accessibility.*`, test
  `tst_accessibility`), is done: give a new control without text or tooltip a name with `setAccessibleName`. 10.3b,
  roles for the widgets we paint (the accessibility factory in `accessibility.cpp`: the Timeline's tracks and
  segments, position bars as sliders, monitor pictures), is done: add a new painted widget to `kPainted` there.
  10.3c, the keyboard (focus into the panel on Cycle windows/Next Tab through `PanelManager::focusInto`, Help > Find
  Command in `src/ui/find_command.*`, the keyboard focus ring in `accessibility.cpp`), is done.
  10.3d's automated half (contrast through `contrastRatio`/`focusRingColor`, rows growing with larger fonts through
  `rowHeightFor` in theme.h) is done; the screen-reader check by hand waits on the owner
  (docs/checklists/screen-readers.md). Size a new item view's rows with `rowHeightFor`.
  Phase 11 (left-over features, ROADMAP; the audit of 2026-11-14) has started. 11.1a, managed media in Apple ProRes
  (`media::resolutions()` with `Resolution::codec`/`quality`, `resolutionEncoder`/`resolutionProfile`: VideoToolbox on
  macOS, `prores_ks` with `LUMENIS_PATENT_CODECS`; `fixOpAtom` for ProRes; `"choicesFrom": "resolutions"` in
  settings.json; test `tst_media transcodeProRes`), is done. 11.1c1, XDCAM HD 50 and EX 35 (`Resolution::formats` with
  `media::setProjectFormat` from `MainWindow::tellMediaTheFormat`, so resolution menus offer what the project's format
  takes; `WriterSettings::interlaced`; encoder options through `AV_OPT_SEARCH_CHILDREN`; `fixOpAtom` for MPEG; test
  `tst_media transcodeXdcam`), is done. 11.1c2, DV25, DV50, DVCPro HD and XDCAM HD 35/17.5 in their own rasters (`ResolutionFormat`
  with the stored raster and scan, `media::resolutionRaster`/`toStoredRaster`, the pixels' shape through
  `WriterSettings::pixelAspect` and the Pixel Aspect Ratio column, frames fitted by their shown shape through
  `media::fitPicture` and `model::withPixelAspect`; test `tst_media transcodeDv`), is done. 11.1c3, MPEG IMX 30/40/50 (D-10's rasters
  with the blanking above the picture through `media::storedPlacement`, `WriterSettings::bottomFieldFirst`; test
  `tst_media transcodeImx`), is done, so 11.1c is. ProRes is offered only where its encoder makes MXF media here
  (`encodesHere` in managed.cpp; VideoToolbox's needs the profile in the stream's parameters, BUILDING.md). 11.2, the time-stretch engine
  (docs/spec/time-stretch.md: WSOLA in `src/media/time_stretch.*`, AudioSuite's Pitch Shift, Time Compression Expansion
  and Time Shift for new master clips through `AudioSuiteInfo::changesLength`; test `tst_time_stretch`), is done.
  11.3, the smaller left-over items, is done (ROADMAP): Show Adapters' S and C (`ui::shownAdapterIcons`),
  AudioSuite's Nested Level Selection (`Segment::nestedAudioSuites`, EQ at level 0), Media Creation's Apply to All and
  Single/Dual Drives Mode (settings option types `applyToAll`, `driveMode`), Motion Effects Render Using
  (`MainWindow::renderMotionClip`); 11.3d, media embedded in AAF files (docs/spec/aaf.md "Embedded media":
  `src/interop/aaf_essence.*`, `cfb::Writer::addFileStream` and `aaf::Builder::setStream` writing straight from files,
  `src/ui/main_window_embedded.cpp` with `readEmbeddedMedia` at the OMFI/AAF tab's Resolution and Media Destinations'
  Embedded in AAF; tests `tst_cfb fileStreams`, `tst_aaf embeddedMedia`, `tst_managed_media aafEmbeddedMedia`). 10.6 is an update check that only tells the user (no
  automatic updates, the owner's decision).
  Name new features generically; ctest `trademark_names` checks.
  The owner's report that Export to File made no movie on Windows 11 was the Windows FFmpeg without H.264/H.265
  encoders: `scripts/get-ffmpeg.ps1` now fetches BtbN's GPL build and checks libx264/libx265, CI requires them
  (`LUMENIS_REQUIRE_ENCODERS`, `tst_movie_export requiredEncoders`), a failed export says why in a message (BUILDING.md
  "Export says there is no H.264 encoder"). Error messages everywhere is in Phase 9 (owner's request). After Phase 7, before Phase 8: 7.6, the owner's report
  that the Titler+ window and the Effect Editor do not look like the FX Guide's pictures (Titler+'s two columns of
  tool icons on the left, ours text buttons side by side and the window as wide as a 1920-pixel screen; ROADMAP).
  Then Phase 8. Phase 9 "Left over stuff"
  (printing, Batch Import, AutoSync ...) comes after Phase 8, just before
  Phase 10, hardening (owner's decision, 2026-10-27).

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
