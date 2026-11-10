# Stereoscopic projects (8.6d)

Editing Guide p.65-66 (the project's Stereoscopic option), p.213 (stereoscopic files are imported as 2D files and then
"converted to stereoscopic clips"), p.243 (Stereo Master Clips), p.562 (the 2048 x 1080 S3D preset) and p.1124
(Grouping Stereoscopic Clips). The guide leaves how stereoscopic clips are made to the Media Composer Family Help
("search for “Working with Stereoscopic Clips” in the Media Composer Family Help", p.213), which we do not have, so
the commands that make and change them are ours (see the watch list in ui-inventory.md).

8.6d1 is the clips and the project's option; 8.6d2 (below) the S3D views and settings; 8.6d3 (below) groups, lists and presets
(ROADMAP).

## Stereo Master Clips (p.243; 8.6d1)

* "A single clip generated from a left and right eye pair of master clips" (p.243): `model::makeStereoClip` in
  `src/model/stereo_clip.*`. Both eyes' links and names are kept in the clip's Source Settings under "stereo"
  (`model::StereoClip`); the clip's own link is its leading eye's, so everything that plays a clip as 2D (the Timeline's
  frames, exports, renders) plays that eye.
* Ours: Create Stereoscopic Clip (`bin.create_stereoscopic_clip`) on the bin's right-click menu when two master clips
  are selected (`MainWindow::createStereoscopicClip`). The left eye is the clip whose name ends in "L" or "Left" (after
  a space, "_", "-" or "."; the right one "R" or "Right"), else the first in the bin. The new clip is named
  "<left name> S3D", takes the left eye's columns, is as long as the shorter eye, and is added to the same bin, selected.
  Refused, with the reason in the Console, for clips without media, sequences, stereo clips, clips of different rates
  and in SD projects ("You can only work with stereoscopic material in an HD project", p.66).
