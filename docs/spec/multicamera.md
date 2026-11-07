# MultiCamera editing: group clips (8.1a)

Editing Guide ch.32, "MultiCamera Editing" (p.1123-1135). This page covers 8.1a: making group clips,
showing them in the Source monitor, editing them into sequences and switching angles. MultiCamera mode,
the Group menu, multigroups and the rest follow in 8.1b and 8.1c (docs/ROADMAP.md).

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
  * Waveform Analysis: dimmed until 8.1a2.
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
  example) reaches the group when the group is made again. Edit Group and Update Group (p.1125-1126)
  come in 8.1a2.

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

## Tests

* `tst_group_clip`: grouping, offsets, names, angle clips and edits.
* `tst_multicam_ui`: the dialog, and grouping, the split views and switching in the application.
* Scenario `43-group-clip.json`.
