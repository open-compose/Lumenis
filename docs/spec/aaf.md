# AAF (Phase 3.2)

AAF is the interchange format Media Composer imports and exports sequences,
clips and their sources with. Cutthulhu reads AAF into the native model
(native-model.md) and writes it from there.

Sources:
* [MS-CFB], Microsoft's public Compound File Binary File Format
  specification: the container.
* The AAF Low-Level Container Specification and the AAF Object
  Specification (AMWA, public): how objects and properties are stored in
  the container, and what the objects are.
* pyaaf2 (MIT, Mark Reid), a complete AAF reader and writer in Python: the
  reference implementation, read for how files from Media Composer are laid
  out in practice. Credited in THIRD-PARTY-NOTICES.md.
* The test files of pyaaf2 and pyavb, made with Media Composer: the first
  corpus (`scripts/interop-corpus.py`).

Avid's AAF SDK is not used: its licence is doubtful for us (ROADMAP §3).

## Layers

| Layer | Where | Status |
|---|---|---|
| Compound file: storages and streams in one file | `src/interop/cfb.*` | 3.2a, done |
| AAF objects: properties, types, the metadictionary | `src/interop/aaf*` | 3.2b, done |
| Import into bins: mobs to clips, sources and sequences | `src/interop/aaf_import.*` | 3.2c, done |
| Export and round trips | `src/interop/aaf_writer.*`, `aaf_export.*` | 3.2d, done |
| File > Import and Export, from the Editing Guide | `main_window.cpp`, `export_dialog.*` | 3.2e, done |

## The compound file (3.2a)

A compound file is a small file system inside one file. Its header (512
bytes) gives the sector size, where the directory starts, and the first 109
entries of the DIFAT, the list of the sectors that hold the FAT. The FAT
chains sectors together: entry n gives the sector after sector n in its
chain. Sector n starts at byte (n + 1) × the sector size.

* **Versions.** Version 3 files have 512-byte sectors and version 4 files
  4096-byte sectors. Both occur in AAF: the corpus files are version 4 but
  for one version 3 file (`sector_size_512.aaf`). The
  reader takes both, and nothing else.
* **The directory** is a chain of 128-byte entries: a name (up to 31 UTF-16
  code units), a type (storage, stream, or the root storage), a class ID,
  times, the first sector and the size. Each storage's children form a
  binary tree ordered by name: shorter names first, then by upper-cased
  code unit, so names are found without regard to case.
* **Small streams.** Streams under 4096 bytes are kept in 64-byte mini
  sectors inside the mini stream, which is the root entry's own chain. The
  mini FAT chains mini sectors.
* **Large files.** More than 109 FAT sectors are listed in DIFAT sectors,
  each holding one sector fewer than it can, with the last entry pointing to
  the next. In files past 2 GB with 4096-byte sectors, the sector holding
  byte 0x7FFFFF00 (the range lock sector) is kept out of every chain.

### The reader (`cfb::Reader`)

* Opening reads the header, the FAT, the mini FAT and the directory. Streams
  are read on demand, in runs of consecutive sectors, so a file larger than
  memory (AAF with embedded media) can be read.
* Version 3 files may hold anything in the high half of a stream's size; it
  is ignored.
* It is made for files from anywhere, damaged ones included. A chain that
  leaves the FAT or loops, a directory tree that loops or shares a child, a
  sector past the end of the file: each is an error naming what is broken,
  never a crash or a hang. A last sector cut short reads as zeros.
  `tst_cfb` (`damaged`) checks this, also with random damage. (Gate 4's
  fuzzing builds on it.)

### The writer (`cfb::Writer`)

Storages and streams are added as a tree, then the whole file is written at
once:
1. the sectors of streams of 4096 bytes or more, in the order they were
   added, each in one run;
2. the mini stream, the mini FAT and the directory;
3. the FAT and, when needed, DIFAT sectors.

Siblings are written as a balanced binary tree with every node black, which
[MS-CFB] permits (2.6.4); olefile reads it (ctest `cfb_olefile`). Streams carry no times, and the root no
creation time. With 512-byte sectors, streams are limited to 4 GB.

