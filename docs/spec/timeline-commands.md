# Timeline commands (UI completion pass U1d1)

Add Filler at Start, Find Black Hole, Find Flash Frame, Rename Track and Create Sequence Based on
Selection. Pages are the Editing Guide's.

Code:
* In `model::Sequence` (`src/model/sequence.*`): `addFillerAtStart`, `findBlackHole`,
  `findFlashFrame`, `selectionCopy`, and `Track::customName`.
* In `Timeline` (`src/ui/timeline.cpp`): `addFillerAtStart`, `findGap`, `renameTrack`,
  `createSequenceFromSelection`.
* `MainWindow` places the new sequence in a bin.

## Add Filler at Start (p.462, p.1220)

* "Select Timeline > Add Filler at Start" or "Right-click in the Timeline, and select Add Filler at
  Start. Filler appears at the beginning of the sequence in the Timeline. You can set a default
  duration for the filler in the Edit tab of the Timeline Settings dialog box" (p.462).
* "You cannot add filler to the end of a sequence or to an empty sequence" (p.462): refused, with a
  message, for an empty sequence.
* The filler goes before every track that has something on it, so the tracks stay in sync. It is one
  undo step, refused while a track is locked.
* **Start Filler Duration** (Timeline settings, Edit tab, p.1220): the guide gives no unit or default.
  Ours: seconds, 2 by default. With 0 the command is refused with a message.

## Find Black Hole and Find Flash Frame (p.644-645, p.1220)

* "Black holes are segments of the sequence consisting of one or more frames of filler. Flash frames
  are clips with an extremely short duration" (p.644).
* "Select the tracks you want to search. Move the position indicator to the beginning of the
  sequence or before the part of the sequence you want to search. Select Timeline > Find Black Hole.
  The position indicator moves to the first segment that contains filler" (p.644-645). "Select
  Timeline > Find Black Hole again" finds the next.
  * Ours: the search starts after the Record position, or at frame 0 when the position is there.
  * Filler up to the end of the sequence counts, including the stretch after a track that ends
    before the others.
  * With nothing left to find, or no track selected, a message says so.
* "Find Flash Frames Shorter Than: ... The default is 10, which indicates the system will detect
  clips with 9 or fewer frames" (p.645, p.1220).

## Rename Track (p.635)

* "Right-click the Track Selector button, and select Rename Track. The Comments window opens. Type a
  new name for the track. Click OK." Its Remove button: "The track name returns to the default track
  name, such as V1."
* The name shows on the track button, cut short to fit.
* "When you rename an audio track, the corresponding name along with the track number appears in the
  Audio Mixer tool": the strip's track button reads, for example, "A1 Dialog".
* Kept with the sequence (`customName` in the composition), without an undo step, as locks are
  (ours). Commands and patching still use V1, A2 ...

## Create Sequence Based on Selection (p.613-614)

* "Select clips in the Timeline ... Right-click and choose Create Sequence Based on Selection. A Select
  a bin dialog box opens (if you have more than one bin open). Choose the bin where you want to place
  the duplicate sequence ... A new sequence is placed in the selected bin, highlighted and ready to be
  renamed" (p.614).
  * The selected clips keep their places; everything else becomes filler.
  * "Tracks that did not have something selected in the original sequence are not included ... If
    there was filler at the end of the original sequence, it is removed in the duplicate."
  * "Track attributes such as custom names, mute/solo states, and waveforms are maintained": the
    tracks are copied whole, with their per track settings.
* **Ours:**
  * The sequence is named as Edit > Duplicate names a copy, "<sequence>.Copy.NN" (p.260).
  * It is selected in the bin.
  * The record sequence's bin comes first in the Select a Bin list.
  * The menu item is dimmed with nothing selected.

## Tests

* `tst_timeline_commands`:
  * the model: black holes, flash frames, filler at start, the selection copy, custom names;
  * the application: the commands, Start Filler Duration, the right-click menus, Rename Track with the
    Audio Mixer, Create Sequence Based on Selection into the bin.
* Scenario `21-filler-at-start.json`.
