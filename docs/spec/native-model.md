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

## Still to come in 3.1

* The source chain: a master clip's file sources (one per track, with the
  media's MobID), and the tape or import source before them, each with its
  own start timecode (AAF's `SourceMob` chain).
* Subclips as a reference to their master clip, with marks.
* A structural dump and diff of bins and projects (Phase 3 gate 1).
