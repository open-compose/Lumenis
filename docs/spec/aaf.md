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
| Import into bins: mobs to clips, sources and sequences | | 3.2c |
| Export and round trips | | 3.2d |
| File > Import and Export, from the Editing Guide | | 3.2e |

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
