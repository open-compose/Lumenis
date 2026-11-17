# Long-run stability: the soak (10.2)

Ours: the guides say nothing about it. An editor leaves the application open for days; anything that grows a little
with each play, render or window (memory, open files, threads, windows that are never deleted) ends in a slow or
crashed session. 10.2 runs what an editor does, in a loop, for hours, and checks that nothing keeps growing.

## `cutthulhu --soak <hours>`

`src/ui/soak.*` (`lumenis::ui::Soak`), from `app/main.cpp`:

* **The sample project**, in a temporary folder of its own: a ten-second 1280 x 720 movie (FFV1 with a 440 Hz tone on
  two channels; every frame differs, so decoding does real work) linked into a bin, spliced in three times, with a
  dissolve on each cut and a Picture-in-Picture on the middle segment. Renders go to a media drive in that folder,
  never the computer's own (the Render dialog is answered with it, through `MainWindow::setRenderHook`).
* **Each cycle**, one step at a time on the event loop: play forward for six seconds from somewhere in the first half;
  scrub 40 random positions; play in reverse for three seconds; Render at Position on the Picture-in-Picture (again
  every cycle) and Clear Renders at Position, then the cleared precompute clip and its media deleted (Clear Renders
  "does not delete ... their precomputes", FX Guide p.60-61, so an editor deletes them; a 72-hour run would otherwise
  leave thousands on the drive); a splice of the clip and Undo; the Audio Mixer, Effect Palette, Effect
  Editor, Markers, Media Tool, Console, Timecode window and Audio Tool opened and closed again. A dialog a step opens is
  answered as an editor would (OK; the Media Tool Display dialog, Editing Guide p.346; Render and Clear Renders
  through `MainWindow::setRenderHook`/`setClearRendersHook`).
* **The log** (`--soak-log`, default `soak.jsonl`): JSON lines, flushed as they are written, so a crash keeps what came
  before. The first line is the header (product, version, OS, processor, hours, interval, seed). Then, every
  `--soak-interval` seconds (default 60) at the end of the cycle that is running (every tool window closed, nothing
  playing, so one line is like the next):

  | Field | What |
  | --- | --- |
  | `t` | seconds since the start |
  | `residentMb` | resident memory (working set on Windows) |
  | `heapMb` | the heap's bytes in use (Linux with glibc 2.33 or later through `mallinfo2`, macOS through `malloc_zone_statistics`; -1 on Windows) |
  | `handles` | open file descriptors (Linux `/proc/self/fd`, macOS `proc_pidinfo`) or kernel handles (Windows) |
  | `threads` | the process's threads |
  | `gdiObjects`, `userObjects` | Windows' GDI and USER objects (-1 elsewhere) |
  | `widgets`, `topLevels` | Qt's widgets and top-level windows alive |
  | `binItems` | items in the open bins (a render's precompute or an edit's copy kept for ever grows a bin) |
  | `cycles` | cycles done |
  | `shown`, `dropped` | frames played and dropped over the whole run |

  Two other lines can appear: `{"closedDialog": <name>, "step": n}` when a dialog was still open at the next interval
  (it is closed so the run goes on; a progress dialog is working, not waiting, and is left alone), and `{"stalled": n}` when no cycle finished for ten minutes (the run ends, exit 1).
  The last line, since 2026-11-17, is `{"ended": {"code": c, "seconds": s, "cycles": n}}` when the run ends by itself
  (its hours or cycles done, a stall, a log it cannot write). It is not a sample (no `t`). A run that crashed, was
  killed or lost its computer has none, and its log otherwise looks like a finished one.
* `--soak-cycles <n>` also ends the run after that many cycles (ctest `soak_smoke` takes one, however slow the
  build); `--soak-seed` sets the scrub positions; `processStats()` gives the process's figures for each OS.

## `scripts/soak-check.py`

Reads a log (or runs the soak first with `--app`) and fits a straight line to each figure after the warm-up
(`--warmup`, default ten minutes, at most a fifth of the run). A figure fails when it rises by more than its allowance
per hour **and** its last quarter is above its second (a cache that filled and stayed full passes):

