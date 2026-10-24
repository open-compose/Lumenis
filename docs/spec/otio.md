# OpenTimelineIO (Phase 3.6b)

OpenTimelineIO (OTIO) is an open interchange format for editorial
timelines. Media Composer imports `.otio` files through File > Input >
Import Media, or by dropping one onto a bin; afterwards "it may be necessary
to relink media" (Relink > Managed Media). It writes them from the List
Tool, with the Output format "OTIO" and Save List. New users get an Active
Setting "Default OpenTimelineIO" (Editing Guide p.968-969). "Only the basic
features included in the OTIO specification are transferred, and only the
most simple effects, such as dissolve" (p.969).

Cutthulhu reads OTIO's JSON (the `.otio` file) into the native model
(native-model.md) and writes a sequence as one (`src/interop/otio.*`).

Sources:
* The Media Composer Editing Guide (2025), p.968-969: Import, export, the
  setting and what is transferred.
* OpenTimelineIO's published file format and schema documentation, and its
  source (Apache-2.0, github.com/AcademySoftwareFoundation/OpenTimelineIO):
  read for the schemas, their versions and how times and ranges combine. Its
  sample files are part of the interchange corpus
  (`scripts/interop-corpus.py`, source `otio`, not committed), and its Python
  package (`pip install opentimelineio`) is the reference reader in the
  checks. Credited in THIRD-PARTY-NOTICES.md.

## The format

A JSON document. Every object names its schema and version in
`"OTIO_SCHEMA": "Name.N"`.

* **Timeline.1**: `name`, `global_start_time` (a time: the sequence's start
  timecode) and `tracks`, a Stack.1.
* **Stack.1**: `children`, layered, the last one on top. The timeline's
  stack holds the tracks.
* **Track.1** (Sequence.1 in old files): `kind` (`Video` or `Audio`) and
  `children` played one after another: Clip, Gap, Transition, and nested
  Track or Stack.
* **Clip.1/Clip.2**: `source_range` and the media: Clip.1's
  `media_reference`, Clip.2's `media_references` map with
  `active_media_reference_key` (usually `DEFAULT_MEDIA`). A clip without a
  source range uses its media's `available_range`.
* **Media references**: ExternalReference.1 (`target_url`,
  `available_range`), MissingReference.1, GeneratorReference.1
  (`generator_kind`), ImageSequenceReference.1 (`target_url_base`,
  `name_prefix`, `name_suffix`, ...).
* **Gap.1**: `source_range` (its length).
* **Transition.1**: `transition_type` (`SMPTE_Dissolve`, or
  `Custom_Transition` with a name), `in_offset` (before the cut) and
  `out_offset` (after it). It sits between the two items it joins and takes
  no time in the track.
* **Effects** on an item: LinearTimeWarp.1 (`time_scalar`, the speed),
  FreezeFrame.1 (speed 0), others by `effect_name`.
* **Marker.2** (Marker.1 in old files): `marked_range`, `color`, `name`,
  `comment`, relative to the item's trimmed range.
* **Times**: RationalTime.1 `{value, rate}` (both numbers, possibly
  fractional); ranges are TimeRange.1 `{start_time, duration}`. OTIO's
  writer puts `Infinity`, `-Infinity` and `NaN` in numbers, which JSON does
  not allow.
* **SerializableCollection.1**: `children`, holding several timelines (or
  further collections).

## Import (`interop::otio::importJson`, `importFile`)

* The file is read as JSON after `Infinity`, `-Infinity` and `NaN` outside
  strings are replaced by `null`. A file with no timeline (a Timeline, or a
  SerializableCollection holding some) is refused; everything else that
  cannot be read is a warning, given once with a count.
* **Rate.** The timeline's rate is `global_start_time`'s, else the first
  range's in its tracks, else 24 fps (with a warning). It goes through
  `rateOf()`, which makes 23.976, 29.97 and 59.94 the exact NTSC rates.
  Times at other rates are converted to it (with a warning) and rounded to
  whole frames.
* **Media.** Each piece of media becomes an offline master clip (an audio
  clip when only audio tracks use it): one for each key, which is the
  media's `target_url` (an image sequence: its first file's pattern), else
  the reference's name, else the clip's. Its MobID is a name-based UUID of
  `otio-media:` and the key (`mediaMobId`), so importing the same media
  again names the same clip, and `reuseClips()` drops the clips a project
  already has. The URL is kept as the clip's import source (a path for
  `file:` URLs), which Relink uses (p.969). Start and length come from the
  `available_range`, else from what the clips use.
