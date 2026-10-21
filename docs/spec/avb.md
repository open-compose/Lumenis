# Media Composer bins (.avb) (Phases 3.3 and 3.4)

A Media Composer bin is one `.avb` file: its clips, subclips, sequences and
their sources, with the bin's view and settings. Cutthulhu reads bins into
the native model (native-model.md), the way AAF is read (aaf.md), and
writes them (3.4).

Sources:
* pyavb (MIT, Mark Reid), a reader and writer of bins in Python: the
  reference, read for the layout of every class (its `read()` methods,
  and its `write()` methods for the writer).
  Credited in THIRD-PARTY-NOTICES.md ("Prior art for interchange").
* Black-box observation of bins made by Media Composer: the test files of
  pyavb (`scripts/interop-corpus.py`), 14 bins from Media Composer 18.9
  and 18.12.

No Avid SDK or documentation of the format is used: Avid publishes none.

## Layers

| Layer | Where | Status |
|---|---|---|
| Objects: the container and every class's properties | `src/interop/avb.*` | 3.3a, done |
| Import into bins: mobs to clips, sources and sequences | `src/interop/avb_import.*` | 3.3b, done |
| File > Open Bin of a `.avb`, from the Editing Guide | `main_window.cpp` (`openBinFile`) | 3.3b, done |
| Writing objects: the header, the table, every class read | `src/interop/avb_writer.*` | 3.4a, done |
| Native bins written as `.avb` | `src/interop/avb_export.*` | 3.4b, done |
| File > Output > Export to File as a Media Composer bin (ours) | `main_window.cpp` (`exportToFile`), `export_dialog.*` | 3.4b, done |

## The container (3.3a)

Integers are in the file's byte order, given by its first two bytes:
`06 00` little-endian (every bin seen), `00 06` big-endian. Doubles are
read little-endian either way, as pyavb does. A *fourcc* is 4 bytes, stored
reversed in little-endian files (the bytes `PMOC` are the class `CMPO`).
A *string* is a u16 size and that many bytes, NULs at either end dropped;
size 0xFFFF is no string. Strings are Mac OS Roman unless said otherwise;
the UTF-8 ones are decoded strictly.

The header:

| Field | Value |
|---|---|
| byte order | `06 00` or `00 06` |
| magic | `Domain` |
| fourcc | `OBJD` |
| string | `AObjDoc` |
| u8 | `04` |
| string | the last save, `2018/11/12 05:38:02` |
| u32 | the number of objects |
| u32 | the root object's index |
| u32 | `49 49 49 49` or `4D 4D 4D 4D` |
| u32 | the last save, seconds since 1970 |
| 4 bytes | not zero in Media Composer's bins, meaning unknown; kept (`Header::unknown`) |
| fourcc, fourcc | `ATob`, `ATve` |
| string | the creator, `Media Composer 18.9.0.49510`, padded with spaces |
| 16 bytes | reserved |

Then the object table: for each object a fourcc class ID, a u32 size and
the payload. Objects are numbered from 1 in the order they appear; a
*reference* is a u32 index, 0 for none, and one past the table is an error.
The root (the last object in the files seen) is the bin, class `ABIN`.

`avb::File` keeps the file in memory, finds where each object starts when it
opens, and parses objects when first asked for (`File::object`), caching
them. An object whose payload is cut short, whose class is not read, whose
tags or checks fail, or that leaves bytes over, gets an `error` and no
properties; the others read on. A count in the header larger than the bytes
can hold is refused at once.

### Payloads

Each class's payload is its base classes' parts, then its own, as in
pyavb's class hierarchy. Each part starts with `02` and a version byte; the
object ends with `03`. A part may be followed by *extensions*: while the
next byte is `01`, the byte after it names an extension, whose fields each
start with a type tag: 65 (bytes, with a length), 66 (Boolean), 68 (u8),
69 (s16), 71 (s32), 72 (u32 or a reference), 75 (double), 76 (string),
77 (64-bit), 80 (UUID). A UUID is Data1 (u32), Data2 and Data3 (u16) in the
file's order and Data4 (8 bytes); in a MobID it is tagged 72, 70, 70,
65 s32(8). A *MobID* is 65 s32(12) and the 12-byte label, 68 length,
68/68/68 the instance number, and the material number as a tagged UUID.
`Object::properties` keeps pyavb's names and the order it sets them.

