# Keyframes on effect parameters (Phase 6.2)

Pages are the FX Guide's (2025.12, the same in 2026.8).

Code: `src/model/effect_keyframes.*` (the model, evaluation and the keyframe operations), the
compositor's use of them in `src/media/compositor.cpp`, Fixed keyframes through trims in
`src/model/sequence.cpp`, the Effect Editor's keyframe tracks and graphs
(`src/ui/effect_editor_keyframes.cpp`, `KeyframeTrack` in `effect_editor.h`), and the Fade Effect
button, `Timeline::fadeEffects` (`src/ui/timeline.*`). Tests: `tst_effect_keyframes`,
`tst_compositor` (`keyframedLevels`), `tst_effects` (`fadeEffect`), `tst_effect_keyframes_ui`,
`tst_settings_window` (the Effect Editor setting), scenario `26-fixed-keyframes-trim.json`.

6.2a was the model, evaluation and the Fade Effect button; 6.2b1 the keyframe operations, the Effect
Editor's keyframe tracks, graphs and menus, and trims keeping Fixed keyframes in place; 6.2b2 the graph
views, copying and pasting, Bézier handles and the Effect Editor setting. Standard keyframes (p.132-134)
live in the Effect Preview monitor's position bar and come with Effect mode (6.2c).

## The model

* Each keyframeable parameter of an effect can have keyframes of its own, the "advanced keyframes"
  (p.94). They are kept in `model::Effect::parameters` under `_keyframes`, keyed by
  "Category/Name"; a parameter without keyframes has its one stored value, or its default, for the
  whole effect. No parameter category is called `_keyframes`.
* A keyframe has a position, a value (a number or a colour "#rrggbb") and, for Bézier, two
  direction handles. Each parameter has one interpolation, Elastic or Fixed timing, and Extrapolate
  or Hold.
* "Effect parameters are keyframeable unless otherwise noted" (p.491).
  `resources/effect_parameters.json` marks the parameters the Effects Reference says are not
  (`"keyframeable": false`, 177 of them, p.491-581). Two notes are left unmarked because it is unclear
  which parameters they cover: Paint's and FluidMotion's Mode parameters (p.537, p.567). SubCap's
  Global Properties dialog (p.504) is marked under SubCap's "SubCap effect parameters are not
  keyframeable" (p.501).
* Ours: only sliders and colours take keyframes for now; flags and menus keep one value.

## Time (`model::EffectSpan`)

* A segment effect's frame f of n is at time f / (n - 1): its first frame is 0 and its last 1.
* A transition's frame f of n is at its progress, (f + 1) / (n + 1), as the compositor blends it
  (docs/spec/compositor.md), so Level keyed 0 and 100 at its ends is the same as no keyframes (ours).
* Fixed keyframes are kept in frames and turned into times through the span; switching between Fixed
  and Elastic keeps every keyframe at its time.

## Interpolation (p.120-122)

* Shelf: the value "does not change between keyframes and then changes abruptly to the new value.
  Some applications call this Hold" (p.120).
* Linear: a straight change from one keyframe to the next (p.121). New keyframes are Linear.
* Spline: a smooth curve through every keyframe (p.121). Ours: a cubic Hermite curve, its tangents
  flat at the first and last keyframes, so two keyframes ease in and out.
* Bézier: smooth like Spline, with handles that shape the curve on each side of a keyframe (p.122).
  Ours: a handle left at zero follows the Spline's tangent, a third of the span long; the curve is
  solved for time by bisection.
* Colours change channel by channel.

## Elastic and Fixed (p.124-125)

* "When you use the Elastic option, Media Composer scales the keyframes to fit the new duration ...
  Elastic is the default behavior for a parameter" (p.124). Elastic positions are fractions of the
  effect: 0 its first frame, 1 its last.
* "When you use the Fixed option, Media Composer leaves the existing duration of your keyframe
  animation intact ... Fixed keyframes let you 'pin' a parameter change to a particular timecode"
  (p.124). Fixed positions are frames from the effect's first frame. Trims that add or remove frames
  at a segment's head (roller trims and slides) move them by the frames added or removed, so they stay
  on the same material (`model::shiftFixedKeyframes`); a slip leaves them (ours: p.124 speaks only of
  trims).
