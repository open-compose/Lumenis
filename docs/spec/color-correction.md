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
  changes this, 6.10b4a); a vector's boundary and centre are drawn in its own colour, the selected one thicker. Only the
  selected vector's centre can be dragged; the Output wheel shows just the selected vector's output.
* Swatches show the input centre's colour at mid luma (at least saturation 20, so pale vectors stay visible).
* Sampling reads the picture as drawn in the monitor under the pointer (the Eyedropper 3 x 3 Averaging setting, 6.10b4a); the samples go to the tool when the mouse is let go, one undo step.

## 6.10b4a: the Correction setting

Code: the "Correction" User setting in `src/ui/resources/settings.json`, applied by `MainWindow::applySettings` to
`CorrectionTool::Options` (`setOptions`), `CorrectionMonitors` (`setEyedropperAveraging`, `setDualSplitDuringPlayback`)
and `Timeline::Options::correctionUseMarks`; units in `model::toCorrectionUnit`/`fromCorrectionUnit` and
`UnitSpinBox`. Tests: `tst_correction_tool` (`settingOptions`, `useMarks`), `tst_settings_window`.

### From the guide
* The dialog opens from the tool's Correction Mode Settings button or File > Settings > User > Correction (FX Guide
  p.606); its tabs and options are listed in the Editing Guide's Correction Settings (p.1164-1167) and the FX Guide
  (p.606-609).
* Tabs: "Define which group tabs appear in the Color Correction tool" (p.606).
* Units: RGB, Hue, Saturation/Gain, Luma and Composite menus with 10 Bit (0 to 1024), 8 Bit (0 to 255), Percent, IRE,
  mVolts and Degree "available in one or more of the menus"; the 10-bit values for reference black and white are 288
  and 726, the 8-bit ones 16 and 235; the units "do not change the degree of precision" (p.606, p.609).
* Levels: Solid Histograms (solid forms, else a line graph), Color Histograms (a single channel's histograms in its
  colour), Dynamic Histograms (update while controls move, else when they are let go), Use Full Ranges (the Red,
  Green, Blue and Master histograms over the full 10-bit range) (p.606-607).
* Features: Secondary Vectors Show All, Show Standard (the default), Show Custom, Show Enabled (p.607, p.672-673);
  Saved Color Labels (None, RGB, Name, Name and RGB, the default); Use Marks for Segment Correction (off by default:
  every segment between the marks, whole); Eyedropper 3 x 3 Averaging; Show Eyedropper Info; Eyedropper Picks from
  Anywhere in Application; Show ChromaWheel and ChromaCurve Graphs (colour backgrounds, off by default) (p.608).
  The Editing Guide also lists Dual-Split remains Active During Playback and Waveform/Vectorscope Updates in Real-Time
  without a description (p.1166).
* AutoCorrect: the first, second and third automatic corrections when the effect is applied from the Effect Palette
  (Nothing, HSL Auto Balance/Black/Contrast/White, Curves Auto Balance/Contrast) (p.608-609; Editing Guide p.1166-1167).
* Preview: Pre-Roll and Post-Roll, 3 seconds by default (Editing Guide p.1167).

### Ours
* 10 Bit is 2v + 256 from the two reference points (so 0 to 1024 holds -128 to 384 on the 8-bit scale); Percent is
  the share of 0-255; IRE puts 16 at 0 and 235 at 100, mVolts at 0 and 700. Changes (Setup) take the scale without the
  offset. The menus: RGB 10 Bit, 8 Bit, Percent; Luma and Composite those and IRE, mVolts; Hue only Degree;
  Saturation/Gain only Percent; 8 Bit by default. What each menu covers: the Levels tabs' points by tab (Composite,
  Luma, the rest RGB), the Curves points RGB, the HSL Setup and Clip levels and the Luma Ranges graph's brightness Luma;
  other values are shown as kept.
* Tabs left out are passed over by Page Up/Down; with none chosen HSL stays.
* Solid Histograms is on by default; Color Histograms colours the Red, Green and Blue tabs' histograms; Dynamic
  Histograms recomputes the output histogram from the uncorrected frame through the side's own correction while a
  slider or graph point is dragged; Use Full Ranges widens only the axis (our pictures hold 0-255).
* The selected vector is always shown on the Input wheel.
* The ChromaWheel backgrounds are `media::wheelColor` at mid luma; the Curves graphs' a ramp from black to the graph's
  channel (white for Master).
* Eyedropper 3 x 3 Averaging applies to every colour the monitors sample (the Secondary tab's syringe and eyedroppers
  until Color Match comes with 6.10c). Dual-Split remains Active During Playback is on by default; off, the box is not
  drawn while the Composer plays. Use Marks needs both marks in the Record monitor.
* Saved Color Labels and Eyedropper Picks from Anywhere came with 6.10c3b, Show Eyedropper Info with 6.10c3a (on by
  default as p.631 has it);
  Waveform/Vectorscope Updates in Real-Time (6.10c), the AutoCorrect tab (6.10e; our defaults HSL Auto Balance, then
  Nothing) and the Preview tab are dimmed. The FX Guide gives Edit Review's preroll and postroll to the Trim settings
  (p.594), so the Preview tab's use is unknown.

## 6.10b4b: the tool's buttons, Comments and the Tab buttons

Code: `CorrectionTool::button`, `editComment`, `showPath` (`src/ui/correction_tool.cpp`); `Segment::correctionComment`,
`Timeline::currentCorrectionComment`/`setCorrectionComment`, Clip Text > Comments (`Timeline::Display::clipComments`),
the `cc.tab_1`-`cc.tab_12` commands and their handlers in `MainWindow`. Test:
`tst_correction_tool` (`buttonsAndComments`).

### From the guide
* "Color Correction Tool buttons. Left to right: Effect Mode button, Color Correction Effect Template button,
  Correction Mode Settings button, Safe Color Settings button, Comments button" on the right side of the tool (p.605).
* "When you are in Color Correction mode, the Effect Mode button simply opens the Effect Editor. It does not display
  the standard Effect Mode layout"; "you cannot close the Effect Editor by clicking the Effect Mode button again"; "To
  enter the standard Effect Mode layout from Color Correction mode: Click the Effect Mode button" (p.684).
* The Correction Mode Settings button opens the Correction setting (p.606).
* Comments: "If the position indicator is on a segment that has a comment, the icon on the Comments button is
  yellow"; the Comments dialog's text window, OK and Remove; Clip Text > Comments shows them in the Timeline
  (p.609-610).
* "The Command palette contains twelve Tab buttons"; clicking one in Active Palette mode displays its tab;
  "Alt+click (Windows) or Option+click (Macintosh) the Tab button with which you want to associate the Color
  Correction tab" (p.601-602).

### Ours
* The buttons' icons are our own drawings. The template button waits for 6.10d and Safe Color Settings for 6.10e,
  dimmed until then.
* The guide has the Effect Mode button leave Color Correction mode (p.589) and also "simply" open the Effect Editor
  (p.684). Ours: the tool's own Effect Mode button opens the Effect Editor and, with the editor already open, enters
  Effect mode; the Effect Mode button elsewhere (the Timeline's, the Command Palette's) enters Effect mode at once.
* A comment is kept on the segment itself, for both Source and Program sides, and on both halves of a cut; an edit
  between halves with different comments is not a match frame. The dialog's text is trimmed; Remove and an empty text
  both take the comment off; each change is an undo step.
