# 3D effects (ROADMAP 6.7a)

FX Guide ch.11 (p.287-300), the 3D parameters (p.568-585), the 3D Promote button (p.86). Code:
`src/media/warp3d.cpp` (`media::is3DEffect`, `warp3dPicture`, called from the compositor for segments and
transitions), defaults in `media::parameterDefault`, `src/model/promote3d.*` (`canPromoteTo3D`,
`promoteTo3D`), the Effect Editor's 3D Promote button (`EffectEditor::promoteTo3D`). Tests: `tst_warp3d`,
`tst_compositor` (`stamps`), `tst_effect_keyframes_ui` (`promoteTo3D`).

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
    other border types are 6.7a2's.
  * Defocus, Internal: "blurs the entire image", Horz and V 10 until set (p.572-573); ours, a value of 15
    blurs 15 pixels of a 1920-wide picture.
  * Highlight: "The default manual highlight creates a blurred disk in the center of the image" (p.576); Soft,
    Inty and Rad 50, Angle, Aspct, X and Y as on p.576, in the Highlight Color (white until set). Ours: Rad 100
    is half the picture's diagonal; X and Y ±999 its edges; Aspct ±100 stretches it four times.
  * Shadow: "places a shadow beneath an image" with Opac. 50, X 50 and Y -50 (p.579); ours, an offset of 100
    is 40 pixels of a 1920-wide picture.
* Foreground (3D) (p.573-574): Opac. (100 until set), Swap Sources ("Switches the foreground and background
  sources"), Reverse Animation ("the entire effect to be reversed, including the direction of movement as
  well as the incoming and outgoing sources"). The key types are 6.7a2's.
* On a transition "Media Composer applies the effect to the incoming video. To apply the effect to the
  outgoing video, click the Swap Sources button" (p.294): the incoming warped over the outgoing.
* 3D Promote: "Promotes the effect to a 3D effect. Available for some effects, including titles,
  Picture-in-Picture, and imported matte keys" (p.86); "You cannot demote a 3D effect to a 2D effect (although
  you can immediately undo the promotion)" (p.289). Ours: the button is on for a Picture-in-Picture (titles
  and imported matte keys come with them); the 3D Warp takes its Position (Y turned up), Scaling (its 50
  made explicit), Crop and Foreground Level as Opac., keyframes and all, and looks the same. Since 6.7d2 the
  Stabilize effect promotes too (p.277), and tracking data comes along (docs/spec/tracking.md).

## 6.7a2: shapes, key types and border types

* "3D shape effects let you map the foreground video channel to various geometric shapes. You can create either
  a Page Fold, a Ball, or a Slats shape effect" (p.579). A shape bends the plane, which is then drawn as a fine
  mesh of triangles into a depth buffer (both sides seen). The Xpress 3D effects 3D Ball, 3D Page Fold and 3D
  Slats have their shape; 3D Warp's Shape category is on once set, a Page Fold until another Type is chosen.
  Corner Pin draws the picture flat.
* "Each shape effect contains a default template ... when you first apply a Page Fold shape effect, Media
  Composer creates a simple page curl" (p.580). Ours: until set, the Page Fold's Curl, the Slats' Ampl and the
  Ball's Radius run over the effect, taking the picture off the screen on a segment and bringing the incoming
  one on over a transition.
* Page Fold (p.581-583): "Curl ... 0 represents no rollup"; "animate the Curl value from 0 to 100 and keep the
  Radius parameter at its default value" to go "from a flat, centered image to one that is completely off the
  screen". Radius 0-100 (15; ours, 100 is a quarter of the picture's height); Angle -180 to 180 (45), which
  edge or corner folds as in the pictures on p.583 (0 the top edge, 90 the right, -90 the left, 45 the top
  right corner); Tuck and Flap: Tuck Over (the flap over the top, the default), Tuck Under, Show Top Only,
  Show Bottom Only. The page rolls over a cylinder of the radius and, past half a turn, lies flat on top.
* Ball (p.580-582): "a rubbery, rectangular sheet stretched around a sphere ... If the radius is small, the
  corners disappear, wrapping completely around the sphere's surface"; Curve "A value of 0 looks like a flat
  circle"; X Pos and Y Pos ("Values increase as you go down"), Aspct and Angle. Ours: Radius 100 is half the
  picture's diagonal, Curve 50 until set, Aspct ±100 a stretch of four.
