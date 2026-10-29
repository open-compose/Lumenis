# The image effects (Phase 6.3b)

Pages are the FX Guide's. The Effects Reference says what each effect does in a sentence or two and
gives the parameters' ranges (ch.18), but no geometry; where the mapping below is ours, it says so.

6.3b1 brought the geometric effects, 6.3b2 the Color Effect, Blur, Mosaic and Acceleration.

Code: `media::isImageEffect`, `imageEffectPicture`, `isReformat`, `reformatPicture` and
`parameterDefault` (`src/media/image_effects.cpp`, with the Color Effect, Blur and Mosaic in
`src/media/color_effects.cpp`), drawn with `src/media/effect_drawing.*` (shared
with the 2D transitions); the compositor applies them (`src/media/compositor.cpp`); the Effect Editor
shows `parameterDefault` and gangs Fixed Aspect (`EffectEditor::defaultValue`, `setValue`). Tests:
`tst_compositor` (`imageEffects`, `colorEffect`, `blurAndMosaic`, `reformat`,
`shapeScalingAndPosition`), `tst_effects` (`effectDefaults`), `tst_effect_keyframes`
(`acceleration`).

## Shared parameters

* Scaling (p.499): X and Y "0 to 400, where 100 indicates 100% scaling (normal size)"; Fixed Aspect
  "Gangs the X and Y sliders together so that they move in unison when you drag either one": the Effect
  Editor sets both (and Hor and Vert the same where Fixed Aspect sits beside them).
* Position (p.498): X and Y from -3000 to 3000, negative X to the left and negative Y to the top. Ours:
  in pixels of a picture 1920 wide, whatever the picture's size.
* Crop (p.494): T "0 (top of the screen) to 999 (bottom of the screen)", B "–999 (top of the screen) to
  0 (bottom of the screen)", L and R the same across.
* Background Color (p.492), black until set.
* Values until set (ours, `parameterDefault`, shown by the Effect Editor too): Scaling 100, but 50 for
  Picture-in-Picture (its illustration on p.458 shows it smaller); 14:9 Letterbox's Position H 50; the
  Color Effect's gains 100 ("100 leaves the color unchanged", p.536); Blur's and Mosaic's Opac. 100,
  Hor and Vert 10.
* Acceleration (p.492): "adjusts the speed of an effect over time by having the effect ease in and ease
  out of every keyframe ... from 0 (no ease in and ease out motion ...) to 100 (maximum ease in and
  ease out motion)"; not keyframeable. Ours: between two keyframes the time is blended by that much
  towards a smoothstep (`model::numberAt`, `colorAt`), for every effect that has the parameter.
* Two parameters of one category with the same name (the Color Effect's Invert under Luma Adjust and
  under Chroma Adjust, and others in effect_parameters.json) are stored apart: the later one as
  "<subcategory>: <name>", e.g. "Chroma Adjust Parameters: Invert" (ours; `EffectParameter::name`).

## The effects

* Picture-in-Picture (p.458): "Video on the top layer, where you apply the effect, appears inside the
  video on the bottom layer. On a transition, the incoming video appears inside the outgoing video."
  Ours: the cropped picture is scaled about the frame's centre and moved by Position; Border draws its
  band outside the picture's edge (docs/spec/wipes.md); Foreground Level is its opacity. Dragging it
  and its handles in the Effect Preview monitor (p.89) comes with the monitor's effect tools.
* Flip, Flop and Flip-Flop (p.469): "Flip flips the video image vertically. Flip-Flop flips the video
  image both horizontally and vertically. Flop flips the video image horizontally".
* Field Swap (p.469): "Inverts the fields of any interlaced media": each pair of lines changes places.
* Resize (p.470): "Modifies the size and position of the video. The background color displays where no
  video is present", with Scaling, Position and Crop, as Picture-in-Picture places its picture. The
  guide's "H Pos, V Pos, and Size sliders" are the Position and Scaling parameters it lists.
* Mask (p.470): "Masks out any area of the image that is not inside a rectangle defined by the Scaling
  and Position parameters of the effect. The area surrounding the rectangle is masked by the effect's
  background color." Ours: at 100 the rectangle is the picture.