* "All keyframes for a parameter are either elastic or fixed" (p.125).

## Extrapolate and Hold (p.125-127)

* Before the first keyframe and after the last, Hold keeps their values; Extrapolate carries the
  change on (p.125). "The default condition is Hold" (p.127). Ours: Extrapolate continues the first
  or last segment of the curve in a straight line.

## Reverse Keyframes (p.124)

* "Reverse Keyframes does not affect keyframe parameter values. It changes only the position of
  keyframes in time", over "the entire effect duration", for one parameter, a group or the whole
  effect. On "a transition effect that contains a Swap Sources button, Media Composer automatically
  enables the Swap Sources button when you reverse the keyframes on the entire effect" (p.124).
  Ours: reversing it again turns Swap Sources off. `model::reverseKeyframes`. Reverse Animation,
  its standard-keyframe form (p.124), is the same command.

## Moving, aligning and removing (p.106-116)

* "You cannot move a keyframe past another keyframe" (p.114): a moved keyframe stops a frame short of
  its neighbours (`model::moveKeyframe`). Keyframes can be moved outside the effect (p.113).
* Align Keyframes moves the reference keyframe (the one selected last) to the position; other
  selected keyframes keep their distance from it and deselected ones stay (p.114-116). "Align
  Keyframes does not move the reference keyframe beyond an intervening keyframe ... the reference
  keyframe moves to within one frame or field of it" (p.115). Ours: the selected keyframes move as a
  group, so the nearest deselected keyframe stops them all. With every keyframe selected it is a slip.
* Slip Keyframes moves every keyframe of the parameter so that the reference keyframe is at the
  position (p.116).
* Remove Redundant Keyframes (p.106-107): with Linear, a keyframe on the straight line between its
  neighbours; with Shelf, one with its predecessor's value; with Spline and Bézier (where "Media
  Composer might leave some keyframes that could be removed", p.107), only one in a flat run without
  handles. The first and last stay.

## The Effect Editor's keyframe tracks (p.94-105)

* The Show/Hide Keyframe Graphs button "in the bottom right corner of the Effect Editor" shows them;
  "Keyframe graphs are hidden when the Effect Editor first opens" (p.96).
* "Effect track (gray), parameter group tracks (darker blue), parameter tracks (lighter blue)"
  (p.95), each beside its row: the effect track by the top, a group track by each category's opener,
  a parameter track by each parameter. Parameters that are not keyframeable have a plain dark track
  in which the position can be dragged but no keyframe added (p.96); so does a group with none.
  Ours: the tracks take 45% of the editor's width, the same for every row.
* A slider's track has a triangular opener for its keyframe graph (p.96-97): the curve across the
  effect, its height the parameter's range, its background "very dark grey" and darker while its
  parameter is the active one (p.97). Ours: colours have tracks but no graphs.
* Keyframes are triangles; a Fixed one also has a pin below it (ours: the guide's picture is not
  described). Keyframe indicators in group and effect tracks stand for every keyframe of the group or
  effect at that time and are darker (p.102). Selected keyframes are pink (p.111); a group or effect
  indicator is pink only when all its keyframes are (p.103).
* Selecting (p.111-112): clicking a keyframe selects it alone; Ctrl+click adds it or takes it away.
  Clicking a keyframe that is already selected keeps the selection, so several can be dragged, and
  makes it the reference (ours). With Set Position To Keyframe, on by default (ours), clicking a
  keyframe moves the position to it (p.132). Clicking a track elsewhere moves the position (p.96).
* Dragging (p.112-113): in a track a keyframe moves in time with every selected keyframe; in a graph a
  drag changes the value of the selected keyframes of that parameter by the same amount, within its
  range; Alt+Shift+drag moves in time only and Alt+drag both. A drag is one undo step.
