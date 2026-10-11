# Extensions, licensed components and hooks

Many features depend on technology that is patented, licensed, or owned by
someone else: codecs, plug-in formats, speech recognition, storage clients. We
handle all of these the same way.

1. **Open source first.** Where a legal open-source implementation exists, we
   bundle it. Its licence must be compatible with GPLv3 and with commercial
   distribution.
2. **Then what the OS (or a driver) already licenses.** If the OS or a vendor
   driver ships a properly licensed implementation (VideoToolbox, Media
   Foundation, VA-API, a capture card driver), we call it. We do not
   redistribute it.
3. **Otherwise, a hook.** Anything that needs a licence or SDK we don't have
   yet gets an extension point now. Support can then be added later as a
   separately built module, if a deal is made, without changing the
   application.

## How the hooks work

* **Providers** (`src/ext/include/cutthulhu/ext/provider.h`). Every capability
  is an interface derived from `Provider`. Each provider declares:
  * its `Category`;
  * its `LicenseTier`: `OpenSource`, `OperatingSystem` or `ThirdParty`;
  * a runtime `probe()`, which reports whether the framework exists, the
    licence is active and the hardware is present;
  * a `priority`.

  The `ProviderRegistry` returns the usable providers for a job, best first.
  Users can switch individual providers off, for example to force software
  decoding.
* **Interfaces defined so far:**
  * `CodecProvider` (`codec.h`): decoders and encoders;
  * `TranscriptionProvider` (`transcription.h`): speech-to-text;
  * `PluginHostProvider` (`plugin_host.h`): OFX, AVX, CLAP, VST3 and AAX
    discovery.

  Video I/O devices and shared storage are reserved as categories. They get
  interfaces in their phases.
* **Modules** (`module_abi.h`, `module_loader.h`). A module is a shared library
  that exports `lumenis_module_info` and `lumenis_module_register`. The loader:
  * checks the module's ABI version and C++ toolchain before calling into it;
  * refuses duplicates;
  * never unloads a module, because providers it created may still be in use;
  * reports every failure. `cutthulhu --list-providers` shows the reasons.

  Module authors compile against the `lumenis::module_sdk` CMake target only.
  `tests/ext/test_module.cpp` is a complete example, and the tests prove it
  loads and works across the library boundary on every platform.
* **Search paths.** Modules are found in `CUTTHULHU_MODULE_PATH`, the
  application's own `modules` directory and the per-user `modules` directory.
  See [INSTALL.md](../INSTALL.md).

## Capability matrix

This table is the plan of record for each patent- or licence-sensitive
capability. Entries marked † must be reviewed by counsel before they ship in
an official build.

| Capability | Bundled open source | OS / driver provided | Third-party hook (needs a deal) |
|---|---|---|---|
| H.264 / AVC | — † (FFmpeg can decode it; patent pool) | VideoToolbox, Media Foundation, VA-API / Vulkan Video; or a system FFmpeg the user installed | Licensed codec pack |
| HEVC / H.265 | — | VideoToolbox, Media Foundation (HEVC extension), VA-API | Licensed codec pack |
| ProRes | Decode via FFmpeg † | VideoToolbox (Apple-licensed encode + decode on macOS) | Apple ProRes licence for Windows/Linux encode |
| ProRes RAW, BRAW, R3D, ARRIRAW | — | Vendor runtime installed by the user (Blackmagic RAW, RED SDK, ARRI) where the vendor's terms allow dynamic loading | Vendor SDK modules |
| DNxHD / DNxHR (SMPTE VC-3) | FFmpeg encode + decode † | — | — |
| XDCAM / IMX (MPEG-2), DV, MJPEG | FFmpeg (MPEG-2 patents have expired) | — | — |
| AVC-Intra, XAVC | — † (H.264 profiles) | As H.264 | As H.264 |
| JPEG 2000 | OpenJPEG (BSD) | — | Kakadu (commercial, faster) |
| AV1, VP9 | dav1d, libaom, libvpx (BSD) | Hardware via OS | — |
| CineForm | GoPro CineForm SDK (open source) | — | — |
| PCM, MP3, FLAC | Built in / FFmpeg (MP3 patents have expired) | — | — |
| AAC | — † | AudioToolbox, Media Foundation | fdk-aac or a licensed encoder |
| AC-3 / E-AC-3 / Atmos | AC-3 decode via FFmpeg (patents have expired) † | OS decoders | Dolby licence |
| MXF OP-Atom / OP1a | bmx (BSD-3) + our own code | — | — |
| QuickTime / MP4 | FFmpeg libavformat (LGPL) | — | — |
| AAF | Our own implementation, modelled on pyaaf2 (MIT) | — | — |
| ScriptSync / PhraseFind equivalent | whisper.cpp (MIT) with Whisper models (MIT), plus our own transcript / phonetic index | Apple Speech framework, Windows speech | Commercial engines or cloud ASR |
| Effect plug-ins | OpenFX host (BSD) | — | AVX bridge (Avid SDK) |
| Audio plug-ins | CLAP host (MIT); VST3 host (check the SDK licence when we start, since Steinberg has moved it to an open licence) | Audio Units (macOS) | AAX bridge (Avid SDK) |
| Colour management | OpenColorIO (BSD), ACES | — | — |
| Video I/O hardware | AJA NTV2 SDK (MIT) | Blackmagic Desktop Video driver (installed by the user) | Other vendors' SDKs |
| Shared storage | Bin locking on any NAS/SAN | — | NEXIS client, MediaCentral |

FFmpeg rules:
* Official builds link FFmpeg dynamically.
* FFmpeg is configured LGPL-only (`--disable-gpl --disable-nonfree`), with the
  decoders and encoders marked † left out until counsel signs off.
* A `system-ffmpeg` provider can load a libavcodec the user installed
  themselves, as Linux distributions and RPM Fusion ship it. That user's
  rights then cover the codecs it contains.

## Licensing of third-party modules

The application is GPLv3. Under the FSF's reading, a proprietary module
loaded into the same process and sharing C++ objects with the application
forms a combined work. That is a problem for modules built on proprietary
SDKs, such as AAX or AVX bridges. OS frameworks are not affected, because
GPLv3's System Library exception covers them. There are two ways to make
proprietary modules possible:

1. **A GPLv3 section 7 additional permission (a "module exception").** It lets
   modules that use only the module ABI be distributed under any licence.
   Only the copyright holders can add it, so it must be added before outside
   contributions arrive, or be covered by a CLA.
2. **Out-of-process bridges.** The proprietary code runs in a separate helper
   process and talks to a small open-source module over IPC. Avid's own AAX
   hosting rules may require this kind of sandboxing anyway.

The module ABI supports both. Choosing between them is a project-owner
decision; see ROADMAP §2.2.