### Checks

* `tst_cfb` writes files with every edge (empty streams, the 64-byte and
  4096-byte boundaries, hundreds of siblings, DIFAT sectors) and reads them
  back.
* `corpus`: every AAF file in the corpus reads with the same storages and
  streams, and the same bytes in each stream, as olefile reads
  (`tests/data/interop_corpus.json`, made with `interop-corpus.py
  manifest`). Each is then copied entry by entry through the writer and read
  back the same.
* ctest `cfb_olefile`: olefile reads what the writer makes (skipped without
  olefile: `pip install olefile`).
* ctest `interop_corpus_fetch` downloads the corpus into
  `build/<preset>/interop-corpus` (or `$CUTTHULHU_INTEROP_CORPUS`) before
  `tst_cfb` runs. Without a network it is skipped, and so is `corpus`.

## Objects (3.2b)

An AAF file is a tree of objects. Each object is a storage of the compound
file: its class ID is the class's AUID, and its `properties` stream holds
its properties (`aaf::File`, `aaf::Object`).

* **The properties stream:** a byte order (0x4c, little-endian), a version,
  a count, then for each property its local ID (pid), stored form and size,
  then the values one after another.
* **Stored forms** (`aaf::StoredForm`): data, decoded by the property's
  type; a strong reference, the name of the child storage holding the
  object; a strong reference vector or set, the name of an index stream
  (`<name> index`) listing the children `<name>{<local key in hex>}` (a
  set's index also gives each element's key); a weak reference, an index in
  the file's `referenced properties` table (the path of pids from the root
  to the set it points into) and the key of its target; weak reference
  vectors and sets, an index stream of keys; a stream, a byte order (0x55)
  and the name of a stream next to `properties`.
* **AUIDs** are stored as GUIDs, with the first three fields
  little-endian. **MobIDs** keep the UMID's first 16 bytes; the material
  number is stored as a GUID. When the material number is a SMPTE label
  (Media Composer's MobIDs: the label's last byte 00, the number starting
  `06 0e 2b 34 7f 7f`), its two halves trade places. `aaf::mobIdFromBytes`
  makes the UMID's own order, as `MobId` and bmx print it; pyaaf2's URNs
  are the same.

### The metadictionary

