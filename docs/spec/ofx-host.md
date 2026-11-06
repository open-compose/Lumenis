# The OFX plug-in host (Phase 6.11)

Media Composer takes third-party video effects as AVX plug-ins (FX Guide
ch.6, p.157-164). AVX needs Avid's SDK, so Cutthulhu hosts OpenFX image
effect plug-ins in its place (ROADMAP 6.11, docs/EXTENSIONS.md). OpenFX is
the open standard of the Academy Software Foundation (BSD 3-Clause; its
1.5.1 headers are in `third_party/openfx`). The audio counterpart is the
CLAP host (docs/spec/clap-host.md).

6.11a, this part, finds and describes plug-ins and lists them in the
Effect Palette. Rendering and the parameters in the Effect Editor are
6.11b; the General context's several inputs, overlays and OCIO are 6.11c.
Until 6.11b, a plug-in effect applied to a segment passes its picture
through unchanged.

## Finding plug-ins (`src/media/ofx_host.*`)

* **Where:** the folders the OpenFX standard lists, then those in
  `OFX_PLUGIN_PATH` and, ours for tests, `LUMENIS_OFX_PATH`
  (`ofxSearchPaths`). Folders that do not exist are left out.
  * Windows: `%CommonProgramFiles%\OFX\Plugins`
    (`C:\Program Files\Common Files\OFX\Plugins`);
  * macOS: `/Library/OFX/Plugins`;
  * Linux: `/usr/OFX/Plugins`.
* **What:** `.ofx.bundle` folders at any depth (`findOfxBundles`). The
  binary is `Contents/<architecture>/<name>.ofx`, where the architecture is
  `Win64`, `MacOS`, `Linux-x86-64` or `Linux-aarch64`. A bundle without a
  binary for this platform is left out.
* **When:** at start-up, before the main window opens and before the
  Effect Palette's catalog is first read (`app/main.cpp`). The guide's rule
  "Do not add or remove plug-ins while Media Composer is running" (p.158)
  holds here too: the palette does not change while running.
  `LUMENIS_NO_OFX=1` skips the scan, for a plug-in that crashes on loading.
* **The provider:** `OfxHostProvider` (`org.openeffects.ofx-host`) is the
  `ext::PluginHostProvider` for OFX, registered in
  `media::registerProviders` next to the CLAP host.

## Describing a plug-in

Each binary is loaded with `QLibrary` and stays loaded. The host reads
`OfxGetNumberOfPlugins` and `OfxGetPlugin`, and keeps only image effect
plug-ins (`OfxImageEffectPluginAPI`, version 1). For each one it:

1. calls `setHost` with our host: its property set and `fetchSuite`;
2. runs `kOfxActionLoad`;
3. runs `kOfxActionDescribe` on a descriptor that starts with the
   standard's defaults (`ofxImageEffect.h`), and reads the label,
   grouping, description and supported contexts;
4. runs `kOfxImageEffectActionDescribeInContext` for the first context we
   take, in this order: Filter, Generator, Transition, General. It reads the
   clips and parameters defined there (`OfxPluginInfo::clips`, `params`).
   **Ours:** the standard leaves the order to the host.

The suites (version 1 of each) answer the describe actions:

| Suite | What it does |
|---|---|
| Property | Typed properties (int, double, string, pointer) with any number of values. A plug-in can set properties the host did not create. Reading a missing property answers `kOfxStatErrUnknown`. |
| Parameter | `paramDefine` for every type in `ofxParam.h`, with the standard's defaults for each type. A name defined twice answers `kOfxStatErrExists`. Values and keyframes belong to instances: they answer `kOfxStatErrUnsupported` until 6.11b. |
| Image Effect | Property and parameter sets of descriptors, `clipDefine` with the standard's clip defaults, image memory. Clip images come in 6.11b. |
| Memory | Allocation. |
| MultiThread | Runs a function on n threads and waits for them; thread index, processor count, recursive mutexes. |
| Message | See the Console below. |

The host's own properties say what it supports: OpenFX API 1.5; the
Filter, Generator, Transition and General contexts; RGBA float images; no
tiles, multiple resolutions, overlays or custom interacts yet. Its name is
`org.open-compose.cutthulhu` (ours).

## In the Effect Palette

"Third-party plug-in effects appear in the Effect Palette in their own
category (usually the name of the plug-in vendor). Individual effects each
have a plug-in icon" (p.157).

* **Category:** the first part of the plug-in's grouping, which by
  convention is `Vendor/Subgroup`. A plug-in with no grouping goes in "OFX
  Plug-Ins" (ours). The categories come after our own.
* **Name:** the plug-in's label. If two plug-ins in one category have the
  same label, the second is named "<label> (<identifier>)" (ours).
* **Where it applies:** Filter, Generator and General plug-ins are segment
  effects. Transition plug-ins are transition effects.
* **Real time:** no. **Ours:** plug-ins render on the processor, so they
  have no green dot.
* **Icon:** our effect tile with a plug on it (`effectIcon(..., plugin)`),
  in the palette and the Effect Editor. The drawing is ours; the guide's is
  not copied. `EffectInfo::ofxPlugin` holds the plug-in's identifier.

## What is left out, and the Console

Why a plug-in is left out goes to the Console (Tools > Console): "The
Console window might display messages about why a plug-in did not load
correctly" (p.163). At start-up the Console says how many plug-ins were
found and how many were added, then one line for each one left out:

* a binary that cannot be loaded (the loader's error, such as a missing
  supporting library, as p.163 describes);
* a binary without the OFX entry points;
* a plug-in whose load, describe or describe-in-context action fails,
  with the status it returned;
* a plug-in that supports none of our contexts (such as Paint only).

Messages a plug-in sends through the message suite also go to the
Console, as "OFX: <plug-in id>: <text>" ("error: " before errors).
Questions are answered No during the scan.

## Tests

* `tst_ofx_host`: our own OFX binary (`tests/media/ofx_test_plugin.cpp`),
  built as `test-ofx/CutthulhuTest.ofx.bundle/Contents/<arch>/CutthulhuTest.ofx`.
  It covers finding bundles, the search paths, describing five plug-ins
  (a Filter with parameters of five types, a Transition, a Generator and
  General plug-in, one that fails and one that is Paint only), skipping a
  plug-in of another API, the messages, registering and the provider. The
  plug-in also checks the property and parameter suites' answers, such as
  errors for duplicate names and missing properties, and runs four threads.
* `tst_ofx_palette`: the same plug-ins in the Effect Palette's catalog and
  list, with the plug-in icon.
