# The Key effects (Phase 6.3c)

Pages are the FX Guide's. "Key effects combine two or more video streams, using components from one of
the streams" (p.472). The reference says what each control does and its range, but gives no formulas;
the mattes below are ours and say so. AniMatte (p.472) comes with the Intraframe tools (6.9).

Code: `detail::lumaChromaKeyPicture`, `rgbKeyerPicture`, `spectraMattePicture`
(`src/media/key_effects.cpp`), reached through `media::imageEffectPicture`; Matte Key is
`matteKeyPicture` in `src/media/compositor.cpp`, which keeps what lay under the track below for it. The
values until set are in `media::parameterDefault`. Tests: `tst_compositor` (`keys`, `keysInSequences`),
`tst_effects` (`effectDefaults`).

## How a key works here

A key makes a matte (0 keyed out, 1 kept) for the segment's picture on the upper track; the picture times
its matte goes over the tracks below. Applied to a transition (Chroma Key, p.473), ours: the incoming
video keyed over the outgoing. Colours are compared in Rec. 709 luma and colour difference (Cb, Cr).

Shared, from Foreground (p.495): Invert Key reverses the matte; Show Alpha "Displays the grayscale alpha
channel used to apply the key effect" (the matte, opaque, white kept); Level is the foreground's
opacity. Luma Key and Chroma Key also take Scaling, Position and Crop, placed as Picture-in-Picture
places its picture but at normal size until set (docs/spec/image-effects.md); Matte Key's wait.

Values until set (ours, so a key keys something at once): Chroma Key, the RGB Keyer and SpectraMatte
key blue, Luma Key black; Gain 20 and Soft 10 for Luma Key, Chroma Key and the RGB Keyer; the RGB
Keyer's Level and SpectraMatte's Opacity 100.

## Luma Key and Chroma Key (p.473, p.548-550)

* Key "selects the primary key color to be replaced by video"; Gain "Specifies how much of the
  foreground and the background video displays. Values range from 0 (shows only the foreground) to 63
  (replaces all the foreground video with the background video)"; Soft: "Colors that border the
  luminance or chroma specified for the key display as a blend of the foreground and the background
  video". Not keyframeable.
* Ours: a pixel is keyed when its distance from the key colour is under Gain/63 of the largest, and
  blended over Soft/100 of a quarter of that range beyond. Luma Key measures luma; Chroma Key measures
  Cb and Cr, in units of the key colour's own chroma (twice it being the largest).
* Chroma Key's Secondary Key "selects a second background color to key out" with its own Gain and Soft;
  Spill Suppression "neutralizes the selected color without affecting the luminance. The spill color
  changes to a grayscale" (p.549-550). Each acts once its colour is set; spill greys colours by how near
  they are to its colour, as the primary key would key them. The Profile display is not built.

## Matte Key (p.474)

"A three-layer track effect. The bottom layer is the background image, the middle layer is the
foreground image, and the top layer contains the grayscale matte or alpha channel." Applied to the
matte's segment on the top track. Ours: the matte's luma is the foreground's opacity over what lies
under it; Invert Key, Level and Show Alpha apply.

## RGB Keyer (p.474, p.550-551)

* Key Color; Gain "Adjusts the threshold of the key. Values range from 0 to 100"; Soft "Softens the
  edge"; Show Alpha; Spill "Suppresses the spill from the key color onto foreground objects"; Level
  "Controls transparency of the foreground elements"; Reverse "Inverts the key"; Blur "from (a blurring
  of 0 pixels) to 100 (a blurring of 10 pixels)", a box filter; Erode "Decreases or 'erodes' the outer
  edges of the blurred matte"; Hue, Sat, Bright and Cont for "post-key color correction of the
  foreground elements".
* Ours: the distance in RGB from the key colour, keyed under Gain/100 of the largest with Soft/100 of a
  quarter blended; Spill takes that share of the key colour's strongest channel down to the others'
  level; Blur's pixels and Erode's (Erode/10) are of a picture 1920 wide; the colour correction is the
  Color Effect's (docs/spec/image-effects.md).

## SpectraMatte (p.474-475, p.551-561)

* The key colour defines "a wedge keyed out of the CbCr swatch" (p.552). Tolerance is the wedge's angle,
  0 to 180, default 90; Key Saturation (0 to 100, default 0) moves the wedge's point from the centre to
  the key colour's saturation; Key Sat Line (default 0) blunts the point with a line across the wedge,
  from the Key Saturation (0) to the key colour (100); Inner Softness (0 to 100, default 10): "When
  Inner Softness is 0, the gradient extends inwards from the edges of the wedge all the way to the Key
  Color value. When Inner Softness is 100, the gradient extends only a small percentage of the way";
  Outer Softness (-100 to 100, default 0) narrows or widens the blend outside the wedge; Alpha Offset
  (-100 to 100): "Negative values expand the key color wedge"; Opacity (0 to 100) is the foreground's
  (p.554-556).
* Suppress Shadow raises darker pixels' saturation "for the purposes of calculating the key" (p.557).
* Matte Blur 0 to 100 with Blur, Erode (inside the edge) or Dilate (outside) (p.558).
* Spill: Spill Saturation (0 is the Key Saturation, 100 the centre) and Spill Angle Offset (degrees
  added on either side of the Tolerance) make a zone around the wedge where "Color values within the
  zone are replaced by color values on the 'safe' side of the zone" (p.559-561).
* Bypass: "you see the original foreground image"; Show Alpha shows the matte.
* Ours: a colour's place in the wedge is the smaller of how far it is inside the wedge's sides (1 on its
  centre line, 0 at a side) and how far it is along from the Key Sat Line to the key colour. The matte
  ramps from 1 at -Outer/200 to 0 at 1 - 0.95 Inner/100, then Alpha Offset/100 is added. Matte Blur's
  radius is Matte Blur/10 pixels of a picture 1920 wide. Spill turns a colour in the zone to the zone's
  edge, keeping its saturation.
* Not built: the SpectraGraph displays and their Source monitor menu, Enable Luma Curve and the Luma
  Curve (it needs the keyframe graph as a luma axis), and Soften Alpha Saturation (on: the corners of
  ours are already soft).
