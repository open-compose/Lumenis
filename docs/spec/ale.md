# ALE shot logs (Phase 3.6a)

A shot log "is a text file that lists information about a roll of film or a
videotape" (Editing Guide p.131). Media Composer imports logs that meet the
"Avid Log Specifications" (p.106-112), Avid Log Exchange (ALE), into bins
(p.131-132) and exports a bin's clips as one (p.129-130). Cutthulhu reads
them into master clips (native-model.md) and writes them from a bin.

Sources:
* The Media Composer Editing Guide (2025), which publishes the format:
  "Avid Log Specifications" (p.106-112) with the sample log (p.112),
  Double-Checking Log Files (p.114), Exporting Shot Log Files (p.129-130),
  Importing Shot Log Files (p.131-132), Export As Avid Log Exchange
  (p.1174), the Import setting's Shot Log tab (p.1197-1198).
* OpenTimelineIO's ALE adapter (Apache-2.0,
  github.com/OpenTimelineIO/otio-ale-adapter): read for logs as other
  programs write them (End is Start plus Duration; several global titles
  on one line; empty cells). Its sample logs are part of the interchange
  corpus (`scripts/interop-corpus.py`, not committed). Credited in
  THIRD-PARTY-NOTICES.md.

## The format (p.106-112)

Three sections, in this order, each starting with its word on a line of its
own:

```
Heading
FIELD_DELIM	TABS
VIDEO_FORMAT	NTSC
AUDIO_FORMAT	44kHz
TAPE	001
FPS	29.97

Column
Name	Tracks	Start	End

Data
CU Josh & Mary	V	01:00:00:00	01:15:05:00
CU Josh	VA1	01:15:06:00	01:20:00:00
```