* Film masks (p.461): "Masks out with a black background any area of the image that is not inside a
  specified rectangle": 1.66, 1.85, 16:9 (16 by 9), Anamorphic (2.35), and Mask (Scaling and Position).
  Ours: the rectangle is as wide as the picture and centred; Scaling and Position change it as they
  change Mask's; the background is the Background Color the effect lists, black until set.
* Blowup (p.462, p.500): Field "0 (leaves the image size unchanged) to 12 (increases the image to the
  maximum size)", X Pos -24 to 24 and Y Pos -18 to 18, after Crop. Ours: Field 12 is three times the
  size; the positions move the part shown up to the picture's edges.
* Reformat (p.229-230, p.479-480): "A Reformat effect works on all tracks below the track to which you
  apply it", its own segment's picture included, so it is usually applied to filler on a track above.
  16:9 Letterbox "Scales the image vertically to the 16:9 aspect ratio within a 4:3 frame"; 14:9
  Letterbox "expands the media horizontally by a fixed amount before scaling it down vertically,
  resulting in a slight horizontal cropping of the image. You control what part of the image is cropped
  with the Position slider"; 4:3 Sidebar "Reformats 4:3 source media to the 16:9 Anamorphic aspect ratio
  by scaling down the media horizontally and applying a black band to the left and right sides". Ours:
  the factors 3/4 high (16:9), 8/7 wide and 6/7 high (14:9), 3/4 wide (4:3 Sidebar); Position H's 0 to
  100 pans the 14:9 crop from the left edge to the right; the bands are the Background Color. Pan and
  Scan (p.230-238) comes with its own tools later.

* Color Effect (p.469, p.533-536). The guide's values are 8-bit video code values, black 16 and white
  235; ours maps black and white of our pictures to those. In order:
  * Luma Range: the Fast Menu's "16 to 235" (the default) or "0 to 255", W Point (default 235) and B
    Point (default 16): "All pixels with that value become white" / "black"; Gamma "-100 to +100 with 0
    being no change", raising the midtones when positive. Ours: the exponent 2^(-Gamma/50).
  * Luma Adjust: Bright and Cont from -100 to +100, 0 no change. Ours: Bright moves by up to half the
    range; Cont scales about the middle, to flat at -100 and steeply at +100. Invert "Reverses the
    image's brightness": ours, the luma turned over and the colour kept.
  * Chroma Adjust: Hue "-180 to +180" degrees round the colour wheel; Sat "-100 removes all saturation
    ... +100 maximizes saturation" (ours: the chroma times 1 + Sat/100); Invert "so that all colors
    display as their complement". Ours: on Rec. 709 colour differences.
  * Luma Clip: High and Low (defaults 235 and 16): "No pixel in the image can be brighter than the High
    value" or darker than Low.
  * Color Style: Post "0 displays all colors and 25 displays the least number of colors" (ours: round(2
    + 254 (1 - Post/25)^3) steps a channel); Solar "0 is normal luminance and 255 inverts all luminance
    values" (ours: values beyond 255 - Solar of the way from black to white are inverted).
  * Color Gain: Red, Green and Blue "0 to 200, where 100 leaves the color unchanged".
  The ChromaHueReverse console command (p.535) is not built.
* Blur Effect and Mosaic Effect (p.468, p.532-536): Opac. is the strength (100 the most), Hor and Vert
  the area's size. Ours, until Intraframe objects (6.9) choose an area: the whole picture, Hor and Vert
  being the blur's radius (three box blurs) or the tiles' size, in pixels of a picture 1920 wide;
  Feathering, Corner Tracking and the Blur/Mosaic Visible pops come with the objects.

## The 2D transitions' Scaling and Position

Center Box, Circle, Diamond and Ellipse take Scaling and Position (p.482), the diagonal Edge Wipes
Scaling (p.461). Ours: Position moves the shape's centre, Scaling stretches it, and it grows until it
covers the picture's farthest corner; the diagonals' X and Y stretch the edge's slope.
