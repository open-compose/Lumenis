# Architecture: where things are

A map of the source for contributors. What the program should do is in
[ROADMAP.md](ROADMAP.md) and the specifications in [spec/](spec/); building
is in [BUILDING.md](../BUILDING.md). Each source file starts with a comment
saying what it implements and what it works with.

## Layers

The code is a stack of static libraries. Each depends only on those below it.

| Library (CMake target) | Folder | What it holds | Depends on |
|---|---|---|---|
| `lumenis_core` | `src/core` | Value types with no Qt widgets: `Rational` (exact edit rates), `Timecode`, `MobId` (SMPTE UMIDs), the project format, branding constants. | — |
| `lumenis_ext` | `src/ext` | Extension points: provider interfaces (codecs, effects) and the module loader for plug-ins (docs/EXTENSIONS.md). | core |
| `lumenis_model` | `src/model` | The editing model, independent of the UI: projects and their bin tree, bins and bin items, sequences (tracks, segments, edits), markers, monitor state, settings files, the calculator, structure dumps. | core |
| `lumenis_interop` | `src/interop` | Interchange: the compound file container, AAF reading, import into bins, writing and export (docs/spec/aaf.md); Media Composer's `.avb` bins, read, imported, written and exported (docs/spec/avb.md); EDLs (docs/spec/edl.md), ALE shot logs (docs/spec/ale.md), OpenTimelineIO (docs/spec/otio.md) and FCP 7 XML (docs/spec/fcp-xml.md), read and written. | model |
| `lumenis_media` | `src/media` | Media through FFmpeg: probing, frame-exact reading, writing, linking files as clips, managed MXF media and the media database, consolidate/transcode, relink, sound output and the playback clock. | model, ext |
| `lumenis_ui` | `src/ui` | Every window, panel and dialog (Qt Widgets), and the main window that ties them to the model and media. | everything above |
| `lumenis_app` (`cutthulhu`) | `app` | `main()`: command-line options (many are test and gate tools), crash reports, then the main window. | ui, interop, media |

Names: the product is Cutthulhu (`cmake/Branding.cmake`); the code name
`lumenis` is in targets, namespaces (`lumenis::model`, ...) and include
paths (`#include "lumenis/model/bin.h"`). Each library keeps its public
headers in `include/lumenis/<library>/`.

## Where to start for a change

* **A menu command.** Every command is declared in
  `src/ui/resources/commands.json` (id, text, shortcut) and placed in
  `menus.json`; `tests/data/menu_inventory.json` is the menu bar's spec,
  from the Editing Guide. Handlers are attached in
  `MainWindow` (`src/ui/main_window.cpp`, the `handle("id", ...)` blocks)
  or by the panel that owns the command (Timeline, Composer, bins).
  A command starts disabled until a handler is attached; after changing
  menus or commands run `scripts/make-checklist.py`.
* **A setting.** Options are declared in `src/ui/resources/settings.json`
  (kind, scope, tabs, options with type, default and guide page; `"later":
  true` shows one dimmed). The Settings window and its dialogs are built
  from it (`settings_window.*`); code reads values through
  `ui::Settings::value/flag/number/text`.
