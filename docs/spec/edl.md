# EDL (Phase 3.6a)

An edit decision list (EDL) is "an instruction list for edits you make ...
used for exchanging information between editing systems" (Editing Guide
p.939). Media Composer writes EDLs with the List Tool (p.944-960) and reads
them with File > Input > Import EDL (p.951). Cutthulhu reads CMX 3600 lists
(and the variants other programs write) into the native model
(native-model.md) and writes CMX 3600 and Media Composer's File_129.

Sources:
* The Media Composer Editing Guide (2025): the List Tool, its output types
  and options (p.944-960), Import EDL (p.951-952), capitalization
  (p.950), troubleshooting (p.967-968).
* The CMX 3600 format, as it is published and widely documented: an event
  line of event number, reel, channels, edit type, a transition's length,
  then source IN, source OUT, record IN and record OUT; `TITLE:` and `FCM:`
  lines; comment lines starting with `*`.
* OpenTimelineIO's CMX 3600 adapter (Apache-2.0,
  github.com/OpenTimelineIO/otio-cmx3600-adapter): read for behaviour in
  practice (how a dissolve's two lines relate, `M2` speeds, `LOC` and
  `FROM CLIP NAME` comments, `AUD` lines). Its sample lists are part of the
  interchange corpus (`scripts/interop-corpus.py`, not committed). Credited
  in THIRD-PARTY-NOTICES.md.
