# 3D effects (ROADMAP 6.7a)

FX Guide ch.11 (p.287-300), the 3D parameters (p.568-585), the 3D Promote button (p.86). Code:
`src/media/warp3d.cpp` (`media::is3DEffect`, `warp3dPicture`, called from the compositor for segments and
transitions), defaults in `media::parameterDefault`, `src/model/promote3d.*` (`canPromoteTo3D`,
`promoteTo3D`), the Effect Editor's 3D Promote button (`EffectEditor::promoteTo3D`). Tests: `tst_warp3d`,
`tst_effect_keyframes_ui` (`promoteTo3D`).

## 6.7a1: the 3D Warp renderer and 3D Promote

* "All the 3D effects features are accessible through the 3D Warp effect in the Blend category" (p.288); the
  Xpress 3D effects (3D Ball, 3D PIP, 3D Page Fold, 3D Slats, p.288, p.489) are the same effect with fewer
  parameter categories. Ours: one renderer for all of them.
* The foreground is a flat plane in 3D, placed by the parameters in the order of "the hierarchy of 3D effects
  parameters (from higher to lower) ... Target, Perspective, Position, Rotation, Axis, Scaling, Crop"
  (p.569): Crop cuts the picture's edges, Scaling and Skew size and shear it, Rotation turns it about the Axis
  (X, then Y, then Z), Position moves it, Perspective sets where it is seen from, and Target moves and sizes
  the whole result ("Post-transformation parameters, such as Target, move the entire path"). Each pixel of
  the result is a ray from the viewer met with the plane; where the plane is not seen (edge on, behind the
  viewer, outside the crop) the background shows.
* Directions: "X refers to the image's left/right direction (values increase from left to right). Y ... up/down
  (values increase from bottom to top). Z ... front/back (values increase from back to front)" (p.290);
  "A positive number of degrees refers to a clockwise rotation" (p.291).
* Units (ours, as for the 2D effects): Position, Axis and Target in pixels of a picture 1920 wide; Crop's
  edges and Skew from -999 to 999 across the picture (Crop: "Values range from –999 (bottom) to 999 (top)",
  p.572, so 999, -999, -999, 999 for T, B, L, R until set); Skew of ±999 a shear of 1. The viewer stands two
  picture widths in front of the screen; Perspective X and Y move it, Z (positive closer) brings it in, to a
  fifth of a width at most.
* Corner Pin (p.571): "Corner 1 — top left corner of the untransformed image" to Corner 4, bottom left; "If the
  image is cropped, Media Composer uses the corners of the cropped image." Ours: each corner's X and Y from
  -999 to 999 across the screen, where the picture's own corners are until set; with Corner Pin set the
  other geometry is not used. The parameter file lists X and Y under each corner.
* A parameter category is on once one of its values is set ("When you enable Defocus ...", p.572; ours, as the
  Effect Editor stores values):
  * Background Color (3D): "replace the bottom video track with a background color" (p.570).
  * Border (3D): the Plain type, a band inside the cropped edges; Wid 0-100 (50), ours 40 pixels of a
    1920-wide picture at 100; Sft 0-255, ours 25 pixels at 255; black until a colour is set (p.570-571). The
    other border types wait for 6.7a2.
  * Defocus, Internal: "blurs the entire image", Horz and V 10 until set (p.572-573); ours, a value of 15
    blurs 15 pixels of a 1920-wide picture.
  * Highlight: "The default manual highlight creates a blurred disk in the center of the image" (p.576); Soft,
    Inty and Rad 50, Angle, Aspct, X and Y as on p.576, in the Highlight Color (white until set). Ours: Rad 100
    is half the picture's diagonal; X and Y ±999 its edges; Aspct ±100 stretches it four times.
  * Shadow: "places a shadow beneath an image" with Opac. 50, X 50 and Y -50 (p.579); ours, an offset of 100
    is 40 pixels of a 1920-wide picture.
* Foreground (3D) (p.573-574): Opac. (100 until set), Swap Sources ("Switches the foreground and background
  sources"), Reverse Animation ("the entire effect to be reversed, including the direction of movement as
  well as the incoming and outgoing sources"). The key types other than Full Key, Show Alpha and Invert wait
  for 6.7a2.
* On a transition "Media Composer applies the effect to the incoming video. To apply the effect to the
  outgoing video, click the Swap Sources button" (p.294): the incoming warped over the outgoing.
* 3D Promote: "Promotes the effect to a 3D effect. Available for some effects, including titles,
  Picture-in-Picture, and imported matte keys" (p.86); "You cannot demote a 3D effect to a 2D effect (although
  you can immediately undo the promotion)" (p.289). Ours: the button is on for a Picture-in-Picture (titles
  and imported matte keys come with them); the 3D Warp takes its Position (Y turned up), Scaling (its 50
  made explicit), Crop and Foreground Level as Opac., keyframes and all, and looks the same.

## Not yet (6.7a2)

* The shapes (Ball, Page Fold, Slats and the Shape category's curl and wave, p.579-583), Trail, Stamp,
  Erode/Expand and Spill Suppression; the Luma, Chroma and SpectraMatte key types; the other border types.
* Direct manipulation: the handle buttons (Corner Pin, Crop, Scale, Axis, Rotation, Position) and their
  handles in the Effect Preview monitor, and Outline/Path (p.292-293).
* Real-time playback limits (p.290): our 3D effects render on the fly like the others.
