# MultiCamera editing: group clips and multigroups (8.1a, 8.1b)

Editing Guide ch.32, "MultiCamera Editing" (p.1123-1135). This page covers 8.1a and 8.1b: making group clips,
showing them in the Source monitor, editing them into sequences and switching angles. MultiCamera mode,
the Group menu and multigroups are in 8.1b; the rest follows in 8.1c (docs/ROADMAP.md).

## Group clips

* Clip > Group Clips (p.1124) groups the bin's selected master clips, subclips and audio clips, in the
  bin's order. Its dialog lists the guide's options:
  * Film TC/Sound TC: dimmed. The guide dims it outside 24p and 25p projects. Ours is dimmed in every
    project, because film timecode is not built yet.
  * Inpoints and Outpoints: "syncing according to IN points [OUT points] set in each clip". Ours: a
    clip's mark is its Source monitor mark when the clip has been loaded there, else the bin's Mark IN
    or Mark OUT column.
  * Source Timecode: the Start column.
  * Auxiliary TC1-TC5: chosen from the menu beside the option.
  * INK Number and Auxiliary Ink: "appears only with 24p and 25p projects". Ours: shown dimmed there
    (no ink numbers yet) and hidden elsewhere.
  * Waveform Analysis: "Media Composer performs an analysis of the waveforms of the selected clips"
    (below, "Syncing by sound").
  * Ours: Inpoints is chosen when the dialog opens.
* The group's name is the first clip's followed by ".Grp.n", where n counts the groups of that name
  in the bin (p.1124).
* "Grouping creates a separate group clip ... from the IN point to the OUT point of the longest clip"
  (p.1123). Ours reads this as a group that covers every clip once synced: it runs from the earliest
  clip's first frame to the latest clip's last.
* Ours, for the group's timecode: the first clip's timecode at the group's first frame.
* Ours, for the group's tracks: V1 when any angle has picture, plus as many audio tracks as the angle
  with the most. The guide's Group menu (8.1b) patches any angle's tracks.
