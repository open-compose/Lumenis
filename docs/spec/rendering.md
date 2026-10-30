# Rendering video effects (Phase 6.4)

Pages are the FX Guide's. "Rendering is the process of calculating and storing video information for the
finished look of an effect so that the effect always plays in real time at full quality. Media Composer
stores a rendered effect and its media file as a precomputed master clip (often referred to as a
precompute)" (p.57).

ROADMAP 6.4 is in four parts: 6.4a, Render Effect and Clear Renders (this page); 6.4b, partial render,
Render Ranges, the playback performance indicators and Render On-the-Fly; 6.4c, ExpertRender; 6.4d, the
Render settings, background rendering and precompute management.

## 6.4a: Render Effect and Clear Renders

Code: the model in `src/model/effect_render.*` (`RenderTarget`, `renderTargets`, `renderTargetAt`,
`renderKey`, `isRendered`, `setRender`, `clearRenders`) and `EffectRender` in `sequence.h`; the pictures
in `src/media/render.*` (`renderEffect`, `precomputeName`) through `media::renderedVideoClip`
(`managed.h`); playing renders in `media::composeFrame` (`Composition::rendered`); the commands and
dialogs in `MainWindow::renderEffects`, `clearRenders` and `src/ui/render_dialogs.*`. Tests:
`tst_effect_render`, `tst_render`, `tst_render_ui`.

* What is rendered. A segment effect or a transition on a video track. "If the top track covers the
  entire duration of the tracks below, Media Composer renders the composite result of all tracks into
  the top track" (p.57). Ours: every render holds its track and all the video tracks below it over the
  effect's frames, so a render in date shows in place of all of them. The Composer, composing a frame,
  starts from the topmost track whose effect has a render in date there and adds only the tracks above
  it.
* The precompute. A master clip of kind Rendered Effect, named "<sequence> - <effect>" (ours, for "The
  clips are named with the sequence name and effect type", p.155), with one V1 file of managed media:
  OP-Atom MXF in "Avid MediaFiles/MXF/<n>/" on the chosen drive. It is in the Render tab's Video
  Resolution, at the project's raster whatever size the monitors show. Its pictures are computed at the
  Render tab's Effects Processing: 8-bit, 16-bit, 32-bit, or Automatic, which "Effects render at 16-bit for
  10-bit media and at 8-bit for 8-bit media" (p.152); ours, by the depth of the render resolution. The
  precompute clips are kept with the effect in the sequence (the segment's `render`, or
  `transitionRender` for its transition). Showing it in bins with "Show reference clips" comes with 6.4d.
* In date or not. A render keeps a key of what it was made from (ours): the effect and its parameters,
  and the clips, source frames, lengths, effects and transitions on its track and those below over its
  frames, relative to its start. Moving the whole picture keeps the key; changing a parameter, trimming
  or editing under the effect changes it, and the effect plays unrendered (computed) until rendered
  again. Tracks above do not matter. A precompute whose media is offline is computed too: "If this drive
  is offline, rendered effects in the Timeline appear to be unrendered" (p.60).
* Render at Position and Render In/Out (Timeline > Render, p.58-59). The effects on the selected video
  tracks at the Record monitor's position, or between its marks ("Mark an IN point before the start of
  the first effect ... and then mark an OUT point after the last"). Without marks, Render In/Out says to
  mark them. Effects already rendered are left out; "To rerender a single effect ... Shift-click the
  Render Effect button" and Shift with Render at Position (p.61) renders them again.
* The Render Effect dialog box (p.58-59). "Click the Drive button, and select a drive for the rendered
  media"; "In the Processing field, select the processing method"; "Greyed-out options are not
  available". Ours: a Drive menu of the media drives (the Render tab's Video Drive, or the last one
  chosen); Processing offers Application, with Background Process and Distributed Processing dimmed until
  6.4d; the number of effects is shown. "To prevent the Render Effects dialog box from opening, press
  and hold the Alt key ... Media Composer uses the last drive selected" (p.58).
* The Effect Editor's Render Effect button (p.84) renders at the position, with Shift and Alt as above.
* Rendering shows a progress dialog; all the renders are kept with their effects as one undo step
  (Render Effect). Interrupting it is 6.4b's partial render, below. The "not enough room on
  the drive" message (Stop, Continue, p.59) is not shown: the write fails with the disk's error instead.
* Clear Renders In/Out and at Position (p.60-61): "Clearing rendered effects does not delete the effects
  or their precomputes, but it disassociates the precomputes from the sequence so that the effects appear
  unrendered." The Clear Renders dialog box's Skip Motion Effects ("Retains all motion effects") and Skip
  Unknown Effects ("Retains all effects with blank effect icons"), both selected until changed (ours, from
  p.152's "Deselect Skip Motion Effects and Skip Unknown Effects" to purge every render). Unknown effects
  are those not in the Effect Palette; motion effects, for now, the imports' Motion Effect (6.5 adds
  Timewarps). Without marks, Clear Renders In/Out takes the whole sequence (ours). "Select Edit > Undo
  Clear Renders" (p.61): one undo step.
