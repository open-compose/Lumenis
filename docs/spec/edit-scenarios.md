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
  V1, A1 and A2; `rate` ("50", "30000/1001") makes a mixed rate clip, which
  the monitors show at 25 fps through a Motion Adapter (Editing Guide p.469).
* **sequence:** the Record monitor's sequence at the start, track by track
  (V1, A1 and A2 always exist; others are added). A segment is
  `[clip, source IN, length]`, with the clip's track as a fourth member
  when it is not the record track, or `["filler", length]`. A trailing
  `{"effect": {...}}` gives the segment (filler too) an effect as
  `model::Effect::toJson` writes it (with keyframes under `_keyframes`). Without it the
  Record monitor starts empty, and the first edit makes the sequence.
* **steps**, in order, as the user gives them:
  * `{"load": "source"|"record", "clip": name}`;
  * `{"mark": monitor, "in": n, "out": n, "at": n}`: the monitor's marks
    cleared, then each one given set (OUT is inclusive), then the position
    put at `at`;
  * `{"position": monitor, "frame": n}`, `{"activate": monitor}`;
  * `{"source_tracks": [...]}` and `{"record_tracks": [...]}`: the Track
    Selector's selected tracks, `{"patch": [source, record]}`,
    `{"sync_lock": track}`; `{"timeline_setting": name, "value": b}` turns
    a Timeline setting on or off (`segmentDragSyncLocks`, `defaultSyncLocks`);
  * `{"select": [[track, start], ...]}`: segments selected in Segment mode,
    and `{"move": [frames, tracks], "tool": "lift"|"extract", "copy": b}`;
  * `{"rollers": [[track, edit point, "a"|"b"|"both"], ...], "kind":
    "ripple"|"overwrite"}` enters Trim mode with those rollers, and
    `{"trim": n}` trims by n frames; `{"slip": [[track, start], ...],
    "slide": b}` slips (or slides) those segments in Trim mode, and
    `{"trim": n}` moves them (positive: earlier material, or to the right);
    `{"jkl": n}` plays n frames from the transition with L (J when
    negative) and stops with K, the J-K-L Trim (p.658);
  * `{"bin_edit": "splice"|"overwrite", "clip": name, "at": frame}` edits a
    clip from the bin into the Timeline, at a frame as a drag does or,
    without `at`, at the Record monitor's marks as V and B do (p.624);
  * `{"lock": track}` locks a track (p.633);
  * `{"source_track": [track, start, source track]}`: the segment's
    right-click Change Source Track (p.595; 2026.8 guide p.507);
  * `{"audio_data": "volume"|"pan"}` picks the automation graph, and
    `{"key_drag": [track, record frame, frames, by]}` drags a keyframe
    (p.716); Add Keyframe is `{"command": "fx.add_keyframe"}`;
  * `{"eq": {...}}` sets the Audio EQ tool's values (those not given at
    their defaults; the keys of `model::AudioEq`), `{"eq_menu": item}`
    chooses an item of its Fast menu (p.739-740), and `{"eq_drop": [track,
    record frame]}` drops its effect icon on that clip in the Timeline
    (p.740; `"refused": true` when it must not take);
  * `{"audiosuite": plug-in id or null, "params": {...}}` chooses an
    AudioSuite plug-in in the Plug-In Selection menu and sets values as its
    dialog's OK does, and `{"audiosuite_menu": item}` chooses an item of its
    Fast menu (p.797-799);
  * `{"split_mono": track or ""}` is Split Track to Mono on a track, or
    Split All Tracks to Mono (p.734-736); `{"patch": [source, record],
    "refused": true}` a patch that must be refused, as one across audio
    formats (p.629); a clip's `"formats"` is its Track Formats column
    (p.671), and its `"gain"` and `"pan"` its Apply Gain and Center Pan
    (p.216, p.714);
  * `{"punch_in": {"clip": name, "targets": [track or "" for New Track, ...],
    "punches": [[record frame, length, clip frame], ...]}}` edits in an Audio
    Punch-In take (p.749-751), an undo step per punch-in;
  * `{"correction": {path: value, ...}}` enters Color Correction mode and sets
    those controls (lumenis/model/color_correction.h) on the segment at the
    Record position, one undo step; `{"correction": {}}` is the mode's Remove
    Effect button (FX Guide p.592); `expect_corrected` gives each track's
    material segments' "has a correction" in order; `{"correction_type": key}`
    is the Correction Type button (FX Guide p.596-598; keys as
    `model::relationshipKey`, converting a corrected segment's correction;
    `"ccEffect"` for the Color Correction effect, p.696), a
    clip's `"tape"` its Tape column, and `expect_relationships` gives each
    track's material segments' "source/program" relationship keys ("" for a
    side without a correction); `{"save_correction": name}` keeps the
    current segment's corrections as a Color Correction effect template
    (FX Guide p.613) and `{"apply_correction": {"name", "relationships":
    "template"|"current"}}` applies it to the current segment, or with
    `"track"` and `"frame"` to that segment in the Timeline (p.614-615);
    `{"manage_correction": "update"|"merge"|"flatten"|"remove",
    "relationships": [key, ...], "new_sequence": b}` is the Color Correction
    tool's Update, Merge, Flatten or Remove Correction with those boxes
    checked (FX Guide p.700-706);
  * `{"clip_note": text}` is Add Timeline Clip Note on the selected segments
    (p.483; "" deletes it, p.484); `expect_notes` gives each track's
    material segments' notes in order;
  * `{"heads_move": [clip, before]}` is an Alt+drag in Heads view (p.639):
    clip `clip` of the sequence cut at every edit point of the selected
    record tracks moves to before clip `before` (the clip count for the end);
  * `{"local_color": "#rrggbb" or ""}` is Set Local Clip Color on the
    selected segments (p.598); `expect_local_colors` gives each track's
    material segments' local colours in order;
  * `{"surround_pan": [track, channel, pan]}` sets a surround pan at the
    Record position (the pan as the composition writes it; p.704-706) and
    `{"lfe_only": [track, on]}` "This track is LFE only" (p.710);
    `expect_surround` gives each material segment's pans,
    `expect_lfe_only` the LFE-only tracks;
  * `{"mix_format": "stereo"|"5.1"|"7.1"}` sets the Audio Mixer's
    Sequence Mix Format (p.686); `expect_mix` is the format at the end;
  * `{"insert": [track, slot, effect id or null]}` and `{"move_insert":
    [from track, slot, to track, slot, copy]}` change Audio Track Effect
    inserts (p.791-795); `expect_inserts` gives each track's five slots as
    effect ids or null;
  * `{"clip_pan": [track, pan or null]}` sets the Clip mode pan of the
    segment under the Record position (null: none of its own; p.702),
    `{"default_clip_pan": [[tracks], commit]}` is Commit (true) or Remove
    (false) Default Clip Pan on Track and `{"adjust_clip_audio": [[tracks],
    dB, pan]}` Adjust Pan/Vols on Track (p.711); `expect_clip_audio` gives
    each material segment's `[gain, pan or null]`;
  * a clip's `"reformat"` (the Reformatting Options column's text) and `"frameflex_size"` (a FrameFlex box of that
    Size) make its spatial adapter (8.5a); `expect_spatial` gives each material segment's `[Reformat, Size or null]`, or
    null for none;
  * `{"subdivide": [track, record frame]}` is the Effect Editor's Subdivide
    Effect on the Pan and Scan effect there (FX Guide p.235-236), guided by
    the selected record tracks;
  * `{"command": id}`: any command of `commands.json` that the Timeline or
    the Composer carries out.
  * `"refused": true` on a command or trim step: it must be refused.
