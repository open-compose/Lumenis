# Trim settings: Play Loop, Edit Review and J-K-L Trim (UI completion pass U2e1)

The Trim settings' Play Loop tab and its J-K-L Trim option, with the Play Loop and Edit Review
commands they serve. Pages are the Editing Guide's.

Code:
* `Timeline::playTrimLoop`, `editReview`, the J-K-L Trim hooks in the constructor and
  `composerPerformed` (`src/ui/timeline.*`); `Timeline::Options::trimPreroll`, `trimPostroll`,
  `trimIntermission`, `jklTrim`, read by `MainWindow::applySettings`.
* `Composer::playLoop(monitor, LoopResolver, pauseMs)`, `isLoopPaused`, `setKStepHandler`, and the
  Play Loop button of the trim display (`src/ui/composer.*`).
* Test: `tst_trim_settings`; scenario `24-jkl-trim.json`.

## The settings (p.1223)

* Play Loop tab: "Preroll — Defines a preroll value for a playback loop. Postroll — Defines a postroll
  value for a playback loop. Intermission — Defines a transition effect duration for a playback loop."
  * Ours: the three are in seconds (the guide gives no unit), with the defaults 2, 2 and 0.
  * Ours: the Intermission is a pause at the end of each pass before the next one starts. The guide's
    "transition effect duration" does not say what it adds to the loop; this is on the watch list.
* Features tab: "When the J-K-L Trim option is selected, you can use the J-K-L keys in Trim mode."
  It is on by default ("To use the J-K-L keys to only play, step, and shuttle without trimming,
  deselect the J-K-L Trim option", p.658).
* Still dimmed: "Go to Transition" and Always use Small Trim mode, Render On-the-Fly, Dual Image Play
  and Sync Rollers for Sync Locked Tracks (U2e2 and Phase 6).

## Play Loop (p.649, p.656-657, p.659-660)

* "The Play Loop feature continuously replays the last trim you performed for review purposes"
  (p.649). "Click the Play Loop button. The system enters a playback loop. This loop begins at a
  preroll point before the transition and ends at a postroll point" (p.657).
  * The transition is the first roller's. The loop plays the Record monitor from the cut less Preroll
    to the cut plus Postroll (the last frame before it), clamped to the sequence.
* "Press a keyboard equivalent to perform a Trim function ... Media Composer performs the trim before
  the next playback loop" (p.660). A trim during the loop is made at once; the position stays with the
  loop, and the next pass is worked out again from the moved transition.
* "When you finish, exit the playback loop by doing one of the following: Press the space bar. Click
  the Play Loop button" (p.660). Trim mode stays.
* From Source/Record mode: "Click the Play Loop button on a palette twice ... When you click the Play
  Loop button once, the system plays the transition in a playback loop ... The action takes you to the
  last trimmed transition" (p.656). Play Loop enters Trim mode at the last rollers (as Alt+Trim Mode),
  or with none at the nearest transition.
* "The Play Loop button does not appear in Source/Record mode by default. You must map it" (p.656):
  the command `trim.play_loop` is on the Command Palette's Trim tab. Ours: the Big Trim display has a
  Play Loop button after the Trim buttons, lit while the loop plays; its picture is ours. The Preroll,
  Postroll and duration boxes of the trim display (p.657) are not built; the settings give the values.

## Edit Review (p.657-658)

* "The Edit Review button (in the Play tab of the Command Palette) lets you review an edit or other
  change that you made to a transition. You can set how far the position indicator moves backward by
  using the Preroll option" (p.657).
* "Using the Edit Review command causes Media Composer to deselect trim rollers on all transitions"
  (p.657): in Trim mode it leaves Trim mode and plays from Preroll before the trimmed transition.
* "Move the position indicator to the transition you want to review. Click the Edit Review button. The
  position indicator moves before the previous transition and begins to play. In a sequence with
  multiple selected tracks, the Edit Review command moves the position indicator before the first set
  of edits that line up on all the selected tracks" (p.658).
  * Ours: the transition is the last one at or before the position; selected tracks with no edit at
    all (an empty track) are left out of "all the selected tracks". With none, it beeps.
* Alternate Edit, which performs an Edit Review after its replace (p.631), is not built.

## J-K-L Trim (p.658-659)

* "When you have transitions selected for trimming, you can use the J-K-L keys to play, step, shuttle
  and trim frames at the same time" (p.658).
* "Press the J or L key once to play at normal speed, or more than once to shuttle at higher speeds.
  When you see the frame where you want to relocate the transition, press the space bar or the K key to
  complete the trim" (p.659): when play started with J or L in Trim mode stops, the transition moves to
  the frame shown, by `trimBy`, within the trim's limits.
* "Press and hold the K key while pressing the J or L key to step slowly backward or forward through
  the footage. When you find the frame where you want to relocate the transition, release the K key to
  complete the trim" (p.659): slow play trims the same way; K held with J or L tapped trims one frame
  (in Source/Record mode it steps one, p.401).
* "When trimming with the J-K-L keys, you cannot completely trim away all frames in a segment. Media
  Composer always leaves one frame" (p.658): the trim limits keep a clip's last frame.
* Ours: the keys trim while the Record monitor is the active one; the trim monitors show the loaded
  clips, not the outgoing and incoming frames ("Click either the outgoing (A-side) or incoming (B-side)
  monitor to play in real time", p.659, waits for those frames). Play (the space bar) started in Trim
  mode does not trim.
