# The 2D transitions (Phase 6.3a)

Pages are the FX Guide's. The Effects Reference (ch.17) names each effect, says what it does in a
sentence and shows its middle in a picture; it gives no numbers. Where the geometry below is ours, it
says so. The comparison of the Box Wipe, Conceal, Push and Squeeze (p.490) is the reference picture for
those four.

Code: `media::isWipe` and `media::wipePicture` (`src/media/wipes.cpp`, drawing with
`src/media/effect_drawing.*`), called by the compositor
(`src/media/compositor.cpp`) for transitions and segment effects; the Custom Grid's More Options in the
Effect Editor (`EffectEditor::setCustomGrid`). Tests: `tst_compositor` (`wipeShapes`, `movingWipes`,
`wipesInSequences`), `tst_effects` (`customGrid`).

## How they are drawn

* Each effect is a geometry: for every pixel, its distance from the incoming picture's region (negative
  inside), and where it comes from in the incoming and the outgoing pictures. The outgoing picture is
  drawn, then the border band outside the incoming region, then the incoming picture; edges are blended
  over the Border's softness, and over a pixel at least (ours, so edges are not jagged). Moved and scaled
  pictures are sampled bilinearly; outside its picture, a moved picture is transparent, so black shows
  (the Push picture on p.490 shows the same black).
* Progress runs from 0 (all outgoing) to 1 (all incoming). In a transition it is the transition's
  progress (docs/spec/compositor.md), or its keyframed Foreground Level, as for the dissolves.
* Applied to a segment ("Apply to: Transitions, multilayer segments", p.459 and the others), the effect
  wipes from the tracks below to the segment. Ours: over the segment's length, or by its keyframed
  Foreground Level; on V1 the tracks below are black.

## Parameters they share

* Foreground (p.495): Swap Sources "swaps the image source for the foreground and background layers";
  Reverse Animation "reverses the entire effect, including the direction of movement as well as the
  incoming and outgoing sources": the progress runs backwards with the pictures exchanged.
* Border (p.493): "the color, softness, and width of a border, for example, on the incoming image in a
  transition"; Push: "The border is on the incoming video" (p.479); X and Y Spin: "Border appears only
  on the incoming video" (p.482). Ours:
  * the band lies outside the incoming region's edge;
  * Width 100 and Soft 255 are each a sixteenth of the picture's width;
  * the colour is white until set;
  * the band's colour runs from Color at the incoming edge to Blend Color at its outer edge, and Fixed
    Border ("Turns off the Blending option") keeps it Color.
* Custom Grid (p.494): "a grid to define the position or progress of certain effects", set with the
  More Options button's dialog box; "The minimum number of rows and columns is 2 x 2, and the default
  number is 8 x 6". Ours: 8 columns by 6 rows; the dialog box takes columns and rows (2 to 64); the
  guide's "standard preset grid" choices are not named, so there are none. Stored as
  `"Custom Grid": {"Columns", "Rows"}`.
* Acceleration (p.492) is not among these effects' parameter categories (it is in the Image and Key
  effects', 6.3b-c). Grid (p.496) is the Effect Grid, which comes with the Effect Preview's tools.

## The effects

Named for the corner or side they start from, unless said otherwise.

* Box Wipe (p.459): "Incoming video begins as a box and wipes out the outgoing video as the box expands
  to fill the screen"; "The effect icons for the category indicate the starting position of the box".
  The box grows from its corner or the middle of its side.
* Conceal (p.459-460): "Incoming video slides on top of the outgoing video", from its side or corner.
* Push (p.479): "Incoming video pushes out outgoing video starting from a corner or side of the screen":
  both pictures move by the same amount.
* L-Conceal (p.475): "Incoming video slides vertically over one half of the screen and then slides
  horizontally to fill the remaining half", from the corner it is named for. Ours: the vertical slide in
  the first half of the effect and the horizontal one in the second.
* Squeeze (p.483-484): "Incoming video squeezes over the outgoing video, beginning as a point or line at
  a specific location on the screen and expanding to fill the screen". The whole incoming picture is
  scaled: Bottom Centered from the middle of the bottom, Centered Zoom from the centre, Top Left from its
  corner (p.490), Right to Left from a line at the right, keeping its height (the picture on p.484). The
  other eleven of the fifteen are not named (effects.json).
* Edge Wipe (p.460-461): "Incoming video begins at the edge or center of the screen and moves out to
  wipe away the outgoing video". Ours: Horizontal moves left to right and Vertical top to bottom; the
  Open ones from the centre out; a diagonal from its corner to the opposite one, its edge through the
  picture's corners. The diagonals' Scaling: docs/spec/image-effects.md.
* Shape Wipe (p.481-482), as the pictures on p.482 show: 4 Corners (a box from each corner), Center Box,
  Circle (reaching the corners), Diamond, Ellipse (the picture's shape, ours), Clock (ours: a hand
  sweeping clockwise from twelve o'clock), Horizontal Bands (ours: bands of the grid's rows from the
  left and the right in turn), Horizontal Blinds and Vertical Blinds (each row or column of the grid
  filling from its top or left). Center Box, Circle, Diamond and Ellipse take Scaling and Position
  (docs/spec/image-effects.md).
* Matrix Wipe (p.476): "Incoming video begins as blocks or bands that follow a pattern while wiping out
  the outgoing video", in the Custom Grid's cells. Grid: every cell grows from its centre at once (the
  picture). The others: one cell after another, each growing from its centre in its turn (ours):
  One-WayRow row by row, left to right; Zig-Zag the rows left to right and right to left in turn (ours);
  Spiral round the edge clockwise from the top left and inwards (ours); Speckle in a fixed shuffled
  order, the same on every platform (ours).
* Sawtooth Wipe (p.480-481): "Incoming video appears with a sawtooth edge and expands to fill the
  screen"; Horizontal Sawtooth "moves left to right", Vertical Sawtooth "moves top to bottom"; the Open
  ones from the centre out. Ours: a tooth for each row (or column) of the Custom Grid, one column (row)
  deep.
* X Spin and Y Spin (p.482): "Outgoing video is squeezed until it appears as a line (vertical for X
  Spin, horizontal for Y Spin). Incoming video then expands from that line until it fills the screen":
  the outgoing in the first half, the incoming in the second, black beside them.
* Z Spin (p.483): "Incoming video begins as a dot in the center of the screen and then enlarges to full
  screen while rotating counterclockwise one full revolution, covering the outgoing video".

Peel waits for the 3D effects (6.7), PlasmaWipe for 6.9; until then they are cuts at the cut point.