* All clips must share one edit rate (ours; mixed rates in groups are not built yet).
* `model::groupClips` (src/model/group_clip.*) makes the group. It is a bin item of kind Group, and
  `composition["group"]` holds:
  * each angle: a copy of its clip, the bin and clip it came from, and its offset (group frame g is
    the clip's frame g + offset);
  * the selected angle.
* Ours: angles are copies taken when the group is made. A later change to a clip (relinking it, for
  example) reaches the group when the group is updated or made again.

## Playing an angle

* `model::groupAngleClip` gives an angle as a clip of the group's frames:
  * its link's `first` is moved by the offset;
  * its duration is the group's;
  * `composition["groupRange"]` gives the frames where the angle has material.
* Outside that range the angle is black (`media::FrameCache::frame`) and silent (`AudioMix::samples`
  plays silence before a file's first sample).
* `model::clipForSegment` plays a group segment through the angle, so the compositor, playback,
  renders and the Timeline's frames all follow it.

## The Source monitor

* Full-Monitor display (p.1127) shows the selected angle, and plays its picture and sound.
* Quad Split and Nine Split Source views (p.1127-1129) are the MCam tab's Quad Split and Nine Split
  buttons (`Composer::setGroupSplit`).
  * The same button again goes back to Full-Monitor display.
  * MainWindow's picture resolver draws the shown bank's angles in two or three rows
    (`MainWindow::groupSplitImage`).
  * Ours: the selected angle is outlined in yellow.
* Swap Cam Bank steps to the next bank of four or nine angles, then back to the first (p.1128).
* "Only the first four clips are shown in the Quad Split display and only the first nine clips are
  shown in the Nine Split display" (p.1131). Ours: further banks are shown with Swap Cam Bank.
* Multi-angle View menus, clicking an angle, the numeric keypad, clip names and the Group menu are 8.1b.

## Editing and switching

* Segments edited from a group keep the group as their clip and the angle they show
  (`Segment::groupAngle`, "groupAngle" in the sequence's JSON). Their name is the angle's.
* The Timeline adds " (G)" to clip names (p.1131).
* Previous In Group and Next In Group (p.1131) work on the active monitor:
  * In the Source monitor they change the group's selected angle, in its bin too ("you also switch
    the frame representing the group in the bin").
  * In the Record monitor they change the group segment at the position, as an undo step
    (`Timeline::stepGroupAngle`). Ours: the segment on the highest selected video track, else on the
    highest video track with one.
  * Ours: only video segments switch. Audio Follow Video comes with the Group menu (8.1b).
  * The angles cycle through the whole group.
* "These buttons are mapped by default to the Up Arrow and Down Arrow keys" (p.1131). Those keys are
  also Move Clip Up and Move Clip Down (p.613). Ours: with no segments selected, they switch angles
  when the active monitor shows a group clip (Source) or a group segment (Record); otherwise they move
  clips (`Timeline::setGroupArrowHook`).

## Syncing by sound (8.1a2)

* `media::soundLag` (src/media/waveform_sync.*), ours: each sound's loudness in 1 ms steps, less its
  average, is cross-correlated through an FFT; the best match is the lag. A match must have a
  correlation of at least 0.2, else there is none.
* Only each clip's first audio track (A1) is used ("only the first one is used for syncing", p.1126),
  and the first 20 minutes of it.
* `media::waveformOffset` gives the frames between two clips: frame f of the first sounds with frame
  f + offset of the second.
* Group Clips' Waveform Analysis: each clip's sync point is its frame that sounds with the first
  clip's frame 0 (`GroupOptions::waveform`).
* Waveform Sync (p.1126) is in the Timeline's right-click menu (`Timeline::waveformSync`):
  * It needs a selected video segment and a selected audio segment.
  * Each selected video segment is moved so that its clip's sound lines up with the earliest selected
    audio segment's.
  * Ours: the move is Lift/Overwrite, as one undo step. It is refused when the video would start
    before the sequence.

## Editing groups (8.1a2)

* Clip > Edit Group (p.1125, `MainWindow::editGroup`) lays the group out as a sequence in the
  Record monitor:
  * each angle with picture on a video track of its own;
  * "One representative audio track from each clip" (its A1) on an audio track of its own.
  * Ours: the sequence goes into the group's bin as "<group>.Edit". It records which group it
    edits (`Sequence::editedGroup`, "editsGroup").
  * "Load the MultiCam group clip in the Record monitor", the guide's other way, is not built yet.
* Update Group, from the Record monitor's right-click menu (p.1126, `MainWindow::updateGroup`):
  * "A dialog asks if you want to update the group clip or create a new one."
  * Each clip's place in the sequence becomes its offset (`model::groupFromSequence`), in the order
    the clips first appear on the video tracks and then the audio tracks.
  * Updating keeps the group's name and selected clip.
  * "You cannot make the group clip shorter when editing the group clip": the group keeps at least
    its length.
* Create Group, from the Record monitor's right-click menu (p.1126), makes a new group of the
  Record monitor's sequence in its bin.
  * Ours: the sequence must show at least two clips; segments of group clips are left out.
* Clip > Refresh Sequence > Group Clips (p.478; "Group Edits" on p.1126):
  * Segments keep the angle offsets they were edited with (`Segment::groupOffsets`), so a changed
    group does not move a sequence until it is refreshed.
  * Refreshing takes in the group's current offsets and moves each segment's source frames, so the
    shown angle shows the same material: "Refreshing group edits in a sequence will not change
    selected shots, only the relationship between the selected shot and the other clips in the
    group."
  * It works on the bin's selected sequences when a bin is active, else on the Record monitor's
    sequence, as Motion Adapters/Timewarps does.

## MultiCamera mode (8.1b1)

* Composer > MultiCamera Mode (p.1129) turns it on and off (`Composer::setMultiCameraMode`).
  * "it gangs all clips in the group clip displayed in the Source monitor with the sequence displayed in
    the Record monitor": the Source monitor shows the group segment at the Record monitor's position,
    its angles split, the shown angle outlined (`MainWindow::multiCamPicture`).
  * "The Source monitor controls are disabled": the Record monitor stays the active one.
  * Entered from Full-Monitor display it shows the Quad Split (ours). Quad Split and Nine Split
    still switch the split, and pressing one again shows the shown angle alone ("switch between
    singular and multi-angle playback without exiting MultiCamera mode", p.1130).
  * The group segment is looked for on the video track the Composer setting says (below), then on the
    selected video tracks and the others from the top (`Timeline::groupTracksAt`).
* Choosing an angle (p.1131-1132), in MultiCamera mode or in Quad or Nine Split Source view:
  * "Click a camera angle in the split display during playback to switch to that camera angle."
  * The numeric keypad: in Quad Split "7: Upper left; 8 or 9: Upper right; 4 or 1: Lower left; 2, 3, 5,
    or 6: Lower right"; in Nine Split the keys by their place on the keypad
    (`model::keypadSplitPosition`).
  * "To switch camera banks in Quad Split display ... Click the right mouse button. Press either the 0
    (zero) key or the . (period) key on the numeric keypad."
  * In MultiCamera mode the choice goes to the sequence (`Timeline::setGroupAngle`, an undo step):
    while playing the segment is cut at the position first, so "The camera angles you selected ... are
    recorded as cuts in the Timeline" (p.1129); when stopped the whole segment switches. Only picture
    switches (Audio Follow Video is 8.1b2).
  * In Quad or Nine Split Source view the choice becomes the group's selected angle (ours).
* The Composer settings' MultiCam tab (p.1160-1161):
  * Split Mode Play: Quad or Nine Split, or Fullscreen ("the active camera angle, which displays in
    full-monitor size" while playing). Ours: Quad or Nine Split by default.
  * MultiCam Mode Client Monitor: dimmed, there is no client monitor yet (7.3c).
  * Show Track in Split View When Playing: V1 or the monitored track. Ours: the monitored track by
    default, and for stopped monitors too.

## The Group menu, Multi-angle View menus, Match Frame and committing (8.1b2)

* The Group Menu icon (p.1133) is in the second row of information over the Source monitor ("Select the
  Second Row of Info option ... for the Group Menu icon to be displayed") while a group is shown there
  (`Composer::setGroupMenu`).
  * Ours: the menu lists each angle (its picture; choosing one switches to it) and Audio Follow Video.
    The guide's menu also patches any clip's video or audio tracks to the sequence's tracks; that comes
    with 8.1c.
  * Audio Follow Video: "switch both audio and video for each camera angle ... switches audio in track
    A1 only". The icon is green while it is on. Ours: the cut on A1 is an ordinary edit, not drawn as a
    match frame edit.
* The Multi-angle View menus (p.1134): Ctrl+click a place of the split display for a menu of the
  group's clips and Sequence (`MainWindow::multiAngleMenu`, `Composer::setSplitCell`).
  * The place then shows that clip, or the Record monitor's sequence: the linecut (p.1132).
  * Choosing the split or the bank again clears the choices (ours).
  * Holding Ctrl shows the clips' names in the split.
* Match Frame (p.1134-1135, `Timeline::matchFrame`):
  * From a group segment the group loads into the Source monitor with the segment's angle selected.
  * From a group in the Source monitor the selected angle's clip loads at the same moment.
* Commit Multicam Edits (p.1135), in the bin's right-click menu for sequences with group segments
  (`MainWindow::commitMulticamEdits`, `model::commitMulticamEdits`):
  * The sequence is duplicated and each group segment of the duplicate replaced by its angle's clip
    at the same frames. "The original sequence is unaffected."
  * Ours: a segment whose angle has no material at its first frame stays a group segment.
* Edit Group is in the bin's right-click menu too ("right click and choose Edit Group", p.1125).

## MultiGroup (p.1123, p.1125)

* Clip > MultiGroup (`MainWindow::multiGroup`, `model::multiGroup`) opens the Group Clips dialog titled MultiGroup
  (Waveform hidden) for the selected clips. "Sort the clips by name in the bin" (step 1): the clips are taken in
  name order.
* "Multigrouping is performed on the basis of common source timecode" (p.1123). Ours: the clips fall into takes
  where their Start timecode spans overlap (Auxiliary TC with that option). Each take of two or more clips becomes a
  group synced as the dialog says, named as Group Clips names it; takes of one clip are left out and counted in the
  Console.
* The multigroup clip "from the groups" is a Group item whose composition["multigroup"] is a sequence of the groups'
  segments end to end in timecode order (`isMultigroup`, `multigroupSequence`, `setMultigroupSequence`). Ours: named
  "<first clip>.MGrp.n". The groups and the multigroup go into the bin and the multigroup is selected.
* The bin shows it with the group icon and "a plus sign" (p.1125), drawn in `bin_view.cpp`.
* In the Source monitor it shows the split of the group segment under the position, and Previous/Next In Group,
  the split clicks and the MultiCam keys switch that segment's angle (V1; A1 too with Audio Follow Video;
  `MainWindow::sourceGroup`/`switchMultigroupAngle`).
* Edited into a sequence (`applyEdit`) it brings its group segments, which then work like any group segment.

## Tests

* `tst_group_clip`: grouping (by sound too), offsets, names, angle clips, edits, Edit/Update Group
  and refreshing.
* `tst_waveform_sync`: lags between sounds, at two rates, refusals, and linked files.
* `tst_multicam_ui`: the dialog, grouping, the split views and switching in the application; Waveform
  Analysis, Edit/Update/Create Group, Refresh Sequence and Waveform Sync on movies with sound.
* `tst_group_clip multiGroups` and `tst_multicam_ui multigroupInTheApplication`: takes, names, the multigroup's
  sequence, the Source split and Next In Group on it, and edits from it.
* `tst_multicam_ui multiCameraMode`: the mode, the keypad, clicks, banks and live cuts.
* `tst_multicam_ui multiCameraMode` also covers Audio Follow Video, the Group and Multi-angle View
  menus, Match Frame and Commit Multicam Edits.
* Scenarios `43-group-clip.json`, `44-waveform-sync.json`, `45-multicam-cut.json` and
  `46-audio-follow-video.json`.
