# User-interface inventory

This is the specification for Phase 1: every window, menu, command and default
keyboard shortcut a Media Composer 2024/2025 user expects.

**Sources.** Both are publicly available from Avid.

| Short name | Document | URL |
|---|---|---|
| Editing Guide (EG) | Media Composer Editing Guide, version 2025.12 (1,317 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf` |
| FX Guide (FX) | Media Composer Effects Guide, version 2025.12 (762 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_FX_Guide.pdf` |
| Editing Guide 2026.8 | Media Composer Editing Guide, version 2026.8 (1,320 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_Editing_Guide.pdf` |
| FX Guide 2026.8 | Media Composer Effects and Color Correction Guide, version 2026.8 (762 pages) | `https://resources.avid.com/SupportFiles/attach/Media_Composer/2026/Media_Composer_v2026.x_FX_Guide.pdf` |
| What's New 2026.8 | Media Composer What's New for Version 2026.8 (10 pages) | from the owner; listed on Avid's "Media Composer 2026 Documentation" Knowledge Base page |
| Shortcut sheet (KS2009) | "Keyboard Shortcuts for Avid Editors", Moviola, 2009 (13 pages); hosted by Avid | `https://resources.avid.com/supportfiles/attach/keyboard%20shortcuts.pdf` |

**The 2009 sheet is a secondary source.** It is older and third-party, so it
is used only where the 2025 guides name the command and do not contradict
it. Entries taken from it are marked `ks2009` and must be confirmed on a
current seat. Example of a contradiction: in 2009 Ctrl+I opened the Console,
but the 2025 guide makes it Get Info (p.388), so the guide wins.

A plain page number ("p.61") refers to the Editing Guide. FX Guide pages are
written "FX p.303".

**Editions.** Page numbers are the 2025.12 guides' unless marked. Material
new in 2026.8 is cited "2026.8 guide p.246" (the 2026.8 Editing Guide) or
"What's New 2026.8 p.8". The 2026.8 FX Guide keeps the 2025.12 page
numbers. The 2026.8 Editing Guide changes only what is listed below, and its
pages are the 2025.12 pages plus:

| 2025.12 pages | 2026.8 pages |
|---|---|
| 1–445 | the same |
| 446–450 | +1 |
| 451–1132 | +2 (505 straddles +2 and +3) |
| 1133–1201 | +3 |
| 1202–1233 (the settings tables reflow) | +3 or +4 |

(`pdftotext` of both editions, compared line by line with page numbers
masked, in the scratchpad; the guides themselves are not committed.)

**Watch list.** These choices are ours because the guides are silent.
Recheck them against each new edition of Avid's documents (Editing Guide,
FX Guide, What's New, ReadMe) and against a seat when there is one:
* Change Source Track > More: the segment then refers to the master clip,
  at the same frames (§11; 2026.8 guide p.507).
* Shared projects on third-party storage: our own lock files (§20a;
  docs/spec/bin-locking.md).
* Audio marks (docs/spec/audio-marks.md): an edit with audio marks is a split edit, each audio mark taking the place
  of its kind's mark for the audio tracks; a split Splice-in of different lengths refused; Go to Audio Mark IN/OUT
  as Go to IN/OUT; Go to Previous/Next Trim (only pictured, p.96) as Select Previous/Next Transition (A and S, p.651).
* Audio CDs (docs/spec/audio-cd.md): which files count as on a CD for CD only,
  the read tracks' clip names.
* Sequence Report (docs/spec/sequence-report.md): the report's words and
  layout, what counts as non-renderable, Mob IDs only with UID, the tape ID as a
  number, other sequences' reports beside the first, the AAF popup's Reports
  folder and its markers.
* Graphics import (docs/spec/graphics-import.md): Matte Key clips as Title
  items with "<name>.fill" and ".matte" precomputes, Dilate Fill's one-pixel
  bleed, Ignore as the Alpha Channel default. Photoshop layers: the message
  box's Cancel, the Select Layers dialog top first with all selected,
  Ignore taken as Invert for layers, the sequence's Source File. Linked files
  with alpha: the Link setting's Alpha Channel rather than the Import
  setting's (p.318 and p.1204 differ), the hidden "<name>.link" clip.
* Batch Import (docs/spec/batch-import.md): Batch Import and Batch Re-import
  doing the same, the mixed-rate message's words, clips at another rate left
  out with a Console line, the earlier media kept as another resolution.
* Printing (docs/spec/printing.md): the printouts' layout (tables, four frames
  to a row, the captions under Print Frame and Print Timeline), Page Setup kept
  only for the session, File > Print Bin reading "Print" while the Markers
  window or the Console was used last.
* Surround panning (docs/spec/surround.md): the pan law; the default pans;
  speaker snapping setting Center/Side to 100; the LFE slider as 0-100;
  no surround automation yet.
* Timeline clip colours (docs/spec/timeline-colors.md): the Clip Color
  dialog's defaults (Offline and Source on, the others off), the colours
  other than offline red and proxy yellow, one Mixed Rates colour per rate
  for ten rates, the layout, and Default Colors also resetting Linked Clips.
* Start Filler Duration in seconds, 2 by default; the name of a sequence made
  by Create Sequence Based on Selection (docs/spec/timeline-commands.md).
* Timeline settings (docs/spec/timeline-settings.md): what Auto-Patching pairs, Position Bar
  Snap's 8 pixels, Segment Drag Sync Locks only into sync-locked tracks, Default Sync Locks on new
  tracks, the defaults of Select Filler (on) and Applying Effects opens Effect Editor (off), the
  segments a Segment Mode lasso selects, White and Black Timeline Text's lightness threshold, and
  Fast Scrub also leaving out clip frames and waveforms.
* Composer settings' Edit tab (docs/spec/composer-edit.md): marks other than the guide's two for Sync
  Point editing are refused; Single Mark Editing off; a source IN with the position on it running to
  the clip's end; phantom marks as blue brackets; spanned copied markers cut short; Load Filler's
  tracks.
* The Timecode window (docs/spec/timecode-window.md): its labels, font sizes, starting line and the
  dimmed empty-monitor submenus; Loop Selected Clips' Tab working anywhere while it plays.
* The Find window (docs/spec/find.md): no indexing (bins read as they are), the default options
  (Ignore Case, Load into monitor, Markers and Clip Names on), the default Results columns, what the
  Timeline and Markers tabs list, and Ctrl+G as a keyboard-only Find Again command.
* AutoSequence (docs/spec/autosequence.md): which of two related clips is kept, the track layout of
  Camera groups, and the name when there is no Tape or Source File.
* The last Bin settings (docs/spec/bin-settings.md): no Frame view border for objects the guide does not
  colour, the icon in a frame's corner, what local and mixed resolution mean, and only one-bin floating
  Bin Containers kept with workspaces, others left as they are.
* Interface colours and fonts (docs/spec/interface-colors.md): a colour option of its own under each
  check box, the Sidebar's quick filter keeping the theme, tabs-only bin colour as a square icon, the
  viewer background only around the pictures, and blending half and half.
* Bin editing (docs/spec/bin-editing.md): the clip loaded into the Source monitor and keeping its marks
  there, the outline on the selected record tracks with the patch choosing the tracks, the drop picking
  only the frame, a 12-pixel snap, and V and B taking the bin's clip whenever a bin is active.
* The attic (docs/spec/bin-attic.md): its folder names, copies keeping the `.cthbin` suffix, the newest
  copy replaced within two hours, one zero limit keeping old copies, shared projects' attic beside the
  project, and an auto-save interval of 0 turning it off.
* Bin Layout (docs/spec/bin-layout.md): closing bins a layout does not name, split groups coming back as
  tabs, Save Current saving into the last layout, the Delete dialog's list, a name reused replacing, and
  the Workspace View's Bin Layout as a text box.
* Set Font and Help > Shortcuts (docs/spec/set-font.md): the size range, nothing for other windows, and
  the Shortcuts window's columns and Find box.
* Unmount (docs/spec/media-unmount.md): the Mount All button, the list not being saved, no ejecting;
  Center Current and Send Current Home acting on any floating panel, and what "home" is.
* Bin > Select (docs/spec/bin-select.md): what counts as a reference (subclips through their Clip
  source, sequences by segment MobID or clip id), sequences counted as offline, the Unreferenced Clips
  message's wording, and Reveal File opening only the folder on Linux.
* The Composer Window tab (docs/spec/composer-display.md): the second row's layout and Fast Menu,
  the Flow Data height, the tick intervals, and the middle panel's six buttons; Dynamic Play's other
  direction, top speed and silence between whole speeds; the Position Bar frame and the outer frames
  of the four-frame display.
* Trim settings (docs/spec/trim-settings.md): Preroll, Postroll and Intermission in seconds, the
  Intermission as a pause between passes (the guide calls it "a transition effect duration"), the
  Play Loop button in the Big Trim display, which transition Edit Review takes and that J-K-L Trim
  works from the Record monitor only; the trim side that follows play, Big Trim in single-monitor
  view, the Small Trim layout and the conditions and shade of sync rollers.
* The Sequence Map (docs/spec/sequence-map.md): the defaults of its four Timeline settings, the
  overlay's place and size, the effect band, a press off the white box moving it, and the window
  staying open when clicking elsewhere.
* Sync breaks and Dupe Detection (docs/spec/sync-dupes.md): what counts as
  a sync partner and the sign of the offset; the Sync Breaks submenu's item
  names; dupes per clip and source track; the dupe colours after orange and
  green; the Dupe Detection Handles choices; what the Clip Text items Clip
  Full Durations, Source Names, Media Full Names and Clip Resolution show.
* Choose Columns: Search matches anywhere in a heading; All/None acts on
  the headings shown; the width is fixed (§9).
* Effect keyframes (docs/spec/effect-keyframes.md): the Spline and default
  Bézier tangents; Extrapolate as a straight line; a segment effect's time
  as frames into it over its length - 1, a transition's as its progress;
  Superimpose at 50 until a level is set; Fade Effect with both lengths 0
  removing the Level's keyframes; reversing twice turning Swap Sources off;
  Align moving the selected keyframes as a group; a slip leaving Fixed
  keyframes; the track colours and widths, the Fixed indicator's pin; clicking
  a selected keyframe keeping the selection; Set Position To Keyframe on and
  Add to Open Groups as the defaults; Enabled Groups as all groups; options
  kept only with keyframes; only sliders and colours keyframed; the graph
  zoom steps, Zoom to Curve Height's margins, the scroll bar for the
  thumbwheel, pasted offsets in frames, handle lengths measured on screen,
  the handle method staying until changed, Indent Rows on by default, and
  the shortcut menu writing the setting.
* Effect mode (docs/spec/effect-mode.md): the Effect Mode button on the
  FX tab and Source/Record Mode on the More tab; a second click on Effect
  Mode leaving it; the Effect Preview's Go to Previous/Next Event going to
  keyframes (p.128) where p.77's table says the sequence's first and last
  frames; the Record monitor returning outside an effect; the zoom steps;
  the toolbar's pictures; no field numbers in the position box.
* The 2D transitions (docs/spec/wipes.md): Border Width and Soft in sixteenths
  of the picture's width, white until set, Blend Color across the band; the
  default grid as 8 columns by 6 rows and no preset grids; the Edge, Clock,
  Bands, Matrix order, Sawtooth and L-Conceal geometry; a segment wipe running
  over the segment's length.
* The geometric image effects (docs/spec/image-effects.md): Position in pixels
  of a picture 1920 wide; Picture-in-Picture at Scaling 50 until set; the crop
  scaled about the frame's centre; the film masks as wide as the picture;
  Blowup's Field 12 as three times the size; the Reformat factors and Position
  H's range; the shapes' Scaling and Position; the Color Effect's curves (Bright,
  Cont, Gamma, Post, Solar, Hue direction) and order; Blur and Mosaic over the
  whole picture; Acceleration as a smoothstep blend; repeated parameter names
  stored as "<subcategory>: <name>".
* Timecode Burn-In (docs/spec/burn-in.md): Display 1 on and the rest off until
  set, the labels other than "Mas TC1", Source Timecode's Types, Timeline Clip
  Text as the Clip Note, the positions, Font Size scaled from 1920 wide, the
  appearance values until set, the Effect Editor's tabs.
* The Illusion FX effects (docs/spec/illusion-fx.md): every slider range the
  reference omits ("ours" in effect_parameters.json), the values until set,
  and each picture (Film Grain's noise, Crystal's cells, Flare's ring, the
  test patterns' levels, the bolt's path, the spark physics, the
  kaleidoscope's folding, Motion Blur's taps, each distortion's mapping,
  Ripple's rings and light, the Border as an inset); "Color Mix Effect Input
  Parameters" read as each effect's own; Pinch's and Radial Blur's Position
  as X and Y; Sphere's and Swirl's Size in percent of the height; the
  transitions' pictures (the iris, the melt's columns and Bleed, Pagecurl's
  corner and Turn Left, the particles' starts and speeds, Random Blend's
  cells, Rollup's roll, Shear's strips), Amount following the transition
  until set, and Particle Bumpiness 0 until set.
* Rendering (docs/spec/rendering.md): every render holds its track and those
  below; what makes a render out of date (the key); precompute names; the
  Render Effect dialog's drive menu and dimmed processing; the Clear Renders
  options selected until changed; Clear Renders In/Out without marks taking
  the whole sequence; Automatic processing by the render resolution's depth;
  Discard deleting this render's precomputes; the Render Ranges line along
  the track's foot; what counts as a red or yellow playback range, and no
  blue; Render On-the-Fly on until turned off, not kept between sessions;
  ExpertRender's analysis (non-real-time effects, more than two stacked,
  the topmost selected track's effect), its three choices always offered,
  ExpertRender at Position left out (not in the menu); Image Interpolation's
  Standard filtering as Advanced does; the Render Sound as two tones;
  precomputes going into the sequence's bin, Rendered Effects standing for
  Set Bin Display's "Effects"; autodeletion when a render is replaced;
  background renders linked by their key, a cancelled job keeping the
  effects it finished, no "computername__bg.1" on shared storage.
* Traditional motion effects (docs/spec/motion-effects.md): the Freeze Frame
  duration of two seconds, only Other in the Freeze Frame menu, no drive
  asked for an unrendered motion effect clip, the rate in names to two
  decimals, no two-field options, reverse and strobe clips computed rather
  than black, the bin a new clip goes into, the "(12.5 FPS)" Timeline text,
  segment Motion Effects' sound at normal speed; the Timewarp templates'
  graphs, the Speed graph's sum from the anchor, Duplicated Field as the
  Timewarp Type until changed, our FluidMotion (block matching); the
  Motion Effect Editor's track choice, frame numbers for the anchor and
  keyframe positions, its graph scales, the render type change as an undo
  step of its own. Motion adapters (6.5d2): a clip linked at the project's
  rate counted as mixed rate by its media's rate, the adapter's 100% speed,
  the Field Motion default from the Format column, 2:3 Film for clips at a
  30 fps rate, the green dot's place, the T icon, "(25 fps)" on video
  tracks only, Refresh Sequence keeping a chosen Type. Fields (6.5d3a):
  the even lines as the first field unless the media says otherwise,
  VTR-Style's line pairs, the Source menu in progressive projects with the
  Output fixed, the formats a Timewarp has until set, the Pulldown
  Insertion and Removal templates' formats and Both Fields, the pattern
  detection, Adaptive Deinterlace's stillness test, adapters' patterns
  found once per file. The Fluid effects (6.5d3b): thresholds in pixels
  of a 1920-wide picture, Blur Amount as a percent of the motion, the
  colour map's brightness up to 70% and its Cb/Cr compass, FluidMorph's
  Stream->Stream and Feature Match until set and its brightness morph.
* Nests (docs/spec/nesting.md): which segment Step In takes, the nest
  shown as a sequence of its own, the FX tab for Step In, Step Out and
  Collapse, Collapse onto the lowest track, Video Mixdown's clip name and
  the whole sequence without marks. Nested effects in the Effect Editor
  (6.6b2): one open at a time under indented title rows, the list going in
  only through nests of one video track, the refusals' wording. Expanded
  nests (6.6b3): the nested tracks named "<track>.<n>" with the clip on the
  nest's V1 (the pictures on p.221 show it on 1.2), one nest at a time,
  Double-Click to Show Nesting on by default.
* Effect templates (docs/spec/effect-templates.md): a template named after
  its effect, Alt keeping the segment without its transition or renders, a
  template with its source media edited in as an overwrite, a parameter
  group the template's effect lacks refused, Fixed keyframes scaled by the
  rates, the template bins listed under the categories, and Promote
  Effects' copy unchanged.
* 3D effects (docs/spec/3d-effects.md): the units (pixels of a 1920-wide
  picture, ±999 across for Crop, Skew and Corner Pin), the viewer's
  distance, a category on once a value is set, the border, highlight and
  shadow sizes, Corner Pin overriding the other geometry, and 3D Promote
  for Picture-in-Picture only until titles and matte keys come. 6.7a2:
  the shapes' default animations and sizes, the Slats' Freq and Ampl,
  the key types' units and defaults, and the look of each border type.
  6.7a3a: Trail drawn from the effect's earlier times (also when stepping),
  Shadow before Trail when both are set, Erode/Expand's cut levels, Spill
  Suppression's colours and tolerances. 6.7a3b: stamps kept as press times
  at the position (not on keyframes), a same-frame Clear clearing the
  stamp, stamps redrawn from their segment's picture at the press. 6.7a4:
  2D handles always shown in Effect mode, lettered handle buttons, the
  rotation and XZ drag rates and directions, Corner Pin's first drag
  pinning all four corners, values rounded and not ganged by Fixed
  Aspect, a shape's handles on its flat plane, the overlay's colours.
* Pan and Scan (docs/spec/pan-scan.md): square frame pixels, the
  sources' and targets' placement in the frame, a non-anamorphic Target
  output boxed in black, Source 1.78 and Target 1.33 until chosen, the
  Target's defaults (Scaling 100, Position 0), the wire frame kept inside
  the source picture, Scaling X and Y ganged, the origin kept as values
  at the position, subdivided pieces keeping the effect as it was.
* Avid Pan & Zoom (docs/spec/pan-zoom.md): Zoom Factor 1 as the image
  fitted to the frame, Position in percent of the image's half-size (Y
  down), Display Source until set, the filters behind each Filtering
  entry, 601 levels stretched to full range, Square Pixels without
  effect, an eight-image cache, Use Subres also when rendering, Safe
  Action at 90%, the Qt locate dialog; 6.7b2b: the ease curves, Hold at the
  midpoint, Constant runs by path length, a Catmull-Rom Spline, the zoom
  by equal factors, Velocity kept at the selected keyframes' times.
* Motion tracking (docs/spec/tracking.md): tracking data kept with the
  effect, the new trackers' boxes and colours, offset tracking's join at
  the frame before a region, Smoothing's and SteadyGlide's Gaussians,
  the nearest data between and beyond points, distances and angles in
  the frame's proportions, the engines' methods (a fixed correlation
  pattern, the optical flow, a 4 x 4 grid's median); 6.7c2: the On boxes
  and selector Fast menus as rows in the Effect Editor, lettered buttons in
  the Tracking window, boxes also shown while a region has no data, the
  target's 1% minimum, tracking frames 480 pixels wide; 6.7c3: Shift+lasso
  adding, Delete with one bound, Stretch Points in proportion to frames,
  a new region's boxes, region starts of every tracker for Go to
  Previous/Next Region, the Display Markers as brackets; 6.7d1: copied
  trackers kept for the session, pasted names numbered, the first region
  reaching back to the effect's first frame after a trim; 6.7d2:
  stabilizing with SteadyGlide undoing only its jitter, Auto-Zoom from
  Position's moves alone with one scale for both axes, "too far" past
  400%, the Auto-Zoom button and its messages' wording, Picture-in-Picture's
  tracking data kept on promotion; 6.7d3: Region Stabilize's sliders
  (names, ranges, a quarter of the frame until set), frame-to-frame
  correlation with a search area twice the region, the original picture
  in the Effect Preview, Progressive Source without effect yet.
* Editing titles in (docs/spec/title-tool.md, 6.8e1): a roll's finish staying
  the roll's end whatever the source OUT, Place in Timeline's tracks (the
  sequence's video tracks and the next one).
* Colour correction (docs/spec/color-correction.md, 6.10a1): the default Luma
  Ranges curves (smooth thirds through the guide's 175 and 215 figures), the
  curves' shares made to add up to one, the formulas for Contrast (about mid
  grey, 100 a slope of 20), Gain/Gamma/Setup on the 16-235 range, the ChromaWheels'
  Amount 100 as a quarter of the chroma range, Clip Low/High only once set.
  6.10a2: the groups applied one after another in the tab order (HSL,
  Channels, Levels, Curves); the Channels tab's Cr and Cb centred on mid grey
  and Offset 100% as full scale; the Levels gray point as a power curve, the
  Levels tabs' order (Master, Red/Green/Blue, Luma, Composite) and Composite as
  the levels on both ends of the composite swing; the Curves graphs as monotone
  cubics, Master before the colour graphs.
  6.10a3: the vectors' predefined centres (but standard Red's 30 degrees and
  custom 3's 35/40), the saturation scale (100 at half the chroma range), the
  membership and Softness formulas, the output as added hue and saturation
  changes, the syringe's choice of the nearest predefined centre, Secondary
  after Curves.
  6.10b1: merging the Source and Program sides (the Program side's set
  controls win), the corrected track (highest selected video track),
  filler not a shot, Remove Effect taking both sides, Reference Current's
  monitor, Dual Split's starting box and handle corners, Play Loop's later
  passes from the start, no position bars under the three monitors.
  6.10b2: the tool as a panel that opens and closes with the mode, an undo
  step per let-go (no live preview while dragging), an Enable button lit
  only with something set under it, slider decimals, Shift's quarter-speed
  wheel drag and the pointer shapes, straight Luma Ranges curves, Three-tone
  by the largest share.
  6.10b3a: the Component Type buttons' "% Name" labels and Invert as a
  toggle in their menu, Levels points dragged as input and output together,
  the histograms' 160-by-90 sample, Master counting all three channels and
  Composite both ends of the chroma swing, Curves points kept between
  neighbours.
  6.10b3b: the selected vector kept in the correction (selection as an undo
  step), the vectors drawn in their own colours, only the selected centre
  draggable, swatches at mid luma and at least saturation 20, samples taken
  from the drawn picture and applied on release.
  6.10b4a: the units' scales but 10 Bit (Percent of 0-255, IRE and mVolts from
  reference black), which menus offer which units and which values each covers,
  Solid Histograms on by default, Dynamic Histograms through the side's own
  correction, Use Full Ranges widening only the axis, the colour backgrounds,
  3 x 3 averaging for every sampled colour, Dual-Split during playback on by
  default and what turning it off does, the AutoCorrect defaults, the Preview
  tab's Pre-Roll and Post-Roll left unused (FX Guide p.594 gives Edit Review
  the Trim settings').
  6.10b4b: the tool buttons' icons, only the tool's Effect Mode button opening
  the Effect Editor (p.684), the others leaving the mode (p.589), comments kept on the segment for both
  sides (and on both halves of a cut), the Tab buttons' names (Tab 1-12), their
  storage in the Correction setting and working only in the mode.
  6.10c1: the scopes' sampling and brightness, their graticule lines, the HDR
  waveforms as PQ code values, the YC Waveform's -20 to 120 IRE limits for its
  red edge, the vectorscope's scale, scope monitors never active, PAL for 25
  and 50 fps projects.
  6.10c2: how the automatic corrections measure and solve (half-percent tails,
  the linear Gain/Setup solve, a grey world per luma range, the Curves points),
  Remove Color Cast's share threshold and one sample per click, the buttons as
  text buttons.
  6.10c3a: which controls each Color Match type changes, NaturalMatch's target
  colour, swatches filled on release rather than during the drag, the control's
  state kept for the session, the swatches' numbers on the video scale.
  6.10c3b: custom colours as Effect items, the RGB and Name labels' form, the
  nearest CSS colour name, swatch-to-swatch drops, Picks from Anywhere taking
  the colour the window draws without averaging.
  6.10d1: a copy of a relationship correction on each related segment (later
  segments only through Update Correction), related segments on every video
  track, the tape from the Tape source or column, a converted correction's left
  out segments kept as segment corrections, Use Marks for the segment
  relationships only, the button following a segment only when the position
  moves, the indicator lines' width and colours.
  6.10d2a: templates as Effect items with both sides, the side a template lacks
  removed by a monitor or Timeline drop, a tab drop changing the tab shown and
  everything under it, a drop anywhere on the mode's monitors, the template's
  relationship winning over p.598's monitor drop, the bucket icon, Save
  Correction's bin.
  6.10d2b: the Color Correction Presets themselves (Black and White, Brighten,
  Darken, Desaturate, High and Low Contrast, Saturate) as templates, automatic
  templates kept for the session, the same segment and side updating the
  newest, the palette list's "N  name" text, Enter in the Effect Palette.
  6.10d3a: correction keyframes Elastic, Curves and Levels points held between
  keyframes, an adjustment keyframing only controls that already have keyframes,
  a reset dropping a control's keyframes, the Program side's keyframes winning.
  6.10d3b: the Effect Editor's category and parameter names for a correction,
  every hue -360 to 360 there, the editor showing only the correction in the
  mode, no group Enable buttons in the editor.
  6.10d3c: the bar's segment span and selection by frame, a selected keyframe
  taking the tool's adjustments, one Enable button per Levels tab in the
  editor, the Curves tab's Master sliders shown as HSL Master's.
  6.10d4: Update and Merge giving segments the first relationship that has a
  correction (Tape, Clip Name, Master Clip, Sub Clip; Track), Prefer Latest
  by the sequences' modified times, overlap meaning the same source clip and
  track with frames in common (Prog Segment on the same track), Remove's
  Segment boxes off at first (its picture, p.706; the text says all), Flatten's
  Segment boxes dimmed, counts shown only in Flatten and Remove, the copy named
  "<name>.Copy.NN" and loaded in the Record monitor, the commands on the
  Command Palette's CC tab too.
  6.10e1: the menu's "CC Effect" text, the tool turning to it on filler and back on
  a clip, a segment with another effect refusing it, the AutoCorrect frame (the
  position in the segment, else its first frame), the filler the sequence's length.
  6.10e2: the Color Info tool's sampling rate, its Video LUT column showing the
  video values, the 10-bit, IRE and mV conversions, the Color Info button's icon.
  6.10e3: the shape layer's storage, Softness as a blur of up to 100 pixels,
  Invert Alpha correcting outside the shapes; the panel's place left of the
  group panes, what the monitors show without Show Result (the layer worked on),
  Show Alpha's grey picture, Softness dimmed without a selected shape, our icons;
  the shape tools only on the Shapes layer and only in the Current monitor; the
  Effect Editor's categories for the effect (Layers, Shapes, Shape Tracking, no
  Show Result/Show Alpha/layer menu there), New Tracker in the tracker menu.
  6.10e4a: the Safe Colors dialog's layout (one row per value), units read as
  chosen (no conversion), IRE/percent without setup, Rec. 601 composite as luma
  plus and minus chroma, half a level of tolerance, the graph's span.
  6.10e4b: pictures read as levels 16-235 for the warnings, the warning indicators'
  size and place (right of the triangle), the swatches' ⚠ character, scopes
  following the limits whatever the Actions.
  6.10e4c1: the Safe Color Limiter's limiting (Rec. 601 luma shift, chroma
  scaled for the composite, RGB clamp, 422 Safe as repeated passes), both Enable
  buttons on at first, the sliders' -300 to 1100 range, units not converted.
  6.10e4c2: the analysis display while the Effect Editor is visible (any mode),
  the Record frame without limiters, the background at a third, the order of
  highlight colours, shown in an empty Source monitor too.
  6.10e5: Color Match as two swatch parameters, R+G+B curves matching, Spot Color
  Correction's Color Match always on once both swatches are set; p.733's Luma
  Adjust/Chroma Adjust kept as the reference's Color Adjust.
