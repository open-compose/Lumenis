# Batch Import and Batch Re-import (9.2a)

Editing Guide p.233-237 "Reimporting Files" and "Batch Import Dialog Box", p.229-230 (XDCAM), p.1097-1098 "Batch
Importing File-Based Media at Different Resolutions". Code: `src/media/batch_import.*` (which clips, where their files
are), `src/ui/batch_import_dialog.*` (the dialog), `src/ui/main_window_batch_import.cpp` (the commands, the import).

## What is imported again

* "The Batch Import command lets you reimport files while automatically linking the new imported material to the
  original master clips and sequences" (p.234). The active bin's selected **imported master clips** and the master
  clips its selected **sequences** use. An imported clip names its file in its Source File and Source Path columns and
  has managed media, or none (`media::isReimportable`). Clips linked to their files (p.315) are left out: they have no
  imported media.
* "When you reimport a media file, the entire media file, including all tracks, is reimported" (p.234).
* Mixed-rate sequences are refused with a message (p.234: "A message box appears if you attempt to reimport such
  material"). A sequence is mixed rate when it is at another rate than the project, or a segment plays a clip through a
  motion adapter (`media::isMixedRateSequence`). The message's words are ours. Clips at another rate than the project's
  are left out with a Console line.
* The new media take the clip's MobID, so the clip and every sequence that uses it play them. The media made before
  stay on their drives as another resolution of the clip, as p.1098 says of Batch Import: "The original media file is
  preserved, and the master clip is now associated with an additional resolution" (MultiRez, docs/spec/dynamic-relink.md).
* **Clip > Batch Import** (p.1098) and **Clip > Batch Re-import** (p.230, p.234) do the same (ours: the guide gives
  both the same steps).

## The message box (the picture on p.234)

"Import clips with offline media only?" with **Offline only** ("Reimports only the selected imported master clips that
are missing their media files"), **All clips** ("Reimports all the selected imported master clips ... if you need to
change the video resolution") and **Cancel**. A clip is offline when it has no media or its files cannot be read.

## The Batch Import dialog box (p.235-237 and its picture on p.236)

* **Selected Clips:** the caption "<found> of <shown> clips found" (the picture's "11 of 11 clips found"), and the list's
  Clip Name and Import File Location. "Found clips display in black. Clips not found in their original location display
  in red." A clip's file is found when it is where it was last imported from (p.235: "Media Composer finds the source
  file automatically if it is located in the same folder where you last imported the file").
* **Skip This Clip** takes the selected clips out of the list.
* **Set File Location...** opens the Locate File dialog for the first selected clip. For the other selected red clips,
  "the system first attempts to find the rest of the clips in the same folder as the first clip and then in folders
  that maintain the same relationship with the first clip's folder" (`media::relocatedSource`).
* **Import Target:** Video Resolution, and the drive menu with the Single/Dual Drives button for separate video and
  audio drives (p.230's picture). They start from the Media Creation setting's Import tab.
* **Import Options:** "Use source compression" is dimmed: it is for OMFI files, which we do not import (p.237).
  **Override clip settings with current settings**: "By default, each file imports using the Import settings from the
  last time it was imported". Each import keeps the Import setting's options it used with the clip
  (`BinItem::sourceSettings` "import", `MainWindow::importSettingsSnapshot`); a clip imported before 9.2a has none and
  takes the current ones. **Current Settings...** opens the Import setting.
* **Import** (dimmed while no clip is found) and **Cancel**.

## Not yet

* The Import setting's XDCAM tab's Batch Import High-resolution Video and Handle Length (p.229-230): there is no XDCAM
  proxy import yet, and with it no partial import of the parts a sequence uses.
* OMFI files (p.234: "OMFI files can contain only one master clip when you reimport them").

## Tests

`tst_batch_import`: the rules (`isReimportable`, `lastImportPath`, `relocatedSource`, mixed rates); Offline only and All
clips through a sequence; the dialog's caption, Cancel, Set File Location and Skip This Clip; the new media under the
clip's MobID with the sequence's picture from them; a moved source's new Source Path; a still's own Import settings and
the override.
