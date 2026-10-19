# Media Composer bins (.avb) (Phase 3.3)

A Media Composer bin is one `.avb` file: its clips, subclips, sequences and
their sources, with the bin's view and settings. Cutthulhu reads bins into
the native model (native-model.md), the way AAF is read (aaf.md).

Sources:
* pyavb (MIT, Mark Reid), a reader and writer of bins in Python: the
  reference, read for the layout of every class (its `read()` methods).
  Credited in THIRD-PARTY-NOTICES.md ("Prior art for interchange").
* Black-box observation of bins made by Media Composer: the test files of
  pyavb (`scripts/interop-corpus.py`), 14 bins from Media Composer 18.9
  and 18.12.

No Avid SDK or documentation of the format is used: Avid publishes none.

## Layers

| Layer | Where | Status |
|---|---|---|
| Objects: the container and every class's properties | `src/interop/avb.*` | 3.3a, done |
| Import into bins: mobs to clips, sources and sequences | | 3.3b |

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
| 4 bytes | |
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
| `MCBR` bin reference | | `01`: s32 bin ID high, low, name. Ext 01: 76 the name in UTF-8 |
| `ATTR` attributes | | `01`: u32 count; each u32 type, name, value (1 s32, 2 string, 3 ref, 4 u32 size and bytes). A name met twice keeps its first place and its last value |
| `BVst` bin view | | `06`: name, kind, s16 attribute count and type, ref attributes; then `0a`: u16 count, columns (title, s16 format, s16 type, hidden). Ext 01: 69 count, each 69 column ID, 71 size, 76, 4 bytes, the UTF-8 format descriptor |
| `ABIN` bin | | `0e` (u16 item count) or `0f` (u32, a large bin): ref view setting, u64 ID, items (ref mob, s16 x, s16 y, s32 key frame, user placed), s32 display mask, s16 display mode, sifted and six sift settings (s16 method, string, column), s16 count of sort columns (u8 direction, column), s16 font, size, image scale, rect (s16 1, four s16), two colours (s16 1, three u16), s16 QuickLook scale, ref attributes, was iconic |

Classes pyavb reads that the corpus does not hold (markers `TMBC`, audio
suite `ASPI`, equalizer `EQMB`, selectors `SLCT`, tape and film descriptors,
MPEG and JPEG descriptors, trackers, positions, `BINF`...) are not read yet:
their objects come back with the error "class not read", until bins that hold
them can check a port.

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