* **Bins.** Model: `src/model/bin.*` (items, columns, sort, sift, the
  `.cthbin` JSON format), `project_bins.*` (the bin tree), `bin_lock.*`
  (shared-storage lock and history files, docs/spec/bin-locking.md),
  `bin_attic.*` (the attic's bin copies, docs/spec/bin-attic.md),
  `bin_relations.*` (Bin > Select's Media Relatives, Sources and Unreferenced
  Clips, docs/spec/bin-select.md). UI (also `unmount_dialog.*`, File > Media >
  Unmount, docs/spec/media-unmount.md; `set_font_dialog.*` and
  `shortcuts_window.*`, Set Font and Help > Shortcuts, docs/spec/set-font.md):
  `bin_view.*`
  (Text, Frame and Script views, `BinDocument`), `bin_container.*` (the
  Bin Container and its sidebar), `bin_dock.*` (docking bins),
  `bin_dialogs.*` (Bin Column Selection with its Search field, Custom
  Sift, Set Bin Display).
* **Editing.** Model: `src/model/sequence.*` (splice, overwrite, lift,
  extract, trims, effects, transitions, Change Source Track),
  `automation.*` (volume and pan keyframes), `audio_eq.*` (Audio EQ's
  bands and filters), `clip_coloring.*` (which colour a segment is drawn in,
  docs/spec/timeline-colors.md), `sync_dupes.*` (sync breaks and duplicate
  frames, docs/spec/sync-dupes.md) and `monitor.*` (marks, positions).
  UI: `timeline.*` (the Timeline panel and its edit commands;
  `timeline_automation.cpp` its keyframe graph and editing), `clip_notes.*` (the
  Timeline Clip Notes window, docs/spec/clip-notes.md),
  `sequence_map.*` (the Sequence Map, docs/spec/sequence-map.md),
  `timecode_window.*` (Tools > Timecode Window, docs/spec/timecode-window.md),
  `find_window.*` (Edit > Find, docs/spec/find.md, over `src/model/text_find.*`),
  `clip_color_dialog.*` (the Timeline's Clip Color dialog),
  `surround_panner.*` (the Pan grid and Advanced Panner; the pans
  themselves are `src/model/surround_pan.*`) and
  `composer.*` (the Source and Record monitors, and the Effect Preview in
  Effect mode, docs/spec/effect-mode.md).
* **Panels and workspaces.** Our own docking framework (`panels.*`,
  `panel_manager.cpp`), not QDockWidget, because Qt cannot draw vertical
  title bars, green drop zones or Alt-drag tabbing. Workspaces:
  `workspace*.*`. Colours and metrics: `theme.cpp`.
* **Media.** `src/media/media.*` (FFmpeg probe, readers, writer, MXF
  fix-ups), `link.*` (files linked as clips), `managed.*` (managed media in
  `Avid MediaFiles/MXF`, the media database), `consolidate.*`, `relink.*`,
  `playback.*` (sound through miniaudio, the audio clock), `audio_input.*`
  (recording from an input), `audio_fx.*` (the audio effect interface and
  the built-in effects), `clap_host.*` (CLAP plug-ins as audio effects,
  docs/spec/clap-host.md), `audio_suite.*` (AudioSuite processes and
  rendering, docs/spec/audiosuite.md), `compositor.*` (sequence frames
  from the video tracks with their effects, docs/spec/compositor.md), `wipes.cpp` (the 2D
  transitions, docs/spec/wipes.md), `image_effects.cpp` (Picture-in-Picture, Resize, masks,
  Reformat, docs/spec/image-effects.md), `effect_drawing.*` (their shared sampling and borders), `mixdown.*`
  (Audio Mixdown, docs/spec/audio-mixdown.md), `surround.*` (speakers,
  monitor mixes and their folds, docs/spec/surround.md), `ltc.*` (audio
  timecode, docs/spec/clip-audio.md), `tone.*` (the calibration tone,
  docs/spec/tone.md), `waveform.*` (Timeline waveform peaks and their cache,
  docs/spec/waveforms.md). UI side:
  `media_frames.*` (frames for monitors, bins and the Timeline),
  `source_browser.*`, `media_tool.*`, `relink_dialog.*`,
  `consolidate_dialog.*`, `background_queue.*`.
* **Interchange.** `src/interop`: `cfb.*` (the container), `aaf.*` (objects
  and the metadictionary; `aaf_model.inc` is generated by
  `scripts/make-aaf-model.py`), `aaf_import.*`, `aaf_writer.*`,
  `aaf_export.*`; `avb.*` reads Media Composer's `.avb` bins, `avb_writer.*`
  writes their objects back (the inverse of `avb.*`), and
  `avb_import.*` reads them into bins (helpers shared with `aaf_import.cpp`
  and the exporters in `import_common.h`), and `avb_export.*` writes bins
  as `.avb` (the import's inverse, built with `avb_writer.*`). `edl.*` reads and writes edit
  decision lists (CMX 3600, File_129) and `ale.*` Avid Log Exchange shot logs; the text helpers they
  share are in `text_common.h`. `otio.*` reads and writes OpenTimelineIO's `.otio` JSON, and `fcp_xml.*` FCP 7 XML through it. The native model they read into and write from is
  described in docs/spec/native-model.md. In the UI, `list_tool.*` is Tools > List Tool and
  `interchange_dialogs.*` the Import EDL and Export Bin As dialogs.
* **Effects.** The Effect Palette's list is `src/ui/resources/effects.json`
  and the Effect Editor's parameters `effect_parameters.json`; UI in
  `effect_palette.*` and `effect_editor.*` (its keyframe tracks and graphs in
  `effect_editor_keyframes.cpp`); the model keeps effects on
  segments (`model::Effect`, `model::Transition`), their parameters'
  keyframes in `src/model/effect_keyframes.*` (docs/spec/effect-keyframes.md).
* **Audio tools.** `audio_tools.*` (the Audio Mixer, docs/spec/audio-mixer.md, and the Audio Tool), `audio_groups.cpp` (the mixer's track sidebar and groups), `clip_audio_dialogs.*` (Apply Gain and Auto VO, with `src/model/clip_audio.*`, docs/spec/clip-audio.md), `multi_mix_dialog.*` (the Multi-Mix Tool, docs/spec/multi-mix.md),
  `audio_eq.*` (the Audio EQ tool, docs/spec/audio-eq.md),
  `audio_punch_in.*` (Audio Punch-In, docs/spec/punch-in.md),
  `audio_track_effect.*` (the Audio Track Effect tool,
  docs/spec/track-effects.md), `audio_suite.*` (the AudioSuite tool,
  docs/spec/audiosuite.md), `modify_dialog.*` (Clip > Modify > Modify Clip's
  Set Multichannel Audio, docs/spec/multichannel.md) and
  `mixdown_dialog.*`; the sound is `src/media/playback.*` and recording
  `src/media/audio_input.*`.

## Data flow

1. `app/main.cpp` creates the `CommandRegistry` (from `commands.json`) and
   the `MainWindow`, which builds the menus (`menu_builder.*`), the panels
   and the default workspace, and shows the Select Project window.
2. Opening a project loads its settings (`ui::Settings`, files of
   `model::SettingsFile`) and its bin tree (`model::ProjectBins`). Bins are
   read when opened (`model::BinFile`) and held by a `ui::BinDocument`,
   which marks them dirty and saves them.
3. Loading a clip into a monitor gives the Composer a copy of the bin item;
   edits in the Timeline change a `model::Sequence` and write it back into
   the sequence's bin item (`Sequence::storeIn`), which the bin saves.
4. Pictures come from `ui::MediaFrames` (a cache over `media` readers);
   sound from `media::AudioPlayer`, whose clock paces playback.
5. Interchange converts between files and bin items: AAF import and `.avb`
   reading make `model::BinItem`s for a bin; export writes bin items to AAF
   or a bin to `.avb` (File > Output > Export to File, `--export-aaf`,
   `--export-avb`). An EDL imports as a sequence and a source for each
   tape (File > Input > Import EDL, `--import-edl`); the List Tool writes a
   sequence as an EDL (`--export-edl`). Shot logs import as master clips
   (the Source Browser's Import, `--import-ale`) and are written by Export
   Bin and Export to File (`--export-ale`). OpenTimelineIO files import as
   offline master clips and sequences (Import Media, a drop into a bin,
   `--import-otio`) and are written from a sequence (the List Tool's OTIO
   output, `--export-otio`); FCP 7 XML goes through the same import and
   export (Import Media, Export to File, `--import-fcpxml`, `--export-fcpxml`).

## Tests

`tests/<library>/tst_*.cpp` are Qt Test programs, one per area, registered
with CTest (`ctest --preset ci`). Python checks live in `scripts/` and
`tests/scripts/` and run as ctests where their tools are installed:

* the menu bar against `tests/data/menu_inventory.json`, keyboard mapping
  against `keyboard_inventory.json`, and the Gate 5 checklist;
* media: the decode corpus, MXF checked by bmx (`mxf_check`), playback
  measurements by hand;
* interchange: the AAF corpus against olefile and pyaaf2, the `.avb`
  corpus against pyavb (`avb_pyavb`), imports against
  Media Composer's bins (`aaf_import_bins`), the bins themselves read into
  bins (`avb_import_bins`), round trips (`aaf_round_trip`, `avb_round_trip`,
  `avb_export_round_trip`), and what the `.avb` writer and export make read
  by pyavb (`avb_write_pyavb`, `avb_export_pyavb`); EDLs and shot logs
  (`tst_edl`, `tst_ale`, with OpenTimelineIO's sample files from the
  corpus, and `tst_interchange` for the menus and the List Tool); OTIO
  (`tst_otio`, `otio_round_trip`, and `otio_python` against
  OpenTimelineIO's own reader); FCP 7 XML (`tst_fcpxml`, `fcpxml_round_trip`,
  `fcpxml_python` against OpenTimelineIO's FCP adapter);
* audio: the mixdown null test (`tst_mixdown`, Phase 5 gate 1) and the
  Audio EQ's sound against a reference (`tst_audio_eq`);
* editing: scripted edit scenarios in `tests/scenarios/*.json` run by
  `tst_scenarios`, with undo and redo checked by hash, and 300 random ones
  from `scripts/make-scenarios.py`'s reference model
  (`edit_scenarios_generated`; docs/spec/edit-scenarios.md).

`fuzz/` holds libFuzzer targets for the interchange parsers (the compound
file, AAF, `.avb` bins and their writer, OTIO, FCP XML; ROADMAP Phase 3 gate 4), built by
the `fuzz` preset and run with `scripts/fuzz.py` (fuzz/README.md, BUILDING.md
"Fuzzing"). Every build also makes `lumenis_fuzz_replay`, and ctest
`fuzz_replay` runs the saved crash inputs (`fuzz/regressions/`) through
them.

Screenshots of the windows are compared across platforms in CI
(`ui-consistency`); custom-painted widgets draw text with palette colours
(`WindowText`, `BrightText`) so the text-free screenshots hide it.

## Rules for patches

* **Clean room** (ROADMAP §3): no Avid code, SDKs or decompiled material.
  Behaviour comes from the public Editing Guide and FX Guide, cited by
  page in comments and in docs/spec/ui-inventory.md; file formats from
  public specifications, MIT-licensed prior art (pyaaf2, pyavb) and
  black-box observation.
* **Warnings are errors** in the `ci` preset; also build with Clang and run
  `scripts/check-apple-conversions.py` (BUILDING.md).
* **Comments**: each file starts with what it does and how it fits; public
  functions say their contract; non-obvious code says why. Keep page
  citations ("(p.459)") where behaviour comes from the guide.
* New dependencies need a GPL-3.0-compatible licence, an entry in
  THIRD-PARTY-NOTICES.md and their licence text installed.
