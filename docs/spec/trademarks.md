# The trademark check (9.8)

The owner's request (2026-11-09): Avid marks some feature names as trademarks, so we must not use them as our menu,
command, window or function names. Avid's full list is at https://www.avid.com/legal/trademarks-and-other-notices (it refuses automated readers;
the owner copied it in on 2026-11-12, and every mark on it is in `tests/data/trademarks.json`). The guides' trademark notice (Editing Guide
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

9.8c showed the effects and parameter categories under our names: `model::shownName` (`src/model/shown_names.*`)
turns a stored name into the one to show, through a table of stored words and ours ("PlasmaWipe Avid Lava" is shown as
"Gradient Wipe Lava"). The Effect Palette (its items keep the stored name in their data; `EffectPalette::category`
and `shownEffects` still give stored names), the Effect Editor's title, category headers, parameter labels and menu
options (each option keeps its stored text as its value), the Sequence Report and new effect templates' names use it;
saved sequences, templates and AAF keep the stored names. The remaining texts (Export/Create SubCap, the SubCap
messages, FluidMotion in the Timewarp's Type menu, the AniMatte message, Send To's "Avid Video - Consolidate Audio",
now "MXF Video - Consolidate Audio") were renamed as in 9.8b. `scripts/check-trademarks.py` checks effects.json and
effect_parameters.json as shown, reading the table in shown_names.cpp. Every mark is now renamed, absent or nominative.

9.8b renamed the texts (commands, menus, windows, settings, messages and the Audio Processing templates' names in bins,
"<clip> - Audio Processing Effect: <effect>"); the effect and parameter category names are 9.8c's, so Titler+ and
NaturalMatch, which are also an effect and a parameter name, stay planned until then, and the FrameFlex effect's name
in the Effect Editor goes with them.

## Other companies' marks (9.8d)

The 9.8 item also asks that no name of ours be one another editing or finishing company owns. Read on 2026-11-12:

* **Published lists.** Apple's (https://www.apple.com/legal/intellectual-property/trademark/appletmlist.html) and
  Grass Valley's (https://www.grassvalley.com/trademarks/) were read. Adobe's
  (https://www.adobe.com/legal/permissions/trademarks.html) and Autodesk's refuse automated readers (Akamai), and
  Avid's shows a bot check; we did not get round those. Blackmagic Design, Boris FX and MAGIX/VEGAS publish no list
  (their legal notices name none).
* **The USPTO register** (tmsearch.uspto.gov's search, live marks in classes 9 and 42, software). We looked at every
  live mark owned by Adobe, Apple, Avid, Autodesk, Blackmagic Design, Boris FX (and Imagineer), Grass Valley (and
  GVBB, Canopus) and MAGIX, plus the feature names the item names (Warp Stabilizer, Morph Cut, Lumetri, Magnetic
  Timeline, Speed Warp, Magic Mask ...), and each of our own new names from 9.8b and 9.8c.
* **EUIPO/TMview** (www.tmdn.org) is reachable but its search has no public interface we can call; it is still to be
  checked by hand (or when the owner's lawyer checks the names).

What we found and did:

* **Every mark against our strings.** Each mark from those lists and the register was searched for in every
  user-visible string. Most of the words found were ordinary words in their ordinary sense: Source, Clips, One,
  Pages, Music and Numbers (Apple's and Adobe's); Motion, Lightning, Metal and Aperture (Apple's); EQ and Profile
  (Grass Valley's); Intensity (Blackmagic Design's). Some were names of formats and products we exchange files with:
  Final Cut Pro 7 XML, Apple ProRes, QuickTime, Photoshop, OpenGL and OMF. These are in `trademarks.json` as
  "descriptive" or "nominative", with the reason.
* **Two real findings, fixed.** The Title Tool's New Title dialog had a dimmed Marquee button, and the Title Style
  Sheet had Convert to Marquee. Marquee is Avid's registered mark (USPTO serial 75467275) for its own title
  application; both buttons are now left out. Transcode's "Frame Flex" check box (FrameFlex with a space) is now
  "Framing".
* **The check now also finds a mark split in two.** `scripts/check-trademarks.py` matches a mark of capitalised
  parts with a space or hyphen between them too, so FrameFlex is caught as "Frame Flex". That found "Expert Render"
  (the Smart Render In/Out window title and its message), now "Smart Render". Our own name is skipped when it is the mark
  split ("Chroma Curve"), and "descriptive" marks are not checked.
* **Our new names in the register.**
  * SMART RENDER is registered (serial 90280696), for web software that displays 3D models of physical assets.
  * SPEED BUMP's registrations cover dosage dispensers, ice cream and vehicle parts.
  * COMPOSER has many registrations, two of them for audio/video software (Symetrix's signal-processing design
    tools, serial 90111614; Jamf's packaging software). Composer is the guides' name for the monitors' window, and
    Grass Valley lists GV Composer and Channel Composer. Kept as our window's name, for the owner to confirm.

  None of our other names (Title Designer, Flow Motion, Gradient Wipe, Phrase Search, Script Alignment, Speaker Cut,
  Color Mask, Spectral Key, Frame Paint, Image FX, Framing, Asset Transfer, Sync Clips ...) is registered in these
  classes, and neither is Cutthulhu.
* **The other companies' product and feature names** are in `trademarks.json` as "absent" with their owners
  (Premiere Pro, After Effects, Lightroom, Warp Stabilizer, Morph Cut, Lumetri, Magnetic Timeline, DaVinci Resolve,
  Fusion, Fairlight, Speed Warp, Flame, Smoke, Maya, Sapphire, Continuum, Mocha, EDIUS, Vegas Pro ...). So are
  Avid's coined marks from its list, except the common words on it (Artist, Impact, Eleven, Space ...), which are
  "descriptive".

A trademark search by a person, in the register itself, is still needed before a public release; this check only
keeps known marks out of the strings.

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