| Class | Base | Its part (version), then extensions |
|---|---|---|
| Component (abstract) | | `03`: refs left and right bob, s16 media kind (1 picture, 2 sound, 3 timecode, 4 edgecode...), edit rate (s32 mantissa, s16 power of ten), name, effect ID, refs attributes, session attributes, precompute. Ext 01: 72 parameter list |
| Clip (abstract) | Component | `01`: u32 length |
| `SCLP` source clip | Clip | `03`: 8 bytes (an old MobID, unused), s16 track, s32 start. Ext 01: the MobID |
| `TCCP` timecode | Clip | `01`: u32 flags, u16 fps, 6 unused bytes, u32 start |
| `ECCP` edgecode | Clip | `01`: 8-byte header, u8 film kind, u8 code format, u16 base perforation, 4 unused bytes, s32 start |
| `TRKR` track reference | Clip | `01`: s16 relative scope, s16 relative track |
| `FILL` filler | Clip | `01` |
| `PRCL` parameter clip (keyframes) | Clip | `01`: s32 interpolation, s16 value type (1 int, 2 double, 4 reference), s32 count, points: s32 num, s32 den, s32 time scale, the value, s16 count of point properties (s16 code, s16 type, a double or an s32). Ext 01: 71 extrapolation; 02: 71 fields |
| `CTRL` control clip | Clip | `03`: s32 interpolation, s32 count, points: offset (num, den), time scale, a Boolean (always 1), value (num, den), s16 count of properties (s16 code, num, den) |
| `SEQU` sequence | Component | `03`: u32 count, references to the components |
| `TRKG` track group | Component | `08`: u8 multicam mode, s32 length, s32 scalars, s32 track count, tracks: u16 flags saying which fields follow (label s16, attributes, session attributes, component, filler proxy, bob, control code s16, sub code s16, start s32, read only; 0xFC00 unknown, an error). Ext 01: 69 lock number for each track. No end tag |
| `TKFX` track effect | Track group | `06`: s32 left, right length, s16 version, s32 current, s32 smooth, s16 colour item, s16 quality, s8 reversed, aspect on, ref key frames, force software, never hardware. Ext 02: 72 tracker manager |
| `PVOL` pan and volume | Track effect | `05`: s32 level, s32 pan, three Booleans. Ext 01, 02: 71 separate gain, trim gain |
| `RSET` essence group | Track group | `01`. Ext 01: 71 type |
| `TNFX` transition | Track group | `01`: s32 cut point, then `05` and the effect info as TKFX. Ext 01: 72 tracker manager |
| Time warp (abstract) | Track group | `02`: s32 phase offset |
| `MASK` capture mask | Time warp | `01`: is double, u32 mask bits |
| `SPED` motion effect | Time warp | `03`: rate (num, den). Ext 01: 75 offset adjust; 02: 72 source parameter list; 03: 66 new calculation |
| `CMPO` mob | Track group | `02`: 8 bytes (old MobID), u32 last modified, u8 mob type (1 composition, 2 master, 3 source), s32 usage code, ref descriptor. Ext 01: 71 creation time; 02: the MobID |
| `MDES` media descriptor | | `03`: u8 mob kind, ref locator, intermediate, ref physical media. Ext 01: 65 s32(16) UUID; 02: 65 UTF-16 bytes; 03: 72 attributes |
| `MDFL` media file descriptor | MDES | `03`: edit rate, s32 length, s16 is OMFI, s32 data offset |
| `PCMA` PCM audio | MDFL | `01`: channels, bits, sample rate, the format; ext 01: 77 EBU time stamp; 03: 76 timecode rate (a string) |
| `DIDD` picture descriptor | MDFL | `02`: stored, sampled and display sizes and offsets, frame layout, aspect ratio, line map (s32 byte size, s32 each), alpha, uniformness, image size, ref next descriptor, compression fourcc, resolution ID, alignment. Ext 01-06 frame index and offsets, 08 valid/essence/source boxes, 09 framing box and reformatting, 10-12 transfer, primaries, coding equations, compression UUIDs, 14 element size kind, 15 checked with mapper |
| `CDCI` component video | DIDD | `02`: u32 horizontal and vertical subsampling, s32 component width, s16 siting, u32 black, white, range, s64 frame index offset. Ext 01, 02: 72 alpha sampled width, ignore levels |
| `RGBA` RGB video | DIDD | `01`: pixel layout (u32 size, codes; u32 size, sizes), three empty palette sizes. Ext 01: 77 frame index offset; 02, 03: reference levels |
| `FILE`, `WINF` file locators | | `02`: path. Ext 01: 76 POSIX path; 02, 03: 76 UTF-8 paths |
| `URLL` URL locator | | only `03` |
| `MSML` media locator | | `02`: 8 bytes (old MobID), last known volume. Ext 01: 71 domain type; 02: the MobID; 03: 76 the volume in UTF-8 |
| `GRFX` graphic | | `01`: s32 size, picture data |
| `FXPS` effect parameters | | `12`: s32 original length, window offset, count, key frame size; each key frame's fixed parameters (crop, scale, box, borders, spill, colours, user bytes) |
| `PRIT` parameter | | `02`: UUID, s16 value type and the value (1 s32, 2 double, 4 ref), name, enabled, ref control track. Ext 01: 66 contributes to signature |
| `PRLS` parameter list | | `01`: s32 count, references |
| `MDTP` tape descriptor | MDES | `02`: s16 cframe (pyavb's name; meaning unknown) |
| `MCBR` bin reference | | `01`: s32 bin ID high, low, name. Ext 01: 76 the name in UTF-8 |
| `ATTR` attributes | | `01`: u32 count; each u32 type, name, value (1 s32, 2 string, 3 ref, 4 u32 size and bytes). A name met twice keeps its first place and its last value |
| `BVst` bin view | | `06`: name, kind, s16 attribute count and type, ref attributes; then `0a`: u16 count, columns (title, s16 format, s16 type, hidden). Ext 01: 69 count, each 69 column ID, 71 size, 76, 4 bytes, the UTF-8 format descriptor |
| `ABIN` bin | | `0e` (u16 item count) or `0f` (u32, a large bin): ref view setting, u64 ID, items (ref mob, s16 x, s16 y, s32 key frame, user placed), s32 display mask, s16 display mode, sifted and six sift settings (s16 method, string, column), s16 count of sort columns (u8 direction, column), s16 font, size, image scale, rect (s16 1, four s16), two colours (s16 1, three u16), s16 QuickLook scale, ref attributes, was iconic |

Classes pyavb reads that the corpus does not hold (markers `TMBC`, audio
suite `ASPI`, equalizer `EQMB`, selectors `SLCT`, film descriptors,
MPEG and JPEG descriptors, trackers, positions, `BINF`...) are not read yet:
their objects come back with the error "class not read", until bins that hold
them can check a port. The tape descriptor (`MDTP`) is the exception: the
export writes tape sources with it (3.4b), so it is read and written, and
pyavb reads the bins that hold it as Cutthulhu does (ctest
`avb_write_pyavb`).

## Import (3.3b)

`avb::importFile` reads a bin's mobs into bin items (native-model.md);
`cutthulhu --import-avb BIN.avb` prints the bin they make as a structure
dump (structure.md). The rules are the AAF import's (aaf.md, "Import"), so
that a bin and an AAF export of the same material read into the same items;
the helpers both use are in `src/interop/import_common.h`.

### The bin's mobs

The bin (ABIN) lists its mobs (CMPO) as items, each with a user-placed flag
(pyavb's `user_placed`). Every mob the bin holds is listed, also the ones
it holds only because a placed one refers to them (a sequence's title clip,
the source mobs). By mob type (`mob_type_id`): master mobs (2) and
composition mobs (1) become items; source mobs (3) are not items but their
clips' sources. An item listed as not placed keeps `userPlaced` false: a
reference clip, shown with Set Bin Display's *Show reference clips*
(Editing Guide p.288). A mob listed twice is one item.

### Clips

* **Master mobs** become master clips, or audio clips when they have no
  picture track. Name, MobID, Created (`creation_time`) and Modified
  (`last_modified`) come from the mob, and so does the edit rate. Tracks
  are the picture (media kind 1) and sound (2) tracks, numbered by their
  label (`index`). The duration is the longest track of the first track's
  kind (picture first). A sequence's length is its components', less its
  transitions', as pyavb computes it.
* **Rates** are stored as doubles (23.976); whole rates are read as they
  are, and others as the exact NTSC rate within 1/1000 (24000/1001), as for
  AAF.
* **The source chain.** Each track's first source clip (SCLP, down
  sequences and the tracks of track groups such as essence groups) names a
  file source mob: one whose descriptor is a media file descriptor (MDFL
  and its subclasses: CDCI, RGBA, PCMA, ...). Its locator is a media
  locator (MSML) for managed media, which names its file by MobID; the
  descriptor's physical media (an MDES) may give its last known path with
  a file locator ("Avid MediaFiles/MXF/1/....mxf", as in test_file_01.avb),
  which is then the File source's path, else it has none. The file mob's track (by the clip's `track_id` and
  media kind) names the mob before it: a Tape source (descriptor MDTP; also
  the Tape column) or an Import source (a plain MDES, the file a clip was
  imported or linked from, with its file locator's path: UTF-8, else POSIX,
  else as stored). A source clip whose MobID's material number is zero ends
  the chain.
* **Start and End** come from the first timecode (TCCP) down that chain
  (also as the first track of a capture mask or the first component of a
  sequence), moved by the source clips' start times; a timecode at another
  frame rate is scaled to the clip's, as for AAF. Bit 0 of a timecode's
  flags is drop frame: it is set only at 30 fps in the corpus, where an
  hour starts at frame 107892 (01:00:00;00).
* **Bin columns.** The mob's attributes hold `_USER`, the user columns by
  heading. Those with a value are the item's; those that are not standard
  headings are the bin's custom columns (Media Composer's UNC Path is one).
