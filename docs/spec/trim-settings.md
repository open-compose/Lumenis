# Trim settings and trim displays (UI completion pass U2e1, U2e2)

The Trim settings (p.1223-1224) and what they serve: the Play Loop and Edit Review commands, J-K-L
Trim (U2e1); the outgoing and incoming frames of the trim displays, Small Trim display, Dual Image
Play and Sync Rollers (U2e2). Pages are the Editing Guide's.

Code:
* `Timeline::playTrimLoop`, `editReview`, the J-K-L Trim hooks in the constructor and
  `composerPerformed` (`src/ui/timeline.*`); `Timeline::Options::trimPreroll`, `trimPostroll`,
  `trimIntermission`, `jklTrim`, read by `MainWindow::applySettings`.
* `Composer::playLoop(monitor, LoopResolver, pauseMs)`, `isLoopPaused`, `setKStepHandler`, and the
  Play Loop button of the trim display (`src/ui/composer.*`).
* `Composer::TrimDisplay` (`small`, `aFrame`, `bFrame`, `dualImage`), `trimFrame`,
  `setTrimPlaySide`, the Small Trim widget and `MonitorPicture::setTrimSide` (`src/ui/composer.*`);
  `Timeline::syncRollers` and `syncTrimDisplay` (`src/ui/timeline.*`).
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
* Features tab: Never use Small Trim mode (the default, p.649), "Go to Transition" uses Small Trim
  mode, Always use Small Trim mode, Dual Image Play and Sync Rollers for Sync Locked Tracks; below.
* Still dimmed: Render On-the-Fly (Phase 6, effects).

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

## The trim monitors' frames (p.648, p.655)

* Big Trim display "replaces the Source and Record monitors with displays of outgoing and incoming
  frames" (p.648); "The new outgoing frame displays in the Source monitor as you trim" (p.655).
  * The Source monitor shows the Record monitor's sequence at the frame before the first roller's
    transition, the Record monitor the frame at it. The marks and markers are not drawn on them.
* "Click either the outgoing (A-side) or incoming (B-side) monitor to play in real time during the
  trim" (p.659). Clicking a trim picture makes the Record monitor active (so J-K-L plays the sequence)
  and makes that side the one that follows play; the other keeps its frame. Ours: the B side follows
  by default, and the side that follows has the active monitor's outline.
  * Ours: what plays is the sequence. For an A-side trim past the cut the picture shows the incoming
    material, not the outgoing clip's later frames.

## Small Trim display (p.648-649, p.1223)

* "Small Trim display leaves the monitor display intact and has smaller displays of outgoing and
  incoming frames ... replaces Record monitor in two-monitor view. Outgoing and incoming frames appear
  side-by-side on the right, with playback loop parameter controls above them, and with frame offset
  counters and Trim buttons beneath them" (p.648).
  * The Source monitor keeps its position bar and buttons. In place of the Record monitor, its position
    bar and buttons: the two trim pictures with a counter under each; the Trim buttons are beneath.
    The loop boxes are not built (as in Big Trim).
* "Always use Small Trim mode — ... Media Composer enters Small Trim mode when you perform any
  operation that activates Trim mode" (p.1223).
* "'Go to Transition' uses Small Trim mode — ... Media Composer enters Small Trim mode only when you
  click the Go to Previous or Go to Next button" (p.1223): Small Trim display when Trim mode moved to a
  transition with Go to Previous/Next Event (p.655), until Trim mode ends.
* Ours: in single-monitor view, where there is no Source monitor to keep, Trim mode uses Big Trim.
  Quick Trim display (p.648) is not built.

## Dual Image Play (p.659)

* "Dual-image playback lets you view A-side and B-side frames in real time while performing a trim.
  You can play through the transition by using the J-K-L keys or the Play and Trim buttons" (p.659).
* With it, while the Record monitor plays in Trim mode, the A picture shows the frame before the
  position and the B picture the position.
* "Using the Dual Image Play option disables real-time effects" waits for real-time effects (Phase 6).

## Sync Rollers for Sync Locked Tracks (p.656, p.1223-1224)

* "When you enter single roller trim, gray sync rollers appear on unselected sync locked tracks making
  it easier to see which tracks will be affected by the trim. When you are actually performing the trim
  you will see the effect that the trim has on the sync locked tracks as you are trimming" (p.656).
  * Gray rollers, on the same side and frame as the first roller, on each sync-locked track without a
    roller, when every roller is single-sided, the trim is a ripple trim and a roller's track is
    sync-locked (overwrite trims move nothing on the other tracks). While a roller is dragged, a dashed
    line shows where they go.
  * The tick marks "for visual reference only" are not drawn. The gray shade is ours.
* "When you <shift> select a transition on a track with a sync roller, the trim is changed to the
  selected transition instead of adding another roller" (p.656).