* The Tab buttons are "Tab 1" to "Tab 12" on the CC tab, work only in Color Correction mode, and keep their tabs as
  the Correction setting's `tabButton1`-`tabButton12`; a hidden group's tab is not displayed.

## 6.10c1: the Waveform and Vectorscope monitors

Code: `src/media/scopes.*` (`media::scopeImage`, `waveformRow`, `vectorscopePoint`, `pqCode`), the scope entries of
`CorrectionSource` in `src/ui/correction_monitors.*` (`isScopeSource`, `CorrectionMonitors::setPal`). Tests: `tst_scopes`,
`tst_correction_mode` (`scopes`).

### From the guide
* The Source menu's waveform and vectorscope commands "configure the monitor to graphically display color information
  ... Media Composer displays the information for the currently active monitor" (p.591, p.686); to show one, activate a
  monitor and choose the command in another (p.687).
* Quad Display: "clockwise from the top left corner: YC Waveform, Vectorscope, RGB Histogram, RGB Parade" (p.687).
* RGB Histogram: the darkest values on the left, the brightest on the right, the height the number of pixels (p.687-688).
  RGB Parade: the R, G and B waveforms side by side (p.688).
* Vectorscope: chroma without luma, the centre no chroma; "Small squares mark the location of standard color bar
  vectors. Inner squares represent the proper values for 75% color bars, and outer squares represent 100% color bars"
  (p.688-689).
* Y Waveform: luma, a left scale of 8-bit levels (16 black, 235 white) and a right scale in percent; the range -8% to
  108% (p.689). The HDR 1000 and HDR 10000 waveforms' scales in nits (p.689-690).
* YC Waveform: Y in green and the chroma as a cyan envelope at Y+C and Y-C; an NTSC (7.5 IRE black, 100 IRE white) or PAL
  (0 mV black, 700 mV white) scale on the left, percent on the right; "A red edge on the display indicates composite
  video values above or below these limits" (p.690).
* YCbCr Histogram: Y on the upper bar, then Cb and Cr; YCbCr Parade: Y in white, then Cb and Cr centred, "Positive Cb ...
  blue hues, and negative Cb ... yellow hues. Positive Cr ... red hues, and negative Cr ... cyan hues" (p.690-691).
* The safe colour limits' marks in the waveforms (p.688-690, p.726) come with 6.10e.

### Ours
* A scope measures a 320-by-180 copy of the active monitor's corrected frame, at most 240 rows; each cell lights by
  1 - e^(-hits / n) with n about a tenth of a column's hits. The waveforms run from level -8 at the bottom to 263 at the
  top; the graticule lines are at 16, 75, 126, 180 and 235 (0, 25, 50, 75 and 100%).
* The HDR waveforms read Y' as a PQ (ST 2084) code value, the top of the scale the code of 1000 or 10000 nits, lines every
  tenth of it.
* The YC Waveform's red edge marks composite values outside -20 to 120 IRE (8-bit -28 to 279), top or bottom.
* The vectorscope's circle is a chroma of 0.5 at 45% of the panel's smaller side; Cb to the right, Cr up.
* A monitor showing a scope cannot be the active monitor; making the active monitor a scope moves the active monitor to
  the one at Current, else the first showing a frame. PAL is 25 and 50 fps projects.

## 6.10c2: the automatic corrections

Code: `src/media/auto_correct.*` (`media::autoCorrect`, `removeColorCast`); the buttons and `CorrectionTool::autoCorrect`,
`startRemoveColorCast` (`ColorSelection::RemoveColorCast`) in `src/ui/correction_tool*.cpp`; the commands
`cc.auto_balance`, `cc.auto_black`, `cc.auto_contrast`, `cc.auto_white`. Tests: `tst_auto_correct`, `tst_correction_tool`
(`automaticCorrections`).

### From the guide
* Auto Black, Auto Contrast and Auto White in HSL > Controls > Master; Auto Balance, Auto Black, Auto Contrast and Auto
  White in HSL > Hue Offsets; Auto Balance and Auto Contrast in the Curves tab (p.620). "Media Composer analyzes the
  currently selected frame in the segment and adjusts the appropriate color correction controls" (p.621).
* Hue Offsets: Auto Balance "Adjusts all three ChromaWheel controls"; Auto Black "Adjusts the Setup slider"; Auto
  Contrast "Adjusts the Gain and Setup slider controls"; Auto White "Adjusts the Gain slider" (p.621). Curves: Auto
  Balance adjusts the Red, Green and Blue curves, Auto Contrast the Master curve (p.621).
* "The four automatic color correction commands described in this table are also available as buttons in the CC tab of
  the Command Palette" (p.621).
* "Automatic color corrections operate within normal limits for legal video levels" (p.619).
* Remove Color Cast: a button below the Highlights, Midtones and Shadows ChromaWheels, each adjusting only its wheel, and
  one in the Curves tab "adding a point to each of the Red, Green, and Blue curves", again for each new area, up to 16
  points; click the button ("The button appears highlighted"), drag into the image and release over the area to make
  neutral (p.621-622).

### Ours
* The frame measured is the uncorrected one at the position (96 by 54 pixels), through the side's correction. The tonal
  range leaves out the darkest and brightest half percent; Black, Contrast and White solve the Master range's Gain and
  Setup as the linear map y' - 16 = Gain/100 x (y - 16) + Setup for 16 and 235 (within the sliders' ranges: Gain up to
  200%). From the Hue Offsets tab they set them through that tab's paths (p.603).
* HSL Auto Balance: three passes moving each range's wheel so that the range's pixels, weighted by their share,
  average to no chroma (a grey world per range); Amount at most 100.
* Curves: measured before the Curves group (and Secondary); Auto Contrast's Master curve is the two points (darkest
  luma, 0) and (brightest, 255); Auto Balance maps each channel's darkest and brightest to the luma's, with the ends kept.
* Remove Color Cast takes the colour under the eyedropper when the mouse is let go; a wheel moves by the colour's chroma
  over its brightness's share of the range (nothing below a share of 0.05); the Curves button follows each channel back
  through its curve and adds (or moves, within 2) a point taking it to the colour's luma. The button turns off after
  one sample.
* The buttons are text buttons with the guide's names as tooltips; the CC tab's four commands are the Hue Offsets tab's
  and work in Color Correction mode.

## 6.10c3a: the Color Match control

Code: `src/media/color_match.*` (`media::matchColor`, `matchTypes`, `matchTypeText`, `naturalMatchColor`),
`ColorMatchControl` and `CorrectionTool::colorMatch`/`matchColor` (`src/ui/correction_color_match.cpp`), the
`ColorSelection::MatchColor` eyedropper, Show Eyedropper Info as `CorrectionTool::Options::eyedropperInfo`. Tests:
`tst_color_match`, `tst_correction_tool` (`colorMatch`).

### From the guide
* "Each Color Correction group includes a Color Match control"; "Media Composer replaces the input color value with the
  output color value and adjusts all the other color values in the image proportionally ... also automatically adjusts
  the other controls in the group" (p.624).
* Its parts: "input color swatch (top left), output color swatch (top right), Match Type button (center), Match Color
  button (bottom)"; a swatch is picked by pressing on it and dragging the eyedropper into a monitor, released to
  complete; double-clicking a swatch opens the Windows Color dialog or the Macintosh Colors panel; Match Color makes the
  correction (p.625-626).
* The Match Types: HSL Controls H+S+L, Hue, Saturation, Luminance, H+S, H+L, S+L; Hue Offsets Master, Highlights,
  Midtones, Shadows; Channels and Curves Master, R+G+B, Red, Green, Blue, R+G, R+B, G+B; Levels Black, Gray and White
  Point; Secondary Hue, Saturation, H+S (p.626-629).
