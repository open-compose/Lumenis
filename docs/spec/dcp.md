# Digital Cinema Packages (Phase 7.5)

The Editing Guide names "Output to DCP requirements for theatrical releases" among
the outputs of a high-resolution master (p.519) and lists the DCI-P3 project
formats (p.536), but documents no DCP export. Everything here is ours, on SMPTE's
standards (ST 429-2 to 429-9), and on the watch list.

## Export As DCP (the Export setting's DCP tab)

* **Export As > DCP**, with a **DCP** tab: Title (empty: the sequence's name),
  Content Kind (Feature, Trailer, Short, Teaser, Advertisement, Test), Container
  (Flat 1998 x 1080, Scope 2048 x 858, Full 2048 x 1080), 4K (twice each), Sound
  (5.1 or Stereo) and Issuer (empty: Cutthulhu). The options cite p.519.
* The file chosen in the Export As dialog names the package: `Reel.dcp` makes the
  folder `Reel` beside it, which must not exist yet. Use Marks and Use Selected
  Tracks act as for movies (7.1a).

## The package (`media::writeDcp`, `src/media/dcp.*`)

* **Picture:** each frame letterboxed into the container and encoded as a JPEG
  2000 codestream in 12-bit X'Y'Z' by FFmpeg's OpenJPEG encoder (FFmpeg's own RGB
  to X'Y'Z' conversion), with the DCI cinema profile (`cinema2k`/`cinema4k`) at 24
  and 48 frames a second. The DCP's rate is the nearest of 24, 25, 30, 48, 50 and
  60 (`media::dcpFrameRate`): a 23.976 sequence plays at 24. It needs FFmpeg built with OpenJPEG
  (`media::dcpEncoderAvailable`): FFmpeg's own JPEG 2000 encoder cannot write X'Y'Z', so without it the export
  says so.
* **Sound:** the mix as 24-bit 48 kHz WAVE in 6 channels (5.1: L, R, C, LFE, Ls,
  Rs) or 2; a stereo mix leaves C, LFE and the surrounds silent.
* **Track files:** asdcplib's `asdcp-wrap` (BSD-3, run as a tool, not linked)
  wraps the codestreams and the WAVE as SMPTE MXF (`-L`), with the asset UUIDs we
  give (`-a`), the rate (`-p`) and, for 5.1, `-l 5.1`. FFmpeg's OP-Atom muxer takes
  only mono sound, so without `asdcp-wrap` there is no DCP export and the Console
  says so (BUILDING.md, "DCP").
* **Documents:** the Composition Playlist `cpl_<id>.xml` (ST 429-7: one reel with
  MainPicture and MainSound, ScreenAspectRatio as the container's width and
  height), the Packing List `pkl_<id>.xml` (429-8: SHA-1 hashes in base64, sizes,
  types), `ASSETMAP.xml` and `VOLINDEX.xml` (429-9). One reel; no encryption, KDMs,
  subtitles or Interop DCPs.

Not yet: validation by a DCP checker (ROADMAP Phase 7 gate 1: ClairMeta, which
needs asdcplib too); IMF packages (p.333-340) are docs/spec/imf.md.

Tests: `tst_dcp` (sizes, rates, the documents, the package through a stand-in
`asdcp-wrap`), `tst_movie_export_ui exportDcp`.