* **Rendered effects.** A master mob whose file mobs are used as "file mob
  with a precompute" (usage code 9) is left out, as the AAF import leaves
  out the same clips. Components' `precomputed` references are not read.
  A master mob used as "precompute" (1), as a title's clip is, is a master
  clip.
* **Subclips** are composition mobs used as subclips (usage code 2): a
  subclip with a Clip source naming its master and where it starts.
* Other composition mob usages (effect, group, motion, ...) are not
  imported, with a warning.

### Sequences

Composition mobs with usage code 0 become sequences, at the mob's edit rate.

* **Tracks.** The first timecode track gives the sequence its TC1 track and
  its Start. Media Composer keeps a track for each video track (V1, V2,
  ...), unlike AAF's nested scope, and the import names them by label,
  video first.
* **Segments.** A source clip is a segment: its clip by MobID (the item
  made from the same bin), the clip's track (the source clip's media kind
  and `track_id`), its `start_time` as the IN point, and its length, in the
  sequence's edit rate as the bin stores its components. Filler, and a
  source clip with no source, are filler; anything else that is not
  material reads as filler with a warning.
* **Transitions** (TNFX) are placed at the cut as for AAF: one of length L
  with its cut point at c shortens the segment before by L − c and starts
  the one after c frames later.
* **Track groups** (TKFX effects and their subclasses, SPED motion effects,
  RSET essence groups, MASK capture masks) apply to the segments of their
  first track that has material, fitted to the group's length (a motion
  effect's input runs at its own speed). The effect is the group's effect
  ID. The model keeps one effect a segment, so where effects are nested the
  innermost is kept: the motion effect inside Media Composer's spatial
  adapter (INT_EFF_SPATIAL_ADAPTER), where the AAF export names the
  adapter. Dissolves (EFF2_BLEND_DISSOLVE, BLEND_AUDIO_DISSOLVE) are
  Blend/Dissolve; other effects keep their effect IDs in the category "AVB",
  until effects are matched to the Effect Palette's (Phase 6). Audio Pan
  Volume (PVOL) is clip gain and pan, not an effect; its values are not read
  yet.
