# Graphics with alpha and PICT files on import (9.2b)

Editing Guide p.217-218 "Photoshop Graphics Import" (single-layer graphics), p.1196 (the Import setting's Image tab:
Alpha Channel, Dilate Fill, Alpha Encoding, Frame Import Duration), p.1231 "Import Specifications for Supported Graphics
File Formats", p.989 (alpha in graphics), FX Guide p.474 (Matte Key). Code: `src/media/graphic_import.*`,
`src/model/matte_key_clip.*`, `matteKeyClipLayer` in `src/media/compositor.cpp`, `src/ui/main_window_graphics.cpp`.

## Alpha Channel (p.1196)

* **Invert on import (white = opaque):** the file's alpha is white where the picture is opaque, as graphics packages
  make it (p.989: "You need to invert the alpha channel of graphics on import").
* **Do not invert (black = opaque):** the file's alpha is already as Avid applications keep it.
* **Ignore:** "import an image that contains alpha channel transparency information as one opaque graphic. The imported
  graphic appears as a single master clip in the bin." Ours by default, as before 9.2b (the guide gives no default).
* With Invert or Do not invert, a file with alpha becomes a **Matte Key clip** ("If the graphic uses a transparent
  background or an alpha channel, Media Composer creates a matte key", p.218). A movie with alpha and sound imports
  "as a Matte Key clip and the audio is not included" (p.1196).
* Which files have alpha: PNG, TIFF, Photoshop (flattened), movies and image sequences whose pixel format has an alpha
  channel (FFmpeg's, `media::pixelFormatHasAlpha`), and PICT pictures with a pixel that is not opaque.
* **Dilate Fill** "bleeds the fill just a bit along the edges where transparent alpha meets non-transparent alpha" (ours:
  each transparent pixel next to a visible one takes their mean colour, `media::fillPicture`).
* **Alpha Encoding** (Uncompressed or RLE) is dimmed: our precomputes are the Media Creation setting's resolution.

## Matte Key clips (ours, after the Title Effect clip)

"Importing an image with alpha channel creates a matte key effect as a single frame" (p.1196), lasting the Frame Import
Duration; a movie's frames one for one. The clip is a bin item of kind Title (the Media Tool lists "Precompute Clips -
Titles and Matte Keys" together, p.346) whose composition is one segment: filler with the Matte Key effect and a nest of
the fill precompute (V1) and the matte precompute (V2). The precomputes are RenderedEffect clips in the same bin,
"<name>.fill" and "<name>.matte" (ours), hidden by the bin's default display. The matte holds the alpha as grey, white
opaque. The compositor keys the fill over what lies below with the effect's Foreground (Level, Invert Key, Show Alpha)
and Scaling, Position and Crop (`matteKeyClipLayer`). The Frame view shows the fill.

## PICT files (p.1231)

FFmpeg does not read PICT, so `.pic`, `.pct` and `.pict` files go through our reader (`media::readPict`, 6.9c2): with
alpha a Matte Key clip, otherwise a master clip of the picture lasting the Frame Import Duration.

## Multilayer Photoshop files (p.218-221; 9.2e1)

Our own reader of the layer and mask information section (`src/media/psd.*`, after Adobe's published "Adobe Photoshop
File Formats Specification"): PSD and PSB, RGB 8 or 16 bits a channel, grayscale and duotone ("Graphics must be RGB 8 or
16 bits, or grayscale", p.220); raw and PackBits channels (ZIP is refused); each layer's name (the Unicode "luni" one when
there is one), rectangle, opacity, visibility and transparency channel. Group markers ("lsct") and layers without pixels
(adjustment layers, "Adjustment Layer ... No") are left out; grouped layers are read as layers ("Import ignores layer
grouping and instead imports all layers, including grouped layers, as individual layers"). Masks, styles and blending
modes are not read (p.220: not supported).

A `.psd` file of more than one such layer asks, once for every such file of the import ("This selection applies to all
files you select for import", p.221), with the message box's Sequence of Layers, Flattened Image and Select Layers, and
Cancel (ours). Flattened Image and files of one layer import as any graphic, from the merged picture FFmpeg reads.
Select Layers opens the Select Layers dialog for each file: its layers top first, all selected (ours).

The chosen layers, bottom to top: a layer without transparency (Photoshop's background) is a master clip ("the graphic
uses a background image, so the system creates the background image as a master clip", p.219); the others, hidden ones
too ("Hidden layers are imported as matte keys", p.220), are Matte Key clips of the layer placed on the canvas, with its
Opacity as the Matte Key's Foreground Level ("Importing converts Opacity to Foreground level in the Matte Key effect").
Each clip is named after its layer. Then a sequence named after the file "with each layer on a separate video track"
(p.219), V1 the bottom layer, at most 24 tracks: "If you select more than 24 layers, Media Composer imports the
additional layers but does not include them in the sequence" (p.221).

Ours: a layer's transparency is always its matte; the Alpha Channel option's Invert and Do not invert apply to it, and
Ignore is taken as Invert (Photoshop's transparency is white = opaque), since p.219 says each layer imports "as an
individual matte key with alpha channel". The sequence carries the file's Source File and Source Path.

## Linked files with alpha (p.318, p.1204; 9.2e2)

"If any of the movies you linked to were QuickTime with Alpha channel files, they appear in your bin as a Matte Key
effect" (p.318). Any linked file whose first video stream has alpha (a still or a movie, `media::alphaSourceOf`) is
linked so, unless the Link setting's Alpha Channel is Ignore: "The imported graphic appears as a single master clip in
the bin" (p.1204). Invert on import, the setting's default, and Do not invert read the alpha as for imports.

No media is made, since the file stays linked. The linked clip is kept in the bin as "<name>.link" (ours), hidden like
a precompute, and the Matte Key clip's "Linked Alpha" says how to read it: the compositor takes the fill and the matte
from the clip's own frames as they play (`matteKeyClipLayer`). Linking into a monitor (loadFiles) links plain clips.

Ours: p.318 says "The Alpha Channel options in the Import Settings Image tab apply to the QuickTime linked file", while
the Link setting has its own Alpha Channel, which "Controls how Media Composer handles the alpha channel in linked
images" (p.1204); we follow the Link setting.

## Not yet

* Batch Import of Matte Key clips (docs/spec/batch-import.md).

## Tests

`tst_graphics_import`: the fill and matte (Invert, Do not invert, Dilate Fill), which files have alpha, a PNG as a Matte
Key clip keyed in the Source monitor and shown by the Frame view, Ignore as a master clip, PICT files with and without
alpha, a movie's alpha frames. `tst_psd_import`: the Photoshop reader with raw and PackBits channels and the modes it
refuses; Sequence of Layers, Select Layers, Cancel and Flattened Image; the 24-track limit. Fuzz target `psd`. `tst_graphics_import linking`: a linked PNG as a Matte Key clip keyed from its link, Do not invert,
Ignore.
