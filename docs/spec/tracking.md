# Motion tracking (ROADMAP 6.7c)

Following a pattern through a segment and letting its movement drive an effect (FX Guide ch.10, p.248-275;
reference p.498-499, p.571, p.577-578). Code: `src/model/tracking.*` (trackers, regions, points, offset tracking,
SteadyGlide, Smoothing, the reference frame, parameter mappings and the change at a frame),
`src/media/tracking.*` (the engines, `media::trackedEffect` and the Tracking category's map), the compositor and
`warp3d.cpp` hooks. Test: `tst_tracking`. 6.7c1 is the data and the engines; 6.7c2 the Tracking window, the Effect
Editor's tracker controls and the Effect Preview's boxes (`src/ui/effect_editor_tracking.cpp`,
`src/ui/tracking_window.*`; test `tst_tracking_ui`); 6.7c3 editing the data.

## From the guide

* "Motion tracking searches for a distinctive pattern of pixels in each frame of a video segment ... Media
  Composer records the movement as a series of points, one for each frame"; "Each set of data points is contained
  within a structure called a tracker"; one tracker for position, two for rotation or scaling, three or four for
  skew and perspective (p.249, p.259).
* "The Position, Scaling, and Rotation parameters let you use tracking data to control the specific kinds of
  motion associated with each parameter"; "The Tracking parameter category ... allowing you to enable up to four
  trackers"; "The two groups of parameters that use tracking data are mutually exclusive" (p.250-251). Effects:
  Picture-in-Picture and Matte Keys and titles (Position, Scaling), the Intraframe effects (Tracking), Stabilize,
  3D Warp (Position, Rotation, Scaling, Tracking as Corner Tracking) (p.251).
* Enabling: "Media Composer creates an appropriate number of trackers (one for Position, two for Rotation or
  Scaling) and maps them to the parameter"; "looks for existing trackers and maps them ... If trackers do not yet
  exist, Media Composer creates them"; names "the next letter of the alphabet ... up to the letter D ...
  'Untitled Tracker'"; "up to eight trackers for each effect"; the whole parameter's Enable "disables all the
  trackers at the same time but does not change the enable status or the mappings of the individual trackers"
  (p.257-260).
* Engines: the Correlation Tracker ("often the best first choice"), FluidTracker ("uses the same motion analysis
  methods that FluidMotion Timewarp uses") and FluidStabilizer ("divides the image into a grid ... combine the
  motion of these points, de-emphasizing any values that are not consistent with the general pattern"); tracking
  runs from the position indicator, frame by frame, and stops on the space bar (p.263-265).
* Offset tracking: "track one target area until it becomes unavailable ... then track another target area that
  shows the same kind of motion ... Media Composer calculates the offset from the first set of tracking points to
  the second"; a region's tracking "steps back one frame from the region boundary" (p.265-267).
* "The reference frame ... is the frame at which the tracking data makes no change to the appearance of the
  effect"; "By default ... the first frame" (p.269). SteadyGlide "reduce[s] abrupt, jittery movements without
  affecting underlying motion"; Smoothing "smooth[s] out the path between the tracking data points"; a value of 1
  to 100; the two are mutually exclusive and leave the original points unchanged (p.269-270). Position's Axis menu
  limits it to X or Y (p.274-275).

## Ours (6.7c1)

* Kept with the effect under its parameters' "Trackers": the trackers (name, colour, enable, offset, processing
  and its value, reference frame, regions with their target and search boxes and their own points), the engine,
  Track Foreground, and the mappings by parameter (on, T1-T4 with each its enable, Axis, Stabilize Tracking).
  Places are fractions of the frame; frames count from the effect's first.
* Offset tracking: a region's points are shifted so that they meet the path at the frame before the region (where
  its tracking began), else at the path's last point. Without offset the path jumps.
* Smoothing is a Gaussian over frames of up to 10 frames (value 100); SteadyGlide one of up to 20, and its jitter
  (the path less the smoothed path) is kept for stabilizing (6.7d). Between points, and beyond the ends, the path
  takes the nearest data (in a straight line between two).
* What the data does at a frame (`model::trackedChange`): Position moves by the path's movement from the
  reference frame (Axis: X or Y only); Scaling and Rotation by the change in distance and angle between the two
  trackers (measured in the frame's proportions; clockwise positive); the Tracking category maps the trackers'
  reference points to their points now (a move, a similarity, an affine or a perspective map for one to four).
  `media::trackedEffect` folds the first three into Position (pixels of a 1920-wide frame, Y up for 3D), Scaling
  and Rotation Z before the compositor draws the effect; Corner Tracking's map moves the 3D Warp's placed picture.
* Engines (`media::trackTarget`): the Correlation Tracker matches the target's pattern from the region's first
  frame (never updated, so it does not drift) by normalized cross-correlation over the search box around the last
  place, to a tenth of a pixel; FluidTracker follows our optical flow at the point; FluidStabilizer takes the
  median of a 4 x 4 grid's flow.

## 6.7c2: the Tracking window, the controls and the boxes

* From the guide: tracking controls "appear in three locations: the Effect Editor, the Effect Preview monitor, and
  the Tracking window" (p.251); the Tracking window opens when tracking is enabled for a tracker not yet mapped, or
  from the Tracking Tool button (p.253); its components (Start Tracking, New Tracker, Tracking Engine, Track
  Background/Foreground, Point Range, Display, and per tracker Enable Tracker, Offset Tracking, SteadyGlide,
  Smoothing, Smoothing Value, Tracker Color, Tracker Name, timeline and Close) (p.254-255); the tracking box display,
  "An inner rectangle used to define the target area, An outer rectangle used to define the search area, A data
  point locator", moved by dragging inside and resized by each rectangle's corner handle, shown at the first frame
  of the segment or region (p.260-261); generating data from the position, stopped by the space bar, Ctrl+period or
  a click (p.264-265); the tracker timeline's black (data), red (none), region bars and reference diamond, the data
  points as dots with the position's as a cross, Point Range Current/In to Out/All and Display Tracking Data/Effect
  Results (p.267-269).
* Ours: the Effect Editor's rows for Position and Scaling (2D "Tracking"), the 3D Tracker Selector rows, and the
  Tracking and Corner Tracking categories show an On box (the parameter's enable), a Fast menu per selector T1-T4
  (the Tracking category's each with its own On box), Position's Axis menu, and Stabilize Tracking; each change is
  one undo step (`EffectEditor::setParameterTracking`, `mapTracker`, `setTrackingAxis`, `setStabilizeTracking`).
  Enabling opens the Tracking window (`src/ui/tracking_window.*`, a tool window kept by the Effect Editor); the
  Tracking Tool button shows it. Alt+click on an Enable Tracker button sets every tracker.
* The Effect Preview shows each enabled tracker's data points and the position's cross, and its boxes at its
  region's first frame (ours: also while the region has no data); dragging inside the search box moves both boxes,
  a box's bottom-right corner resizes it (the target at least 1% of the frame; the search area grows to hold the
  target). Display Effect Results hides tracking data; In to Out shows all points until 6.7c3's Display Markers.
* Start Tracking (`EffectEditor::startTracking`) runs each enabled tracker from the position to the end of its
  region with the chosen engine over frames MainWindow composes (480 pixels wide): the video tracks below the
  effect's own, or with Track Foreground the effect's own segment without its effects. Data already there is
  replaced; the run is one undo step and what was tracked before a stop is kept.

## Not yet
* 6.7c3: selecting, moving, deleting and stretching points, regions, the reference frame, the display range.
* The Tracking category on 2D effects (the Intraframe effects, Blur and Mosaic), and Stabilize (6.7d).
