# Titler+ (ROADMAP 6.8a-c)

Avid Titler+, "the preferred tool for creating titles" (FX Guide ch.12, p.302-326). Ours is called **Titler+**,
as its Tools menu item is (docs/spec/ui-inventory.md, "Renamed"). Code: `src/model/titler.*` (the title kept with
the effect, layer order, groups, applying it), `src/media/titler.cpp` (its picture), `Timeline::applyTitler`.
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

## Not yet
* 6.8b: the Record monitor's tools (Text, Selection, Rectangle, Ellipse), the Dashboard, the Effect Editor's object
  parameters, alignment and the keyboard shortcuts.
* 6.8c: keyframes, Roll and Crawl, tracking, templates and presets, promotion from the classic Title Tool, EDLs and
  Find.
* Fill Overlay (media on the effect's subtrack, p.324).
