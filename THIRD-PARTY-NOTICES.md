# Third-party components

The application includes or links the following components. Their licences
are compatible with this project's GPL-3.0-or-later licence and allow
commercial distribution.

| Component | Version | Licence | Use |
|---|---|---|---|
| [Qt](https://www.qt.io) | 6.8 (6.4+ on Linux distributions) | LGPL-3.0, linked dynamically | GUI toolkit |
| [Inter](https://github.com/rsms/inter) | 4.1 | SIL Open Font License 1.1 (`src/ui/resources/fonts/Inter-LICENSE.txt`) | Interface font, embedded as a resource |
| [FFmpeg](https://ffmpeg.org) | 6.0 or later | LGPL-2.1-or-later (`packaging/licenses/FFmpeg-LGPL-2.1.txt`), linked dynamically | Reading and writing media (Phase 2) |
| [miniaudio](https://miniaud.io) | 0.11.25 | Public domain (Unlicense) or MIT-0, at the user's choice (`packaging/licenses/miniaudio-LICENSE.txt`); built in from `third_party/miniaudio` | Sound output for playback (Phase 2.3) |

Qt's source is available from https://download.qt.io. Because Qt is linked
dynamically, users can replace the Qt libraries that ship with the
application with their own build.

FFmpeg's source is available from https://ffmpeg.org. Development builds
use the platform's FFmpeg:
* the Linux distribution's packages;
* Homebrew on macOS;
* an LGPL shared build from BtbN/FFmpeg-Builds on Windows
  (`scripts/get-ffmpeg.ps1`).

Some of these are built with GPL parts. That is compatible with this
project's GPL-3.0-or-later licence. Official releases will ship an LGPL-only
FFmpeg without the codecs awaiting legal review (docs/EXTENSIONS.md).

miniaudio is a single header kept unchanged in `third_party/miniaudio`
(SHA-256 `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`).
It loads each platform's sound libraries (Core Audio, WASAPI, PulseAudio or
ALSA) at run time, so nothing needs installing to build or run.

## Prior art for interchange

Cutthulhu's AAF and bin readers and writers (`src/interop`, ROADMAP Phase 3)
are written after the public specifications, with Mark Reid's
[pyaaf2](https://github.com/markreidvfx/pyaaf2) and
[pyavb](https://github.com/markreidvfx/pyavb), both MIT-licensed, as the
reference implementations. Where their design or tables are followed (the
built-in AAF model, `src/interop/aaf_model.inc`, is made from pyaaf2's, and
the bin reader `src/interop/avb.cpp` and writer `src/interop/avb_writer.cpp`
port pyavb's class layouts), the MIT
licence applies to that part (`packaging/licenses/pyaaf2-LICENSE.txt` and
`packaging/licenses/pyavb-LICENSE.txt`). Their test files are the
first interchange corpus. They are downloaded for testing
(`scripts/interop-corpus.py`) and not distributed.

The EDL and shot log readers and writers (`src/interop/edl.cpp`,
`src/interop/ale.cpp`, Phase 3.6a) follow the published CMX 3600 format and
the Editing Guide's Avid Log Specifications, with the behaviour of
OpenTimelineIO's [CMX 3600](https://github.com/OpenTimelineIO/otio-cmx3600-adapter)
and [ALE](https://github.com/OpenTimelineIO/otio-ale-adapter) adapters
(Apache-2.0, Copyright Contributors to the OpenTimelineIO project) as prior
art: how a transition's two lines relate, `M2` and `LOC` lines, and logs as
other programs write them. No code of theirs is copied. Their sample files
are downloaded for testing (`scripts/interop-corpus.py`) and not
distributed.

The OpenTimelineIO reader and writer (`src/interop/otio.cpp`, Phase 3.6b)
follow [OpenTimelineIO](https://github.com/AcademySoftwareFoundation/OpenTimelineIO)'s
published file format and schemas (Apache-2.0, Copyright Contributors to the
OpenTimelineIO project). No code of theirs is copied. Its sample files are
downloaded for testing (`scripts/interop-corpus.py`) and not distributed;
its Python package is used only by the checks (`scripts/otio-check.py`).

The FCP 7 XML reader and writer (`src/interop/fcp_xml.cpp`, Phase 3.6c)
follow Apple's published Final Cut Pro XML Interchange Format, with the
behaviour of OpenTimelineIO's [FCP 7 XML adapter](https://github.com/OpenTimelineIO/otio-fcp-adapter)
(Apache-2.0, Copyright Contributors to the OpenTimelineIO project) as prior
art: how Premiere Pro and Hiero write the format and how it maps to
OpenTimelineIO. No code of it is copied. Its sample files are downloaded for
testing (`scripts/interop-corpus.py`) and not distributed; the adapter itself
is used only by the checks.

## Development tools

[actionlint](https://github.com/rhysd/actionlint) (MIT, Copyright rhysd),
installed through the [actionlint-py](https://github.com/Mateusz-Grzelinski/actionlint-py)
package (MIT), checks the CI workflows (`scripts/check-workflows.py`). It is
used only by the checks and not distributed.

When a component is added, also add it to this table and install its licence
text (see `cmake/Packaging.cmake`).
