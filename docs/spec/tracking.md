# Motion tracking (ROADMAP 6.7c)

Following a pattern through a segment and letting its movement drive an effect (FX Guide ch.10, p.248-275;
reference p.498-499, p.571, p.577-578). Code: `src/model/tracking.*` (trackers, regions, points, offset tracking,
SteadyGlide, Smoothing, the reference frame, parameter mappings and the change at a frame),
`src/media/tracking.*` (the engines, `media::trackedEffect` and the Tracking category's map), the compositor and
`warp3d.cpp` hooks, `src/media/region_stabilize.cpp` (Region Stabilize). Test: `tst_tracking`. 6.7c1 is the data and the engines; 6.7c2 the Tracking window, the Effect
Editor's tracker controls and the Effect Preview's boxes (`src/ui/effect_editor_tracking.cpp`,
`src/ui/tracking_window.*`; test `tst_tracking_ui`); 6.7c3 editing the data (the same files, `model::deletePoints`
and friends; tests `tst_tracking`, `tst_tracking_ui`).

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
  target). Display Effect Results hides tracking data; In to Out shows the points between the Display Markers (6.7c3).
* Start Tracking (`EffectEditor::startTracking`) runs each enabled tracker from the position to the end of its
  region with the chosen engine over frames MainWindow composes (480 pixels wide): the video tracks below the
  effect's own, or with Track Foreground the effect's own segment without its effects. Data already there is
  replaced; the run is one undo step and what was tracked before a stop is kept.

## 6.7c3: editing the data
From the guide (p.265-274):
* "Click the data point"; "Shift+click the first and last data points in the range"; lasso them; double-clicking a
  point moves to its frame. Selected points are "highlighted with a circle" and white on the tracker timeline
  (p.267, p.271).
* "Media Composer repositions the selected data points along a straight line between the two unselected points
  that bound the selected points" (Delete, p.272); dragging one "All the data points move along with the one you
  drag and maintain their positions relative to one another" (p.272).
* Stretch Points Mode: the others move "proportionally along the path between the point you have moved and the
  anchor point or points. If you move a point at either end of the selected range, the point at the opposite end
  becomes the anchor. If you move a point in the middle of the range, the two end points become anchors" (p.273-274).
* The tracker timeline's right-click menu: Add New Tracker Region (to "the start of the next tracker region or the
  end of the timeline"), Delete Tracker Region ("deletes the tracker region and any tracking data for it"), Set
  Reference Frame (p.266, p.269); Go to Previous/Next Region "move to the previous or next region boundary".
* "Drag the Display Markers in any one of the tracker timelines to define the beginning and end of the range"; In
  to Out shows the data points between them (p.268).

Ours:
* Points are kept per tracker by frame (`EffectEditor::selectedPoints`); a click on a selected point keeps the
  selection for a drag and, without a drag, selects that point alone. Shift+lasso adds to the selection. The
  selection belongs to the effect shown and clears when another one is.
* Delete with a bound on one side only puts the selected points on it; the Delete key reaches the editor through
  `Timeline::setDeleteHook` before the Timeline's own uses of it.
* Stretch Points moves each point in proportion to its frame's distance from the anchors.
* A new region's boxes are where the tracker is at its first frame (else its region's). Deleting the first region
  only deletes its data.
* Go to Previous/Next Region uses every tracker's region starts. Clicking a tracker timeline moves the position.
  The Display Markers are brackets, dragged within 4 pixels; the whole effect at first. Moving them with Mark IN
  and Mark OUT (p.268) is not yet.

## 6.7d1: tracking data through edits, copying and pasting trackers
From the guide (p.281-284):
* "Tracking data persists during Segment editing ... If you lift a portion of the segment out of your sequence,
  tracking data is preserved on all parts of the segment for the range of frames in each part"; "If you trim in from
  the edges of a segment, tracking data is preserved for both the new length of the segment and for the material you
  have trimmed. If you later trim the edges of the segment back out, the tracking data is still available"; "If you
  trim out beyond the tracked data ... you must track over the range of these new frames".
* Copy: "Enable the trackers you want to copy in the Tracking window", Edit > Copy or Ctrl+C; Paste with "the
  Tracking window ... active": "The trackers appear in the Tracking window below any existing trackers", in the same
  effect as "Duplicate copies of the enabled trackers ... with new tracker numbers".

Ours:
* The data counts frames from the effect's first frame, so `model::shiftTrackingData` moves points, region starts
  and the reference frame wherever material is added to or taken off a segment's head (`Sequence::cut` for the
  second part of a split, trims, slides). The first region always reaches back to the effect's first frame. Data
  outside the segment stays and comes back when it is trimmed out again.
* Copied trackers are kept for the session (not on the system clipboard); a pasted tracker whose name is taken gets
  " 2", " 3" ..., since the selectors find trackers by name; at most eight in all. Ctrl+C and Ctrl+V in the Tracking
  window, and Edit > Copy and Paste while it is the active window.
* Scenario `33-tracking-through-edits.json`.