* Slats (p.581-582): "alternating bands of video that enter the screen from opposite directions"; Angle "0 for
  horizontal and 90 for vertical". Ours: Freq is how many slats cross the picture (4 until set), Ampl 100 moves
  them a diagonal's length (off the screen), Offset 100 a slat's width, Sk Amp weaves them in depth (100: a
  slat's width) and Sk Angl tilts each in degrees.
* The Shape's Highlight menu (p.581): Auto Highlight (until set) shades the bent surface by how it faces the
  viewer, at the Highlight category's Inty when that is on; Manual Highlight draws the Highlight category's disc.
  Reverse Manual Highlight is drawn as Manual for now.
* Foreground (3D)'s key types (p.574-575), keyed before the picture is placed: Luma Key ("All brightness values
  above the Thresh value display"; with Lum Range, "Thresh becomes the median point of a range"), Chroma Key
  (Color, H Tol., H Soft, S Low, L Tol., Lum Range and Range), SpectraMatte Key (the SpectraMatte effect's
  parameters, docs/spec/keys.md); Invert and Show Alpha. Ours: Thresh, Soft and Range in percent of the
  brightness (50, 10 and 20 until set); H Tol. and H Soft in percent of half the colour wheel (20 and 10);
  S Low in percent of the saturation (20); L Tol. keeps what is brighter (100: nothing); blue until a colour is
  set. Invert on a Full Key turns the picture's own transparency around.
* Border (3D)'s types (p.570-571): "The default border color is black for Plain borders and shades of gray for
  other border types." Ours: Metal a brushed grey, Bevel 1 lit from the top left and Bevel 2 from the bottom
  right, 3D Frame both in turn, Round a rounded profile, Video the picture itself at half brightness.

## 6.7a3a: Trail, Erode/Expand and Spill Suppression

* Trail: "Use Trail to add a trail to an effect as it moves across the screen. You do not see the trail when you
  step through the effect because it is a cumulative effect" (p.585); Decay 0-100 (75). Ours: the effect as it
  was on the frames before, each copy Decay percent as opaque as the one after it until they fade below 5%,
  worked from the effect's own times (so stepping shows it too) with the foreground as it is now; Use Sparkle
  breaks the copies into glittering dots. "The Stamp, Shadow, and Trail parameters are mutually exclusive"
  (p.585): with more than one set, Shadow wins, then Trail.
* Erode/Expand "controls the blending of the edges on 3D Warp effects. This parameter is not keyframeable"
  (p.573): Filter 1-7 pixels each side, Center 0 (most erosion) to 255 (most expansion), Soft 0-255. Ours: the
  placed picture's alpha blurred over Filter pixels of a 1920-wide picture and cut at a level from 0.98
  (Center 0) to 0.02 (255), softened by Soft.
* Spill Suppression "replaces one color with another color" (p.584): Target Color and Replace Color, Use Key
  Color (the Chroma Key's), Gain and Soft 0-63. Ours: green and grey until set; Gain the hue tolerance up to a
  quarter of the colour wheel, Soft a blend over up to another quarter; colours of low saturation are left.

## 6.7a3b: Stamp

* "The Stamp category of the 3D Warp effect lets you make certain effects — such as titles and Matte Key effects
  — remain on screen while other effects come and go, even though the effect segment itself has ended"; "Each
  one has Stamp enabled on the last keyframe, which lets each element remain in place as the next element
  appears on the screen ... you could add additional stamps until you enable Clear to clear the Stamp buffer"
  (p.298-299). Stamp Image "Lets you add the stamp on one or more keyframes"; Clear "Lets you clear the stamp
  buffer of everything in it"; Opac. 0-100 (100); "Foregnd. On Top: Switches the image in the Stamp buffer to
  the background. If Foregnd. On Top is not enabled, the Stamp buffer is always in the foreground" (p.585).
* Ours: the Effect Editor's Stamp Image and Clear buttons keep each press at the position's time in the effect
  (the Stamp category's "Stamps" and "Clears"; `EffectEditor::stampImage`, `clearStamps`, one undo step each).
  The compositor keeps a stamp buffer for each track (`stampsOf` in compositor.cpp): every press on the
  track's 3D effects up to the frame, after the last Clear, each the effect's picture as it was at that moment
  (`media::warp3dLayer`) at the Stamp's Opac., over the track's own picture, or under it with Foregnd. On Top.
  A Clear on the same frame as a stamp clears it. With any stamp on the shown tracks every frame is composed,
  as the stamp may outlast its segment. Stamps are drawn on the fly ("You cannot render Stamp effects").

## 6.7a4: direct manipulation

* 2D effects: "You can make some adjustments to effects by manipulating them directly in the Effect Preview
  monitor. These adjustments use handles and wire-frame outlines that you can display in the monitor ... You
  can define a motion path on multilayer video effects such as Picture-in-Picture effects. Use the wire-frame
  preview feature when you want to see the path an image takes on the screen. You can also use the resize
  handles to set the size of the effect image at each keyframe location"; "Create keyframes where you need
  them, make sure that the appropriate keyframe is selected, and then manipulate the wire frame and handles ...
  Drag the wire frame to reposition the effect. Drag the resize handles to resize the image — the top handle
  adjusts the height of the effect image, the side handle adjusts the width, and the corner handle adjusts
  width and height together, maintaining the aspect ratio"; Reduce shows "the area outside the background
  image and lets you extend the motion path into that area"; "Each dot on the path represents the location of
  the center of the PIP at a keyframe. The center handle and the resize handles are highlighted" (p.92-93).
* 3D effects: "Various 3D effect handle buttons appear along the right side of the Effect Editor": Corner Pin,
  Crop, Scale, Axis, Z Rotation, X Rotation, Y Rotation, XY Position, XZ Position. "click one of the image
  manipulation buttons to activate the specific handles ... Click the handles, and drag them ... Click the
  image manipulation button a second time to turn off the handles"; "Use the Outline/Path button when you want
  to see the path a moving effect takes on the screen"; p.293's illustrations show the Scale handles (top, side,
  corner), the Axis cross, X Rotation's handles above and below the centre, Y Rotation's left and right, and Z
  Rotation's on a line to a corner (p.292-293).
* Ours, the geometry (`src/media/effect_handles.*`): the outline is where the effect puts its foreground: the
  2D placement of Picture-in-Picture, Resize and the masks (as image_effects.cpp draws them), and for 3D effects
  the cropped plane projected through the hierarchy, Perspective and Target (`warp3dScreenPoint`, the inverse
  of the renderer's ray-plane mapping; a shape's flat plane before it is bent; Corner Pin's homography). A
  handle drag is solved numerically: the parameters the handle stands for (Position X/Y, Scaling X or Y, one
  factor on both for the corner, a Crop edge, Axis X/Y) change until the handle lies under the pointer, so it
  follows through rotation and perspective. Corner Pin's corners follow the pointer directly; the first drag
  of an effect without Corner Pin pins all four where they are seen. X and Y Rotation turn half a turn per
  picture height or width dragged (the top handle dragged down tips the top towards the viewer; the right
  handle dragged left takes the right edge away), Z Rotation by the angle swept about the centre, clockwise
  positive (p.291). XZ Position: X follows the pointer, dragging down brings the picture closer (two units
  of a 1920-wide picture per pixel).
* Ours, the Effect Editor (`effect_editor_handles.cpp`): in Effect mode a 2D effect with an outline always shows
  its wire frame and its centre, top, side and corner handles; a 3D effect shows those of the handle button
  selected (lettered buttons, shown only for 3D effects); Outline/Path adds the outline and the centre's path
  over the effect with a dot at each keyframe time. Values are rounded to whole numbers within the controls'
  ranges and go where a control's value would (the selected keyframes of their parameter, a keyframe at the
  position when the parameter has keyframes but none is selected, else the plain value); Fixed Aspect does not
  gang them. A drag is shown as it goes (the outline and the controls) and stored as one undo step on release.
  The Composer draws the overlay over the Record monitor's picture (also beyond it, when reduced) and reports
  drags (`Composer::effectHandleDragged`); pressing inside the outline drags the centre handle.

## Not yet

* Real-time playback limits (p.290): our 3D effects render on the fly like the others.
