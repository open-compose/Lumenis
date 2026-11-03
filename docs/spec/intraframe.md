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
