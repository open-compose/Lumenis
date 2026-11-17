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
| `audio_mix_16_tracks` | mixing 16 tracks read from a WAV file into 10 seconds of 48 kHz stereo (`media::AudioMix`) |
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
* GitHub's hosted runners (10.1c): the Linux (system Qt), both macOS and the Windows jobs of CI run
  `scripts/perf.py --repeat 3 --print-times` after the tests, as machines `github-ubuntu-24.04`, `github-macos-15`,
  `github-macos-26` and `github-windows-2022`. Each benchmark's time is the median of its three runs' medians, and the
  times are printed between `perf-times-begin` and `perf-times-end` lines. A runner's host changes from run to run, so
  its baseline comes from several CI runs (`scripts/perf.py --record-from <saved job logs> --machine <name> --margin
  1.0`, the median of every run's times) with a margin of its own in its entry: a benchmark fails CI when it is twice
  its recorded time, three times on Linux. The first baselines (2026-11-17) come from three CI runs, nine runs of the
  benchmarks each: Windows' runs varied by under 7%, macOS's by up to 1.7 times, and Linux's by up to 2.3 times (one run
  landed on a slow host), hence its margin of 2.0. Slower and much faster benchmarks are also annotations on the run. Until a runner has a baseline
  the step only reports.

## Profiling (10.1b)

Found and fixed so far (times on the 4-thread Linux development machine):

| Benchmark | Before | After | What it was |
| --- | --- | --- | --- |
| `compose_color_correction_1080p` | over 75 s | about 0.2 s | every control read by its path for every pixel; now `media::CorrectionPlan` |
| `compose_dissolve_1080p` | 234 ms | about 100 ms | single-threaded passes and a library call to round every value; a copy of the picture per track for Matte Key |
| `compose_3d_warp_1080p` | 481 ms | about 170 ms | the same passes |
| `compose_blur_1080p` | 155 ms | 37 ms | the same passes |
| `bin_show_10k` | 6.7 s | about 10 ms | the Script view built for every bin, a widget row per clip, with a linear look-up per row |
| `timeline_paint_16x400` | 250 ms | about 11 ms | the fitted scale worked out (a walk over the sequence) for every segment drawn; sync breaks compared every piece with every other |
| `compose_3d_warp_1080p` | (170 ms) | about 150 ms | its placement on one thread, and two copies of the sources to swap them |
| `compose_dissolve_1080p` | (about 110 ms) | about 50 ms | memory traffic: both sides copied into the transition, and every blend making a new picture, zeroed before it was written |
| `compose_blur_1080p` | 223 ms | about 120 ms | the benchmark had no object, so it timed a copy (the Blur Effect shows the picture as it is without one, FX Guide p.392); with one over most of the frame, the box blur ran on one thread and down the picture a pixel per row |
| `compose_3d_warp_1080p` | (about 116 ms fastest) | about 100 ms fastest | a copy of the background; Shadow and Trail's sparkle on one thread |
| `compose_3d_warp_1080p` | about 103 ms fastest | about 73 ms fastest | `planePoint` (Cramer's rule, three divisions) solved for every pixel; its determinants are linear in the screen point, so `Placement::planeMap` works the map out once a frame, two linear forms over a third (the same points to within 2e-11 of a pixel, checked over the benchmark's frame and every 3D Warp test) |
| `compose_blur_1080p` | about 119 ms fastest | about 112 ms fastest | a division for every value the box blur writes, and two clamps per pixel; now the window's reciprocal, and the edges apart from the middle (test `tst_compositor boxBlurMatchesAPlainOne` against a plain blur) |
| `compose_dissolve_1080p` | 4.0 billion instructions (callgrind, three frames and a warm-up) | 3.1 billion | the passes every frame goes through ran one value at a time: `Picture::quantize` (a third of a dissolve), `toImage`'s rounding to bytes and `fromImage`'s table look-up. GCC's `-O2` leaves a loop with a comparison in it unvectorised while floating-point exceptions may be observed, so `lumenis_media` is built with `-fno-trapping-math` under GCC (Clang vectorised them already), and the loops work in blocks of eight with no branches, the byte stores through a block of their own (a byte pointer may alias the floats); the values are the same, value for value |
| `compose_color_correction_1080p` | (about 163 ms fastest) | about 105-120 ms fastest | the luma ranges' shares (a walk of three curves) worked out for every pixel when no Shadows, Midtones or Highlights control was set, unused ranges blended in, and the hue and wheels' sines and cosines per pixel; the results are the same value for value |

Looked at and left as they are: `seek_ffv1_1080p` takes about seven times a frame in order because FFmpeg's FFV1
encoder leaves a keyframe only every 12 frames, so a seek decodes up to 11 frames first; the media Cutthulhu makes is
intra-frame (DNx, ProRes), and only the benchmark and the soak write FFV1. The 3D Warp's bilinear `sample` is four
reads and multiplies a value; what is left of its cost is the call. Open: `audio_mix_16_tracks` takes 47 ms on GitHub's
Linux runner but 150-180 ms on macOS and 134 ms on Windows (2026-11-17). Its 16 pieces come from 8 places in one stereo
file, and readers are kept by file, stream and channel (`AudioMix::reader`), so two readers seek between 8 places on
every block: on Linux 60% of the mix is those seeks and decoding, and opening a reader costs 80 million instructions
(FFmpeg probing the WAV's data). A reader per source offset made Linux 40% slower (16 opens), so it was not kept; where
the time goes on macOS and Windows needs Instruments and WPA there (BUILDING.md "Profiling"). The audio mix of 16 file tracks takes about 1% of real time (the calibration tone's
generator is slower, but one tone is still under 1% of real time); FFV1 coding is FFmpeg's.

The compositor's memory traffic (a 1080p picture is 33 MB of floats): `over` and `mix` have versions that take a
picture the caller is done with (`Picture&&`) and blend in its pixels, which `composeFrame` and `transitionPicture`
use by moving their pictures in; pictures whose every pixel is about to be written start unwritten
(`Picture::uninitialized`, `PixelData`'s `detail::DefaultInitAllocator`), as does `fromImage` when the image fills the
frame. Write a new blend or effect the same way: take a `Picture&&` when it can work in place, and start an output it
writes whole with `Picture::uninitialized`. The box blurs (`boxBlur` in color_effects.cpp, `blurMask` in
intraframe.cpp) share their lines among threads and go down the picture a group of columns at a time into a buffer of
their own, with the same sums as before value for value; the Mosaic's rows of tiles, the Intraframe objects' blend,
the 3D Warp's Shadow and Trail sparkle run on threads, and `warp3dPicture` has a `Picture&&` background. Times on the
development machine vary by a third from run to run since it moved to a busier host: compare the fastest of several
runs, or two builds' instruction counts under callgrind (BUILDING.md "Profiling"). `composeFrame` takes the first layer
laid over its transparent start as it is (layer + 0), not blended: 145 million instructions fewer for a 1080p dissolve
frame. Looked at and left: rounding to the effects' precision by a table of the steps' values in place of the division
measured no faster. (The division was not vectorised under GCC either, as it turned out: see `-fno-trapping-math` above.)
`toImage`'s packing of bytes into pixels stays one pixel at a time: its loads are four bytes apart. Left for later: lighter pixels (16-bit half floats) where the effects' precision allows, and the other effects'
passes that still copy their source.

The slowest benchmarks are profiled and their hot paths fixed; BUILDING.md "Profiling" says how (perf on Linux,
Instruments on macOS, Windows Performance Recorder and Analyzer on Windows).
