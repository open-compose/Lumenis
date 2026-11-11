# The Sequence Report (9.2c)

Editing Guide p.368-371 "Sequence and Clip Information Summary", "Creating a Summary of Effects and Source Information",
"Summary Information Options"; p.237-238 "Sequence Report for AAF Imports". Code: `src/media/sequence_report.*` (the
report), `src/ui/sequence_report_dialog.*` (the dialog), `src/ui/main_window_report.cpp` (the commands, files and the
AAF popup report).

## Opening it

* "From a bin, right-click a sequence and select Sequence Report. You can select multiple sequences for generating
  reports" (`bin.sequence_report`).
* "With a sequence loaded in a monitor, right-click the monitor and select Sequence Report" (`composer.sequence_report`).
  "Loading a sequence in the Source/Record monitor before you generate a report allows you to select which part of the
  sequence": that monitor's marks (Use Marks) and, for the Record monitor, the Timeline's selected tracks (Enabled
  Tracks Only).

## The dialog (the picture on p.369)

* Bin, Name and Starting TC with **Apply Changes** ("You can modify the sequence name and the starting timecode in the
  Sequence Report dialog box"): the sequence is renamed and its Start column set. Dimmed when several sequences are
  reported.
* **Enabled Tracks Only**, **Use Marks**.
* Summary Info Options (p.370-371): Create Effect Summary; Create Effect Location List with Skip Non-Renderable Effects,
  Skip Relationship-Only Color Correction, Show Nested Effects Only, Show Missing Effects Only; Create Clip Summary;
  Create Source Summary with Offline Only, Skip Non-Selected Clips In Group Clips, Show Globally Unique Identifier
  (UID). The four summaries start on, as in the picture.
* **Generate Report** opens Save Summary Output File As for the first report; other sequences' reports go beside it,
  named after them (ours). "If you select more than 8 sequences, a dialog box asks if you want to generate sequence
  reports for all selected items." "The application writes the report to a text file and opens a text editor": the
  system's application for text files.

## What the report holds (the words and layout are ours)

* A heading: the bin, the sequence, its Starting TC, the project, the tracks and marks covered, when it was made.
* **Effect Summary**: "the types of effects and how many were found in your sequence, the breakdown by effect type, and
  an effect plug-in summary". Plug-ins (OFX, 6.11) with name, vendor, version and ID; a missing one as "Unavailable
  Effect".
* **Effect Location List**: "track, start timecode, end timecode and effect name". The end is the timecode after the
  last frame, as an OUT is. Effects counted: segment effects (titles, keys, Resize ...), transitions, color corrections,
  Audio EQ, AudioSuite and pan/volume (gain, pan or automation on an audio segment). Non-renderable: Audio EQ and
  pan/volume (ours: what plays in real time without a render). Relationship-only correction: a correction applied
  through a relationship (Src Tape, Master Clip ...), not to the segment alone (p.616). Nested effects show "(V2) V1"
  for track V1 of a nest on V2, indented.
* **Clip Summary**: "the number of clips found, type of clip, track, offline information, clip name, and clip Mob ID".
  A group clip's angles are listed, the selected one marked. Offline: master, sub and audio clips whose media are missing
  or not found.
* **Source Summary**: "the number of tape-based sources found, project name, tape name, tape ID, and tape Mob ID ... a
  list of import paths for any imported clips". Ours: the tape ID is the tape's number in the list.
* Show Globally Unique Identifier (UID) adds the Mob ID columns to both summaries (ours: without it they are left out,
  although the guide lists the clip Mob ID under the Clip Summary).
* CSV ("Report As Comma Separated (CSV)", p.238): the same sections, each row as comma-separated fields.

## Popup Report on AAF Import (p.237-238)

The Import setting's OMFI/AAF tab: Popup Report on AAF Import; Report As Generic Text or Report As Comma Separated
(CSV); Include Effect Summary with Add Markers at Effect Locations and Show Missing Effects ONLY; Include Effect
Location; Include Clip Summary; Include Source Summary with Show Offline Sources ONLY. "When you import an AAF, a
sequence report will automatically open": each imported sequence's report, written to the project's `Reports` folder
(ours) and opened. Add Markers at Effect Locations puts a marker named after each listed effect at its first frame on
its track (a nested effect's parent track).

## Tests

`tst_sequence_report`: the effects and the options that leave them out, Use Marks and Enabled Tracks Only, the
summaries (offline clips, tapes, import paths, UIDs), CSV; the bin's Sequence Report with Apply Changes and the file;
the AAF popup report as CSV with a marker at the Dissolve.
