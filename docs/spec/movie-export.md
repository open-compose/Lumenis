# Movie, still and audio export (Phase 7.1a-b)

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

## Stills, DPX, OpenEXR and audio files (7.1b)

Export As **Graphic**, **DPX**, **OpenEXR** and **Audio** go through the same
picture and sound as a movie (`MainWindow::exportStills`,
`media::writeStill`, `media::exportAudioFile`).

* **Graphic** (Export Settings: Graphic, p.1185-1187): BMP, JPEG (Quality),
  PICT (our writer, `src/media/pict.*`), PNG and TIFF (8 or 16 bits);
  Cineon and Photoshop later. Width x Height with Size to Fit (stretched)
  or Crop/Pad (1:1 centred on black); Color Levels "Keep as Legal Range"
  squeezes RGB into 16-235, "Scale from Legal to Full Range" keeps our
  full-range pictures as they are.
  * Without **Sequential Files** one frame: the IN with Use Marks, else
    the position of the monitor showing the item (p.868).
  * With it, every frame of the range as numbered files,
    `<name>.<7 digits>.<suffix>` counted from 0 (ours).
  * **Markers only**: the frames with the item's markers, named by their
    comments with **Use Comments** (p.1186), else by frame number; Topmost
    Image is how our pictures are made anyway (every track composed).
* **DPX** (p.890-891): Color Depth RGB 8, 10 or 16 bit; **OpenEXR** (p.900):
  16 or 32 bit floating point. Both write the range as an image sequence;
  OpenEXR writes one frame as one file, DPX numbers it too (ours). DPX's
  options (7.2a): Create subfolder name; Start Frame, or Convert Timecode
  to Frames (the frame's timecode counted in frames); Digits in File Name
  (1-9, more when the count needs them); Include in Header (Input Device),
  at most 32 characters at offset 1556; FPS (the sequence's rate by
  default, ours) in the film and television headers, which we add after
  FFmpeg's 1664-byte header (`media::setDpxHeaderInfo`, SMPTE 268M
  offsets); Levels Video (64-940) or Full Range (the default, ours). Set
  to highest scaling quality later (we always scale smoothly). OpenEXR's values are the display-referred pictures, not
  converted to scene linear (ours; the custom Color Space, p.897, later).
* **Audio** (Export Settings: Audio, p.1185): WAVE or AIFF-C, Mono or
  Stereo, 32, 44.1, 48, 88.2 or 96 kHz, 16 or 24 bit.

## MXF OP1a (7.2a)

Export As **MXF OP1a**, **XDCAM MXF OP1a** and **MXF OP1a (legacy)**
(p.886-890, p.1174) write OP1a files through FFmpeg's `mxf` muxer
(`MovieContainer::Mxf`, `Xdcam`, `MxfLegacy`), with the Export setting's
"MXF OP1a" tab:

* **Video Compression** (MXF OP1a): Avid DNxHR, Apple ProRes (p.888,
  p.904), XDCAM-50/35/25/17 (p.884, p.887) and Panasonic AVC Long-GOP
  G6/G12/G25/G50 (p.887-888). **Video Format** (XDCAM MXF OP1a): the XDCAM
  ones only. MXF OP1a (legacy) is JPEG 2000 only ("available when exporting
  J2K", p.1174).
* Ours, where the guide names only the resolutions: XDCAM-50 is MPEG-2
  4:2:2 at a constant 50 Mb/s, XDCAM-35 4:2:0 at up to 35 Mb/s, XDCAM-25
  4:2:0 at 25 Mb/s and XDCAM-17 at up to 17.5 Mb/s, both at 1440 x 1080
  with the picture squeezed in; a GOP of 12 with two B frames. AVC
  Long-GOP G6 and G12 are H.264 4:2:0 8-bit, G25 and G50 4:2:2 10-bit, at
  6, 12, 25 and 50 Mb/s, a GOP of 12. JPEG 2000 is FFmpeg's encoder, 4:2:2
  10-bit.
* **Audio Format** Mono, Stereo, 5.1 or 7.1 as one sound track of that many
  channels (ours; "Up to 16 channels", p.888, later with more than the mix's
  formats); **Audio Sample Rate** 48 kHz; **Bit Depth** 16 or 24.
* The picture is the project's raster, legal range; the item's timecode is
  embedded.

## Not yet

* QuickTime Movie's Same as Source and QuickTime Reference (p.879-880),
  Display Aspect Ratio, File Field Order and Create Preview (p.1179-1180).
* MP4's Frame Rate (p.896), Direct Out, the custom Color Space (p.897),
  Tier (p.899) and Render Unrendered Effects (p.1175).
* Export by dragging to a folder (p.869): with the Send To templates in 7.1c.
* Export's "Mask Margins active area" source raster (p.894). Enable Mask Regions burns the mask margins into movie and
  still frames (8.5e3), and ACES projects' frames go to the Output Color Space (8.5e4b2; docs/spec/color-adapters.md).
* File Field Order (p.1186), Cineon, Photoshop, PNG Interlaced, JPEG Progressive.

## Tests

* `tst_movie_export`: the scalings, the compression table and the build's
  codecs, MOV with PNG and 24-bit PCM read back (codec, size, frames,
  colours, sound, timecode), another raster letterboxed with 16-bit sound
  at 44.1 kHz, video only and audio only, MP4 with H.264 (or AV1) and AAC
  (or Opus), DNxHR refused in an MP4, full range, a cancel.
* `tst_movie_export_ui`: a sequence exported from the bin, its frames and
  sound read back; Use Marks with the Record monitor's marks; a master clip
  at its own timecode, video only; PNG refused in an MP4. 7.1b: a Graphic
  still at the Record monitor's position, Markers only named by comments,
  a DPX sequence, a WAVE file.
* `tst_movie_export` (7.1b): every still format and depth read back, PICT,
  Crop/Pad, legal range, sequence names, WAVE 24-bit and AIFF-C 16-bit mono
  at 96 kHz.
* 7.2a: `tst_movie_export mxfOp1a` (the OP1a label, XDCAM-25's raster and
  timecode, DNxHR refused in XDCAM, JPEG 2000 in the legacy file, DNxHR in
  OP1a), `stills` (Digits in File Name, the DPX header's input device and
  frame rate read back); `tst_movie_export_ui` (XDCAM-50 from Export to
  File, a DPX sequence in a subfolder from Start Frame 20 with 3 digits).
