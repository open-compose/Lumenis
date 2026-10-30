# Motion effects (Phase 6.5)

Pages are the FX Guide's unless marked. "Motion effects divide into two main categories, traditional
motion effects (sometimes known as source-side motion effects) and Timewarp effects" (p.165).

ROADMAP 6.5 is in four parts: 6.5a, traditional motion effects (this page); 6.5b, Timewarp; 6.5c, the
Motion Effect Editor; 6.5d, FluidMotion, format conversion and motion adapters.

## 6.5a: traditional motion effects

Code: the model in `src/model/motion_effect.*` (`MotionEffect`, `motionEffectOf`, `freezeFrameClip`,
`motionEffectClip`) and `model::segmentSourceFrame` in `sequence.*`; the dialogs in
`src/ui/motion_effect_dialogs.*`; `MainWindow::freezeFrame`, `motionEffect`, `followMotion`; the
Timeline's icons in `TimelineView::paintEffects`. Tests: `tst_motion_effect`, `tst_motion_effect_ui`.

* The clips. "You create a traditional Freeze Frame, Variable Speed, or Strobe Motion effect by
  generating a new clip with specific motion characteristics from an original clip that you open in the
  Source monitor. You then edit the motion effect clip into your sequence" (p.165). Ours: a bin item of
  kind Motion Effect that names its source clip, the source frame its first frame shows, its speed
  (source frames per frame: 0 for a freeze frame, negative for reverse) and its strobe; picture only. The
  monitors and the compositor show, for each of its frames, the source clip's frame it maps to
  ("Media Composer creates slow-motion effects by duplicating frames in the original media. It creates
  fast-motion effects by eliminating frames", p.168). A motion effect clip made from a motion effect clip
  refers to the first one's source.
* Composer > Freeze Frame > Other (p.166-167, p.561-562): "type a custom duration in the dialog box"; the
  duration as timecode or frames, two seconds until changed (ours), then the last one. "A new clip appears
  in the Source monitor and in the current bin ... The new clip has the original clip name followed by the
  letters FF." The guide's preconfigured durations are not named (the menu's only item is Other, FX
  p.562), so only Other is offered.
