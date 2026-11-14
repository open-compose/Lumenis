# Media format support (Phase 2 specification)

**Source.** *Avid Supported Video File Formats* (Avid, dated 4/7/2021,
revised 2024; 11 pages). It is publicly available at
`https://resources.avid.com/SupportFiles/attach/Avid_Supported_Video_File_Formats.pdf`.
It lists the formats Media Composer links (AMA), imports and exports. The
Editing Guide's ch. 35 gives the resolutions Media Composer creates (DNx,
ProRes, XAVC, …), and ch. 11, ch. 9 and ch. 25 cover linking, import and
export.

**Route.** Each row says how Cutthulhu will support the format, following
`docs/EXTENSIONS.md`:
* **OSS:** bundled open source.
* **OS:** operating-system codec.
* **Hook:** a licensed or vendor module.
* **†:** the codec needs legal review before an official build ships it.

Phase 2's decode corpus (ROADMAP Phase 2 gate 1) takes one sample of each
row.

## Camera and industry formats (source p.4–7)

| Format | Container | Codecs / sub-formats | Media Composer support | Cutthulhu route |
|---|---|---|---|---|
| ARRI Alexa | QuickTime | ProRes, DNxHD | Link | ProRes: OS on macOS; FFmpeg decode elsewhere †. DNxHD: OSS (FFmpeg) † |
| ARRIRAW | ARRI | ARRIRAW (4K, 2K) | Link (vendor plug-in) | Hook: ARRI SDK, if its terms allow |
| ProRes RAW | QuickTime | ProRes RAW | Link | Hook: Apple framework on macOS only |
| AVCHD (Canon, JVC, Panasonic AVCCAM, Sony NXCAM) | MTS (MPEG-TS) | H.264 | Link | OS H.264 decoder; or a user-installed FFmpeg † |
| AVC-Intra (Panasonic P2) | MXF OP1a | AVC-Intra 50/100 | Link, native write-back | MXF: OSS (bmx). Codec: OS H.264 † |
| AVC-LongG (Panasonic P2) | MXF OP1a | AVC Long GOP 12/25/50 (HD, UHD, 2K, 4K) | Link | as AVC-Intra |
| CINE (Phantom) | .cine | Phantom raw | Link (plug-in) | Hook: vendor SDK |
| DVCPRO (Panasonic P2) | MXF OP-Atom | DVCPRO HD (DV100), DV50, DV25 | Link, native write-back | OSS (FFmpeg DV decoders; the DV patents have expired) |
| HDCAM SR (Sony SR Lite/SQ) | MXF | MPEG-4 SStP | Link | OSS (FFmpeg MPEG-4 SStP) † |
| MXF (third-party) | MXF OP1a | DNxHD / VC-3 | Link | OSS (bmx + FFmpeg) † |
| QuickTime | MOV | DV25/50/100, ProRes 422 / HQ / LT / Proxy / 444, AVC-Intra, MPEG-2 I-frame, XDCAM HD / HD422, DNxHD, DNxHR, MP4 (4K); any other codec is transcode-only | Link; native codecs consolidate and transcode without re-encoding | OSS (FFmpeg demux); codecs as listed elsewhere |
| RED (EPIC and others) | R3D | REDCODE 2K–8K | Link (plug-in) | Hook: RED SDK |
| XAVC Intra / Long GOP / S (Sony) | MXF OP1a; MP4 for XAVC-S | CBG/VBR Class 100/300/480; Long GOP 25/35/50; MPEG-4 | Link | OS H.264 decoder † |
| XDCAM IMX (Sony D10) | MXF OP1a | MPEG-2 I-frame 30/40/50 | Link | OSS (FFmpeg MPEG-2; patents expired) |
| XDCAM EX / HD (Sony) | MXF OP1a; MP4 | XDCAM EX 35, XDCAM HD 35/50, HD420, HD422 (MPEG-2 Long GOP) | Link, native write-back | OSS (FFmpeg MPEG-2) |
| XDCAM Proxy | MP4 | MPEG-4 | Link | OSS (FFmpeg) |
| XF (Canon) | MXF OP1a | MPEG-2 Long GOP HD | Link | OSS (FFmpeg MPEG-2) |
| AirSpeed / Interplay columns | — | — | Avid server products | Not applicable; see `docs/EXTENSIONS.md` (shared storage) |

## Application and device-generated formats (source p.8–10)

| Format | Notes from the source | Cutthulhu route |
|---|---|---|
| AVI | DV25 | OSS (FFmpeg) |
| DPX | image sequence | OSS (OpenImageIO or FFmpeg) |
| HDV (.m2t) | MPEG-2 Long GOP | OSS (FFmpeg) |
| AVCHD (.mts) | H.264 | as above |
| MXF OP-Atom / OP1a | D10, VC-3/DNxHD, MPEG-2, DV50, HDV, MPEG-2 I-frame 40/50, MPEG-2 HD Long GOP 50, MPEG-2 HD I-frame 100, DV25, DVCAM, XDCAM HD | OSS (bmx + FFmpeg) |
| QuickTime / QuickTime reference | all codecs; reference movies link only | OSS (FFmpeg). Reference movies: resolve the referenced files |
| Windows Media | VC-1; Windows only | OS (Media Foundation) on Windows only |
| Still images | BMP, Alias PIX, Chyron, Cineon, ERIMovie, Framestore, IFF, JPEG, OMFI, PCX, Photoshop PSD, PICT, Pixar, PNG, QuickTime stills, Rendition, SGI, Softimage, SunRaster, TARGA, TIFF, Wavefront, X Windows, YUV | OSS (OpenImageIO for the common ones: BMP, Cineon, DPX, IFF, JPEG, PNG, PSD, SGI, Softimage PIC, TARGA, TIFF, PCX). The obscure legacy formats (Chyron, ERIMovie, Framestore, Rendition, Wavefront, Pixar, Alias PIX) need a feasibility check |
| AAF, AFE, OMF 1.0 / 2.0 | interchange; import and export | OSS (our own AAF layer, after pyaaf2). AFE and OMF: Phase 3, feasibility to check |

## Stereoscopic 3D (source p.11)

* Frame-compatible (FC) and full-frame (FF) stereo media in uncompressed
  YUV 4:2:2 8/10-bit, RGB 10-bit, DNx444, DNxHD and AVC-Intra. Some
  hardware needs two-pass renders.
* Each eye is linked, imported or exported separately, then grouped into an
  S3D clip.
* Hardware columns (Nitris DX, Mojo DX) refer to Avid I/O hardware. They are
  not applicable, apart from the general video I/O hook.

## Created media (Editing Guide ch. 35)

The project format tables (p.1241–1275) list the resolutions Media Composer
creates. Cutthulhu's creation targets, in priority order:

1. **Avid DNx** (DNxHD, DNxHR LB/SQ/HQ/HQX/444, DNxUncompressed) in MXF
   OP-Atom: FFmpeg's VC-3 encoder † with bmx for MXF.
2. **ProRes** (Proxy, LT, 422, HQ, 4444, 4444 XQ), done (ROADMAP 11.1a). On
   macOS through VideoToolbox, as the guide says media creation for ProRes is
   "macOS only" (p.1240). Elsewhere through FFmpeg's `prores_ks` in builds with
   `LUMENIS_PATENT_CODECS` only (development and tests), until legal review.
3. **XAVC, AVC-Intra and other H.264 families:** OS encoders, where the OS
   offers them †.
4. **JPEG 2000:** OpenJPEG.
