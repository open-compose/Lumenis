# Avid Pan & Zoom (ROADMAP 6.7b2)

A still image seen through a moving, zooming field of view, "similar to a motion control camera" (FX Guide ch.9,
p.239-247; reference p.468, p.528-531). Code: `src/media/pan_zoom.cpp` (`media::panZoomView`,
`panZoomPicture`, the image cache), the compositor's branch, the crosshair and Safe Action in
`src/media/effect_handles.cpp`, Import Image and the locate dialog in `src/ui/effect_editor*.cpp`. Tests:
`tst_pan_zoom`, `tst_effect_keyframes_ui` (panAndZoom). 6.7b2a is the image and its view; 6.7b2b the Velocity and
Path interpolation (`media::panZoomMotion`).

## From the guide

* "Load a sequence and create a placeholder segment ... The footage you use in the placeholder segment is the
  footage the effect uses if you set the Background parameter to Video"; "In the Effect Editor, click the Other
  Options button. A file selection dialog box opens ... Media Composer associates the image file with the effect,
  and superimposes the image on the placeholder segment"; "If you select a file that Media Composer does not
  recognize as an image file, Media Composer takes no action and no error message appears"; "If you move an image
  file after you associate it with the effect ... The next time you open the effect a dialog box opens asking you
  to locate the file" (p.239-240).
* Display: "Source — Shows you the entire image scaled to fit inside the Effect Preview monitor ... The pan and
  zoom settings appear as a rectangle superimposed over the image"; "Target — Shows you the results of your pan and
  zoom settings ... A crosshair appears at the center of the image" (p.241). Show Action Safe: "the safe action
  area appears as a rectangle superimposed on the field of view" (p.529).
* "Zoom Factor ranges from 0.1 to 20. At 0.1, the effect scales the image down to one-tenth the original. At 1,
  the effect applies no magnification to the image"; "The Position parameters refer to the center of the field of
  view. A Position of 0, 0 centers the field of view over the center of the image"; "click the crosshair, and drag
  it to a new location" in either display mode (p.242). "The Avid Pan & Zoom effect automatically preserves the
  clip's aspect ratio."
* Filtering from Real-Time (draft) to Avid Ultra Qual (p.245-246, p.531); Background Color or Video (p.246, p.531);
  Source Has Square Pixels and Source Levels RGB or 601 (p.246-247); the Cache menu (p.247).

## Ours (6.7b2a)

* The image file's absolute path is kept with the effect as "Image" in its parameter category; Import Image (the
  Other Options button) reads it with Qt's image readers. Without a readable image the placeholder segment shows.
  The locate dialog is Qt's own (not the system's), named `panZoomLocate`.
* Geometry: at Zoom Factor 1 the whole image is fitted into the frame (the largest rectangle of its shape,
  centred); a factor of z magnifies z times around the field of view's centre. Position X and Y are that centre's
  offset from the image's centre in percent of its half-width and half-height (sliders -100 to 100; Y down).
* Display Source draws the whole image only in the Effect Preview (`Composition::effectPreview`); elsewhere, and
  playing, the result shows. The Effect Preview overlay draws the field of view, the crosshair (its centre in
  Display Source, the picture's in Display Target) and the Safe Action area (90% of the field of view or picture).
  Dragging the crosshair moves the field of view: by the distance dragged over the image in Display Source, by
  that distance divided by the zoom in Display Target.
* Filtering: Real-Time and Triangle bilinear; Quadratic, Cubic and Gaussian a soft cubic (B-spline); B-Spline
  Catmull and the Avid qualities a sharp cubic (Catmull-Rom); every option samples the halved copy of the image
  nearest above the output's size (mip levels), so shrinking stays clean.
* Source Levels: our pictures are full-range RGB, so RGB images are left as they are and 601 images are stretched
  from 16-235. Source Has Square Pixels changes nothing while our frames have square pixels.
* The cache keeps up to eight images (each with its halvings), read again when the file changes; Free Current
  Cache and Free All Caches forget them (the other Cache entries of p.247 are not in the reference's menu, p.532).
  Use Subres in Preview reads the image at most twice the frame's size on its long side (also when rendering).
* Sliders with decimals (`"decimals"` in effect_parameters.json): Zoom Factor counts in tenths; values are stored
  as numbers with that many decimals.

## 6.7b2b: Velocity and Path

* From the guide: "The Velocity parameters divide the time between keyframes in half"; Velocity In "Controls the
  rate of movement from the selected keyframe to the midpoint between the selected keyframe and the next
  keyframe" (Linear, Constant, Ease In), Velocity Out "from the midpoint ... to the next keyframe" (Linear, Ease
  Out, Constant, Hold); "When you select Constant for Velocity In, Media Composer automatically selects Constant
  for Velocity Out"; Hold: "At the midpoint ... the image freezes ... Movement resumes at the next keyframe";
  Constant "uses the keyframe's Position parameters to determine the path" but not the speed (p.530); "Click the
  keyframe indicator for the keyframe you want to adjust. Click the In Fast Menu button" (p.243); Path Linear
  "straight-line changes", Spline "smooth through all points. If you move a point the effect recalculates the
  entire path" (p.244, p.530). The velocities apply to "changes in the Zoom Factor setting in the same way" (p.242).
* Ours (`media::panZoomMotion`): the motion's keyframes are the times at which X, Y or the Zoom Factor has one,
  with each value as the effect has it there. Each half of the time between two keyframes is a cubic through the
  midpoint at full speed; Ease In starts the first half at rest, Ease Out ends the second at rest; Hold keeps the
  midpoint's place until the next keyframe. A run of keyframes set to Constant moves along the path at one speed
  (by its length in Position units) from the run's first keyframe to the one after its last. Spline is a
  Catmull-Rom curve through the keyframes. The zoom changes by equal factors (1 to 4 passes 2 halfway).
* In the Effect Editor the Velocity menus set the selected keyframes' times (kept as keyframes of the menu), or
  the effect's own value when none is selected; they show the value at the position. Before the first keyframe
  and after the last the values hold.

## Not yet

* Render Mode's Progressive check box (p.245, p.532): not in our parameter file yet.
