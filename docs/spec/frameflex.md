# Reformatting and FrameFlex (8.5)

Editing Guide p.475-478 (the Reformat attribute, Refresh Sequence) and p.527-535 (Source Settings, FrameFlex,
Reformatting). 8.5a is the data and the picture; 8.5b the Source Settings dialog and FrameFlex in the Effect Editor.

## The Reformat attribute (p.475-477, p.534, p.1204; 8.5a)

* "Media Composer uses the Reformat attribute of a clip to resize and reposition the clip so that it conforms to the
  current frame size and aspect ratio" (p.475): Stretch, Pillarbox/Letterbox, Center Crop and Center Keep Size
  (`model::Reformat`), kept in the clip's Reformatting Options column (`model::kReformatHeading`, the p.277 heading).
* "If you are linking to clips, then the default is set to Center Keep Size" (p.475), as the Link setting's Link Options
  "Reformattting option" says ("Set the default for all new clips that are created", p.1204; `LinkOptions::reformat`).
  Ours: a clip with no attribute (made before 8.5a, or by Import, which makes media at the project's raster) is
  pillarboxed or letterboxed, as before.
* The column is changed in the bin: "Click the Reformat field for the clip ... and select an option" (p.476; ours: the
  column's right-click menu, as for Field Motion, `reformatMenu`). Master clips and subclips only.

## FrameFlex (p.529-533; 8.5a data)

* `model::FrameFlex`, kept in the clip's Source Settings (`BinItem::sourceSettings`): Image Size (an override of the
  raster: "a good indicator for source media that may have been preprocessed to a proxy resolution"), Image aspect
  ratio, Pixel aspect ratio, Frame aspect ratio, X, Y, Size, Z Rotation and the box's Color (p.529-530).
  `model::sizeMatchingRaster` is "Size matches project raster"; `isFullImage` the state Reset gives.
* Ours: X and Y are pixels from the image's centre, Size the box's width over the image's, Z Rotation degrees
  anticlockwise. A box that is the whole image is no box.

## Spatial adapters (p.528, p.531; 8.5a)

* "A Spatial Adapter effect is applied either when the clip is reframed, or reformatted to fit within the project frame
  size" (p.528): `model::SpatialAdapter` (Reformat + box), which a video segment takes when the clip is edited
  (`Segment::spatial`, `model::spatialAdapterOf`), as the Source monitor's own Timeline view does. Not an effect.
* `media::spatialAdaptedImage` draws it: the frame scaled to its Image Size and widened by its pixel aspect, the box cut
  around its centre and turned by its Z Rotation, then fit into the frame by the Reformat attribute; black elsewhere
  ("The application first crops out the unwanted area and then fits the image within the project frame size", p.535).
  Center Keep Size keeps one image pixel per project pixel. The Composer's frame source (`MainWindow::adaptedFrame`)
  applies it to whole frames from the frame cache, for segments and for clips in the Source monitor ("The new framing
  of the image will be applied when you drop the clip in the Source viewer or on the Timeline", p.531).
* The green dot (p.531): on segments with a FrameFlex box (ours: not for the Reformat attribute alone, which every
  linked clip has); Show Adapters > Spatial stays dimmed until 8.5b.
* Clip > Refresh Sequence (p.478): Aspect Ratio and Reformatting Options gives segments their clip's current adapter;
  All FrameFlex including creative framing also replaces boxes changed in the sequence (`SpatialAdapter::creative`, set
  from 8.5b by the Effect Editor). `model::refreshSpatialAdapters`.

## The Source Settings dialog's FrameFlex tab (p.527-535; 8.5b1)

* Opened from the bin's right-click Source Settings on master clips and subclips ("Select one or more clips in the bin,
  right-click and choose Source Settings", p.529) and from the Source monitor's ("Right clicking on the clip in the Source
  monitor, selecting Source Settings", p.316), command `bin.source_settings`; `SourceSettingsDialog` in
  `src/ui/source_settings_dialog.*`, run by `MainWindow::sourceSettings`.
* Show Viewers (p.529): the top viewer (`FramingViewer`) shows the first clip's frame with the framing box; dragging inside
  it moves it, a corner resizes it, the handle above its top edge rotates it ("use the center handlebar in the top viewer
  to rotate the image", p.532). The bottom viewer "displays the framed area as it would appear within the actual project
  frame" (p.531).
* The fields (p.529-530): Raster Dimension, Image Size with the button that sets it to the raster (p.531), Image, Pixel
  and Frame aspect ratio, X, Y, Size, Z Rotation with Rotate Left and Rotate Right (90 degrees, p.532), Size matches
  project raster, Color, Reset and the Reformat menu; Revert ("Reverses any changes you made since the last time the
  Apply button was clicked") and Apply. Apply sets the settings on every selected clip; ours: OK applies too, Cancel
  leaves what was applied.

## The Playback Rate tab and the Timeline (p.542, p.545; 8.5b2)

* The Timeline's right-click Source Settings on a video segment opens the dialog for its clip ("On the Timeline, or in
  the bin, select the clip ... right-click and choose Source Settings", p.542, p.545; `Timeline::sourceSettingsRequested`,
  `MainWindow::sourceSettingsForSegment`).
* Playback Rate: "Select the desired Playback Frame Rate" (p.545), the clip's original rate or the project's. At the
  project's rate the clip's file frames play one per frame (`Link::conformed`, `media::setPlaybackAtProjectRate`): its
  duration becomes the file's frame count and it is no longer a mixed rate clip. Back at its original rate it plays its
  frames at their own time again. Ours: only clips without sound (their sound would not follow), and only linked clips
  with a moving picture; the menu is dimmed with the reason otherwise.
* "If you applied this change to a clip in a bin, and want to update all instances of this clip that may already be on
  your Timeline, you need to select Clip > Refresh Sequence > Motion Adapters/Timewarps" (p.545): segments of a clip
  now at the project's rate lose their adapter. "There is no automatic ripple" (p.545): the segments keep their length.
  Ours: a clip set back to its original rate gets adapters in new edits; Refresh does not add them to old segments.

## Tests

* `tst_frame_flex`: the attribute's texts, adapters from clips through JSON and bins, Size matches project raster, the
  four Reformat options and boxes (moved, turned, with an Image Size), Refresh Sequence with creative framing.
* `tst_frameflex_ui`: linking with Center Keep Size, the column changed, the Source monitor's picture, the segment's
  adapter, a box set after the edit and Refresh Sequence; the Source Settings dialog's fields, buttons, drag, Revert and
  Apply on two clips; the Playback Rate tab from the Timeline, the clip's frames and duration, Refresh Sequence dropping
  the adapter, and back; a failed Export to File's message.
* Scenario `49-spatial-adapter.json`.
