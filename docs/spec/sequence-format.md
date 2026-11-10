# Sequence and project formats (8.7c)

Editing Guide ch.36, "Working in HD and High-Resolution Projects": Changing the Project Format (p.1287-1293), Changing
the Sequence Format (p.1294), the 24p and 23.976p NTSC conversions (p.1294-1295), Editing at 60 fps (p.1295-1296),
Displaying 24p and 25p Media (p.1296) and HD Universal Mastering (p.1297-1298). In parts (ROADMAP): 8.7c1a Set Format on
sequences, 8.7c1b the project's format on the Format tab, 8.7c2 HD Universal Mastering, 8.7c3 editing at 60 fps and the
24p/25p display.

## Set Format on sequences (p.1294; 8.7c1a)

* "To modify the format of a sequence: ... Select Clip > Modify. Select Set Format from the top list. Select the format to
  which you want to convert from the Format menu. Click OK" (p.1294): the Modify dialog's Set Format option
  (`ModifyDialog::setsFormat`, `formats`, `chosenFormat`), no longer dimmed. Ours: the Format menu (`setFormatMenu`) lists
  the project format presets (`projectFormatPresets()`), the project's format first; the dialog starts on Set Format when
  only sequences are selected; other items are named in the Console and left alone.
* "You may not always need to change the edit rate of the sequence. For example, you can change an NTSC 30i sequence to
  1080i/59.94 or to 720p/59.94" (p.1294): at the same edit rate the sequence is modified in place and its Format column
  shows the new format's label ("You can check the format of the sequence in the Format column of the bin").
* "In cases where timecode needs to be converted, Media Composer creates a new sequence and unlinks the media. No media is
  converted" (p.1294), "Media Composer creates a new sequence labeled with the extension 23.976 fps", "Media for the new
  23.976p sequences and clips is offline. Sequences and clips cannot be linked to the original 24p media" (p.1294): at
  another edit rate `model::setSequenceFormat` (`src/model/sequence_format.*`) makes a new sequence (a new id and MobID)
  named "<name> <rate> fps" in the same bin. Ours: every segment's length and source frame and every marker keep their
  time at the new rate (`model::convertFrames`, rounded to the nearest frame); a clip segment's clip becomes one no bin has
  ("offline-<id>"), so it stays a clip, offline, with its name and source track for batch capture or relinking.
* "A message box tells you whether the sequence has been duplicated or modified" (p.1294): `MainWindow::modifySelection`
  shows it and writes the same lines to the Console.
