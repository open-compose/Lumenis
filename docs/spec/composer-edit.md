# Composer settings, Edit tab, and Load Filler (UI completion pass U2a)

The Composer settings' Edit tab options that were still dimmed (Editing Guide p.1159-1160), Composer >
Sync Point Overwrite and Composer > Load Filler. Pages are the Editing Guide's.

Code:
* Model (`src/model/sequence.*`): `planEdit(..., singleMark)`, `planSyncPoint`,
  `applyEdit(..., createTracks)`.
* Timeline (`src/ui/timeline.cpp`): the options in `Timeline::Options`; `edit`, `loadFiller`,
  `setSyncPointEditing`, `phantomMarks`.
* Composer (`src/ui/composer.cpp`): the phantom marks in the position bars, Load Filler in the
  Source monitor's Clip Name menu.
* `ButtonMapper::setMarked` (`src/ui/button_mapping.*`): the orange mark on a button.
* Tests: `tst_composer_edit`, scenario `23-composer-edit.json`.

## Sync Point editing (p.495-496, p.1159)

* "Sync Point editing lets you overwrite material onto your sequence so that a particular point in
  the source material is in sync with a particular point in the sequence."
* The positions are the sync points. Two marks give the duration: "Mark the In and Out points in
  either the Source or Record monitor, leaving the opposite monitor clear of marks", or "an In or Out
  point in the Source monitor, or an In or Out point in the Record monitor".
  * Ours: any other set of marks is refused with a message.
* It is turned on by "Sync Point Editing (Overwrites) in the Edit tab of the Composer Settings dialog
  box or select Composer > Sync Point Overwrite. The orange mark on the Overwrite button signals Sync
  Point editing is active."
  * The menu command turns the mode on and off for the session. The setting gives the mode at start
    and whenever the settings are applied again.
  * The orange mark is a small triangle in the corner of every button showing Overwrite.
  * Only Overwrite edits by sync point; Splice-in edits as usual.

## Single Mark Editing (p.468, p.1159)

* "You can mark an In point in the Source monitor and then perform a splice-in, overwrite, or replace
  edit without marking an Out point. You can mark an Out point, locate a frame for the In point, and
  then perform the edit without marking the In point."
* A source IN alone ends at the source position when the position is after it. With the position on
  or before the IN, the edit runs to the clip's end, as with phantom marks (p.501).
* A source OUT alone starts at the source position.
* Off (ours): a source IN alone runs to the clip's end; a source OUT alone starts at the clip's start.

## Phantom Marks (p.501, p.1159)

* "Media Composer displays blue mark In or Out icons in the position bars below both the Source and
  the Record monitors. These phantom marks indicate one, two, or sometimes three edit points
  calculated by Media Composer to complete an edit."
* Drawn as blue brackets where the next Splice-in or Overwrite would put its IN and OUT, on each
  monitor where that mark is not set. They follow Single Mark Editing and Sync Point editing.

## Auto-create New Tracks, Auto-enable Source Tracks, Copy Source Markers (p.1159-1160)

* **Auto-create New Tracks** (on by default): "whenever you edit material into the Timeline, Media
  Composer automatically creates any new tracks in the sequence that match existing tracks in the
  source material."
  * Off: "if you have at least one track ... but your source material is patched on an empty v1, a
    new track will not be created." Those tracks are left out of the edit.
* **Auto-enable Source Tracks** (on by default): "whenever you load new source material into the
  Source monitor, Media Composer automatically enables all existing source tracks."
  * Off: tracks keep the selection they had.
* **Copy Source Markers** (on by default): "if you have markers in a clip in the Source monitor, Media
  Composer copies the markers when you edit the clip into the Record monitor."
  * The markers within the edited material go onto the record tracks their tracks are patched to.
  * Ours: a spanned marker is cut short at the end of the edited material.
* **Undo Only Record Events** stays dimmed: our marks are not undo steps, so Undo already undoes only
  record events (p.466).
* **Color Framing** stays dimmed: it is for tape (Phase 9).

## Load Filler (p.462, p.99)

* "Click above the Source monitor, and select Load Filler from the Clip Name menu. The system loads a
  2-minute clip of filler into the Source monitor." It is also Composer > Load Filler.
* Ours: the filler is a sequence of filler in no bin, as Clipboard Contents is. It has the record
  sequence's video and audio tracks (V1, A1 and A2 without one), so it patches like a clip.
