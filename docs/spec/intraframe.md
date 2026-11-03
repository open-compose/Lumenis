# Intraframe effects

ROADMAP 6.9. FX Guide (2025.12) ch.15, p.391-441, and the parameter reference p.493-497 and p.532-548.
Code: `src/model/intraframe.*` (the objects), `src/media/intraframe.cpp` (their pictures), called from
`src/media/compositor.cpp`. Tests: `tst_intraframe`, `tst_compositor` (`intraframe`).

## 6.9a: objects and their pictures

### From the guide
* "You can use the Paint Effect and the AniMatte effect to perform paint or animated matte effect operations within an
  individual frame or series of frames"; Scratch Removal and the Blur and Mosaic effects use the same tools (p.391).
  The objects are vector based, "composed of mathematically described lines and Bézier curves"; "Any object you paint
  and any changes you make to it appear for the duration of the segment"; "You can create multiple paint or matte
  shapes within a single effect. You can also add keyframes and apply effects separately to each of the shapes"
  (p.391).
* "Media Composer renders painted objects to the screen starting with the bottom layer" (p.392); "Each object you paint
  is on its own foreground layer" (p.396).
* The tools: Rectangle, Oval, Polygon, Curve and Brush (p.396). Control points have straight-edge, smooth or corner
  joins with direction bars and handles (p.405).
* Keyframeable: an object's position, shape, Z rotation and scale, Feathering, Foreground level and Magic Mask
  (p.416).
* The Paint Effect's modes (p.537-544): Solid, Erase, Outline, Clone, Colorize, Hue, Saturation, Luminance, Darken
  Only, Lighten Only, Darken, Lighten, Add, Subtract, Invert, Mosaic, Blur, Median, Unsharp Mask, Gradient, Emboss,
  Scratch Removal, Color Adjust, Luma Range, Luma Clip, Color Style, Color Gain, Color Match; Opac. and Object Visible
  (p.537).
* AniMatte (p.548): Key In "displays the image on the higher track (foreground) within the matte selection while
  revealing the image on the lower track (background) outside"; Key Out the reverse; on a transition, the incoming
  segment (Key In) or outgoing (Key Out) within the matte. Swap Src and the Foreground category (p.394, p.495).
* Brush (p.493): Path (how much of the stroke shows), Soft, Size (0 to 100 pixels), the Shape buttons and Spin.
  Feathering (p.495): Bias (0 feathers from the outside edge, 100 from inside, 50 centred), Hor and Vert (0 to 63
  pixels), Fixed Aspect, Anti-alias. Magic Mask (p.497): a picked colour, Gain (the tolerance) and Soft.
* The Blur and Mosaic effects "act as intraframe effects": a blur or mosaic within their objects (p.439-441).

### Ours
* The objects are kept under the effect's "Intraframe" category, bottom layer first. Each has an id, its kind, closed
  or open (Brush strokes), shapes (control points as fractions of the frame, keyframed in the effect's time and
  interpolated linearly between keyframes; between shapes of different point counts the earlier one holds), a group and
  a lock, and its own parameters with their keyframes, copied from the effect's own when the object is drawn
  (`model::addIntraframeObject`); `model::objectEffect` gives the effect as one object sees it.
