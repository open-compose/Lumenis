# Keyframes on effect parameters (Phase 6.2)

Pages are the FX Guide's (2025.12, the same in 2026.8).

Code: `src/model/effect_keyframes.*` (the model and evaluation), the compositor's use of them in
`src/media/compositor.cpp`, and the Fade Effect button, `Timeline::fadeEffects` (`src/ui/timeline.*`).
Tests: `tst_effect_keyframes`, `tst_compositor` (`keyframedLevels`), `tst_effects` (`fadeEffect`).

6.2a, this page, is the model, evaluation and the Fade Effect button. 6.2b is the Effect Editor's
keyframe tracks and graphs, and trims keeping Fixed keyframes in place.

## The model

* Each keyframeable parameter of an effect can have keyframes of its own, the "advanced keyframes"
  (p.94). They are kept in `model::Effect::parameters` under `_keyframes`, keyed by
  "Category/Name"; a parameter without keyframes has its one stored value, or its default, for the
  whole effect. No parameter category is called `_keyframes`.
* A keyframe has a position, a value (a number or a colour "#rrggbb") and, for Bézier, two
  direction handles. Each parameter has one interpolation, Elastic or Fixed timing, and Extrapolate
  or Hold.

## Interpolation (p.120-122)

* Shelf: the value "does not change between keyframes and then changes abruptly to the new value.
  Some applications call this Hold" (p.120).
* Linear: a straight change from one keyframe to the next (p.121). New keyframes are Linear.
* Spline: a smooth curve through every keyframe (p.121). Ours: a cubic Hermite curve, its tangents
  flat at the first and last keyframes, so two keyframes ease in and out.
* Bézier: smooth like Spline, with handles that shape the curve on each side of a keyframe (p.122).
  Ours: a handle left at zero follows the Spline's tangent, a third of the span long; the curve is
  solved for time by bisection.
* Colours change channel by channel. Flags are not keyframed yet (6.2b).

## Elastic and Fixed (p.124-125)

* "When you use the Elastic option, Media Composer scales the keyframes to fit the new duration ...
  Elastic is the default behavior for a parameter" (p.124). Elastic positions are fractions of the
  effect: 0 its first frame, 1 its last.
* "When you use the Fixed option, Media Composer leaves the existing duration of your keyframe
  animation intact ... Fixed keyframes let you 'pin' a parameter change to a particular timecode"
  (p.124). Fixed positions are frames from the effect's first frame. Trims that add or remove frames
  at the head move them by the frames added or removed (6.2b).
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
  Ours: reversing it again turns Swap Sources off. `model::reverseKeyframes`; the Effect Editor's
  menus come with 6.2b. Reverse Animation, its standard-keyframe form (p.124), is the same command.

## Where the compositor reads them

* A segment effect at a frame is at (frames into the segment) / (its length - 1).
* A transition's keyframes are placed in its progress, (frames into it + 1) / (its length + 1)
  (docs/spec/compositor.md), so Level keyed 0 at the start and 100 at the end is the same as no
  keyframes (ours).
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
