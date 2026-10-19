# Native model for interchange (Phase 3.1)

Cutthulhu's own projects and bins (`.cthbin`, JSON) are the model that every
interchange format reads into and writes from: AAF, Media Composer's `.avb`
bins and `.avp` projects, EDL, ALE, OTIO and FCP XML (ROADMAP Phase 3). This
page records what the model holds for that, and where each rule comes from.

Sources:
* SMPTE ST 330, Unique Material Identifier (UMID): the 32-byte basic UMID.
* The AAF Object Specification (AMWA, public): mobs, source clips and the
  source chain.
* Black-box observation of files: what bmx's `mxf2raw` prints for MXF we
  write (`scripts/check-mxf.py`).

## Identity: MobIDs

In AAF, and so in Media Composer's bins, every clip, sequence and source is a
*mob*. A mob is named by a MobID, a basic UMID (ST 330). References between
them use MobIDs: a sequence's source clip names the clip it uses by its MobID
(AAF `SourceClip::SourceID`).

`lumenis::MobId` (`src/core/mob_id.h`) is a basic UMID:

| Bytes | Value |
|---|---|
| 0-11 | universal label `06 0a 2b 34 01 01 01 01 01 01 0f 10` |
| 12 | `0x13`, the length of what follows |
| 13-15 | instance number, `00 00 00` for an original |
| 16-31 | material number |

Byte 10 is the material type: `0f` is "not identified", as AAF writers
commonly use. Byte 11 holds the two generation methods (ST 330). The high
nibble `1` means the material number is a UUID, and the low nibble `0`
means local registration of the instance number.
`MobId::generate()` uses a random version 4 UUID. The text form is
`urn:smpte:umid:` and 32 bytes in 8 dot-separated groups, as bmx prints
them. MobIDs read from elsewhere keep their own label bytes (FFmpeg's start
`060a2b34.01010105.01010d00.13...`).

### In bins

* `BinItem::mobId` is the clip's or sequence's MobID. When it is null, the
  MobID is derived from the item's id (`derivedMobId`). The derived MobID
  is a version 5 (SHA-1, name-based) UUID of the id in Cutthulhu's own
  namespace, so it is the same every time and different for every id. Bins
  saved before MobIDs existed (version 1) therefore get stable MobIDs
  without being rewritten. A MobID that came from elsewhere (AAF, a bin) is
  stored as it is.
* `mobIdOf(item)` is the item's MobID either way. Bin files (version 2)
  write it as `"mobId"`. One equal to the derived MobID is read back as
  null, so a bin reads back the same as it was written.
* A duplicate (Edit > Duplicate) is a new clip with a MobID of its own
  (`duplicateItem` clears it). The same clip in two bins (copied, not
  duplicated) is the same mob.

### In sequences

* `Segment::sourceMob` is the MobID of the clip the segment uses, from when
  it was edited in. When it is null, it is the MobID derived from `clipId`
  (`Segment::sourceMobId()`), which matches a clip without a MobID of its
  own. It is written as `"mob"` only when it is not derived.
* Whatever changes a segment's clip (Relink, Consolidate, the Timeline's
  view of a clip) changes its `sourceMob` with it.

`clipId` and `binId` still find the clip inside a project. The MobID is how
the clip is named outside the project, and how an imported sequence finds
the clips it uses.

## The source chain

`BinItem::sources` is a list of `ClipSource` entries: where the clip's
material came from, in the order of AAF's source mobs.

| Kind | What it is | Fields |
|---|---|---|
| File | a media file (AAF file source mob); one entry for each track it carries | `mob`, `track`, `name`, `path`, `from` |
| Tape | the tape it came from (tape source mob) | `mob`, `name` (the tape's), `startTimecode` |
| Import | the file it was imported or linked from (import source mob) | `mob`, `name`, `path`, `startTimecode` |
| Clip | a subclip's master clip | `mob` (the master's), `name`, `offset`, `startTimecode` |

`from` names the source before a file: its tape or import source. Clips made
before the source chain existed have none, and it is never required.

* **Linked clips** (`media::linkClip`): one file source, one MobID, listed
  for each track of the file, from an import source for the file with its
  start timecode. When the file is MXF, FFmpeg reports its
  `material_package_umid` and its stream's `file_package_umid`. The clip
  then takes the material package as its MobID and the file package as its
  file source, so a clip linked from Media Composer's media keeps Media
  Composer's MobIDs.
* **Managed media** (Import, Transcode, Consolidate; `media::transcodeClip`):
  the new master clip gets a new MobID, and each track's OP-Atom file gets a
  file source with a MobID of its own. They come from the original clip's
  tape or import source, which keeps its MobID; without one, a new one is
  made (a tape source when the clip has a Tape). The MXF carries the same
  MobIDs. The material package is the master clip in every track file, so
  Media Composer sees one clip and not one per file. The file package is the
  file's source. FFmpeg 6.1 ignores `material_package_umid` and writes random
  UMIDs that differ in every file: one stem per file, the material package
  ending `00` and the file package `01` (observed with bmx's mxf2raw).
  `MediaWriter` therefore replaces them in the header metadata after the
  trailer (`fixOpAtom`, `media.cpp`). `scripts/check-mxf.py --make` checks
  that a clip's files share their material package and each have their own
  file package.

## Subclips

"Subclips do not directly reference the original media. Subclips remain
linked to the master clips from which they are created" (Editing Guide
p.407). `model::subclipItem` and `media::makeSubclip` make one of frames
[in, out) of a master clip:
* Its name is "<master>.Sub.NN". The guide says "a numbered .Sub file name
  extension" (p.407), and its picture shows `RedSky.Sub.01`. NN counts on
  from the highest in the bin.
* It has one Clip source: the master's MobID, with `in` as the offset.
* Its Start and End are the master's, moved by `in`.
* Its media is the master's, from `in` on (`Link::first`), so it plays
  without the master being open.

Subclips made in 24p and 25p projects are "hard": they cannot be trimmed
past their edges (p.407). That belongs with trimming and is left for when
the Make Subclip command arrives.

## Still to come in 3.1

* A structural dump and diff of bins and projects (Phase 3 gate 1).