* An object's place is its "Intraframe Object" category: X and Y offsets in pixels of a frame 1920 wide, Rotation in
  degrees and Scale in percent about Center X/Y (the shape's centre until set), all keyframeable
  (`model::placedShapeAt`). Rotation keeps the shape's proportions in pixels.
* `media::intraframeMask`: the outline filled (Anti-alias as the guide's option), or a Brush stroke stamped with its
  head (Circle until set; Oval, Ellipse and Square) every quarter of its size, Spin turning it and Soft blurring by up
  to a quarter of its size; Magic Mask limits it to colours within Gain of the picked colour (in steps of four 8-bit
  values) and ramps over Soft; Feathering blurs it by half of Hor and Vert each way and Bias remaps the blurred edge
  (50 keeps it centred); Opac. scales it; Object Visible (Blur Visible, Mosaic Visible) hides it, a keyframed flag
  holding its last keyframe's value.
* The Paint Effect's modes act within each object's coverage on the picture so far: Solid and Outline (two pixels wide)
  paint the Color category's colour; Erase brings back the segment's own picture; Clone takes the picture from where
  the object was drawn before it was moved; Colorize, Hue, Saturation and Luminance replace those components with the
  colour's; Darken Only and Lighten Only take the darker or lighter per channel; Darken and Lighten change by Amt in
  8-bit steps; Add and Subtract the colour; Invert; Mosaic and Blur as those effects with this category's Hor and
  Vert; Median over three by three pixels; Unsharp Mask adds Amt percent of the difference from a Hor by Vert blur;
  Gradient fades the colour across the object's bounds along Angle; Emboss is grey from the luma's slope along Angle;
  Color Adjust, Luma Range, Luma Clip, Color Style and Color Gain take the Color Effect's categories. Color Match
  changes nothing until colour correction (6.10). The Scratch Removal mode takes material from this frame, Hor and
  Vert away, until 6.9d brings other frames.
* The Blur and Mosaic effects blur or tile within their objects only; without objects the segment shows as it is.
* AniMatte: objects combine in layer order, Key In adding to the foreground's area and Key Out taking from it; with
  only Key Out objects the foreground shows outside them. Foreground's Level, Invert Key, Swap Sources and Show Alpha
  apply. On a transition the incoming clip is the foreground.

## 6.9b1: drawing and selecting objects

### From the guide
* Applying the Paint Effect or AniMatte in Effect mode (p.393); the Effect Editor's Selection tool, Bring Forward and
  Send Backward ("With the Alt key ... bring a painted object to the front or send an object to the back"), and the
  painting tools (p.394-395); "Top to bottom: Rectangle, Oval, Polygon, Curve, and Brush tools" (p.396).
* "After you use the Rectangle, Oval, Polygon, and Curve tools, Media Composer reverts to the Selection tool ... After
  you use the Brush tool, it remains the active tool" (p.396).
* Rectangles and ovals by dragging their diagonal, anchored where the drag starts (p.400); polygons by clicks, a drag
  after a click making a curved segment, "double-click ... or click the initial control point" to close (p.400-402);
  freehand curves with the Curve tool (p.403).
* Selecting: click, Shift+click, a lasso; "When you select multiple objects, one set of selection handles appears
  around all the objects"; Go to Next/Previous Event changes the selection; Delete (p.403). Moving by dragging inside
  the outline; rescaling by a selection handle, Alt from the centre (p.404, p.415); the object's colour and feathering
  as its own parameters (p.404).
* The Blur and Mosaic effects' tools: Selection, Z-rotation, Reshape, Bring Forward, Send Backward, Rectangle, Oval
  (p.439).

### Ours
* `IntraframeTool` (`src/ui/intraframe_tool.*`) is the Effect Preview's `Composer::OverlayInput` for an Intraframe
  effect at the outermost level. The Effect Editor's lettered buttons (`intraframeSelection`, `intraframeBringForward`,
  `intraframeSendBackward`, `intraframeRectangle`, ...) show for Intraframe effects; Blur and Mosaic show no Polygon,
  Curve or Brush.
* Every object shows as a wire frame (strokes as open lines, `Composer::EffectPreview::lines`); the selection has four
  corner handles. While a drag goes on the Record monitor shows it (`EffectEditor::pictureOverride`); its end is one
  undo step ("Add Rectangle", "Move", "Scale", ...).
* A polygon point dragged after its click gets smooth direction handles; Return also closes a polygon, Escape drops
  it. The Curve and Brush tools keep a point every 1.5% of the width with Catmull-Rom handles (`smoothCurve`).
* Moving and scaling change the objects' "Intraframe Object" X, Y and Scale (a keyframe at the position when the
  parameter has keyframes); scaling keeps the opposite corner (or the centre with Alt) where it was.
* The lasso selects the objects whose bounds lie inside it. `selectAdjacent` steps through the objects in layer order.
* With one object selected, the editor's controls show and change that object's parameters
  (`EffectEditor::setIntraframeSelection`, the object branch of `changeShown`); with none, the effect's own, which new
  objects take.
* Tests: `tst_intraframe_ui`.

## 6.9b2: reshaping, rotating and the Object menu

### From the guide
* Control points and direction handles (p.405); reshaping with the Reshape tool or a double-click (p.406, p.413-414);
  the modifier clicks: Shift+click a handle keeps the opposite bar's length, Alt+click a point turns a straight-edge
  join smooth and back, Alt+click a handle turns a smooth join into a corner and back (p.409-410). A Curve tool
  object's opposite bar moves in unison without changing length (p.413).
* Adding a control point by clicking the outline, "calculated so that the object does not change dramatically in
  shape", in all keyframes; changes at a selected keyframe or a new one interpolate (p.410-411). Removing with Delete;
  Go to Next/Previous Event moving clockwise and counterclockwise through points; moving one or several points
  (Shift+click, lasso, Ctrl+A); nudging by one pixel with the arrow keys, Alt for up and down (p.411-412). Alt+click the
  Reshape tool hides the direction handles (p.414).
* Z-Rotation: the rotation handle and the + centre, which can be dragged (p.414-415).
* The Object menu: Bring Forward, Send Backward, Bring to Front, Send to Back, Group, UnGroup, Lock and Unlock
  (p.417-418); grouping groups and ungrouping them completely (p.417).

