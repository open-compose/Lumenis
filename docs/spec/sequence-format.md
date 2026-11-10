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