* The Motion Effect button (the Command palette's FX tab, Editing Guide p.97; FX p.168-171). With the
  Source monitor active it opens the Motion Effect dialog for the Source monitor's clip between its marks
  (all of it without). "If the Record monitor is active, clicking the Motion Effect button opens the Motion
  Effect Editor" (p.168): that comes with 6.5c; until then the button says so in the Console.
* The Motion Effect dialog (p.168-171, p.562-563): Variable Speed by "Duration n Frames", "Rate n FPS"
  or "% Speed" ("All of the values change to reflect the rate you specify"; negative for reverse), or Fit
  To Fill (the Record monitor's marked duration, dimmed without both marks); Strobe Motion's "Update every
  n frames"; the Target Drive; Create, Create and Render, Cancel. The new clip is named "the original clip
  name followed by the frame rate in parentheses" (p.170), "followed by the word Strobe and the number of
  update frames in parentheses" (p.172), both as "Turbocharge (15FPS) (Strobe: 5)" (p.178); ours, the rate
  to two decimals without trailing zeros. Reverse plays from the OUT back to the IN. Create and Render
  writes its frames as managed media (the Motion Effects tab's Video Drive, the Render tab's resolution:
  "The resolution options are not available in the Motion Effects tab", Editing Guide p.1207), and the clip
  plays its media from then on.
* Two-field options (Two Field Freeze Frames, Render 2-Field Motion Effect Using, Ignore Render Setting):
  "There are no two-field rendering options available in the Motion Effect dialog box in a progressive
  project" (p.174). Our projects and media are progressive, so they are not offered; the clip keeps
  Duplicated Field for when fields come.
* The bin: the new clip goes into the source clip's bin when it is open and can be written, else into an
  open bin the user picks (the active one if it is the only one); it is selected there and loaded into the
  Source monitor.
* The Timeline (p.172, p.175): "Freeze Frame ... No icon appears on a Freeze Frame clip in the Timeline.
  Variable Speed, forward motion ... a motion effect icon and a green dot ... Variable Speed, reverse
  motion [and] Strobe Motion ... a motion effect icon and a blue dot", no dot once rendered; "Traditional
  motion effects display the effect speed in parentheses beside the effect icon" (ours: "(12.5 FPS)"). A
  segment's own Motion Effect (below) shows its speed the same way.
* Playing (p.172). Reverse and strobe clips "play as filler (black image) until you render them"; ours
  computes them, as our compositor can, and keeps the blue dot.
* A segment's own Motion Effect: Fit to Fill's (4.1) and the imports' speed changes (EDL M2, OTIO
  LinearTimeWarp and FreezeFrame) are kept on the segment rather than as a clip; they now play at their
  speed from the segment's source IN (`model::segmentSourceFrame`), in the monitors, the compositor and
  Match Frame. They render in the Timeline as any segment effect does (Render Effect, 6.4), and Clear
  Renders' Skip Motion Effects keeps them. Fit to Fill keeps making such a segment Motion Effect rather
  than a motion effect clip: unrendered, as Alt makes it (p.170), until rendered. Their sound still plays
  at normal speed.

## 6.5b: Timewarp

Code: `src/model/timewarp.*` (`Timewarp`, `timewarpForSegment`, `timewarpPosition`, `blendsFrames`,
`promoteToTimewarp`); `model::segmentSourcePosition` and `applySegmentEffect` in `sequence.*`;
`media::segmentBlend` and the blended `segmentPicture` in `compositor.cpp`. Tests: `tst_motion_effect`,
`tst_motion_effect_ui`.

* "You apply Timewarp effects to segments in a sequence, and then use keyframes and other controls in the
  Motion Effect Editor to control the speed and the position of any image in the effect" (p.174). The
  Timewarp category's effects (p.484-488) are applied from the Effect Palette as any segment effect is;
  "the duration of the segment in the sequence does not change" (p.175), and the Timeline shows no speed.
* The two graphs (p.175, p.187-189), kept as keyframed parameters (docs/spec/effect-keyframes.md):
  "Speed/Percent Speed", the percent speed at each output frame (100 without keyframes), and
  "Position/Input Position", the source frame (from the segment's source IN) at each output frame (each
  output frame's own without keyframes). One is active: "When you render a Timewarp effect, Media
  Composer applies only the information from the active graph" (p.175); the other is kept.
* The Speed graph's frames (ours, for p.181 and p.187): the source position at output frame o is the
  Source Anchor Frame plus the speeds of the frames from the anchor to o, each speed / 100. "The default
  Source Anchor Frame is the first frame or field of the source, and the default point for anchoring the
  Source Anchor Frame is timecode 0" (p.181). The anchor is kept as a source frame and the point as the
  effect's time (0 to 1), so it stays at the same place in the effect when the segment is trimmed.
* Strobe ("Strobe n frames", p.184; p.193-194): each nth output frame is held for n.
* Type (p.185-186): Duplicated Field, Both Fields, Interpolated Field and VTR-Style show the whole source
  frame at or before the position (our media has no fields); Blended Interpolated and Blended VTR "blend,
  or average, pixels from the original frames": the two frames around the position, weighted by how far
  between them it lies. FluidMotion and FluidMotion Draft blend the same way until 6.5d brings motion
  estimation. Duplicated Field until changed (ours).
* The templates (p.203-204, p.484-488), whose graphs the guide shows only in pictures, ours: 0% To 100%
  ramps the speed from 0 to 100; "100% to 0% ... The last frame of the clip is the last frame of the
  motion effect" (anchored at the end); Reverse Motion's Position graph runs from the last source frame to
  the first; Speed Boost "starts out at half speed, reaches approximately double speed at the midpoint, and
  returns to half speed" (50, 200, 50 on a Spline); Speed Bump "starts out at full speed, slows to
  approximately half speed at the midpoint, and returns to full speed" (100, 50, 100); Trim to Fill a
  Position graph from the first source frame to the last, which trimming stretches ("You can adjust the
  speed by entering Trim mode and trimming the effect"); the FluidFilm templates render with FluidMotion
  (p.204). The graphs are set when the effect is applied, for the segment's length then. Pulldown Insertion
  and Removal and the FluidFilm format conversions are 6.5d's.
* Rendering: a Timewarp renders as any segment effect (Render Effect, 6.4); Clear Renders' Skip Motion
  Effects keeps it. The Render setting's Timewarps Render Using applies when it renders (6.5c, below).
* Promote to Timewarp (p.177): "The Motion Effect Editor uses the duration and speed information from the
  traditional motion effect to create a Timewarp effect ... Strobe motion information does not carry
  over." A segment's Motion Effect becomes a Timewarp at its speed; a motion effect clip's segment shows
  the clip's source from the frame it showed, at the clip's speed. The Promote button is the Motion Effect
  Editor's (6.5c).
* Clip > Refresh Sequence > Motion Adapters/Timewarps "Refreshes the sequence so that media with Motion
  Adapters or Timewarps use the current Field Motion attribute value from their source clip" (Editing Guide
  p.478): with no Field Motion attribute in our progressive media it stays dimmed until 6.5d.

## 6.5c: the Motion Effect Editor

Code: `src/ui/motion_effect_editor.*` (`MotionEffectEditor`, `MotionGraph`), the "MotionEffectEditor"
panel and `MainWindow::applyTimewarpRenderTypes`; `model::renderTypeFor` and `Timewarp::ignoreRenderSetting`
in `timewarp.*`. Test: `tst_motion_effect_ui`.

* Opening it: "If the Record monitor is active, clicking the Motion Effect button opens the Motion Effect
  Editor" (p.168); "You do not have to be in Effect mode to use the Motion Effect Editor" (p.178). It follows
  the Record monitor's position: the segment there on the topmost selected video track (all video tracks
  when none is selected; ours).
* "The initial appearance of the Motion Effect Editor depends on whether there is no motion effect, a
  Timewarp effect, or a traditional motion effect in the segment at the position indicator" (p.182): a
  blank message; the Timewarp's controls; or the traditional effect's speed (and strobe) with the
  Promote button ("The Promote button appears ... only when you have the position indicator on a
  traditional motion effect", p.177), which uses `model::promoteToTimewarp` as one undo step.
* The controls (p.182-185): the Speed Graph and Position Graph display buttons ("Clicking a graph display
  button does not activate the graph"); Edit Graph (Speed or Position); Type; Ignore Render Setting; the
  Anchor text box (ours: the source clip's frame number, not timecode) and the Source Anchor Frame button,
  which anchors at the active keyframe where the effect is now, so the picture does not jump; Strobe; the
  Duration display; the Source and Output menus, dimmed until format conversion (6.5d).
* The graphs (p.180, p.187-190): the Speed graph's line is the percent speed, the Position graph's the
  input position over the output frames, with the blue position indicator; the active graph shows its
  keyframes, the active one pink. Click a keyframe to make it active and drag it up or down; with Alt,
  sideways too (p.188). A click elsewhere moves the position. The inactive graph draws what the active
  one makes (p.176). The value scale fits the line (ours; no scale sliders yet).
* Under the active graph (p.184-185): the Fast menu's interpolations (Shelf, Linear, Spline, Bézier;
  p.120), Go to Previous/Next Event ("If there is no keyframe to the left, nothing happens"), Add Keyframe
  ("Alt+click ... to delete a keyframe"), and the active keyframe's Percent Speed (or Input Position) and
  Output Position text boxes (ours: frames, not timecode). A new keyframe keeps the value the graph has
  there.
* The toolbar (p.184): Render Effect (Render at Position), Remove Effect, Match Frame, Play Loop (the
  segment), Play, Trim Mode.
* Ignore Render Setting (p.173, p.183): "The Ignore Render Setting check box appears below the Type menu
  when you have made a rendering selection other than Original Preference ... In this situation, the Type
  menu is dimmed." When a render starts, each Timewarp being rendered takes the Render setting's Timewarps
  Render Using type unless its Ignore Render Setting is on: "If the option is set to a specific motion
  effect type, Media Composer changes all motion effects to that type when it renders them" (p.173);
  "Changes to motion effect types are permanent" (p.174); ours, as an undo step of its own before the
  render. Motion Effects Render Using stays dimmed: it chooses among field types our progressive media
  does not have.
* Not yet: dragging the Timewarp Effect icon to a bin as a template (p.182) comes with effect templates in
  bins (ROADMAP 6.6); the Value and Position scale sliders and Ctrl+Alt hand dragging (p.180-181); the Motion
  Effect button in the Tool palette and the Timeline's top toolbar (p.179).