### Ours
* `IntraframeTool`'s Reshape and Z-Rotation (`src/ui/intraframe_reshape.cpp`) with the model's `editableShape`,
  `insertControlPoint` (de Casteljau), `nearestSegment`, `removeControlPoints`, `toggleSmooth` (handles a third of the
  way towards the neighbours), `toggleCorner`, `toggleFixedLength`, `setDirectionHandle` and `unplacedPoint`/
  `unplacedVector` (src/model/intraframe.*). Points are handles "intraframe:point:i"; a selected point's bars
  "intraframe:in:i"/"out:i".
* Joins change in the shape being edited only; inserted and removed points change every shape. Nudging is one pixel of
  a frame 1920 wide; the lasso for points and the Trim keys are not built.
* Z-Rotation turns the object's Rotation by the angle swept about its centre (in pixels); the centre's drag sets Center
  X/Y. The rotation handle is 8% of the width from the centre.
* The Object menu is a menu button ("intraframeObjectMenu") beside the drawing tools. A click on a grouped object
  selects its group; locked objects are not moved, scaled, reshaped or deleted.
* Tests: `tst_intraframe` (`reshaping`), `tst_intraframe_ui` (`reshapeRotateGroup`).

## 6.9c1: brushes and the eyedropper

### From the guide
* "In the Brush parameter category of the Effect Editor, click one of the Brush Shape buttons. The default shape of the
  brush head changes to your selection" (p.397). "The parameter sliders do not appear until you select the
  corresponding drawing tool" (p.396).
* A custom brush head: click a Brush Shape button as its basis, click the head in the Brush Preview window, drag its
  control points; rounded shapes' points get direction bars and handles, "but you cannot use the modifier key
  variations" (p.398). Spin turns the head, -360 to 360 (p.399).
* Brush templates: "click the Brush Template icon, and then drag it to a bin ... the name of the template defaults to
  Brush"; dragging the template to the Brush Preview window gives the brush its parameters (p.399).
* The eyedropper: "Position the pointer over the Color Preview window to activate the eyedropper ... drag the
  eyedropper to the Effect Preview monitor, and click on the color" (p.89); with the Brush tool in Solid mode, Alt
  makes the pointer an eyedropper over the Effect Preview and the colour picked "becomes the new default color"
  (p.397).

### Ours
* The Brush category keeps "Shape" (Circle, Oval, Ellipse, Square: `model::brushShapes`) and, for a custom head,
  "Head": control points in a unit box (`model::brushHeadPoints`, `setBrushHead`, `setBrushShape`); a Shape button
  drops the custom head. The renderer stamps the head the category gives (`brushHead` in `src/media/intraframe.cpp`).
* The Effect Editor's Brush row (`effect_editor_brush.cpp`): the Shape buttons ("brushShape:<shape>"), the Brush
  Template icon ("brushTemplate") and the Brush Preview window (`src/ui/brush_preview.*`), which shows the head with
  Size as a percentage of its width (at least 8 pixels) turned by Spin. Its control points show after a click on the
  head; each drag of a point or handle is one undo step ("Brush Head"). The square's corners are straight-edge joins.
* A brush template is an Effect item holding the Brush category under "brush" (`model::brushTemplateItem`); it drags as
  `brushTemplateMime()` and drops on the Brush Preview window from the icon or a bin ("Apply Brush Template").