* **Sequences.** Each timeline becomes a sequence named by the timeline (or
  `ImportOptions::name`), starting at `global_start_time`. Video tracks are
  V1, V2, ..., audio tracks A1, A2, ..., in the stack's order; other kinds
  are left out.
* **Segments.** A clip is a segment of its media's clip; its source frame is
  its `source_range` start less the media's available start. A gap is
  filler. A disabled clip, a generator and an unknown item read as filler,
  with a warning.
* **Transitions** go on the incoming segment, with their length
  `in_offset + out_offset` and an alignment from the two (centred on the
  cut, ending at it or starting at it). `SMPTE_Dissolve` is a dissolve; other
  types keep their name. Transitions at a track's start or end are left
  out.
* **Nesting.** A nested track is flattened into its parent, trimmed to its
  `source_range`. A nested stack keeps its top layer that has clips (the
  lower ones are hidden under it in playback), with a warning when there are
  more. Items nested more than 32 deep are left out. A `source_range` that
  starts before 0 (some writers put a timecode there) is ignored, with a
  warning.
* **Speed.** LinearTimeWarp and FreezeFrame become a Motion Effect on the
  segment (as the EDL import's `M2` lines do, edl.md); other effects are
  left out by name.
* **Markers** on clips, gaps and tracks go on their track at their place;
  the stack's go on TC1. Markers outside their item's range are left out.
  OTIO's colours map to Media Composer's marker colours by name.

## Export (`interop::otio::exportJson`, `exportFile`)

* One Timeline.1 named as the sequence, `global_start_time` its start
  timecode, at its edit rate, with a Stack.1 of the tracks: video first,
  then audio (or the tracks `ExportOptions::tracks` names). Data tracks are
  left out.
* Each segment is a Clip.2 with `DEFAULT_MEDIA`: an ExternalReference
  naming its media's file when the clip that `ExportOptions::resolve` finds
  has one (a `file:` URL for an absolute path, the plain path otherwise), a
  MissingReference with the clip's name otherwise. `available_range` is the
  clip's start and length; `source_range` is its start plus the segment's
  source frame. A clip that cannot be found is counted from 0, with a
  warning.
* Filler is Gap.1. A transition is a Transition.1 before its segment
  (`SMPTE_Dissolve` for a dissolve, `Custom_Transition` otherwise), with
  `in_offset` and `out_offset` from its length and alignment.
* Motion Effects are LinearTimeWarp.1 (FreezeFrame.1 for speed 0); other
  effects, clip gain and pan are left out, with a warning.
* Markers go on their track's `markers`, at their place; those on tracks
  not written go on the stack. Colours OTIO lacks are written as the nearest
  (lime green, teal cyan, navy blue, brown orange, gray white).
* The file is written through QSaveFile, so a failed write leaves the old
  file.

A round trip keeps clips, gaps, places, lengths, source frames, media
files, dissolves, speed changes and markers. What the import leaves out
(above) is gone after the first pass; after it nothing changes.

## In the application

Task 3.6b's second part: Import Media takes `.otio` files (also dropped
onto a bin), the List Tool gets the "OTIO" output format, and the Default
OpenTimelineIO setting (p.968-969).

## Checks

* `tst_otio`: the rules above on timelines built in the test, collections,
  refusals, non-finite numbers, export and round trips, reused clips,
  damaged files, and every corpus sample.
* ctest `otio_round_trip` (`scripts/otio-check.py --round-trip`): each
  corpus sample exported, and the export exported again, gives the same
  JSON.
* ctest `otio_python` (`scripts/otio-check.py --compare`, skipped without
  `opentimelineio`): OpenTimelineIO reads each export and finds, track by
  track, the same clips at the same places from the same frames of the same
  media, and the same transitions, as it finds in the original (less what
  Cutthulhu reads differently by design: generators, disabled clips, the
  lower layers of nested stacks, transitions at a track's ends).
* The fuzz target `otio` (fuzz/README.md).
* By hand: `cutthulhu --import-otio FILE.otio` prints the bin as a structure
  dump; `cutthulhu --export-otio BIN.cthbin SEQUENCE OUT.otio` (an
  `.otio` in place of the bin is a round trip, and then SEQUENCE may be left
  out).

## Later

* OTIO's other adapters (FCP XML is 3.6c, in Cutthulhu itself).
* Metadata: OTIO's `metadata` dictionaries are neither read nor written.
* Nested stacks as nested sequences, once the model has them.
* Checking what Media Composer writes against this, when the seat is at
  hand again.