* "The leading eye image is defined by the S3D Leading Eye clip attribute" (p.66): the "S3D Leading Eye" column
  (`model::kLeadingEyeHeading`), Left for new clips. Ours: changed from the bin's right-click menu's S3D Leading Eye
  submenu (`bin.s3d_leading_eye_left`/`_right`, `MainWindow::setStereoLeadingEye`; `model::setLeadingEye` moves the
  clip's link to that eye).

## The project's Stereoscopic option (p.65-66; 8.6d1)

* Off, Leading Eye, Left Eye Only, Right Eye Only, Side by Side, Over / Under and Full (`stereoscopicModes()` in
  core/project_format; `model::stereoModeOf`). It is chosen when the project is made (the Select Project dialog, "not
  available in SD") and shown on the Format tab. Ours: `MainWindow::setProjectStereoscopic` changes it, kept in the
  project file.
* "Off ... Any stereo material in the sequence is treated as a standard format, and only the leading eye image is
  used"; Leading Eye, Left Eye Only and Right Eye Only use that eye (`model::shownEye`, `model::eyeClip`).
* Side by Side and Over / Under are "Frame compatible", "using horizontal half res for each eye" or "vertical half res
  for each eye"; "If you have any standard (non-stereo) material in the sequence, it will use the same image in both"
  frames (p.66): `media::frameCompatible` in `src/media/stereo.*`, applied by `MainWindow::adaptedFrame` before the
  colour and spatial adapters of each eye (`MainWindow::monoFrame`). Ours: the left eye on the left or on top.
* "Full: Uses both left and right images in a stereo master clip": the monitors show both through their S3D View
  (8.6d2); a picture made without one (the Timeline's frames, exports) is the leading eye's.

Tests: `tst_stereo` (the model and the pictures), `tst_render_ui stereoscopic` (the bin, the monitor in each mode).

## S3D Views and Overlays (p.1162-1163, p.1190-1191, p.1226-1227; 8.6d2)

* The Composer setting's Viewer tab: Source and Record Monitor S3D View and S3D Overlay (`sourceS3DView`,
  `recordS3DView`, `sourceS3DOverlay`, `recordS3DOverlay`); the Full Screen Playback setting's S3D View and S3D Overlay
  (p.1190-1191); the Video Output tool's S3D View and S3D Overlay (p.1226-1227), kept with its values and shown by
  `MainWindow::videoOutputPicture` for the output modules (7.3c), which send only test patterns until then.
* The views (`model::S3DView` in `src/model/stereo_view.*`, `media::stereoView` in `src/media/stereo.*`):
  * Mix, "a 50/50 blend of both the left and right eye images";
  * Difference, "gray pixels represent no differences" (ours: middle gray plus half the difference of the eyes'
    brightness, so differences show lighter or darker, embossed);
  * Frame Compatible, "as dictated by the project settings (side/side or over/under)"; ours: Side by Side in a Full project;
  * Mono with Left, Right and Leading eye (the eyes are listed for Full Screen Playback and the Video Output tool, p.1190,
    p.1227; ours: the monitors take them too);
  * Anaglyph Red-Cyan and Green-Magenta, and Amber-Blue for Full Screen Playback and the Video Output tool (p.1190,
    p.1227): ours, each eye's colour channels (Red-Cyan: the left eye's red, the right eye's green and blue);
  * B/W Anaglyph (the Video Output tool's Mono-Anaglyph): the same with each eye's brightness;
  * Checkerboard 1 x 1 to 100 x 100 blocks, the left eye's block first;
  * Compare 25, 50 or 75%, "a diagonal split screen": ours, the left eye above a diagonal from top right to bottom left
    placed at that part of the way along the other diagonal;
  * the Video Output tool's Follow Project (the project's own picture), Full Frame ("both the left and right eye at full
    resolution": ours, side by side at twice the width) and Follow S/R ("the settings in the source/record monitors";
    ours, its default).
* Ours: the views apply in projects whose Stereoscopic option uses both eyes (Full, Side by Side, Over / Under); the
  others have one picture to show. Each eye's picture is made by composing the monitor's picture with that eye of every
  stereo clip (`MainWindow::stereoPicture` through `eyeOverride_`), so sequences mix stereo and 2D clips as in 8.6d1. The
  Source monitor's group splits show no view ("the stereoscopic view is turned off", p.1124). The default view is Frame
  Compatible (ours), which shows Side by Side and Over / Under projects as 8.6d1 did.
* The S3D Overlay's guides (`media::drawParallaxGuides`): "Parallax Near: Displays green guides", "Parallax Far: Displays
  blue guides", Near/Far both (p.1163). Ours: each a pair of vertical lines either side of the picture's centre as far
  apart as the parallax, scaled from the S3D Settings' Raster Resolution to the picture.

## S3D Settings (p.1216; 8.6d2)

* The Project setting "S3D": Unit System (Imperial, Metric), Viewing Distance, Screen Width, Raster Resolution, Eye
  Separation ("the default is 6.35 cm"), Depth Limit Near ("always a negative value") and Far ("always a positive
  value"), Parallax Near and Far ("automatically calculated. If you set the drop-down button to manual, you may override
  the calculated setting").
* Ours: lengths in metres (Metric; the eye separation in centimetres) or feet (Imperial; inches); Raster Resolution as
  pixels across; defaults of a 10 m screen 10 m away, 1920 pixels, -2 m and +10 m. The parallax is the eye separation
  times depth / (viewing distance + depth), as a part of the screen width, in pixels of the raster
  (`model::parallaxPixels`); the calculated values are written into the setting so it shows them
  (`MainWindow::applySettings`), and the drop-downs are Calculated or Manual.

Tests: `tst_stereo viewValues`/`viewPictures`/`parallaxAndGuides`, `tst_render_ui stereoViews`.

## Groups, lists, presets and playback (8.6d3)

* "Stereoscopic clips can be used for multicam editing in the same manner as regular clips ... (You can mix both regular
  and stereoscopic clips in a group clip.)" (p.1124): a Stereo Master Clip is a master clip, so Group Clips takes it.
  "While viewing stereoscopic clips in the source monitor, the stereoscopic view is turned off. You can choose to display
  either the left eye image, the right eye image, or the image set as the leading eye. This view mode is set by the
  Stereoscopic option in the Project Format dialog box": a group in the Source monitor, whole or split, and MultiCamera
  mode show Left Eye Only's left eye, Right Eye Only's right eye, else (ours) each clip's leading eye, with no S3D View
  (`MainWindow::stereoPicture`).
* The List Tool's S3D Contributor (p.959; Formatting tab, `listS3DContributor`): "Monoscopic: Set this option for
  standard sequences, or if you only want to use the 'leading eye' clips in a stereoscopic sequence. S3D Left: Only
  displays the left eye clips. S3D Right: Only displays the right eye clips." A stereo clip's segment is listed as that
  eye's master clip: its name for FROM CLIP NAME, its reel and timecode (`interop::edl::ExportOptions::s3dContributor`,
  `model::contributingEye`/`eyeSource`; the clip keeps its eyes' ids and MobIDs). When the eye's clip is gone its name is
  still listed, with a warning. Ours: only the EDL output takes it; OpenTimelineIO lists the clip as it is.
* Refresh Sequence > Stereo Correction Effects (p.478): "Refreshes the sequence so that all source-side effects applied
  to stereoscopic clips in the bins are updated accordingly in the sequence." Neither guide describes such effects (the
  FX Guide's S3D category lists none, p.37), and our clips carry no source-side effects, so the command is in the menu
  dimmed (no handler) until they come (watch list).
* "2048 x 1080 S3D (stereoscopic)" (p.562): the "2K DCI Full S3D" preset in core/project_format, the 2K DCI Full raster;
  ours: choosing it in the Select Project dialog turns Stereoscopic on as Full when it is Off.
* "Media Composer can only play back interlaced stereoscopic material using one of the Full Quality options. When you are
  working with interlaced stereoscopic material, other options are not available" (p.403): ours, in an interlaced project
  whose Stereoscopic option is on, `Composer::setFullQualityOnly` keeps Full Quality and the Video Quality Menu dims the
  others.

Tests: `tst_stereo makeStereoClip`, `tst_edl s3dContributor`, `tst_render_ui stereoViews` (the group and the List Tool),
`tst_render_ui interlacedStereoFullQuality`.