* Delete deletes the selected keyframes (p.105); Edit > Deselect All Keyframes deselects the active
  parameter's keyframes, or with none active every keyframe (p.112).
* Sliders show the value at the position. Changing a keyframed parameter sets its selected keyframes,
  else the keyframe at the position, else adds one there: "You can also add keyframes automatically
  by adjusting a parameter when you place the position indicator in a part of the keyframe graph that
  has no keyframe" (p.103). The active parameter is the control last used, its name in bold (ours for
  "a slider that you clicked is highlighted", p.107).

## The tracks' shortcut menu (p.130)

* Add Keyframe, Delete Keyframe, Add Start and End Keyframes ("at the first and last frames of your
  effect"), Remove Redundant Keyframes, Select All Keyframes, Deselect All Keyframes, Align Keyframes,
  Slip Keyframes, Reverse Keyframes, Shelf/Linear/Spline/Bézier, Fixed/Elastic and
  Extrapolate/Hold, for the track's parameter, its group ("Apply to Group") or the whole effect
  ("Apply to All"), the text shown on the track while the menu is open (p.104).
* The options are checked when every keyframed parameter of the track has them. Ours: an option is
  kept with a parameter's keyframes, so it applies to parameters that have keyframes.
* Reverse Keyframes on the effect track reverses the whole effect, which also swaps a transition's
  sources (p.124).

## The Add Keyframe button and Go to Previous/Next Event (p.103-111, p.128, p.131-132)

* The bottom row's Go to Previous Event and Go to Next Event move the position one keyframe left or
  right and select the keyframes there; with none, the position does not move (p.128).
* Add Keyframe opens the Add Keyframe Mode menu with the default command checked, and active so that a
  second click runs it (p.104-105); Alt+click opens the Delete Keyframe Mode menu, which deletes the
  selected keyframes of the parameters the command covers (p.105-106). The command chosen becomes the
  default of both (p.105, p.107). Without Show Add Keyframe Mode Menu the button runs the default at
  once (p.132).
* The commands (p.107-111): Active Parameter; Active Group (a parameter in a subcategory takes only
  its subcategory); Open Groups ("whether or not the group is enabled"); Enabled Groups (ours: groups
  have no enable switches yet, so all); Open Graphs; All Parameters. Ours: the default is Add to Open
  Groups, the guide's own example (p.107).
* New keyframes take the parameter's value at the position and are selected (p.104).
* Alt+click on the Add Keyframe button opens the Delete menu; Delete and Edit > Delete with the Effect
  Editor active delete the selected keyframes, and Go to Previous/Next Event there go to its keyframes.

## Graph views (p.97-101)

* The horizontal scale bar "expands or contracts the view in the keyframe graphs centered around the
  position indicator" (p.99): every track and graph shows the same time; the position keeps its place.
  Ours: the bar runs from the whole effect to a thirty-second of it.
* The scroll thumbwheel moves the view (p.101). Ours: a scroll bar, in thousandths of the effect,
  that can go past either end to keyframes outside the effect.
* Each open graph has a vertical scale bar to its left: "Drag the vertical scale bar down (to zoom in)
  or up (to zoom out)"; "You can zoom out beyond the limits of the parameter value range" (p.99).
  Ours: around the middle of the values shown, from half to double per quarter of the bar.
* Zoom to Curve Height (its button above the scale bar, or the shortcut menu) fits the curve over the
  time shown, with the direction handles of a selected Bézier keyframe (p.100). Ours: a tenth of the
  height spare at each edge, and a band one unit high for a flat curve.
* "Alt+click ... the keyframe graph. The pointer changes to a hand. Drag the hand to reposition the
  view" (p.101): Alt+drag away from keyframes scrolls the graph in time and value.
* Dragging a graph's bottom border changes its height (p.98).
* Reset All Graph Views (p.101) shows the whole effect and every graph's full range again.

## Copying and pasting (p.116-119)

* With the Effect Editor active, Edit > Copy (Ctrl+C) copies the selected keyframes and Edit > Paste
  (Ctrl+V) pastes them; with none selected the Timeline's Copy and Paste work as before.
* One keyframe copied: "Media Composer pastes the parameters from the keyframe that you copy to the
  keyframes that you select, or creates a new keyframe using the parameters that you copy at the
  location of the position indicator" (p.117).
* Several: "pastes the leftmost keyframe at the location that you select and pastes the other keyframes
  in locations that preserve the original offset" (p.117); the location is the position. Ours: the
  offset is kept in frames, so pasting into a longer or shorter effect keeps the timing.
* Copy All Values at Position (the tracks' shortcut menu, Ctrl+Alt+C in a graph) takes every
  keyframeable parameter's value at the position, "regardless of whether a keyframe already exists",
  and Paste gives every parameter a keyframe with that value (p.118-119).
* Pasting into another effect pastes the parameters it has. One undo step.

## Bézier direction handles (p.122-123)

* A selected keyframe of a Bézier parameter shows a direction bar with two handles in its open graph
  (p.123). Handles not set show where the curve's default tangents are.
* Dragging a handle: Symmetrical "changes the length and rotation of both direction handles
  symmetrically"; Asymmetrical changes the dragged handle's length and turns both; Independent
  changes only the dragged one (p.122-123). "The default adjustment method is symmetrical"; Alt+click
  on a handle moves to the next method, which stays until changed (p.123). Ours: lengths and turns are
  measured on screen. The way in stays at or before its keyframe in time and the way out at or after.
* Handles are kept in the units of the keyframe's position (frames for Fixed keyframes) and keep the
  curve's shape when switching between Fixed and Elastic (`model::setHandles`,
  `model::effectiveHandles`).

## The Effect Editor setting (Editing Guide p.1171-1172; FX Guide p.131-132)

* A User setting, "Effect Editor": Indent Rows, Large Text ("12-point size"), Set Position To
  Keyframe, Update Position While Playing ("deselected by default"), Show Add Keyframe Mode Menu, and
  the Add Keyframe button commands. Thumbwheels stay dimmed: the guide describes them only in the Help.
* **Real Time Update** (on by default): "Media Composer updates the rendered effect image in real time.
  Because the update can be slow for complex effects, you have the option to deselect real-time updating"
  (p.1171). On, dragging a slider previews the effect as it goes in the monitors and the Effect Preview
  (`EffectEditor::previewValue`, shown through `pictureOverride` at the outermost level), as a handle drag
  does; the release stores it as one undo step. Off, the picture changes on the release only.
* The Effect Editor shortcut menu (right-click outside the tracks) has the same options, mirrored:
  "The Effect Editor settings and the commands in the Effect Editor shortcut menu are similar but not
  identical" (p.1171). Ours: changing one in the menu changes the setting.
* The Editing Guide's list of Add Keyframe commands (p.1172) leaves out Enabled Groups, which the FX
  Guide's has (p.109-110, p.132); ours has all six.
* Ours: Indent Rows is on by default; the guide gives no default.

## Where the compositor reads them

* At the effect's time (above) for the frame shown.
* The dissolve family: the Foreground Level, when keyed, is how far through the blend the
  transition is; the Background Color can be keyed.
* A segment effect with a keyed Foreground Level, and Superimpose, fade their layer by the Level
  (p.458). Superimpose is 50 until a level is set (ours).

## The Fade Effect button (p.46-47)

* "You can use the Fade Effect button to create basic fades for segment effects. The Fade Effect
  feature creates keyframes for the effect automatically" (p.46). It is on the Command Palette's
  FX tab and can be mapped (command `fx.fade_effect`).
* Which effects: "To fade a single segment effect, move the position indicator to the segment. To
  fade multiple segment effects, click the Segment mode button ... and then Shift+click the
  segments" (p.46). Segments on locked tracks are left alone.
* "The Fade Effect dialog box opens. Type the number of frames to fade up and fade down, and click
  OK" (p.47). The Foreground Level is keyed 0 to 100 over the first frames typed and 100 to 0 over
  the last; 0 leaves that end out, and both 0 removes the Level's keyframes (ours). One undo step.