* OFX plug-ins (docs/spec/ofx-host.md, 6.11a) in place of AVX: the category of a
  plug-in with no grouping ("OFX Plug-Ins"), a second plug-in of the same label named
  with its identifier, plug-in effects without the green dot, the plug-in icon's drawing,
  the order of contexts a plug-in is described in (Filter, Generator, Transition, General).
  6.11b1: the "<label> Plug-In Parameters" category, the controls chosen for each OFX parameter type (decimals,
  ranges, 2D/3D as sliders per axis, RGBA without alpha), one instance per plug-in, renders one at a time, the
  same frame for every time of an input clip, segment plug-ins on transitions working on the incoming picture.
  6.11b2: the "OFX Plug-In" tag in the effect, the blank icon drawn as the tile's outline, a missing plug-in's
  transition dissolving, "Can't find effect: <label> (<id>)" once per plug-in, closeallofxplugins.
  6.11c1: General chosen when it has more inputs; the dialog's name "OFX Optional Inputs" and all inputs chosen at first;
  inputs as nest tracks V1, V2 ...; the Angle control's look (0° up, clockwise) and keys; values a plug-in sets kept
  only during a push button's action.
  6.11c2: V2 overlays only (no OpenGL for V1), the standard overlay colours, stipples drawn dashed, the pixel scale,
  1920 x 1080 canonical until the project's size is set.
  6.11c3 (colour management; the guide is silent on it): the project colour spaces' native names (601 as Rec.709,
  ACES RGB as ACES2065-1), Core without OpenColorIO and OCIO with it, the automatic config ($OCIO, then the built-in
  studio config), only colour-managed plug-ins asked for clip preferences, the project's colour space when a
  preference cannot be supplied, sRGB as the display of a scene-referred project, no OCIO display or view.
* Movie export (docs/spec/movie-export.md, 7.1a): one Compression menu naming each family in place of the three
  menus, its entries and encoders, the Bit Rate as one value, the Image Size presets, Center crop filling the raster,
  even sizes, Opus in MP4s without AAC, a clip exported through an AutoSequence, the marks of the monitor showing the
  item, all tracks for an item not in the Record monitor, several items written beside the first. 7.1b: sequence file
  names counted from 0 with 7 digits, DPX and OpenEXR always sequences, OpenEXR display-referred, "Scale from Legal to
  Full Range" as the default Color Levels, a repeated marker comment named with its frame. 7.1c1: copies named " 2"
  when taken, consolidated media under `<folder>/Avid MediaFiles/MXF/1`, consolidated clips keeping their names in the
  AAF, one consolidation for both tabs. 7.1c2 (docs/spec/send-to.md): .stt files as JSON in Send_To_Templates beside the
  user's settings, what each Pro Tools option writes, consolidated media beside the AAF, the summary listing every
  option, no Disc/Sorenson/Avid DS templates, no drag export. 7.1c3 (docs/spec/aaf.md "Simplified AAF"): one mixdown
  clip per stretch of effects, named "<sequence> Effects Mixdown <n>", nests counted as effects, the Audio Tracks label,
  the simplified sequence only in the AAF, excluded media left linked, the choice among the open bins' sequences. 7.2a
  (docs/spec/movie-export.md "MXF OP1a"): the XDCAM and AVC Long-GOP encodings, JPEG 2000's 4:2:2 10-bit, MXF sound as
  one track of the mix's channels at 48 kHz, DPX's Full Range and the sequence's FPS as defaults, one DPX frame numbered. 7.2b
  (docs/spec/export-device.md): XDCAM disks recognised by their Clip folder, clips numbered by us, the stereo mix only,
  the first clip's channels as the disk's track count, no built-in P2 device. 7.2c (docs/spec/as11.md): AMWA's
  shims as a menu in place of Avid's shim files, the DM fields offered, the rewrap through bmxtranswrap. 7.2d
  (docs/spec/as02.md): the volume's Video Compression, one bundle per sequence, any shim XML read for its three names,
  Media Status as text. 7.3a (docs/spec/digital-cut.md): tapes kept in folders, Full Quality the only Output
  Mode, Local mode as a crash record where the deck is, Preview as a cue and a report, frames made as fast as they can be,
  the stereo mix onto the first two enabled tape tracks. 7.3b (docs/spec/video-output.md): calibration sliders -100 to
  100 around 0, a lit preset clicked showing the manual level, 75% bars with 100% white, test patterns at 1920 x 1080. 7.4a (docs/spec/subcap.md): the SubCap picture's defaults, the frames
  between imported captions keeping the effect without text, one ASCII TTI block per caption, stylesheets as flat XML
  beside the user settings, Synchronize "Sequence" until chosen. 7.4b1 (docs/spec/closed-captions.md): caption
  data kept in the bin item, colours and styles not kept, the 80% caption grid in the monitors, the D1 row's drawing,
  a broken caption as a cut through a sending span, SCC lines before the start left out, TTML layout and times.
  7.4b2 (docs/spec/timeline-find.md): what each Quick Find option searches, Enter finding to the right, the position
  kept without a match, options not kept. 7.5 (docs/spec/dcp.md): all of DCP export, which the guide only names
  (p.519): Export As DCP and its tab, the package folder named for the file, the rates, letterboxing, the 5.1 layout.
  7.6 (docs/spec/titler.md): our icons for the Titler+ tools and the Effect Editor's side buttons, the Dashboard's keyframe
  row, the Effect Editor's Titler+ categories open at first, the shadow angle as a number.
* PlasmaWipe (docs/spec/plasma-wipe.md, 6.9e): our own gradient effects in the
  "PlasmaWipe Basic ..." categories in place of Avid's 64 presets, how Soft and
  Width act on the gradient, the frame's colouring by its grey, the defaults
  (Soft 40, Level 50 on segments; frames Soft 0, Level 5, grey and white), no
  Chroma Key Profile category (p.478 lists it; nothing in ch.16 uses it).
* Scratch Removal (docs/spec/intraframe.md, 6.9d): the Frame/Field default -1
  of p.547 (p.427's table says 0; both name the first frame From Start), the
  slider's -10 to +10 range with frames kept to ±5, the Scratch Removal button on
  the Tool palette in Clear Both Marks's place, isolating whole frames (no
  field-accurate segments), the effect auto-nested over the segment's effect.
* Previsualization Markers and Export Matte PICT (docs/spec/intraframe.md, 6.9c2):
  the marker's look (a disc 20 pixels across, a square in the Effect Preview),
  the tool returning to Selection, a message instead of the default Export
  command when no AniMatte effect is shown, Magic Mask's colours from the
  Record monitor's picture, PackBits-packed planes in the PICT file.
* Intraframe brushes (docs/spec/intraframe.md, 6.9c1): the Brush Shape buttons'
  shapes (Circle, Oval, Ellipse, Square), the Brush Preview window's scale (Size
  as a percentage of its width), the Brush Template icon's picture, a click on a
  Color Preview window opening the colour dialog, picked colours as shown
  (effects included), Alt+click picking in any Paint mode.
* Intraframe reshaping (docs/spec/intraframe.md, 6.9b2): the Object menu as a
  button by the tools, new smooth handles a third of the way to the neighbours,
  joins changed in the shape being edited, the Z-Rotation arm 8% of the width,
  one-pixel nudges of a 1920-wide frame.
* Intraframe tools (docs/spec/intraframe.md, 6.9b1): lettered tool buttons,
  wire frames for every object, Return and Escape for polygons, freehand points
  every 1.5% of the width, the lasso taking objects wholly inside it, moves and
  scales as the objects' X, Y and Scale.
* Intraframe objects (docs/spec/intraframe.md, 6.9a): shapes interpolated
  linearly between keyframes, objects keeping their own parameters (copied from
  the effect's when drawn), offsets in pixels of a frame 1920 wide, the brush
  heads Circle, Oval, Ellipse and Square, Magic Mask's tolerance in steps of four,
  Feathering's Bias as a remapping of the blurred edge, Amt in 8-bit steps, the
  Outline mode two pixels wide, Unsharp Mask's Amt as a percentage, Emboss and
  Gradient as described there, AniMatte objects combined in layer order, Blur
  and Mosaic showing nothing without objects, Color Match unchanged until 6.10,
  the Scratch Removal mode taking its material as 6.9d describes.
* Titles promoted to 3D (docs/spec/title-tool.md, 6.8e3b): a 3D Warp effect that
  keeps the title and its media, still revised as a title; Scaling starting at
  100; Rolling and Crawling titles promoted too.
* Title media (docs/spec/title-tool.md, 6.8e3a): Re-create Title Media on the
  whole sequence without marks, its precomputes in the sequence's bin, no
  multiformat check boxes; Wrong Format for media made at another raster than
  the project's (our resolutions are codecs, which play at any raster); the HD
  title filter's [1 2 1] / 4 vertical kernel from 720 lines, off at start of
  each session; Use Classic Character Mapping dimmed (titles keep Unicode
  text, so there are no older mappings to return to).
* Revising titles in sequences (docs/spec/title-tool.md, 6.8e2): no Marquee
  prompt, the Edit Title button above the parameter groups, Place in Timeline
  dimmed while revising, a segment whose clip's bin is closed saved as a new
  clip, a fill track replaced by whatever is on the nest's V1 that is not the
  fill precompute, the Title effect's Scaling, Position and Crop applied as
  Resize to fill and matte alike.
* Promote Titles (docs/spec/title-tool.md, 6.8d4c): lines as turned rectangles,
  blends as four-corner gradients, transparency blends as their mean, depth
  shadows as drop shadows, rounded corners, arrowheads, groups and locks left
  out, rolls and crawls as Roll and Crawl Left.
* Classic title templates and styles (docs/spec/title-tool.md, 6.8d4b): `.ttpl`
  JSON files, templates fixed but for their text (fonts too), moved by dragging
  and arrow keys instead of Arrow buttons, styles applied to any selected object,
  F1-F12 without modifiers, "Aa" samples, no Refresh button, the Title Style
  setting kind listed with the User settings (FX p.364).
* Classic Title Effect clips (docs/spec/title-tool.md, 6.8d4a): two precompute
  clips "<name> Fill"/"<name> Matte" instead of one, "unrendered.<name>",
  Save Title As numbering when the bin has a Title clip of the name, rolls and
  crawls moving evenly through the clip until 6.8e, Place in Timeline dimmed
  until 6.8e, Export Title as PNG/TIFF (not PICT), Frame view showing the
  title without the Home key. Aspect ratios (9.5): 16:9 and 4:3 check boxes,
  the project's checked to start with, the other saved as "<name> 4:3", a
  4:3 title's matte cleared outside the centre cut.
* Classic title manipulation (docs/spec/title-tool.md, 6.8d3b): one level of
  groups (grouping groups merges them), locked text not typed in, groups
  aligned as units, Distribute Left to Right/Top to Bottom spanning the safe
  title area, First to Last by centres, Duplicate's 20-pixel offset with
  copies unlocked in new groups, the name Deselect All.
* Classic title attributes (docs/spec/title-tool.md, 6.8d3a): the standard
  corner radii (10-80) and widths (1-10), borders outside the edge, arrowhead
  sizes in line widths (3 by 3 until set) and the Arrowhead Size dialog's scale,
  blend directions as degrees clockwise from left to right, Remove Border
  leaving a line's width, a width with nothing selected setting both the
  line and the border default, shadow direction as an angle (45 until set),
  softness 1-3 rounded to 4 or 0, the session (not the next one) keeping the
  softness, QColorDialog for the Color Picker, the attribute row above the
  toolbar.
* Classic rolling and crawling titles (docs/spec/title-tool.md, 6.8d2): pages
  under or beside each other with page 1 at the origin, the limits by frame
  height, Insert Page refused across an object, Make Crawl's place and width,
  the arrows scrolling 54 pixels only with nothing selected, the lit button
  making the title still.
* The classic Title Tool (docs/spec/title-tool.md, 6.8d1): 1920 x 1080 frame
  pixels from the top-left with points as pixels, Titler+'s text layout, the
  window's own Edit/Object/Alignment menus and undo, Pair Kerning always on,
  Loose/Tight as +/-10 percent and Alt+arrows by one percent, new text out to
  the safe title edge, the grid sizes, the Safe Colors limits, Marquee dimmed.
* Titler+ presets and EDL titles (docs/spec/titler.md, edl.md; 6.8c4b): the
  sections a preset drop picks, the whole style elsewhere without the
  Transform, the source object by kind, title events with reel BL, their
  names numbered and written with Clip Names off.
* Titler+ templates (docs/spec/titler.md, 6.8c4a): a template edited as a
  sequence of its length (ten seconds when unknown) kept out of the bins,
  Save As naming its copy "<name> Copy" and editing that, Save and Save As
  with no template open making a new one in the active bin, the save prompt
  coming after the other sequence is loaded.
* Titler+ Roll / Crawl and tracking (docs/spec/titler.md, 6.8c3): the path's
  ends (bounds just outside a 1920 x 1080 frame), the default linear keyframes
  being Fixed, Edit Mode off leaving the tools inactive, the Effect Position
  number standing in for the scroll bar, tracked positions in 1920 x 1080
  title pixels, the Tracking window taking the mouse.
* Titler+ character formatting (docs/spec/titler.md, 6.8c2): which properties
  characters can have (family, style, size, underline, kerning, fill and
  outline colours), new characters taking the format before them, a line as
  tall as its tallest character, Shift+click extending the highlight.
* Titler+ keyframes (docs/spec/titler.md, 6.8c1): the keyframed numbers and
  their `Titler:<object id>` categories, always Fixed, the panel's Add/Delete
  Keyframe buttons working on the selection or the whole title, keyframes in
  the position bar only, Fixed keyframes moving with a split's second half.
* Titler+ (docs/spec/titler.md): 1920 x 1080 pixel coordinates with y
  down, font sizes in those pixels, the defaults (72-pixel font, 4-pixel
  black outline, shadow 75% 8 pixels at 45 degrees), vertical justification
  by baselines, Right To Left and Top To Bottom by characters, the coats'
  order and box blur, the Foreground crop in percent, titles on segments
  with effects nesting those, the toolbar button's drawing; 6.8b1: editing
  in Effect mode's Effect Preview, typing and drags as one undo step at their
  end, Home/End over the whole text, scaling always from the anchor point,
  the handle and rotation reaches (16 and 40 title pixels), drawn shapes of
  at least 4 pixels, Page Up/Down going round; 6.8b2: one panel for the
  Dashboard and the Effect Editor, values stored when entered, Align Center
  vs Horizontal/Vertical Center, the grid's 90%/80% areas, anchor presets
  on the object's own bounds, a solid colour setting the gradient corners.
* The Key effects (docs/spec/keys.md): the mattes' formulas (distances,
  Gain and Soft ranges, the SpectraMatte wedge, spill), blue and black key
  colours with Gain 20 and Soft 10 until set, Chroma Key on a transition as
  the keyed incoming over the outgoing, Matte Key's matte as luma.
* Volume and pan automation (docs/spec/automation.md):
  * pan automation takes the place of the clip pan instead of adding to it;
  * a -∞ keyframe ramps from -96 dB;
  * an Auto mode fader between keyframes moves the keyframes either side;
  * a value drag also changes the hidden copy of the keyframe in the other
    half of an Add Edit;
  * the pan graph runs from L at the top to R at the bottom;
  * Audio Data acts on all tracks.
* Audio EQ (docs/spec/audio-eq.md):
  * the filter shapes (Audio EQ Cookbook shelves and peak);
  * the midrange starts at 500 Hz, and its range is 50 Hz to 15 kHz;
  * a change in the tool sets the segment under the position at once;
  * Set EQ - In/Out with only an OUT acts from the start;
  * the Timeline's EQ icon;
  * Audio Mixdown keeps the EQ while Bypass RT EQ is on.
  * the predefined templates' values, except Telephone EQ "A" (observed)
    and Tape Hiss Filter's 4 kHz (p.743): recheck them against a seat's
    Console (src/ui/resources/eq_templates.json).
