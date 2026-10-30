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
  Match Frame. Fit to Fill making a motion effect clip and its Alt "unrendered" choice (p.170) come with
  6.5b, when such effects render in the Timeline. Their sound still plays at normal speed.
