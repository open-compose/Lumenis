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

## Left for 8.5c2

The Source Settings dialog's Color Encoding tab (Source color space, Auto, the transformation list with Add, Delete and
dragging, Bypass all color transformations, Revert, the viewer) and Transcode's "Apply source transformations > Color
encoding" (p.544).

## Tests

* `tst_color_adapter`: the encoding with its column and JSON, Auto, bypass; segments' adapters, Refresh Sequence and
  Remove Color Adapters; each built-in transformation's values, their order, a registered one.
* `tst_frameflex_ui colorAdapters`: the Source monitor's and the Record monitor's pictures, Refresh Sequence > Color
  Adapters, Remove Color Adapters and its undo.
