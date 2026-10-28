# The Timeline Sequence Map (UI completion pass U1d2)

Timeline > Show Sequence Map and the Timeline settings' Sequence Map options. Pages are the
Editing Guide's.

Code:
* `SequenceMap` and `SequenceMapWindow` (`src/ui/sequence_map.*`).
* The Timeline (`src/ui/timeline.cpp`) owns the map: `showSequenceMap`, `setSequenceMapInWindow`,
  `scrollTo`.
* Test: `tst_sequence_map`.

## The map (p.606)

* "The Sequence Map allows you to easily see and navigate the Timeline through a simple to use
  interface that displays the entire sequence and your current view of it."
  * The map draws every row of the Timeline's tracks area, top to bottom, scaled to fit. Segments are
    drawn in their track's or clip's colour; filler is left empty.
  * The white box is the part of the sequence the Timeline shows: its frames across and its rows down.
  * The position indicator is drawn as a blue line (ours).
* "You simply mouse down on the white box and drag in any direction on the map which re-adjusts the
  current view of the Timeline."
  * Dragging the box scrolls the Timeline across and down, and the scroll bars follow.
  * When the whole sequence fits across, the box covers it and only the vertical scroll moves.
  * Ours: a press elsewhere on the map moves the box there first.
* "Select Timeline > Show Sequence Map."
  * The command shows the map, or hides it while it is showing. The settings say the map can also be
    hidden "using menu pick or mapped key" (p.1219).
* "Right click in the white area and select Show in Dedicated Window. The Sequence map opens in its
  own window. If you want to return it to the Timeline, right click in the dedicated window and
  select Show in Timeline."
  * The window is a tool window of our own. Closing it hides the map.
  * The guide's "float, dock or tab" in the panel framework is left for U3, with the other windows.

## Timeline settings, Display tab (p.1219)

* **Open Sequence Map as overlay on Timeline**: "When this option is selected, the Sequence Map will
  appear as an overlay in the Timeline. When deselected, the Sequence Map will appear in its own
  window."
  * The overlay sits at the bottom right of the tracks area. It is 2/5 of the area's width and height,
    within limits (ours).
  * The setting decides where the map opens each time it is shown. The right-click menu moves it
    until it is hidden (ours).
* **Clicking Elsewhere Hides the Sequence Map**: "When disabled, the user must manually hide the
  Sequence Map using menu pick or mapped key."
  * Applies to the overlay: any mouse press outside the map and its menu hides it.
  * The map in its own window stays until it is closed or the command hides it (ours).
* **Display waveforms**: "Display waveforms in Sequence Map."
  * The first channel's peaks are drawn across each audio segment, from the Timeline's waveform cache
    (docs/spec/waveforms.md).
* **Display effects**: "Display effects in Sequence Map."
  * A segment with an effect, or a transition at its start, gets a violet band along its top (ours).
* The guide gives no defaults. Ours: overlay on, Clicking Elsewhere Hides on, waveforms off, effects
  on.
