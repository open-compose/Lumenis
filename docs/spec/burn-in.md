# Timecode Burn-In (Phase 6.3d1)

Pages are the FX Guide's. "Lets you add timecode displays that update in real time, or text
annotations, to the video in your sequences ... A typical use is to apply the effect to an empty video
track above all other video content" (p.463); "Timecode Burn-In effect parameters are not
keyframeable" (p.505).

Code: `media::isBurnIn`, `burnInText`, `burnInSetting`, `burnInPicture` (`src/media/burn_in.cpp`),
called by the compositor for a segment with the effect; `media::BurnInInfo` in `Composition` (the
sequence's bin item and marks, and the source clips, filled by the Composer's picture resolver in
`MainWindow`); `model::timecodeFormatOf` and `startFrameOf` (`bin.h`). The Effect Editor's tabs:
`EffectEditor::burnInPanel`, `setBurnIn`, `burnInControl`. Tests: `tst_burn_in`, `tst_effects`
(`burnInPanel`).

## The displays

* Three Displays, each turned on with its Enable button, and a Notes Track whose display "appears as
  soon as you type text in the text box" (p.505, p.508). Ours: a tab for each in the Effect Editor;
  stored as `"Timecode Burn-In Effect Parameters": {"Display 1".."Display 3", "Notes": {...}}`.
* Reader menu (p.505-506):
  * Timecode: the sequence's, as the tracking displays give it, from the sequence's Start (one hour
    until set).
  * Source Timecode: "The timecode of the source clip at the current location", from the clip's Start
    column, on the Source Track: "Current Track, which refers to the track the Timecode effect is on,
    or any other video or audio by its track name" (p.507).
  * Frames: the sequence's frame number, or with Local Frame Count "the frame number for the current
    clip", plus Start Counting From (p.507).
  * Source Clip Name, Sequence Name; Source Bin Column and Sequence Bin Column with the Column Name.
  * Timeline Clip Text: "The comments will appear with the clip ... The effect will not show anything on
    clips where there are no comments": ours, the segment's Timeline Clip Note (docs/spec/clip-notes.md).
  * Edgecode, Source Edgecode and Other Counters need film material: they show nothing until film
    projects come.
* Type menu (p.506): Current, End, Duration, Mark IN, Mark OUT, I/O, Remaining, Absolute, "equivalent
  to options available in the Tracking Information menus". For Source Timecode ours: End and Duration
  of the clip; Mark IN, Mark OUT and I/O the segment's first and last frames and length.
* Display Label: "the display begins with a label identifying the timecode format, for example, Mas TC1
  01:00:01:16" (p.506). Ours for the rest: "Src TC1" for Source Timecode, "Mas Dur" and the like for the
  other Types, "Frm" for Frames; no label for names and columns.
* Display Fields adds the field number for field-based media; ours are progressive, so nothing is added.

## Appearance (p.507-508)

* Position: the X and Y of "the display's anchor point ... (located at the top left corner of the
  display)". Ours: percent of the picture; until set Display 1 at (5, 85), 2 at (5, 75), 3 at (5, 65),
  the Notes at (5, 5). Dragging the anchor point in the monitor comes with the Effect Preview's tools.
* Font Size "in absolute pixels". Ours: pixels of a picture 1920 wide, scaled to the picture, so the
  monitors show what an HD output would; 36 until set.
* Text colour (white) and Text Opacity (1); Use Text Outline with the Line Weight as the outline's width,
  or without it the Line Weight thickens the characters (1); Background colour (black) and Border
  Opacity (0.5), "0 (fully transparent) to 1 (fully opaque) in 0.1 increments". The values until set
  are ours.