The root object (class `Root`) has two strong references: the Header and
the MetaDictionary. The MetaDictionary holds the definitions of the classes
(each with its parent class and its properties: name, AUID, pid, type,
optional, unique) and of the types the file uses. It is not complete: the
corpus files define about 150 types of the 190 AAF and Avid use. So the
reader has the AAF baseline and Avid's extensions built in
(`src/interop/aaf_model.inc`, made by `scripts/make-aaf-model.py` from
pyaaf2's model tables) and adds what each file defines. A file's own
definitions win: its names, and the pids it gives extension properties,
which have none of their own (dynamic pids, 0x8000 and up).

Types decode as `File::decode` describes: integers, Booleans and other
enumerations (by element name), extensible enumerations (by name, else the
AUID), strings, records (AUID, MobIDType, or a map of members), fixed and
variable arrays, sets, renamed types, and indirect values (a byte order,
the AUID of the type, the value). A value that does not fit its type comes
back as its bytes.

### Dumps

`cutthulhu --dump-aaf FILE` prints every object below the Header with every
property as JSON (`aaf::dump`). `scripts/aaf-dump.py` prints the same form
from pyaaf2's reading, so the two readers are compared on every file:
`aaf-dump.py --compare --app cutthulhu FILE...`. With `--canonical` both
print compact, sorted JSON, byte for byte the same when they agree.

* tst_aaf (`corpusDumps`): every corpus AAF dumps to the digest pyaaf2's
  dump has (`tests/data/interop_corpus.json`). For `test_file_01.aaf`,
  exported from Media Composer 6.5, that is 2,706 objects with nothing left
  undecoded. `objects` checks the API on it: mobs by class, names, MobIDs,
  edit rates, and a weak reference resolved to its data definition.
* ctest `aaf_pyaaf2` runs the comparison itself when pyaaf2 is installed
  (`pip install pyaaf2`), and prints each difference.
* `damaged`: 200 randomly damaged files open or are refused, and dump,
  without a crash or a hang.

## Import (3.2c)

`aaf::importFile` reads an AAF file's mobs into bin items
(native-model.md); `cutthulhu --import-aaf FILE` prints the bin they make
as a structure dump (structure.md).

### Clips

* **Master mobs** become master clips, or audio clips when they have no
  picture. Name, MobID, Created and Modified come from the mob. Each
  picture and sound slot is a track: its kind from its segment's data
  definition (AAF's labels and the older ones Avid writes), its number from
  the slot's PhysicalTrackNumber. The edit rate and duration are the first
  picture slot's.
* **NTSC rates.** Media Composer writes 23.976 as 23976/1000; the import
  reads it, and every rate within 1/1000 of n×1000/1001, as the exact rate
  (24000/1001), as its bins do.
* **The source chain.** Each track's source clip names a file source mob:
  a File source with the media's path (the descriptor's locator). The file
  mob's own source clip names the tape or import source mob before it: a
  Tape source (TapeDescriptor; also the Tape column) or an Import source
  (ImportDescriptor, with the original file's path), each with its own
  start timecode.
* **Start and End** come from the first timecode down that chain (inside a
  Pulldown if need be), moved by the source clips' start times. A timecode
  at another frame rate is scaled to the clip's (30 fps 00:00:30;00 is
  24 fps 00:00:30:00).
* **Bin columns** travel as the mob's user comments (Name, Value). Those
  that are not standard headings are the bin's custom columns.
* **Rendered effects.** Media Composer exports the clips it made for
  rendered effects as master mobs whose file mobs carry AppCode 9 ("file
  mob with a precompute" in pyavb). Its bins do not show them, and the
  import leaves them out.
* **Subclips** are composition mobs used as Usage_SubClip: a subclip with a
  Clip source naming its master and where it starts.

### Sequences

Composition mobs used as Usage_TopLevel (or with no usage) become
sequences.

* **Tracks.** A timecode slot gives the sequence its TC1 track and its
  Start. Sound slots are A tracks. Media Composer keeps all video tracks in
  one picture slot as a NestedScope: its slots are V1, V2 and up.
* **Segments.** A source clip is a segment: its clip (by MobID; the clip
  imported from the same file, in the same bin), the clip's track (the
  slot it names), its start time as the IN point, and its length. Filler,
  and a ScopeReference (the track below shows through), are filler.
* **Transitions** overlap the segments on either side in AAF; in a
  sequence they sit at the cut. A transition of length L with its cut
  point at c frames shortens the segment before by L − c and starts the one
  after c frames later. Its alignment follows c: 0 is Starting at Cut, L
  Ending at Cut, L/2 Centered on Cut, others Custom.
* **Effects** (OperationGroups) apply to the segments of their first input
  that has material. A timewarp's input runs at another speed, so the
  segments are fitted to the effect's length. Audio Gain and Audio Pan are
  clip gain and pan, not effects. Video and audio dissolves become
  Blend/Dissolve; other effects keep their AAF names, in the category
  "AAF", until effects are matched to the Effect Palette's (Phase 6).
* A segment whose clip the file does not hold keeps its MobID and name and
  is offline.

What was read only in part comes back as warnings, once each with a count.

### Checks

* ctest `aaf_import_bins`: each corpus AAF that has a Media Composer bin of
  the same material (`test_file_01` and the twelve `retimes` files), once
  imported, is the same as that bin by `structdiff.py --no-effects`: the
  same clips and counts, names, MobIDs, edit rates, durations, Start and
  End, custom columns, tracks and segments. The bins' dumps are made with
  pyavb (`scripts/avb-structure.py`, `interop-corpus.py reference`) and
  kept in `tests/data/aaf_reference`, so the check runs without pyavb.
  This is Phase 3 gate 1 on the corpus there is until Media Composer is
  at hand again. Effects are left out of it because the bin names them by
  Media Composer's effect IDs.
* tst_aaf (`importsBin`): what the dump does not show: sources and their
  paths, transitions' alignment, and segments naming their clips' items.

## Writing (3.2d)

`aaf::Builder` builds objects by class and property name and writes them
as one compound file (4096-byte sectors, version 4).

* **Values** are encoded by the property's type: the forms `File::decode`
  returns, so what is read can be written back. A value that does not fit
  its type, or a property the class does not have, is an error, and nothing
  is written.
* **Storages** are named as Media Composer and pyaaf2 name them: the
  property's name, "-" and its pid in hex (`Content-3b03`), squeezed in the
  middle to fit; the elements of a vector or set `<name>{<index in hex>}`,
  with the index stream `<name> index`. A set's index gives each element's
  key: the value of its class's unique property (a mob's MobID, a
  definition's AUID).
* **Weak references** are entered in the `referenced properties` table as
  the path of pids their type's target set names, with the key pid of the
  class they point to.
* **The metadictionary** defines every class and type of the built-in
  model, as pyaaf2's files do, so a reader needs nothing of its own.
  Extension properties get local IDs from 0xFFFF down, in a stable order.
* `Builder::missing()` lists required properties that were not set; export
  refuses to write a file with any.

## Export (3.2d)

`aaf::exportItems` writes bin items in the layout Media Composer's own
exports have, the import's rules reversed:

* **Master clips**: a master mob with a slot for each track (its Tracks
  column), each a source clip naming the track's file source mob. File mobs
  carry a descriptor (CDCI for picture, from the Format column; PCM for
  sound) with the media's path as a locator, and name the tape or import
  source mob before them. That mob has the clip's tracks and a timecode
  slot with the clip's Start. A clip with a Start but no tape or import
  source gets a tape source (named for its Tape column, or itself) so its
  Start travels. Columns that are not standard headings travel as user
  comments.
* **Subclips**: a composition mob used as Usage_SubClip, its slots naming
  the master's.
* **Sequences**: a composition mob used as Usage_TopLevel. TC1 is a
  timecode slot with the Start. All video tracks are the slots of one
  nested scope, V1 first, every number up to the highest; filler above V1
  is a ScopeReference (the track below shows through). Audio tracks are
  slots of their own. Transitions go back between the segments they
  overlap. Effects are OperationGroups around their segment's clip.
* **Effects**: dissolves are AAF's VideoDissolve and MonoAudioDissolve;
  others are operation definitions named "Category/Name", which the import
  reads back as that effect, until effects are matched to Media Composer's
  (Phase 6).
* Data tracks, and items that are neither clips nor sequences, are left
  out, with a warning.

`cutthulhu --export-aaf <bin.cthbin | file.aaf> --to OUT.aaf` exports a bin
(an AAF file is imported first: a round trip).

### Checks

* ctest `aaf_round_trip`: every corpus AAF, imported, exported and
  imported again, is the same (structdiff.py, effects included). With
  pyaaf2 installed, pyaaf2 also reads every export as Cutthulhu does
  (`aaf-dump.py --compare`), and finds every mob and definition valid (by
  hand). This is Cutthulhu's half of Phase 3 gate 3; the halves through
  Media Composer wait for its seat (ROADMAP gate status).
* tst_aaf (`exportRoundTrip`): a bin made in code (clips with sources and
  custom columns, a subclip, a sequence with two video tracks, filler, a
  centred dissolve and a segment effect) exports with no warnings and reads
  back the same, sources and dates included.

## In the application (3.2e)

The menus and dialogs are in ui-inventory.md §18, from the Editing Guide.

* **Import**: an `.aaf` file imported through the Source Browser
  (`MainWindow::importAaf`). Master clips find managed media by MobID: the
  media database knows each file's material package
  (`MediaFileInfo::material`, read from the MXF; its folder indexes are
  version 3 for this), so media made by Cutthulhu and exported with a
  sequence comes online in another project at once.
* **Export**: File > Output > Export to File (`MainWindow::exportToFile`,
  the Export As dialog in `export_dialog.*`) with the Export setting.
* tst_managed_media (`exportAndImportAaf`): media imported, cut into a
  sequence, exported from the bin through the dialog, imported into
  another bin: the clip online, the sequence naming it; then exported with
  Include All Audio Tracks off, and no audio tracks come back.
