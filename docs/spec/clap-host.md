# The CLAP plug-in host (Phase 5.5b)

Cutthulhu hosts CLAP audio plug-ins (CLAP, the CLever Audio Plug-in API,
MIT; headers 1.2.9 in `third_party/clap`). The effect plug-ins that fit join
the built-in effects as Audio Track Effects (Editing Guide p.790-795;
docs/spec/track-effects.md). This follows ROADMAP's decision: Avid's AAX
needs an Avid licence, so open CLAP takes its place (docs/EXTENSIONS.md).

## Finding plug-ins (`src/media/clap_host.*`)

* **Where:** the folders the CLAP specification lists (`entry.h`), then
  those in the environment variable `CLAP_PATH`:
  * macOS: `~/Library/Audio/Plug-Ins/CLAP` and
    `/Library/Audio/Plug-Ins/CLAP`;
  * Windows: `%LOCALAPPDATA%\Programs\Common\CLAP` and
    `%COMMONPROGRAMFILES%\CLAP`;
  * Linux: `~/.clap` and `/usr/lib/clap`.
* **What:** `.clap` files at any depth. A folder named `.clap` is a bundle,
  and its binary is `Contents/MacOS/<name>`; its insides are not searched.
* **When:** at start-up, before the main window opens (`app/main.cpp`).
  The Console says how many were found and added, which were left out and
  why, and which files could not be loaded. `LUMENIS_NO_CLAP=1` skips the
  scan, in case a plug-in crashes while loading (BUILDING.md).
* **Reading a file:** the host loads the library and checks the CLAP
  version. It initialises `clap_entry` with the file's path and reads the
  plug-in factory. Each plug-in is made once, to read:
  * its descriptor: id, name, vendor, version, features;
  * the channel counts of its main audio ports;
  * its parameters. Hidden and read-only ones are left out. A stepped
    parameter with fewer than 64 values becomes a choice, named by the
    plug-in's `value_to_text`.
* **The provider:** `ClapHostProvider` (`org.cleveraudio.clap-host`) is the
  `ext::PluginHostProvider` for CLAP. It is registered with FFmpeg's
  provider in `media::registerProviders` and listed by `--list-providers`.

## What fits

* A plug-in with the `audio-effect` feature whose main input and output
  ports have one or two channels. Instruments and analysers without an
  audio input are left out, with the reason in the Console.
* **Mono and stereo.** Our tracks are mono:
  * a mono plug-in gets the track as it is;
  * a stereo plug-in gets the track on both inputs, and its two outputs
    are averaged. **Ours:** the guide puts only mono plug-ins on mono
    tracks (p.790); with no stereo tracks yet, this lets stereo plug-ins
    be used.
* **The id** is `clap:<plug-in id>`, under which a sequence saves the
  insert. A plug-in missing on another machine shows as "Unavailable
  Effect" with its name (p.790).
* **The category** in the Effect Palette's Audio Track tab comes from its
  features:

  | Features | Category |
  |---|---|
  | compressor, limiter, gate, expander, transient-shaper | Dynamics |
  | equalizer, filter | EQ |
  | delay | Delay |
  | reverb | Reverb |
  | chorus, flanger, phaser, tremolo, pitch-shifter, frequency-shifter | Modulation |
  | distortion | Harmonic |
  | anything else | CLAP |

## Running a plug-in

* Each Audio Track Effect insert is one plug-in instance (`ClapEffect`, a
  `media::AudioEffect`):
  * it is made, initialised and activated at 48 kHz with blocks of up to
    1024 frames, on the thread that makes the mix (CLAP's main thread);
  * `start_processing` and `process` run on the thread that plays (the
    audio thread);
  * it is deactivated and destroyed with the mix.
* **Parameter values** are sent as `CLAP_EVENT_PARAM_VALUE` events at the
  start of the next block, clamped to the plug-in's range.
* **The tail** is the plug-in's `clap.tail`, at most 4 s, or 4 s when it says
  "infinite". **Ours:** 1 s when it does not say.
* `reset` follows a seek (AudioMix settles the effects first, as with the
  built-in ones).
* A block the plug-in fails leaves the sound as it was.
* **The host offers no extensions of its own**, and does not act on requests
  for a restart, a process call or a main-thread callback. A loaded file
  stays loaded until the program ends.

## macOS signing

A Developer ID build has the hardened runtime, which refuses libraries
signed by other teams. `packaging/macos/entitlements.plist` therefore turns
library validation off (`com.apple.security.cs.disable-library-validation`),
as plug-in hosts do. Ad-hoc signed builds, CI's and the owner's, load
plug-ins without it.

## Checks

* `tst_clap_host` loads a CLAP file of our own
  (`tests/media/clap_test_plugin.cpp`, built as `cutthulhu-test.clap`). It
  offers three plug-ins: a mono gain with a dB parameter, a stepped Invert
  and a tail; a stereo level; and an instrument. The test checks:
  * finding plain files and bundles, a broken file named in the errors,
    and `CLAP_PATH`;
  * the descriptors, ports, parameters and choices, the categories, and the
    instrument left out;
  * registering (twice is once), processing over more than one block,
    parameter events and clamping, the tail, stereo averaging, `reset`;
  * the plug-in as an Audio Track Effect insert in `media::AudioMix`;
  * `ClapHostProvider`.

## Later

* The plug-ins' own windows (`clap.gui`). For now their parameters show as
  sliders in the Audio Track Effect tool.
* Latency compensation (`clap.latency` is noted, not used).
* Saving and restoring a plug-in's state (`clap.state`) beyond its parameter
  values.
* Stereo tracks, and plug-ins with sidechain or more ports.
* Scanning in a separate process, so a plug-in that crashes cannot take
  the application with it, and a cache of scan results.
* VST3 (once its SDK licence is checked) and Audio Units on macOS
  (docs/EXTENSIONS.md).