* **Global titles:** `FIELD_DELIM` (TABS), `VIDEO_FORMAT` (NTSC, PAL;
  others write 1080, 720, CUSTOM), `FILM_FORMAT`, `AUDIO_FORMAT`, `TAPE`
  (the clips' tape; "If you omit this Title, the file name becomes the
  global tape name", p.107), `FPS` (23.98, 24, 25, 29.97), `SOUNDTC_FPS`.
* **Column titles:** Name, Tracks, Start, End are required (p.108), then
  the standard titles of p.108-109 (Audio, Auxiliary TC1-5, Camera, Camroll,
  Duration, FPS, KN Start, Reel #, Scene, Sound TC, Take, Tape, DESCRIPT,
  COMMENTS, ...) and custom ones. At most 64 titles in all (p.106).
* **Data:** a line for each clip, its values in the order of the titles;
  an empty value leaves the column empty. Tracks: V, VA1, VA2, VA1A2, A1A2,
  A1, A2, D (p.110), "up to four audio tracks". Timecodes with colons for
  non-drop frame and semicolons for drop frame.
* Clip names "31 characters maximum" (p.110): not enforced either way.

## Import (`interop::ale::importText`, `importFile`)

| Log | Native model |
|---|---|
| A data line | A master clip with a new MobID; an audio clip when its Tracks have no V. |
| Name | Its name. A line without one is left out. |
| FPS (the clip's, else the global one) | Its edit rate: 23.98 and 23.976 are 24000/1001, 29.97 30000/1001, 59.94 60000/1001. Without FPS, 23.976 with a warning. |
| Start | Start, as the bin writes it (`;` before the frames for drop frame). A clip without one is left out. |
| End, Duration | The clip's length is End − Start (End is the frame after the last, as Avid's own logs and bins have it; past midnight counts on), else Duration. When both are given and disagree the clip is left out, as Media Composer leaves it out and reports it (p.114). End is written as Start + length. |
| Tracks | The Tracks column in the bin's form: VA1A2 is "V1 A1-2". |
| Tape (else the global TAPE, else the file's name) | The Tape column and a tape source (with Start as its start timecode). A clip with a Source File and no tape gets an import source instead (path: Source Path and Source File). |
| Titles that are standard bin headings (p.274-278), matched without regard to case (COMMENTS is Comments) | Those fields. |
| Other titles | Custom columns of the bin (p.269), in the log's order. |
| Creation Date, Modified Date, Color, Lock | Not read (the bin shows its own); Color and Lock with a warning when set. |

* **Leniency:** lines may end in CR LF, LF or CR; the text is UTF-8, else
  Latin-1; a global title may be followed by spaces instead of a tab, and
  several may share a line; cells past the titles are left out (with a
  warning unless empty), missing cells are empty. Nothing makes the import
  fail.
* `ale::isShotLog`: an `.ale` file, or a `.txt` file whose first line is
  `Heading` ("You can use the file name extension .txt", p.106).

## Export (`interop::ale::exportText`, `exportFile`)

* **Which clips:** master clips, audio clips and subclips for Export to
  File ("ALE and tab-delimited files include information for master clips
  and subclips only", p.130, p.1174); master clips only for Export Bin
  ("The system creates a shot log of only the master clips in the bin",
  p.130). Other items are left out with a warning.
* **Heading:** FIELD_DELIM TABS; VIDEO_FORMAT from the first clip's Format
  column (1080, 720, NTSC for 486 or 480 lines, PAL for 576; 1080 without
  one); AUDIO_FORMAT 48kHz (ours); FPS the first clip's rate.
* **Columns:** Name, Tracks, Start, End, Duration, then Tape when a clip has
  one, FPS when the clips' rates differ, then the columns asked for (the
  bin's Text view columns and custom columns in the application) or every
  heading the clips have (standard ones in the guide's order, then the
  others). More than 64 titles: a warning.
* Values with tabs or line breaks are written with spaces. Lines end in CR
  LF.

## In the application

The menus and dialogs are in ui-inventory.md §19.

* **Import:** a shot log selected for Import in the Source Browser
  (p.131-132 steps 3-6) is read as clip information, not media
  (`MainWindow::importShotLogs`). Its custom columns are added to the bin.
* **The Shot Log tab** (11.4c2; `model::shotLogEvents` in `src/model/shot_log.*`): see "Combining and merging events"
  below.
* **File > Output > Export Bin** (p.130): the Export Bin As dialog, Avid
  Log Exchange; the active bin's master clips with the bin's columns.
* **File > Output > Export to File** with the Export setting's Export As at
  Avid Log Exchange (p.129-130, p.1174): the selected master clips and
  subclips. **Tab Delimited** (11.4f3; p.130, p.1174: "a shot log file in the
  form of a tab-delimited ASCII text file") writes the same clips and columns
  as a `.txt`: a line of the column titles, then a line per clip, without the
  Heading, Column and Data sections (`ExportOptions::tabDelimited`; ours: the
  guide does not give the layout).

## Combining and merging events (p.1197-1198)

The Import setting's Shot Log tab chooses what a log's events become. The guide gives each choice a sentence; the
rest is ours, marked so.

| Choice | Ours |
|---|---|
| Maintain events as logged | A master clip (or audio clip) for each event, as above. |
| Combine events based on scene and automatically create subclips | Events with a Scene value become one master clip for each scene, and a subclip of it for each event, with the event's name and columns. **Ours:** events are grouped per source (Tape, else Source File) and rate as well as by scene, since one master clip has one source; the master clip is named for the scene's value, spans its events' first Start to last End, has every event's tracks and the first event's other columns, and comes into the bin before its subclips. Events without a Scene are kept as logged, with a warning. |
| Combine events based on camera roll and automatically create subclips | The same with the Camroll column. The guide adds "for a film project"; ours takes the column in any project. |
| Merge events with known sources and automatically create subclips | Each event becomes a subclip of the selected clip (master or audio clip) with the same source and rate whose Start to End holds it. **Ours:** an event that no selected clip holds is kept as logged, with a warning. |
| Merge events with known master clips | The log's columns are merged onto the selected master clip with the same tape name or source file name and exactly the same Start and End (Merging Additional Metadata for Clips: "This must be an exact match and so should the START and END timecodes"). The clip keeps its name, media and sources. **Ours:** an event that matches no selected clip is added as logged, with a warning. |

The merge modes read the clips selected in the bin when the import starts ("You must select the clips that you want
to merge before selecting this option", p.1198).

## Checks

* `tst_ale`: the guide's sample log (p.112), standard and custom columns,
  durations (drop frame, disagreement, midnight, missing values), leniency,
  FPS values, round trips (bin -> ALE -> bin, the same by structure dump,
  through a file), Export Bin's and Export to File's choices, 300 randomly
  damaged logs, which files are shot logs, and every OpenTimelineIO sample
  log read, written and read again the same; `shotLogEvents`, the Shot Log
  tab's five choices; `exportChoices`, Tab Delimited too.
* `tst_interchange` (UI): Import through `importFiles`, Export Bin and
  Export to File; merging onto a selected take and combining by scene.
* `cutthulhu --import-ale FILE` prints the bin as a structure dump;
  `cutthulhu --export-ale BIN OUT.ale` writes a .cthbin's clips (an .ale is
  imported first, a round trip).

## Later

* CDL values merged through the Shot Log tab onto clips' colour adapters
  (p.543, p.549): the columns are merged, not yet applied.
* Film titles carried as such (KN Start/End/Duration, Ink Number, Pullin,
  Pullout, Perf): kept as columns now.
* SOUNDTC_FPS and AUDIO_FORMAT applied to clips; FILM_FORMAT.
