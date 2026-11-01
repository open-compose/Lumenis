# Titler+ (ROADMAP 6.8a-c)

Avid Titler+, "the preferred tool for creating titles" (FX Guide ch.12, p.302-326). Ours is called **Titler+**,
as its Tools menu item is (docs/spec/ui-inventory.md, "Renamed"). Code: `src/model/titler.*` (the title kept with
the effect, layer order, groups, applying it), `src/media/titler.cpp` (its picture), `Timeline::applyTitler`.
The Record monitor's tools are `src/ui/titler_tool.*` with `effect_editor_titler.cpp`; the Dashboard and the Effect
Editor's controls `src/ui/titler_panel.*`.
Tests: `tst_titler`, `tst_titler_ui`, scenario `34-titler-marks.json`.

## From the guide
* "When applied, Avid Titler+ adds a title effect to clips or empty track segments in your sequence, which can be
  directly viewed and manipulated in the Record monitor" (p.302). "Select the clip or an empty track segment where
  you want to add a title. If you want to create a new clip containing your title, set in and out marks in the
  Timeline to highlight a portion of the track before proceeding. Click the Avid Titler+ icon at the top of the
  Timeline or select Tools > Avid Titler+. The Avid Titler+ Dashboard and the Effect Editor open" (p.303).
* Objects: text (point text that grows, or "a defined paragraph text box" that wraps once resized, p.304),
  rectangles and ellipses (p.305-306). Font: Family, Style, Size, Underline, Justification Horizontal and Vertical,
  Font Orientation (Left To Right, Right To Left, Top To Bottom), Kerning, Leading, Skew (p.306-310, p.321-322).
* Transform: Opacity, Position, Scale (Fixed Aspect), Rotation, Alignment and the Anchor Point; "Avid Titler+ uses a
  universal coordinate system where "0,0" ... is always the center of the frame"; transformations happen about the
  anchor point; "when text is left justified, the anchor point will be at the baseline left of the object" (p.310-314).
* Style: Fill (on by default; Solid Color or a four-corner Color Gradient, Opacity, Softness), Outline (Color,
  Opacity, Size, Softness, Type Outer/Inner/Border, Shape Round/Square), Shadow (Color, Opacity, Distance, Angle
  0-360, Softness) (p.314-317, p.322-324).
* Background: "a solid color or color gradient behind all of your text and graphic objects" with its Opacity;
  Foreground: Fill Overlay, Opacity and Crop "of the entire composition" (p.317, p.324).
* Layer order with Bring Forward and Send Backward; Group and Ungroup (p.318).
* "A label called "Title Text" appears in the Timeline on segments created with Avid Titler+ ... enabled by default
  and can be toggled from the Timeline Clip Text menu ... based on their topmost Titler+ text content" (p.305).

## Ours (6.8a)
* The title is JSON under the effect's "Titler" parameter (`model::Title`): objects back to front, groups holding
  their children, the Background and Foreground. Positions and sizes are pixels of a 1920 x 1080 frame from its
  centre, x right and y down; font sizes the same pixels; a picture of another raster is scaled by its height.
* Defaults where the guide gives none: white fill; a 72-pixel font in the system's sans-serif family; outlines 4
  pixels, black; shadows black at 75%, 8 pixels at 45 degrees (down and to the right), softness 4; rectangles and
  ellipses centred on their origin; a group's opacity on its children.
* Text lines: the first baseline at the origin; Vertical Center and Bottom move the lines up so the middle or last
  baseline is there; Right To Left reverses each line's characters, Top To Bottom puts one character under the other;
  Skew shears about the baseline. Kerning adds letter spacing in pixels; Leading adds to the line height.
* The picture (`media::titleImage`): each object its own layer, Shadow then Fill then Outline (Outer around, Inner
  within, Border astride the edge), each softened by a three-pass box blur of its Softness, at the object's
  Opacity; the Background under all; the Foreground's Crop (percent of the frame from each edge) and Opacity over
  all. Gradients are bilinear between the four corners of the shape's bounds. Fill Overlay is kept, not drawn yet.
