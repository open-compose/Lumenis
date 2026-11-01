# Avid Pan & Zoom (ROADMAP 6.7b2)

A still image seen through a moving, zooming field of view, "similar to a motion control camera" (FX Guide ch.9,
p.239-247; reference p.468, p.528-531). Code: `src/media/pan_zoom.cpp` (`media::panZoomView`,
`panZoomPicture`, the image cache), the compositor's branch, the crosshair and Safe Action in
`src/media/effect_handles.cpp`, Import Image and the locate dialog in `src/ui/effect_editor*.cpp`. Tests:
`tst_pan_zoom`, `tst_effect_keyframes_ui` (panAndZoom). 6.7b2a is the image and its view; 6.7b2b the Velocity and
Path interpolation.

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

## Not yet (6.7b2b)

* Velocity In and Out at each keyframe (Linear, Constant, Ease In, Ease Out, Hold) and Path Linear or Spline
  (p.242-245, p.530).
* Render Mode's Progressive check box (p.245, p.532): not in our parameter file yet.