* **expect:** tracks as `sequence` writes them, without trailing filler
  (filler with an effect stays, as `["filler", length, {"effect": name}]`); a
  segment with a motion effect adds `{"speed": s}` (Fit to Fill), and one
  with automation `{"volume": [[clip frame, value], ...], "pan": [...]}`,
  and one with an Audio EQ `{"eq": {...}}` (the values not at their
  defaults), and one with an AudioSuite effect `{"audiosuite": [plug-in,
  {the values not at their defaults}]}`, and one whose effect has
  keyframes `{"keyframes": {"Category/Name": [[position, value], ...]}}`
  (frames for Fixed keyframes, else the effect's time to 1/1000; FX Guide
  p.124), and one whose effect has tracking data `{"tracking": {"Tracker
  name": [[frames with data points], reference frame]}}` (frames from the
  segment's first; FX Guide p.281-282), and one with a Motion Adapter
  `{"adapter": clip rate}`.
  **expect_monitors:** a monitor's position, clip, and marks (`null`: no
  mark). **expect_formats** gives tracks' audio formats ("mono", "stereo",
  "5.1", "7.1") and **expect_channels** the channels a track's segments
  come from.

The test records a SHA-256 of the sequence (`Sequence::toJson()`) at each
undo level as the steps run; an Undo or Redo among the steps must give back
the state recorded at its level, and a new edit replaces the levels above.
After the steps it undoes back to the start and redoes as far as it goes,
comparing each level's state. A change that is not an undo
step (a sync lock) belongs to the state of its undo level. A step that makes
several undo steps (`punch_in`) leaves the levels between unrecorded: each
is recorded the first time undo or redo reaches it. Before the
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
