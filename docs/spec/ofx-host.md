# The OFX plug-in host (Phase 6.11)

Media Composer takes third-party video effects as AVX plug-ins (FX Guide
ch.6, p.157-164). AVX needs Avid's SDK, so Cutthulhu hosts OpenFX image
effect plug-ins in its place (ROADMAP 6.11, docs/EXTENSIONS.md). OpenFX is
the open standard of the Academy Software Foundation (BSD 3-Clause; its
1.5.1 headers are in `third_party/openfx`). The audio counterpart is the
CLAP host (docs/spec/clap-host.md).

6.11a finds and describes plug-ins and lists them in the Effect Palette.
6.11b1 renders them and shows their parameters in the Effect Editor.
6.11b2 handles missing plug-ins, templates of plug-in effects and
`closeallofxplugins`. 6.11c1 adds plug-ins with several inputs, the
Angle control and push buttons; 6.11c2 overlays; 6.11c3 OCIO.

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

## Parameters in the Effect Editor (6.11b1)

A plug-in effect has one parameter category, "<label> Plug-In
Parameters" (`ofxParameterCategory`; ours), listed in its `EffectInfo`
and filled from `ofxEditorParameters`. The values are kept in the
effect's parameters under that category, as ours are, so they are
undoable, keyframed and saved with the sequence. The standard leaves the
controls to the host; ours:

| Plug-in type | Control | Stored as |
|---|---|---|
| Double, Integer | a slider over the display range, else the range, else 0 to 100; Double with 2 decimals, 3 when the range is at most 2 wide | the number at the parameter's name |
| Double2D/3D, Integer2D/3D | a slider per axis, "<label> X", "Y", "Z" | `<name>.x`, `.y`, `.z` |
| Boolean | a checkbox (not keyframed) | true or false |
| Choice | a menu of its options (not keyframed) | the option's text |
| RGB, RGBA | a colour; RGBA's alpha stays its default | `#rrggbb` |
| Group | the subcategory of the parameters in it | — |

Parameters that do not animate (`kOfxParamPropAnimates` 0) take no
keyframes. Secret parameters, the Transition context's own "Transition"
(the transition's progress) and the types not shown yet (String, Custom,
Push Button, Page, Parametric, Bytes, StrChoice) have no control and keep
their defaults. Defaults show through `media::parameterDefault`.

## Rendering (6.11b1)

The compositor gives a plug-in effect to the host
(`ofxSegmentPicture` for segments, `ofxTransitionPicture` for
transitions). The effect's plug-in is found by its category and name
(`ofxPluginOf`).

* **Instances:** each plug-in gets one instance, made at its first render
  with `kOfxActionCreateInstance` in the context it was described in, its
  clips and parameters copied from that context's descriptor.
  `closeOfxInstances` destroys them (`kOfxActionDestroyInstance`); the next
  render makes them again. If an instance cannot be made, its effects pass
  their pictures through and the Console says why.
* **One render:** the instance's project size (the picture's), frame rate
  (the project's, `setOfxFrameRate`) and effect duration are set; then
  `kOfxImageEffectActionIsIdentity` (if the plug-in names a clip, its
  picture is the output), `BeginSequenceRender`, `Render` and
  `EndSequenceRender` for the frame, at render scale 1, the whole frame as
  the render window, no fields. Renders are one at a time (a mutex), so
  plug-ins need not be thread safe.
* **Time:** the plug-in's time is frames into the effect
  (`EffectSpan::frameOf`); `getValueAtTime` at any frame reads the
  effect's keyframes there, `getNumKeys`/`getKeyTime`/`getKeyIndex` list
  them, and the derivative and integral are worked out from the values.
* **Images:** float RGBA, premultiplied, pixel aspect 1, bounds the whole
  frame, rows bottom to top as the standard has them. Input clips:
  * Filter and General: "Source" is the segment's picture;
  * Generator: none;
  * Transition: "SourceFrom" the outgoing picture, "SourceTo" the
    incoming one, and the "Transition" parameter the progress, 0 to 1.

  Every time asked of an input clip gives the render's frame (no
  temporal clip access yet). Other clips are not connected.
* **Segment plug-ins on transitions:** ours, the plug-in works on the
  incoming picture, shown over the outgoing one.
