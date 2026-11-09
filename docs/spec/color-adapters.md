# Colour encoding and colour adapters (8.5c)

Editing Guide 2025.12, "Setting the Color Properties of Acquired Media" (p.536-541) and "Removing Color Adapters from a
Sequence" (p.544). LUT files, CLF and CDLs are 8.5d; the Display Color Space and ACES are 8.5e.

## The clip's encoding (p.536-540; 8.5c1)

* A clip's Source Settings keep its Color Encoding (`model::ColorEncoding` in `BinItem::sourceSettings` "colorEncoding";
  `src/model/color_adapter.*`): the Source color space, the colour transformations "applied cumulatively starting from
  top to bottom" (p.540) and "Bypass all color transformations: Ignore all color encoding settings" (p.539).
* "Any applied transformations can be seen from the Color Encoding tab or under the Color Transformations bin column"
  (p.536): the column `kColorTransformationsHeading`, the transformations separated by ", ". Ours: "Bypassed: ..." when
  bypassed; the column is added to the bin headings after Color Space (p.273-278's table does not list it).
* The Source color space menu: ours, since the guide lists none: Unknown, Rec.709, Rec.601, Rec.2020, sRGB, Linear,
  Gamma 2.6, Printing Density (Cineon), Arri Log-C, Canon C-Log, Sony S-Log2 S-Gamut. "If you click the Auto button, the
  application will do the necessary color mapping to go from the specified color space to the project's color space"
  (p.538): `model::autoTransformations` gives the guide's transformations from each space to Rec. 709 (ours: every
  project counts as Rec. 709 until 8.5e; Rec. 601, 2020 and S-Log2 have none we make).

## The transformations (p.539-540; 8.5c1)

`src/media/color_transform.*` (`media::colorTransformationNames`, `applyColorTransformations`, `transformColor`), on RGB
from 0 to 1:

* Levels scaling (full range to video levels): 0-255 to 16-235; (video levels to full range): back, clamped.
* Gamma 2.6 to Linear: v^2.6.
* Linear full range to REC709: ITU-R BT.709's transfer function, then video levels ("applies a 2.2 gamma correction
  (Rec.709) while scaling the levels to become legal"). Linear video levels to REC709: video levels in and out ("while
  keeping the same black and white points").
* Printing Density (Cineon) to linear: Kodak's printing density (black 95, white 685 in 10 bits, 0.002 density per code
  value, 0.6 gamma), "Automatically clamps the high white values".
* SRGB (IEC 61966-2.1) to linear: the standard's curve. (The guide's text prints "IEC 619662.1".)
* Arri ALEXA Log-C SUP 3.x to ITU 709: ARRI's published Log-C v3 curve at EI 800, the ALEXA Wide Gamut to Rec. 709
  matrix, then the 709 transfer function, full range. Ours: scene-referred, without a display tone curve.
* Canon C-Log to REC709: Canon's published C-Log curve, then the 709 transfer function, full range.
* Sony's four looks (p.540) are not built: their curves are not published. A provider adds them, and 8.5d's LUT files,
  with `media::registerColorTransformation`; names not registered are skipped.

## Colour adapters on segments (p.528, p.540, p.544; 8.5c1)

* "When clips are viewed in the Source monitor or dropped on the Timeline ... the color transformation is applied"
  (p.540): a video segment edited from a clip whose encoding has transformations, not bypassed, takes it as its Color
  Adapter (`Segment::color`, `model::colorAdapterOf`). MainWindow's frames go through the transformations before the
  spatial adapter (`MainWindow::adaptedFrame`). Segments with one show the adapters' green dot (p.528).
* Clip > Refresh Sequence > Color Adapters: "Refreshes the sequence so that clips whose color space has been changed now
  use the attributes set on the master clip" (p.478; `model::refreshColorAdapters`).
* Refresh Sequence > Remove Color Adapters (p.544): "It does not affect adapters on the master clips (as source
  settings); only the sequence is modified" (`model::removeColorAdapters`). Ours: a later Refresh Sequence > Color
  Adapters puts them back.
* Both work on the sequences selected in the active bin, else the Record monitor's (`MainWindow::refreshSequences`).

## The Color Encoding tab (p.537-540; 8.5c2)