* The motion adapters the guide mentions for a sequence whose edit rate changes ("the modified sequence uses motion
  adapter effects", p.1294) are not added: ours always unlinks then. "Understanding Options for Modifying the Sequence
  Format" is in Avid's Help, not the guide.

Tests: `tst_sequence_format` (same rate, another rate, markers, refused items), `tst_multichannel_ui setFormatOnSequences`
(the dialog through Clip > Modify, the new and the modified sequence).

## Changing the Project Format (p.1287-1293; 8.7c1b)

* "Select File > Settings, and click the Format tab to change the format of the project to another format that shares the
  same frame rate" (p.1287); "You can select only projects with the same edit rate" (p.1288): the Format tab's Project
  Type is a menu (`formatProjectType`, `SettingsWindow::projectFormatChanged`) of the presets with the project's edit rate,
  the project's own first; `MainWindow::setProjectFormat` writes the change to the project file and refuses another
  edit rate.
* "When you change the project format ... The available resolutions and, for some configurations, raster dimensions change
  to those of the new project. Any new sequences you create use the format of the new project" (p.1287): the raster and
  everything that follows it (proxy modes, mask margins' frame, renders' keys) come from the new format; new sequences
  get it in their Format column (`BinContainer::setSequenceFormat`); existing sequences keep theirs ("If necessary, you can
  then modify the format of an existing sequence", Set Format above). "The hardware changes to support input and output
  for the new project" has no hardware here.
* Aspect Ratio (p.1289): the table gives SD a choice of 4:3 or 16:9, HD always 16:9 and High-Res one calculated from the
  project's resolution: an SD project changed to another SD format keeps its aspect ratio; otherwise it
  is the new format's. Ours: a Proxy mode the new raster has no room for goes back to Off.
* The table's other rows (Color Space, Color Depth, Mask Margins, Stereoscopic, Film, p.1290-1293) are the Format tab's
  existing fields; Raster Dimension's custom and thin rasters are set when the project is made (New Project, p.65).

Tests: `tst_settings_window changingProjectFormat` (the same edit rate, the file, new and old sequences' Format, another
edit rate refused), `tst_settings_window windowTabsAndUserProfiles` (the Project Type menu's formats).

## HD Universal Mastering: the audio (p.1297-1298; 8.7c2a)

* "HD Universal Mastering preserves synchronization with the converted video by remastering audio at a different sample
  rate to generate replacement audio tracks from previously mixed-down audio clips" (p.1297): the table's six conversions
  and rates are `model::masteringConversions()` (`src/model/universal_mastering.*`); each rate is 48000 x the original
  frame rate / the mastered one, to the sample (`model::masteredSampleRate`: 47952, 46034, 48048, 46080, 50000, 50050).
* "Select the mixed-down audio clip in the bin, and ... Select Clip > Audio > Change Sample Rate ... Click the Sample Rate
  menu, and select the appropriate conversion option" (p.1298): Change Sample Rate's menu (`ChangeSampleRateDialog`) lists
  them after 32, 44.1 and 48 kHz, with the guide's texts, and writes the clip's sound again at the remastered rate (a WAV
  beside the MXF media, as other rates off 48 kHz are; its Audio SR column says the rate). Quality (High, Medium, Low),
  Delete Original Media and Target Drive are the dialog's own (5.7b2). Ours: the conversions are offered in every
  project, not only 1080p ones. The remastered sound has as many samples per frame as the output needs (1920 a frame for
  24 to 25 fps), so a digital cut at the mastered rate plays it at 48 kHz in sync. That output is the Digital Cut tool's
  Output Format menu (8.7c2b, docs/spec/digital-cut.md "Output formats"), whose table (p.1299) is `model::outputFormats`.
* Steps 10-14 (two new audio tracks, Overwrite at the sequence's first frame) are ordinary editing.

Tests: `tst_sequence_format universalMastering` (the table), `tst_clip_audio moreDialogs` (the menu) and
`timecodeAndRateInTheApplication` (a clip remastered at 46080 Hz).

## Editing at 60 fps (p.1295-1296; 8.7c3)

* "Single-field step commands are deactivated; if you click a button, Media Composer beeps" (p.1295), as for every
  progressive format: "The single-field step feature is not available when you work with progressive formats" (p.399).
  Step Forward/Backward One Field (`Composer::perform`, `setInterlacedProject`, which `setAdapterOptions` also sets) beep
  in progressive projects. In interlaced ones they park on field 2 ("A number 2 displays in the upper right corner of
  the monitor to indicate you are parked on field 2 of a frame", p.399; `Composer::parkedField`), where the monitor shows
  that field's lines (`media::fieldPicture`); "Subsequent single-frame steps are based on this field" (p.399); playing
  goes back to field 1.
* "Draft Quality plays back at 30 fps. Full Quality plays back at 60 fps" (p.1295): while playing at Draft Quality a
  clip of 50 frames a second or more shows every second frame (`Composer::shownFrame`).
* Marks and trims at 1/60th of a second are the sequence's own frames. "Transition effects default to one-second
  duration (60 frames)" (p.1296): `Timeline::defaultTransitionLength` is one second of the timecode's frames.
* "Deck control for capture and digital cut is limited to 30 fps. A message box warns you if you try to mark an odd
  timecode value (such as 01:00:00:03)" (p.1296): the Digital Cut tool's Mark IN in 50 and 60 fps timecode
  (`MainWindow::digitalCut`); the cut is not recorded. There is no Capture tool.
* "1080i/50 and 1080i/59.94 are interlaced resolutions that you edit at 25 frames per second and 30 frames per second"
  (p.1296): their edit rates are 25 and 29.97.

## Displaying 24p and 25p Media (p.1296; 8.7c3)

* "This section applies to you only if you are working with Standard Definition (SD) formats" (p.1296). The Source,
  Record and pop-up monitors play the media at its own rate. The NTSC and PAL monitor's pulldown (2:3 at 23.976 or 24,
  two extra fields a second at 24 PAL, 25 frame for frame) is the Digital Cut's (docs/spec/digital-cut.md "Output
  formats"), which "Displaying Media During a Digital Cut" follows. A client monitor fed in real time through video
  output hardware takes the same cadence when those modules come (7.3c).

Tests: `tst_composer_display fieldStepsAndSixtyFps`, `tst_digital_cut sixtyFpsMarkIn`.
