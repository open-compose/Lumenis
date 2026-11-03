# PlasmaWipe effects

ROADMAP 6.9e. FX Guide (2025.12) ch.16, p.442-455, and the Effects Reference p.478. Code: `src/media/plasma_wipe.*`,
called from `src/media/compositor.cpp` (transitions and segments); the effects are added to the Effect Palette by
`EffectCatalog::builtin()` (`src/ui/effect_palette.cpp`). Test: `tst_plasma_wipe`.

## From the guide
* "PlasmaWipe effects are two-stream, real-time effects that use gradient image bitmaps to create wipes and segment
  effects. There are 64 preset effects, and you can create your own by creating new gradient images" (p.442).
* "PlasmaWipe effects uses the Level parameter value to define a threshold point that changes gradually from black to
  white. Media Composer replaces gray tones in the gradient image that are lighter than the threshold point by outgoing
  video, and gray tones that are darker than the threshold point by incoming video" (p.442). On segments they are
  "static wipes" (p.443).
* The images are raw greyscale files, 720x576 for PAL and 720x486 for NTSC, in parallel folders of a PlasmaWipes folder;
  "The name you choose for the image becomes the name of the effect"; folders inside the 720x486 and 720x576 folders
  organise them, and "Media Composer system displays the full path as part of the name in the effect category"; new
  effects appear after a restart (p.443-445, p.450-451).
* Frame and border effects (p.452-455): frames are segment effects only; "the PlasmaWipes software looks for the words
  border and frame in the pathname ... If both words appear in the path, the software applies the frame effect
  defaults"; frames have crisp edges; "Border adjusts the base color of the frame. Blend Color adjusts the highlight
  color"; "Foreground video displays through the black area and background video displays through the white area";
  frame images are 4:3, resized by the nearest neighbour.
* Parameter categories (p.478): Chroma Key Profile, Secondary Key and Spill Suppression; Border; Foreground;
  Acceleration; Grid.

## Ours
* Avid's 64 presets are Avid's artwork and are not shipped. Our own effects are generated (histogram-equalized so that
  they use the whole range, p.445, p.448): PlasmaWipe Basic Horiz (Linear Ramp, Twirl), Center (Radial, Diamond,
  Clock), Techno (Clouds, Ripples) and Borders (Soft Edge Border, and Square Frame with the frame defaults).
* PlasmaWipes folders: the application's `SupportingFiles/PlasmaWipes`, the folders in `LUMENIS_PLASMAWIPES` (the
  platform's path list) and `media::setPlasmaWipeFolders`; the 720x576 folder when there, else 720x486. A file
  `PlasmaWipes/720x576/My Effects/Wipes/Fog.raw` is the effect Fog in the category "PlasmaWipe My Effects Wipes"; a
  file directly in the resolution folder is skipped. A raw file is 720 pixels wide and 576, 486 or 540 lines high (its
  size says which); others are not read. A sequence moved to a system without the image shows its incoming picture.
* The threshold runs from just below black at Level 0 to just above white at 100, so that the ends show only one
  picture; on a transition it follows the transition's progress (or Foreground Level when keyframed, as the other
  transitions do). Border Soft (0-255) spreads the edge over up to a quarter of the gradient's range; Width (0-100)
  adds a band of up to a quarter in the Border Color, blended towards the Blend Color across it.
* Segments: the segment over the tracks below at Foreground Level, 50 until set. Frames: the segment where the image is
  darker than Level (5 until set), the tracks below where it is white, the frame between in the Border Color blended to
  the Blend Color by its grey (grey and white until set), Soft 0.
* The parameter categories are Acceleration, Border, Foreground and Grid; the Chroma Key Profile category of p.478 is
  left out, nothing in the chapter using it.