* For File_129's reel length: the name of the type, and a public
  description of the File_16/File_32/File_129 family (the reel field holds
  that many characters; CMX 3600's holds eight). **Unverified** against a
  list from Media Composer; checking one is a gate item for when the seat is
  at hand again.

## The format

```
TITLE:   Harbor Cut
FCM: NON-DROP FRAME
001  DAY1     AA/V  C        10:00:00:00 10:00:02:00 01:00:00:00 01:00:02:00
* FROM CLIP NAME:  Wide
002  DAY1     V     C        10:00:02:00 10:00:02:00 01:00:02:00 01:00:02:00
002  DAY2     V     D    012 11:00:00:00 11:00:03:00 01:00:02:00 01:00:05:00
* FROM CLIP NAME:  Wide
* TO CLIP NAME:  Close
003  DAY3     NONE  C        00:10:00:00 00:10:05:00 01:00:00:00 01:00:05:00
AUD  3    4
M2   DAY3           012.0                00:10:00:00
* LOC: 01:00:01:00 RED     check focus
```

* **Channels:** `V`, `A` (audio 1), `A2`, `AA` (1 and 2), `B` or `A/V`
  (video and audio 1), `AA/V`, `A2/V`, `NONE`; audio 3 and 4 on an `AUD`
  line after the event. The reader also takes `An` and `Vn`, which some
  programs write.
* **Edit types:** `C` cut, `D` dissolve, `Wnnn` wipe (SMPTE wipe number),
  `K B`, `K`, `K O` keys (background, foreground, key out). A transition
  has two lines with the same number: the first, a cut of no length, names
  the outgoing source; the second the incoming one, with the transition's
  length in frames. The transition starts at the event's record IN.
* **Reels:** `BL` (also `BLK`, `BLACK`) is black; `AX` an auxiliary source.
* **FCM:** `DROP FRAME` or `NON-DROP FRAME`; it holds for the lines after
  it, so a list can change it between events.
* **Timecodes** are labels at the list's rate, which the list does not
  give. A bare number is a frame count.

## Import (`interop::edl::importText`, `importFile`)

An EDL names its material by tape and timecode only. Media Composer's
Import EDL makes a sequence and asks to Decompose it before it is used
(p.951-952): the sequence refers to sources, not master clips. Cutthulhu
does the same:

| EDL | Native model |
|---|---|
| Each reel (not black) | A source item (`BinItemKind::Source`, a tape): named for the reel, Tape column the reel, Start 00:00:00:00, a day long, Tracks the channels used. Its MobID is `edl::tapeMobId(reel)`, a name-based UUID, so every import of the same reel names the same source. Source items are hidden by Set Bin Display's default (p.288). |
| The list | A sequence named for `TITLE:` (else the file), its Start the first record IN, TC1 shown. |
| An event | On each of its tracks, a segment overwritten at its record IN (`Sequence::overwrite`), in the order of the list, as an edit controller records them: a later event over an earlier one wins, with a warning. Its source IN is the frame of the tape timecode; its name `FROM CLIP NAME` (`TO CLIP NAME` for a transition's incoming source), else the reel. |
| Black | Filler. |
| Gaps | Filler. |
| `D` | The incoming segment's transition, Blend/Dissolve, Starting at Cut, the length given. |
| `Wnnn` | The same with effect EDL/`Wnnn` (kept by number until effects are matched, Phase 6). |
| `K`, `K O` | The foreground on V2 with a segment effect EDL/Key; the background (`K B`) on V1. A warning says a key is only a layer here. |
| `M2` | A segment effect EDL/Motion Effect with the speed (frames per second of the source) as `{"M2": {"Speed": n}}`; the segment lasts the event's record length. |
| `* LOC:` | A sequence marker at that record timecode (Media Composer writes them inside the event's record times), its colour when the word after the timecode is one (else Red), its comment the rest, on the event's first track. |
| `AUD 3 4` | Tracks A3 and A4 of the event. |

* **Frame rate:** the Import EDL dialog's Frame Rate (p.951 step 4), the
  project's by default. Without one (`--import-edl` with no `--rate`),
  it is guessed: 29.97 for drop frame, else from the highest frame number
  (23.976, 25, 29.97 or 59.94), with a warning.
* **Drop frame** at a rate without it is read as non-drop, with a warning.
* **Not read, with a warning:** split edits (`SPLIT:`), comments other
  than those above (`SOURCE FILE`, `ASC_SOP`, ...: counted by key), lines
  that are not events (named by line number, up to 20). Nothing in a list
  makes the import fail.
* **Again:** `edl::reuseSources` drops source items whose MobID the bin
  already holds and points the sequence at those, so a tape imported twice
  is one source (Import EDL does this).

## Export (`interop::edl::exportText`, `exportFile`)

The List Tool's EDL output for one sequence. Each listed track's segments
become pieces with record and source timecodes, sorted by record IN (Sort
Mode A, p.959) and written as events numbered from the Starting Event
Number.

* **Tracks:** the first listed video track and audio tracks 1 to 4; others
  are left out with a warning (an EDL describes one picture track; the List
  Tool can save a list for each, p.949 step 9: later).
* **Reels:** the clip's Tape column, its tape source, a source item's name,
  its import source's file name, else its name. Spaces become underscores;
  a name longer than the format holds loses its end (Tape or File Name
  truncation End, p.960), with a warning. CMX_3600: 8 characters; File_129:
  129, the reel field as wide as the longest.
* **Source timecode:** the clip's Start plus the segment's IN point.
* **Capitals:** names, reels and the title are capitalized, as Media
  Composer's lists are (p.950); `upperCase = false` keeps the case
  (toupper_conversion FALSE).
* **Transitions** (List Options Picture/Sound Dissolves, p.958): written
  where they start, so a transition centred on its cut moves the cut back
  by the frames before it; read back, it is one starting at that earlier
  cut, the same frames on the screen. Blend/Dissolve is `D`, EDL/`Wnnn` its
  wipe; others are written as dissolves, with a warning. From filler: a
  `BL` outgoing line.
* **Black Edits** (p.958): gaps as `BL` events; off, gaps are left out.
* **Clip Names** (p.958): `* FROM CLIP NAME:` and `* TO CLIP NAME:`, with
  two spaces before the name as Media Composer writes it.
* **Markers** (p.958): `* LOC:` after the event whose record times hold
  the marker, on its track when listed; markers outside every event are
  left out with a warning.
* **Optimize EDL** (p.958): pieces of V, A1-A4 with the same edit (record
  and source times, reel, transition, speed, name) become one event (`B`,
  `AA/V`, `AUD 3 4`).
* **Motion effects** made by the import are written back as `M2`; other
  segment effects are left out with a warning.
* Lines end in CR LF.

### List Options and Formatting (11.4c1)
The options of p.958-960 whose form a published list shows (the OpenTimelineIO adapter's Avid sample lists in the
corpus) or that the guide spells out:

* **Other Transitions** (p.958): on, wipes are `Wnnn` and other transitions dissolves, as above; off, every transition
  but a dissolve is a cut.
* **Source File Name:** `* SOURCE FILE: <the clip's Source File>` after the event's clip names.
* **Clip Comments:** `* <the clip's Comments>`.
* **Color Decision Lists:** the clip's ASC_SOP and ASC_SAT columns as `*ASC_SOP (s s s) (o o o) (p p p)` and
  `*ASC_SAT x`, as the corpus's lists write them; an ASC_SOP the reader cannot split into three triples is written as
  it is.
* **Other Segment Effects:** `* <category>, <name>` for each segment effect but the motion effects written as `M2`
  (the corpus's `* BLEND, DISSOLVE` form); on, they are no longer warned about.
* **All Markers at End** (p.954): the markers outside every event, as `* LOC:` lines after the last event, in record
  order.
* **Sort Mode** (p.959): A (Record In), B (Source, Record In), C (Source, Source In), D and E as B and C with the
  effect events (transitions, motion effects) last, S (by source IN alone). Events are numbered after sorting.
* **Source TC:** Start, Sound TC or Auxiliary TC1-5; a clip without the column is listed with its Start, with a
  warning. VITC, Film_TC and TC24 stay dimmed.
* **Reel ID Type:** Tape, Soundroll, Camroll, Labroll or Disk Label; a clip without the column keeps its tape reel.
* **Tape or File Name truncation** (p.960): End, Middle, Beginning and First_N, the guide's own examples (CHICAGO138101
  becomes CHICAGO1, CHIC8101, GO138101 and CO138101).
* **Convert tape names to numbers** (p.960): every reel but BL and AX as 001, 002 ... in the order first used (ours:
  the guide does not say the order).
* **Starting TC** (p.960): the record timecode of the sequence's first frame; empty, the sequence's Start.

### Titles (6.8c4b)
"Beginning with Media Composer v2025.6, all titles used in the Timeline can be exported as part of an Edit Decision List
(EDL). Each Titler+ effect is assigned a dedicated event timecode and a unique name (based on the Clip Text label)" (FX
Guide p.327). Ours: every Titler+ segment on a listed video track, also one past the single video track an EDL lists, is
a video event over its record times with reel BL and source timecode from 00:00:00:00; its name is the title's topmost
text ("Titler+" when it has none), with " 2", " 3" ... after a name already used, and is written as `* FROM CLIP NAME:`
even with Clip Names off. A title on filler is not also a black event; a title over a clip does not warn that the
segment's effect is left out.

## In the application

The menus and dialogs are in ui-inventory.md §19.

* **File > Input > Import EDL** (`MainWindow::importEdl`): the Import EDL
  dialog, then the sources and the sequence go into the bin chosen (or a
  new one), the sequence selected, and the Console says to Decompose it
  (p.951 step 6). Decompose comes later.
* **Tools > List Tool** (`ListTool`, `list_tool.*`): Output Format
  CMX_3600 or File_129, the Input, List Options and Formatting tabs,
  Preview and Save List.

## Checks

* `tst_edl`: cuts, every channel field, drop frame, dissolves (from the
  outgoing clip and from black), wipes and keys, `M2` freeze frames and
  speeds, `LOC` markers, the rate guessed, malformed lines and 300 randomly
  damaged lists (warnings, never a crash), reel names (CMX_3600 and
  File_129), round trips (sequence -> EDL -> sequence, the same by
  structure dump, optimized or not, through a file), centred transitions,
  sequences of master clips with Black Edits, and every OpenTimelineIO
  sample list read, written and read again the same; `listOptions`, the
  List Options and Formatting above with the guide's truncation examples.
* `tst_interchange` (UI): Import EDL and the List Tool.
* `cutthulhu --import-edl FILE [--rate R]` prints the bin as a structure
  dump; `cutthulhu --export-edl BIN SEQUENCE OUT.edl [--edl-type
  File_129]` writes a sequence of a .cthbin (an .edl is imported first, a
  round trip).

## Later

* The other output types (CMX_DigitalCut, CMX_Transfer, Cuedos, File_16,
  File_32) and the cut and change lists (Columnar, TabbedLists, WebLists,
  XML, p.947).
* Source TC VITC, Film_TC and TC24, Record TC other than TC1, Frame
  Count, Reel Names, the source table in the saved list (p.959-960).
* Split edits, audio levels (Volume), EQ, Track Patching, Color Effects,
  the adapters' comments and spanned markers (p.954-958): no published list
  shows their form.
* One list per video track (p.949 step 9), the Edit mode (p.949-950), New
  List and Clone List, Active Settings (p.956-957), dragging a sequence in.
* Import EDL's Project Type and Audio Mapping (p.951 step 4), and
  Decompose (p.951-952).
