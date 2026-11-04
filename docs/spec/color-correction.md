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

## 6.10b1: Color Correction mode and its monitors

Code: `Segment::sourceCorrection`/`programCorrection`, `model::segmentCorrection`, `correctionShotStart`,
`uncorrectedShotStart` (`src/model/color_correction.*`); `Composition::corrections` and `segmentPicture` in
`src/media/compositor.cpp`; `Timeline::setCorrectionMode` and the correction commands; `Composer::setCorrectionMode`,
`playOnce` and `src/ui/correction_monitors.*`. Tests: `tst_color_correction` (segmentCorrections),
`tst_correction_mode`, scenario `38-color-correction.json`.

### From the guide
* Relationship corrections have Source and Program sides; "You can make both a Source correction and a Program
  correction on a segment", and then "Media Composer makes a single adjustment using the merged values from the Source
  and Program sides" (p.596-597). "Src (Source) Segment: Corrects only the current segment"; "Prog (Program) Segment:
  Corrects only the current segment" (p.597). "Media Composer processes relationship color corrections before all
  other effects, with the exception of Timewarp effects" (p.597).
* "To enter Color Correction mode ... Click the Color Correction Mode button, for example, in the Timeline palette. To
  exit Color Correction mode ... Click a mode button other than Color Correction, for example, the Effect Mode button"
  (p.589).
* Three monitors; by default "the center monitor shows the frame at the current location of the position indicator,
  while the left and right monitors show the last frame of the segment preceding the current segment, and the first
  frame of the segment following the current segment" (p.590). Source menus: Empty, Entire Sequence, Reference, Current
  (not offered when another monitor is Current), Previous, Next, Second Previous, Second Next, and the waveform and
  vectorscope displays (p.591). Reference Current from the right-click menu; the reference shows in the right monitor
  (p.591, p.594). Escape switches between the centre and right monitors (p.593).
* Under each monitor: Dual Split, Go to Previous Uncorrected Shot, Go to Next Uncorrected Shot, Remove Effect (p.592).
  Dual Split shows the uncorrected image on the left of a box with triangular handles (p.592).
* Play Loop "plays the whole sequence in the active monitor, starting from the current position"; Edit Review plays
  part of the previous clip, the current clip and part of the next by the Trim settings' Preroll and Postroll, then
  returns (p.594).

### Ours
* Merging: the Source side with the Program side's set controls in their place, and the buttons each side turned off
  (but those over a control the Program side set). A corrected segment's picture goes through `colorCorrectedPicture`
  in `segmentPicture`, after its Timewarp and before its effect; renders include the correction in their key.
* The track the mode corrects is the highest selected video track (else the highest video track); filler is not a
  shot; the Go to buttons go to the first frame of the shot.
* The mode's commands: `cc.color_correction_mode` on the Timeline's toolbar after Effect Mode and on the Command
  Palette's CC tab with `cc.dual_split`, `cc.previous_uncorrected_shot`, `cc.next_uncorrected_shot` and
  `cc.reference_current`. Trim mode, Effect mode and Source/Record mode leave it; the Effect Mode button's work inside
  the mode (p.684) comes with 6.10d.
* Remove Effect in the mode removes both sides of the current segment's correction.
* Reference Current goes into the right monitor unless only the left one shows Reference; Escape switches the active
  monitor between the centre and the right.
* Dual Split's box starts as the left half of the picture; its handles are its top left and bottom right corners.
* Play Loop's later passes start at the sequence's start. The monitors have no position bars of their own: the
  Timeline's position indicator moves the Current monitor.
* The Correction Type button and the tool come in 6.10b2; until then the mode works on the Source side.

## 6.10b2: the Color Correction tool and the HSL group's panes

Code: `src/ui/correction_tool.*` (`CorrectionTool`, `ChromaWheel`, `LumaRangesGraph`), the "ColorCorrection" panel in
`MainWindow`, `CorrectionMonitors::setPreview`, `ColorCorrection::hasValues`. Test: `tst_correction_tool`.

### From the guide
* "The default display shows the controls in the Hue Offsets tab within the HSL ... tab" (p.595). The Correction
  Type button, the group tabs and the subdividing tabs, the tool's buttons, the Color Match control and the buckets
  (p.595-596). Source types and their tabs are green, Program blue, the Color Correction effect grey (p.597).
* Group tabs HSL, Channels, Levels, Curves, Sec (p.599); the HSL group's subdividing tabs Controls, Hue Offsets, Luma
  Ranges, with Master, Highlights, Midtones, Shadows below Controls (p.599, p.632) and Highlights, Midtones, Shadows
  below Luma Ranges (p.642). Click a tab's name to show it, not its Enable button; Page Up and Page Down go between
  group tabs (p.600).
* Enable buttons (p.603-605): click to turn on or off; adjusting a control turns on its button and those above it;
  "You cannot reset controls that are not currently displayed. If you Alt+click ... the Enable button for a tab whose
  controls are not currently displayed, you display the controls but do not reset them. Alt+click ... the button
  again to reset the controls" (p.603).
* A control: "Enable button, text box, direction button, slider, direction button" (p.633); Invert Chroma, Invert
  Luma, Clip Low and Clip High on the Master tab (p.633-634).
* ChromaWheels in the order Shadows, Midtones, Highlights, Master, each with its own crosshair pointer, "with red near
  the top"; drag the pointer, Shift for finer control; Hue and Amount text boxes; Gain, Gamma and Setup sliders on the
  same tab (p.635-639).
* The Luma Ranges graph shows all three curves, only the active one adjustable; click the curve to add a point, drag
  it, type In and Out, Delete removes it; Input, Three-tone and Output previews (p.640-643).