* **Failures:** a render that fails leaves the picture as it was, and the
  Console says so once per plug-in ("If you have problems rendering a
  plug-in effect, check the Console window", p.163).
* **Values the plug-in sets** (in actions we do not send yet) are accepted
  and not kept: the Effect Editor's controls own the values.

## Several inputs, the Angle control and push buttons (6.11c1)

"Some plug-in effects take an arbitrary number of tracks as input. Media
Composer lets you select the number of video inputs from the AVX Optional
Inputs dialog box" (p.161).

* **Contexts:** every context we take is described; the first of Filter,
  Generator, Transition and General is used, or General when it has more
  input clips (ours). Its input clips are `OfxPluginInfo::inputs`, with
  their labels and whether they are optional or masks.
* **The dialog:** applying a plug-in that has optional inputs (from a drop
  or the palette) opens the OFX Optional Inputs dialog (ours, after
  Avid's name) with a Number of Tracks menu, from the required inputs to
  all of them, all of them chosen at first (ours). Cancel applies nothing.
  The number goes in the effect's tag as `inputs` (`ofxInputCount`).
* **Which clips:** every required input, then the optional ones in their
  order, as many as were chosen (`connectedInputs`); the others are not
  connected.
* **Inputs are nest tracks** (p.161 sends the reader to "Nesting
  Effects"): the effect's segment gets a nest (as Step In makes one, the
  segment's own picture on V1) with a video track per input. The first
  input is V1 alone, the second V2 alone, and so on; edit material onto
  them by stepping in or expanding the nest. An input whose track has
  nothing at a frame is connected and empty. With one input the nest is
  composited as usual and given as Source.
* **The Angle control:** a Double of `kOfxParamDoubleTypeAngle` is a circle
  the pointer drags around ("Values can be positive or negative and are not
  limited to a single 360º rotation", p.162): `AngleDial`, 0° up and
  clockwise, whole turns adding up, arrow keys a degree (Shift ten; ours).
  It is kept when the drag ends, keyframed like a slider.
* **Push buttons:** a Push Button parameter is a button in the Effect
  Editor. Pressing it sends the instance `kOfxActionInstanceChanged` (with
  begin and end) for that parameter, as a user's edit at the position's
  time; plug-ins open their own dialogs there ("Custom dialog box only ...
  clicking the Other Options button or another custom button", p.161).
  The values the plug-in sets meanwhile come back (`ofxPushButton`) and are
  kept in the effect as edits of the Effect Editor's controls. Values set
  at other times are not kept.

## Missing plug-ins, templates and closing (6.11b2)

* **The tag:** a plug-in effect applied from the palette gets the
  parameter category "OFX Plug-In" with the plug-in's `id`, `label` and
  `version` (`markOfxEffect`, from `Timeline::seededEffect`; ours). It is
  not one of the effect's parameter categories, so the Effect Editor does
  not show it. A tagged effect is its plug-in's whatever its palette name
  (`ofxPluginOf` looks for the tag first).
* **A missing plug-in** ("If you are transferring a project that includes
  sequences with AVX plug-in effects from another system, you must install
  matching AVX plug-ins to see the effects", p.157): a tagged effect whose
  plug-in is not registered (`isMissingOfxEffect`).
  * Its icon in the Timeline is blank, the tile's outline only ("If the
    effect icon in the Timeline is blank, Media Composer could not find
    the plug-in", p.163). Installed plug-ins' icons have the palette's plug.
  * Its picture passes through; a missing transition dissolves (ours).
  * The Console says "Can't find effect: <label> (<id>)" the first time
    one is drawn ("look for the message 'Can't find effect.' The message
    identifies the plug-in", p.163).
* **Effect templates** (p.157) keep the whole effect, the tag and the
  values with it, so a template of a plug-in effect applies that plug-in.
* **Closing plug-ins:** the Console's `closeallofxplugins` destroys every
  plug-in instance (`closeOfxInstances`), as `closeallavxplugins` closes
  AVX 1.x plug-ins ("The AVX plug-ins automatically open again as needed",
  p.158); the Console also takes Avid's name for it.

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
  6.11b1 adds: the Effect Editor controls, rendering the Filter (the
  defaults, the effect's values, keyframes, an identity), the Generator
  (the rows' order) and the Transition, alone and through the compositor,
  and instances made and destroyed (the plug-in counts them).
* `tst_ofx_palette`: the same plug-ins in the Effect Palette's catalog and
  list, with the plug-in icon; the Effect Editor's controls for one dropped
  on a segment, and a change kept in the effect. 6.11b2 adds: the tag, a
  template of the effect, the blank Timeline icon of a missing plug-in, and
  the Console's closeallofxplugins. `tst_ofx_host`'s `missingPlugin` checks
  the pass-through and the "Can't find effect" message.
* 6.11c1: the test bundle's sixth plug-in, a General compositing plug-in
  with an optional input, an optional mask, an angle and a push button.
  `tst_ofx_host` checks its inputs, rendering with one, two and three
  inputs alone and from nest tracks in the compositor, the angle's values
  beyond a turn and the push button's values; `tst_ofx_palette` the
  dialog, applying with a number chosen or cancelled, the Angle control's
  drag and keys, and the button kept in the effect.
