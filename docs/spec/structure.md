# Structure dumps (Phase 3 gate 1)

Gate 1 asks that bins and projects made with Media Composer open in Cutthulhu
with the same clip count, names, timecodes, custom columns and sequence
structure, checked with a structural diff tool. Every reader fills the native
model (docs/spec/native-model.md), so the comparison runs on a dump of the
model:

* `cutthulhu --dump-structure <bin.cthbin | project.cthproj | project folder>`
  prints the dump (`model::structureOf`, `src/model/structure.cpp`).
* `scripts/structdiff.py A.json B.json` compares two dumps.

The schema is ours and versioned, so other tools can write it as well: a
reference dump of a Media Composer bin, made by another reader, is compared
with Cutthulhu's.

## Schema, version 1

```json
{
  "format": "cutthulhu-structure",
  "version": 1,
  "project": "Harbor",
  "bins": [
    {
      "name": "Dailies",
      "customColumns": ["Circled"],
      "items": [
        {
          "kind": "masterClip",
          "name": "A001_C001",
          "mobId": "urn:smpte:umid:060a2b34....",
          "editRate": "25/1",
          "duration": 250,
          "start": "01:00:00:00",
          "end": "01:00:10:00",
          "tracks": "V1 A1-2",
          "columns": {"Circled": "yes"},
          "fields": {"Tape": "A001"},
          "sources": [{"kind": "file", "mob": "urn:...", "track": "V1", "name": "..."}],
          "sequence": {"tracks": [{"name": "V1", "segments": [
            {"source": "urn:...", "name": "A001_C001", "track": "V1", "in": 10, "length": 50},
            {"filler": 25, "transition": "Blend/Dissolve"}
          ]}]}
        }
      ]
    }
  ]
}
```

* `project` appears only for a project. A project's bins are listed depth
  first, without the Trash, and each takes its name from the sidebar.
* `kind` is the bin file's name for the object kind: `masterClip`,
  `subclip`, `audioClip`, `sequence`, `group`, and so on.
* `start` and `end` are the Start and End columns, as text.
* `columns` holds the custom columns. These are the bin's user-made headings
  (p.269), plus any heading that is not in the guide's table (p.274-278).
  `fields` holds the other, standard headings. Start, End and Tracks have
  keys of their own.
* `sequence.tracks` lists the tracks in order. Each segment is filler
  (`"filler": length`) or a source clip: its MobID (`source`), its name when
  edited in, its track, IN point and length. Effects are named
  "Category/Name".
* `sources` is the clip's source chain (native-model.md). It is informative:
  structdiff does not compare it.

## What structdiff compares

* Bins, matched by name.
* In each bin: the custom column list, and the count of items of each kind.
* Items are matched by MobID, and otherwise by kind and name (and their
  order among items with the same kind and name). For each item it compares
  name, kind, edit rate, duration, Start, End and custom column values.
  `--fields` adds the standard columns and Tracks.
* Sequences: track names in order, and each track's segments in order.
  `--no-mob` matches items only by kind and name, and segments' sources by
  name, for comparing with a reader that does not keep MobIDs.

It prints every difference, one per line, and exits 1 when there are any.
`tests/scripts/test_structdiff.py` (ctest `structdiff`) covers each case,
and `tst_bins` (`structureDump`) covers the dump.