## 6.7d2: the Stabilize effect
From the guide (p.275-279, p.472):
* "Stabilizing lets you track an object in a shot that should not be in motion and then use the tracking data to
  keep that object stationary ... using the tracking data to move each frame of the video enough to keep the tracked
  object stationary" (p.275); "The tracking process exposes black around the edges of the frames" (p.276).
* AutoStabilize, on applying Stabilize to one segment (not to several, not Alt-nested): "Opens the Effect Editor
  (if necessary) and the Tracking window. Creates a tracker and enables the tracker in the Position parameter for
  the effect. Tracks the image using the FluidStabilizer tracking engine. Processes the tracking data using the
  SteadyGlide option and applies the data to the image. Uses the Auto-Zoom option"; stopping it keeps the setup for
  stabilizing by hand (p.276-277).
* SteadyGlide is "particularly useful if you want to remove jittery motion ... while retaining underlying motion such
  as a pan" (p.278).
* Auto-Zoom, "In the Tracking subcategory of the Position parameter category": Position and Scaling that "remove black
  edges. Any existing keyframes on the Position and Scaling parameters are removed"; recalculated when the data, its
  SteadyGlide or Smoothing change; warnings without data or when the image "moves too far offscreen"; moving the
  Position or Scaling sliders turns it off with a message (p.279).
* "When you promote the Stabilize effect to 3D, Stabilize Tracking is automatically activated for all effect
  parameters that use tracking data. The Run Auto-Zoom option is not available after you promote" (p.277); 3D Warp's
  Stabilize Tracking "Enables tracking for stabilizing the image" (p.577-578).

Ours:
* `model::stabilizes`: the Stabilize effect's tracking always stabilizes, other effects' where Stabilize Tracking is
  on. `trackedChange` then undoes the change: Position the movement from the reference frame (with SteadyGlide only
  the jitter SteadyGlide takes out, measured from the reference frame's), Scaling's factor inverted, Rotation's turn
  reversed, the Tracking category's map from the current points back to the reference frame's.
* Stabilize renders as Resize does (`imageEffectPicture`) with its Border, the Tracking category's map applied first
  (compositor).
* Auto-Zoom (`model::applyAutoZoom`) takes the stabilizing Position over every frame: the scale (the same on both
  axes) that covers the largest range of moves, the offset that centres it; rotation and scale changes are not
  counted. "Too far" is more than 400%, Scaling's maximum. It is kept as `autoZoom` with the tracking data; any change
  to the trackers recalculates it (`EffectEditor::keepAutoZoom`, in every undo step of the shown effect), and a change
  to Position or Scaling (values or keyframes) otherwise turns it off.
* AutoStabilize (`Timeline::autoStabilizeRequested`, `EffectEditor::autoStabilize`) runs after the drop or
  double-click, moves the position to the segment's first frame and is one undo step.
* Promoting Stabilize or Picture-in-Picture to 3D carries the tracking data (`model::promoteTo3D`); Stabilize's with
  Stabilize Tracking on and Auto-Zoom off.

## 6.7d3: the Region Stabilize effect
From the guide (p.279-281; the Effects Reference has no entry for it):
* It "does not work automatically, and has fewer controls than Stabilize"; "the Progressive Source option ... lets
  you specify that the footage you are tracking is progressive material so that it is tracked correctly in an
  interlaced project" (p.276, p.279).
* "A wire frame appears in the Effect Preview monitor to indicate the region of interest"; "Use the sliders in the
  Region of Interest parameter category" or "Click the wire frame, and drag it"; the Model Fast Menu: Translational
  "steady along both the horizontal and the vertical axes", Horizontal "Features in the region of interest can move
  vertically but not horizontally", Vertical the other way; Options' Auto Zoom "compensates by resizing and, if
  necessary, repositioning the clip"; "Render and review the effect"; "add keyframes to reestablish or refine the
  region of interest over the course of the clip" (p.280-281).

Ours:
* Its categories (effect_parameters.json): Region of Interest's X and Y (its centre from the frame's) and Width and
  Height, percent of the frame (a quarter of the frame, centred, until set); Model; Options' Auto Zoom and
  Progressive Source.
* `media::regionMotion` follows the region from each frame to the next with the Correlation Tracker (the region of
  the frame before, a search area twice its size) through the segment's own pictures at most 480 pixels wide, and
  adds the moves up from the effect's first frame. The motion is kept per clip, first frame, picture size and region
  (values and keyframes), so each frame is tracked once.
* `media::regionStabilizePicture` moves the picture back on the Model's axes; Auto Zoom enlarges and centres it to
  cover every frame's move, as the Stabilize effect's Auto-Zoom does. Uncovered parts show what lies below.
* In the Effect Preview the original picture shows under the region's wire frame (as Pan and Scan's): inside drags
  it, the top, side and corner handles size it about its centre.
* Progressive Source is kept; our tracking follows whole frames, so it changes nothing yet.

## Not yet
* The Display Markers from Mark IN and Mark OUT (p.268).
* Region Stabilize's Progressive Source (field-based tracking for interlaced projects).
* FluidStabilizer drifts on pictures of pixel-sized noise (a test pattern, not footage); a smooth pattern tracks
  exactly.
* The Tracking category on 2D effects (the Intraframe effects, Blur and Mosaic).
