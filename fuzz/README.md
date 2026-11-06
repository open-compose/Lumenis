# Fuzzing the interchange parsers

ROADMAP Phase 3 gate 4: *fuzzing the bin and AAF parsers for 24 hours
produces no crashes.* This folder holds the fuzz targets, the driver that
replays saved inputs, and the inputs found by fuzzing that once crashed or
hung. BUILDING.md, "Fuzzing", has the procedure and the record of runs.

## What is here

| File | What it is |
| --- | --- |
| `fuzz_targets.h` | The targets' contract: one function per target, taking a whole file in memory. |
| `target_cfb.cpp` | `cfb`: the compound file container (`cfb::Reader::openData`), every stream read whole and in part, the tree written back with `cfb::Writer` and read again. |
| `target_aaf.cpp` | `aaf`: `aaf::File::openData`, every storage read as an object with every property decoded and followed, the `--dump-aaf` dump, and `aaf::importMobs`. |
| `target_avb.cpp` | `avb`: `avb::File::load`, every object parsed, the `--dump-avb` dump, and `avb::importBin`. |
| `target_avb_rewrite.cpp` | `avb_rewrite`: `avb::rewrite` of whatever was read; the result must load with the same objects and rewrite to itself, in both byte orders. |
| `target_fcpxml.cpp` | `fcpxml`: `fcpxml::importXml`, then every sequence exported with `fcpxml::exportXml`, which must succeed, and the export imported again, which must read. |
| `target_scc.cpp` | `scc`: `model::parseScc` (Scenarist Closed Caption imports, ROADMAP 7.4b), then the caption data is decoded, written again with `model::writeScc` and must read back the same. |
| `target_pict.cpp` | `pict`: `media::readPictData` (Export Matte PICT's files, ROADMAP 6.9c2), then a picture that reads is written again with `media::pictData` and must read back the same. |
| `target_otio.cpp` | `otio`: `otio::importJson`, then every sequence exported with `otio::exportJson`, which must succeed, and the export imported again, which must read. |
| `seeds.cpp` | Small seeds made by our own writers (compound files, an AAF export and bins of a tiny bin), so no download is needed to start. |
| `common.cpp` | The table of targets, `fuzzCheck` (a failed check aborts, which counts as a crash) and the Qt message filter. |
| `libfuzzer_main.cpp` | libFuzzer's entry point, compiled once per target (`fuzz_cfb`, `fuzz_aaf`, `fuzz_avb`, `fuzz_avb_rewrite`, `fuzz_otio`, `fuzz_fcpxml`, `fuzz_pict`, `fuzz_scc`). |
| `replay_main.cpp` | `lumenis_fuzz_replay`: runs files through the targets with any compiler; ctest `fuzz_replay`. |
| `regressions/<target>/` | Minimised inputs that crashed or hung a target before a fix. Each is also a unit test in `tests/interop/` (named below). |

The libFuzzer executables are built only by the `fuzz` preset (Clang,
`LUMENIS_FUZZ=ON`; the whole tree is compiled with
`-fsanitize=fuzzer-no-link,address,undefined`). The replay driver is built
with the tests on every platform, so `ctest` replays `regressions/`, the
built-in seeds and the downloaded interchange corpus through every target
with GCC, Clang and MSVC alike.

## What is not here, and why

The seed corpora (`build/fuzz/corpus/<target>/`, made by
`scripts/fuzz.py seed`) start from the interchange corpus, pyaaf2's and
pyavb's test files. They are MIT-licensed but are downloaded at pinned
commits by `scripts/interop-corpus.py`, not committed, and neither are the
corpora a run grows from them. Only inputs we made ourselves and the
minimised reproducers in `regressions/` are committed.

## Running

    scripts/fuzz.py build                       # cmake --preset fuzz, build
    scripts/fuzz.py seed                        # build/fuzz/corpus/<target>/
    scripts/fuzz.py run --time 30m --jobs 2     # all targets, 30 minutes in all
    scripts/fuzz.py run --targets avb --time 2h --each
    scripts/fuzz.py replay fuzz/regressions     # any build's lumenis_fuzz_replay

A run stops at the first crash, hang (`--timeout`, 10 s an input) or
sanitizer report, prints the stack, and prints the reproducer as base64.
With `--keep-going` (as the Fuzz workflow runs it) every target runs for its
whole time instead, every failing input is kept, and at the end they are
grouped into problems (the kind of failure and the first frame of the stack
in our code), each printed with its stack and its smallest reproducer. To
reproduce: `base64 -d > input`, then `build/fuzz/bin/fuzz_<target> input`,
or `lumenis_fuzz_replay --target <target> input` in any build (under the
`asan` preset for a sanitizer report).

## After a crash

1. Minimise it before fixing, while the fuzzer still crashes on it:
   `build/fuzz/bin/fuzz_<target> -minimize_crash=1 -max_total_time=120
   -exact_artifact_path=small input`.
2. Fix the root cause in the parser, minimally, with a comment saying what
   the damaged input did.
3. Add the minimised input to `regressions/<target>/` with a name that says
   what it does, and a unit test in `tests/interop/tst_cfb.cpp`,
   `tst_aaf.cpp` or `tst_avb.cpp` that builds or embeds it and fails without
   the fix.
4. Run the target again until a full run is clean.

## Regression inputs

| Input | Target | What it did | Test |
| --- | --- | --- | --- |
| `regressions/avb/negative-group-length.avb` | avb | A track group with a negative length: the import's loop that fits segments to the group's length never ended. | `tst_avb importsHostile` |
| `regressions/avb/shared-components.avb` | avb | Sequences that each name the next several times (made by hand while fixing the hang above): the import's depth-limited walks took 4^32 steps, and the dump, which expands references in place, ran to its flat limit of 2,000,000 objects (two minutes and gigabytes under the sanitizers). | `tst_avb importsHostile` |
| `regressions/avb_rewrite/tiny-edit-rate.avb` | avb_rewrite | An edit rate of 2.4e-63 (exponent -66): the writer wrote it as 0e-6, so the rewrite read back as another bin. Now refused. | `tst_avb writerRefuses` |
| `regressions/avb_rewrite/rate-23976e-4.avb` | avb_rewrite | A rate stored as 23976e-4 was written back as 2397600e-6 (repeated multiplication by ten gathers rounding errors), which reads back one unit in the last place lower. | `tst_avb writesRatesAsRead` |
| `regressions/avb_rewrite/long-creator.avb` | avb_rewrite | A creator longer than the header's 30-byte field, with a 0 as its 30th character: cut there, it read back without the 0, so the rewrite did not rewrite to itself. | `tst_avb writesLongCreator` |
| `regressions/aaf/negative-group-length.aaf` | aaf | The same hang in the AAF import, an OperationGroup with a negative Length (found by reading the AAF import after the bin import's; the file was then made to reproduce it). | `tst_aaf negativeGroupLength` |
| `regressions/aaf/slow-edit-rate.aaf` | aaf | A sequence at an edit rate of 1/3 (no timecode: 0 nominal frames a second) with a timecode slot: making its Start divided by its 0 frames a day (SIGFPE). | `tst_aaf slowEditRate` |
| `regressions/aaf/shared-strong-refs.aaf` | aaf | Found by the first 24-hour run (a timeout): strong reference vectors whose index lists one element many times, nested, so the dump wrote each element as often as it was listed, the product of the counts at every level (25 s for 250 KB). Each storage is now dumped once. | `tst_aaf sharedStrongRefs` |
| `regressions/aaf/huge-start-*.aaf` | aaf | Found by the second 24-hour run (UBSan): a tape timecode start near 2^62 frames at 30 fps on a 24 fps clip; converting it multiplied before dividing, and adding the clip's StartTime and the duration overflowed. | `tst_aaf hugeStart` |
| `regressions/aaf/huge-lengths.aaf` | aaf | Found by the third 24-hour run (UBSan): an effect whose input segments' lengths, near 2^63, overflowed when added up. Frame counts from AAF files are now clamped to 2^40 as they are read. | `tst_aaf hugeLengths` |
| `regressions/cfb/huge-stream-size.cfb` | cfb | A version 4 stream of size 0xFFFFFFFFFFFFFF93: its sector count wrapped to 0 and `read()` indexed an empty chain (UBSan). | `tst_cfb hugeStreamSize` |

The tests write these files when `CUTTHULHU_FUZZ_REGRESSIONS` is set to this folder
(`CUTTHULHU_FUZZ_REGRESSIONS=$PWD/fuzz/regressions build/ci/bin/tst_avb`), so they can be made
again; a reproducer from a fuzz run can also go here as it is, once minimised.
