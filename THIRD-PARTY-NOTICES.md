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

When a component is added, also add it to this table and install its licence
text (see `cmake/Packaging.cmake`).
