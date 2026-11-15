# The release builds' codecs (Phase 12.2a)

Official releases are built with `LUMENIS_PATENT_CODECS` off (ROADMAP 10.7; the owner's answer on patent codecs,
2026-11-12: "releases follow US rules by default"). The owner's decision of 2026-11-15 says what that means:

* **DNxHD and DNxHR are allowed** in releases (SMPTE VC-3). They are not among the codecs awaiting review any more
  (`media::isPatentCodec`), so managed media, its default Avid DNx HQ and the DNxHD bit-rate resolutions work in every
  build.
* **H.264 and HEVC are the operating systems'.** FFmpeg's own implementations of the codecs awaiting review (H.264,
  HEVC, ProRes, AAC, AC-3, MPEG-4 Part 2, VC-1; libx264 and libx265) are not used. Their streams are decoded and encoded
  by what the operating system or a driver licenses, as the licensed-technology rule allows (CLAUDE.md, docs/EXTENSIONS.md):
  * **Decoding** (`openDecoder` in media.cpp): a video stream FFmpeg's own decoder may not take is decoded on the OS's
    hardware decoder through FFmpeg's hwaccels: VideoToolbox on macOS, D3D11VA (else DXVA2) on Windows, VA-API on
    Linux. Its `get_format` takes only the hardware format, so FFmpeg never falls back to decoding it itself; the
    frames are copied off the device as they are received (`takeFromDevice`), and everything downstream reads them as
    before. Audio: AudioToolbox's decoders on macOS (`aac_at`). Where there is no such decoder (no GPU or driver, or
    Windows' AAC) the stream is probed and shown but not decoded, and the reason says so.
  * **Encoding:** the OS's encoders, which this FFmpeg has: VideoToolbox's H.264, HEVC and ProRes and AudioToolbox's
    AAC on macOS, Media Foundation's H.264, HEVC and AAC on Windows (`media::osEncoderFor`). Movie export picks them in
    place of libx264 and libx265 (`media::movieEncoder`), managed ProRes takes VideoToolbox's (as before), and MP4's
    sound is the OS's AAC, else Opus. On Linux there is no OS encoder (ours: VA-API's take frames on the device), so
    H.264 and HEVC export is not offered there.
  * `media::isOsCodec` names the OS's and drivers' implementations by FFmpeg's suffixes (`_videotoolbox`, `_at`,
    `_mf`, `_vaapi`, `_qsv`, `_nvenc`, `_cuvid`, `_amf`); `encoderAvailable` takes those, and FFmpeg's own only for a
    codec `codecAllowed`.
* **Probing** still reads every stream; `StreamInfo::decodable` follows `media::decodingAvailable`, which asks whether
  the OS's device can be made here (tried once).
* `LUMENIS_PATENT_CODECS_OFF=1` gives the release policy at run time in a development build, for tests and for anyone
  checking how a release behaves.

## Watch

* FFmpeg's hardware decoding goes through its own `h264`, `hevc` and `prores` decoders' front ends: FFmpeg reads the
  stream's headers and the operating system decodes the pictures. So a release's FFmpeg keeps those decoders compiled
  in (FFmpeg's own decoding of them stays refused by the policy above), unlike EXTENSIONS.md's "decoders and encoders
  marked † left out"; its encoders (libx264, libx265, FFmpeg's AAC and ProRes) are left out. For counsel to confirm
  with the rest of the † list.

* Hardware decoding differs between machines (a GPU without H.264 4:2:2 or 10-bit decoding refuses those streams); the
  message names the operating system as the reason.
* Linux releases are the AppImage on the distribution's FFmpeg (the owner's answer): VA-API decoding where the
  distribution's FFmpeg and the GPU's driver have it.

## Tests

`tst_media releaseCodecPolicy` (with `LUMENIS_PATENT_CODECS_OFF`): DNx managed media made and read, FFmpeg's own H.264
and libx264 refused, an H.264 movie decoded by the operating system (the Mac runner's VideoToolbox) or refused with the
reason, movie export's H.264 through the OS's encoder or not offered.
