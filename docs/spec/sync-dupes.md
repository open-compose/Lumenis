# Sync breaks, Dupe Detection and Clip Text (UI completion pass U1c1)

The Timeline Fast menu's Sync Breaks, Dupe Detection and the rest of Clip Text. Pages are the Editing
Guide's.

Code:
* `model::syncBreaks`, `model::dupes` and `model::dupeColor` in `src/model/sync_dupes.*`.
* `Timeline::clipText`, `setSyncBreaks`, `setDupeDetection` and their painting in
  `src/ui/timeline.cpp`.

## Sync breaks (p.490-492)

* "By default, the Timeline displays sync breaks whenever they occur while you edit. They appear at
  break points as white numbers indicating negative or positive offset values relative to zero. The
  Sync Breaks option also displays match-frame edits as an equal sign (=) on the edits" (p.491).
* **Which material.** "The Sync Breaks feature applies only to master clips in which audio and video
  tracks were captured simultaneously, to autosynced subclips, or to any other subclip with video and
  audio tracks" (p.491).
  * Ours: a segment's partners are the segments of the same clip on tracks of the other kind (video
    or audio) that overlap it in the record.
  * Where any partner shows the same source frame at the same record frame, the segment is in sync
    there.
  * Where only out-of-sync partners overlap, it is a break. The offset is taken from the partner on
    the first track (V1 before V2, A1 before A2).
  * Data tracks and filler have no sync.
* **The number.** Ours: the source frames by which a track's material is ahead of its partner's
  (positive when it shows later material). "Sync-break offset numbers appear by default only in the
  affected tracks" (p.491): both sides of a break show it, with opposite signs.
  * The number is drawn at the right end of the out-of-sync run, in the palette's BrightText (white
    on the default theme).
* **Sync Breaks submenu.** "You can customize the Timeline view to display sync breaks and
  match-frame edits in video tracks only, audio tracks only, or neither" (p.491).
  * The guide names no items. Ours: All Tracks (the default), Video Tracks Only, Audio Tracks Only and
    None.
  * The choice is kept in Timeline views (docs/spec/timeline-views.md).

## Dupe Detection (p.641-642, p.1220)

* "You can choose to activate Dupe Detection for video, audio and data tracks" (p.641): the Dupe
  Detection submenu has Video, Audio and Data, each on or off.
  * The table on p.593 says "for V1 track"; we follow p.641.
* "each set of duplicate frames is tagged with a different color. (Up to 10 color sets can be
  distinguished during a single detection process.) Matching frames have matching colors ... The
  colored bars that distinguish duplicate frames in the sequence appear automatically above the
  frames in the Timeline" (p.641).
  * Ours: two segments are duplicates where they use the same source track of the same clip and their
    source ranges overlap. Each overlapping pair is a set.
  * The sets are numbered by their first record frame.
  * "Orange bars mark the first set of duplicate frames, green bars mark the second set, and so on"
    (p.641). The eight colours after those are ours (`model::dupeColor`), and the 11th set repeats the
    first's colour.
  * A bar is 4 pixels high along the top of the row.
  * Bars are worked out at every paint: "Dupe Detection occurs instantaneously and retroactively"
    (p.641).
* **Dupe Detection Handles** (Timeline settings, Edit tab, p.1220): "Media Composer adds the chosen
  number of frames at the beginning and the end of each clip before checking for overlap."
  * "The typical 35mm safety frame setting is 0.5 frame" (p.642): half frames are exact.
  * "the handles are added to the colored dupe indicators" (p.642): a segment's bar covers its frames
    within the other's widened range.
  * The choices (0, 0.5, 1, 2, 3, 4, 5, 8, 10) are ours.

## Clip Text (p.592-593)

* Clip Names, Clip Tracks and Clip Durations were there already. Now also, in the menu's order:
  * Clip Full Durations: the source clip's whole duration;
  * Clip Trans Duration: the duration of the transition at the segment's start;
  * Source Names: the clip's Tape column, or else its Source File;
  * Media Full Names: the name of the media file the segment's track plays from, with its extension;
  * Clip Resolution: the clip's Video column.
* The guide names these items but not what each shows. What each shows is ours, as above.
* **Still dimmed:**
  * Group Clip Name, which needs groups (multicamera editing);
  * Source Track Name, which needs the per-channel track names of production sound metadata
    (p.593-594);
  * Timeline Clip Notes, which comes with Add Timeline Clip Note and the Timeline Clip Notes window
    (U1c2, p.483-484).
* **Zoom > Show Every Frame** stays dimmed: "To enable this option, you must first select Show Track >
  Film" (p.593), and there is no Film track until film projects.

## Tests

* `tst_sync_dupes`:
  * sync breaks: in sync, extracted, a second track still in sync, and other clips;
  * dupes, with half-frame and two-frame handles, and the colours;
  * the menus, Clip Text, Sync Breaks and Dupe Detection bars in the Timeline.
