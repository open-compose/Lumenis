# Edit scenarios (Phase 4 gates 1 and 2)

ROADMAP Phase 4's gates ask for scripted edit scenarios, each a sequence of
commands, whose resulting sequence must equal the expected one (gate 1),
and for undo and redo of every scenario to give back identical states,
checked by hash (gate 2). `tests/ui/tst_scenarios.cpp` runs them.

## Scenario files (`tests/scenarios/*.json`)

Each file is a JSON list of scenarios:

```json
{
  "name": "overwrite at the record IN replaces and keeps the length",
  "guide": "p.467",
  "clips": [{"name": "A", "frames": 100, "tracks": "V1 A1-2"}],
  "sequence": {"tracks": {"V1": [["A", 0, 50], ["filler", 10], ["B", 0, 50]]}},
  "steps": [ ... ],
  "expect": {"V1": [["A", 0, 40], ["C", 10, 20], ["B", 10, 40]]},
  "expect_monitors": {"source": {"clip": "B", "position": 20, "in": 20, "out": null}}
}
```

* **clips:** master clips at 25 fps; `frames` defaults to 100, `tracks` to
  V1, A1 and A2.
* **sequence:** the Record monitor's sequence at the start, track by track
  (V1, A1 and A2 always exist; others are added). A segment is
  `[clip, source IN, length]`, with the clip's track as a fourth member
  when it is not the record track, or `["filler", length]`. Without it the
  Record monitor starts empty, and the first edit makes the sequence.
* **steps**, in order, as the user gives them:
  * `{"load": "source"|"record", "clip": name}`;
  * `{"mark": monitor, "in": n, "out": n, "at": n}`: the monitor's marks
    cleared, then each one given set (OUT is inclusive), then the position
    put at `at`;
  * `{"position": monitor, "frame": n}`, `{"activate": monitor}`;
  * `{"source_tracks": [...]}` and `{"record_tracks": [...]}`: the Track
    Selector's selected tracks, `{"patch": [source, record]}`,
    `{"sync_lock": track}`;
  * `{"select": [[track, start], ...]}`: segments selected in Segment mode,
    and `{"move": [frames, tracks], "tool": "lift"|"extract", "copy": b}`;
  * `{"rollers": [[track, edit point, "a"|"b"|"both"], ...], "kind":
    "ripple"|"overwrite"}` enters Trim mode with those rollers, and
    `{"trim": n}` trims by n frames; `{"slip": [[track, start], ...],
    "slide": b}` slips (or slides) those segments in Trim mode, and
    `{"trim": n}` moves them (positive: earlier material, or to the right);
  * `{"lock": track}` locks a track (p.633);
  * `{"source_track": [track, start, source track]}`: the segment's
    right-click Change Source Track (p.595; 2026.8 guide p.507);
  * `{"audio_data": "volume"|"pan"}` picks the automation graph, and
    `{"key_drag": [track, record frame, frames, by]}` drags a keyframe
    (p.716); Add Keyframe is `{"command": "fx.add_keyframe"}`;
  * `{"command": id}`: any command of `commands.json` that the Timeline or
    the Composer carries out.
  * `"refused": true` on a command or trim step: it must be refused.
* **expect:** tracks as `sequence` writes them, without trailing filler; a
  segment with a motion effect adds `{"speed": s}` (Fit to Fill), and one
  with automation `{"volume": [[clip frame, value], ...], "pan": [...]}`.
  **expect_monitors:** a monitor's position, clip, and marks (`null`: no
  mark).

The test records a SHA-256 of the sequence (`Sequence::toJson()`) at each
undo level as the steps run; an Undo or Redo among the steps must give back
the state recorded at its level, and a new edit replaces the levels above.
After the steps it undoes back to the start and redoes as far as it goes,
comparing each level's state. A change that is not an undo
step (a sync lock) belongs to the state of its undo level. Before the
first edit makes a sequence the state is an empty sequence, which undoing
that edit leaves (undo does not cover the bin).

## Generated scenarios (`scripts/make-scenarios.py`)

ctest `edit_scenarios_generated` runs `tst_scenarios` on 300 random
scenarios that `make-scenarios.py` writes into the build folder (ctest
`edit_scenarios_generate`; `LUMENIS_SCENARIOS` names the folders to read).
Each makes one to four Splice-in, Overwrite, Lift and Extract edits on
random tracks, and its expected sequence comes from a small reference
model of three-point editing in the script, written apart from the C++
model. A deliberately broken reference fails about half of them, so each
model checks the other.

## Gate status

* Gate 1: the scenarios here come from the Editing Guide's rules (each
  names its page) and from the reference model. The gate's expected
  sequences "recorded from real Media Composer and exported as AAF/EDL"
  wait for the owner's seat; then a scenario's `expect` can be filled from
  the AAF or EDL Media Composer exports (the AAF and EDL imports, Phase 3).
* Gate 2: every scenario, generated or written, is undone and redone by
  hash.
* Gate 3 (keyboard-only editing, **M**) is by hand.
