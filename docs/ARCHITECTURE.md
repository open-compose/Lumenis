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
  extract, trims, effects, transitions, Change Source Track; the effect value
  `model::Effect` is in `effect.h`),
  `automation.*` (volume and pan keyframes), `audio_eq.*` (Audio EQ's
  bands and filters), `clip_coloring.*` (which colour a segment is drawn in,
  docs/spec/timeline-colors.md), `sync_dupes.*` (sync breaks and duplicate
  frames, docs/spec/sync-dupes.md) and `monitor.*` (marks, positions).
  UI: `timeline.*` (the Timeline panel and its edit commands;
  `timeline_automation.cpp` its keyframe graph and editing), `clip_notes.*` (the
  Timeline Clip Notes window, docs/spec/clip-notes.md),
  `sequence_map.*` (the Sequence Map, docs/spec/sequence-map.md),
  `timecode_window.*` (Tools > Timecode Window, docs/spec/timecode-window.md),
  `find_window.*` (Edit > Find, docs/spec/find.md, over `src/model/text_find.*`, and PhraseFind AI over `src/model/phrase_find.*`), the Script window `script_window.*` (docs/spec/scriptsync.md, over `src/model/script.*`, with `main_window_script.cpp`) and the ScriptSync AI dialog `script_sync_dialog.*` (over `src/model/script_sync.*`),
  `clip_color_dialog.*` (the Timeline's Clip Color dialog),
  `surround_panner.*` (the Pan grid and Advanced Panner; the pans
  themselves are `src/model/surround_pan.*`) and
  `composer.*` (the Source and Record monitors, and the Effect Preview in
  Effect mode, docs/spec/effect-mode.md).
* **Panels and workspaces.** Our own docking framework (`panels.*`,
  `panel_manager.cpp`), not QDockWidget, because Qt cannot draw vertical
  title bars, green drop zones or Alt-drag tabbing. Every window it (or a
  tool such as the Titler+ Dashboard) opens is fitted to its screen
  (`fitOnScreen`, `keepOnScreen`). Workspaces: `workspace*.*`. Colours and
  metrics: `theme.cpp`.
* **Media.** `src/media/media.*` (FFmpeg probe, readers, writer, MXF
  fix-ups), `link.*` (files linked as clips), `managed.*` (managed media in
  `Avid MediaFiles/MXF`, the media database), `consolidate.*`, `relink.*`,
  `playback.*` (sound through miniaudio, the audio clock), `audio_input.*`
  (recording from an input), `audio_fx.*` (the audio effect interface and
  the built-in effects), `clap_host.*` (CLAP plug-ins as audio effects,
  docs/spec/clap-host.md), `ofx_host.*` (OFX image effect plug-ins in the
  Effect Palette, docs/spec/ofx-host.md; `ocio_colour.*`, their colour conversions through
  the optional OpenColorIO), `audio_suite.*` (AudioSuite processes and
  rendering, docs/spec/audiosuite.md), `compositor.*` (sequence frames
  from the video tracks with their effects, docs/spec/compositor.md), `wipes.cpp` (the 2D
  transitions, docs/spec/wipes.md), `image_effects.cpp` and `color_effects.cpp` (Picture-in-Picture,
  Resize, masks, Reformat, Color Effect, Blur, Mosaic, docs/spec/image-effects.md), `key_effects.cpp`
  (the keys, docs/spec/keys.md), `burn_in.cpp` (Timecode Burn-In, docs/spec/burn-in.md), `illusion_fx.cpp` and `illusion_transitions.cpp` (the Illusion FX segment effects
  and transitions, sharing `illusion_common.h`; docs/spec/illusion-fx.md), `effect_drawing.*` (their shared sampling and borders), `render.*` (Render Effect's precomputes,
  docs/spec/rendering.md), `optical_flow.*` (FluidMotion's in-between frames, docs/spec/motion-effects.md), `fields.*` (interlaced fields, pulldown and the format conversions of Timewarps and adapters), `fluid_fx.*` (FluidBlur, FluidColorMap and FluidMorph on the optical flow), `movie_export.*` (MOV and MP4 export, docs/spec/movie-export.md), `mixdown.*`
  (Audio Mixdown, docs/spec/audio-mixdown.md), `surround.*` (speakers,
  monitor mixes and their folds, docs/spec/surround.md), `ltc.*` (audio
  timecode, docs/spec/clip-audio.md), `tone.*` (the calibration tone,
  docs/spec/tone.md), `waveform.*` (Timeline waveform peaks and their cache,
  docs/spec/waveforms.md). UI side:
  `media_frames.*` (frames for monitors, bins and the Timeline),
  `source_browser.*`, `media_tool.*`, `relink_dialog.*`,
  `consolidate_dialog.*`, `background_queue.*` (with the background services' state, 8.4a), `background_services.*` (the
  Timeline's progress indicator and Tools > Background Services, with `main_window_services.cpp`), Dynamic Media
  Folders' watcher and Acquire `main_window_dmf.cpp` over `src/media/dynamic_media_folders.*`, the window and the
  Profile Editor `dynamic_media_folders_window.*`.
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
  `interchange_dialogs.*` the Import EDL and Export Bin As dialogs. File > Output > Export to File runs through
  `MainWindow::writeExport` (movies, stills and sound in `src/media/movie_export.*`, docs/spec/movie-export.md; AAF's
  Export Methods in `MainWindow::prepareAafMedia`); Send To templates are `src/model/send_to.*` with
  `src/ui/send_to_dialog.*` (docs/spec/send-to.md); the Simplified AAF's spans are `src/model/simplify_export.*`; Export to Device goes through `ext::ExportDeviceProvider`
  (`src/ext/include/lumenis/ext/export_device.h`, `src/media/xdcam_disk.*`, `src/ui/export_device_dialog.*`); AS-11 and AS-02 are bmxtranswrap rewraps (`src/media/as11.*`), Export Volumes
  `src/model/export_volume.*` and `src/ui/export_volume_dialog.*`; the Digital Cut tool `src/ui/digital_cut_tool.*` records
  onto `ext::DeckProvider` decks (`src/ext/include/lumenis/ext/deck.h`, the folder tape `src/media/tape_deck.*`); the Video Output tool
  `src/ui/video_output_tool.*` drives `ext::VideoOutputProvider` hardware, with `src/media/test_patterns.*`. The SubCap
  effect's captions and caption files (Avid DS, EBU N19) are `src/model/subcap.*`, its picture `src/media/subcap.cpp`
  and its dialogs `src/ui/subcap_dialogs.*` (docs/spec/subcap.md). Closed captions on D1 (SCC, CEA-608, TTML) are
  `src/model/closed_captions.*`, drawn in the Timeline by `src/ui/timeline_captions.cpp` and in the monitors by the
  Composer, imported through `src/ui/ancdata_import_dialog.*` (docs/spec/closed-captions.md). The Timeline Quick
  Find is `src/ui/timeline_find.cpp` (docs/spec/timeline-find.md). DCPs are `src/media/dcp.*` (docs/spec/dcp.md).
* **Effects.** The Effect Palette's list is `src/ui/resources/effects.json`
  and the Effect Editor's parameters `effect_parameters.json`; UI in
  `effect_palette.*` and `effect_editor.*` (its keyframe tracks and graphs in
  `effect_editor_keyframes.cpp`; OFX plug-ins' Angle control `angle_dial.*` and the OFX Optional Inputs dialog
  `ofx_inputs_dialog.*`); the model keeps effects on
  segments (`model::Effect`, `model::Transition`), their parameters'
  keyframes in `src/model/effect_keyframes.*` (docs/spec/effect-keyframes.md), and their renders in
  `src/model/effect_render.*` with Render Effect and Clear Renders in `MainWindow::renderEffects` and
  `render_dialogs.*` (docs/spec/rendering.md); traditional motion effect clips in `src/model/motion_effect.*`
  with `motion_effect_dialogs.*`, Timewarps in `src/model/timewarp.*` with the Motion Effect Editor
  `motion_effect_editor.*`, nests (a segment effect's own tracks, Step In and Collapse) in `src/model/nest.*` with docs/spec/nesting.md,
  effect templates (Effect items in bins holding an effect, applied to segments, transitions and parameter
  groups) in `src/model/effect_template.*` with docs/spec/effect-templates.md, the 3D effects' renderer in
  `src/media/warp3d.cpp` with 3D Promote in `src/model/promote3d.*` (docs/spec/3d-effects.md), direct
  manipulation's outlines, handles and drags in `src/media/effect_handles.*` with the Effect Editor's side in
  `effect_editor_handles.cpp` and the Effect Preview's overlay in `composer.cpp`, Pan and Scan in
  `src/media/pan_scan.cpp` with its Actions in `src/model/pan_scan.*` (docs/spec/pan-scan.md), Avid Pan & Zoom
  in `src/media/pan_zoom.cpp` (docs/spec/pan-zoom.md), motion tracking's data in `src/model/tracking.*` with its
  engines and its effect on the picture in `src/media/tracking.*` (Region Stabilize in `region_stabilize.cpp`) with the Tracking window
  `tracking_window.*` and the Effect Editor's side in `effect_editor_tracking.cpp` (docs/spec/tracking.md),
  Titler+ titles in `src/model/titler.*` with their picture in `src/media/titler.cpp`,
  `Timeline::applyTitler`, the Record monitor's tools in `titler_tool.*` (through
  `Composer::OverlayInput`) and the Dashboard and Effect Editor controls in `titler_panel.*`
  (docs/spec/titler.md), the classic Title Tool's titles in `src/model/classic_title.*` drawn by
  `src/media/classic_title.cpp` through Titler+'s text layout and edited in `title_tool.*`
  (docs/spec/title-tool.md) and saved as Title Effect clips (`src/model/title_clip.*`, fill and matte precomputes
  keyed by the compositor's `classicTitleLayer`), the Intraframe effects' objects in `src/model/intraframe.*` drawn by
  `src/media/intraframe.cpp` (Paint modes, Blur and Mosaic within objects, AniMatte mattes; docs/spec/intraframe.md) and
  drawn and selected in the Effect Preview by `intraframe_tool.*` (reshaped and rotated in `intraframe_reshape.cpp`; brushes in `brush_preview.*` and `effect_editor_brush.cpp`; PICT files in `src/media/pict.*`), PlasmaWipe effects from gradient images in `src/media/plasma_wipe.*` (docs/spec/plasma-wipe.md), colour corrections in `src/model/color_correction.*` (their relationships in `src/model/correction_relations.*`, shapes on the Color Correction effect in `src/model/correction_shapes.*`, Update/Merge/Flatten/Remove Correction in `src/model/correction_manage.*` with their dialogs in `src/ui/correction_manage_dialog.*`, templates in `src/model/correction_template.*` applied by `src/ui/timeline_correction.cpp`, keyframes in `src/model/correction_keyframes.*`, shown in the Effect Editor through `src/model/correction_effect.*`) drawn by `src/media/color_correction.*` (docs/spec/color-correction.md; Color Correction mode's three monitors in `src/ui/correction_monitors.*` with their waveform and vectorscope displays from `src/media/scopes.*`, the automatic corrections in `src/media/auto_correct.*`, the Color Match control's in `src/media/color_match.*` (its widget in `src/ui/correction_color_match.cpp`; custom colours in bins from `src/media/custom_color.*`), the Color Correction tool in `src/ui/correction_tool.*` (its shape panel in `src/ui/correction_tool_shapes.cpp`, drawing through `src/ui/correction_shape_host.*`), the Color Info tool in `src/ui/color_info.*`, safe colour limits in `src/model/safe_colors.*` (the Safe Color Limiter effect in `src/model/safe_color_limiter.*`) with the Safe Colors setting's graph in `src/ui/safe_color_graph.*`), clips' Reformat attribute, FrameFlex boxes and Spatial Adapters in `src/model/frame_flex.*` drawn by `src/media/frame_flex.*` (docs/spec/frameflex.md; the Source Settings dialog in `src/ui/source_settings_dialog.*`; the FrameFlex effect that pans a shot, as `EffectTarget::Spatial`, with its Effect Preview drags in `src/ui/effect_editor_handles.cpp`), clips' Color Encoding and segments' Color Adapters in `src/model/color_adapter.*` with the colour transformations in `src/media/color_transform.*` and the external LUT files in `src/media/lut_file.*` (docs/spec/color-adapters.md; the monitors' Display Color Space in `src/media/display_color.*`), and mixed rate clips' Motion Adapters in `src/model/motion_adapter.*`: the
  monitors load a clip at another rate as its adapted copy at the project's rate, the segments edited
  from it carry `Segment::adapter`, and `BinDocument` stores the clip itself back (docs/spec/motion-effects.md).
* **Audio tools.** `audio_tools.*` (the Audio Mixer, docs/spec/audio-mixer.md, and the Audio Tool), `audio_groups.cpp` (the mixer's track sidebar and groups), `clip_audio_dialogs.*` (Apply Gain and Auto VO, with `src/model/clip_audio.*`, docs/spec/clip-audio.md), `multi_mix_dialog.*` (the Multi-Mix Tool, docs/spec/multi-mix.md),
  `audio_eq.*` (the Audio EQ tool, docs/spec/audio-eq.md),
  `audio_punch_in.*` (Audio Punch-In, docs/spec/punch-in.md),
  `audio_track_effect.*` (the Audio Track Effect tool,
  docs/spec/track-effects.md), `audio_suite.*` (the AudioSuite tool,
  docs/spec/audiosuite.md), `modify_dialog.*` (Clip > Modify > Modify Clip's
  Set Multichannel Audio, docs/spec/multichannel.md) and
  `mixdown_dialog.*`; the sound is `src/media/playback.*` and recording
  `src/media/audio_input.*`.
* **Multicamera and transcripts.** Group clips and multigroups are `src/model/group_clip.*`
  with `group_clips_dialog.*` and MainWindow's MultiCamera handlers (docs/spec/multicamera.md);
  syncing by sound is `src/media/waveform_sync.*`. Transcripts are `src/model/transcript.*` (the
  store and which bins are transcribed), made by `src/media/transcription.*` through an
  `ext::TranscriptionProvider` (whisper.cpp's `whisper-cli` built in) and queued by
  `main_window_transcripts.cpp`; Manage Speaker Names is `speaker_names_dialog.*`, the Transcribe window and Manage Transcripts `transcript_dialogs.*`; the Transcript Tool is `transcript_tool.*` showing `src/model/transcript_view.*`
  (docs/spec/transcripts.md).

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