### Ours
* The tool is a panel ("ColorCorrection") that opens when Color Correction mode starts and closes when it ends. Every
  change is an undo step on the current segment's correction (`Timeline::setCurrentCorrection`), made when a slider,
  wheel or curve point is let go of or a value is typed; the monitors show it then (no live preview while dragging).
* The Correction Type menu lists every type of p.597 and the Color Correction effect; only Src Segment and Prog
  Segment work (the other relationships with 6.10d, the effect with 6.10e).
* An Enable button is lit when it is on and something under it is set; clicking a lit button turns it off, an unlit
  one that was turned off on again. Alt+click resets a control's own button at once (it is on screen with it).
* Slider text boxes show two decimals (Gamma three); the slider moves in hundredths.
* The ChromaWheels are outlines (the coloured backgrounds come with the Correction Mode settings, 6.10b4); Shift moves
  the pointer a quarter as far as the mouse; the pointers are a cross, a circled bar, a square and a diamond.
* The Luma Ranges graph draws its curves as straight lines between points (as `lumaRangeWeights` reads them); a point
  stays between its neighbours; a curve keeps at least two points. The previews act on the active monitor; Three-tone
  shows each pixel white, grey or black by the range with the largest share.
* The Channels, Levels, Curves and Sec tabs' panes come with 6.10b3.

## 6.10b3a: the Channels, Levels and Curves panes

Code: `src/ui/correction_tool_panes.cpp` (`CorrectionTool::channelsPane`/`levelsPane`/`curvesPane`, `ChromaCurveGraph`,
`Histogram`), the channel previews in `CorrectionMonitors`, the histograms' pictures from `MainWindow`. Test:
`tst_correction_tool`.

### From the guide
* Channels (p.645-647): the Preview pane's Red, Green, Blue and Master Input and Output ("Displays the uncorrected red
  channel in monochrome"); the Channel Blending pane, each channel an Enable button, four text boxes (-200 to 200) and
  Component Type buttons (Red, Green, Blue, Luma, Cr, Cb, Offset, and Invert, which puts "A minus sign (–) ... after
  the component name").
* Levels (p.649-658): the subdividing tabs Composite, Luma, Red, Green, Blue, Master; the Input histogram, the
  ChromaCurve graph and the Output histogram, each histogram with Black, Gray and White sliders and text boxes; "The
  Levels controls are dynamically linked to one another"; "The input histogram always shows the distribution of values
  for the uncorrected image"; the graph's points dragged; Composite and Luma have only black and white points; a
  background grid at the quartiles.
* Curves (p.659-663): four ChromaCurve graphs (Red, Green, Blue, Master), each with an Enable button and Input and
  Output text boxes; points added by clicking the curve, selected (a circle), dragged, typed and deleted with Delete,
  two to sixteen; the Master Saturation, Gain, Gamma and Setup sliders.

### Ours
* Component Type buttons read "% Red" and so on, the amounts in text boxes before them; Invert is the last item of the
  type menu and toggles.
* A Levels point dragged on the graph sets its input and output together, one undo step; the graph shows
  `model::levelsValue` and its points; a point stays between its neighbours. Histograms count the frame at the position
  (160 by 90, from the Composer) on the 8-bit video scale: Master counts all three channels, Composite both ends of the
  chroma's swing (as the Composite levels do). They are drawn in the text colour (the Color, Solid and Dynamic
  Histograms options come with the Correction Mode settings, 6.10b4) and refresh when the correction or position does.
* The Curves graphs draw `model::curveValue`, following a dragged point as it moves; no colour backgrounds yet (the
  Show ChromaWheel and ChromaCurve Graphs option, 6.10b4).
* The channel previews show the active monitor's channel as grey, uncorrected (Input) or corrected (Output).

## 6.10b3b: the Secondary pane

Code: `src/ui/correction_tool_secondary.cpp` (`CorrectionTool::secondaryPane`, `VectorWheel`), sampling in
`CorrectionMonitors` (`setSampling`, `colorsSampled`), `media::wheelColor`. Test: `tst_correction_tool`.

### From the guide
* The tab's parts: Output Vector controls, Input Vector controls, Input and Output Vector colour wheels, the Selected
  Vectors panel, the Color Match control, the Color Selection buttons and the buckets (p.669).
* The wheels show each vector's boundary and centre point, wedges for standard vectors and ellipses for custom ones;
  standard centres are filled circles, custom ones crosshairs (p.670); Show Standard is the default display (p.673).
* The Selected Vectors panel: names, colour swatches that "update in real time", Enable buttons (Alt+click resets),
  Invert Last Vector and Isolate; the selected vector's swatch has a white box (p.673-675). A vector is selected by
  clicking its centre point or its swatch, or by the syringe (p.674).
* Input sliders Hue, Saturation, Hue Width, Saturation Minimum (standard) or Saturation Width (custom) and Softness;
  dragging the centre point in the Input wheel; Lock Vectors (p.675-677). Output Hue, Saturation and Luminance, and the
  Output wheel (p.680-681).
* The syringe samples "every color value that the tip of the syringe passes over"; Add to Selection and Subtract from
  Selection click a colour; each stays on until clicked again or another is clicked (p.678-679).

### Ours
* The selected vector is kept in the correction ("secondary/selected"), so selecting one is an undo step.
* The wheels show the standard vectors, the enabled custom ones and the selected one (the Secondary Vectors setting
  comes with 6.10b4); a vector's boundary and centre are drawn in its own colour, the selected one thicker. Only the
  selected vector's centre can be dragged; the Output wheel shows just the selected vector's output.
* Swatches show the input centre's colour at mid luma (at least saturation 20, so pale vectors stay visible).
* Sampling reads the picture as drawn in the monitor under the pointer (the Eyedropper 3 x 3 Averaging setting comes
  with 6.10b4); the samples go to the tool when the mouse is let go, one undo step.

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
