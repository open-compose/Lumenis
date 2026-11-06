# Movie export: MOV and MP4 (Phase 7.1a)

File > Output > Export to File writes the selected sequences and clips as
movies when the Export setting's Export As is **MOV**, **MP4** or
**QuickTime Movie** (Editing Guide p.868-869, "Exporting QuickTime Movies"
p.879-881, "MOV Export" and "MP4 Export" p.893-897, "Exporting H.265"
p.898-899, the QuickTime Movie options p.1179-1180).

## What is exported (`MainWindow::exportMovieFile`)

* **Which items:** each sequence or clip selected in the active bin. The
  first goes to the path chosen in the Export As dialog; the others beside
  it under their own names (ours). A clip is exported through a sequence of
  its own made by AutoSequence (`model::autoSequence`).
* **Use Marks** (p.868, p.1175): the IN and OUT of the monitor showing the
  item, the Record monitor before the Source monitor. "If you mark an IN
  point and no OUT point, Media Composer exports from the IN point to the
  end." An item in no monitor is exported whole (ours).
* **Use Selected Tracks** (p.1175): "the tracks that are enabled in the
  Timeline", for the sequence in the Record monitor; every track for other
  items (ours). The video tracks are the ones the Record monitor shows
  (`shownVideoTracks`). **Include Inactive Audio Tracks** (p.1175) adds the
  inactive audio tracks.
* **Picture:** each frame through the compositor (`media::composeImage`) at
  the project's raster, as the Record monitor shows it, effects and renders
  included.
* **Sound:** the selected audio tracks through the mixdown's mix
  (`media::MixdownRenderer`): "All current Pan and Level settings in the
  sequence are carried to the exported media" (p.863).
* **Timecode:** the item's start plus the range's start, embedded in the
  movie (p.881).

## The Export setting's MOV and MP4 tab

| Option | Values | Source |
|---|---|---|
| Export | Video and Audio, Video only, Audio only | p.880 |
| Compression | see below | p.895, p.899 |
| Bit Rate | Mbps for H.264, HEVC and AV1; Automatic | p.899 (Tier, bit rate sliders; ours: one value) |
| Image Size | the project raster or a preset | p.895 |
| Source Scaling | Stretch, Pillarbox/letterbox, Center crop, Center keep size | p.895-896 |
| Color Levels | 601/709 (Legal Range), RGB (Full Range) | p.881, p.897 |
| Audio Format | Mono, Stereo, 5.1, 7.1 (Direct Out later) | p.1179-1180 |
| Audio Compression | PCM, AAC | p.897 |
| Sample Rate | 48 kHz, 44.1 kHz | p.879, p.1178 |
| Bit Depth | 16 bit, 24 bit | p.1178 |

Compressions (`media::movieCompressions`; ours: the guide names the Codec
Family, Compression and Color Depth menus and HEVC's two profiles, not the
entries). One Compression menu names each family, since the settings
dialogs have no menus that depend on others:

| Compression | Encoder | Depth | MOV | MP4 |
|---|---|---|---|---|
| Avid DNxHR LB, SQ, HQ | dnxhd | 8 | yes | |
| Avid DNxHR HQX, 444 | dnxhd | 10 | yes | |
| Apple ProRes 422 Proxy, LT, 422, HQ, 4444 | prores_ks | 10 | yes | |
| H.264 | libx264 | 8 | yes | yes |
| H.265/HEVC Main Profile, Main 10 Profile | libx265 | 8, 10 | yes | yes |
| AV1 | libsvtav1 | 8 | yes | yes |
| Motion JPEG | mjpeg (always full range) | 8 | yes | |
| PNG | png | 8 | yes | |
| Uncompressed 8-bit, 10-bit 4:2:2 | rawvideo (2vuy), v210 | 8, 10 | yes | |

DNxHR, ProRes, H.264, HEVC and AAC wait for legal review
(`LUMENIS_PATENT_CODECS`, docs/EXTENSIONS.md); a build without them says
"... is not available in this build". `MediaWriter` checks the codec, not
only the encoder's name, so `libx264` counts as H.264. An MP4's sound is
AAC, or Opus in a build without AAC (ours). A compression that does not go
in an MP4 is refused with the reason in the Console.

## Writing (`src/media/movie_export.*`)

`media::exportMovie` fits each frame to the output raster
(`fitMoviePicture`), writes it, then the sound of that frame (resampled to
44.1 kHz through `Resampler` when asked), through `MediaWriter`. Sizes are
rounded up to even numbers for the chroma subsampled formats (ours). A
failure or a cancel in the progress dialog removes the file.

## Not yet

* QuickTime Movie's Same as Source and QuickTime Reference (p.879-880),
  Display Aspect Ratio, File Field Order and Create Preview (p.1179-1180).
* MP4's Frame Rate (p.896), Direct Out, the custom Color Space (p.897),
  Tier (p.899), Enable Mask Regions and Render Unrendered Effects (p.1175).
* Export by dragging to a folder (p.869): with the Send To templates in 7.1c.

## Tests

* `tst_movie_export`: the scalings, the compression table and the build's
  codecs, MOV with PNG and 24-bit PCM read back (codec, size, frames,
  colours, sound, timecode), another raster letterboxed with 16-bit sound
  at 44.1 kHz, video only and audio only, MP4 with H.264 (or AV1) and AAC
  (or Opus), DNxHR refused in an MP4, full range, a cancel.
* `tst_movie_export_ui`: a sequence exported from the bin, its frames and
  sound read back; Use Marks with the Record monitor's marks; a master clip
  at its own timecode, video only; PNG refused in an MP4.
