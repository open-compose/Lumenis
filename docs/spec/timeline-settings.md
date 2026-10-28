# Timeline settings (UI completion pass U1e)

The Timeline settings' options that were still dimmed (Editing Guide p.1218-1221). Pages are the
Editing Guide's. U1e1 is the Edit tab; U1e2, the Display tab, comes next.

Code:
* The options are `Timeline::Options` (`src/ui/timeline.h`), read from the settings in
  `MainWindow` (`src/ui/main_window.cpp`); the entries are in `src/ui/resources/settings.json`.
* Model: `model::moveSegments(..., keepSync)` and `model::syncLockAllTracks` (`src/model/sequence.*`).
* Tests: `tst_timeline_settings`, scenario `22-segment-drag-sync-locks.json`.

## Edit tab (p.1220-1221)

* **Auto-Patching**: "Media Composer automatically patches the enabled source tracks to the tracks
  enabled in the Timeline sequence."
  * Ours: of each kind, the enabled source tracks in order go to the enabled record tracks in order.
    An audio track goes only to a record track of its format (p.629).
  * It happens when another clip is loaded into the Source monitor and when a source or record track
    button is clicked. A patch made by hand stays until then.
* **Auto-Monitoring**: "Media Composer monitors the track you patch. This option is selected by
  default." "When you patch from one video track to another, the Video Track Monitor icon moves to
  the track you are patching to" (p.629).
  * The monitor moves from the old record track to the new one, if the old one was monitored.
* **Segment Drag Sync Locks** (p.618, p.493): after an Extract/Splice-in drag, filler is added
  "where the segment was moved from in the sequence" and "on all other sync-locked tracks that
  correspond to the new location of the segment you moved."
  * Ours: this applies when the track the segment goes to is sync-locked.
  * Tracks that are locked, or that end before the new place, get no filler.
* **Segment Drag Snap**: about dragging clips from a bin into the Timeline (bin editing, p.623-624),
  which is not built yet. It stays dimmed until bin editing comes with the bins in U3.
* **Position Bar Snap**: "clicking in the Timeline will snap the position indicator to the nearest
  event, such as IN and OUT points, markers, and segment start and end points"; p.397: "to the nearest
  transition."
  * Ours: a click snaps when an event is within 8 pixels. Dragging the indicator does not snap.
  * Ctrl's own snapping (p.609) is unchanged.
* **Default Sync Locks On**: "Enables sync locks on all video and audio tracks as the default
  Timeline setting."
  * New sequences have every video and audio track sync-locked: from New Sequence, and the first edit
    into an empty Record monitor.
  * Ours: so are tracks added with New Video/Audio Track.
* **Select Filler with Segment Tools** (p.616): whether a click or a lasso with a segment tool selects
  filler.
  * Ours: on by default, as segment selection worked before. Filler at the ends of a track is never
    selected (p.611).
* **Applying Effects opens Effect Editor**: "the Effect Editor automatically opens when you apply an
  effect."
  * Applies to effects dragged onto the Timeline and effects double-clicked in the Effect Palette.
  * Ours: off by default.
* **Clicking the TC Track or ruler Disables Smart Tools** (p.722): a click on the ruler or the TC1
  track turns off both segment tools and both trim tools.
* **Lassoing transitions enters**: Trim Mode (the default), or Segment Mode: "Select this option if
  you want Media Composer to enter Segment Mode when lassoing transitions" (p.1221).
  * Ours, in Segment Mode: a lasso that holds no whole segment selects the segments the lasso
    reaches, either side of the transitions. The Default Segment Tool goes on if no segment tool is.
  * The guide's "only when the Keyframe Selection Tool is deselected" has nothing to check yet: we
    have no Keyframe Selection Tool.