* NaturalMatch (Curves and Secondary) "adopts the new hue value, preserves the original luminance value, and adjusts the
  saturation value in relation to the other values"; the match types then read "(Nat)" (p.627, p.629).
* "By default, the color swatches in the Color Match control display the RGB values" (p.631); Show Eyedropper Info turns
  them on and off (p.608).

### Ours
* Which controls a match changes: HSL Controls the range tab's Hue (by the hue between the colours), Saturation (by
  their chroma) and Gain (by their brightness above black); Hue Offsets the chosen wheel by the chroma difference over
  the range's share of the input's brightness (at least 0.2); Channels each channel's own component amount by the ratio
  of the values (Master: the luma's); Levels the Master tab's input and output point; Curves a point on each curve
  (Master: the master curve, by luma), as Remove Color Cast adds them; Secondary the selected vector's output hue and
  saturation by their difference on the wheel.
* NaturalMatch's target is the output's chroma direction at the input's luma, its chroma scaled by the input's
  brightness over the output's (a quarter to four times).
* The colours are picked like the Secondary eyedroppers: a press on a swatch arms the eyedropper and the colour under
  it when the mouse is let go in a monitor fills the swatch (it does not follow the drag). The two swatches, Match Type
  and NaturalMatch are kept per control for the session, not with the correction; the swatches' values are on the 8-bit
  video scale ("R: 61 G: 62 B: 66").

## 6.10c3b: custom colours in bins

Code: `src/media/custom_color.*` (`customColorItem`, `customColorOf`, `customColorName`, `nearestHtmlColorName`), the
Color Match swatches' drags and drops (`ColorMatchControl::colorMime`, `dropColor`, `colorAt`) and `customColorMime()`;
bins take the drop in `BinItemModel` and show the colour as the item's icon and frame. Tests: `tst_color_match`,
`tst_correction_tool` (`customColors`).

### From the guide
* "In the Color Match control, Alt+drag (Windows) or Option+drag (Macintosh) the swatch that contains the color you want
  to save to a bin ... The color appears in the bin as a rectangular color swatch icon. Media Composer assigns the color a
  name based on the current Saved Color Labels settings"; renaming it in the bin; loading it by dragging the icon to a
  swatch, or with the eyedropper from its swatch when Eyedropper Picks from Anywhere in Application is on (p.630-631).
* Saved Color Labels: None, RGB (the 8-bit values), Name (the nearest standard HTML colour), Name and RGB, the default
  (p.608). Eyedropper Picks from Anywhere: "the eyedropper can pick a color from anywhere in Media Composer's
  interface, for example the swatch for a custom color in a bin" (p.608).

### Ours
* A custom colour is an Effect item holding "customColor" ("#rrggbb"); its Text view icon and Frame view picture are the
  colour. RGB names are "R:61 G:62 B:66" on the video scale (as the swatches show them); Name is the nearest CSS/SVG
  colour name in R'G'B' with its first letter capitalised ("Saddlebrown"); Name and RGB joins them with a space. Swatches
  also take another swatch's colour.
* Picks from Anywhere: a press on a swatch and a release anywhere over the application's windows takes the colour the
  window draws there (no 3 x 3 averaging); with it off, the eyedropper samples the monitors as before.

## 6.10d1: relationship corrections

Code: `src/model/correction_relations.*` (`CorrectionKeys`, `correctionKeysOf`, `relatedSegments`,
`applyRelationshipCorrection`, `convertRelationship`), the relationship enum and names in `color_correction.h`,
`Segment::sourceRelationship`/`programRelationship`, `Timeline::setCorrectionRelationship`/`followCorrectionType`, the
Correction Type menu in `correction_tool.cpp`, the Fast menu's Color Correction and the indicator lines in
`timeline.cpp`. Tests: `tst_correction_relations`, `tst_correction_tool` (`relationships`), scenario
`39-correction-relationships.json`.

### From the guide
* The Correction Type menu: the relationships, then the Color Correction effect (p.596). "When you move the position
  indicator to a segment that has color correction, the Correction Type button changes to indicate the type of
  correction that is applied to that segment" (p.596).
* "Options for Relationship Color Corrections" (p.597): Src Tape, Src Clip Name (ignoring ".new" and what follows),
  Master Clip, Sub Clip, Src Segment, Prog Track ("the current segment and all other material on that segment's track"),
  Prog Segment. Green for Source, blue for Program (p.597).
* Converting a Source relationship to another Source one (or Program to Program): "Click the Correction Type button,
  and choose a new type from the menu"; Source to Program only through a template (p.598).
* The Timeline Fast menu's Color Correction: "A segment in the Timeline that has Source color correction is marked with a
  green line at the bottom of the segment. A segment that has Program color correction is marked with a blue line at
  the top ... A dotted line indicates a correction that applies only to that segment ... A solid line indicates a
  correction that applies beyond the individual segment" (p.616).

### Ours
* Each segment keeps its own copy of a correction and the relationship it was made with ("sourceRelationship",
  "programRelationship" in the sequence, kept only while that side has a correction). Making or changing a correction
  with a relationship sets it on every related video segment there is then, on any video track for the Source
  relationships and on the segment's track for Prog Track; filler is never related. Segments edited in later take it
  with Update Correction (6.10d4). Remove Effect takes the correction off the current segment only.
* The keys: a clip's tape is its Tape source's name, else its Tape column; a subclip is of its own MobID (Sub Clip) and
  of its master clip's (Master Clip); a segment without a clip in the bins has only its name and MobID.
* Converting gives the correction to the segments the new relationship reaches; the segments the old one reached (with
  the same values and relationship) that the new one leaves out keep their values as Src or Prog Segment corrections.
* Use Marks for Segment Correction applies to the segment relationships only (p.597 names Source and Program Segment).
* The button follows a corrected segment's type when the position moves to another segment (the selected side's
  correction first); a type chosen for the other side of the same segment stays until then.
* The lines: two pixels, green (Source) at the foot of the segment and blue (Program) at its top; dotted for the segment
  relationships. The setting is kept with the Timeline's view ("correctionLines").

## 6.10d2a: Color Correction effect templates

Code: `src/model/correction_template.*` (`CorrectionTemplate`, `correctionTemplateItem`, `correctionTemplateOf`,
`applyCorrectionGroup`), `correctionTemplateMime()`, the Timeline's `applyCorrectionTemplate`,
`applyCorrectionTemplateGroup`, `dropCorrectionTemplate` and `applyCorrectionTemplateToSelection`
(`src/ui/timeline_correction.cpp`), the tool's template button, buckets and tab drops (`CorrectionTool::clickBucket`,
`templateMime`, `saveCorrection`), the monitors' `setTemplateDrop`, `cc.save_correction` and `cc.bucket_1`-`8`, and the
bin's double-click and Enter in `MainWindow`. Tests: `tst_correction_relations` (`templates`, `group`),
`tst_correction_tool` (`templates`), scenario `40-correction-templates.json`.

### From the guide
* The Color Correction Effect Template button dragged to a bin, or the CC tab's Save Correction; "By default, Media
  Composer names the template using the clip name of the segment"; "Effect icons for open bins also display in the
  Effect Palette" (p.612-613). Templates keep "both the values set for each control and the status of each Enable
  button" and do not follow later changes (p.611).
* Eight buckets C1-C8 below the Color Match control, kept for the session; Alt+click saves (overwriting), Alt+click on an
  uncorrected segment clears; a Color Correction icon replaces the label; also CC tab buttons (p.613-614).