* The Source Settings dialog's tab (`SourceSettingsDialog::setColorEncoding`/`colorEncoding`): the Source color space
  menu, Auto (`autoTransformations`, the list becomes `model::autoTransformations` of the space), the Color
  transformations list, the drop-down of `media::colorTransformationNames()` with Add, Delete for the selected row,
  dragging rows to reorder ("change the order in which they are applied by selecting and dragging the transformation up
  or down in the list", p.540; `moveTransformation`), and Bypass all color transformations.
* "The selection is added to the Color transformations list and applied to the image in the viewer" (p.539): both
  viewers show `shownFrame()`, the frame through the list (as it is when bypassed).
* Revert goes back to the last Apply with the other tabs; "Click Apply to propogate the settings to all clips that you
  selected in the bin" (p.540): the Applier sets each clip's encoding (`model::setColorEncoding`) and its column. The
  dialog opens with the first clip's encoding. Ours: a source space the menu does not list stays as it is. The Color
  Management Settings button and the LUTs are 8.5d's.

## Transcode (p.544; 8.5c2)

* The Consolidate/Transcode dialog's "Apply source transformations: Color encoding" (`ConsolidateOptions::
  applyColorEncoding`; Frame Flex stays dimmed, ours): "Select this option if you want to transcode clips with their
  applied color adapters". Each clip's colour adapter goes into its new media (`MediaCreation::colorTransformations`,
  through an 8-bit picture), so the new clip has no encoding and the new sequence's segments no adapter.
* Off, "the adapters are bypassed during the sequence transcode only. The source transformations still remain on the
  clips in the bin": the media is the source's colours, and the new clip keeps the clip's encoding (so it plays as
  before). "The transcode operation only applies source transformations that were made on the clip (from the bin)"
  (p.513): the clip's encoding, not the segments' adapters.

## External LUTs and CDLs (p.539-544; 8.5d1)

* "The application supports two different types of LUT formats: 1D LUT ... 3D LUT" (p.541). `src/media/lut_file.*`
  reads the products the guide lists by their first line (`media::lutProduct`):
  * IRIDAS 1D (.itx, "LUT_1D_SIZE"), IRIDAS 3D (.cube, "LUT_3D_SIZE") and Mistika 3D (.itx): the keyword format, with
    TITLE, DOMAIN_MIN/MAX and the INPUT_RANGE keywords, red changing fastest;
  * Kodak KDM (.3dl, "# IDENTIFICATION: 3DMODEL-3DLUT") and Lustre grids: the input grid's line, then integer RGB with
    blue changing fastest, the output scaled by its bit depth;
  * Autodesk ("LUT:" and the number of channels and entries): each channel's entries in turn.
  1D tables are interpolated linearly, cubes trilinearly (`LutFile::apply`). Ours: Avid DS ("AVID DS LUT"), LUTher
  ("#channels: c3") and Nucoda ("NUCODA_3D_CUBE 2") files are recognised and refused with a reason, as their layouts are
  not published.
* "The installed LUT(s) will be listed at the bottom, prefixed with the word External" (p.542): each readable file of the
  project's `LUTs` folder ("Imported LUTs are stored in the project folder", p.542; `media::projectLutFolder`) and of the
  folder for every project (`media::sharedLutFolder`: ours, the application data's `ColorManagement/LUTs`, or
  `LUMENIS_SHARED_LUTS`; none with `LUMENIS_NO_SYSTEM_DRIVES=1`) is registered as "External <file name>" when a project
  opens (`media::registerLutFolders`); the files that cannot be read are named in the Console. `media::installLut` copies
  a file in and registers it, for 8.5d2's Select LUT file.
* "CDL ASC_SOP=(111)...)(111)ASC_SAT=1 - Applies CDL values already associated with the master clips (found in the
  ASC_SOP and ASC_SAT bin columns)" (p.540): `media::cdlTransformationName` names the clip's values in the transformation
  ("CDL ASC_SOP=(s s s)(o o o)(p p p) ASC_SAT=x"), so it stands on its own in a list and a segment's adapter; it applies the
  ASC CDL: (in x slope + offset) clamped, to the power, then saturation about Rec. 709 luma. "Alternatively, you can
  select the CDL option from the Color Transformation list" (p.543): the Color Encoding tab offers the clip's at the end
  of the drop-down (`SourceSettingsDialog::setClipCdl`).
* The fuzz target `lut` reads arbitrary bytes as both kinds of file.

## The Color Management setting and CLF files (p.539-544, p.1157-1158; 8.5d2)

* The User setting "Color Management" (settings.json; p.1157-1158, the Installed LUTs from p.539):
  * "Insert color transformations automatically in the source settings upon linking": a linked clip's encoding takes
    Auto's transformations for the colour space its media names (`LinkOptions::insertColorTransformations`). Ours: off
    to start with ("Do not select this option if you want to work with the original media files").
  * "Use CDL values from ASC_SOP and ASC_SAT bin columns when available": the Color Encoding tab's Auto adds the clip's
    CDL ("click the Auto button on the Color Encoding tab", p.543; `SourceSettingsDialog::setClipCdl(name, true)`).
    Ours: off to start with; a clip linked afresh has no CDL columns yet ("CDL transformations are only applied if the bin
    columns of the master clip have already been populated via an ALE/EDL merge", p.539).
  * "Automatic HDR conversions from SDR media (Rec709)": its two choices are kept for 8.5e's HDR projects.
  * Installed LUTs, Project, Shared or Both, and the Select LUT file button (`MainWindow::installLutFile`): the file is
    copied into the project's `LUTs` folder, every project's, or both, and listed as External; one that cannot be read
    or copied says why in a message (`lutNotInstalled`) and the Console. Ours: the button uses the saved Installed LUTs
    choice, as the button opens the file dialog before the setting's dialog is closed.
* "For some known camera formats, the application reads the color space metadata within the source media" (p.538): a
  linked clip's Source color space comes from its video's transfer characteristic (`StreamInfo::colorTransfer`,
  `media::sourceSpaceOfTransfer`: bt709, smpte170m/bt470bg, bt2020-10/12, iec61966-2-1 and linear). Ours: by the
  transfer alone; our own writer declares bt709 or smpte170m (`WriterSettings::colorTransfer` to change it).
* "Right-click your clip, select Source Settings, click the Color Encoding tab and click Color Management Settings"
  (p.542): the tab's button opens the setting (`MainWindow::openColorManagementSetting`) and fills the drop-down again.
* "You can import CLF (Common LUT Format) files" (p.542): a `.clf` file is read through OpenColorIO's FileTransform
  (`detail::ocioBakeFile`) and kept as a 33-point cube (ours); without OpenColorIO in the build it is refused with that
  reason. The fuzz library refuses CLF files.

## Display Color Space (p.544, p.559; 8.5e1)

"Media Composer supports Rec. 709, BT/Rec. 2020 and DCI-P3 color spaces. The source and record viewers also support these
colors if your monitor has been calibrated for them ... Right-click in the desired viewer (source /record), select
Display Color Space, and choose from the list" (p.544).

* Each monitor's right-click menu has Display Color Space with Rec.709, Rec.2020 and DCI-P3 (`Composer::
  setDisplayColorSpace`), a choice of each monitor's like Show Captions. Ours: until one is chosen, the one the project's
  colour space is shown in (`media::displaySpaceOfProject`); not kept between sessions.
* The picture, in the project's colour space, is converted for the display as it is drawn (`Composer::displayed`,
  `media::toDisplayColorSpace` in `src/media/display_color.*`); the scopes, renders and exports are not. Ours, as the guide
  gives no numbers: Rec.709 and Rec.2020 displays are BT.1886 (gamma 2.4) with a D65 white, DCI-P3 is gamma 2.6 with the
  DCI white, the primaries mapped through XYZ with a Bradford adaptation to the display's white; PQ and HLG pictures are
  shown with their 203 cd/m² reference white (BT.2408) at the display's white and brighter values clipped; 601 projects are
  shown as Rec.709.
* ACES projects' pictures are not converted until their output transforms (8.5e4; "Select Display Color Space and choose
  the ACES color space", p.559).

## The proxy mode (p.526-527, p.549-550; 8.5e2)

"Set the proxy mode for the Timeline to 1/4 or 1/16: Effectively, this reduces the number of pixels to be processed as you
can play your sequence at 1/4 or 1/16 of the current project resolution" (p.526); "Select File > Settings, click the
Format tab, click Proxy and choose the appropriate setting" (p.527).

* The Format tab's Proxy menu (`formatProxy`): Off, 1/4 and 1/16 (the texts are ours), only in high-resolution projects
  ("The proxy mode is only available for high-resolution projects", p.526) and only while the smaller raster stays at
  least 256 x 120 ("in a 960x540 project, only 1/4-proxy (480x270) will be available", p.65, p.355;
  `model::proxyModes`). 1/4 is half the width and height, 1/16 a quarter (`model::proxyRaster`). The mode is saved in the
  project file (`ProjectSettings::proxy`, "proxy"; `MainWindow::setProxyMode`); a file that cannot be written says why.
* "During playback, each frame in the sequence is transcoded on-the-fly (no files created) based on the proxy mode"
  (p.526): the monitors' pictures are composed at most at the proxy raster and scaled up to the monitor.
* "When you render any effects applied on the sequence, however, the application creates new media at this resolution"
  (p.526): renders are made at the proxy raster (`mediaCreation("render")`) and the mode is part of the render key
  (`model::setRenderProxy`), so a render made at another mode is offline. "Should you switch back to that proxy mode, the
  rendered proxy media will still be available" (p.527): a render replacing one of another mode keeps it in
  `EffectRender::kept` (the latest of each mode), and `model::renderOf` returns it while its key matches.
* Transcode's "Project Dimensions: Transcodes the media based on the project size and the proxy mode setting" (p.527):
  `mediaCreation("transcode")` takes the proxy raster.
* Ours: exports keep the project's raster ("When you want to output/export your sequence, you need to turn off the proxy
  mode", p.527; unrendered effects are composed at full size, renders of a proxy mode are scaled up). The green dot of
  resized clips ("Any clips that are resized will have a spatial adapter applied", p.527) is not shown: the whole picture
  is made smaller, not each clip.

## Mask margins and Target Mask (p.550-552, p.900, p.1161, p.1175, p.1191; 8.5e3)

"You can specify mask margins on the output frame to view a master with a different aspect ratio than the project
setting ... This is for viewing purposes only" (p.550).

* The Format tab's Mask Margins button opens the Target Settings dialog (`src/ui/target_settings_dialog.*`, p.551): a
  Mask Margins group with the "Mask margins preset" menu (4:3, 9:16, 16:9, 1:1, 1.32:1, 1.37:1, 1.66:1, 1.75:1, 1.85:1,
  1.90:1, 2:1, 2.33:1, 2.35:1, 2.37:1, 2.39:1 and Custom..., as the guide's picture lists them) and top, bottom, left and
  right percentages, with OK, Cancel and Apply. A preset fills the percentages: a wider shape than the picture's is
  letterboxed, a narrower one pillarboxed, centred (`model::maskForPreset`, ours). A percentage typed by hand makes it
  Custom... (ours). The margins are the project's (`ProjectSettings::mask`, "maskMargins" in the project file;
  `MainWindow::setMaskMargins`); a file that cannot be written says why.
* "Right-click in the source or record viewer and select Target Mask" (p.552): No Mask, Mix to White, Mix to Black and
  Black Mask (`Composer::setTargetMask`, `media::paintTargetMask`). The Composer setting's new Viewer tab has Source and
  Record Monitor Target Mask (p.1161), which set them as the setting is applied; the right-click choice lasts until then
  (ours). Mix to White and Mix to Black are half white and half black (ours: "translucent", p.1161).
* The Full Screen Playback setting's Target Mask (p.1191) draws the same over the full screen picture.
* Export's Enable Mask Regions (p.1175: "you can export your sequence with mask margins burned in", p.900) blacks the
  masked region of exported movie frames (`media::burnMaskMargins`).
* Left: the Output Tool's Target Mask (p.552, with the hardware modules), Export's "Mask Margins active area" source
  raster (p.894), the Viewer tab's Resize Monitor to Image and its Color Space options (p.1161: Project, Rec709, Rec709
  (full range), SRGB, DCI P3, Rec2020, ACESproxy; with 8.5e4).

## Tests

* `tst_color_adapter`: the encoding with its column and JSON, Auto, bypass; segments' adapters, Refresh Sequence and
  Remove Color Adapters; each built-in transformation's values, their order, a registered one.
* `tst_frameflex_ui colorAdapters`: the Source monitor's and the Record monitor's pictures, Refresh Sequence > Color
  Adapters, Remove Color Adapters and its undo; the Color Encoding tab's Auto, Add, reorder, Delete, viewer, Bypass,
  Revert and Apply onto the clip.
* `tst_color_adapter cdl` and `lutFiles`: the CDL's name and formulas; each LUT format's table, the refused products and
  a broken file, installing, the External names and a folder's failures. `tst_frameflex_ui colorAdapters`: the clip's
  CDL in the drop-down.
* `tst_color_adapter clfAndTransfers`: a CLF matrix through OpenColorIO (or the refusal without it), the transfers'
  colour spaces. `tst_frameflex_ui colorAdapters`: linking a linear file with and without Insert color transformations,
  Use CDL values with Auto, the Color Management Settings button, Select LUT file and its refusal. `tst_settings_window`:
  the setting in the User list.
* `tst_color_adapter displayColorSpace`: white and black kept, BT.2087's 709 to 2020 matrix, PQ's and HLG's reference
  white, alpha kept. `tst_composer_display displayColorSpace`: the menu, each monitor's choice, the converted picture.
* `tst_effect_render proxyModes`: the key per mode, renders kept and found again, through JSON, removed. `tst_project_store
  proxyModes`: the modes per format, the rasters, the project file. `tst_render_ui proxyMode`: the Format tab's menu,
  renders at 1024 and 512 wide, offline and back, Transcode's raster.
* `tst_project_store maskMargins`: the presets, letterbox and pillarbox margins, the masked area, the project file.
  `tst_composer_display targetMask`: the Target Mask menu, Black Mask and Mix to White over the picture, the burn-in.
  `tst_render_ui proxyMode`: the Format tab's Mask Margins button and dialog, the Viewer tab's Record Monitor Target Mask.
* `tst_media transcodeColorEncoding`: Transcode with and without Color encoding: the media's colours, the new clip's
  encoding and the segment's adapter.
