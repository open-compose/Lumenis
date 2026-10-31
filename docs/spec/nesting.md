# Nested effects, Collapse and Video Mixdown (ROADMAP 6.6)

FX Guide ch.8, p.216-228, and p.52-55 for effect templates (6.6b). Code: `src/model/nest.*` (`nestOf`,
`setNest`, `ensureNest`, `nestEffect`, `collapseTracks`, `nestLevelLabel`), `Segment::nest` and `nestIn`
in `sequence.h`, the compositor's nest in `segmentPicture`, `Timeline::stepIn`/`stepOut`/`collapse`,
`MainWindow::videoMixdown` with `VideoMixdownDialog` (`mixdown_dialog.*`). Tests: `tst_nest`,
`tst_compositor`, `tst_nest_ui`, scenario `28-collapse.json`.

## 6.6a: nests

* "You can place effect tracks inside one another ... step into a track, perform a series of editing
  operations, and then step out to view and render the effect as one segment on the track" (p.219). Ours: a
  segment can hold a nest, a sequence of its own (kept as its composition JSON in `Segment::nest`), whose
  composite of all its video tracks is the picture the segment's effect works on, in place of the clip.
  The segment's frame n shows the nest's frame `nestIn + n`; trims and Add Edits move `nestIn` with the
  clip's source IN (`Segment::shiftStart`), so the nest stays in place. Filler with an effect (a Submaster)
  can have a nest too, and is not merged into the filler around it. Nests can hold nests, without limit
  ("constrained only by your Avid system's memory").
* Step In and Step Out (p.220): "Media Composer replaces the full sequence in the Timeline with just the
  tracks that make up the nested effect". Ours steps into the segment with an effect under the position
  on the highest selected video track (the highest video track with one when none is selected). "You can
  step into any segment effect in this way": a segment without a nest gets one, its own picture on the
  nest's V1 under the effect (`model::ensureNest`, an undo step "Step In"). The nest is loaded in the Record
  monitor as an item of its own; every change to it (edits, undo) is stored back into its segment, and from
  level to level out to the sequence in its bin. Step Out loads the level out again on the same frame and
  takes the nest out of the Clip Name menu. Loading another sequence leaves the levels. "N2 indicates that
  you are two levels in": the Timeline's Timecode display shows N1, N2 ... before the timecode. The Step In
  and Step Out buttons in the Timeline's top toolbar and the Command Palette's FX tab (ours: the guide does
  not say which tab).
* Nesting an effect inside another: "press and hold the Alt key ... and drag the Matte Key Effect icon ...
  to nest the Color Effect within the Matte Key effect" (p.216); "by Alt-double-clicking in the Effect
  Palette or by Alt-dragging to the Timeline" (p.276). The new effect goes on the segment, and the segment
  as it was (clip, effect, render, nest) goes inside on the nest's V1 (`model::nestEffect`). "If you apply
  the Timewarp effect as part of a nested effect, it must be the innermost effect" (p.177): a Timewarp is
  not nested over another effect.
* Submaster: "a single-track segment effect, available in the Image category ... Media Composer renders the
  composite result of all tracks into the top track" (p.224-225). On filler between Add Edits it shows the
  tracks below it, as an effect on filler does, and renders them (6.4).
* Collapse (p.226-227): "Select all the tracks you want to collapse. The tracks must be adjacent. Mark an IN
  point and an OUT point ... Click the Collapse button", or select segments in segment mode and click it. The
  tracks' material between the marks goes into a Submaster's nest, each track on the nest track of its
  place (the lowest on V1); the Submaster goes on the lowest track and the others become filler there, one
  undo step. Not adjacent, no marks or a locked track: refused with a message. The Collapse button is on
  the Command Palette's FX tab (ours).
* Video Mixdown (p.227-228): "Video Mixdown collapses the selected track and all tracks under it": what the
  Record monitor shows (its monitored track and the tracks below), between the marks, rendered into a new
  master clip named after the sequence (ours: "<sequence> Mixdown") in the chosen bin, on the chosen drive
  and resolution. Without marks, ours mixes the whole sequence, as Audio Mixdown does. The sequence is left
  as it is; the data track is not mixed down.
* Not yet (6.6b): expanding a nest in the Timeline by double-clicking, nested effects in the Effect Editor
  and their order, effect templates in bins. Nests are not written to AAF, `.avb`, EDL or OTIO
  ("Nested effects do not appear in edit decision lists", p.219): those keep the segment as it is.
