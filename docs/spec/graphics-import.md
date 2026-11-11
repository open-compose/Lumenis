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

## Not yet

* Multilayer Photoshop files (p.218-221: Sequence of Layers, Flattened Image, Select Layers): they need a reader of
  Photoshop's layers; ROADMAP 9.2e. A layered file imports flattened, as FFmpeg reads it.
* The Link setting's Alpha Channel (p.1204) for linked movies with alpha (p.318: "they appear in your bin as a Matte Key
  effect").
* Batch Import of Matte Key clips (docs/spec/batch-import.md).

## Tests

`tst_graphics_import`: the fill and matte (Invert, Do not invert, Dilate Fill), which files have alpha, a PNG as a Matte
Key clip keyed in the Source monitor and shown by the Frame view, Ignore as a master clip, PICT files with and without
alpha, a movie's alpha frames.
