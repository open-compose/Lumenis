# Stereoscopic projects (8.6d)

Editing Guide p.65-66 (the project's Stereoscopic option), p.213 (stereoscopic files are imported as 2D files and then
"converted to stereoscopic clips"), p.243 (Stereo Master Clips), p.562 (the 2048 x 1080 S3D preset) and p.1124
(Grouping Stereoscopic Clips). The guide leaves how stereoscopic clips are made to the Media Composer Family Help
("search for “Working with Stereoscopic Clips” in the Media Composer Family Help", p.213), which we do not have, so
the commands that make and change them are ours (see the watch list in ui-inventory.md).

8.6d1 is the clips and the project's option; 8.6d2 the S3D views and settings; 8.6d3 groups, lists and presets
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
* "Full: Uses both left and right images in a stereo master clip": ours, until the S3D views of the monitors (8.6d2)
  show both, Full shows the leading eye.

Tests: `tst_stereo` (the model and the pictures), `tst_render_ui stereoscopic` (the bin, the monitor in each mode).