* Applying all values: drag to the monitor with the current segment (or to the segment in the Timeline), Enter on a
  selected template, a click on a bucket; one tab: drag "to the tab, and drop it anywhere in the tab"; selected segments:
  double-click in editing or Effect mode (p.614-615). Applying overwrites the existing correction (p.615).
* Relationships: a drop on a Timeline segment or the Current monitor uses the template's; a double-click, a tab drop or
  a bucket uses the Correction Type menu's (p.615).

### Ours
* A template is an Effect item holding "colorCorrection": both sides with their relationships. Templates show in the
  Effect Palette's bin lists; activated there they act as from the bin.
* With the template's relationships each side the template has goes on with its relationship and the current segment
  loses the side the template lacks. With the Correction Type's, the template's merged values replace the current side's
  correction. Enter (and a double-click) in Color Correction mode applies to the current segment; outside it, to the
  selected video segments.
* A tab drop changes the controls and Enable buttons at and under the tab the tool shows. A drop anywhere on the mode's
  monitors counts as one on the Current monitor; the duplicate Color Correction effect p.615 mentions waits for 6.10e.
  p.598's Source-to-Program conversion through a monitor drop does not happen: the template's relationship wins
  (p.615); a bucket or Enter does it.
* The bucket icon is the template button's sheet drawn green; a bucket's drag carries its template, to a bin or a monitor.
* Save Correction goes to the active bin, else the first open one.

## 6.10d2b: Color Correction Presets and automatic templates

Code: the presets in `resources/effects.json` (`EffectInfo::correctionPreset`), applied through
`Timeline::applyEffectToSelection` and `droppedCorrectionTemplate`; `Timeline::automaticCorrections`,
`automaticCorrectionBin`/`automaticCorrection` and `MainWindow::refreshTemplateBins`; Enter in `EffectList`. Tests:
`tst_effects` (`catalog`, `palette`), `tst_correction_tool` (`automaticTemplatesAndPresets`).

### From the guide
* "A number of color correction presets are included in the effects library ... Select Tools > Effect Palette and choose
  Color Correction Presets" (p.611). The guide does not name them.
* Automatic templates: the 16 most recent corrections in a sequence, listed under each color-corrected sequence loaded in
  the Record monitor "at the bottom of the scrollable list of effect categories", named by the comment or the clip name,
  newest first and numbered from 1; applied by dragging, or "highlighting it in the Effect Palette and pressing Enter"
  (p.612). They "update when you make new adjustments on the same segment"; a Program correction after a Source one makes
  a second template with both (p.611-612).

### Ours
* Seven presets of our own, each a Source correction of the HSL Master controls: Black and White, Brighten, Darken,
  Desaturate, High Contrast, Low Contrast, Saturate. They apply as templates do (the Correction Type's relationship; on
  the current segment in the mode, on the selected segments outside it; dropped on a segment with their own).
* The automatic templates live for the session, by sequence; a correction on the same segment and side as the newest
  updates it, anything else is a new one. They show as a template bin named after the sequence, items "1  name", for the
  sequences the Record monitor has loaded. Enter on any item in the Effect Palette's list acts as a double-click.

## 6.10d3a: keyframes on corrections

Code: `src/model/correction_keyframes.*` (`correctionKeyframing`, `setCorrectionKeyframe`, `correctionAt`,
`adjustedAt`, `correctionKeyframeFrames`), `ColorCorrection::keyframes`, the compositor's `segmentPicture`, and
`Timeline::currentCorrection`/`storedCorrection`/`setCurrentCorrection`. Tests: `tst_correction_keyframes`,
`tst_compositor` (`keyframedCorrection`), `tst_correction_tool` (`keyframedCorrections`).

### From the guide
* "You can use the advanced keyframes interface in the Effect Editor to animate most color correction adjustments";
  the parameter controls and graphs "update as you scrub to show the parameter values at each point in the clip"
  (p.683).
* Keyframeable (p.685): every HSL > Controls parameter, every Hue Offsets parameter (each ChromaWheel as Hue and Hue
  Amount), the Secondary tab's sliders; the Curves and Levels tabs keyframe "in their keyframe tracks" without graphs;
  Isolate, Invert Last Vector, the Luma Ranges and Channels tabs not at all.
* "Make an adjustment to the appropriate parameter in the Color Correction tool. A new keyframe appears below the
  Current Color Correction monitor at the location of the position indicator" (p.686).

### Ours
* A correction keeps its keyframes by control path (the Hue Offsets and Curves tabs' Master Gain, Gamma, Setup and
  Saturation are the Controls tab's), in the effect keyframes' form, Elastic over the segment. Numbers blend with the
  interpolation; Curves and Levels points hold (Shelf) from one keyframe to the next.
* The tool shows the values at the position. A changed control that has keyframes gets one at the position (replacing
  one within half a frame); a reset control loses its keyframes; any other change is a plain value. With keyframe
  markers selected (6.10d3c) the change goes to the control's keyframes at those frames instead.
* The Program side's keyframes override the Source side's for the same control; a control the Program side sets plainly
  stops following the Source side's keyframes.

## 6.10d3b: the correction in the Effect Editor

Code: `src/model/correction_effect.*` (`correctionParameters`, `correctionEffect`, `correctionFromEffect`, `toolHue`),
the Effect Editor's `refresh`/`rebuild`/`changeShown` in Color Correction mode and its generated parameter categories,
`Timeline::setStoredCorrection`. Tests: `tst_correction_keyframes` (`asEffect`), `tst_correction_tool`
(`effectEditorCorrection`).

### From the guide
* "Manipulate many color correction parameters in the Effect Editor using sliders or parameter Enable buttons"; "Add
  and adjust keyframes for many color correction parameters in keyframe graphs"; "You can also adjust effects other than
  color corrections in the Effect Editor while in Color Correction mode" (p.683).
* The Secondary sliders "appear in the Effect Editor ... Controls and Enable buttons in the Effect Editor and in the Color
  Correction tool are ganged together"; each ChromaWheel is "two sliders in the Effect Editor, one for the Hue and the
  other for the Hue Amount"; Hue in the Effect Editor runs from -360 to +360, "+340 in the Effect Editor displays as -20
  in the Color Correction tool" (p.685).
* "Add a keyframe in the Effect Editor using normal advanced keyframe techniques" to start, then adjust in the tool
  (p.686).

### Ours
* In Color Correction mode the Effect Editor shows the current correction (the side the Correction Type works on) in
  place of the segment's effect: categories HSL Master, HSL Highlights, HSL Midtones, HSL Shadows (Hue, Saturation,
  Brightness, Contrast, Gain, Gamma, Setup, and Clip Low and High for Master), HSL Hue Offsets (each wheel's Hue and Hue
  Amount) and Secondary <vector> for the twelve vectors' input and output sliders. Other effects are shown out of the
  mode; "adjusting effects other than color corrections ... in Color Correction mode" waits for the Color Correction
  effect (6.10e) to decide which shows when.
* Every hue slider runs from -360 to 360 in the editor; the tool's value is the same angle in -180 to 180. Keyframes
  keep the editor's numbers.
* The editor's groups have no Enable buttons yet (ours; the tool's stay); a value back at its default is left out as the
  tool leaves it. The Curves and Levels keyframe tracks came with 6.10d3c.

## 6.10e5: Spot Color Correction and the Paint Effect's Color Match

