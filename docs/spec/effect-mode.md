# Effect mode and the Effect Preview monitor (Phase 6.2c)

Pages are the FX Guide's unless marked "Editing Guide".

Code: `Timeline::setEffectMode`/`isEffectMode` and the mode buttons (`src/ui/timeline.*`), the Effect
Preview in the Composer (`Composer::EffectPreview`, `setEffectPreview`, the toolbar and picture zoom in
`src/ui/composer.*`), which the Effect Editor drives (`EffectEditor::syncPreview`,
`effect_editor_keyframes.cpp`), and MainWindow opening the Effect Editor. Tests: `tst_effect_mode`,
`tst_effects` (`mainWindowTools`).

## Entering and leaving

* "Click the Effect Mode button. Media Composer opens the Effect Editor and the Record monitor becomes
  the Effect Preview monitor. If the position indicator in the Timeline is on an effect, the controls
  for adjusting that effect display in the Effect Editor" (p.49). Command `fx.effect_mode`, on the
  Command Palette's FX tab (ours) and the Timeline's top toolbar.
* "To enter Source/Record mode from another mode: Click the Source/Record Mode button" (Editing Guide
  p.457); it also clears trim selections (p.652). Command `edit.source_record_mode`, on the Command
  Palette's More tab (ours) and the Timeline's top toolbar beside the Trim Mode and Effect Mode buttons
  ("Source/Record Mode button (left) and Effect Mode button (right)", Editing Guide p.652). With Alt it
  switches between the single Record monitor and the Source/Record monitors (Editing Guide p.381).
* The mode buttons show which mode is on. Trim mode (and slipping) leaves Effect mode and Effect mode
  leaves Trim mode. Ours: a second click on Effect Mode leaves it too; the guide says only that in Color
  Correction mode it cannot close the Effect Editor (p.684).
* In Effect mode, Delete with no segments selected removes the effect at the position on the selected
  tracks: "In Effect mode, select the effect and press Delete" (p.47).

## The Effect Preview monitor (p.49-50, p.75-82)

* "The Effect Preview monitor displays the selected effect in the sequence where the position indicator
  is located in the Timeline. The effect's position bar ... represents only the selected effect, not the
  entire sequence" (p.75): clicking and dragging the bar stays within the effect (p.50).
* Its keyframe indicators are the effect track's (p.102-103): one per time, pink when every keyframe at
  that time is selected (p.76, p.103). Clicking one selects them all and, with Set Position To
  Keyframe, moves the position there; Ctrl+click adds or takes away.
* "The two boxes above the Effect Preview monitor display the length of the effect in seconds and
  frames and the current position in the effect's position bar ... These boxes replace the name of the
  sequence" (p.77-78). Ours: no field numbers (the guide's ".1" and ".2" for two-field media wait for
  field stepping).
* Away from an effect, or outside Effect mode, the Record monitor is itself again. Ours: the guide
  shows a black frame there (p.76).

## The toolbar (p.76-77)

* Under the monitors in place of the Record monitor's buttons: Reduce, Enlarge, Add Keyframe, Step
  Backward One Frame, Step Forward One Frame, Play, Play Preview, Play Loop, Dual Split, Go to Previous
  Event, Go to Next Event. The pictures are ours.
* Reduce and Enlarge size the picture (ours: in steps of two, from a quarter to eight times the
  monitor); "You can also Ctrl+click ... in the Effect Preview monitor to enlarge the image" (p.77), and
  "Press and hold Ctrl+Alt ... Drag the hand in any direction to reposition the image" when enlarged
  (p.82). Leaving the Effect Preview resets them.
* Add Keyframe "Creates a keyframe at the current position" by the default command of the Effect
  Editor, without the Add Keyframe Mode menu (p.105); Alt+click deletes the selected keyframes the
  default command covers (p.106).
* The steps stay within the effect. Play "Plays the effect from the current position in the effect's
  position bar. Click again or press the space bar to stop play" (p.77), to the end of the effect
  (p.55); Play Loop "Plays back the current effect repeatedly in a loop" (p.77) from its start (p.55).
* Go to Previous Event and Go to Next Event go to the effect's keyframes as the Effect Editor's do
  (p.128). The table on p.77 says "Places the position indicator at the first [last] frame or field of
  the sequence"; ours follows p.128 (on the watch list).
* Play Preview ("a wire-frame preview of an unrendered effect") and Dual Split ("showing the image with
  and without the effect applied") are dimmed until the wire frames and the compositor's "without"
  picture come (6.3, 6.4).

## Not yet

* Standard keyframes (p.132-134): the guide does not say which effects still use them ("Most effects
  use advanced keyframes only, while a small number of effects use standard keyframes only", p.94), so
  every effect is an advanced keyframe effect here until one is named.
* Mapping buttons in Effect mode (p.77), the Grid button and Safe Title/Action (p.78-80), effect
  templates (p.52-54), Render On-the-Fly (p.56-57), the Workspace View setting's Link Timeline Mode
  Buttons to a Workspace (Editing Guide p.1229), manipulating effects in the monitor (p.92).