* Applying (`model::applyTitler`): between the Record monitor's marks the highest selected video track gets Add
  Edits at both marks (filler added past its end) and every segment between gets an empty title; else the selected
  segments, else the one at the position. A segment that has an effect keeps it in a nest under the title. One undo
  step; the Effect Editor opens.
* The effect is in the Effect Palette's Title category as "Titler+" (it is not in the Effects Reference).
* The Timeline top toolbar's Titler+ button (a T with a plus, our drawing) runs Tools > Titler+.

## 6.8b1: the Record monitor's tools
From the guide (p.303-319):
* Text Tool: "you can type anywhere and create a text layer at that location ... by simply double-clicking in an
  empty area of the viewer or by pressing T ... You can also return to an existing text layer to edit the text by
  clicking inside its bounding box while in text mode. If you click in an empty area but do not type anything, that
  empty layer gets deleted"; "Double-clicking the title text allows you to enter text mode"; "click and drag inside a
  text box to highlight any text"; "Highlighted text is indicated by an overlay" (p.303-304).
* Selection Tool: "pressing M ... The bounding box appears, and you may click and drag the text to move it around";
  resizing a text's bounding box makes "a defined paragraph text box" that wraps; "Command (macOS) or Alt (Windows):
  Freeform scale; Shift: Constrained scale (maintain aspect ratio); Command+Shift (macOS) or Ctrl+Shift (Windows):
  Constrained scale from Anchor Point" (p.304, p.311); "Shift+Drag Up or Down: Moves the object along the Y axis"
  (p.311); "hover next to one of the bounding box corners to activate the Rotation icon ... hold down the Shift key
  while dragging to rotate the shape in increments of 15 degrees" (p.312).
* Rectangle and Ellipse Tools: "Click and drag in the viewer"; Shift for a square or circle, Ctrl+Shift (Command+Shift)
  "will originate from the Anchor Point" (p.306).
* Anchor points "appear in the viewer as crosshairs on selected objects" (p.313).
* Bring Forward "+" and Send Backward "-"; Group Ctrl+G, Ungroup Ctrl+Shift+G, Shift+click to pick several, Alt+click
  to "deep pick" inside a group; Select Previous Object (Page Down) and Next (Page Up) (p.318-319).
* "When no object is selected ... pressing the spacebar will start playback" (p.305).