Code: Spot Color Correction among the Intraframe effects (`model::isIntraframeEffect`), `spotCorrected` and
`colorMatched` in `src/media/intraframe.cpp`, the Paint Effect's Color Match mode in `paintMode`, the Color Match
swatches in `effect_parameters.json`. Tests: `tst_intraframe` (`spotColorCorrection`, `paintColorMatch`).

### From the guide
* "The Spot Color Correction effect lets you make precise color adjustments to a part of an image that you define
  using the Intraframe drawing tools"; "Using the Spot Color Correction effect, you can enable several kinds of color
  adjustments at the same time", where a Paint Effect object takes one mode (FX p.731-733).
* "Lets you apply color adjustments to selected parts of images as well as to whole images ... includes all of the
  color adjustment parameter categories available in the Color Effect, as well as the Color Match control" (FX p.471).
* Color Match "lets you correct color in the area you have isolated using the drawing tools by replacing the Input
  color value with the Output color value and adjusting all the other color values in the area proportionally"; the
  Input (left) and Output (right) swatches; Use NaturalMatch "replaces color hue ... while retaining the saturation and
  luminance characteristics" (FX p.547-548). The Paint Effect's Color Match mode: "Click the Enable Color Match button to
  activate the color match for the selected area" (FX p.544).
* The Paint Effect's colour adjustment modes, p.733: Luma Adjust, Luma Range, Luma Clip, Chroma Adjust, Color Style,
  Color Gain, Color Match.

### Ours
* The guide's "Color Match Input and Output color swatches" are two colour parameters, Color Match Input and Color Match
  Output, shown as the Effect Editor's swatches with their eyedropper (6.9c1).
* Color Match is the Color Correction tool's (6.10c3a) as R+G+B on the Curves group, NaturalMatch when its button is on.
  It does nothing until both swatches have a colour. Spot Color Correction has no Enable button for it (none in the
  guide's list).
* The Paint Effect's Fast menu keeps the reference's mode names (p.537-544). Its Color Adjust covers p.733's Luma
  Adjust and Chroma Adjust, which the reference's list does not have.

## 6.10e4c2: the Safe Color Limiter's analysis display

Code: `media::safeColorAnalysisImage` (`src/media/scopes.*`), `Composition::limitSafeColors`,
`EffectEditor::safeColorAnalysis`, `Composer::setMonitorOverride`/`overridePicture` and MainWindow's override. Tests:
`tst_scopes` (`safeColorAnalysis`), `tst_effect_mode` (`safeColorLimiterAnalysis`).

### From the guide
* "By default, when the Effect Editor is open, the effect changes the display in the Source monitor to an analysis mode
  that highlights pixels in the image that fall outside the current safe color limits" (FX p.729).
* "The background layer is a copy of the image with luminance reduced ... The foreground layer highlights those pixels
  in the image whose color values fall outside the limits currently being used by the effect"; Yellow composite, White
  luminance, Red, Green and Blue RGB gamut; "If a pixel in the image falls outside current limits for multiple limit
  types, the analysis display uses the color for only one of the types"; "When you adjust the limit parameters in the
  Effect Editor, the analysis display updates" (FX p.729-730).
* The Source Monitor Analysis menu: Highlight Out of Range Colors, or Source Monitoring – Off, where "the Source monitor
  reverts to the normal display of Source footage" (FX p.545).

### Ours
* The display shows while the Effect Editor is visible and shows a Safe Color Limiter at the position, in or out of
  Effect mode. It analyses the Record monitor's frame at the position, composed without any Safe Color Limiter and
  without renders, with the shown limiter's limits and Enable buttons.
* The background is the picture at a third of its levels. A pixel outside several limits takes the first of Composite,
  Luminance, Red, Green, Blue. The guide's example only shows composite giving way to RGB.
* The Source monitor shows the display even when nothing is loaded in it.

## 6.10e4c1: the Safe Color Limiter effect

Code: `src/model/safe_color_limiter.*` (`isSafeColorLimiter`, `safeColorLimiting`, `seedSafeColorLimiter`,
`limitSafeLevels`, the units menus' text), `detail::safeColorLimiterPicture` in `src/media/color_effects.cpp`, the
"Safe Color Limiter Parameters" category in `effect_parameters.json`, `Timeline::setEffectSeed` with MainWindow's seed.
Tests: `tst_safe_colors` (`limiterParameters`, `limiting`), `tst_compositor` (`safeColorLimiter`), `tst_effect_mode`
(`safeColorLimiterSeed`).

### From the guide
* "Lets you limit the color values of video material to a specific range, for example, to meet safe color standards
  set by a broadcaster"; Image category, single-layer segments; parameter categories Safe Color Limiter Parameters and
  Grid (FX p.471).
* "When you first apply a Safe Color Limiter effect, its parameters default to the current limits in the Safe Color
  settings"; "all limiting by the Safe Color Limiter effect is performed in the 601 color space"; applied to filler on
  an empty top track it limits the whole sequence (FX p.728-729).
* 422 Safe: off (the default) is fast, though "a small number of transient pixel values might exceed your safe color
  ranges"; on, "every pixel in every frame is guaranteed to be within your safe color ranges". The Source Monitor
  Analysis menu (Highlight Out of Range Colors, Source Monitoring – Off). Composite/Luma Levels: an Enable button,
  Composite L/H, Composite units (IRE, mVolts), Luma L/H, Luminance units (8 Bit, %, IRE, mVolts); RGB Levels: an Enable
  button, RGB Gamut L/H and RGB Gamut units; pixels past a limit "are modified so that" they are within it (FX
  p.545-546).

### Ours
* The limiting, on 8-bit video levels (a picture's 0-1 as 16-235, as 6.10e4b reads it): luma moved into its limits
  with every channel shifted alike (chroma kept); then luma moved inside the composite limits if it is outside them, and
  the chroma scaled towards grey (luma kept) until luma plus and minus its amplitude fit; then R, G and B clamped to the
  gamut. 422 Safe repeats the steps until nothing changes (16 passes at most); off, they run once, so the RGB clamp can
  leave luma or the composite level slightly outside.
* Both Enable buttons start on. The sliders run -300 to 1100 in any unit, with two decimals; changing a units menu
  does not convert the numbers, as in the Safe Colors setting.
* The palette's effect is seeded only when applied from the Effect Palette (a drop or a double-click); effect
  templates keep the values they were saved with.

## 6.10e4b: safe colour warnings

Code: `model::pictureLevel` and `warnedViolations` (`src/model/safe_colors.*`); `media::pictureViolations` and the
`limits` of `media::scopeImage` (`src/media/scopes.*`); `CorrectionMonitors::setSafeColors`/`safeWarnings` and the
warning drawn in `correction_monitors.cpp`; `ColorMatchControl::setSafeColors`/`rgbWarnings` and its `infoText`.
Tests: `tst_safe_colors` (`picturesAndWarnings`), `tst_scopes` (`safeColorLimits`, `pictureViolations`),
`tst_correction_mode` (`safeColorWarnings`), `tst_correction_tool` (`colorMatch`).

### From the guide
* "Warnings appear in the monitors for all limit types — Composite, Luminance, and RGB Gamut. The warning indicators
  do not appear unless color levels exceed at least one of the limits currently set to Warn. Warnings appear in the
  Color Match control for RGB Gamut limits only" (FX p.724).
* "You see an orange safe color warning triangle in the top left corner of the image, along with warning indicators
  for each of the safe color value types that are currently set to warn. Warning color bars appear in one of three
  places in a column: At the top (indicating a level that is too high), At the bottom (indicating a level that is too
  low), In the center (indicating a level that is within limits)"; the picture shows five columns, Composite (yellow),
  Luminance (white), Red, Green and Blue, and a column can have a bar at both ends; "Once all levels are within safe
  limits, the warning triangle and warning indicators no longer display in the monitor" (FX p.724).
* "If you use the eyedropper to select an input or output color that exceeds limits, a warning triangle appears beside
  the color information in the appropriate color swatch" (FX p.724).
* "The Y Waveform, YC Waveform, and RGB Parade displays incorporate any safe color limits you have set": Y Waveform
  luma legal green, illegal white; YC Waveform composite legal cyan, illegal yellow, luma green and white, "Outside
  display boundaries" red; RGB Parade each channel legal in its colour, illegal white (FX p.727).
* The indicators are those "that can appear in the monitors in Color Correction mode" (FX p.729).

### Ours
* Pictures are read as the rest of the app reads them: a picture's 0-255 stands for the video levels 16-235
  (`pictureLevel`). So with the default limits only composite warnings can appear (vivid yellow is past 120 IRE, vivid
  blue below 0 IRE); RGB Gamut and Luminance warn once their limits are tightened.
* The warnings are in Color Correction mode's monitors, drawn over the picture: the triangle, then one column 12 pixels
  wide per warned type to its right (Composite and Luminance for the top Actions button, Red, Green and Blue for the
  bottom one). Every pixel of the shown picture counts; for two-field media, both fields are in the picture.
* In the Color Match swatches the triangle is the character ⚠ after the value ("R: 153 ⚠ G: 119 B: 93").
* The scopes follow the limits whatever their Actions. Their legality uses the Rec. 601 levels of 6.10e4a while the
  traces stay where 6.10c1 draws them (Rec. 709 luma). The default composite limit, 120 IRE, lies above the YC
  Waveform's display, so its excursions there show as the red edge.

## 6.10e4a: the Safe Colors setting

Code: `src/model/safe_colors.*` (`SafeColorLimits`, `SafeUnit`, `safeLevels`, `safeViolations`), the "Safe Colors" Project
setting in `settings.json` with the settings' `number` and `safeColorGraph` option types, `src/ui/safe_color_graph.*`,
`MainWindow::safeColorLimits`, the Color Correction tool's Safe Color Settings button (`CorrectionTool::Options::safeColors`,
`safeColorSettingsRequested`). Tests: `tst_safe_colors`, `tst_settings_window` (`safeColorsDialog`).

### From the guide
* "Media Composer lets you set three different types of safe color limits — for the composite signal range, for the
  luminance range, and for the RGB gamut"; Units: Composite IRE or mVolts, Luminance and RGB Gamut 8 Bit, %, IRE or
  mVolts; "the 8-bit RGB values for reference black and reference white are 16 and 235"; Actions: "The top button
  controls both the Composite and the Luminance limit types. The bottom button controls the RGB Gamut limit type",
  Ignore ("This is the default setting") or Warn; "If Warn is selected in one or more of the Actions menus, the icon on
  the Safe Color Settings button appears orange ... Otherwise, the icon ... appears black" (FX p.721-722; Editing Guide
  p.1216-1217).
* The graph: "colored bars that represent the current low and high levels for each limit type ... gray vertical lines
  that represent the default high and low levels"; top to bottom Composite, Luminance, Red, Green and Blue; "Bars on the
  left indicate the low limit, while bars on the right indicate the high limit"; the pictures' defaults: Composite 0.00
  to 120.00 IRE, Luminance 10.00 to 108.45 IRE, RGB Gamut 16.00 to 235.00 8 Bit (FX p.722-723).
* "To open the Safe Color Settings dialog box ... Select File > Settings, click the Project tab and double-click Safe
  Colors. In the Color Correction tool, click the Safe Color Settings button" (FX p.725).

### Ours
* The dialog is the generic settings dialog, one row per value (Low, High, Units, then the two Actions), and the graph
  below. Values are read in the units chosen beside them; changing the units does not convert the numbers.
* IRE and percent put 0 at level 16 and 100 at 235 (no setup), millivolts 0 and 700; the composite signal's extremes
  are Rec. 601 luma plus and minus the chroma's amplitude, so 100% yellow reaches about 133 IRE. Checks allow half an
  8-bit level, so limits typed in other units do not catch their own edges.
* The graph spans 8-bit levels -40 to 295; the button's icon is the palette's text colour where Media Composer's is
  black.

## 6.10e3b3: the Color Correction effect in the Effect Editor, shapes on trackers

Code: `EffectEditor::drawsObjects`, `chooseShapeTracker` and the "tracking:shape" menu (`effect_editor_tracking.cpp`);
`model::correctionShapeView`, `keepCorrectionShapeChange`, `trimShapeParameters` (`src/model/correction_shapes.*`);
`model::objectTracker`/`setObjectTracker` and the tracker's movement in `model::placedShapeAt` (`src/model/intraframe.*`);
the "Layers", "Shapes" and "Shape Tracking" categories in `effect_parameters.json`. Tests: `tst_intraframe`
(`shapesFollowTrackers`), `tst_intraframe_ui` (`colorCorrectionShapes`).

### From the guide
* "You can also use the Effect Editor to make additional edits including Tracking"; the picture shows Color
  Correction's categories HSL, Levels, Curves, Secondary, Shapes, Layers (Shape Layer menu, Serial Mode, Show Result,
  Show Alpha, Invert Alpha) and Tracking (an enable button and a "No Tracker" menu), and the drawing tools on the right;
  "a. Click on tracker to track the area. b. Select the shape you want to attach a tracker to. c. Select the tracker
  from the list of created trackers"; "Different shapes can be connected to different trackers" (p.699).

