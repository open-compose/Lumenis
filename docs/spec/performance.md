# Performance profiling (10.1)

Ours: the guides say nothing about speed beyond real-time playback (Phase 2 gates 3 and 4, `--measure-playback`).
10.1 measures the work an editor waits for, keeps the times per machine, and fails when one gets much slower.

## The benchmarks (10.1a)

`lumenis_bench` (`tests/perf/bench.cpp`) runs each benchmark on material it makes itself, so every machine does the
same work, and prints the median and fastest of its timed runs after one untimed warm-up run:

| Benchmark | What it times |
| --- | --- |
| `compose_dissolve_1080p` | a 1080p frame in the middle of a dissolve (`media::composeFrame`) |
| `compose_3d_warp_1080p` | a 1080p frame through a rotated and scaled 3D Warp |
| `compose_blur_1080p` | a 1080p frame through the Blur Effect |
| `compose_color_correction_1080p` | a 1080p frame through a Color Correction (gain and saturation) |
| `encode_ffv1_1080p` | encoding 25 1080p frames as FFV1 (`media::MediaWriter`) |
| `decode_ffv1_1080p` | decoding those 25 frames in order (`media::VideoReader`) |
| `seek_ffv1_1080p` | 10 of them out of order, as scrubbing reads them |
| `audio_mix_16_tracks` | mixing 16 tracks of tone into 10 seconds of 48 kHz stereo (`media::AudioMix`) |
| `bin_write_10k`, `bin_read_10k` | saving and reading a bin of 10,000 clips (`model::BinFile`) |
| `bin_show_10k` | opening that bin in a Bin view (`ui::BinView`) |
| `bin_sort_10k` | sorting it by Name |
| `timeline_paint_16x400` | painting the Timeline of a sequence of 8 video and 8 audio tracks of 400 segments each |

FFV1 is the codec because it is free of patents and so in every build; the release builds leave out H.264 and HEVC
unless the operating system provides them (ROADMAP Phase 10, the owner's answers).

## Recorded times and the check

* `tests/data/perf_baseline.json` holds each machine's median times, by machine name (`--machine`, else the host
  name, or `CUTTHULHU_PERF_MACHINE`). `scripts/perf.py --bench <lumenis_bench> --record` writes them.
* `scripts/perf.py --bench <lumenis_bench>` runs the benchmarks and fails when one is more than the margin (30%) slower
  than its recorded time and also more than 2 ms slower. Much faster times say to record again. A machine without a
  baseline only reports: times on different machines cannot be compared.
* ctest `perf_benchmarks_run` runs every benchmark once in the ordinary test run, so a change that breaks one fails
  there. ctest `perf_compare` (label `perf`) is the timed comparison; the test presets leave it out, and
  `ctest --preset perf` runs it alone.
* Times on shared CI machines vary from run to run; the hosted runners' baselines (after the repository is public,
  10.0) are recorded from several runs, and the margin is the baseline's to widen.

## Profiling (10.1b)

The slowest benchmarks are profiled and their hot paths fixed; BUILDING.md "Profiling" says how (perf on Linux,
Instruments on macOS, Windows Performance Recorder and Analyzer on Windows).