* The Color Preview window (`ColorSwatch` in `effect_editor.cpp`) opens the system colour dialog when clicked (ours:
  the Other Options button's dialog), and is the eyedropper when dragged: releasing over either monitor's picture
  picks its colour there (`Composer::colorAtScreen`, `colorAt`). The colour is the picture as shown, effects and all.
* Alt+click with the Brush tool sets the Paint Effect's colour from the Effect Preview's picture and paints nothing; we
  do not require the Solid mode.
* Tests: `tst_intraframe` (`brushHeads`), `tst_intraframe_ui` (`brushAndEyedropper`).

## 6.9c2: the Previsualization Marker and Export Matte PICT

### From the guide
* "The Previsualization Marker tool lets you place a marker in a segment to represent an effect that will be processed
  at an optical facility. This tool is applicable to 24-fps film projects only and does not appear in the Effect Editor
  for the Paint Effect in any other project types"; each marker, "along with its position in the segment on a
  keyframe-by-keyframe basis, appears as an optical event"; the Color category sets its colour; "Move the pointer to the
  location in the frame where you want to place the marker, and click"; keyframes move it; "Click the marker to
  highlight it, and then press the Delete key" (p.412-413).
* Export Matte PICT: "a high-contrast 32-bit PICT file that includes an alpha channel" of the AniMatte matte at the
  position, "transparent (black) and opaque (white) components"; File > Export Matte PICT after clicking the Effect
  Editor, the Export PICT As dialog (p.418). Importing it back as a Matte Key clip with the Alpha options (p.418-419).

### Ours
* The marker tool ("intraframeMarker", lettered "Pv") shows for the Paint Effect when `EffectEditor::setFilmProject`
  says the project is a film project at 24 fps (MainWindow from the project's film type and rate). A click adds a
  Marker object with one control point (`model::addPrevisualizationMarker`, "Add Marker") and returns to Selection.
  It shows in the picture as a disc 20 pixels across (of a frame 1920 wide) in its Color category's colour, whatever
  the mode, and in the Effect Preview as a small square; it is found within twice the stroke reach of its point.
* `model::previsualizationEvents` lists each marker's colour and place at each keyframe of its X, Y or shape (one event
  without keyframes): the data for the optical list, which comes with the List Tool's film lists (Phase 9) together
  with Effect Grid coordinates.
* `media::aniMatteImage` makes the matte (Key In and Key Out objects, Invert Key, Level) white or black with the same
  alpha; Magic Mask's colours come from the Record monitor's picture. File > Export Matte PICT
  (`MainWindow::exportMattePict`) writes it at the project's raster; without an AniMatte effect in the Effect Editor a
  message says so, where Media Composer shows its default Export command instead.
* PICT (`src/media/pict.*`): our own writer and reader from Apple's published format (Inside Macintosh: Imaging With
  QuickDraw, appendix A): an extended version 2 picture with one DirectBitsRect of 32-bit pixels, the alpha, red,
  green and blue planes of each row PackBits-compressed (packType 4); the reader also takes unpacked direct pixels and
  refuses the rest. Fuzz target `pict`. Import of PICT files and Matte Key clips with the Alpha options comes with
  graphics import (Phase 9).
* Tests: `tst_intraframe` (`pictFiles`, `exportedMatte`, `previsualizationMarkers`), `tst_intraframe_ui`
  (`markerAndMatte`).

## 6.9d: Scratch Removal

### From the guide
* "You can replace a flaw with clean material from the same frame or field or from other nearby frames or fields"
  (p.419); "You cannot access frames or fields outside the segment that contains the effect" (p.428).
* Frame processing "copies replacement material from Field 1 of the clean frame to Field 1 of the flawed frame and from
  Field 2 of the clean frame to Field 2"; field processing "copies replacement material from only one field to all
  subsequent fields"; frame processing is the default (p.420). The Frame Processing button, in the Paint Effect's Mode
  category or the Scratch Removal effect's Scratch category, labels the slider Frame (-5 to +5) or Field (-10 to +10),
  and "does not appear when you are working with single-field material" (p.421, p.547).
* The Hor and Vert sliders place the material relative to the shape; Frame/Field counts from the Offset Fast Menu's
  zero point: Relative (the frame shown), From Start (the default) or From End; "You cannot access frames more than 5
  frames (or fields more than 10 fields) from the zero point" (p.427-428, p.546-547).
* The Scratch Removal button, on the Tool palette by default: "a two-frame segment ending at the frame marked by the
  position indicator", on the highest selected track, auto-nesting over an effect already there (p.422-423); with IN
  and OUT marked and the position between them or just before IN, "the scratched frames or fields and the clean frame
  or field immediately before them" (p.426).

### Ours
* `media::scratchRemovalSource` gives the picture the Scratch Removal mode (Paint) or effect takes its material from;
  the compositor hands the Intraframe effects the segment's own pictures at its other frames (`IntraframeFrames`).
  `model::scratchSourceIndex` keeps the slider within 5 frames or 10 fields and the result within the segment. Fields
  count from the segment's start (field 1 of frame n is field 2n, in the media's field order); a field's lines come
  from the source field's lines.
* The default Frame/Field is -1 (p.547; p.427's table says 0: From Start both take the first frame). The slider runs
  -10 to +10 either way; with frame processing the value is kept to ±5.
* `fx.scratch_removal` (`Timeline::scratchRemoval`, `model::applyScratchRemoval`) adds edits around whole frames (no
  field-accurate segments) on the highest selected video track and applies the effect, auto-nested over one there; one
  undo step. It is on the Tool palette in Clear Both Marks's place and on the Command Palette's FX tab.
* The Frame Processing button shows when the project is interlaced (`EffectEditor::setInterlacedProject`); turning it
  off labels the slider Field.
* Tests: `tst_intraframe` (`scratchRemoval`), `tst_intraframe_ui` (`frameProcessing`), scenario `37-scratch-removal.json`.