| Figure | Allowed per hour |
| --- | --- |
| `residentMb`, `heapMb` | 32 |
| `handles` | 4 |
| `threads` | 2 |
| `widgets` | 20 |
| `topLevels` | 2 |
| `binItems` | 2 |
| `gdiObjects`, `userObjects` | 20 |

Where `heapMb` is given (Linux, macOS), memory is judged by it and `residentMb` is shown but not judged: resident
memory also counts what the allocator keeps after it was freed (glibc's per-thread arenas, macOS's zones), which rises
in steps as decoders' threads come and go and then stays, without a leak. On Windows `residentMb` (the working set) is
judged. `--max-<figure>` changes one. A stall always fails; too few cycles (`--min-cycles`) fails; an `ended` line with another code than 0 fails; a log
without one whose samples stop more than two intervals short of the header's hours fails as "stopped early" (logs
from before the closing line was written are judged this way too); a run the check starts itself (`--app`, ctest
`soak_smoke`) fails without the closing line; the dialogs the soak had to
close are listed. `--smoke` checks only the log's form, the cycles and stalls: ctest `soak_smoke` runs one cycle of
it, which is too short to judge growth. Dropped frames are reported, not judged (they depend on the machine).

## The runs

* **72 hours once on each OS** on the owner's machines (BUILDING.md "Soak runs"), and on GitHub's hosted runners once
  the repository is public: a job there may run 6 hours, so the hosted runs are six hours each.
* **Sanitizer soaks of a few hours** with the `asan` and `tsan` presets (`--soak 3` under each).

Results:

| Run | OS | Hours | Cycles | Dropped | Result |
| --- | --- | --- | --- | --- | --- |
| received 2026-11-17 | Windows 11 25H2 | 71.98 of 72 | 11156 | 18.4% | passed: every figure flat (working set +0.11 MB/hour, handles +0.20/hour, GDI and USER objects flat) |
| received 2026-11-17 | Ubuntu 26.04.1 | 64.60 of 72 | 11744 | 36.9% | not a pass: the figures are flat (heap +0.89 MB/hour; resident memory 1117 to 1695 MB, not judged, the heap is), but the log stops 7.4 hours short with no gap before it, so the run crashed, was killed or was stopped; to be run again |

Both ran version 0.1.0 (development builds, before 2026.1). The dropped share is the whole loop's (scrubbing, reverse
play and renders as well as playback), so it is reported, not judged.

## Found and fixed by the soak

* **Readers of files no longer used stayed open** (`media::FrameCache`): eight files, each a decoder with a thread per
  core and its buffers, so renders that had been cleared stayed open (Windows cannot delete an open file) and the
  threads stayed until newer files pushed them out. A file not read for a minute is now closed the next time any file
  is asked for. Threads in a 30-minute run went from 6 to about 50 before, and level off at 26-30 after.
* **The failure box and the Console window grew without end** while something failed over and over: the box now skips
  a line it already shows and stops at 20 lines ("... more in the Console"), and the Console window keeps the log's
  5000 lines (`tst_command_wiring failedCommandsSayWhy`).
* **The bins grew by a precompute a cycle** (`binItems` 11 to 39 in 40 minutes): what the guide says Clear Renders
  does, so the soak now deletes them; with that, a two-hour run's figures are flat.
* **Resident memory rose while the heap did not**: once the precomputes were deleted, each cycle cleared the frame
  cache and its decoders' threads came and went; resident memory rose 714 to 794 MB in six minutes while the heap in
  use stayed at 62-74 MB (with `MALLOC_ARENA_MAX=2`, one step to 570 MB and then flat). Not a leak: the allocator keeps
  what the threads freed. So the log has `heapMb` and the check judges it where it is given. Every line is a cycle's
  end: the run writes no last sample when it stops, which could fall in the middle of a render (only the closing
  `ended` line).
* **Clear Renders asked a question nobody answered** (an `exec()` dialog): the soak answers it through
  `MainWindow::setClearRendersHook`, and its watchdog closes any dialog left open and logs which.