* Audio Punch-In (docs/spec/punch-in.md):
  * a new track goes with the first punch-in's undo, and every punch-in
    is its own undo step (the guide's third undo removes all of them);
  * the punch-out is at the frame before the position;
  * the whole target track is silenced while a take runs;
  * Automatic monitoring judges by the input device's own delay.
* Audio Track Effects (docs/spec/track-effects.md):
  * the built-in effects (our names, parameters and DSP);
  * Ctrl+Bypass is not saved;
  * locked tracks refuse insert changes;
  * a double-click in the Effect Palette inserts an Audio Track effect;
  * an effect's tail in filler keeps the last piece's pan.
* Group clips (docs/spec/multicamera.md):
  * the group covering every clip once synced;
  * its timecode (the first clip's) and tracks (V1 and the most audio tracks);
  * Source monitor marks before the bin's Mark IN and Mark OUT for Inpoints and Outpoints;
  * Inpoints chosen when the dialog opens;
  * angles kept as copies of their clips;
  * the yellow outline on the selected angle in the split views;
  * which Record monitor segment Previous/Next In Group switches, and only its picture;
  * the Up and Down Arrow keys switching angles only with no segments selected.
  * syncing by sound: loudness envelopes cross-correlated, a correlation of at least 0.2, the first 20
    minutes of A1;
  * Waveform Sync moving the video with Lift/Overwrite against the earliest selected audio segment;
  * Edit Group's sequence kept in the group's bin as "<group>.Edit", and the order of a group's angles
    made from a sequence;
  * segments keeping their angle offsets until Refresh Sequence > Group Clips.
  * Sync Clips (docs/spec/sync-clips.md): the name for the guide's AutoSync; Include audio from audio-only clips off
    keeping all their tracks; the dialog opening as last left; synced subclips as subclips holding a sequence.
  * MultiCamera mode entered from Full-Monitor display showing the Quad Split; the defaults of Split Mode Play
    (Quad or Nine Split) and Show Track in Split View When Playing (the monitored track, stopped too);
  * a choice in Quad or Nine Split Source view becoming the group's selected angle; stopped choices in
    MultiCamera mode switching the whole segment.
  * the Group menu's patching: unpatched audio tracks from the selected angle and V1 always from it, an audio choice in
    MultiCamera mode switching the segment on the sequence track of the same name; Change Source Track on group
    segments listing the shown angle's tracks, without More; Audio Follow Video's A1 cut not drawn as a
    match frame edit; Multi-angle View choices cleared with the split or bank; committed segments kept as group
    segments where their angle has no material.
  * MultiGroup (p.1125): takes as clips whose timecode spans overlap, takes of one clip left out, the groups strung end to
    end in timecode order, the name "<first clip>.MGrp.n", Previous/Next In Group switching the multigroup's group
    segment under the Source position (V1, and A1 with Audio Follow Video), the plus sign drawn before the group icon.
* Transcripts (docs/spec/transcripts.md): the store outside the project, one file per clip by MobID; lines ending at
  speaker turns, 2-second pauses and 30-second sentences; Speaker IDs alternating at turns; the Language Hint's
  languages; background transcription starting new clips after ten idle seconds and finishing a running one; the
  Sidebar's Transcription column; audio-only master clips transcribed too.
* The Transcript Tool (docs/spec/transcripts.md): the side following the last load, search ignoring punctuation and
  going round, Transcribe at High priority and for excluded bins too, the timecode, speaker and words on one line, a clip
  on two tracks shown once.
* Transcript editing (docs/spec/transcripts.md): unchanged words found by the longest common sequence, new words beside
  a kept one sharing its time, Find and Replace's own punctuation, Identify Speaker naming a speaker heard on one line
  only, edits not undoable, Group Speaker Lines and New Line After not kept between sessions.
* Background services (docs/spec/background-services.md): services in the application (no separate process or taskbar
  icon), a running job finishing when they stop, Always Start ... at Launch on to start with, Pause in minutes (10), the
  pause during playback rechecked each second, the options in the user's Interface setting, the indicator drawn by us
  before the Quick Find field. Dynamic Media Folders: kept in Dynamic_Media_Folders.json beside the user's settings,
  looked in every 2 seconds, an entry taken once it looks the same twice, an entry done whether it worked or not,
  Link Using kept with FFmpeg linking, Start from four pairs of digits or eight digits in the file name, Checkin,
  Convert Audio Format, Relink before skipping, Apply Reformatting and Apply color transformations kept without effect.
  The window's "-" button, the Profile Editor's sections open to start with and its summary in words, Track Formats
  typed in place of the Multiple Mixes dialog, the Default FPS menu's rates, an entry acquired while processing as an
  offline clip named after it, a new bin when Use active bin finds none.
* Reformatting and FrameFlex (docs/spec/frameflex.md): clips without a Reformat attribute pillarboxed or letterboxed as
  before 8.5a; the column changed through its right-click menu; FrameFlex X and Y in pixels from the centre, Size as the
  box's width over the image's, Z Rotation anticlockwise; the green dot only for a FrameFlex box; Show Adapters >
  Spatial dimmed until 8.5b. Source Settings: the box's colours (Yellow, Red, Green, Blue, White), the rotation handle
  18 pixels above the top edge, OK applying, Image and Frame aspect 0 meaning the image's own. Playback Rate: only
  clips without sound, the menu's texts "Original: <rate> fps" and "Project: <rate> fps", Refresh Sequence only
  dropping adapters. Panning (8.5b3): the FrameFlex effect left out of the Effect Palette, a segment effect shown before
  it, its sliders' ranges, the whole image pillarboxed or letterboxed in the Effect Preview, the rotation handle 4% of the
  picture's width above the box, the box drawn while bypassed, the Layout button lettered above the groups, Alt+Layout
  without keyframes resetting the values, frames counted from the source IN under a motion effect.
* Colour adapters (docs/spec/color-adapters.md): the Source color space menu's entries, Auto taking every project as
  Rec. 709, the Color Transformations column's text ("Bypassed: ..."), Log-C and C-Log scene-referred without a tone
  curve and full range, Sony's looks only from a provider, Remove Color Adapters undone by a later Refresh. The Color Encoding tab laid out
  top to bottom (space and Auto, the list, the drop-down with Add and Delete, Bypass), the dialog opening with the first
  clip's encoding; Transcode's Frame Flex dimmed, colours baked through 8 bits. LUTs (8.5d1): Avid DS, LUTher and Nucoda files refused,
  the shared LUT folder in the application's data, the CDL transformation's name carrying its values, its place at the end
  of the drop-down. Color Management (8.5d2): Insert color transformations and Use CDL values off to start with, Installed
  LUTs on Project, Select LUT file using the saved choice, the colour space from the transfer characteristic alone, CLF
  kept as a 33-point cube, the HDR conversion choices kept unused until 8.5e. Display Color Space (8.5e1, 8.5e4a):
  the Viewer tab's choices (p.1161) in the right-click menu, which lasts until the setting is applied; BT.1886 gamma 2.4
  for Rec709/Rec2020, the sRGB curve for SRGB and gamma 2.6 with the DCI white for DCI P3, Bradford adaptation; Rec709
  (full range) shown as Rec709; PQ/HLG reference white 203 cd/m² at the display's white, clipped above. ACES (8.5e4b1):
  the ACES adapters' names and which are built in, Bradford rather than CAT02, the working space in 8 bits, the output
  transform as a fitted tone curve, ACES Generic chosen first; the Output Color Space choices (Rec709, SRGB, DCI P3,
  Rec2020, Rec709 first) and Project on the monitors following it; the blue Video Quality Menu a sign only.
  Proxy (8.5e2): the Format tab's Off, 1/4 and 1/16 texts; exports at the project's raster; no green dot for proxy
  resizing; the latest render of each other mode kept. Mask margins (8.5e3): presets centred, letterboxed or
  pillarboxed; a typed percentage makes Custom...; without margins the menu shows the picture's shape; Mix to White/Black
  at half; the right-click Target Mask lasts until the Composer setting is applied again; the preset "1.32:1" as the
  picture on p.551 reads.
* Dynamic Relink (docs/spec/dynamic-relink.md, 8.6a): our managed media naming a clip's MobID as the Media Indexer's
  resolutions; the setting's three tabs (the top area as a tab of its own); Keep Existing Media first on both tabs; the
  Relink Parameters as menus starting with Any, and their values; Highest Quality as the project's raster, then pixels,
  then bytes a frame; field order and colour space not compared; clips back on their own media when the setting is off;
  Create new clips dimmed without Dynamic Relink; sequences' clips still getting new clips. The MultiRez button (8.6b):
  its place after the Video Quality Menu, the click order, its picture, Resolution Tracking offered with Dynamic Relink
  on and coloured blue/yellow by default with the Offline red, Override dimmed with Dynamic Relink off, mismatched
  renders against the Render tab's resolution, Find Next's message when the display is inactive. Partial and shared
  media (8.6c): a clip's source as its tape or source file, spans compared by timecode, partly online media not played,
  the headings as our resolutions and 32-96 kHz, ●/◐/○ as the circles, the Drive column left as it is, Delete's
  resolution boxes all ticked, whole multiples as cross rates.
* Stereoscopic clips (8.6d1, docs/spec/stereoscopic.md; the guide leaves making them to the Help, p.213): Create
  Stereoscopic Clip and the S3D Leading Eye submenu on the bin's right-click menu, the left eye found by its name's L or
  Left ending else bin order, the name "<left> S3D", the shorter eye's length, refused in SD projects; the left eye on the
  left or on top in Side by Side and Over / Under; Full showing the leading eye until the S3D views (8.6d2); the
  Stereoscopic option changeable after the project is made (`MainWindow::setProjectStereoscopic`). S3D Views (8.6d2): applied
  only in Full, Side by Side and Over / Under projects, Frame Compatible as the default and Side by Side in Full projects,
  Mono's eyes and Amber-Blue where the guide lists them, Difference as mid gray plus half the brightness difference, the
  anaglyphs as channel swaps, Compare's diagonal, Full Frame side by side, Follow S/R as the Video Output tool's default;
  the parallax guides as vertical line pairs about the centre; S3D Settings' units, defaults and parallax formula, the
  calculated values written into the setting. 8.6d3: groups in the Source monitor showing each clip's leading eye unless
  Left/Right Eye Only, the S3D Contributor for EDLs only, Refresh Sequence > Stereo Correction Effects dimmed (no
  source-side stereo effects described), the S3D preset turning Stereoscopic on as Full, Full Quality only in interlaced
  projects with Stereoscopic on.
* Proxy media (8.7a1, docs/spec/proxy-media.md): the media drives for the Create Proxies window's drive or workspace, one
  Background Queue job per clip, subclips/groups/sequences giving their master clips, proxies at half the raster (at
  least 256 by 144) in Avid MediaFiles/Proxy/1 with the sound of 48 kHz media only, the proxy columns after the MultiRez
  headings with Proxy Offline as "V"/"V A", orange Play for both preferred modes and the mode kept for the session, the
  other proxy file when one is gone, sound played from the clip's own media.
* Copy Media and Distributed Processing (8.7a2, docs/spec/proxy-media.md): Copy Media in no menu bar menu, bin items copying
  their own bin under Bins, bins saved first, Copy Bins to starting at the first drive, proxies in a folder's Avid Copied
  Media, files of the same name and size not copied again and " 2" for a second of one name, the not-enough-space and
  offline lines; storage modules' workspaces after the media drives, Processing Options dimmed with Distributed Processing,
  jobs polled every two seconds.
* Universal Mastering's sample rates (8.7c2a, docs/spec/sequence-format.md): the six conversions offered in every project,
  after the three plain rates, written as WAV.
* The Frame view's represented frame (8.7d3): K+L and K+J roll at 8 frames a second; the keys K, L, J, Home, End, N
  and ; go to the Frame view while it has clips selected; L alone is not ours there.
* Mouse Jog and Mouse Shuttle (8.7d2, docs/spec/composer-display.md): 4 pixels a frame for jogging, 1 fps a pixel for
  shuttling; sound only at whole multiples of normal speed; clicks while on go to the mouse control.
* Font replacement (8.7d1, docs/spec/title-tool.md): the Site setting is one "Font Replacement" entry in place of
  AvidFontSub.avt; replacing on opening is not a change to save; the dialog's message and preview text are ours.
* Single-field step (8.7c3, docs/spec/sequence-format.md): field 1 shows the whole frame (the guide: "displays the
  first field of every frame"); field 2 its lower lines, each shown twice; the "2" in the picture's upper right corner in
  the palette's text colour. The 60 fps Mark IN warning is checked against the Digital Cut tool's Mark IN.
* The Digital Cut's output formats (8.7c2b, docs/spec/digital-cut.md): the menu's descriptions shortened from the guide's
  table; the 1080 table's descriptions ours; the project's own play rate first; 23.976p SD projects offer the NTSC formats
  only (the guide's "all output play rates are available" read as Symphony's); SD downconverts letterboxed; the
  sound resampled with the speed change, so it stays in sync with or without remastered audio; NTSC SD tapes lower field
  first.
* Changing the Project Format (8.7c1b, docs/spec/sequence-format.md): Project Type as a menu of the same-rate presets, the
  project's first; SD aspect kept between SD formats; Proxy back to Off when the new raster has no such mode.
* Set Format (8.7c1a, docs/spec/sequence-format.md): the Format menu as all project presets with the project's first, the
  dialog starting on Set Format for sequences, "<name> <rate> fps", segment times and markers kept, offline "offline-<id>"
  clips, no motion adapters, the message box and Console lines.
* Peer-to-peer transcript sharing (8.7b, docs/spec/transcripts.md): Host/Use Shared Transcripts as a Transcript Sharing
  menu (with Off) and a Shared Transcripts Folder in the Transcript setting, any shared storage for Avid NEXIS, hosting
  copying this station's transcripts in, lock files per entry, the "Transcript Locked" question, Console lines for a
  folder off shared storage and an unreachable host.
* Scripts (docs/spec/scriptsync.md): the script kept in its bin item instead of an .avc file, a changed script saved
  when another opens, tabs as 8 columns, a 600-pixel text width to start with, an empty number removing a scene or page,
  the status bar's wording, the editor's own undo. Slates (8.3b): where slates stand (halfway across the text column, above
  their line, new ones beside those over the same lines, a top margin for them), blue take lines, Script > Color's six
  colours, lettered OS/SC buttons, Shift+arrows stepping ten frames, Alt+click writing to the Console in place of the Info
  window, a slate's Shift+click adding its active take. Marks (8.3c): Add Script Mark on the take last loaded and the first
  selected line, the Record and Find Bin buttons lettered and placed after Play, Add Script Mark and Find Script in a
  column of their own on the Other tab, a range before every mark splicing nothing. ScriptSync AI (8.3d): transcripts as the
  analysis, the half-the-words rule, Dialog Indent 10 to start, options kept for the session, no progress tracker; Create
  Script from Clip's "<clip> Script" name, "Speaker: words" lines and the empty line between clips. Shared scripts (8.3e):
  any shared storage (not only NEXIS), "(modified)" as the bins say it, the lock icon a square glyph after the toolbar's
  buttons, dark red for protected bins, Save Script As into the active (or first writable) bin; the Find window's Script
  Text columns Script/Line/Text and its filter rows, Scripts in Project finding the takes' clips.
* AutoCut (docs/spec/transcripts.md): Assign to Camera Angle on the speaker's right-click menu, kept in the group by
  speaker name; the group's shown transcript; only video tracks switched.
* Transcripts out (docs/spec/transcripts.md): the exported .txt as an Avid DS caption file with the speaker as its first
  row, several items into a folder, two rows to a caption under the character limit (32 offered), Create SubCap on a new
  top track, the shared file as our JSON `.cttr`, Manage Transcripts listing the current project's clips with Export.
* PhraseFind AI (docs/spec/transcripts.md): the scores below 100 (half or more of the words in a line, a word's start
  counting half), one row per place, no Cancel button, the Search Data Directory fixed, indexing meaning background
  transcription.
* Manage Speaker Names (docs/spec/transcripts.md): the project's list in `Speakers.json`, Merge into the first selected
  name and the lowest Speaker ID, "+" starting from "New Speaker", renaming to a name a transcript already has merging the
  two, a cleared name kept in the list (dimmed) when it was listed, Clear and Delete for each selected name.

What 2026.8 changed, and where it is here:
* The Bin Column Selection dialog's Search field, keyboard and vertical
  resize (2026.8 guide p.246; What's New 2026.8 p.8): §9, built.
* Shared projects on third-party storage (2026.8 guide p.301; What's New
  2026.8 p.2): §20a; no new behaviour to build.
* Change Source Track for subclips and group clips, with More (2026.8 guide
  p.507; What's New 2026.8 p.7): §11, built for subclips, and for group and
  multigroup clips in 8.1c (docs/spec/multicamera.md).
* Effects Processing gains 32-bit (FX p.151-152, 2026.8): §17's Media
  Creation setting lists it; rendering is later. The Editing Guide's
  settings table (2026.8 guide p.1212) still lists 8-bit, 16-bit and
  Automatic.
* Markers in the monitors (p.413, p.416): an upright oval in the marker's colour, sized to the
  picture, above a one-line dark band at the bottom of the frame holding the marker's comment (the
  picture on p.413; ours: the comment, not the name, and nothing when it is empty); clicking the oval
  opens Edit Marker. Test `tst_monitor_markers`.
* Markers keep User Name and Marker Name apart (2026.8 guide p.418), as
  ours always have (`model::Marker`).
* Avid DNx targets in Consolidate/Transcode are "Resolution Independent";
  2026.8 notes this was called DNxHR (2026.8 guide p.355). Our menu already
  says "Avid DNx ...".
* OpenTimelineIO "can also be used by supported third-party integrations"
  (2026.8 guide p.971): no change.
* Later phases:
  * the Transcript Tool's Group Speaker Lines, New Line After, Start New
    Line and Revert (2026.8 guide p.446; What's New 2026.8 p.5-6);
  * Multicam Auto Cut from transcript speakers (2026.8 guide p.451 and
    p.1134; What's New 2026.8 p.3-4);
  * Titler+ keeping keyframes through trims, splits and duplicates (FX
    p.325, 2026.8; What's New 2026.8 p.9).
  These are in ROADMAP Phases 6 and 8.

**Clean-room rules (ROADMAP §3).** Entries record names, behaviour and layout
only. Avid's artwork, icons and prose are never copied into this project, and
the guide itself is never committed.

**What the guide does and doesn't settle.** The guide names commands, windows,
fields and some shortcuts. It does not show complete menus in order, nor the
full default keyboard. Those parts are marked *unverified*. They will be
confirmed against a licensed Media Composer (screenshots from the project's
test contact) and then locked by the tests.

| Part | File | Test |
|---|---|---|
| Menu bar | `tests/data/menu_inventory.json` | `tst_menu_inventory` |
| Default keyboard | `tests/data/keyboard_inventory.json` | `tst_keyboard` |
| Window appearance | CI screenshots | `ui-consistency` CI job |

---

## 1. Starting, quitting and project files (ch. 2–3)

* **Start (p.57–58).** The application starts with the **Select Project**
  window.
* **Quit (p.59).**
  * macOS: *Media Composer > Quit*. Windows: *File > Exit*.
  * *File > Close Project* returns to Select Project. It is also
    **Ctrl+Shift+W** (p.69).
  * In Select Project, **Quit** asks *Leave* / *Cancel*.
* **Project files (p.67–68).** A project is a folder named after the project
  in an *Avid Projects* folder. It contains:
  * a project file (`.avp`);
  * a project settings file (`.xml`);
  * a bin file (`.avb`).

  More `.avb` files are added as bins are created.
* **Default project locations (p.67).**

| | Private | Shared |
|---|---|---|
| Windows | `Users\<user>\Documents\Avid Projects` | `Users\Public\Documents\Shared Avid Projects` |
| macOS | `/Users/<login>/Documents/Avid Projects` | `/Users/Shared/Avid Media Composer/Shared Avid Projects` |

  Cutthulhu uses equivalent locations, under its own names, on each platform.
  Projects in the Avid locations can be opened with **Browse**.
* **User profiles (p.68).** A user folder in *Avid Users* contains a profile
  file (`.ave`) and a user settings file (`.xml`).
* **Naming limits (p.63).**
  * Project names have at most 56 characters; bin names at most 64.
  * For cross-platform names, avoid `/ \ : * ? " < > |` and leading or
    trailing spaces or periods.
* **Delete a project (p.69).** Select it in Select Project and press
  **Delete**, then confirm. This deletes the project and its bins, but not
  its media.
* **Rename (p.70).** Renaming is not possible inside the application; it is
  done by renaming files on disk.
* **Auto-open (p.69).** *Interface settings > "Automatically launch last
  project at startup"*.
* **Avid Attic (p.71–73).** Each save keeps a numbered backup copy of every
  bin (`Bin.1`, `Bin.2`, …) per project. The oldest copy is overwritten only
  if the second-oldest is more than 2 hours old.

## 2. Select Project window (p.61–67)

The layout is taken from the screenshots on p.61–64; behaviour from the text.

* **Left column:** *Select Project* and *What's New* buttons (What's New lists
  features by release).
* **Header:** "Welcome to …" and the user profile name.
* **Two tabs:** **Projects** | **New Project**.
* **User Profile** drop-down, and **Location** drop-down with a folder
  (browse) button. Locations: Private, Shared, External (a browsed folder,
  remembered).
* **Projects tab.**
  * The project list has two views, *text/column view* and *frame view*,
    switched by buttons at the top right, with a size slider.
  * Columns: **Name, Frame** (poster frame), **Project Type, Frame Rate,
    Color Space, Raster Dimension, Stereoscopic, Creation Date, Modified
    Date**.
  * The fast menu changes the font and the columns shown.
* **Poster frame:** set from the Composer monitor's context menu with "Save as
  project poster frame".
* **Buttons:** **Quit** and **Open**. Double-clicking a project opens it.
* **New Project tab.**
  * **Project name** field, then **Settings** (expandable).
  * **Choose for Me** check box: no raster or edit rate is fixed until the
    first clip is added, then a *Project Properties* dialog appears with
    *Raster* and *Edit Rate* (p.64).
  * **Format** preset: a combination of video format, frame size, colour
    space, aspect ratio and frame rate. **Custom** raster: even values from
    256×120 to 8192×8192. Stereoscopic rasters need width divisible by 8 and
    height divisible by 4. Custom presets have **Save Preset** and **Manage
    Presets**, and appear as *My Presets* (p.65).
  * **Raster Dimension, Aspect Ratio** (SD 4:3/16:9, HD 16:9, high-res
    computed) and **Frame Rate** (custom: 23.98 to 60 fps).
  * **Color Space:** SD uses YCC; HD uses YCC 709 or RGB 709; high-res has
    several options (p.65–66). The New Project screenshot on p.522 shows
    **YCbCr 2020 / SMPTE 2084**. HDR projects choose HLG or SMPTE2084
    (p.522). ACES projects choose **ACES RGB** (p.558) and switch the
    Timeline to 32-bit float.
  * **Color Depth:** "the bit depth at which the media will be stored"
    (p.66). The guide does not list the values.
  * **Stereoscopic:** Off, Leading Eye, Left Eye Only, Right Eye Only, Side by
    Side, Over/Under, Full.
  * **Film:** for 23.976p, 24p, 25p, 720p and 1080p, with a film gauge type.
  * **OK** creates the project folder and files and selects the new project
    (p.67).
* **Opening a project (p.68)** shows the **Bin Container**, the **Composer**
  and the **Timeline**, with the user's settings.
* **"Choose for Me" projects (p.64)** get two bins, a green source bin and a
  blue record bin, and a new sequence. Empty bins and the empty Timeline show
  hints ("Drag … to add clips", "Drag clips here to start editing").

**Implementation status (1.2a).** All of the above is built, except:
* **Save Preset / Manage Presets:** not yet.
* **Poster frames:** shown as placeholders until the Composer exists.
* **The project list's fast menu:** offers column choice; its font option is
  not yet built.
* **Colour-space labels:** *YCbCr 709*, *RGB 709*, *YCbCr 2020 / SMPTE 2084*
  and *ACES RGB* follow the guide (p.65, p.522, p.558). *YCbCr 601*,
  *YCbCr 2020*, *RGB 2020*, *YCbCr 2020 / HLG* and *DCI-P3* follow the same
  pattern but are unverified, as is the order.
* **Colour-depth values** (8, 10, 12 bit, 16 and 32 bit float): unverified.
* **Film types:** 16mm, 35mm 3-perf and 35mm 4-perf, from p.107.
* **Project files:** Cutthulhu's own `.cthproj` (JSON). Media Composer's
  `.avp` is Phase 3.

## 3. Panels, docking and workspaces (ch. 4)

* **Default layout (p.74).** Single monitor: Bin Container top left, Composer
  in the middle, Workspace toolbar on the right, Timeline at the bottom.
* **Window chrome (p.74).** Every window has a **vertical title bar** with a
  tab showing its name.
* **Docked, floating and tabbed (p.75–78).**
  * Drag a title bar to dock it. Valid targets show **green drop zones**, and
    a white area previews the result.
  * **Alt-drag** onto another title bar to tab windows together.
  * A tab group is moved by its grey area or grey box.
* **Keyboard (p.78).**
  * **Ctrl+Up/Down** (Cmd on macOS) cycles windows.
  * **Ctrl+Tab** cycles tabs.
* **Menus (p.78–79):** *Windows > Float All Panels* and *Windows > Float
  Active Panel*.
* **Tool palettes (p.79–80)** can be docked and saved in workspaces. Named
  palettes are listed in the Tools menu.
* **Interface settings (p.80–83).**
  * Brightness: dark or light.
  * Button highlight colour.
  * Timecode window brightness.
  * Timeline and viewer brightness.
  * Bin font overrides.
  * *Windows > Set Font* sets the font of the Bin, Composer, Script and
    Timeline windows.
* **Blank panel (p.83).** "Closing a docked tool will replace it with a blank
  panel"; also available from the Windows menu.
* **Multiple monitors (p.84).** A *Host Panels* submenu in Windows has one
  host panel per display.
* **Extensions (p.85).**
  * Third-party panels appear in the **Extensions** menu and can be docked
    like any window.
  * In Cutthulhu, this maps onto the module/provider system
    (`docs/EXTENSIONS.md`).
* **Workspaces (p.85–93).**
  * Defaults: **Edit, Effects, Color Correction, Audio**. Up to 12 workspace
    buttons. Workspaces are user settings.
  * *Windows > Workspaces*: New Workspace (optionally "Based on Workspace"),
    Delete Workspace, Restore to Saved, Restore Current to Default,
    Properties, Show Workspace Toolbar.
  * "When moving to a workspace", a setting chooses between loading the last
    known state and the last saved state. Another setting chooses what
    happens "when changing monitor configuration": prompt, use the same, or
    duplicate.
  * Some tools can't be part of a workspace: Hardware, Communication Ports and
    Media tool.
* **Workspace toolbar (p.87–89).**
  * Sits on the right edge.
  * Right-click shows icon and text, icon only, or hides the bar. When
    hidden, a workspace icon at the top right brings it back.
* **Linked settings (p.89).** Settings named like a workspace load with it.
* **Pictures (p.74–89), for the look.**
  * Title bar: a hatched grey box at the top, then a dark round tab-menu
    button. Each tab has a round close button and its name reading upwards.
    The current tab is purple, the others grey, and grey space sits below.
  * Toolbar: icons with captions **EDIT, COLOR, EFFECTS, AUDIO**, in that
    order. Its right-click menu is headed **WORKSPACE BAR** and offers **Icon
    and Text**, **Icon only** and **Hide**.
  * Workspace button menu (p.89): the workspaces, then **Save Current**,
    **Restore Current to Default**, **New Workspace**, **Delete Workspace**,
    and **Show Workspace Bar**.
  * The guide's text names the second workspace "Color Correction" (p.85);
    the 2025 pictures show "COLOR". We follow the pictures.

**Implementation status (1.2b).** Built: the vertical title bars, docking
with drop zones, tabbing (Alt-drag and Alt-drop), floating windows with
panels inside, the tab menu (p.94), Float All Panels, Float Active Panel,
Add Blank Panel, blank panels left on close (p.83), File > Close, Tools >
Timeline, Ctrl+Up/Down and Ctrl+Tab, and Escape to cancel a drag. Also the
four default workspaces (up to 12): New Workspace (with Based on Workspace),
Delete Workspace, Restore to Saved, Restore Current to Default, Save
Current, Show Workspace Toolbar, the toolbar's three modes, and the
hidden-bar button. Layouts persist per user.

Our own choices, unverified against Media Composer:
* **Default settings.** "When moving to a workspace" defaults to *Load the
  last known state*. "Closing a docked tool will replace it with a blank
  panel" defaults to off.
* **New panels.** A new blank panel docks to the right of the active
  window. A tool reopened with no place to return to floats (p.94 says tools
  open in their own windows).
* **Default layouts.** Effects, Color and Audio use the Edit layout until
  their tools exist (1.5).
* **The workspace button.** With the toolbar hidden it sits at the top
  right of the panel area on every platform, because the macOS menu bar is
  outside the window.

Not yet built:
* Workspaces > Properties: done in 1.6 (below);
* Bin Layout (1.3);
* Host Panels for more monitors, and per-monitor-count workspaces (p.84);
* reordering tabs by dragging;
* Set Font, Center Current, Send Current Home, Close All But Active and
  Close All Bins But Active;
* Interface settings UI (1.5);
* workspace buttons in the Command Palette: done in 1.6;
* the gear at the bottom of the toolbar in the p.74 picture, whose purpose
  the guide does not give.

## 4. Tools and the Command Palette (ch. 5)

* **Tools as tabs (p.94–95).** Tools open in their own windows. They can be
  dragged together as tabs, with previous/next buttons, a tab menu and close
  buttons. Tools cannot be tabbed into bin windows.
* **Command Palette (p.96–100).**
  * Tabs: **Move, Play, Edit, Trim, FX, 3D, CC, MCam, Other, More, Smart
    Tool**.
  * Modes at the bottom: **Button to Button Reassignment**, **Menu to Button
    Reassignment** and **Active Palette**.
  * **Quick Find** field.
  * *Other* tab: a **Blank** button, plus modifier buttons (Add Alt Key on
    Windows; Add Control Key and Add Option Key on macOS).
  * Right-click a button and choose *What's This?* for help.
* **Where mappings are stored (p.97):**
  * Keyboard palette → Keyboard settings;
  * Composer tool palette → Composer settings;
  * trim-mode palettes → Trim settings;
  * Tool palette → Interface settings.
* **Menu to Button.** A mapped menu command shows its initials on the button,
  for example *Composer > Load Filler* shows "LF" (p.98–99). Bin fast menu
  commands can also be mapped to keys.
* **Tabs in the picture (p.96):** Move, Play, Edit, Trim, FX, 3D, CC, MCam,
  Tracks, Smart Tools, Other, More, Workspaces, Plug-ins. The text lists
  fewer; we follow the picture.
* **Move tab (picture, p.96),** three columns:
  * Step Forward/Backward 1 Frame, 10 Frames and One Field;
  * Go to IN, Go to OUT, Go to Audio Mark IN/OUT, Go to Start, Go to End;
  * Go to Previous/Next Trim, Event and Marker.
* **Edit tab (picture, p.100):** Mark IN, Mark OUT, Mark Clip, Audio Mark
  IN/OUT; Clear IN Mark, Clear OUT Mark, Clear Both Marks, Add Edit, Fit To
  Fill; Splice-in, Overwrite, Replace Edit, Lift, Extract; To the Left, To
  the Right, Select In/Out, Copy to Clipboard, Clipboard Contents; Top,
  Tail, Move Clip Up, Move Clip Down, Sync Lock All Tracks; Segment Mode
  (both), Make Subclip, Mark Markers.
* **Quick Find (p.99–100):** a field at the top right. As you type, matching
  buttons are listed under it and only the tabs with matches stay open.
* **Activating (p.99):** in Active Palette mode, clicking a button performs
  its command.
* **Where mappings are saved (p.97):** see above. The Tool palette's changes
  are saved in the Interface settings (p.97). The Settings list also has a
  **Tool Palette** setting that opens the palette (p.392, p.1221).
* **Tool palette (p.392).** It has extra buttons for editing and moving.
  * Open it from File > Settings > User > Tool Palette, or with the Tool
    Palette button (Other tab of the Command Palette).
  * Its buttons can be mapped.
  * **Show Labels in Tool Palette** (Interface settings) puts the button
    names under the icons.
  * Tool palettes can be docked or tabbed and are saved with workspaces;
    named Tool Palettes are listed in the Tools menu (p.79–80).
* **Calculator (p.100).** Tools > Calculator.
  * A **Format** menu with frame code and key number formats.
  * Type with its own keys, the numeric keypad or the top row of digits; no
    leading zeros, colons or semicolons are needed.
  * Choosing another format converts the total. For example, drop-frame
    and non-drop-frame timecode convert into each other, and 30 fps
    durations convert to 25 fps.
* **Console (p.101–102).** Tools > Console.
  * It shows system information, with a line starting "System ID:", and
    errors that were logged.
  * File > Get Position Info writes information about the Timeline at the
    position; the bin's Get Bin Info does the same for a bin.
  * A command line takes commands such as `alldrives` and
    `toggleStatSpace`.

**Implementation status (1.5b).**
* **Built.**
  * The Command Palette (`src/ui/command_palette.*`, tabs and buttons in
    `resources/command_palette.json`):
    * the fourteen tabs, with the Move and Edit tabs as pictured;
    * Quick Find, with a list of matches and only the matching tabs left
      open;
    * the three modes:
      * Button to Button: drag a button onto a Composer or Tool palette
        button, including the Blank button;
      * Active Palette: a click runs the command;
      * Menu to Button: click a target button, then choose a menu
        command; the button shows the command's initials.
  * Mapped buttons are saved. The Composer's go in the Composer settings
    and the Tool palette's in its Tool Palette setting. Restore to
    Default restores them.
  * The Tool palette (twelve buttons), from the Tool Palette button or the
    Settings list, with Show Labels in Tool Palette.
  * The Calculator (`src/model/calculator.*`, `src/ui/tool_windows.*`):
    * the Format menu, the display and keypad, and the keyboard;
    * adding and subtracting;
    * converting between formats: the same frames at the same rate,
      otherwise the same duration in time.
  * The Console:
    * system information, including a System ID derived from the
      computer's ID;
    * warnings and errors from the application;
    * the `help`, `clear`, `sysinfo` and `version` commands;
    * File > Get Position Info.
  * Tools open as floating panels (p.94), which can be docked and tabbed.
* **The Play button while playing (the owner's design, not Media
  Composer's look; it may change if editors dislike it).** The playing
  monitor's Play button shows how it plays (`Composer::playDisplay`):
  * forward at normal speed: the play arrow turns green;
  * forward at any other speed, slow play included: the rate in frames per
    second with ">" after it ("120 >"), light green on a dark green box;
  * backward, at every speed: "<" and the rate ("< 60"), light amber on a
    dark amber box;
  * the number is centred where the play arrow is, and the box may spread
    over the buttons beside it. The rate counts the project's nominal
    frames (60 for 2x in a 30-frame project, as on p.400-401).
* **Our choices, unverified.**
  * The Play, Trim, Smart Tools, Other and More tabs: which buttons, and
    in what order.
  * The Tool palette's twelve default buttons, except Scratch Removal, which the FX Guide puts there (p.420, p.422).
  * Its mapping is saved in the Tool Palette setting rather than in the
    Interface settings (p.97). This allows several named versions (p.80).
  * The Calculator's formats and keypad: the guide has no picture.
  * The Console's commands. The System ID is a hash of the operating
    system's machine ID. HDTitleFilter is the FX Guide's (p.376);
    closeallofxplugins is ours after its closeallavxplugins (p.158), which
    it also answers to.
  * Initials are at most three letters. Commands without a picture show
    their initials in the palettes.
  * Closing the Command Palette in Menu to Button mode returns to Button
    to Button mode.
* **Not yet.**
  * The 3D, Workspaces (1.6) and Plug-ins tabs; the MCam tab past Quad Split, Nine Split, Swap Cam Bank and Previous/Next In Group (8.1b).
  * The modifier key buttons, and mapping onto the Keyboard palette (1.6).
  * What's This? help.
  * Named Tool Palettes in the Tools menu.
  * Get Bin Info.
  * The `alldrives` and `toggleStatSpace` console commands.
  * The Deck Controller (p.95), Hardware, Usage and Configuration (p.102–105).

## 5. Menus

The full inventory, with a page for every item, is
`tests/data/menu_inventory.json`. In summary:

* **Menu bar order (p.74 screenshot):** File, Edit, Bin, Clip, Timeline,
  Composer, Tools, Windows, Script, Help.
* **Extensions** (p.85): its position is unverified.
* **Avid Link** (p.59) is excluded, because it is Avid's licensing service.
* **macOS** also has the application menu, which holds Quit and About.
* **Item order within each menu, and separators:** unverified.
* **Additions not in Media Composer:** *Help > About Qt* (Qt's LGPL notice).
* **Renamed:** *Tools > Avid Titler+* (FX p.303) is called *Titler+*, because
  "Avid" is a trademark.
* **Where the two guides differ, the Editing Guide's spelling is used:**
  * "Recreate Title Media" (p.475), not "Re-create" (FX p.374);
  * "Tools > Background Queue Window" (p.358), not "Background Queue"
    (FX p.59).
* **Contextual items, not part of the static menus:**
  * While the legacy Title Tool is open, the File menu offers *Save Title*,
    *Save Title As* and *Export Title* (FX p.358–364).
  * The Windows menu offers *Title Tool* (FX p.327).
  * The Timeline's context menu offers *Promote Titles > Avid Titler+* (FX p.326).
  * The Effect Editor's Object menu button for Intraframe effects: *Bring Forward*, *Send Backward*, *Bring to
    Front*, *Send to Back*, *Group*, *UnGroup*, *Lock*, *Unlock* (FX p.417-418).
  * The Timeline's context menu offers *Edit Title* on a classic title, and the Effect Editor shows an Edit Title
    button for one (FX p.372).
  * The Title Tool's Edit, Object (with Soften Shadow, p.354), Alignment and Templates (p.360) menus (FX p.330, p.334, p.338) are in the
    Title Tool window's own menu bar (ours), not the main one.
  * The Edit menu's Undo item names the last action, for example *Undo Clear
    Renders* (FX p.61).

## 6. Documented shortcuts

Only shortcuts the guide states are listed. Ctrl is Command on macOS. The full
default keyboard is still to come (§7).

| Shortcut | Command | Page |
|---|---|---|
| Ctrl+Shift+W | File > Close Project | 69 |
| Ctrl+Z | Edit > Undo | 466 |
| Ctrl+R | Edit > Redo | 466 |
| Ctrl+C / Ctrl+V | Edit > Copy / Paste | 423 |
| Shift+Ctrl+V | Edit > Paste Audio Keyframes | 722 |
| ' | Add Keyframe (the guide prints the key as a quote mark) | 716 |
| Ctrl+A | Edit > Select All | FX p.153 |
| Ctrl+W | File > Close | KS2009 |
| Ctrl+P | File > Print Bin | KS2009 |
| Ctrl+D | Edit > Duplicate | KS2009 |
| Ctrl+Shift+G | Clip > Group Clips | KS2009 |
| Ctrl+T | Bin > Align and Fill > Align to Grid | KS2009 ("align to an invisible grid"; EG p.251 names the item) |
| Ctrl+Y | Timeline > New > Video Track | KS2009 |
| Ctrl+' | Windows > Send Current Home | KS2009 |
| Ctrl+0 … Ctrl+8 | Tools > Timeline (0), Audio Tool (1), Calculator (2), Command Palette (3), File > Media > Media Creation Settings (5), Console (6), File > Input > Tape Capture (7), Effect Palette (8) | KS2009 |

Ctrl+4 (the Composer window, KS2009) has no matching menu command in the 2025
guides, so it is not mapped yet.
| Ctrl+F | Edit > Find | 417 |
| Ctrl+G | Find next occurrence | 429 |
| Ctrl+L / Ctrl+K | Edit > Enlarge / Reduce (Frame in bins, Track in the Timeline) | 250, 594 |
| Ctrl+I | Get Info (monitor or bin item) | 388 |
| Ctrl+Shift+F | Composer > Full Screen Playback | 391 |
| Ctrl+9 | Tools > Project | 243 |
| Ctrl+Up / Ctrl+Down | Cycle windows | 78 |
| Ctrl+Tab | Cycle tabs | 78 |
| Ctrl+. (period) | Cancel waveform redraw or render | 678, 805 |
| Ctrl+X | Cut (segment, script text) | 573, 622 |

## 7. Default editing keyboard

**Machine-readable form:** `tests/data/keyboard_inventory.json`, enforced by
`tst_keyboard`.

**What the guides state.** Every key below is documented in text, US
English layout:

| Keys | Command | Page |
|---|---|---|
| \`, Tab, 5, Space | Play (start/stop) | p.400 |
| 1 / 2 | Step Backward / Forward 10 frames (8 frames in progressive formats) | p.400 |
| 3 / 4, Left / Right | Step Backward / Forward one frame | p.400 |
| Home / End | Go to start / end of clip or sequence | p.400 |
| J / K / L | Play Reverse / Pause / Play Forward | p.400–401 |
| N / ; | Mouse jog / mouse shuttle | p.403 |
| I / O | Mark IN / Mark OUT | p.405–406 |
| Alt+I | Go to IN Point | p.97 |
| V / B | Splice-in / Overwrite | p.624 |
| E / R / T | Mark IN / Mark OUT / Mark Clip (the T-E-X and T-R-X sequences) | p.639 |
| X | Extract | p.639 |
| Up / Down | Move Clip Up / Move Clip Down; Previous In Group / Next In Group when a group clip is shown and no segment is selected | p.613, p.1131 |
| Ctrl+X | Cut (Ctrl+C, Ctrl+V: Copy, Paste) | p.622 |
| U / A / S | Nearest / previous / next transition, both sides (Trim mode) | p.651 |

**Shuttle and contextual keys (p.400–401, 117–195, 1132):**
* Pressing L repeatedly plays at 2×, 3×, 5× and 8×.
* K+L and K+J give slow forward and backward.
* Holding K and tapping J or L steps one frame.
* Alt+J and Alt+L change speed one step at a time.
* In the Capture tool: F4, F1/F2, Esc and Tab.
* In MultiCamera: keypad 1–9 choose camera angles.

**Not yet confirmed.** The Keyboard settings illustration (p.1202) shows the
whole default layout, but as icons only. Keys not listed above stay unmapped
until their functions are confirmed in text or on a licensed seat.

**Other keyboard settings (p.1202).** Media Composer also ships Keyboard
settings named "MC1 English", "NC English", "Premiere Pro" and "Resolve".

**Keyboard settings window (p.1202).**
* Double-clicking the Keyboard setting opens it. The title is "Keyboard -"
  followed by the setting's name, e.g. "MC English (United States)".
* It shows a keyboard: function keys, the main keys, and a block with
  Home, End, Page Up and Page Down. Keys show button pictures; hover for a
  key's name.
* At the bottom: **Standard** and **Map Foreign Keyboard** (for French or
  German regional settings on Windows).
* Buttons are mapped onto keys from the Command Palette (p.98). The Add Alt
  Key button (macOS: Add Control Key, Add Option Key) gives a key its
  modified function: on I it gives Go to IN Point (p.97).
* Mapping the keyboard is saved in the Keyboard settings (p.97).

**Workspaces and settings (p.85–92).**
* Workspace settings are User settings; each user can have 12 (p.86).
* The Settings list shows a Workspace View for each workspace (picture on
  p.1136). Double-clicking one, or Windows > Workspaces > Properties, opens
  the **Workspace View Setting** dialog (p.89–90):
  * **Link to Named settings**, with a box for the name. Opening the
    workspace then uses the User settings with that name; with the box
    empty, the unnamed ones. Only User settings can be linked.
  * A **Bin Layout** menu.
* The **Workspaces tab** of the Command Palette (picture, p.91) has buttons
  W1–W12. Each has a menu of workspaces (and bin layouts below a divider).
  * A workspace button shows the first two characters of its workspace.
  * They are shown in italics when the workspace no longer exists (p.90).
  * Workspace buttons are dragged onto a Tool palette or the Keyboard
    setting (p.91).

**Implementation status (1.6).**
* **Built.**
  * The Keyboard setting and its palette (`src/ui/keyboard_palette.*`):
    * a US English keyboard with a navigation block;
    * keys mapped by dragging from the Command Palette, Menu to Button, or
      Blank;
    * the modifier key buttons;
    * holding Shift shows the Shift layer;
    * the changed keys saved in the user's Keyboard setting; the title
      shows the setting.
  * Workspaces are User settings:
    * one Workspace View version per workspace, its check mark on the
      active one;
    * choosing another version switches to it;
    * saved as they change and on quit, and restored after a restart and
      when the user changes (gate 4);
    * each user has their own.
  * Link to Named settings (Workspace View Setting dialog, also from
    Windows > Workspaces > Properties).
  * The Workspaces tab: W1–W12 with their menus. Their pictures show two
    characters, in italics when the workspace is gone. They can be mapped
    onto any palette or key.
  * "Closing a docked tool will replace it with a blank panel" (Interface
    settings) now works.
* **Our choices, unverified.**
  * The key names on the keys, and the navigation block's layout: the
    p.1202 picture shows icons only.
  * Holding Shift to see and map the Shift layer.
  * W1–W4 start as Edit, Color, Effects and Audio.
  * The workspace toolbar's look is kept in the Interface settings.
  * The Keyboard setting has one version; we don't know the other
    layouts' keys.
* **Not yet.**
  * Map Foreign Keyboard, and the MC1, NC, Premiere Pro and Resolve
    keyboards.
  * Bin layouts and their menu on the Workspaces tab.
  * Menu shortcuts (Ctrl+...) in the Keyboard setting: they keep their own
    map.

## 8. Colour menus for later phases

These are recorded now so they are not entered from memory later.

* **Display Color Space** (right-click the Source or Record monitor; p.544).
  * Settings table (p.1161–1162): the project colour space, **Rec709**,
    **Rec709 (full range)**, **SRGB**, **DCI P3**, **Rec2020** and
    **ACESproxy**. The exact menu text is unverified.
  * In an ACES project the submenu shows (screenshot, p.559): **Output color
    space**, **ACES sRGB**, **ACES Rec.709**, **ACES Rec.2020 HLG**, **ACES
    Rec.2020 ST2084**, **ACES P3D65 ST2084**, **ACES P3D65 DCI** and **ACES
    2065**.
* **Source color space** (Source Settings, Phase 2). The screenshot on p.522
  shows **Rec.709 [video levels]** and the transformation "From Rec.709 [video
  levels] to Rec.2020 [SMPTE 2084]". The colour adapter "Levels scaling (full
  range to video levels)" also appears there. Colour Adapter Types (p.558):
  Generic, User Installed LUTs, ACES ADX, ACES Generic, ACES Panasonic, ACES
  Sony, ACES Arri, ACES Canon, ACES Working Color Space.
* **Video Quality Menu** (Timeline, p.404): **Full Quality float** (32-bit
  float), **Full Quality 10bit**, **Full Quality** (8-bit), **Draft Quality**
  and **Best Performance**. These are playback qualities, not the project's
  Color Depth.

## 9. Bins (ch. 10, p.239–305)

* **Bin Container (p.239–243).**
  * The Sidebar is on the left and open bins on the right. Its vertical tab
    reads **Bins** (pictures on p.240–242).
  * Double-click a bin to open it; more bins open as tabs.
  * The Sidebar has a bin colour column, and its columns sort.
  * Bins can be docked within the container: drag a tab to a green edge.
    Dragged out, a bin floats in a new container with the Sidebar closed
    (**Show Sidebar** on right-click).
  * Alt-drag from the Sidebar tabs a bin.
  * Items dragged onto a bin in the Sidebar move into it; a closed folder
    opens.
  * Commands: **Hide Sidebar** (Sidebar fast menu), **File > Close Bin
    Container**, **Bin > Show Sidebar Only**.
* **Project Bin Container (p.243):** *Tools > Project* (Ctrl+9). A
  Sidebar-only container named "Project"; it cannot be renamed. Closing it
  leaves bins open. It takes part in workspaces even when floating.
* **Arranging bins (p.241–243).**
  * Bins can only be tabbed or docked with other bins in a Bin Container.
  * Drag a bin tab to an eligible edge (green) to subdivide the right
    panel; bins can be arranged horizontally or vertically, with splitters
    between them.
  * Drag a bin out and release it to float it in a new Bin Container,
    Sidebar closed.
  * Alt-drag a bin from the Sidebar to the right panel to tab it; release
    Alt while dragging and the drop zones appear.
  * Drag bin items onto an open bin in the Sidebar (a count shows while
    dragging); onto a closed bin, which then opens with the items selected;
    onto a closed folder, which expands.
  * A floating Bin Container closes when its last bin is closed or dragged
    to another Bin Container.
* **Object icons (p.243–244):** master clips, subclips, audio clips,
  sequences, sources, effects, motion effects, rendered effects, groups,
  titles and more.
* **Bin views (p.245–254).**
  * **Text, Frame and Script** buttons; the **Bin View menu**, whose
    defaults are Bin (basic), Capture, Custom, Film, Format, Media Tool and
    Statistics; **Save as** for custom views.
  * The Bin View menu works in Text view only (p.248). The table on p.248
    describes each view:
    * Bin (basic default): Name, Duration, Drive, IN-OUT, Creation Date,
      TapeID, "etc.";
    * Capture: start and end timecodes, tape, tracks and resolutions;
    * Custom: Name only;
    * Film: key number, ink number and pullin;
    * Format: video formats, resolutions and projects;
    * Media Tool: the Media tool's headings;
    * Statistics: start and end timecodes, duration and resolution.
  * A changed view's name turns italic with ".n" added (p.249). *Save as*
    opens the View Name dialog. Saved views can be changed in Settings
    (Bin View dialog).
  * Text view: *Choose Columns* (the Bin Column Selection dialog: click
    to select or deselect, All/None, OK; p.246). New columns go to the
    right of the selected column, otherwise at the far right (p.247).
    Since 2026.8 (2026.8 guide p.246; What's New 2026.8 p.8):
    * a Search field at the top, focused when the dialog opens, shows the
      matching headings. **Ours:** headings that contain the text, any
      case; All/None acts on the headings shown;
    * Tab moves from the Search field to the list, and Shift+Tab moves back;
    * the arrow keys move through the list;
    * typing in the list scrolls to a matching heading without selecting
      it. **Ours:** the first heading that starts with the typed text,
      else one that contains it;
    * the dialog resizes vertically. **Ours:** its width stays fixed.
  * Sorting (p.247–248):
    * double-click a heading to sort, and again to reverse;
    * the heading's right-click menu has *Sort on Column, Ascending* and
      *Descending*;
    * *Bin > Sort* sorts on the selected headings, the leftmost first;
      Ctrl/Cmd+click and Shift+click select headings; *Sort* is dimmed
      when no heading is selected;
    * *Sort Again* with no column selected reapplies the last sort;
    * selected items stay selected;
    * colours sort by hue, saturation and value.
  * Frame view (p.249–252):
    * a frame per clip with its name below, the head frame by default;
    * a tooltip with Duration, Format and Creation Date;
    * *Edit > Enlarge Frame* (Ctrl+L) and *Reduce Frame* (Ctrl+K), up to
      seven times;
    * frames are dragged anywhere;
    * *Bin > Align and Fill*: *Align to Grid*, *Align Selected to Grid*,
      *Fill Window* (evenly spaced) and *Fill Sorted* (in Text view's
      order);
    * Bin settings: border colours by object type or clip colour, and
      icons;
    * *Snap to Grid* (fast menu or right-click): Disable, Temporary,
      Enable or Invisible; a dropped clip goes to the nearest grid point.
  * Script view (p.252–254):
    * frames down the left, the clip's details above a text box for each;
    * Page Up and Page Down scroll a long text;
    * clips can be dragged up or down, and Text view then shows the same
      order;
    * sort and sift in Text view first.
* **Bin status bar (p.254–257):** "Viewing: n/n", the selected count and
  the total duration.
* **Bin tabs (p.258–259):** an asterisk before an unsaved bin's name, a tab
  menu listing every open bin, tabs reordered by dragging, a close button
  on each tab.
* **Fast menu (p.259):** every Bin menu command.
* **Selecting (p.259–260):** Ctrl/Cmd+click, Shift+click, lasso,
  *Select > Reverse*.
* **Duplicating, copying and moving (p.260–261).**
  * *Edit > Duplicate* names the copy "name.Copy.n".
  * Dragging items to another bin moves them.
  * With Alt (Windows) or Option (Mac) on p.260, or Ctrl (Windows) or
    Command/Option (Mac) on p.261, dragging copies: a clone of the same
    clip, without ".Copy.n". Clips cannot be copied into their own bin.
  * Custom columns come along to the other bin, in the order they were
    made.
* **Deleting (p.261–262):** a Delete dialog for clips and their media.
* **Colours (p.263–265).**
  * *Set Background Color* (right-click or fast menu) colours the bin and
    its tab.
  * Clip colours: right-click in the Color column, or *Set Clip Color* in
    the right-click menu of any view; None resets.
  * The colour palettes have a button for the system colour picker.
* **Lock (p.265):** *Clip > Lock/Unlock Bin Selection*. Bins on shared
  storage are locked as a whole: §20.
* **Columns (p.267–278).**
  * Drag a heading to move its column (p.268).
  * *Bin > Align and Fill > Align Columns* spaces columns by the length of
    their contents.
  * Drag a separator to resize; Frame and Color have fixed widths.
  * *Hide Column* (Bin menu or the heading's right-click menu).
  * *Edit > Delete* or Delete on a selected heading: a standard column is
    hidden; deleting a custom column asks, with OK to delete it and its
    contents or Hide to keep them.
  * *Add Custom Column* (heading right-click; Text or Script view) adds
    "Untitled", typed over in the heading; *Rename Column* is for custom
    columns only (p.269).
  * *Bin Left Lock* (p.270).
  * Tab and Shift+Tab move along the row, Enter and Shift+Enter down and
    up the column, wrapping at the ends (p.270).
  * The full list of headings is on p.274–278.
  * Headings "modifiable after capture with no restrictions" can be typed
    into: Name, Auxiliary TC1–TC5, Comments, Journalist, Production, Shoot
    date, Start, Take, TapeID and the TC columns; Frame Count Start also
    (p.275). Color is set from its menu.
* **Sift (p.288–291).**
  * *Bin > Sift Bin Contents* opens the **Custom Sift** dialog (picture on
    p.289): "Find clips that meet these criteria" and "Also find clips
    that meet these criteria", each with three rows of Criterion
    ("Contains:"), Text to Find and Column or Range to Search ("Any");
    Clear, OK, Cancel and Apply.
  * A colour is sifted by its exact name, with its capitals.
  * The sifted bin shows "(sifted)" after its name; *Show Sifted* and
    *Show Unsifted* switch.
  * *Sift Selected Items* keeps the selected items.
  * Timecode and keycode ranges (Start to End Range and others) appear in
    the column menu when their columns are shown (p.290).
* **Favorite Bins (p.293).**
  * A bin reachable from any project, kept in the user's Bin settings.
  * *Bin > Add Bin to Favorites*, the Bin Fast Menu, or right-click in a
    bin. The bin appears in the Sidebar's Favorite Bins folder.
  * Bin settings: "Favorite Bins show at" Top or Bottom of the Bin
    Container Sidebar (the picture shows Top).
  * Select a bin in the folder and press Delete to take it off the list.
* **Filtering items (p.294):** the quick filter searches the selected
  columns, or Name when none is selected.
* **Set Bin Display (p.288):** object types to show (by default all but
  source clips and rendered effects), "Show clips created by user" (on)
  and "Show reference clips".
* **The Sidebar list (p.294–299).**
  * A new project gets a bin named after the project.
  * *New Bin* gives "the name of the project ... with a number appended".
    We use "Project.1", "Project.2", …; the exact form is unverified.
  * Naming: the project rules, up to 64 characters (p.295).
  * Open bins are dimmed, and bins from other projects are in italic.
  * *File > Open Bin* lists a bin from elsewhere under **Other Bins**;
    deleting it removes only the pointer. "Bins have the file name
    extension .avb" (p.296): a Media Composer bin is read into a bin of ours
    first (docs/spec/avb.md, "Import").
  * *File > Open Selected Bins (In One Window)*.
  * *New Folder*, and drag bins into folders.
  * *Flat View* (fast menu).
  * Delete moves bins and folders to a **Trash** that appears on the first
    delete; *Empty Trash* deletes the files.
  * *File > Save Bin* (dimmed when nothing changed), *Save All*, and
    auto-save (Bin settings, interval in minutes).
  * A quick filter shows matching bins as a flat list.
* **Pictures (p.240–257), for the look.**
  * Sidebar: a "Show All" menu, a fast menu and a search field above the
    list; sizes in K.
  * Bin header row: the Bin View menu ("Untitled" or "Basic"), the three
    view buttons, search and the fast menu.
  * Rows show an icon column, then Name.

**Implementation status (1.3a).**
* **Built.**
  * Bins as files (`.cthbin`, JSON) and the project's Sidebar tree
    (`<project>.cthbins`); `src/model/bin.*` and `project_bins.*`.
  * The Sidebar: list, sizes, dimmed open bins, italic other bins, New Bin,
    New Folder, rename in place, drag into folders, Delete to the Trash,
    Empty Trash, Flat View, quick filter, Hide Sidebar and Show Sidebar.
  * Bin tabs with asterisks, a tab menu and close buttons.
  * Text view with an icon column, Name and the default headings; Name is
    editable. Quick filter, an optional status bar, background colour.
  * Commands: *Timeline > New > Sequence* ("Untitled Sequence.01"),
    *Edit > Duplicate*, *Edit > Delete* (locked items stay), *Edit > Select
    All*, *File > Open Bin*, *Open Selected Bins*, *Close Bin*, *Save Bin*,
    *Save All*, *Bin > Show Sidebar Only* and *Windows > Close All Bins But
    Active*.
  * Bins open when a project was closed reopen with it, and changes save
    when bins close or the project closes, plus auto-save.
* **Our choices, unverified.**
  * Auto-save every 15 minutes.
  * "Untitled Folder" for new folders.
  * A new project opens its bin in its first session.
  * Default headings are those named for the Bin view on p.248: Creation
    Date, Duration, Drive, IN-OUT, TapeID.
  * With no active bin, *New Sequence* asks with a simple list rather than
    the Select dialog with New Bin.
  * Deleting items asks nothing yet: there is no media to delete with them.

**Implementation status (1.3b).**
* **Built.**
  * Text, Frame and Script views, with the selection shared between them.
    The view is saved with the bin.
  * The Bin View menu: the seven built-in views, Save As (kept in the
    user's settings), and the italic "<view>.1" for a changed view.
  * Columns: Choose Columns (the Bin Column Selection dialog, with
    2026.8's Search field and keys), moving,
    resizing, Hide Column, Align Columns, Add Custom Column, Rename Column,
    and Delete (hide, or delete a custom column after asking). All headings
    from p.274–278 can be shown; the modifiable ones can be typed into.
  * Sorting: double-click a heading, the heading's Sort on Column items,
    Bin > Sort on selected headings, and Sort Again.
  * Sifting: the Custom Sift dialog, Sift Selected Items, Show Sifted and
    Show Unsifted, and "(sifted)" on the bin's tab.
  * Frame view: Enlarge and Reduce Frame (eight sizes), frames dragged
    anywhere and kept there, Align to Grid, Align Selected to Grid, Fill
    Window, Fill Sorted, Snap to Grid, and the tooltip. Changing the frame
    identifying a clip (8.7d3; p.251): K with L or J rolls it at 8 frames a
    second (ours), Home and End, Mouse Jog and Shuttle (N, ;) on the selected
    clips, kept in the bin ("representedFrames"); groups beep.
  * Script view: a frame, the name and duration, and a text box per clip;
    clips dragged up or down.
  * Set Clip Color, the Color column, Set Background Color, Clip > Lock
    and Unlock Bin Selection, and Bin > Select > Reverse.
* **Our choices, unverified.**
  * The Bin View menu says "Basic" for the basic view, as the picture on
    p.246 does; the table on p.248 says "Bin (basic default)".
  * The headings of each built-in view are our reading of p.248. Media
    Tool uses the basic headings until the Media tool exists.
  * The colour palette's names and colours (the guide shows no palette),
    which a colour sift matches.
  * "Contains" is the only sift criterion, the only one the guide names.
  * "Any" searches Name and the shown columns.
  * Frame sizes from 64 to 264 pixels wide, 16:9; the third is the default.
  * Without media, a frame is black with the object's icon.
  * New custom columns after the first are "Untitled.1", "Untitled.2", …
* **Not yet built.** See "Implementation status (1.3c)".

**Implementation status (1.3c).**
* **Built.**
  * Bin docking: bin tabs dragged to green zones at a group's edges split
    the right panel; splitters between groups; tabbing into a group; Alt
    for tabbing only.
  * Floating Bin Containers with their own Sidebar (closed at first, Show
    Sidebar on the tab bar's right-click), closed when emptied. The bins
    open in every container reopen with the project (in the main one).
  * Sidebar bins dragged to the right panel; bin items dragged between
    bins (move, or copy with Alt or the platform's copy key) and onto bins
    in the Sidebar, closed ones opening with the items selected; folders
    expand while dragged over.
  * The Project Bin Container (Tools > Project, Ctrl+9), a panel holding a
    Sidebar.
  * Favorite Bins with the Favorite Bins folder, Add Bin to Favorites,
    Delete, and top or bottom placement (kept in the user's settings).
  * Set Bin Display, the quick filter on selected columns, Tab and Enter
    between cells, and timecode range sifts (Start to End, Mark In to Out,
    Film TC, Sound TC, Auxiliary TC1–TC5).
* **Our choices, unverified.**
  * Drop zones are a group's four edges (a quarter of its size) and its
    middle for tabbing; the guide shows only "eligible edges".
  * A bin dragged out of a tab bar starts docking once the pointer leaves
    the bar by 12 pixels.
  * The Favorite Bins folder appears once it has a bin.
  * The Project Bin Container first floats at 300 × 460.
  * Closing a floating Bin Container's window closes its bins.
  * Range sifts read timecodes as non-drop-frame unless the text has ";".
* **Not yet built (later phases).**
  * The arrangement of docked and floating bins is not saved with the
    project or with workspaces yet.
  * The drag count, Bin Left Lock, Bin Map, the Delete dialog
    with media, keycode and ink number range sifts, Duplicate on columns,
    Set <column> for selected clips.
  * The Bin settings dialog (Favorite Bins placement, auto-save interval)
    comes with Settings in 1.5.
  * Frame view border colours and icons (U3h), Faster Thumbnails. "Show reference clips" shows items marked as not
    placed by the user (`BinItem::userPlaced`); only bins read from Media
    Composer (3.3b) will have them.
  * Copies are clones only in name: a change to one does not yet reach
    the other.
  * The Avid Attic backups.
  * Choose for Me's green and blue bins (p.64).
  * The Sidebar's "Show All" menu, whose options the guide does not give.

## 10. Composer (ch. 13, p.378–430)

* **Viewing methods (p.378–379):** bins (Frame and Script views), the
  Source monitor (clips and sequences), the Record monitor (a sequence),
  pop-up monitors and the Timeline (Toggle Source/Record in Timeline).
* **The window (p.379–381).**
  * Source and Record monitors in the project's aspect ratio: HD 16:9, SD
    4:3 or 16:9 (p.379). Black letterbox or pillarbox bars as needed.
  * The picture on p.380 shows, left to right above the monitors: the
    Source Clip Name menu, a tracking display (e.g. "V1 TC1 01:06:09:11"),
    the center duration display ("15:28"), the Record tracking display
    ("Mas TC1 01:00:04:11") and the Record Clip Name menu. Below each
    monitor: the position bar and two rows of buttons.
  * Illustrations show two button rows, one information row and the
    Center Duration display (Composer settings, Window tab, p.380).
  * Resize by dragging the lower right corner; Flow Data Dynamically puts
    the data in one or two rows (p.380).
  * *Composer > Resize Monitor to Image* and the right-click menu (p.381).
  * *Composer > Show Single/Dual Monitor*, or right-click: a single Record
    monitor. Alt/Option-click the Source/Record Mode button toggles.
    *Workspaces > Source/Record Editing* restores the standard size.
  * Right-click > *Hide Video* (floating Composer only); Button Display at
    Bottom: Off hides the controls (p.381–382).
* **Source/Record colours (p.382–383):** Interface settings, Timeline &
  Viewers: *Show Source/Record colors in Composer* turns the Source
  position bar green and the Record one blue (pop-ups green); *Show
  Source/Record colors in Timeline* colours the selected track enable
  buttons: source green, record blue (docs/spec/timeline-colors.md).
* **Second row of buttons (p.384):** Composer settings, Button Display at
  Bottom, Two Rows under.
* **Tracking information (p.384–387).**
  * Above the monitors, one or two rows; nothing until a format is chosen,
    and nothing with no clip loaded. Click the area for the Tracking
    Information menu (three panes: format; Master, Duration, In/Out,
    Absolute, Remain; a source track's timecode).
  * Options (p.387): Master, Duration, In/Out, Absolute, Remain, V1/A1/A2
    TC (or EC), D1 TC1, TC1/24/25/25PD/30D/30ND (24p and 25p), Clip Name,
    Timecode, Footage (24p and 25p), Frames.
  * Abbreviations shown: Mas, Dur, I/O, Abs, Rem (p.386).
* **Info window (p.387–388):** right-click > *Get Info*, or Ctrl/Cmd+I
  over a monitor or in a bin. **Timecode window (p.388–389):** *Tools >
  Timecode Window*, up to 48 lines.
* **Loading (p.393–395).**
  * Source/Record mode; double-click in a bin loads into the Source or
    Record monitor (Bin settings: "Double-click loads clip in" can choose
    a new pop-up monitor); Alt-double-click the other way; drag to a
    monitor; Alt-drag clips to the Record monitor for an instant rough cut.
  * The Clip Name menu lists loaded clips alphabetically (Alt: in load
    order), with *Clear Monitor* (black, still loaded) and *Clear Menu*
    (only the shown clip stays).
* **Playback (p.395–403).**
  * Position bar and indicator ("blue position indicator", p.396): click
    or drag; far left or right for start or end; Ctrl/Cmd-click or drag
    snaps to transitions, marks and markers; Ctrl+Alt snaps to the frame
    before.
  * Buttons (p.397–399): Play (Monitors; becomes Stop), Stop, Pause and
    Play Reverse (Play tab), Go to Next/Previous Event (Monitors), Play
    Standby, Step Backward/Forward one frame (Monitors; Alt: 10 or 8
    frames), Step Backward/Forward 10 or 8 frames and by field (Move tab).
  * Keyboard (p.399–401): see section 7. L again: 2×, 3×, 5×, 8×; hold K
    and tap J or L to step a frame.
* **Marking (p.405–407):** Mark IN and Mark OUT buttons "under the
  monitor" stop playback; I and O keys; sawtooth marks on the IN and OUT
  frames; Clear IN Mark, Clear OUT Mark, Clear Both Marks; Alt-drag a mark
  to move it; Mark Clip marks the whole clip or segment (Alt: ignore the
  track selection).
* **Buttons named elsewhere:** Match Frame is by default in the second row
  under the Source monitor (p.433); Gang in the second row under both
  monitors (p.495; the Record monitor is always ganged); Quick Transition
  in the second row (p.727); Play In to Out in the Play tab (p.487); Add
  Marker buttons in the More tab (p.412–414).

**Implementation status (1.4a).**
* **Built.**
  * The Composer panel: Clip Name menus, one tracking display per
    monitor (Master, Duration, In/Out, Absolute, Remain, Clip Name), the
    center duration display, black monitors in the project's aspect
    ratio, position bars with the marked span and the indicator, and two
    button rows per monitor.
  * Loading by double-click (clips to the Source monitor, sequences to the
    Record monitor) and by dragging items from a bin; Clear Monitor and
    Clear Menu.
  * Play, J-K-L with the shuttle speeds, K+J/L steps, holding K with L or J
    for slow play (8 fps for 30-frame projects, 6 for 25 and 24; p.401;
    no sound), Play In to Out, step
    one and 10 (8) frames, Go to Start/End and Previous/Next Event, Mark
    IN/OUT, Mark Clip, Clear IN/OUT/Both, Go to IN/OUT, Gang, Show
    Single/Dual Monitor. With no media yet, the position moves in real
    time over a black picture.
  * The default keyboard (section 7) works while the Composer has the
    focus.
  * New sequences get a Start timecode.
* **Our choices, unverified.**
  * The button rows follow the picture on p.380 slot by slot. The guide
    names the functions but does not show which icon is which; we matched
    them by shape: Step Backward/Forward one frame, Mark IN ("]"), Play,
    Mark OUT ("["), Mark Clip, Add Marker (red), Splice-in (yellow),
    Overwrite (red) on the first Source row; Gang, Go to Previous/Next
    Event, Go to IN Point ("]" and an arrow pointing at it), Play In to
    Out, Go to OUT Point (an arrow, then "["), Clear Both Marks on the
    second. The bracket shapes are verified: the Capture tool's labelled
    buttons on p.117 show Mark IN as "]", Mark OUT as "[", Go to IN as
    "]" with an arrow and Go to OUT as an arrow with "[". (Before
    this was checked, the two marks were the other way round; the owner
    noticed.) Slots we could not identify (the leftmost
    buttons, the Record row's film strip, green and second-row tools) are
    Blank buttons. "Go to OUT Point" is named after Go to IN Point.
  * Double-click loads sequences into the Record monitor and other items
    into the Source monitor.
  * One information row; Source/Record colours off; the center duration
    shows the active monitor's IN to OUT duration.
  * Default Starting TC 01:00:00:00 (General settings, p.1191, gives no
    value).
  * Go to Previous/Next Event go to the edits on the selected tracks of a
    sequence (1.4b), and to the start and end of a clip.
  * The active monitor has a thin highlight.
* **Not yet built.**
  * Add Marker; phantom marks (p.501);
    Source/Record mode, pop-up monitors, the Info and Timecode windows,
    the Tracking Information menu's panes and track timecodes, two
    information rows, Resize Monitor to Image, Hide Video, Play Standby,
    snapping on the position bar, moving marks by dragging,
    Loop Selected Clips, subclips and markers.
  * Composer settings (1.5).

## 11. Timeline and editing (ch. 15, p.457–479; ch. 19, p.591–645)

* **Sequences (p.457–464).**
  * *Timeline > New > Sequence* puts a new sequence in the bin, the Record
    monitor and the Timeline (p.457). With no active bin, a Select dialog
    asks for one.
  * A new sequence has V1, A1, A2 and TC1 (p.459). Up to 99 video and 99
    audio tracks and one data track (p.459, p.634); p.624 says 64. We use
    99.
  * Making a first edit with no sequence creates one (p.463). The Record
    monitor then "displays the end of the last frame of the new edit"
    (p.464).
* **Edits (p.466–468).**
  * Three-point editing: two marks on one side and one on the other; the
    fourth is worked out (p.466). With only a source IN, the record IN is
    the position indicator and both OUTs follow from the clip (p.501).
    Single-mark editing uses the position indicator for the missing mark
    (p.468).
  * Splice-in (yellow) inserts at the record IN or the position; later
    material moves down (p.466). Overwrite (red) replaces and only
    lengthens the sequence past its end (p.467). Replace (blue) is on the
    Command Palette's Edit tab (p.467).
  * Fit to Fill, on the Command Palette's Edit tab, "performs an overwrite
    edit and uses a Variable Speed motion effect to fill the marked
    duration" (FX Guide p.170): the source IN to OUT into the record IN to
    OUT, at the speed that makes it fit. **Ours:** it needs all four marks,
    and a clip (not a sequence) in the Source monitor; the motion effect is
    the one the EDL and OTIO imports make (`model::motionEffect`). Alt for an
    unrendered effect comes with rendering (Phase 6).
  * Undo (Ctrl+Z) and Redo (Ctrl+R), 100 levels, and *Edit > Undo/Redo
    List*, which undoes or redoes back to the chosen command (p.466).
  * Lift leaves filler and keeps the length; Extract closes the gap. Both
    act between the marks on the selected tracks and put the material in
    the Clipboard (p.478–479). Sync locks extract from every track
    (p.479).
  * Keyboard: V Splice-in and B Overwrite (p.624); T-R-X (Mark Clip, Mark
    Out, Extract) and T-E-X (Mark Clip, Mark In, Extract) are "keyboard
    command sequences" (p.639): T, E, R and X.
* **Track Selector panel (p.624–630).**
  * Source tracks on the left, record tracks on the right, with Track
    Monitor buttons, Sync Lock and Lock buttons (p.625, p.633).
  * The source side shows only the loaded clip's tracks; the record side
    shows the sequence's tracks. A selected source track for a track the
    sequence lacks adds it on the record side when edited (p.626).
  * A track is edited only when selected on both sides (p.626). *Edit >
    Select All Tracks* selects all of them (p.627).
  * Patching: drag a source track to a record track; "a white arrow
    appears"; audio only to audio, video only to video; tracks of other
    formats dim; data tracks cannot be patched. The patched source track
    moves beside its record track. *Timeline > Restore Default Patch*
    undoes patches (p.629–630).
  * Track Monitor buttons choose what is seen and heard; Ctrl-click and
    Solo isolate a track (p.627–628).
* **The window (p.591–609).**
  * The picture on p.607: the Timeline Top toolbar, the Track Control
    panel, the position indicator, and the bottom toolbar: Timeline Fast
    Menu, Focus, Toggle Source/Record in Timeline, Video Quality, Client
    Monitor, Step In, Step Out, View Menu, Search in visible timeline
    text, Find to the left, Find to the right, Zoom Out, Scale bar, Zoom In
    and the scroll bar.
  * Rows top to bottom in that picture: D1, V2, V1, TC1, then A1 down to
    A11. The ruler shows the master timecode (p.598).
  * The Fast menu's options are listed on p.592–593: Default Setup, View
    Type, Track Panel, Effect Icons, Render Ranges, Dissolve Icons, Clip
    Frames, Clip Text (Clip Names, Group Clip Name, Clip Tracks, Source
    Track Name, Clip Durations, Clip Full Durations, Clip Trans Duration,
    Timeline Clip Notes, Source Names, Media Full Names, Clip Resolution),
    Sync Breaks, Dupe Detection, Color Correction, Audio Data (Waveform,
    Clip Volume, Volume, Pan, Allow Per Track Settings), Clip Color
    (the Clip Color dialog, p.594–597; docs/spec/timeline-colors.md),
    Segment Selection, Mark IO Selection, Track Color, Show Markers, Show
    Adapters, Show Track, Track Control Panel and Zoom (Zoom In, Zoom Out,
    Show Entire Sequence, Zoom into Region, Zoom Back to Last View, Show
    Every Frame). Waveform Options (Source Levels, Output Levels, Full
    Height) is also on it (p.679). Waveforms: docs/spec/waveforms.md. The
    Track Control panel, per track settings, Track Color, the selection
    colours, Show Markers and track heights: docs/spec/track-control.md.
    View Type (Heads, Heads Tails), the View Menu button's Timeline views and
    the More tab's TV1-TV8: docs/spec/timeline-views.md. Sync Breaks, Dupe
    Detection and the Clip Text items: docs/spec/sync-dupes.md. Timeline Clip
    Notes (Add Timeline Clip Note, Tools > Timeline Clip Notes, p.483-486):
    docs/spec/clip-notes.md. Clip Text also has Title Text (FX Guide p.305)
    and Comments, the Color Correction comments (FX Guide p.610;
    docs/spec/color-correction.md). Color Correction shows the correction
    indicator lines (FX Guide p.616; docs/spec/color-correction.md, 6.10d1). Add Filler at Start, Find Black Hole/Flash
    Frame, Rename Track and Create Sequence Based on Selection:
    docs/spec/timeline-commands.md.
  * The scale bar zooms around the position indicator; zoomed in, the
    indicator gets a dotted "shadow" at the end of the frame. Zoom In:
    drag over the area. Focus: 90 pixels per second around the position
    indicator; click again to go back (p.607–608).
  * Ctrl-drag snaps the indicator to the head of transitions, Ctrl+Alt to
    the tail (p.609). Page and Scroll movement during play (p.609).
  * IN to OUT highlighting (p.638).

**Implementation status (1.4b).**
* **Built.**
  * Sequences with tracks and segments, stored in their bin item
    (`src/model/sequence.*`). Timeline > New > Video Track, Audio Track
    (all four formats) and Data Track; New Sequence loads the sequence.
  * Splice-in and Overwrite from the Composer buttons, the menus and the
    keys, three-point and single-mark; Lift and Extract; Undo, Redo and
    the Undo/Redo List; a first edit makes the sequence in the active
    bin.
  * The Timeline panel showing the Record monitor's sequence, or the
    Source monitor's clip (Toggle Source/Record): the Track Selector with
    selection, drag patching (the arrow, dimmed tracks), Restore Default
    Patch and Track Monitor buttons; the ruler and TC1; segments with
    Clip Names, Clip Tracks and Clip Durations; IN to OUT highlighting;
    the position indicator with its shadow, click and drag, Ctrl and
    Ctrl+Alt snapping; Page movement during play.
  * The bottom toolbar: Fast menu, Focus, Toggle Source/Record, Zoom Out,
    the scale bar, Zoom In and the scroll bar. The Fast menu's Default
    Setup, Track Panel, Clip Text, Show Track > TC1 and Zoom items.
  * Go to Previous/Next Event and Mark Clip use the selected tracks'
    edits. T, E, R and X keys.
* **Our choices, unverified.**
  * Sizes and colours of rows, segments and buttons; the toolbar
    pictures are our own drawings.
  * The scale bar runs from the whole sequence (plus 5%, at least ten
    seconds) to 40 pixels per frame; Zoom In/Out buttons halve or double
    the scale; Zoom into Region zooms four times.
  * After an edit the record marks are cleared and the source marks kept.
    After Lift the marks stay; after Extract only the IN stays.
  * Lift and Extract need at least one mark; a missing mark is the start
    or end of the sequence.
  * Patching a second source track to a record track moves the first one
    to the second one's old track.
  * Clips without a Tracks column offer V1, A1 and A2; audio clips A1 and
    A2.
  * Undo covers sequence edits and new tracks, not bin changes; the
    history is cleared when the project closes.
  * Messages for edits that cannot be made show as a tooltip.
* **Not yet built.**
  * Trim mode (1.4d), Solo, Sync Point edits, markers, Add Filler at
    Start, Load Filler, the Track Control panel (U1b1), Timeline views (U1b2), Search,
    Video Quality, Step In/Out (nesting), clip colours, waveforms (U1a) and the
    other Fast menu options (shown disabled).

**Segments, locks and the Clipboard (1.4c; ch. 15 p.479–481, ch. 19
p.610–640, ch. 13 p.432–434).**
* **Segment tools (p.610–623).** Lift/Overwrite (red) and Extract/Splice-in
  (yellow) on the Timeline palette; with both on, the upper half of a
  segment is Lift/Overwrite and the lower half Extract/Splice-in.
  * Click selects, Shift-click adds or removes (p.611). Filler can be
    selected except at the head or tail of a sequence.
  * A lasso starts above the tracks, or with Alt between them; left to
    right selects segments. With no tool on, a lasso turns on
    Lift/Overwrite (p.611–612).
  * Dragging moves the selection: Lift/Overwrite leaves filler and
    overwrites; Extract/Splice-in closes the gap and splices (p.620).
    Alt-drag copies (p.620). Dragging out of the Timeline cancels. Not
    onto locked tracks (p.619). Dragging past the top or bottom track adds
    a track (p.635). The segments stay selected (p.620). Ctrl snaps
    (p.609).
  * Delete: Lift/Overwrite leaves filler, Extract/Splice-in closes up;
    with both tools on, the Default Segment Tool decides (p.621).
  * Mark Clip with selected segments marks their span (p.622).
  * Ctrl+C, Ctrl+X and Ctrl+V with selected segments; paste goes at the
    position (p.622–623). Pastes go onto the tracks copied from (ks2009).
  * Move Clip Up and Move Clip Down on the Edit tab, mapped to the up and
    down arrows; past the last track a new track is made (p.613).
  * Right-click > Select: To the Left, To the Right, Clips with Same Source
    Clip Color, Clips With No Source Clip Color, Offline Clips, Clips With
    Same Local Clip Color, Reverse; filler is not selected. Create
    Sequence Based on Selection (p.613–614).
  * Right-click the Fast Menu button > Set Local Clip Color: a colour for
    the selected segments, or None (p.597–598).
  * The Timeline Top toolbar holds editing buttons and can be shown in the
    Timeline settings (p.603); the Lift/Overwrite button is there (p.621).
    Its picture (p.607) has a master timecode display at its left.
* **Change Source Track** (p.595; 2026.8 guide p.507; What's New 2026.8
  p.7). Right-click an audio segment > Change Source Track lists the clip's
  audio tracks, with its own ticked. For a subclip, *More* lists the master
  clip's audio tracks that the subclip lacks, found by its Clip source's
  MobID among the open bins. The segment keeps its place, length, clip
  gain, pan and effects, and the change is one undo step. The clip in the
  bin is not changed. **Ours:** after More the segment refers to the
  master clip, at the same frames. This is the owner's decision
  (2026-10-25), and it is on the watch list below: the guide does not say
  what Media Composer does. Group and multigroup segments list the
  tracks of the angle they show (docs/spec/multicamera.md). Not yet
  built: source track names from the
  production metadata (p.595-596).
* **Volume and pan automation** (p.714-723; docs/spec/automation.md).
  * Fast menu > Audio Data: Clip Volume, plus Volume or Pan.
  * Keyframes: Add Keyframe (the ' key), click/Shift-click, drag
    (Ctrl snaps, Alt in time), Ctrl+Shift+click, Delete, and
    Ctrl+Shift+arrows.
  * Ctrl+C / Ctrl+V copy and paste keyframes, and so does Edit > Paste
    Audio Keyframes (Shift+Ctrl+V) with its dialog.
* **Locks (p.633, p.493).** Clip > Lock Tracks / Unlock Tracks, the
  Timeline right-click menu, or a track button's right-click Lock Track.
  Sync Lock buttons per track and Sync Lock All (Edit tab; the sync icon
  next to TC1). A splice into a sync-locked track puts filler into the
  other sync-locked tracks (p.493); Extract with sync locks extracts from
  all of them (p.479).
* **Tracks (p.634–636).** Right-click > New; the Delete key with track
  buttons selected opens Delete Track(s); a track button's right-click
  Delete Track deletes without asking. Rename Track, Add Track dialog
  (Alt) and track numbering.
* **Clipboard (p.479–481).** Lift, Extract and Copy to Clipboard fill it;
  it holds one item. Source monitor right-click > Clipboard Contents, or
  the Edit tab's Clipboard Contents button, loads it as "Clipboard
  Contents.n"; with Alt, Lift, Extract or Copy load it as "<sequence>.Sub".
  Tools > Clipboard Monitor.
* **Replace Edit (p.467–468):** the positions in the two monitors are the
  sync frames; the segment's (or the IN to OUT's) length is kept.
* **Top and Tail (p.639):** the T-R-X and T-E-X key sequences.
* **Add Edit (p.639–640):** at the position on the selected tracks; Alt on
  every track with filler. Match-frame edits show an equal sign. Timeline
  > Remove Match Frame Edits between the marks or in the whole sequence.
* **Match Frame (p.432–434):** loads the clip under the position into the
  Source monitor at the matching frame, marked IN, old marks removed (Alt
  keeps them). The highest selected track wins, "in descending order: V1,
  A1, A2". Right-click a track button > Match Frame Track.
* **Reverse Match Frame (p.433–434):** a button on the Command Palette's
  Other tab. The Source monitor's frame, on its selected tracks, is looked
  for in the Record monitor's sequence on its selected tracks: the first
  place it is shown, then each later place at each click. **Ours:** after
  the last place it starts again at the first; with a sequence in the
  Source monitor, its clip under the position is looked for. Later: the
  track button's right-click Reverse Match Frame, which searches every
  track (p.434).

**Implementation status (1.4c).**
* **Built.**
  * Both segment tools, click and Shift-click, Alt lasso, drag with Lift/
    Overwrite or Extract/Splice-in, Alt-drag copy, snapping, new tracks
    by dragging up, Delete, Mark Clip on the selection, Ctrl+C/X/V, Move
    Clip Up/Down (up and down arrows), Select > To the Left/To the
    Right/Reverse.
  * Lock and Sync Lock (per track, Sync Lock All, Clip > Lock/Unlock
    Tracks), with their effects on edits; locks are saved with the
    sequence.
  * The Clipboard, Copy to Clipboard and Clipboard Contents (also in the
    Composer's right-click menu); Replace Edit, Top, Tail, Add Edit (Alt
    for filler), match-frame equal signs, Remove Match Frame Edits, Match
    Frame and Match Frame Track; Delete Track(s) with and without the
    dialog; right-click > New.
  * The Top toolbar with the master timecode.
* **Our choices, unverified.**
  * The Top toolbar is shown, and holds Extract/Splice-in, Lift/Overwrite,
    Lift, Extract, Copy to Clipboard, Add Edit, Replace Edit, Top, Tail and
    Match Frame. The guide does not list its default buttons.
  * The Default Segment Tool is Lift/Overwrite.
  * A segment dropped inside another splits it there; with Extract/
    Splice-in the drop point is read before the gap closes.
  * Match Frame takes video tracks from the top down, then audio from A1.
  * Lock and Sync Lock changes are not undo steps.
  * The Clipboard keeps the edit rate of the sequence it came from.
* **Not yet built.** Trim mode and trim tools, Segment Drag Sync Locks,
  link selection, the four-frame display, nonadjacent drags across
  tracks, the Select items that need clip colours, Create Sequence Based
  on Selection, Rename Track, the Add Track dialog, the Clipboard
  Monitor, Sync Point editing, markers (1.4d). (Reverse Match Frame came
  in Phase 4.)

**Trim mode and markers (1.4d; ch. 20 p.647–666, ch. 13 p.412–424).**
* **Trims (p.647–656).**
  * Overwrite trims (red) add filler or overwrite and keep the length;
    ripple trims (yellow) move the rest of the track; dual-roller trims
    (pink) move the cut and keep the length (p.647, p.651, p.653–655).
  * No clip trimmed to zero frames; trims stop at the next segment and at
    the end of the media; several transitions move in unison and stop with
    the first that cannot go on (p.647–648, p.659).
  * Trim tools on the Timeline palette: Overwrite Trim and Ripple Trim;
    with both on, the upper corners are overwrite and the lower ripple;
    over the cut a dual roller (p.650).
  * The Trim Mode button ("in the Timeline", p.644; also on the Tool
    palette, p.664) selects the transition nearest the position on the
    selected tracks for a dual-roller trim; overlap edits take only the
    topmost track; Alt restores the last rollers (p.651, p.655).
  * Selecting sides: the A-side or B-side of a transition, Cycle Trim
    Sides, lasso a transition, the keys U (nearest), A (previous) and S
    (next transition), the trim counters under the monitors (Shift for
    both); Shift-click adds transitions; the track buttons add tracks
    (p.650–653). Go to Previous/Next Event selects the transition there
    (p.655).
  * Leaving: the Trim Mode button, Source/Record or Effect Mode, a frame
    step or the arrow keys, clicking TC1 or the ruler (p.651); Escape for
    slip (p.664).
  * Trim displays: Small, Quick and Big Trim (p.648); Big Trim replaces
    both monitors, outgoing frames left, incoming right; the trim counters
    are purple when active (p.651).
  * Refining: Trim Backward 10 (8) Frames, Backward 1, Forward 1, Forward
    10 (8) Frames buttons; J-K-L; the numeric keypad; Ctrl+Alt one frame
    at a time; Ctrl snaps (p.656–657).
  * Play Loop, Edit Review, trimming on the fly, dual-image playback, end
    of trim indicators (p.657–660).
  * Sync locks: a single-roller trim on a sync-locked track moves the
    other sync-locked tracks with filler; a track that would have to lose
    material stops the trim (p.662).
  * Slip and slide: lasso a segment right to left (Shift+Alt: slide),
    double-click a transition; drag a roller, the Trim buttons, J-K-L, the
    keypad; Slip Left / Slip Right in Source/Record mode, Alt 10 (8)
    frames (p.663–665).
  * Extend: the edit nearest a mark moves to it (an IN pulls the next
    edit back, an OUT pushes the previous one forward), on the selected
    tracks (p.661).
* **Markers (p.412–424).**
  * Sixteen Add Marker buttons, one per colour (the Markers tab p.412, the
    More tab p.413); colours in priority order, Red the highest (p.414).
  * A marker is an oval in the Timeline, the position bar and at the
    bottom of the monitor (p.413). Add Marker opens the Edit Marker dialog
    (name, user, colour, frame, track, comment; the user name of the
    system by default, p.413–418). Alt makes a spanned marker over IN to
    OUT, shown on TC1 in a sequence; spanned markers cannot overlap
    (p.414–415).
  * Double-click a marker in the position bar, or click its oval in the
    monitor, to edit it (p.416, p.418).
  * Go to Previous/Next Marker (Move tab), Mark Markers (Edit tab), the
    Delete key on a selected marker (p.419).
  * Tools > Markers or right-click a monitor > Markers Window: the active
    monitor's markers; double-click or Jump to Marker; Change Color,
    Change Track; Delete; Display columns, sorting, columns, Set Font,
    Quick Find, export and import (p.419–424).
  * Copy Source Markers (Composer settings, p.414, p.418).

**Implementation status (1.4d).**
* **Built.**
  * Overwrite, ripple and dual-roller trims with their limits, several
    rollers in unison, sync-locked tracks kept in sync, slip and slide,
    Extend (`src/model/sequence.*`), all undoable.
  * Trim mode: the Trim Mode button (Alt for the last rollers), U/A/S,
    Go to Previous/Next Event, the Ripple Trim and Overwrite Trim tools,
    clicking and Shift-clicking transitions, dragging rollers (Ctrl
    snaps), right-to-left Alt lasso for slip (Shift for slide), lassoing
    transitions, Cycle Trim Sides, the trim counters, the four Trim
    buttons, Slip Left/Right, leaving with the button, Escape, frame steps
    and the ruler or TC1; rollers drawn red, yellow or pink.
  * Big Trim display in the Composer: counters and Trim buttons in place
    of the position bars and button rows.
  * Markers saved with clips and sequences; the red Add Marker button (Alt
    for spanned markers), the Edit Marker dialog, markers drawn in the
    Timeline, position bars and monitors; editing by double-click; Go to
    Previous/Next Marker, Mark Markers; the Markers window with jump,
    Edit Marker, Change Color, Change Track and Delete.
* **Our choices, unverified.**
  * Without a trim tool, single-roller trims are Ripple trims.
  * The Trim Mode, Ripple Trim and Overwrite Trim buttons are on the
    Timeline Top toolbar; their pictures are ours.
  * The guide names only Red and Blue among the sixteen marker colours;
    the other fourteen names and all the colour values are ours.
  * The names of the U, A and S commands ("Select Nearest/Previous/Next
    Transition") are ours.
  * Trim mode ends when another sequence is loaded.
  * Filler next to a cut can be trimmed away entirely; clips keep one
    frame.
* **Not yet built.** Quick Trim display, the trim display's loop
  boxes, the numeric keypad, end of trim
  indicators, dual-image playback, the Transition Corner Display, link
  selection, Render On-the-Fly (Play Loop, Edit Review, J-K-L Trim and the
  Play Loop tab came in U2e1; the outgoing and incoming frames, Small Trim
  display, Dual Image Play and Sync Rollers in U2e2,
  docs/spec/trim-settings.md), the other fifteen Add Marker buttons, deleting
  a marker selected in the Timeline, the Markers window's columns,
  sorting, Quick Find, export, import, Find, Copy Source
  Markers.

## 12. Settings (ch. 33, p.1136–1224)

* **Settings window (p.1136).** Opened with File > Settings. It has four
  tabs: **Format, Project, User, Site**.
  * The User tab has a **User Profile** menu above the list.
  * Columns: **Setting** and **Name**.
  * A setting with several versions is a row with an arrow. Its versions
    are listed under it, each with a check box (picture on p.1136).
* **Types (p.1136–1137).**
  * User settings are stored in the user's folder.
  * Project settings are stored in the project folder.
  * Site settings are stored in a Settings folder of their own. They are
    the defaults for new users and projects.
* **Working with settings (p.1137–1141).**
  * Open a setting by double-clicking it, or by typing its first letter
    and pressing Enter. Click OK or Cancel; changes are saved in the
    settings file.
  * **Edit > Duplicate** copies the selected settings. A custom name gets
    a period and a version number.
  * Click the **Name** column to type a custom name, then press Enter.
  * One version of each setting is **active**: click to the left of a
    version to check it.
  * **Delete** key or **Edit > Delete** removes settings; this cannot be
    undone.
  * Right-click > **Restore to Default** asks first:
    * **Copy & Restore** keeps a copy of the current settings;
    * **Restore** discards them.
  * Drag settings between settings files. Dropped into the Settings
    list, a message box offers:
    * **Add**: a new version; the current one is unchanged.
    * **Replace**: replaces the current version.
  * **Windows > Site Settings** opens the Site Settings window. Settings
    are dragged into it.
* **User profiles (p.1138, p.1143–1145).**
  * The User Profile menu lists the users and their profiles, then:
    **Create User Profile**, **Import User or User Profile**, **Export
    User or User Profile**, **Update User Profiles** and **Reveal User
    Profile**.
  * Choosing another user saves the previous user's settings and loads
    the new user's.
  * The **Create User Profile** dialog has the profile's default
    settings and a **Profile Name** box. The new profile is selected
    in the menu afterwards.
* **Format tab (p.1146).** Shows the project's format as chosen in New
  Project: Project Type, Raster Dimension, color space and so on.
* **Settings dialogs.** Each setting's options are listed in
  `src/ui/resources/settings.json`, with the guide page of each option.
  The labels follow the guide.
  * **Bin** (p.1152–1154).
  * **Composer** (p.1158–1161): Window, Edit, Move, MultiCam and Viewer
    tabs (Viewer: the Target Masks, 8.5e3, and Color Spaces, 8.5e4a; Resize Monitor to Image later).
  * **Effect Editor** (p.1171–1172): the Effect Editor's options
    (docs/spec/effect-keyframes.md).
  * **General** (p.1191–1193).
  * **Interface** (p.1199–1202): General, Bins, and Timeline and Viewers
    tabs.
  * **Timeline** (p.1218–1221): Display and Edit tabs.
  * **Trim** (p.1223–1224): Play Loop and Features tabs.

**Implementation status (1.5a).**
* **Built.**
  * Settings files, in `src/model/settings.*`:
    * versions, the active version, Duplicate with numbered names,
      Delete, Add and Replace;
    * new files start from the site settings;
    * the files: `<user>/Settings.json`, `<project folder>/Settings.json`
      and `<Settings folder>/Settings.json` beside the Users folder.
  * The Settings window (`src/ui/settings_window.*`):
    * the four tabs and the grouped versions with check boxes;
    * opening a setting by double-click or Enter, naming by clicking
      the Name column;
    * Edit > Duplicate, Edit > Delete and the Delete key;
    * Restore to Default, with Copy & Restore;
    * dragging settings between the lists and into the Site Settings
      window, with Add or Replace;
    * the User Profile menu with Create User Profile, Update User
      Profiles and Reveal User Profile;
    * the Format tab.
  * One dialog type for all settings, made from `settings.json`. Options
    whose feature is not built yet are shown dimmed and keep their
    defaults.
  * Settings that work now:
    * **Bin:** Auto-Save interval; Favorite Bins show at; the Quick
      Filter's columns; tabbed or floating bins (Ctrl inverts).
    * **Composer:** Data Display at Top (Off or one row); Button Display
      at Bottom; Center Duration; Activate Record Monitor After Edit;
      Show Composer Markers; Show Spanned Markers; the Move tab.
    * **General:** Default Starting TC for new sequences.
    * **Interface:** Show ToolTips; Show Source/Record colors in
      Composer and in Timeline; When moving to a workspace; Show Labels in Tool Palette
      (p.392, added in 1.5b).
    * **Tool Palette** (p.1221): opens the Tool palette, whose mapped
      buttons it keeps (1.5b).
    * **Keyboard** (p.1202) and **Workspace View** (p.89–90, p.1136):
      section 7 (1.6).
    * **Timeline View** (p.604, p.1137): docs/spec/timeline-views.md (U1b2).
    * **Timeline:** Show Toolbar; Show Marked Region, including its
      effect on Replace Edit; Movement During Play; Only One Segment
      Tool; Default Segment Tool.
    * **Trim:** Auto focus when entering Trim mode.
* **Our choices, unverified.**
  * Which settings are User settings and which are Project settings. The
    picture on p.1136 shows Interface, Timeline and Trim as User
    settings, and General absent from the User list. Bin and Composer
    are not visible in the picture; they are User settings here.
  * Defaults the guide does not give: Data Display (one row), Button
    Display (two rows), Center Duration on, Tick Marks off, Single Mark
    Editing on, Movement During Play (Page), Default Segment Tool
    (Segment Overwrite), and the Trim Play Loop values (2, 2, 0).
  * The Quick Filter choices are worded "Search the Name column only" and
    "Search all columns"; the guide describes them but does not name them.
  * Create User Profile has one set of default settings, the product's.
  * The wording of the Restore to Default and Add/Replace message boxes is
    ours.
  * The Settings window opens on the User tab.
* **Not yet.**
  * Import and Export User Profile, with Personal or Group.
  * File > New Settings File and File > Open Settings File.
  * Settings whose tools come later: Audio, Capture, Import, Grid,
    Keyboard (1.6), Link, Video Display and others.
  * Options shown dimmed in the dialogs. Their features come with later
    phases: the attic, pop-up monitors, two information rows, MultiCam and
    Viewer tabs, colors and fonts, Small Trim mode and others.
  * The tooltip delay.
  * The Bin "Set Default Bin View" option, which needs a list of bin
    views.

## 13. Effects (FX Guide ch. 1–3, ch. 17–18)

### Effect Palette (FX p.36–38)
* **Opening:** Tools > Effect Palette, Ctrl+8 (FX p.38). It is a tool
  panel, so it floats, docks and tabs like the others.
* **Layout (picture on FX p.37):**
  * Tabs: Filters and Transitions (video), Audio Track and Audio Clip
    (audio). The text calls the audio ones Clip Effects and Track Effects;
    the tab labels follow the picture.
  * A Quick Find field. It searches all categories by default, or only the
    selected one (FX p.37).
  * The categories on the left, starting with All; the selected category's
    effects on the right, alphabetically, each with an icon.
  * Real-time effects have a green dot; non-real-time effects have none
    (FX p.36).
  * Bins holding effect templates are listed below the categories (FX p.38).
    *Not yet:* templates come with Phase 6.
  * Set Font from the right-click menu (FX p.38).
* **Categories (picture on FX p.37):** All, Blend, Box Wipe, Color
  Correction Presets, Conceal, Edge Wipe, Film, Generator, Illusion FX,
  Image, Key, L-Conceal, Matrix Wipe, Peel, PlasmaWipe Avid Borders, Center,
  Horiz, Lava, Paint and Techno, Push, Reformat, S3D, Sawtooth Wipe, Shape
  Wipe, Spin, Squeeze, Timewarp, Title, Xpress 3D Effect.
* **Effects:** `src/ui/resources/effects.json` lists 159 effects from the
  Effects Reference (FX p.456–490). For each effect it records the page,
  where the effect can be applied (its "Apply to" row), whether it is real
  time, and its parameter categories. The guide's gaps:
  * S3D lists no effects; the Color Correction Presets are ours (FX Guide p.611; docs/spec/color-correction.md, 6.10d2b).
  * The PlasmaWipe presets are not named (FX p.478).
  * Only 4 of the 15 Squeeze effects are named (FX p.483–484, p.490).
  * Motion effects (Freeze Frame and others, FX p.476–477) are made from the
    Source monitor, not from the palette (docs/spec/motion-effects.md).
* **Ours:** the Filters tab lists every video effect, because the picture
  shows Illusion FX transitions under Filters. The Transitions tab lists the
  effects that apply to transitions. The audio tabs are empty until audio
  plug-ins (Phase 5). The icons are our own drawings.

### Applying and removing effects (FX p.44–48)
* **Applying (FX p.44–45):**
  * Drag an effect's icon onto a transition or a segment in the Timeline.
    Near a cut, a transition effect goes on the cut. On a segment, a
    transition effect goes on the segment's nearer cut.
  * Double-click an effect to apply it to the selected segments.
  * Double-click an effect to apply it to the selected transitions. We use
    the transitions that have trim rollers; Effect mode comes in Phase 6.
  * Effects cannot go on data tracks (FX p.44). The palette's effects are
    video effects, so they go on video tracks only.
* **Transitions:**
  * They default to one second (p.1296), centered on the cut.
  * Transition Effect Alignment (FX p.500): Ending at Cut, Centered on Cut,
    Starting at Cut, Custom Start.
  * Transition Effect Duration is seconds:frames, where 1:00 is one second
    (FX p.500).
* **Insufficient Source (FX p.48):** asks when the outgoing clip (Media A)
  or the incoming clip (Media B) is short of frames.
  * Size To Fit shortens the effect and keeps its alignment where it can.
  * Without frames before the cut, it becomes Starting at Cut.
  * Cancel applies nothing.
* **Removing (FX p.47):**
  * Remove Effect removes the effect at the position on the selected track:
    a transition spanning the position, or else the segment effect.
  * Remove Effect is one of the effect buttons pictured on p.97. We put it
    on the Command Palette's FX tab, where the guide puts the Fade Effect
    button (FX p.46).
* **Fade Effect (FX p.46-47):** the FX tab's Fade Effect button keys the
  Foreground Level of the segment effect at the position, or of the
  segments selected in Segment mode, from the frames typed to fade up and
  down (docs/spec/effect-keyframes.md).
* **Timeline (FX p.36, p.40):**
  * A transition shades its span and shows its icon on the cut. A segment
    effect shows its icon in the segment.
  * The icons have a green dot for real-time effects and a blue dot
    otherwise.
  * The Timeline Fast menu's Effect Icons and Dissolve Icons options turn
    the icons off. Dissolve Icons is dimmed while Effect Icons is off.
* **Editing:**
  * Effects are part of the sequence. Applying, changing and removing them
    are undoable steps.
  * A segment effect stays with both halves after Add Edit.
  * A transition stays with its cut.
  * An edit with a transition is not a match-frame edit.

### Effect Editor (FX p.83–88, p.491–586)
* **Opening:** Tools > Effect Editor.
* **What it shows:** the effect at the Record position on the selected
  tracks, top down. It is blank if the position is not on an effect or the
  effect's track is not selected (FX p.84).
* **Layout (picture on FX p.84):**
  * The effect's name and icon.
  * Its parameter categories, each with a triangular opener.
  * Under each category, sliders with their values, checkboxes, colour
    swatches and menus.
  * The controls of each parameter category come from the Effects Parameter
    Reference (FX p.491–586), in `src/ui/resources/effect_parameters.json`:
    92 categories.
  * Slider ranges and defaults are used only where the reference states
    them.
* **Slider keys (FX p.87):** Left and Right Arrow change a value by 1; with
  Shift, by 10.
* **Transition parameters (FX p.63, p.500):**
  * Transition Effect Alignment button and Transition Effect Duration box.
    They are active for a transition effect.
  * In Trim mode, typing a duration for a cut without an effect adds a
    Dissolve (FX p.63).
  * With no transition, the alignment icon is a vertical bar (FX p.500).
* **Not yet (Phase 6):** these buttons and controls are shown but off:
  * Down the right side: Reduce, Enlarge, Dual Split, Play Loop, Play, HQ,
    Tracking and Grid.
  * Along the bottom: Render Effect, Outline/Path, 3D Promote and the
    keyframe graph.
  * Controls that need the Effect Preview monitor, such as eyedroppers and
    tracking.

### Implementation status (1.7a)
* **Done:**
  * The Effect Palette.
  * Applying effects by drag and double-click, with Insufficient Source and
    Size To Fit.
  * Remove Effect.
  * Effect icons in the Timeline, with their Fast menu options.
  * The Effect Editor's parameters and Transition parameters, all
    undoable.
* **Not yet:**
  * Effect mode and the Effect Preview monitor: done in 6.2c
    (docs/spec/effect-mode.md).
  * Keyframes: done in 6.2 (docs/spec/effect-keyframes.md). Rendering and
    effect templates.
  * Quick Transition, Head Fade and Tail Fade.
  * Audio effects.
  * Applying effects to clips in the Source monitor.

## 14. Audio tools (ch. 21) and the Media Tool (p.344–348)

### Audio Mixer (p.690–726)
* **Opening:** Tools > Audio Mixer (p.690). The guide prints no shortcut.
* **Modes (p.690, p.692):**
  * Clip (Clip Volume and Pan), Auto (Volume and Pan Automation) and Live
    (Live Mix), shown as a three-part button.
  * Clip is the default.
  * The mode is saved in the Audio Project setting. Live Mix is not saved
    there.
* **Top bar (picture and table on p.691–693):**
  * Show Track Sidebar (">>").
  * The Mix Mode Selection menu: Stereo, Mono, the 4-channel mixes, and
    the 5.1 and 7.1 orders of the Monitor Mix Format (p.689, p.691–692).
    Direct Out comes later (docs/spec/surround.md).
  * The three mode buttons.
  * Bypass, hidden in Live mode (p.692).
  * Audio Loop Play, Render Effect and the Fast menu.
  * The Sequence Mix Format button at the top of the Master strip (p.693):
    Stereo, 5.1 or 7.1 Sequence (p.686).
  * Unlinked / Stereo Linked / Stereo Mirror (p.693); in a 5.1 or 7.1
    sequence, Pan grids and the Advanced Panner button in place of the
    pan knob, and right-click > This track is LFE only (p.705-710;
    docs/spec/surround.md).
* **Strips (picture and table on p.693–694):**
  * One strip per audio track of the sequence in the Record monitor, and the
    Master strip on the right. The strips scroll sideways (p.694–696).
  * Each strip has, top down: the gang button, the pan knob and Pan Value
    display, the effect insert "+", S and M, and the fader with its scale.
    Below the fader are the Volume Level display and the track button.
  * The scale is +12, +9, +6, +3, 0, −3, −7, −11, −15, −30, −45, −∞. The
    marks are equally spaced, as pictured.
  * The track button selects the Timeline track, and the reverse (p.698).
  * Solo and mute stay through mode changes and closing the mixer (p.694,
    p.699).
  * Right-click the gang button to choose a gang, A to J. Ganged faders move
    together (p.704).
* **Values:**
  * The fader goes to +12 dB. Typed values go to +36 dB and show yellow
    (p.669–670).
  * The default is 0 dB (p.702).
  * Alt-click (Option-click) the fader for 0 dB, and the Pan Value display
    for MID (p.703).
  * The pan shows as L57, MID or R57 (picture on p.693). The guide gives no
    range; ours is L100 to R100.
* **Clip mode (p.702, p.711):**
  * The fader and pan set the clip gain and pan of the whole segment under
    the position, never part of a segment. Each change is undoable.
  * Clip gain and pan are saved with the segment.
  * An edit between segments with different levels is not a match-frame
    edit.
* **Auto mode (p.701, p.714-720):** the faders and pan show and set the
  volume and pan automation at the position, and Record (or B) records it
  while playing, with preroll and postroll. The fader areas are blue
  (p.717). The Volume Level display shows "Clip" when the segment has clip
  volume. In Clip mode it shows "Auto" when the segment has automation
  (p.694). Details: docs/spec/automation.md.
* **Live mode (p.723–725):** values are temporary and are not saved. The
  fader areas turn red.
* **Fast menu (p.710–711, p.719–720, p.725–726):**
  * The items depend on the mode. They are dimmed until a track is
    selected.
  * They act on the selected tracks: IN to OUT, IN to the end, the start to
    OUT, or the whole track when there are no marks.
  * Clip mode: Set Level on Track, Set Pan on Track, Adjust Pan/Vols on
    Track, Remove and Commit Default Clip Pan on Track, Remove Clip Gain on
    Track, Remove Pan on Track, Remove Pan/Vols on Track, Disable Track
    Monitoring and Set Display Options all work (docs/spec/audio-mixer.md).
  * Auto mode, working: Filter volume automation (Alt: peaks only) and
    Filter Pan; Adjust Volume/Pan on Track; Remove volume automation, Remove
    Pan and Remove Volume/Pan; the three Remove Hidden items; Set Display
    Options.
  * Auto mode, not yet: Calibrate Hardware Sliders (external controllers,
    p.757).
  * Live mode: Set Live Mix to Default, Set Live Mix to Automation and Set
    Live Mix as Automation work (docs/spec/audio-mixer.md).
* **Audio Loop Play, Render Effect and the insert buttons (p.692, p.712,
  p.790):** see docs/spec/audio-mixer.md. So are Show Track Sidebar and Audio
  Grouping (p.691, p.696-698).

### Audio Tool (p.157–159)
* **Opening:** Tools > Audio Tool. Ctrl+1 comes from ks2009 p.12; the 2025
  guide prints no shortcut.
* **Layout (picture on p.158):**
  * RP (Reset Peak), an In/Out button per channel, and the PH (Peak Hold)
    menu.
  * Meters for 2 to 16 channels; we show 8.
  * The digital scale (0 to −90 dB) on the left. The VU scale on the right
    puts 0 VU at the −20 dB reference.
* **Meter colours (p.157):** green below the reference level, yellow up to
  −3 dB, red above.
* **Reset Peak** resets the peaks (p.157).
* **PH menu:**
  * Peak Hold and Infinite Hold (p.712, p.753).
  * Set Calibration Tone: level and frequency, −20 dB and 1000 Hz by
    default (p.920).
  * Play Calibration Tone (p.920) and Create Tone Media (p.158–159);
    details: docs/spec/tone.md.
* **Meters** show the output's peaks while sound plays (docs/spec/tone.md).

### Media Tool (p.344–348)
* **Opening:** Tools > Media Tool. The Media Tool Display dialog opens first
  (picture on p.346):
  * The media drives, with All Drives.
  * The projects, with Current Project and All Projects.
  * Checkboxes: Master Clips, Linked Master Clips, Precompute Clips -
    Rendered Effects, Precompute Clips - Titles and Matte Keys, Media Files.
    The first two are ticked by default.
* **The window (picture on p.347):** a bin-like list with the columns Color,
  Name, Duration, Drive, IN-OUT and Creation Date. It shows master clips,
  never sequences or subclips (p.345).
* **Media files and Delete Media (2.4b):** see §17.
* **Not yet:**
  * Precompute clips (they come with rendering).
  * The bin views and the Fast menu.

### Help > About: Configuration, Usage and Hardware (p.94, p.102–105)
* The 2025 guide has no Project window with Format, Usage, Info and Volumes
  tabs. That information is now found in these places:
  * Bins and volumes: the Bin Container's sidebar (p.240), or Tools >
    Project (p.243).
  * The format: the Format tab of the Settings window (p.1146). Our
    General settings show the project format.
  * Usage, Hardware and Configuration: buttons of Help > About (p.94).
* **Configuration (p.104–105):** the version, product options, licensing
  type, GPU and the installed plug-ins. This is information only.
* **Usage (p.102–104):** the project's statistics, such as the time the
  project has been open. *Not yet:* the Statistics files.
* **Hardware (p.102):**
  * A Drives tab with a bar for each online drive, showing the space used
    and the number of GB free.
  * A System tab with the operating system and its version.
  * *Not yet:* physical memory and Clear Memory.

### Audio Project setting (p.1148–1150)
* **Main tab:** Sample Rate, Bit Depth (24 Bit by default), Convert Sample
  Rates When Playing, Show Mismatched Sample Rates as Different Color, and
  Remove Extra Filler After Punchin (dimmed until Extended Audio Punch-In).
  Details: docs/spec/audio-settings.md.
* **Output tab:** Mix Mode Selection (Stereo or Mono). It is the same as the
  mixer's menu.
* **Mixer mode:** the mixer saves it here (p.692). It is not shown in the
  dialog.

### Implementation status (1.7b and 1.7c)
* **Done:**
  * The Audio Mixer: Clip and Auto modes work, and Live mode is temporary.
  * The Audio Tool.
  * The Media Tool and its display dialog.
  * Help > About's Configuration, Usage and Hardware.
  * The Audio Project setting.
* **Not yet:**
  * Meters that move (Phase 5).
  * Media files and Delete Media (Phase 2).
  * Surround panning, the track sidebar, the groups pane, and display
    options.

## 15. Linking file-based media (ch. 11, p.310–347)

### Source Browser (p.312–317)
* **Opening (p.315):**
  * File > Input > Source Browser.
  * Right-click in a bin, or the bin's Fast menu: Input > Source Browser.
  * Tools > Source Browser (p.327).
  * It is a panel, so it docks, floats and takes part in workspaces. The
    picture on p.313 shows it with a vertical title bar.
* **Top: navigation (p.313):** in this order:
  * Back, Forward and Up.
  * Home.
  * Collapse Directories, which collapses the tree to the top levels.
  * Add or Remove Favorites (the current folder).
  * Media Folder View.
  * Breadcrumbs: the path, each folder clickable.
* **Media Folder View (p.313):** shows a folder as a media volume. Every
  media file in it, and in the folders below, appears as a clip. "Other
  structural contents will not be displayed." While files are probed, the
  display fills in.
* **Left: places (p.313–314):** tabs Explore, Favorites and Recent. Explore
  is the tree of drives and folders.
* **Right: the display (p.314):**
  * Text view and Frame view buttons.
  * A search field.
  * A thumbnail slider (Frame view only).
  * A scroll bar for more columns.
* **Text view columns (ours):** Name (the file name), Duration, Start, End,
  Tracks, Video, FPS, Format, Audio SR, Tape, Plug-In and Source Path. The
  guide names the Plug-In column (p.322). A file no plug-in reads shows
  "Not detected" there.
* **Frame view (p.315–316):**
  * Thumbnails show the first frame.
  * Hovering across a thumbnail previews the clip (left edge: first frame;
    right edge: last). The frame last shown stays.
  * Ctrl+L (Command+L) enlarges thumbnails. The guide names only Ctrl+L;
    Ctrl+K reduces them, as it does for bin frames (p.250).
  * *Not yet:* J-K-L playing in a thumbnail (Phase 2.3).
* **Source Browser Settings (p.315):**
  * Double Click to: Link or Import, or Load Clips in Source Monitor.
  * Close Source Browser after Link or Import.
  * Clear Source Monitor Upon Closing Source Browser.
  * A Collapse/Expand button hides this part for more clips (p.316–317).
    The splitter above it can also be dragged.
* **Bottom (p.315, p.317):**
  * Link and Import. Import waits for Avid media files (Phase 2.4).
  * The Settings button, which opens the Link settings.
  * The Processing media indicator.
  * Target Bin, and the Link button.
  * Linking is allowed while files are still being processed.
* **Linking (p.317–318):**
  * Link links the selected files into the Target Bin.
  * With nothing selected, Link links every media file in the folder. This
    is "Linking a Volume to a Bin".
  * Dragging files to a bin also links them.
  * Right-click > Add to Bin links the selection.
  * Right-click a folder > Add Volume Bin makes a bin named after the folder
    with its media (p.318).
* **Previewing (p.316):**
  * Dragging a clip to the Source monitor loads it without putting it in a
    bin.
  * Cutting it into a sequence commits it to the Target Bin.
  * "Load in Source Monitor" on right-click is ours; it does the same.
* **Kept between sessions (ours):** the folder, Favorites, Recent, the view,
  the thumbnail size and the settings.

### Linked clips (p.310, p.317–330, columns p.274–278)
* **What a linked clip is:**
  * A master clip that points at the file where it is.
  * A file without picture becomes an audio clip.
  * The clip is named after the file without its extension (p.318).
* **Columns it fills:**
  * Start and End.
  * Tracks: V1, and one mono track per audio channel (A1-2).
  * Tape (the reel name).
  * Video: the codec, such as DNxHR LB, ProRes 422 HQ or H.264.
  * FPS.
  * Format: lines, p or i, and the rate, for example 1080p/25 or
    1080i/59.94.
  * Image and Pixel Aspect Ratio.
  * Color Space: RGB, or YCbCr 709 or 601.
  * Audio SR, Audio Bit Depth and Audio Format.
  * Video File Format (MXF).
  * Source File, Source Path, Drive.
  * Plug-In: ours is "FFmpeg".
* **Rates:**
  * A clip runs at the project's rate; its frames come from the file at the
    same time.
  * The file's start timecode is read as a time in the project's rate, as
    Sound TC is (p.330).
  * Audio clips run from the frame with the first sample to the frame with
    the last (p.329).
* **Frames:**
  * The monitors show a linked clip's frames. A sequence shows its top
    video track's frames, in the order Match Frame uses (p.434). Clips in
    sequences resolve through open bins.
  * Bin Frame and Script views show a clip's head frame (p.250).
* **Offline:** a clip whose file is gone shows "Media Offline" on black
  (p.347).
* **Link settings (p.1202–1205):** Quality, Volume Mounting, Link Options
  and AIS Metadata tabs. Only "Always use 16:9 aspect ratio for SD clips"
  works now. The rest come with relinking, volumes and multichannel audio.
* **Not yet:**
  * File > Input > Link to Volume for Export (AS-02, p.328).
  * Automatic linking of mounted volumes (p.311).
  * Multichannel audio mapping (p.330).
  * Live Link (p.319).
  * The Media Cache settings (p.1205).
  * The AIS Metadata options for DPX reel names and frame counts
    (p.1204–1205).

### Image sequences (p.325–328)
* **Grouping:** numbered stills (DPX, OpenEXR, TIFF, PNG, JPEG, TGA, BMP,
  Cineon) with the same name, number width and type link as one master
  clip. Which files go into it depends on the selection:
  * Linking a folder: one clip for each run of consecutive numbers (p.326).
  * One file of a run selected: the whole run (p.326).
  * Several files selected: one clip for each run of consecutive numbers
    among them, as in the guide's Ernie (1 and 2), Ernie (5 and 6) example
    (p.326).
  * A file with no neighbour is a clip of its own.
* **The clip:**
  * It is named for the files without their numbers ("Kermit" from
    Kermit_0001.dpx), or for the folder when the names are only numbers.
  * It plays at the project's rate.
  * Source File is the first file.

### Relink and Unlink (p.364–365, p.368)
* **Clip > Modify > Unlink Media (p.368):**
  * It unlinks the selected master clips, which then show "Media Offline".
  * The link is kept, so Relink can find the media again.
  * Sequences and subclips cannot be unlinked.
* **Clip > Relink > Linked Media (p.364–365):**
  * **Which clips:** the linked clips selected in the bin. With none
    selected, those in the bins selected in the Sidebar, or in every open
    bin.
  * **The dialog (picture on p.365):**
    * At the top: the "Offline only" / "All media" menu (Offline only by
      default) and a search field.
    * The list's columns: Color, Name, Source File, Source Path, Format,
      Duration, Tracks and Creation Date. Offline clips are red.
    * At the bottom: Locate Media and "Viewing: n/m".
  * **Locate Media:** asks for a folder and looks through it, and the
    folders in it, for each clip's source file by name. It works on the
    selected clips, or on all the clips shown.
  * **What relinks:** only media with the clip's duration and tracks, as
    for Relink to File(s) (p.320). A sequence is found by its first file,
    and its run is moved to that folder.
  * **Clips not relinked** are named in the Console (p.320). The dialog
    shows "n relinked, m not found".
* **Not yet:**
  * Relink > Managed Media and its Relink dialog (p.361–364), which come
    with Avid media files (Phase 2.4).
  * Relink to File(s) on right-click (p.320).

### Timeline Clip Frames (p.592)
* **Fast menu > Clip Frames:** "switches the display of start frames for
  each segment". The frame sits at the left end of each video segment,
  with the clip text after it.
* It shows only where the segment is wide enough.

## 16. Playback (p.389–404, p.627)

### Playing
* **Speeds (p.400–401):** L and J play at 1x, 2x, 3x, 5x and 8x, forward
  and backward; K pauses. This existed since 1.4a. The position now
  shows the media's frames.
* **What plays (p.395):**
  * A clip plays its picture and every audio channel; each channel is a
    track.
  * A sequence plays its top video track's picture, in the order Match
    Frame uses (p.434), and the audio tracks monitored in the Track
    Selector panel.
* **Panning:**
  * Mono tracks go odd to the left and even to the right (p.627). The
    Audio setting's Default Pan for mono tracks can centre them instead
    (p.1147).
  * A segment's clip gain and pan apply (p.702). A segment whose pan is
    centred (MID) keeps its track's side.
  * A one-channel clip in the Source monitor plays in the middle (ours).
* **Smooth audio scrub (p.675):**
  * Sound plays while shuttling up to three times normal speed, forward
    or backward. "The audio cuts out at greater than three times the
    normal speed."
  * Faster sound takes every n-th sample (averaged), so its pitch rises.
    With Pitch Correction during Shuttle (Audio settings, p.1148) it keeps
    its pitch (docs/spec/audio-settings.md).
* **Sync (ours):**
  * Up to 3x the picture follows the sound. The position is where the
    samples heard so far put it. Devices take samples in bursts and hold
    them, so the clock runs smoothly between bursts and leaves out what
    the device holds.
  * Faster, or without a sound device, the picture runs on the wall
    clock.
* **Digital audio scrub (p.676):**
  * It plays the frames around the position when the position moves
    while stopped: stepping, dragging, or any other move. It works with
    Shift held, or with the Digital Audio Scrub button on. The button is
    in the Play tab of the Command Palette (p.676).
  * It plays the outgoing frames before the position and the incoming
    ones after it. The defaults are 0 and 1 (p.675); the Audio setting's
    Source Monitor Scrub and Record Monitor Scrub change them (p.1147).
  * *Not yet:* placing the position at the head or tail of the last
    sampled audio (p.676).
* **Dropped frames (ours):**
  * The Composer counts the frames shown and the frames the picture
    skipped because it fell behind, from play to stop.
  * Dropped frames are reported in the Console. The real-time gate
    (2.5) measures with these counts.
* **Not yet:**
  * Dynamic Play (p.401–402) and mouse jog and shuttle (p.402–403).

### Video Quality Menu (p.403–404)
* **The button:** at the bottom of the Timeline.
  * A click moves to the next quality.
  * Right-click lists them, with Video Display Settings, which opens the
    Project's Video Display setting (p.405).
* **The qualities** of a software-only system (p.404):
  * Full Quality: the whole picture.
  * Draft Quality: half the width and half the lines.
  * Best Performance: a quarter of each.
* **The icon (ours):** green for Full, green and yellow for Draft, yellow
  for Best Performance. The guide gives the colours ("yellow/green",
  p.229); the drawing is ours.
* **Video Display setting (Project, p.390, p.1224–1225):**
  * High-Quality Scaling for Real-Time Decode works: it is smooth scaling
    of the picture, at Full Quality only.
  * The other options need graphics or I/O hardware and are shown as
    *later*.
* **Not yet:** the Full Quality 10-bit and float options, which come with
  effects processing.

### Client monitor and full screen (p.389–391)
* **Composer > Full Screen Playback (p.391):**
  * It shows the active monitor's picture on a whole screen: the second
    screen when there is one (ours).
  * Shift+Ctrl+F (Shift+Command+F) leaves it (p.391); so does Esc
    (ours). Other keys play and stop as in the Composer.
* **The Full Screen Playback setting (User, p.1189–1191):**
  * Reformat: Stretch, Pillar/Letterbox (our default), Center Crop,
    Center Keep Size and Raw Pixel.
  * Full Screen: off, the picture is shown with as little scaling as
    possible.
  * S3D View, Target Mask and Display Both Fields are *later*.
* The Toggle Client Monitor button is gray and disabled in software-only
  mode (p.390), as ours is.

### Sound output (ours)
* miniaudio, through Core Audio, WASAPI, PulseAudio or ALSA, at 48 kHz
  stereo. Files at other rates are resampled.
* Without a sound card, its null device keeps time, so CI tests the clock.

## 17. Managed media (ch. 9, p.211–216; ch. 12, p.349–359)

Media the application makes itself: imported, consolidated or transcoded.
The guide calls it Managed Media, unlike Linked Media (p.361).

### Where it goes
* `<drive>/Avid MediaFiles/MXF/<n>/`: the "1" folder when there is none,
  otherwise the next number for a new set (p.513, p.358). Ours: new media
  goes in the highest-numbered folder.
* Video Wrapper Format Avid OP-Atom: "each video and audio track will be a
  separate file" (p.1209). Ours: MXF OP-Atom through FFmpeg, named
  `<clip>.<track>.<id>.mxf`, carrying the clip's name, timecode and tape.
  The files' user comments `lumenis_media` and `lumenis_track` say which
  clip and track a file is, for the media database.
* Each media folder holds database files for the media in it (p.358). Ours:
  `MediaDatabase.json`, rebuilt for the files whose size or time changed.
* MXF OP-Atom audio is 48 kHz: FFmpeg's muxer writes no other rate. 16 or
  24 bits, from the Audio Project setting's Bit Depth (p.1148).

### Resolutions (p.1240–1256)
* Avid DNx LB, SQ, HQ, HQX and 444 (RGB only, p.1241), through FFmpeg's
  DNxHR encoder at the project's raster.
* At least 256x120 (p.355).
* Later: DNxUncompressed, 1:1, the DNxHD bit-rate names, ProRes, XAVC,
  AVC-Intra, DV, XDCAM, MXF OP1a and OMF.

### Media Creation setting (p.134–136, p.1206–1209)
* A project setting, from File > Media > Media Creation Settings (Ctrl+5)
  or the Settings list.
* Tabs: Capture, Titles, Import, Mixdown & Transcode, Motion Effects,
  Render, Media Type, Drive Filtering & Indexing (p.1207).
  * Video Resolution and Video/Audio Drive on Import and Mixdown &
    Transcode; the rest later.
  * The drive with the most space is bold and is used when the setting
    names none (p.135).
  * The Render tab's Effects Processing offers 8-bit, 16-bit, 32-bit and
    Automatic, the default. 32-bit is new in 2026.8 (FX p.151-152, 2026.8).
    It waits for rendering.
* Ours: drives are the writable volumes, plus the home folder where the top
  of the system volume cannot be written (macOS, Linux). Folders can be
  added through `LUMENIS_MEDIA_DRIVES` (BUILDING.md).
* Later: Apply to All, the Single/Dual Drives Mode button, drive groups and
  DisabledRes.txt (p.135–136).

### Import (p.211–216, settings p.1194–1199)
* File > Input > Import Media opens the Source Browser with Import chosen
  (p.214). Import makes new media at the Resolution beside it; Settings
  opens the Import setting (p.214). Double-click and Add to Bin import too.
* Import setting, a user setting with tabs Image, OMFI/AAF, Shot Log, Audio
  and XDCAM (p.1194–1199). Working now:
  * Frame Import Duration (default 10 seconds, p.1196);
  * Autodetect Sequentially Numbered Files (p.1197).
  The rest is shown and waits.
* Pictures are fitted to the raster keeping their shape ("Resize image to
  fit format raster", p.1195). The sound keeps its channels, one track each.
* The Audio tab's options (p.1198-1199): docs/spec/audio-settings.md.
* Later: Set Multichannel Audio (p.214), gain on import (p.215–216).

### Consolidate/Transcode (p.349–356)
* Clip > Consolidate/Transcode on the bin's selection. Consolidate or
  Transcode in the upper left; Target Drive(s); options; the button in the
  lower right (p.350–355).
* Master clips are copied whole (p.349).
  * After Consolidate, the Copying Media Files dialog (p.352): "Keep Master
    clips linked to media on the original drive" makes `.new.01` clips of
    the copies. Its other choice, "Relink Master clips to media on the
    target drive" (our wording), relinks the clips and makes `.old.01`
    clips of the old media.
* Sequences: the parts of their clips that they use, with handles, become
  new `.new.01` master clips, and the sequence uses them (p.349). Create
  new sequence(s) leaves the sequence alone and makes `<name>.new.01`
  (our name).
* Handle length defaults: 60 frames for NTSC rates, 50 for PAL, 24 for film
  rates (p.351).
* Options working now:
  * Video and audio on same drive(s);
  * Consolidate/Transcode only linked media;
  * Skip media files already on the target drive;
  * Target Video Resolution;
  * Raster Dimensions: project, source, source 1/4, source 1/16 (p.355);
  * Convert Audio Bit Depth;
  * Create new clips, offered with Dynamic Relink on (p.354-355; docs/spec/dynamic-relink.md).
* Consolidate copies the picture without converting it. That works for DNx
  (managed media, or linked DNx files at the project's rate). Other media
  is skipped with a note in the Console: transcode it.
* Later:
  * Delete original media files;
  * Relink selected clips before skipping;
  * group clips;
  * converting the sample rate and audio format;
  * Keep Source's Frame Rate;
  * source transformations;
  * Background Process and the Background Queue (p.356–357);
  * Distributed Processing;
  * subclips.

### Media database (p.358–359)
* Read when a project opens. File > Media > Load Media Database and
  Refresh Media Directories read it again. Refresh only rereads the files
  that changed (p.358–359).
* Bins opening, and open bins on a reload, point their offline managed
  clips at their files wherever they now are.
* The index keeps what each file is for relinking: clip, track, project,
  source file, tape, start timecode, length, rate and resolution. These are
  written into the files as user comments too (ours).

### Relink > Managed Media (p.361–364)
* Clip > Relink > Managed Media opens the Relink dialog.
* Working now:
  * Relink selected items to: All Available Drives, or Media on drive;
  * Master clips; All other items (sequences);
  * Relink only to media from the current project;
  * Selected items in ALL open bins;
  * Relink by Source Name: Tape Name or Source File Name, or Name;
  * Ignore extension; Ignore characters after last occurrence; Match case;
  * Relink method: Highest Quality, Most Compressed, Specific Resolution,
    with Relink if quality and Resolution;
  * If no match is found: Use Existing Media, Unlink (Take Offline);
  * Create new sequences (on by default), Allow relink across rates.
* Matching:
  * A master clip relinks to managed media of the same source name whose
    timecode covers all of the clip's frames and holds its tracks. Media
    that starts earlier is used from the clip's start (ours: `Link::first`).
  * A sequence's segments relink to master clips in the open bins holding
    the same material (p.361). With Create new sequences, the changes go in
    a copy named `<name>.relinked` (p.364).
  * Quality order: Avid DNx LB < SQ < HQ < HQX < 444.
* Later:
  * Timecode other than Start (Aux TC1–5, Sound Timecode);
  * the other Source Name choices;
  * Relink to (format) choices other than Any video format;
  * Format; Tracks to Relink; Allow relinking to offline items;
  * Relinking by Resolution and to Selected Clips (p.365–366).

### Media Tool media files and Delete Media (p.344–348)
* The Media Tool Display dialog lists the media drives and the projects with
  media on them, the current project first (p.346).
* With Media Files ticked, each media file is a row, named for its clip and
  track ("Interview V1", ours). Managed master clips show when Master Clips
  is ticked, linked ones when Linked Master Clips is.
* Edit > Delete or the Delete key opens the Delete Media dialog (p.349):
  * a box for each track the selection has media for: Video media file
    (V), Audio media file (A1, A2, ...);
  * then a confirmation, then Delete.
  * Precompute, Audio mixdown and Linked Delete metadata wait.
* Only managed media is deleted. Linked clips' files are never touched.
* Deleting managed clips from a bin opens a Delete dialog with check boxes
  for the clips and for their media files (p.359). The wording is ours.

### Background Consolidate and Transcode (p.356–357)
* Processing Options: Background Process makes a job for each selected
  item. The jobs run one at a time on their own thread while editing goes
  on, highest priority first.
* Tools > Background Queue Window lists each job with its name, Cancel
  button, progress bar and Priority menu (High, Normal (default), Low).
  Finished jobs show Completed. The Tool menu has Clear Inactive Jobs.
  * The Priority menu doesn't affect a running job (p.357).
* When a job finishes, its clips go in the bin as `.new.01` (p.356).
  Sequences are relinked, as when not in the background.
* Later:
  * offline placeholders in the bin while a job runs (p.356);
  * Resume for suspended jobs; Show Job Groups.

## 18. AAF import and export (p.547, p.861-870, p.1173-1177)

### Import (p.547)
* "Right-click in the bin, and select Import ... Locate the AAF file that
  you want to import and click Open" (p.547). Here: Import through the
  Source Browser (File > Input > Import Media, or the bin's Import), as for
  media. An `.aaf` file selected for Import is read as a composition, not
  transcoded: its master clips, subclips and sequences go into the target
  bin (docs/spec/aaf.md, "Import").
* "If you imported an AAF, all clips in the bin will automatically be
  linked to the corresponding MXF media" (p.547): master clips whose MobID
  is a managed media file's material package are online at once (the
  media database, read first).
* Columns the AAF carries that are not standard headings become the bin's
  custom columns. The Console lists each file's items and what was read
  only in part.
* Later: the sequence report (Popup Report on AAF Import, p.237), and the
  Import setting's OMFI/AAF tab (p.1197), which is about media.

### Export to File (p.868-869)
* Select the clips or sequences in a bin, then File > Output > Export to
  File (p.868, steps 1-2). The Export As dialog opens with a file name
  from the first item (p.868 step 2), the Export Setting menu (step 3),
  Options (step 4; it opens the setting, p.869), and Save (step 7).
* The Export Setting menu lists the User settings of the Export kind
  (p.861: "Select from the Export Settings list in the Export As dialog
  box"); one not named shows as Untitled, the guide's default (p.868).
* A sequence's clips are exported with it, so it is not offline where it
  is read.
* **Our choices, unverified.** The Export As dialog is ours: a file name
  with Browse, the Export Setting menu and Options; the guide's is the
  system save dialog with those controls. The setting chosen becomes the
  active Export setting. The folder last used is offered next time.
* The file name's extension is the format's (p.869): `.aaf`, or `.avb`
  for Media Composer Bin, also when the setting is changed in the dialog.
* Later: exporting from a monitor (p.869 step 2), drag-and-drop export
  (p.869), and every format but AAF and Media Composer Bin.

### Export setting (p.1173-1177)
* Export As (p.1173-1174): every format the guide lists; AAF and Avid Log
  Exchange (docs/spec/ale.md) work (others are shown dimmed). **Ours, not in the guide:** a last choice,
  Media Composer Bin, writes the selection as a `.avb` bin (docs/spec/avb.md,
  "Export"): the guide has no command that writes a bin for another system,
  Media Composer's bins being `.avb` files already (p.296), and File >
  Output > Export Bin writes shot logs (p.130). **Also ours:** after it,
  Final Cut Pro 7 XML writes the first sequence selected as FCP 7 XML
  (docs/spec/fcp-xml.md); Media Composer has no FCP XML.
* Use Marks, Use Selected Tracks (p.1174-1175) and AAF Edit Protocol
  (p.1175): shown dimmed, for later.
* Include All Video/Data Tracks in Sequence and Include All Audio Tracks in
  Sequence (p.1175): a sequence's tracks of that kind are left out when
  off.
* Video Details and Audio Details tabs: Export Method (p.1175): Link to
  (Don't Export) Media; Copy All Media and Consolidate Media are dimmed.
  Split Tracks to Mono (p.1176), "selected by default", is dimmed.
* **Our choices, unverified.** Defaults the guide does not give: Export As
  AAF, the two Include options on, Link to (Don't Export) Media.

## 19. EDLs, shot logs and OpenTimelineIO (p.106-132, p.939-969, p.1174)

The formats and what maps are in docs/spec/edl.md, docs/spec/ale.md and
docs/spec/otio.md.

### File > Input > Import EDL (p.951-952)
* Steps: File > Input > Import EDL; the Select File to Open dialog; a
  dialog with Frame Rate, Project Type and Audio Mapping; OK; the Select
  window: one of the bins, or New Bin; a message to Decompose the sequence
  before using it (p.951 steps 1-7).
* Here: the system's open dialog, then one Import EDL dialog with the file
  (and Browse), Frame Rate (the project's first), Project Type and Audio
  Mapping (dimmed: the guide does not list their choices) and the bin (New
  Bin first, the active bin chosen). **Our choice, unverified:** the
  options and the Select window as one dialog. The message is a Console
  line.
* The sequence and a source for each reel go into the bin, the sequence
  selected. Sources of reels the bin holds already are used again.
* Later: Decompose (p.951-952 steps 8-11).

### Tools > List Tool (p.944-960)
* A tool panel. Left (p.945, p.947 pictures): Active Setting (Default EDL
  and Default OpenTimelineIO, p.969 note; Save As and Remove Current,
  p.956-957, later), Output Format, List
  / Change List (Change List dimmed), the Input, List Options and
  Formatting tabs. Right: Preview, Save List, Clear List, New List, Clone
  List; the Master EDL and Sources tabs (p.947 picture).
* Output Format lists CMX_3600, CMX_DigitalCut, CMX_Transfer, Cuedos,
  File_16, File_32, File_129, then Columnar, TabbedLists, WebLists, XML
  (p.945 picture, p.947 step 2), then OTIO (p.969 step 2). CMX_3600,
  File_129, Columnar, TabbedLists, WebLists, XML (9.4a-b,
  docs/spec/cut-lists.md) and OTIO work; the rest are dimmed. WebLists'
  page names other than CutAsm.htm, its style sheet and our XML elements
  are ours. Change List (9.4c) is enabled for the cut list types and adds
  an Old Sequence row with Load and a Change List options box; **ours:**
  how shots are matched, the change list's columns, Preview Code as a
  numbering column, Show only changes on by default. For the cut list types the List Options tab
  shows a Cut List box (Assemble with Mark Short/Jump Cuts, Scene Assemble,
  Optical with Key Frames and Optical Footage, Dupe with Assume and Print
  Handles, Pull with its sorts and order, Reel Names, Clip Comments, Start
  at). **Our choices, unverified:** that box's layout and defaults
  (Assemble, Optical, Dupe on; Pull off; First Sort by Camroll), Start at in
  frames, Save List's `.txt`. **Our choice, unverified:**
  OTIO last (the p.945 picture predates it).
* Default OpenTimelineIO and the OTIO format go together: choosing the
  setting chooses OTIO, choosing OTIO makes it the active setting
  ("current user profiles can create one by setting the output format to
  OTIO", p.969), and an EDL format brings back Default EDL. With OTIO,
  Preview shows the .otio file's JSON in Master EDL, and Save List writes
  an `.otio` file (p.969 step 3); the List Options and Formatting tabs do
  not apply to it.
* Input tab (p.945, p.948 steps 3-4): the sequence (Load: the Record
  monitor's; Clear), a toggle button for each track, all on, and the Title,
  the sequence's name. A sequence selected in the active bin when the tool
  opens is loaded (p.948 step 3). Dragging a sequence in: later.
* List Options (p.958): Picture Dissolves and Black Edits, Sound Dissolves,
  Clip Names and Markers work; the others are shown dimmed (a subset of the
  guide's list).
* Formatting (p.958-960): Optimize EDL and Starting Event Number work;
  Source TC (Start), Record TC (TC1), Reel ID Type (Tape), Sort Mode (A),
  Tape or File Name truncation (End) show their working first entry, the
  rest dimmed; Convert tape names to numbers and Include source table
  dimmed.
* Preview fills Master EDL and Sources (reel, source name, MobID; p.950-951)
  and sends the list's warnings to the Console. Save List writes the list
  to one file (p.949 step 9), named for the Title.
* **Our choices, unverified:** the defaults of List Options (Dissolves,
  Clip Names, Markers on; Black Edits off) and Formatting (Optimize EDL
  off); the guide gives none. The panel's first size (980 × 560).
* Later: multiple files, Edit mode with Find (p.949-950), New List, Clone
  List, Sources' Save List, the template's case setting (p.950: names are
  capitalized).

### OpenTimelineIO import (p.969)
* File > Input > Import Media with an `.otio` file (step 2), or a drop of
  `.otio` files into a bin (step 1), from the Source Browser or the
  desktop's file manager: a sequence for each timeline and an offline
  master clip for each piece of media, its file kept for Relink > Managed
  Media; clips the bin holds already (the same media) are used again. The
  counts and warnings go to the Console.
* A bin's own Input > Import Media (step 3, a right-click in the bin)
  comes with the bin's context menu, later.
* **Ours:** Import Media and a drop into a bin take FCP 7 XML (`.xml`
  files whose root is `<xmeml>`) in the same way (docs/spec/fcp-xml.md).

### Shot logs
* **Import** (p.131-132): Import in the Source Browser of an `.ale` file
  (or a `.txt` whose first line is Heading) puts its clips into the target
  bin; the log's custom columns join the bin's.
* **File > Output > Export Bin** (p.130): the Export Bin As dialog: the
  Export Bin As menu (Avid Log Exchange; Tab Delimited dimmed), and the file
  name with Browse (**ours**: the guide does not show where it is named),
  then OK. The active bin's master clips, with its Text view and custom
  columns.
* **Export to File** with the Export setting's Export As at Avid Log
  Exchange (now working, p.1174): the selected master clips and subclips,
  to an `.ale`.
* Later: the Import setting's Shot Log tab (p.1197-1198), Tab Delimited.

## 20. Audio Mixdown (p.731-732)
Details: docs/spec/audio-mixdown.md.
* **Timeline > Mixdown > Audio > To Sequence** opens the Audio Mixdown
  dialog box: Source Tracks, Range, Mono / Dual Mono / Stereo / 5.1 / 7.1
  with the target track, Include Master Fader, a bin and a drive, Save
  Premix Sequence, OK (p.732). **Ours:** the layout; Stereo, 5.1 and 7.1
  dimmed until multichannel tracks; Dual Mono targets are pairs of empty
  tracks; the clip is named "<sequence> Mixdown".
* Timeline > Mixdown > Audio > Multiple Mixes opens the Multi-Mix Tool
  (p.732-734); details: docs/spec/multi-mix.md.

## 20b. Audio EQ (p.736-745)
Details: docs/spec/audio-eq.md.
* **Tools > Audio EQ** (p.736) opens the tool as a panel.
* **Top row** (table on p.736-737): the Effect icon (dragged to a bin or a
  Timeline audio clip, p.740), Audio Loop Play, Render Effect (later), the
  Fast menu with the predefined templates and Site_EQs_Bin's (p.745), the
  Track Selection Menu button (plus signs for several tracks), In, and
  Bypass RT EQ.
* **Bottom part** (picture on p.737): the EQ Parameter display, the low
  shelf, parametric midrange and high shelf sliders (+15 to -20 dB, p.739),
  the 3-band buttons (50/80/120/240 Hz; 2 octaves or 1/4 octave; 6/8/12/15
  kHz, p.738), the EQ Range slider (50 Hz to 15 kHz) and the EQ Parameter
  graph.
* **Fast menu** (picture on p.744): Set EQ - In/Out, Remove EQs On Track -
  In/Out, Remove EQs On Enabled Tracks - In/Out, then the twelve
  predefined templates. Their values come from
  src/ui/resources/eq_templates.json: Telephone EQ "A" is observed, Tape
  Hiss Filter partly documented (p.743), the rest guesses. A template's
  values are shown but cannot be changed (p.744).
* **Remove Effect** removes an EQ at the position (p.741).
* **Ours:** the filter shapes, the defaults (500 Hz midrange), a change
  setting the segment at once, an OUT alone acting from the start, and
  the Timeline's EQ icon.

## 20c. Audio Punch-In (p.746-753)
Details: docs/spec/punch-in.md.
* **Tools > Audio Punch-In** (p.750) opens the tool as a panel.
* **Layout** (picture and table on p.748-749):
  * Play In/Out (V), Record (B), Stop, Go to Mark In; Cancel, Audio Tool;
  * Monitor Mode Input: Automatic, On, Off, Manual;
  * Stop at end or at mark out; Pre/Post (sec); Handles (sec); Input
    Source;
  * CH 1-4 (Alt+click for another input channel), each with a Timeline
    Track menu: New Track or a mono audio track that is not locked;
  * Target Drive, Target Bin.
* **Scenarios** (p.749-750): only an OUT (the position is the IN), IN and
  OUT with Record, and no marks with Record pressed on and off.
* **Result:** one master clip per take, "<sequence> Voice Over.<n>"
  (picture on p.751); each punch-in is an undo step (p.751).
* **Ours:** button glyphs; the whole target track silenced during a take;
  no latency compensation; the punch-out at the frame before the position.

## 20d. Audio Track Effects (p.790-795)
Details: docs/spec/track-effects.md.
* **Five inserts per audio track**, a to e, processed left to right in real
  time (p.790, p.794).
* **Tools > Audio Track Effect** (p.791): Select Track, Select Insert,
  Select Effect, the Plug-In Settings menu, Bypass (Ctrl+click: all
  tracks), and the parameters (picture on p.793). The arrow keys change
  the track and the insert (p.794). Select Insert > "no insert" removes one
  (p.795).
* **Right-click an audio Record Track button:** Audio Track Effects [n] >
  Insert [a-e] > [effect], and Audio Track Effect Tool (p.791, p.793).
* **The Effect Palette's Audio Track tab** (p.791-792): drag an effect onto
  an audio track, and the Select Insert dialog picks the insert.
* **Unavailable Effect** with the effect's name for one not installed
  (p.790).
* **Ours:** the built-in effects, Ctrl+Bypass not saved, double-click
  inserts. Templates, Compare and settings files come later; the Track
  Control panel's insert buttons came with U1b1.

## 20a. Shared bins (p.300-304)
Details: docs/spec/bin-locking.md.
* **Third-party storage** (2026.8 guide p.301; What's New 2026.8 p.2):
  Media Composer 2026.8 Enterprise supports shared projects on NAS and SAN
  storage that is not Avid NEXIS. These use "the same project-sharing and
  bin-locking workflows as other shared project environments". Neither
  document describes Avid's lock files or the protocol, so ours stays as
  it is. It was built for any shared volume, NAS included, and its
  behaviour matches that description (docs/spec/bin-locking.md).
* **Bin Lock Status button** (p.301-303), at the left of a bin's top row,
  only for a bin on shared storage (p.302 note). Green: this user holds the
  lock and may change the bin. Red: another user holds it, or it is
  protected (for everyone). Yellow: the holder saved changes since it was
  read; the bin's tab adds "(modified)" (p.302; the wording is **ours**);
  a click reads it again and turns it red. Blue: the holder released the
  lock; a click takes it and turns it green. Alt (Option)+click on green
  releases the lock (p.303). Otherwise a click shows the bin's history: who
  saved it, on which machine, when (p.301), in a window of its own
  (**ours**: the guide does not show it).
* **Bin Container** (p.301-302): bins locked by another user are in bold;
  **ours**: a tooltip names the holder ("ed on EDIT-1", with `*` for Locked
  and `**` for Protected, p.302). Alt (Option)+double-click opens a bin
  without taking its lock (p.302).
* **Right-click a bin** in the Bin Container: Lock Project Bin, Protect
  Project Bin and Unlock Project Bin (p.302), for the selected bins or the
  one clicked; **Clip > Lock Project Bin** and **Clip > Unlock Project
  Bin** (p.302 note) for the selected bins, else the active bin.
* **Restrictions** (p.304): no drops into a bin someone else holds, nor new
  sequences, imports or duplicates; items dragged out of one are
  duplicated; it cannot be moved, renamed or deleted. **Ours**: changes to
  its items are refused (the Console says so), rather than made and then
  not saved.

## 20f. Scripts (ch.18, p.566-590; docs/spec/scriptsync.md)

* File > New > New Script, File > Save Script, File > Save a Script Copy As (p.571), 8.3a.
* The Script window (p.570-576): the AS and AP buttons, Edit Mode, Show Find Tools, the Scene and Page menus, the status
  bar, the Delete dialog's Delete scene(s) and Delete page break(s); Script > Add Scene, Add Page, Text Layout, 8.3a.
* The Script setting (p.1217), 8.3a; its slate and take options 8.3b; Pre-roll with 8.3c.
* Slates and takes (p.576-582), 8.3b: clips dropped from bins, the Play, Set Offscreen and Set Color buttons, Delete
  Takes; Script > Color (p.582), Show All Takes, Show Frames (p.578), Hold Slates Onscreen (p.578), Interpolate Position
  (p.577); Edit > Enlarge Frame and Reduce Frame on slates (p.578).
* Script marks (p.582-589), 8.3c: the Command Palette's Other tab Add Script Mark and Find Script; the Script window's
  Record and Find Bin buttons; Delete mark(s); Ctrl+Alt+double-click splicing; the Script setting's Pre-roll.
* Script > ScriptSync AI and its dialog (p.584-585), the bin's Transcript > Create Script from Clip (p.570), 8.3d.
* The script lock icon and the close dialog's Save Script As / Don't Save / Cancel (p.589-590), Edit > Find's Script Text tab
  and Scripts in Project (p.429-430, p.440-441), 8.3e.

## 20e. Transcripts (ch.14, p.436-453; docs/spec/transcripts.md)

* Bin > Include Bin in Transcription, Exclude Bin from Transcription (p.447), also on a bin's right-click menu in the
  Sidebar; "Transcription On" or "Transcription Off" to the right of each bin.
* File > Settings > Project > Transcript (p.447-450): Include all new bins in transcription, Pause background
  transcription while editing, Language Hint, Subclips/Groups, Manage Speakers (p.445, 8.2b2b: the Manage Speaker Names
  window). Manage Transcripts later (8.2d).
* File > New Project's Language Hint under Transcription (p.448), dimmed when the Site settings have a Transcript
  setting.
* Tools > Transcript Tool (p.442), 8.2b1; its editing and speakers 8.2b2.
* Edit > Find's PhraseFind AI button, Score and Transcription, the PhraseFind AI Index and the Settings button
  (p.436-441), 8.2c.
* The bin's right-click Transcript menu: Export Transcript, Share Transcripts, Delete Transcript, Export SubCap
  (p.450-453, 2026.8 guide p.456); the Timeline's Transcript > Create SubCap (p.452); Script > Manage Transcripts and
  the Transcript setting's Manage Transcripts button (2026.8 guide p.455), 8.2d.
* The Timeline's right-click AutoCut on a group segment and the Transcript Tool's Assign to Camera Angle (2026.8 guide
  p.451-452), 8.2e.

## 20g. Background services (ch.12, p.371-377; docs/spec/background-services.md)

* Tools > Background Services (p.376-377) with its dialog, and the Timeline's progress indicator with its right-click
  menu (p.375-376), 8.4a. Tools > Dynamic Media Folders (p.372) with 8.4b-8.4c.

## 20h. Unconnected items (9.6)

The owner's request (2026-11-05): no command, button, menu item or key may reach nothing. Checked by
`tst_command_wiring` once a project is open:

* every command declared in `src/ui/resources/commands.json` has a handler, else it must be listed in
  `tests/data/dimmed_commands.json` with the reason it is dimmed on purpose (hardware and capture, Avid's
  shared-storage and asset management services, features not made yet); a listed command that has a handler, or is
  not declared, fails too, so the list stays current;
* every key of the Keyboard setting (`keyboard.json`) and every mapped button (the Composer's, the Timeline's, the Tool
  palette's and the Command Palette's) names a declared command that reaches something;
* a key mapped to a menu command works in the Timeline as in the Composer.

menus.json's and command_palette.json's ids are checked against the declarations when they load (menu_builder.cpp,
command_palette.cpp). Found and fixed by the check:

* Composer > MultiCamera Mode (p.1129) and the Digital Audio Scrub button (p.676) were checkable actions whose
  `toggled` did the work but had no handler, so they stayed dimmed;
* in the Timeline, a key mapped to a command that is neither the Timeline's nor the Composer's did nothing;
* the Edit tab's To the Left, To the Right and Select In/Out (p.615-616) and Make Subclip (p.408) had no handler.

Names the guides give one item (the command keeps the first; ours where none):

| Command | Names in the guides |
| --- | --- |
| `edit.toggle_source_record` (Esc) | "Toggle Source/Record button in the Timeline toolbar" (p.480); "Toggle Source/Record in Timeline button" (p.603, p.607, p.1119); "Toggle Source/Record button" (Capture tool, p.145, another button). Effect mode's Source/Record Mode button (`edit.source_record_mode`, FX Guide) is a different item. |
| `edit.to_the_left`, `edit.to_the_right` | "To the Left", "To the Right" in the Edit tab's picture (p.100) and the Timeline's right-click Select menu (p.614); "Select Left", "Select Right" buttons in the text (p.616). The menu takes "the clips to the left of the current position indicator" (p.614); the buttons also take the segment under it, and filler unless Alt is held (p.616). |
| `edit.select_in_out` | "Select In/Out" (p.100, p.616). |
| `clip.autosync` | The guide's AutoSync (p.223, p.497), ours "Sync Clips" (ROADMAP 9.8; docs/spec/sync-clips.md). |
| `file.input.source_browser`, `tools.source_browser` | "Source Browser" in File > Input and Tools: one window, two menu places. |
| `bin.autosequence`, `clip.autosequence` | "AutoSequence" in the Bin and Clip menus: one command. |
| `clip.split_all_tracks`, `clip.audio.split_all_tracks`, `timeline.split_all_tracks` | "Split All Tracks" in three menus: one command. |
| `clip.group_clips`, `clip.refresh_sequence.group_clips` | Clip > Group Clips makes groups; Clip > Refresh Sequence > Group Clips refreshes group edits: two items with one name. |
| `bin.sequence_report`, `composer.sequence_report` | "Sequence Report" from the bin (its selected sequences) and the Record monitor (its sequence): one report. |
| `move.go_to_previous_trim`, `move.go_to_next_trim` | Only in the Move tab's picture (p.96); ours, as `trim.select_previous_transition` and `trim.select_next_transition` (Select Previous/Next Transition, A and S, p.651). |
| `move.go_to_audio_mark_in`, `move.go_to_audio_mark_out` | Only in the Move tab's picture (p.96): Go to Audio Mark IN/OUT, as Go to IN/OUT for the audio marks (9.6b). |

## 21. Still to document
* Menu order and separators, confirmed against a licensed seat.