* The dots (p.36): "Non-real-time effects have no dot in the Effect Palette. In the Timeline, the effect
  icon shows a blue dot until you render the effect"; "All colored dots disappear in the Timeline when you
  render effects". A render in date hides the effect icon's dot.

## 6.4b: partial render, Render Ranges, playback indicators, Render On-the-Fly

Code: `EffectRender::clips`, `model::renderedFrames`, `renderedClipAt`, `isPartlyRendered`,
`isRenderedAt`; `media::renderEffect`'s `from` and `keepOnCancel` (and `renderedVideoClip`'s); the
Composer's `PlaybackRange`, `playbackDifficulties`; `Timeline::RenderRanges`, `setPlaybackIndicators`;
`Composition::effects`. Tests: `tst_effect_render`, `tst_render`, `tst_render_ui`.

* Partial render (p.150): "If you interrupt a render that is not complete, Media Composer lets you save
  the completed portion of the render. You can finish the render later by resubmitting the effect for
  rendering. Media Composer then renders only the part of the effect that has not rendered previously
  and saves that part as a separate precompute." Ctrl+period or Cancel interrupts; "Click Keep to save the
  material already rendered. Click Discard to end the render without saving" (ours: Discard deletes the
  precomputes this render made; earlier parts stay). A render holds its precompute clips end to end from
  the effect's first frame. Rendering again finishes a partial render in date from where it stopped;
  Shift renders it all again. "If playback begins at a rendered frame ... You see the rendered portion"
  (p.150): ours, each frame plays its render where it has one and is computed where it has not.
* Render Ranges, in the Timeline Fast menu (p.150): None; Partial Only, "a red line on the portion of any
  partially rendered effect that is currently unrendered ... This is the default option"; All, also "on
  all completely unrendered effects". Ours: the line runs along the foot of the effect's track; the
  choice is kept with the Timeline's display (and Timeline views).
* The playback performance indicators (p.136-137): "colored bars appear in the timecode track of the
  Timeline when playback ends". Red "Marks the ranges where Media Composer was unable to display frames":
  ours, the frames the Composer passed over while playing the Record monitor. Yellow "Marks the ranges
  that caused difficulty ... because of processing power. No frames were dropped": ours, a frame shown
  more than half a frame period late. Blue (disk speed) is not told apart from yellow: ours cannot know
  which held a frame up. They show while the Timeline setting Highlight Suggested Render Areas After
  Playback is on ("This option is selected by default", p.137), until the next play.
* Composer > Render On-the-Fly (p.56-57): "When Render On-the-Fly is deselected, Media Composer shows the
  monitored frame as if no effects are applied to it"; it "only controls how Media Composer displays a
  frame when the position indicator is stationary". Ours: on until turned off (the guide gives no
  default), not kept between sessions; off, a stationary frame leaves out segment effects and shows
  transitions as cuts, while renders in date still show; playback is unchanged. Dragging the position
  indicator does not yet turn it off for the drag.

## 6.4c: ExpertRender

Code: `model::expertRender` (`ExpertRender`, `ExpertMode`); `ExpertRenderDialog` in `render_dialogs.*`;
`MainWindow::expertRender`, `renderExpertSelection`, `renderTargetsNow`; the Timeline's
`setExpertSelection`, `toggleExpertTarget`, `cancelExpertSelection`. Tests: `tst_effect_render`,
`tst_render_ui`.

* "ExpertRender determines which effects need to be rendered to achieve successful playback of a
  sequence" (p.140). Timeline > Render > ExpertRender In/Out (the Editing Guide's menu, p.926). The FX
  Guide also names ExpertRender at Position (p.142), which that menu does not show; it is left out
  until a guide's menu shows it.
* The analysis (ours, for "ExpertRender attempts to generate a minimum set of effects to render", p.141).
  The marked frames of the selected video tracks are looked at, with every video track at or below the
  topmost selected one ("Media Composer considers tracks V1 and V2 when determining playback
  possibilities but highlights effects to render only from tracks V3 and V4", p.146). A frame cannot
  play in real time when an unrendered effect there is not real-time (no green dot in the Effect
  Palette, p.35-36) or when more than two unrendered effects stack there (ours: our CPU compositor's
  limit). Each such frame is helped by the unrendered effect on the topmost selected track there,
  whose render takes in the tracks below it (p.141).
* The Expert Render dialog box (p.142-144): it "indicates how many of the effects require rendering"
  and its message changes with the choice; Drive and Processing as in the Render Effect dialog; Cancel,
  Modify Selection and OK. "Render recommended ranges" (only after a playback attempt left red
  indicators; only frames in them), "Render entire selection" and "Prepare effects for Digital Cut"
  (every unrendered effect, as "On systems without Avid input/output hardware, you must render all
  effects before performing a digital cut", p.141). Ours: the three choices are offered whenever they
  apply, not only after a playback attempt.
* Highlighting and Modify Selection (p.142, p.145): the chosen effects are highlighted in the Timeline;
  Cancel removes the highlighting; Modify Selection keeps it, "Shift+click effects in the Timeline to add
  them or remove them", and the Effect Editor's Render Effect button renders the set (through the Render
  Effect dialog; Alt skips it). "Click in the timecode track" cancels.