* A segment whose clip the bin does not hold keeps its MobID and name and
  is offline.

### Left out, with a warning

What is read only in part comes back as warnings, once each with a count:
edgecode and data tracks, timecode tracks after the first (Media
Composer's sequences carry several: eight in the corpus), clip gain and
pan, nested effects, objects the reader could not read (by class: MDTP and the other classes not read
yet, or damage), and the `_COLOR_R`, `_COLOR_G`, `_COLOR_B` attributes.
These are on one sequence of the corpus (16 bits each) and may be its clip
colour (p.264), but nothing seen confirms it, so clip colours are not read
until a bin shows what they are. The bin's view (its columns, sort and
sift) is not read either: an opened bin starts with the default display
and the bin's custom columns.

### In the application

File > Open Bin opens "a bin from another project" (Editing Guide p.296:
"Bins have the file name extension .avb"; it "appears in the Bins list in a
folder called Other Bins"). Cutthulhu's bins are its own files, and the
import does not read all a Media Composer bin holds, so an `.avb` is read
once into a bin of ours (not written back: "Export" below), `<project>/Other Bins/<key>/<name>.cthbin` (the key
is made from the `.avb`'s path, so that bins of the same name from
different projects do not meet), and that bin is listed under Other Bins and
opened (`MainWindow::openBinFile`). Master clips whose MobID the media
database knows come online, as with AAF import (p.547). Opening the same
`.avb` again while it is listed opens the bin made before, with whatever was
changed in it since; deleting it from Other Bins removes only the pointer
(p.296), and opening the `.avb` after that reads it again. The import's
warnings go to the Console.

### Checks

* ctest `avb_import_bins` (`interop-corpus.py check-avb-import`): each
  corpus bin that has a reference dump in `tests/data/aaf_reference` (made
  with pyavb by `scripts/avb-structure.py`) reads into the same bin by
  `structdiff.py --no-effects`. It needs only the corpus, not pyavb. The
  thirteen bins are the same; the AAF exports of the same material are too
  (`aaf_import_bins`), so the two readers agree on everything but the
  effects' names.
* tst_avb: `importsBin` (test_file_01.avb: item kinds, a source chain with
  its import path and drop-frame timecode, columns, a reference clip, the
  sequence's segments, transitions and effects, warnings), `importsRetime`
  (a motion effect in a 23.976 sequence) and `importsDamaged` (100 damaged
  bins import or are refused without a crash).
* tst_main_window (`openBinReadsAMediaComposerBin`): File > Open Bin of a
  corpus bin lists it under Other Bins, its sequence names its clips there,
  and opening it again opens the same bin.

## Writing (3.4a)

`avb::Builder` (`src/interop/avb_writer.h`) writes a bin: the header
(`avb::Header`, which `File::header()` gives for a bin read) and the object
table, in the order objects are added. Objects are the reader's
(`avb::Object`: a class ID and properties by pyavb's names; ATTR's
attributes as properties; PRLS's references as items), references are
`Value::ref(index)` into the table being written, so `add()` returns each
object's index at once and a reference may point forward. The property
names and value kinds the reader gives are the contract between the two.
Each class's write is ported from pyavb's `write()` methods and is the
inverse of its read, part by part:

* Properties a class always writes and the builder was not given are zero,
  empty or no reference; an extension is written only when its property is
  set (SCLP's and CMPO's `mob_id`, CMPO's `creation_time`, a component's
  `param_list`, DIDD's extensions in tag order...). TRKG writes its tracks'
  lock numbers (extension 01) whenever it has tracks, 0 for a track without
  one, and each track's flags from which of its fields are set. MCBR writes
  `name_utf8` only when set (pyavb always writes it).
* What pyavb's reader skips is written as pyavb writes it: the old 8-byte
  MobIDs of SCLP, CMPO and MSML from the MobID's material number (Data1,
  then Data2 + Data3 << 16), the unused bytes of TCCP and ECCP as zeros,
  BVst's format descriptor size as s16(size + 2), s16(0). A component's
  name or effect ID, and PRIT's name, that is empty is size 0xFFFF; other
  empty strings are size 0. UTF-8 strings start with two NULs. The creator
  is a 30-byte field padded with spaces. Edit and sample rates are
  multiplied by ten until whole (at most six times), and 59.94 is written
  59940e-3, as pyavb does.
* Values are checked against their fields: an integer out of range, a value
  of the wrong kind, a reference past the table, a string Mac OS Roman
  cannot hold or longer than 65534 bytes, a rate that is not finite or
  whose mantissa does not fit 32 bits make `toBytes()` fail with the
  object and the property named, never a bad file.

`avb::rewrite(file)` writes every object of a bin read back as read, with
its header and root; objects that could not be read (a class not read yet,
damage) are copied as they were stored, which keeps the file's byte order.
Given the other byte order, it writes the bin big-endian (or back), when
every object was read. `cutthulhu --rewrite-avb IN OUT` runs it.

What is and is not round-tripped: all 14 corpus bins come back the same
files, byte for byte, with no normalisation; pyavb's writer does the same
for their objects, all but the header's 4 unknown bytes, which pyavb writes
as zeros and Cutthulhu keeps. What the reader keeps cannot represent a
few forms no bin seen uses, which would be written in pyavb's form: a
Boolean byte other than 0 or 1 (written 0), a rate stored with a needless
power of ten (2500e-2 for 25 is written 25e0), a string with NULs at
either end or an empty one stored with size 0xFFFF where 0 is written (or
the reverse), a name given twice in an ATTR (one is kept), DIDD's line map
size when not a multiple of 4, extensions out of order or repeated, and
unused bytes that are not zero. Bins that hold them would be compared
after that normalisation; none is needed yet.

### Checks

* ctest `avb_round_trip` (`interop-corpus.py check-avb-rewrite`): every
  corpus bin through `--rewrite-avb` is the same file. Needs only the
  corpus.
* ctest `avb_write_pyavb` (`interop-corpus.py check-avb-write`, skipped
  without pyavb or the corpus): tst_avb's `forPyavb` writes a bin built
  from scratch (an ABIN listing a master clip CMPO whose picture track's
  source clip names a source mob with an MDES descriptor and FILE locator,
  user columns in ATTR, a BVst view), little- and big-endian, and every
  corpus bin written big-endian. pyavb reads the bin from scratch as
  Cutthulhu does (the same canonical dump) and finds the clip, its source
  clip, source mob and user column; it reads each big-endian bin as it
  reads the original.
* tst_avb: `rewritesCorpus` (test_file_01.avb and a retimes bin written
  back byte for byte, big-endian and back again), `buildsBin` (the bin from
  scratch, both byte orders: every property reads back as given, the
  import reads its master clip, and it writes back to the same bytes),
  `editRates`, `writerRefuses` (values out of range or of the wrong kind,
  bad references, class IDs and header fields are errors) and
  `rewritesDamaged` (200 damaged bins written again without a crash).

### For 3.4b

The export ("Export", below) builds the objects the import reads with this
builder.

## Export (3.4b)

`avb::exportBin` (`src/interop/avb_export.h`) writes a bin of the native
model as a Media Composer bin; `cutthulhu --export-avb <bin.cthbin | bin.avb>
OUT.avb` runs it. An `.avb` is imported first: a round trip. A `.cthbin`
finds the clips its sequences use in the other bins of its folder (the
project's). The export is the import's inverse, laid out as Media
Composer's own bins are (the corpus) and as the AAF export lays out the
same mobs (aaf.md, "Export"): what the import reads, the export writes, and
reading the result back gives the same bin.

### What maps

* **The bin** (ABIN, the root, last in the table): its mobs as items, the
  user's items with `user_placed` set, then the reference clips and the
  source mobs without it; positions -30000, -30000 as in every bin seen.
  The display values every corpus bin has (font 139 size 11, image scales,
  home rectangle, foreground colour, display mask 98999, six empty sift
  settings); the background colour is the bin's (p.263, as 16-bit
  channels). Its ID is made from the export time and the bin's name. The
  header names the creator ("Cutthulhu 0.x") and the time of the export.
* **The bin view** (BVst): Media Composer's columns as the corpus lists
  them (title, format and column type; the media resolutions' columns,
  whose types differ from bin to bin, are left out). The icon, colour and
  Name columns, then the bin's Text view headings, in their order, are
  shown; the rest are hidden. Headings are matched without regard to case
  (our "Plug-In" is its "Plug-in"). The view's name is the bin's
  (Untitled when it has none).
* **Master and audio clips**: a master mob (CMPO, mob type 2) with a track
  for each of its Tracks (labelled by number, with the filler proxy Media
  Composer gives every track), each a source clip naming the track of the
  same number of its file source mob. File source mobs (mob type 3), one
  for each file source MobID, with the tracks it carries: a CDCI
  descriptor for picture (8-bit 4:2:2, the size from the Format column, as
  the AAF export), PCMA for sound (one channel, 24-bit, 48 kHz). Managed
  media (a path in `Avid MediaFiles/MXF/`, or none) is named by MobID with
  an MSML locator, its path kept as the physical media's file locator, as
  Media Composer's bins have it; a linked file has a file locator (FILE:
  the path, its POSIX form, and UTF-8, which the import reads first). Each
  file's tracks name the source before it (`from`): a tape source mob (an
  MDTP descriptor, mob kind 2 as in pyavb's test_create.py) or an import
  source mob (MDES, mob kind 5 as in the corpus, with the file's locator).
  Its tracks end the chain (a source clip with no MobID), and its timecode
  track (TCCP) gives the clip its Start. A clip with a Start or a Tape
  column but no tape or import source gets one made up (a tape named for
  the Tape column, else an import source named for the clip), so that they
  travel.
* **Source timecode.** The model keeps a tape's or import's start as text
  (`ClipSource::startTimecode`), without its frames per second. The TCCP's
  rate is the first (drop frame: 30, 60; else the clip's, 24, 25, 30, 50,
  60) at which the text reads as a timecode that gives the clip's Start as
  the import reckons it (`startAtRate`); without one, the clip's Start at
  its own rate, with a warning. So 00:00:30;00 behind a 24 fps clip is
  written at 30 fps drop frame, as Media Composer wrote it.
* **Subclips**: a composition mob used as a subclip (usage code 2), each
  track naming its master's track of the same number from the subclip's
  offset (its Clip source).
* **Sequences**: a composition mob (usage code 0) with a timecode track
  (TC1: its Start at the edit rate's nominal frames per second, drop frame
  as the Start is written) and a track for each video and audio track,
  labelled by number, each a SEQU between the zero-length fillers Media
  Composer puts at each end. Segments are source clips (the clip's MobID,
  its track by kind and number, the IN point) or filler. A transition goes
  back over its cut, as for AAF: the segment before is lengthened by the
  frames after the cut, the one after by the frames before it and starts
  that much earlier, and a TNFX of the transition's length, its cut point
  the frames before the cut, lies between them. A segment's effect is a
  track effect (TKFX) around its clip, or a motion effect (SPED, at speed
  1:1) for Media Composer's time warps (`EFF_TIMEWARP...`). The Export
  setting's Include options leave video or audio tracks out.
* **Effects** by effect ID: Blend/Dissolve is `EFF2_BLEND_DISSOLVE` on
  video and `BLEND_AUDIO_DISSOLVE` on audio; effects the import kept by
  their Media Composer effect IDs (category "AVB") get them back, so the
  corpus's effects round-trip.
* **Reference clips.** Clips the bin's sequences use that are not in it,
  and subclips' masters, are found through `ExportOptions::resolve` (the
  project's other bins) and written as reference clips (not user placed,
  p.288), as Media Composer's bins hold the clips their sequences use.
  Without them the segments name the clips by MobID and are offline where
  the bin is read (a warning with the count).
* **Columns**: the clips' `_USER` attributes hold the custom columns and
  the standard headings that can be typed into (p.274-278: Comments, Take,
  ...), as text. Start, End, Tracks and Tape travel in the bin's
  structure.
* **Names, MobIDs and dates.** Every mob keeps the item's MobID (or the one
  derived from its id, native-model.md), name, Created (`creation_time`)
  and Modified (`last_modified`), to the second. Source MobIDs the model
  lacks are derived from the item's id, so they are the same on every
  export.
* **Text Mac OS Roman cannot hold.** Mob and component names, attribute
  names and values, and view names are Mac OS Roman, and their classes
  have no UTF-8 form (pyavb reads none). Characters Mac OS Roman cannot
  hold (and UTF-16 surrogate pairs) are written as `?`, with one warning.
  File paths keep every character: the file locator's UTF-8 path is
  written too.

### What does not map (left out, a warning for each kind)

Items that are not clips, subclips or sequences (groups, effects, titles,
...); clip colours (the `_COLOR_*` attributes are not confirmed, see
"Import"); Lock Bin Selection; markers (`TMBC` is not read yet); the
Script view's text; standard columns Media Composer works out from the
media (Video, Format, FPS, ...: one warning listing them); a Tape column on
a clip imported from a file; data tracks; stereo and surround audio tracks
(written as mono tracks); clip gain and pan (PVOL's values are not read);
effects and transitions with no Media Composer effect ID (the Effect
Palette's until Phase 6, AAF's); effect parameters; motion effects' speeds
(the model keeps none); transitions at the start of a track; add edits in
filler (the import joins filler); the bin's sort, sift, Frame view
positions, and the Frame or Script view (written as the Text view).

### In the application

The Editing Guide gives no command that writes a bin for another system:
Media Composer's bins are `.avb` files already, copied or opened from
another project (File > Open Bin, p.296), and File > Output > Export Bin
writes shot logs (p.130). So the entry point is ours, the smallest that
fits: File > Output > Export to File (p.868-869) with the Export setting's
Export As at **Media Composer Bin**, a choice added to the guide's list
(p.1173). The selection is written as a bin named for the file, with the
source bin's view and custom columns; the clips its sequences use and the
subclips' masters that are not selected go in as reference clips. The file
name ends in `.avb` (p.869: the extension stays the format's). Warnings go
to the Console. Writing an opened `.avb` back to its own file was not
chosen: the import does not read everything a Media Composer bin holds
(effect parameters, clip gain, markers...), so writing over it would lose
them.

### Checks

* ctest `avb_export_round_trip` (`interop-corpus.py check-avb-export`):
  each corpus bin with a reference dump, read (`--import-avb`), written
  (`--export-avb`) and read again, is the same bin (`structdiff.py --fields
  --sources`: items, columns, standard fields, source chains, sequences,
  effect and transition names). Needs only the corpus. All 13 are the
  same.
* ctest `avb_export_pyavb` (`interop-corpus.py check-avb-export-pyavb`,
  skipped without pyavb or the corpus): pyavb opens each bin written,
  reads every object as Cutthulhu does (`avb-dump.py --compare`), and the
  bin it reads (`avb-structure.py`) is the one exported, as Cutthulhu read
  it from the corpus (`structdiff.py --no-effects`, because
  avb-structure.py does not name effects; the round trip above checks
  them). All 13 pass.
* ctest `avb_write_pyavb` also writes tst_avb's exported bin (below) in
  both byte orders: pyavb reads it as Cutthulhu does and as the bin
  Cutthulhu reads back, with the tape descriptor and subclip the corpus has
  none of.
* tst_avb: `exportsBin` (a bin in the application's terms: a linked clip, a
  managed clip from a tape, a subclip, a sequence with filler, a centred
  dissolve, an effect and a clip from another bin; in both byte orders:
  only the expected warning, every object read in full and written back to
  the same bytes, and the import gives the same structure, dates,
  user-placed flags and source chains with their paths) and `exportsNames`
  (text Mac OS Roman cannot hold, and what has no place in a bin, with
  their warnings).
* tst_managed_media (`exportAsMediaComposerBin`): media imported, cut into
  a sequence, the sequence exported through the dialog as a bin, and that
  file opened with File > Open Bin: the clip is a reference clip, online by
  MobID, and the sequence names it.

## Dumps

`cutthulhu --dump-avb FILE` prints the root object as JSON (`avb::dump`)
with every object it refers to expanded in place; `scripts/avb-dump.py`
prints the same form from pyavb's reading. With `--canonical` both print
compact, sorted JSON (`aaf::canonicalJson`, Python's `json.dumps(...,
sort_keys=True, separators=(",", ":"))`), byte for byte the same when the
readers agree:

* an object is `{"class": "CMPO", "properties": {...}}`; a parameter list
  `{"class": "PRLS", "items": [...]}`; an unreadable object (Cutthulhu
  only) `{"class": ..., "error": ...}`;
* a reference is the object, `null` for none, `{"ref": index}` for an object
  already being dumped further up the same path (a cycle);
* MobIDs as their URN (`urn:smpte:umid:...`, as pyavb's `str()` gives), UUIDs
  as lowercase hyphenated text, times as seconds since 1970 (pyavb's local
  datetimes are read as UTC for this), bytes as `{"bytes": hex}`;
* floating-point numbers as `{"float": text}` with Python's `repr()` text
  (`avb::pythonRepr`), and integers outside the signed 64-bit range as
  `{"int": text}`, so that neither side's JSON number writer matters.
  Edit rates are computed as pyavb does (`float(mantissa) * pow(10, exp)`),
  so they are the same doubles.

### Checks

* ctest `avb_pyavb` (`interop-corpus.py check-avb`, skipped without pyavb):
  `avb-dump.py --compare` on all 14 corpus bins; each must be the same, also
  byte for byte.
* tst_avb (`corpusDumps`): every corpus bin reads with no object in error and
  dumps to the digest of pyavb's dump (`tests/data/interop_corpus.json`,
  "avb", made by `interop-corpus.py manifest`); `objects`: the API on
  `test_file_01.avb` (2,190 objects: class counts, the bin's view and 160
  items, a master mob's name, MobID and edit rate); `damaged`: 200 randomly
  damaged or cut bins open or are refused, and dump, without a crash or a
  hang; `strings`, `floats`: Mac OS Roman, strict UTF-8, Python's float text.