Ours:
* The tools work in the Effect Preview: Titler+ turns Effect mode on when it is applied, and the Effect Editor gives
  the Record monitor a `Composer::OverlayInput` (`TitlerTool`) that takes its presses, drags and keys before the
  Composer does (keys it wants also beat the menus' shortcuts, so Ctrl+G groups there instead of Find Again).
* Typing and drags are previews (`EffectEditor::previewTitle`, shown in the Record monitor through
  `EffectEditor::pictureOverride`) and become one undo step at their end: typing when Escape, another click or another
  tool ends it (the text is lost if the shown effect changes first); a drag on release.
* Text editing: Left, Right, Home and End (the text's start and end), with Shift to select; Return a new line;
  Backspace and Delete; Ctrl+A selects all. Clicking in a text being typed does not move the cursor.
* Scaling is always from the anchor point, by how far the handle moved along the object's own axes: a text without
  modifiers gets a paragraph width; Alt scales it freely and Shift evenly; a rectangle or ellipse changes size
  (Shift evenly). The rotation zone is 40 title pixels just outside a corner; handles take presses within 16.
* Drawing: from corner to corner, or from the centre with Ctrl+Shift; shorter than 4 pixels makes nothing.
* Page Up and Page Down go round the top-level objects; Delete or Backspace removes the selection; Escape clears it.

## 6.8b2: the Dashboard and the Effect Editor's controls
From the guide (p.302, p.306-324):
* "The Avid Titler+ Dashboard design includes dedicated Font, Transform, and Style sections" (p.302); "The Effect
  Editor and the Dashboard update to dynamically reflect the current selection with only the relevant parameters"
  (p.320); the Effect Editor's Selected Object Parameters (Title Text, Font, Transform, Fill, Outline, Shadow) and Global
  Group Parameters (Foreground, Background, Roll / Crawl) (p.321-324).
* Alignment: "Options for Align H include Align Left, Align Center, Align Right, and Align Horizontal Center. Options
  for Align V include Align Top, Align Center, Align Bottom, and Align Vertical Center"; "If multiple objects are
  selected, alignment is relative to the selected objects, not to the frame"; with the Grid on the first click aligns to
  the grid, the second to the frame edges (p.312).
* The Anchor Point presets Center ... Bottom Left and "Custom (appears automatically after manually moving the
  sliders)" (p.313).
* "Click the Grid button ... to quickly display the Safe Action/Title grid" (p.319).

Ours:
* One widget, `TitlerPanel`, across in the Dashboard (a tool window the Effect Editor keeps, shown when Titler+ is
  applied) and down at the top of the Effect Editor, which adds the Title Text box (stored when it loses the focus).
  Number fields store their value when it is entered (one undo step each); dragging the numbers is not ours yet.
* The panel shows the first selected object and changes all the selected ones; during typing a change joins the text's
  undo step. The Font section is for text objects only; Transform and Style need a selection; Background and
  Foreground are the title's.
* Align Center centres on the reference (the frame, or the selection's bounds with several selected); Align Horizontal
  Center and Align Vertical Center always on the frame. The grid's first stop is the Safe Title area, 80% of the frame;
  the Grid shows the Safe Action (90%) and Safe Title (80%) areas. Right-clicking the Grid button for the Grid Settings
  (p.319) is not ours yet.
* Anchor presets are points of the object's own bounds; the object shifts with its anchor.
* A solid colour also sets the gradient's four corners until they are set one by one.

## 6.8c1: keyframes on titles
From the guide: the Effect Editor gives "access to animation and other standard Effect Editor controls" (p.320); "You
can animate text and graphic objects in Avid Titler+ by applying keyframes to supported parameters in the Effect
Editor. When title segments are modified during editorial operations such as trimming, overwriting, splitting, or
duplicating, Avid Titler+ preserves the relationship between the title and its associated keyframes" (2026.8 p.325,
pointing to "Working with Keyframes" p.94 and "Controlling Advanced Keyframe Behavior with Trimmed Effects" p.124).

Ours:
* An object's numbers are effect keyframes of the category `Titler:<object id>` (`model::titleCategory`), the title's
  own of `Titler`: every object's Opacity, Position X/Y, Scale X/Y, Rotation, Anchor X/Y, Fill Opacity/Softness,
  Outline Opacity/Size/Softness and Shadow Opacity/Distance/Angle/Softness, a text's Size, Kerning, Leading and Skew;
  the title's Background Opacity, Foreground Opacity, Fill Overlay and Crop Top/Bottom/Left/Right
  (`model::titleParameters`). Colours, fonts, text and on/off choices are not keyframed.
* Title keyframes are always Fixed (p.124), so trims keep them on their frames; a split (add edit) now moves the second
  half's Fixed keyframes with its material, for every effect (`Sequence::cut`; scenario
  `35-title-keyframes-split.json`).
* `model::titleAt` is the title at a time; the picture (`media::titlePicture`), the Record monitor's tools and the
  panels all see the title at the position. `model::storeTitleAt` stores an edited title: a keyframed number that
  changed gets a keyframe at the position (or its keyframe there replaced), everything else is stored as it is.
* The panel's Add Keyframe and Delete Keyframe buttons work on every number of the selected objects, or on the
  title's own numbers with nothing selected (`EffectEditor::setTitleKeyframes`); one undo step each. The keyframes
  show in the Effect Preview's position bar and Go to Previous/Next Event stops at them
  (`EffectEditor::positionBarParameters`). Keyframes of a removed object go with it (`model::setTitle`).

## Not yet
* Title keyframes as Effect Editor tracks and graphs (they live in the position bar and the panel only).
* 6.8c2-c4: formatting selected characters (p.304), Roll and Crawl, tracking, templates and presets, promotion from
  the classic Title Tool, EDLs and Find.
* Dragging the panel's numbers; the Grid button's Grid Settings; the Color Info Tool from a colour box (p.315).
* Fill Overlay (media on the effect's subtrack, p.324).
