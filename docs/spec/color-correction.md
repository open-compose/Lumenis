# Colour correction

ROADMAP 6.10. FX Guide (2025.12) ch.19-24, p.587-733. Code: `src/model/color_correction.*` (what a correction holds),
`src/media/color_correction.*` (its picture), the Color Correction effect in `src/media/compositor.cpp`. Test:
`tst_color_correction`.

## 6.10a1: corrections and the HSL group

### From the guide
* Groups and subdividing tabs (p.598-602): the HSL group with its Controls and Hue Offsets tabs (and, with the
  Symphony option, Luma Ranges; the Controls tab's Master, Highlights, Midtones and Shadows tabs); "Media Composer
  applies adjustments that you make in each group cumulatively" (p.602).
* Enable buttons (p.603-605): every group, tab and control has one; adjusting a control turns on its button and those
  above it; a button turned off stops everything under it while the values stay; Alt+click resets a button's
  controls and everything below. "Adjustments to the Gain, Gamma, and Setup controls in the Hue Offsets subtab ...
  enable both the Hue Offsets and the Controls subtabs" (p.603).
* The Controls tab (p.633-634): Invert Chroma (as Hue 180), Hue -180 to 180, Saturation 0 to 200 (100 unchanged, 0
  monochrome), Brightness -100 to 100 (added to every pixel's luminance), Contrast -100 to 100 (-100 neutral grey),
  Invert Luma, Gain 0 to 200 (a percentage), Gamma 0.1 to 10 (raising it lightens the midtones), Setup -255 to 255,
  Clip Low and Clip High (16 and 235: pixels at or past them clipped to black or white).
* The Hue Offsets tab (p.635-639): ChromaWheels for Shadows, Midtones, Highlights and Master; "Red is at the 0° point
  on the wheel ... green is at +120° ... blue is at –120°"; Amount 0 to 100 from the centre out.
* Luma Ranges (p.640-643): three curves of percentage over brightness 0-255, "brightness level 175 is defined as 50%
  highlight, 50% midtone, and 0% shadow. Brightness level 215 is defined as 95% highlight, 5% midtone".

### Ours
* `model::ColorCorrection` holds control values by path ("hsl/controls/master/hue", "hsl/hueOffsets/shadows/amount",
  "hsl/lumaRanges/highlights" as [in, out] points) and the Enable buttons that are off; unset controls have their
  defaults (`colorControlDefault`) and do nothing. The Hue Offsets' Gain, Gamma and Setup are stored as the Controls
  tab's Master ones. The Color Correction effect keeps its correction under its "Color Correction" category.
* The picture: Y'CbCr (Rec. 709) with Y' on the 8-bit video scale. Invert Chroma and Luma first; then Master's
  controls, then Shadows', Midtones' and Highlights' each blended by the pixel's share of that range; then the wheels
  (Amount 100 a quarter of the chroma range, each by its range's share, Master on all); then the clips. Hue rotates
  the chroma; Saturation scales it; Contrast scales about mid grey (100 a slope of 20) and fades the chroma towards -100;
  Brightness adds in 8-bit steps; Gain, Gamma (l^(1/gamma)) and Setup act on the 16-235 range.
* The default Luma Ranges curves are smooth steps fitted to the guide's two figures (highlights from 119 to 231,
  shadows the mirror image, midtones the rest); the three curves' values are made to add up to one.

## 6.10a2: the Channels, Levels and Curves groups

### From the guide
* The group tabs are HSL, Channels, Levels, Curves and Sec (p.599); adjustments in each group are applied
  cumulatively (p.602).
* Channels (p.644-648): each output channel is a blend of four components, each a percentage from -200 to 200 and a
  Component Type: Red, Green, Blue, Luma, Cr, Cb, Offset ("A constant value that adjusts all the other values"), and
  Invert ("20% Green – is the same as –20% Green"). The default rows (p.645's illustration): 100% of the channel itself,
  0% of it twice, 0% Offset. "The total of the percentages you set for a channel does not need to be 100."
* Levels (p.648-659): tabs Composite, Luma, Red, Green, Blue, Master; black, gray and white points in and out, 16, 128
  and 235 by default, 0-255; Composite and Luma only black and white points (p.657). Input black 60 maps 60 and below to
  16 and spreads 60-235 over 16-235; input gray 100 maps 100 to 128 and brightens; output gray 100 darkens; the curve is
  "steeper near the black point and somewhat shallower near the white point" for a raised gray point (p.649-656). A
  flat line at 128 on all three channels makes a uniform mid grey.
* Curves (p.659-667): Red, Green, Blue and Master graphs, input to output, the 45 degree line by default with end points
  that can move; 2 to 16 control points; close points anchor part of the curve (p.663). The Master Saturation, Gain,
  Gamma and Setup sliders are the HSL group's and enable it (p.659).

### Ours
* Paths: "channels/<channel>/<1-4>/amount|type|invert", "levels/<tab>/inBlack|inGray|inWhite|outBlack|outGray|outWhite",
  "curves/<graph>" (points) and "curves/masterSaturation|masterGain|masterGamma|masterSetup" (stored as the HSL
  Master controls).
* Groups apply in the tab order, HSL in Y'CbCr and the others on R'G'B' (0-1), each only when something in it is
  adjusted.
* Channels: components on 0-1; Luma is Y', Cr and Cb are centred on 0.5 (neutral is mid grey, as their monochrome
  previews show), Offset 100% adds full scale. A channel with any adjusted component takes its whole row, defaults for the
  rest; the result is clipped to 0-1.
* Levels: R'G'B' on the 8-bit video scale (0-1 as 16-235). Between the black and white points a power curve y = x^k
  through the gray points (`model::levelsValue`). Master on all channels, then Red, Green and Blue, then Luma on Y', then
  Composite: Y' minus and plus the chroma's amplitude (224 steps for the full chroma range) both go through the levels,
  and Y' and the chroma follow them.
* Curves: on 0-255; a monotone cubic through the points (Fritsch-Carlson), flat past the end points; Master first, then
  each colour's graph.
* The Color Correction tool (6.10b) and the Secondary group (6.10a3) come later.

## 6.10a3: the Secondary group

### From the guide
* Twelve vectors: six standard (Green, Yellow, Red, Magenta, Blue, Cyan) and six custom (p.670). Standard vectors are
  wedges with no upper saturation limit and constant hue ranges; custom vectors are ellipses whose "hue ranges scale with
  the saturation level" (p.670). The standard Red vector covers 30 degrees; custom vector 3 has saturation 35 and
  saturation width 40 (p.671).
* Input Vector sliders (p.675-676): Hue (0 red, ±180 cyan), Saturation 0-100 from centre to edge, Hue Width (0-180
  standard, 0-100 custom), Saturation Minimum (standard), Saturation Width (custom), Softness -255 to 255, 30 by
  default ("Positive values blend the corrected color into areas of the image beyond the limits ... Negative values
  blend the uncorrected color into parts of the area defined by the input vector"). Lock Vectors moves the Output Hue
  and Saturation with the Input's (p.676).
* Output Vector (p.680-681): Hue, Saturation, and Luminance -10 to +10 (10% of the black-to-white range).
* The syringe (p.678): selects and enables the closest vector, adjusts its input to hold every sampled colour, sets
  the output to the input and selects Isolate. Add to and Subtract from Selection grow or shrink the vector within its
  shape (p.679).
* Isolate shows what is outside the selected vector in grey, after the output adjustment (p.679-680). Invert Last
  Vector inverts "the enabled vector furthest to the right" (p.673, p.682). Vectors apply cumulatively from left to
  right (p.683).

### Ours
* Paths: "secondary/vectors/<vector>/input/hue|saturation|hueWidth|satMin|satWidth|softness",
  "secondary/vectors/<vector>/output/hue|saturation|luminance", "secondary/isolate", "secondary/invertLast",
  "secondary/selected" (Green by default, p.674's illustration). The output's hue and saturation follow the input's
  until set, so a vector changes nothing until its output does. A vector is enabled when anything in it is set.
* The wheel: hue from red counterclockwise (green +120, as the ChromaWheels), saturation 100 at half the chroma range
  (`media::wheelHueSaturation`).
* The predefined centres: the standard vectors at saturation 60 with Saturation Minimum 10; custom 1 hue 25 sat 35
  (skin), 2 hue 100 (foliage), 3 hue -150 (sky), 4 hue 45 sat 50, 5 hue -90, 6 hue 150; custom Hue Width 30.
* Membership: a distance outside the vector in its own size (wedges: 90 degrees of hue or 50 of saturation; ellipses:
  the normalized radius, with Hue Width the angle the ellipse spans as seen from the wheel's centre); Softness 255 is
  one size of blending outside (positive) or inside (negative), linear.
* The output moves each member colour by the vector's change of hue and saturation (added), and its luminance; blended
  by membership. The Secondary group comes after Curves, in Y'CbCr.
* The syringe picks the predefined centre nearest the samples' mean, with 2 degrees and 2-4 of saturation to spare;
  Subtract shrinks whichever of the hue or saturation range needs the smaller change.
