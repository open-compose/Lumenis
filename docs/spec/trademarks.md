# The trademark check (9.8)

The owner's request (2026-11-09): Avid marks some feature names as trademarks, so we must not use them as our menu,
command, window or function names. Avid's full list is at https://www.avid.com/legal/trademarks-and-other-notices (not
reachable from the build machines; checked against the guides instead). The guides' trademark notice (Editing Guide
p.2, FX Guide p.2-3) lists "Avid, the Avid Logo, Avid Everywhere, Avid DNXHD, Avid DNXHR, Avid Nexis, AirSpeed,
Eleven, EUCON, Interplay, iNEWS, ISIS, Mbox, MediaCentral, Media Composer, NewsCutter, Pro Tools, ProSet and RealSet,
Maestro, PlayMaker, Sibelius, Symphony, and all related product names and logos". The guides mark others in the text
with a trademark or registered sign (pages in `tests/data/trademarks.json`: AutoSync, AudioSuite, SubCap, FluidMotion,
AniMatte, SpectraMatte, SpectraGraph, Intraframe, SteadyGlide, Magic Mask, NaturalMatch, ChromaCurve, ChromaWheel, Frame
Chase, DigiTranslator, AVX ...). Avid's coined feature names that the guides do not mark (ScriptSync, PhraseFind,
MultiRez, FrameFlex, Titler+, PlasmaWipe, ExpertRender, Illusion FX, AutoCut, MultiGroup, the Fluid effects) are
"related product names" and are treated the same.

## What we do

* `tests/data/trademarks.json` lists each mark with its source, our generic name and its status:
  * **renamed**: ours already (AutoSync is Sync Clips, 9.3);
  * **planned**: renamed in 9.8b (command, menu, window, setting and message texts) or 9.8c (effect and parameter
    category names, which are also the keys effects are kept under, so they get display labels and keep their stored
    names);
  * **absent**: not in our UI (Frame Chase, DigiTranslator, AVX, EUCON, ...);
  * **nominative**: kept, because the name identifies Avid's own product or format we interoperate with, not a
    feature of ours (Media Composer Bin and `.avb` bins, Send To Pro Tools and its channel order, Avid DNx/DNxHD/DNxHR
    codecs, Avid Log Exchange, Avid DS captions, Avid OP-Atom media).
* ctest `trademark_names` (`scripts/check-trademarks.py`) looks through every user-visible string, the values of
  `src/ui/resources/*.json` (not their comments, sources and pages) and the `tr()` strings in `src/`, and fails for a
  renamed or absent mark; a nominative name is taken out of a string before the others are looked for, so "Avid DNx HQ"
  does not count as "Avid". Planned marks are listed with their step.
* The specs keep the guides' names as the source (they cite the guide); only what the user sees changes.

9.8b renamed the texts (commands, menus, windows, settings, messages and the Audio Processing templates' names in bins,
"<clip> - Audio Processing Effect: <effect>"); the effect and parameter category names are 9.8c's, so Titler+ and
NaturalMatch, which are also an effect and a parameter name, stay planned until then, and the FrameFlex effect's name
in the Effect Editor goes with them.

## Our names

| Avid's | Ours |
| --- | --- |
| AutoSync | Sync Clips (done) |
| AudioSuite | Audio Processing |
| SubCap | Subtitles |
| FluidMotion, FluidBlur, FluidColorMap, FluidMorph, FluidFilm | Flow Motion, Flow Blur, Flow Color Map, Flow Morph, Flow Film |
| AniMatte | Animated Matte |
| SpectraMatte, SpectraGraph | Spectral Key, Key Graph |
| Intraframe | Frame Paint |
| SteadyGlide | Smooth Glide |
| Magic Mask | Color Mask |
| NaturalMatch | Keep Luminance |
| ChromaCurve, ChromaWheel | Chroma Curve, Color Wheel |
| ScriptSync (ScriptSync AI) | Script Alignment |
| PhraseFind (PhraseFind AI) | Phrase Search |
| MultiRez | Multi-Resolution |
| FrameFlex | Framing |
| Titler+, Avid Titler+ | Title Designer |
| PlasmaWipe | Gradient Wipe |
| ExpertRender | Smart Render |
| Illusion FX | Image FX |
| AutoCut | Speaker Cut |
| MultiGroup | Group of Groups |
| Avid Pan & Zoom | Pan & Zoom |
| Avid Background Services | Background Services |
| Interplay (Interplay Transfer) | Asset Transfer |
| Symphony ("Symphony Option Only") | left out |