### Ours
* Outside Color Correction mode the Effect Editor shows the Color Correction effect with the Intraframe drawing tools
  and the categories Layers (Serial Mode, Invert Alpha), Shapes (Softness) and Shape Tracking (the tracker menu). The
  corrections themselves are edited in the Color Correction tool, and in the editor in Color Correction mode (6.10d3b).
  Show Result, Show Alpha and the layer are the tool's display choices, so the editor leaves them out.
* With one shape selected, Shapes and Shape Tracking are that shape's; the other categories stay the effect's.
* The tracker menu lists "No Tracker", the effect's trackers and, while there is room, "New Tracker". New Tracker makes
  one (Point A ...) for the shape and opens the Tracking window to track it. The shape then moves by the tracked
  point's movement from the tracker's reference frame, as Titler+ objects do (6.8c3).

## 6.10e3b2: drawing shapes in the Current monitor

Code: `src/ui/correction_shape_host.*` (`CorrectionShapeHost`, an `IntraframeHost` like the Effect Editor), the
Intraframe tool's `IntraframeHost` interface in `intraframe_tool.h`, `CorrectionMonitors::setOverlayInput`/`setOverlay`,
`Timeline::correctionEffectAt` and `setCorrectionShapeObjects`, the panel's tool buttons in
`src/ui/correction_tool_shapes.cpp`. Test: `tst_correction_tool` (`shapeDrawing`).

### From the guide
* "Use the shape tools in the Color Correction tool to mark the area you to want to color correct"; the picture shows
  the selection, rectangle, oval, polygon, curve and brush tools (p.698). How each tool draws is the Paint Effect's
  (p.395-404).

