# Audio Mixer: loop, inserts, render, Fast menu and groups (Phase 5.7a)

The Audio Mixer itself (strips, modes, gangs, solo and mute) is in
docs/spec/ui-inventory.md, its automation in docs/spec/automation.md and its
surround panning in docs/spec/surround.md. 5.7a adds the controls that were
still dimmed. Pages are the Editing Guide's.

## Audio Loop Play (p.692, p.712)

* The Loop button in the Audio Mixer, Audio EQ and AudioSuite tools ("appears
  in several of the audio effect tools", p.712). It is also the command
  `play.audio_loop_play` on the Command Palette's Play tab.
* What loops, in the Record monitor (`Timeline::audioLoopRange`):
  * with IN and OUT, IN to OUT;
  * with neither, "the shortest segment on the selected audio track at the
    position indicator". With several selected tracks, the shortest of their
    segments there. When none of them has one, any audio track's (ours);
  * with only an IN, from it to the end of that segment; with only an OUT, from
    the segment's start to it.
* It plays at normal speed. At the end it starts again from the start, and so
  does the sound (`Composer::playLoop`). Clicking the button again stops it, as
  does Stop. Playing at another speed ends the loop (ours).
* Changes made while it loops are heard at once: an edit restarts the sound
  where play is (as for Audio Track Effects, p.793). Clip mode keeps them as
  clip volume and pan ("automatically saves your changes as part of a Clip
  Volume effect").
* The Audio tool's Peak Hold menu and Reset Peak work while it loops, as they
  already did.

## Audio Track Effect inserts (p.790-795)

* Each track strip has an insert button: "+" without inserts, else the letters
  of the filled inserts ("ac"). Its menu is the track button's Audio Track
  Effects menu (Insert a-e, each with No Insert and the effects by category,
  p.791), plus Audio Track Effect Tool (p.793). The picture on p.790 shows the
  inserts in the mixer; ours is one button with a menu.

## Render Effect (p.692)

* Renders the AudioSuite effects that need it on the selected tracks: in the
  segments under the position, or between the marks.
* The media goes to the Media Creation setting's audio drive.
* The guide's example is an audio dissolve. Audio dissolves are mixed as they
  play, so there is nothing to render for them yet (Phase 6, rendering). The
  Audio EQ tool's Render button waits for that too.

## Clip mode Fast menu (p.710-711)

The items act on the selected tracks between the marks. With no marks they act
on the whole track. They are dimmed until a track is selected.

* **A segment's own pan (`Segment::hasPan`).**
  * Before, a pan of 0 meant "no pan of its own". A mono segment then played
    at its track's Default Pan (p.1147: odd tracks left, even right), so a MID
    pan on an odd track played on the left.
  * Now a segment can have a centred pan of its own. The bin file writes `pan`
    whenever the segment has one, even 0.
  * The strip shows where a segment without a pan of its own plays.
* **Adjust Pan/Vols on Track...** A dialog with the volume (dB) and pan to add.
  It adds them to every segment there. A pan is added to where the segment
  plays, its default included.
* **Remove Default Clip Pan on Track.** Segments whose own pan equals the
  track's default lose it, so they follow the default from then on.
* **Commit Default Clip Pan on Track.** Segments without a pan of their own
  take the default as theirs, so they keep it when the Audio setting changes.
* **Remove Pan on Track** and **Remove Pan/Vols on Track** remove the
  segments' own pans.
* **Disable Track Monitoring.** The selected tracks' monitor buttons are
  turned off.
* **Set Display Options...** (also in Auto mode; it needs no selected track).
  Check boxes for Narrow Mixer, Faders, Legends (the fader scales), Audio Track
  Effects (the insert buttons) and Solo/Mute.
  * A narrow mixer has smaller pan knobs and value boxes (ours).
  * The mixer keeps the choice while it runs; it is not saved (ours).
  * The Display Visibility Priority menu (p.694) comes later.

## Live mode (p.723-726)

* **Hearing the Live Mix.** "When you play the audio, the system uses your new
  settings" (p.724).
  * Each track's Live Mix level is a gain on top of what the track plays
    (ours: the guide likens Live mode to an external mixer).
  * Its pan, once moved, takes the place of the track's pans.
  * A move while playing is heard at once.
  * Surround pans and multichannel tracks keep their own placement.
* **Set Live Mix to Automation.** Each track's Live Mix takes the volume and pan
  automation's values at the position.
  * The guide's table says this "permanently removes the existing ... key
    frames between the In and Out points". Its example on p.726, though,
    describes writing the Live Mix into the keyframes, which is Set Live Mix
    as Automation.
  * We keep the keyframes for this item.
* **Set Live Mix as Automation.** On the selected tracks between the marks, the
  volume and pan keyframes give way to the Live Mix values
  (`model::setAutomationTo`).
  * With marks, "keyframes at the In and Out points and ... ramps from the In
    and Out points to the new value": the IN and OUT keep the values they had,
    and the value one frame inside them is the Live Mix.
  * Without marks, each segment holds the value throughout.
  * It is one undo step.

## Track sidebar and Audio Grouping (5.7a2; p.691, p.696-698; `src/ui/audio_groups.cpp`)

* **Show Track Sidebar.** The top bar's ">>" button shows the sidebar at the
  mixer's left ("<<" hides it). The sidebar holds:
  * the Tracks list, highest track at the top as pictured on p.697, with a
    check box for each track. Unchecking a track hides its strip ("choose how
    many tracks to display", p.691);
  * the Groups pane below it.
* **Groups.** A group is a name and a set of audio tracks (`ui::AudioGroup`).
  * Right-click in the Groups pane: New Group from Selected Tracks and
    Disable All Groups (picture on p.697).
  * On a group, also Modify the Group, Duplicate the Group, Delete the Group,
    and Gang (or Ungang) the Group (p.698).
* **The Audio Track Grouping window** (picture on p.698):
  * Group Name;
  * Select Tracks in Group, a text of track numbers and ranges ("1-3, 5"),
    with Apply (the text to the check boxes) and Restore (back to the group
    as it was);
  * a check box for each audio track, and OK and Cancel.
  * Checking a box rewrites the text.
* **Clicking a group (p.696):**
  * selecting adds its tracks to the selection;
  * deselecting removes them, except those in another selected group;
  * Alt/Option-click when selecting replaces the selection with the group;
  * Alt/Option-click when deselecting deselects every audio track.
  * A selected group is shown in bold.
* **Gang the Group** gives its tracks the first free gang letter, A-J
  (p.704). Ungang takes the letter away again. The pane shows the letter.
* **Disable All Groups** (ours, since the guide only names it): while it is on,
  clicking a group does nothing.
* Groups, like gangs and Live Mix, live in the mixer and are not saved (ours).

## Left for later

* Calibrate Hardware Sliders (Auto mode): it needs external controllers
  (p.757).
* The Display Visibility Priority menu (p.694).
* Saving groups with the project.

## Checks

* `tst_audio_mixer`:
  * the loop range rules, looping and its buttons, the command;
  * a segment's own centred pan, its bin file, Remove Pan;
  * Commit and Remove Default Clip Pan with undo, Adjust Pan/Vols;
  * Disable Track Monitoring, Set Display Options;
  * the insert button, Render Effect;
  * Set Live Mix to and as Automation (ramps at the marks; whole segments
    without them).
* `tst_audio_mixer` also covers the sidebar's Tracks list, the track
  number text, the Audio Track Grouping window and the group rules.
* Scenario `16-mixer-clip-pan.json`: Commit Default Clip Pan and Adjust Pan/Vols
  as undo steps.