### Ours
* The tools are 6.9's Intraframe tools, drawing in the monitor whose Source menu is Current; they take its mouse and,
  over the monitors, Delete for the selected shapes, Escape and Enter for a polygon. The other monitors keep their own
  mouse use (Dual Split's handles, sampling).
* They work only on the Shapes layer, and are dimmed on Base. The first shape on a segment without the Color Correction
  effect adds the effect, as a first adjustment does. Each shape is an undo step. A new shape takes no corrections of
  its own: the corrections and shape options stay the effect's.
* Selecting a shape selects it for Softness. Outlines are drawn while a drag goes on; the picture changes when the
  drag ends.

## 6.10e3b1: the Color Correction tool's shape panel

Code: `src/ui/correction_tool_shapes.cpp` (`CorrectionTool::shapesPane`, `setShapeLayer`, `setShapeSoftness`), the
Timeline's `correctionLayer`, `setCorrectionShapeOptions`, `setSelectedShapeSoftness` and `correctionShapeView`
(`editCorrectionEffect` making the effect as an adjustment does), `model::CorrectionShapeView`/`shapeViewFor` and the
`view` of `media::colorCorrectedShapesPicture` through `Composition::shapeView`. Tests: `tst_correction_tool`
(`shapePanel`), `tst_compositor` (`correctionShapeViews`).

### From the guide
* The picture (p.698): the shape tools in a column (selection, rectangle, oval, polygon, curve, brush), the Shapes and
  Base buttons, the Show Alpha icon beside Shapes ("top right"), Serial Mode (checked), Show Result and Invert Alpha
  boxes, and Softness with its box, arrows and slider; "Show Alpha Icon - when selected displays the alpha"; "Show
  Result - when selected, shows the composite result of the base+ shape layer (the final result)"; "Softness - Use the
  slider to add softness to the selected shape" (p.698-699).

### Ours
* The panel shows left of the group panes only while CC Effect is the Correction Type. Shapes and Base choose which of
  the effect's corrections the tool's controls, keyframes, templates and automatic corrections work on; Base at first.
* Without Show Result the monitors show the layer worked on: Base, the base correction alone; Shapes, the shaped
  picture inside the shapes over the uncorrected picture. Show Alpha shows the shapes' softened, inverted coverage in
  grey, white inside. These are display state for the session, not kept with the sequence; outside Color Correction
  mode the effect always plays its result.
* Serial Mode, Invert Alpha and Softness are undo steps on the effect at the position; a first change adds the effect as
  an adjustment does. Softness is dimmed without a selected shape (the drawing tools select them, 6.10e3b2) and sets
  every selected shape's.

## 6.10e3a: shapes on the Color Correction effect

Code: `src/model/correction_shapes.*` (`shapeCorrectionOf`, `layerCorrectionOf`, `CorrectionShapeOptions`,
`shapeSoftness`), `media::colorCorrectedShapesPicture` and `correctionShapesMask` in `src/media/intraframe.cpp`, the
compositor's Color Correction effect. Test: `tst_compositor` (`correctionShapes`).

### From the guide
* "Select CC Effect ... Use the shape tools in the Color Correction tool to mark the area you want to color correct";
  "Shapes - selects the shape layer that allows you to use the shape based tools to color correct portions of the
  material"; "Base - sets the base layer"; "Serial Mode - when selected, any changes you have made to the base will be
  included when you are performing a shaped based color correction"; "Invert Alpha - when selected displays the
  inverted alpha"; "Softness - ... Each shape can have a different softness"; the picture shows the tools (select,
  rectangle, oval, polygon, curve, brush), Serial Mode checked (p.697-699).

### Ours
* The shapes are the effect's Intraframe objects (their outlines, Brush strokes and placing as in 6.9), the shape
  layer's correction its "Shape Correction" category (with keyframes as the base correction's), Serial Mode and Invert
  Alpha its "Layers" (Serial on at first), each shape's Softness its own "Shapes" category (0 to 100: a blur of
  up to 100 pixels of a frame 1920 wide).
* The picture: the base correction on the segment (or, on filler, on the tracks below); the shape correction on that
  (Serial Mode) or on the uncorrected picture; the two mixed by the union of the shapes' softened coverage, inverted by
  Invert Alpha, so that the shape layer corrects outside the shapes.

## 6.10e2: the Color Info tool

Code: `src/ui/color_info.*` (`ColorInfoTool`, the "ColorInfo" panel), `MainWindow`'s sampler and `tools.color_info_tool`,
the Color Correction tool's Color Info button (`CorrectionTool::colorInfoRequested`), the Effect Editor's
`colorPickingStarted` with the Effect Editor setting's "Open the color info tool when picking color". Test:
`tst_color_info`.

### From the guide
* "The Color Info tool allows you to see the RGB values for the pixels located under the pointer on the screen ...
  without having to go to Color Correction Mode"; Tools > Color Info Tool; "Move your pointer"; its shortcut menu:
  Active only for video monitors ("When deselected, moving the pointer anywhere on your screen will display color
  information"), Show video color after apply Display Color Space ("before and after applying a Display Color Space
  option (LUT)"), RGB 8 bit (0 to 255), RGB 10 bit (0 to 1024), RGB Percent ("can be lower than 0 or higher than 100"),
  IRE, mVolts; "the Color Info button in the Color Correction Tool" (p.696-697, picture: Video and Video LUT columns).
* The Effect Editor setting: "Allows you to enable/disable the Color Info Tool from automatically launching when you
  access a color picker" (Editing Guide p.1172).

### Ours
* The tool reads the pointer ten times a second while it is shown: over the Source or Record monitor the colour of the
  picture there (`Composer::colorAtScreen`), elsewhere (with Active only for video monitors off) the screen's pixel.
* The Video LUT column shows the same values as Video until display colour spaces come (Phase 7); it hides when "Show
  video color after applying Display Color Space" is off.
* RGB 10 bit is the 8-bit value scaled to 0-1023; IRE and mVolts count from video black 16 (0 IRE, 0 mV) to white 235
  (100 IRE, 700 mV); Percent is the share of 255.
* The Effect Editor's eyedropper (a Color Preview window dragged) opens the tool when the setting is on, its default.

## 6.10e1: the Color Correction effect as a correction

Code: `Timeline::correctionOnEffect`/`setCorrectionOnEffect`, `effectCorrection` and `putEffectCorrection` behind
`currentCorrection`/`storedCorrection`/`setCurrentCorrection`, `followCorrectionType`, `removeCorrection`,
`Timeline::setAutoCorrection` with `autoCorrectEffects` (MainWindow gives the Correction setting's AutoCorrect tab), the
tool's "CC Effect" menu entry, the compositor's Color Correction effect on filler and with keyframes. Tests:
`tst_correction_tool` (`correctionEffect`), `tst_compositor` (`correctionEffectOnFiller`), scenario
`42-correction-effect-top-track.json`.

### From the guide
* The Correction Type menu's last option makes "a color correction using the Color Correction effect"; "Gray indicates
  a Color Correction effect correction" (p.596-597); "Click the Correction Type button and select CCEffect ... Once you
  make an adjustment, the Color Correction effect appears on the segment", without the AutoCorrect corrections (p.696).
* "Media Composer processes the relationship correction first, and its result is the input to the color correction
  effect" (p.597, p.693).
* A whole sequence: a video track above the others, its Record Track button selected; "Once you make a color adjustment,
  Media Composer fills the entire length of the empty track with filler material and applies a Color Correction
  effect. The filler in this case is not opaque material but simply a container" (p.692-693); "You cannot apply
  relationship color corrections to filler" (p.695).
* From the Effect Palette, "Media Composer makes the automatic color corrections set in the AutoCorrect tab", up to
  three: Nothing, HSL Auto Balance/Black/Contrast/White, Curves Auto Balance/Contrast (p.622-623, p.695-696).

### Ours
* With CC Effect the tool reads and writes the effect's correction on the segment at the position on the correction
  track, its keyframes as a relationship correction's (6.10d3). The effect is added at the first adjustment; a segment
  with another effect refuses (nest the Color Correction effect from the Effect Palette instead).
* On filler, or an empty correction track, the tool turns to CC Effect by itself and back when the position returns to
  a clip. A track holding only filler (or nothing) is filled with one filler segment the sequence's length.
* Remove Effect with CC Effect removes only the effect; a segment corrected only by the effect shows CC Effect.
* The AutoCorrect corrections are made from the uncorrected frame at the position when it is in the segment, else the
  segment's first frame, in the order of the three menus.

## 6.10d4: managing colour-corrected sequences

Code: `src/model/correction_manage.*` (`correctionCounts`, `updateCorrections`, `mergeCorrections`, `flattenCorrections`,
`removeCorrections`), `src/ui/correction_manage_dialog.*`, `CorrectionTool::shortcutMenu`, `Timeline::manageCorrections`/
`openCorrectionManagement`/`canMergeCorrections` in `src/ui/timeline_correction.cpp`, the commands `cc.update_correction`,
`cc.merge_correction`, `cc.flatten_correction`, `cc.remove_correction`, `MainWindow`'s `correctedSequenceCreated`.
Tests: `tst_correction_manage`, `tst_correction_tool` (`managingCorrections`), `tst_main_window`
(`correctionCopyGoesToTheBin`), scenario `41-update-correction.json`.

### From the guide
* Each command is on the Color Correction tool's shortcut menu ("Right-click in the Color Correction tool, and select
  Update Correction", p.700) and opens a dialog of Source (Tape, Clip Name, Master Clip, Sub Clip, Segment) and Program
  (Track, Segment) relationship boxes with Create New Sequence (pictures p.701, p.706).
* Update: "Media Composer updates any color corrections based on Source Tape, Clip Name, Master Clip, or Sub Clip
  relationships, or based on Program Track relationships" to "the appropriate new segments"; "By default, all available
  relationships are selected" (p.700-701).
* Merge: the Source monitor's and the Record monitor's versions; unavailable until both are loaded in Color Correction
  mode; Segment relationships merge "using the Segment's source and timecode", the source segment "with the most
  overlap" winning, tried first for segments without a correction, and "are not selected by default"; a segment
  "will not change from Source Tape to a Source Segment"; "In case of conflict": Prefer Source Sequence, Prefer Record
  Sequence, Prefer Latest (the default); without Create New Sequence the result is the Record monitor's sequence
  (p.701-703). The example (p.703-704) is `mergeExample` in `tst_correction_manage`.
* Flatten: "all the color corrections become segment-only relationships"; the dialog gives each relationship's number
  of segments (p.704-705).
* Remove: "removes color correction for all segments affected by the relationships you have selected" (p.705-706).
* Create New Sequence: "a new sequence appears in the bin with the title [OldTitle].Copy 1"; without it, Edit > Undo
  goes back (p.701, p.705, p.706).

### Ours
* The commands look at every video track. Update gives a segment without a correction on a side the correction of a
  segment related to it by a selected relationship, the first of Tape, Clip Name, Master Clip and Sub Clip (Program:
  Track) that has one; corrected segments are left as they are.
* Merge puts both sequences' corrections per relationship key together (the conflict setting deciding between two for
  one key) and gives them to the Record version's segments corrected with that relationship and to those without a
  correction. Prefer Latest compares the two sequences' modified times. Overlap is the same source clip and track with
  source frames in common; a Prog Segment correction also needs the same track.
* Remove's Segment boxes start off as its picture shows (the text says "all"); Flatten's are dimmed, segment-only
  already. Only Flatten and Remove show the counts, with each column's total.
* Create New Sequence leaves the loaded sequence as it was and puts the result in its bin as "<name>.Copy.NN" (the
  bin's numbering for copies), loaded in the Record monitor. Each command is one undo step otherwise.
* The four commands are also on the Command Palette's CC tab.

## 6.10d3c: keyframe markers and the Curves and Levels tracks

Code: `CorrectionMonitors::setKeyframeResolver`/`keyframeBar` and `CorrectionKeyframeBar` in
`src/ui/correction_monitors.cpp`; `Timeline::correctionKeyframeMarkers`, `selectCorrectionKeyframe`,
`setCorrectionKeyframeSelection`, `deleteCorrectionKeyframes`, `moveCorrectionKeyframes`; `model::adjustedAt`'s
selection, `removeCorrectionKeyframesAt` and `moveCorrectionKeyframes` in `src/model/correction_keyframes.*`; the
track-only parameters of `src/model/correction_effect.*` and the Effect Editor's "track" rows with
`EffectEditor::setCorrectionEnabled` and `selectCorrectionKeyframes`. Tests: `tst_correction_keyframes`
(`trackOnlyAsEffect`, `markers`), `tst_correction_mode` (`keyframeMarkers`), `tst_correction_tool`
(`effectEditorCurvesAndLevels`).

### From the guide
* "Keyframe markers display in the position bar below the Current monitor in Color Correction mode. In general, these
  keyframe markers behave like those you see in the position bar below the Effect Preview monitor": click to select,
  "Delete selected keyframes", "Alt-drag (Windows) or Option+drag (Macintosh) keyframe markers to move them"; "You
  cannot add keyframes manually in the Color Correction monitor" (p.686).
* The steps: add a keyframe in the Effect Editor, adjust, "Ctrl-click ... the keyframe added in step 1 to deselect it",
  move the position indicator in the bar, adjust again and "A new keyframe appears below the Current Color Correction
  monitor at the location of the position indicator" (p.686).
* Curves and Levels: "You can enable and disable these parameters in the Effect Editor. You can also keyframe these
  parameters in their keyframe tracks. However ... you cannot open keyframe graphs or adjust the parameter values
  directly in the Effect Editor" (p.685).

### Ours
* The bar spans the current segment on the correction track, under whichever monitor's Source menu is Current; its
  markers are every keyframed control's keyframe frames, drawn as the Effect Preview bar's (pink when selected). A click
  away from the markers moves the position within the segment.
* The selection is the Timeline's, by frame, and belongs to the segment it was made in. The Effect Editor's selection
  shows there (a marker is selected when every keyframe at its frame is) and the bar's selects every keyframe at those
  frames in the editor. So a keyframe just added in the editor is selected, and the next adjustment changes it until it
  is deselected, as the guide's steps go.
* An adjustment with markers selected sets the changed control's keyframes at the selected frames; with none (or none of
  the control's) it adds one at the position.
* Alt-drag moves the selected markers' keyframes of every control together, each stopping a frame short of a keyframe
  of its control that stays, all within the segment. Delete (over the monitors or in the Timeline in the mode) removes
  every control's keyframes at the selected frames.
* The Effect Editor shows "Levels Composite", "Levels Luma", "Levels Red", "Levels Green", "Levels Blue" and "Levels
  Master" (each tab's points; Composite and Luma have no gray points) and "Curves" (Red, Green, Blue, Master) as rows
  with a keyframe track and no slider or graph opener. A Levels tab has one Enable button, on its first row; each curve
  has its own. A keyframe added on such a track takes the value the control holds there. The Curves tab's Master sliders
  are the HSL Master controls and keyframe there, with graphs.

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
