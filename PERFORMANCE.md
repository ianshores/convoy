# CPU optimization measurements

## Current measurements: second FFT round, 2026-10-22

On the Intel Core Ultra 9 185H, Linux, GCC 16.2.1 `-O3`, the custom engine measures
**0.192% mono / 0.397% two-channel**, versus PFFFT's **0.179% / 0.370%**.
Conditions: 48kHz, four-second dense synthetic IR, 256-sample callbacks,
256/2048-sample partitions, 256-sample wet latency, CPU affinity 1. Five shuffled
30-second-audio trials after full-history warmup; median elapsed processing cost
as a percentage of one core's audio-time budget. No fast-math, FMA, or native-CPU
flags. PFFFT is the default. The [second-round report](FFT_ROUND2_REPORT.md) and
[raw logs](docs/performance/fft-round2/README.md) cover each optimization,
short-IR results, validation, callback outliers, and stress-test deadline misses.

## First FFT optimization round, 2026-10-22

The [optimization report](FFT_OPTIMIZATION_REPORT.md) and
[per-experiment log](docs/performance/fft-optimization/README.md) supersede the
custom-FFT performance claims below. On the Intel Core Ultra 9 185H, Linux,
GCC 16.2.1 `-O3`, 48kHz, four-second dense IR, 256-sample callbacks, and the
same 256/2048 two-stage policy, five alternating 30-second-audio trials measure
**0.24% mono / 0.50% two-channel** for the optimized custom engine, versus
**1.59% / 3.19%** for its original reference and **0.18% / 0.37%** for PFFFT.
All percentages are elapsed processing cost relative to one core's audio-time
budget. Latency stays at 256 samples. Raw outliers and stress failures are
included in the new log. PFFFT remains the default.

The sections below preserve the history of earlier experiments; they do not
describe the current optimized custom FFT.

## Custom FFT with identical partitioning (historical baseline)

The custom radix-2 FFT now supports the same two-stage scheduling as PFFFT.
The scheduler was moved into `heavyverb.hpp`, above the two uniform FFT
building blocks, so early/tail split, activation policy, latency alignment,
callback handling, and reset are identical. No additional custom FFT math
optimizations were made in this comparison. This establishes a baseline for
future work on that implementation.

**Hardware and method:** Intel Core Ultra 9 185H laptop, Linux, benchmark
pinned to logical CPU 1, GCC 16.2.1, `-O3 -std=c++17` for both engine variants
and `-O3 -std=c99` for PFFFT. No fast-math flags. Both variants use the same
dense generated IR, 48kHz audio, and fully warmed history. Three alternating
runs of each of the four configurations processed three seconds of audio per
case. CPU percentages are medians of the three average wall-time costs,
relative to one core. Two channels means independent mono engines.

| Four-second IR, 256-sample callbacks | Mono CPU | Two-channel CPU |
| --- | ---: | ---: |
| Custom FFT, uniform 256-sample partitions | 1.87% | 3.82% |
| Custom FFT, two-stage 256/2048-sample partitions | 1.62% | 3.19% |
| PFFFT, uniform 256-sample partitions | 1.25% | 2.60% |
| PFFFT, two-stage 256/2048-sample partitions | 0.18% | 0.37% |

The dependency-free two-stage variant costs roughly nine times more CPU than
PFFFT with the same policy: about +1.44 percentage points of one core for
mono and +2.82 points for two channels. The earlier comparison of uniform
backends therefore understated the difference once larger tail transforms
were introduced. The current custom backend benefits only modestly from
two-stage scheduling for four-second IRs. For the one-second IR, it actually
regresses: about 0.87% mono with uniform partitions versus 1.48% with the same
two-stage policy, compared with PFFFT's 0.08% two-stage result.

Two-stage scheduling changes the transform workload: the late stage uses
4096-point FFTs instead of the uniform path's 512-point FFTs. The custom
backend's high, nearly constant cost between one- and four-second IRs suggests
its transform implementation dominates this workload. This is an inference
from the ablation, not a separate per-function profile. Optimizing those
transforms and/or tuning a backend-specific partition policy is future work.

The custom two-stage p99 callbacks for four-second IRs were about 0.43–0.46ms mono
and 0.89–0.91ms for two channels. In the 96,000-callback custom diagnostic,
one two-channel 64-sample callback exceeded its 1.333ms deadline: 2.223ms
elapsed, with 2.220ms of thread CPU during that callback. Other configurations
had no misses in that run, though a mono 256-sample callback reached 4.792ms.
This ordinary-priority shared-laptop run does not establish a hard realtime
guarantee or the cause of the outliers; average CPU alone is insufficient.

**Decision:** PFFFT remains the default; no dependency was removed. The custom
two-stage backend is selectable through `HEAVYVERB_PORTABLE_FFT` and CMake's
`-DHEAVYVERB_PORTABLE_FFT=ON`. Both backends pass all 173 audio reference cases.
The custom variant passes C/C++ allocation checks, address/undefined-behavior
sanitizers (with leak detection disabled), and a native Pd render of early and
late response samples. Its CMake build does not link PFFFT, and the resulting
Pd module contains no PFFFT symbols. The default PFFFT export renders and
Linux CLAP/VST3 builds also pass. Cradle was not contacted.

Raw measurements: [machine/setup and summary](docs/performance/fft-ablation-summary.json),
[custom callback diagnostic](docs/performance/fft-ablation-custom-timing.json).
All twelve raw CSV files are in `docs/performance/fft-ablation-*.csv`, named
by FFT backend, partition policy, and run number. Build the four benchmarks:

```sh
make build/heavyverb_benchmark build/heavyverb_benchmark_uniform \
     build/heavyverb_benchmark_portable build/heavyverb_benchmark_portable_uniform
```

Use the same CPU affinity for each. `build/heavyverb_benchmark` is PFFFT with
two-stage partitions; `_uniform` is PFFFT/uniform; `_portable` is custom/two-stage;
`_portable_uniform` is custom/uniform. Header hashes in the summary identify
the common-scheduler implementation used for this ablation.

## Two-stage partitioning with PFFFT (historical)

Long responses now keep small blocks for the early reflections and use larger
blocks for later samples. At the default 256-sample wet latency, the first
2048 IR samples use 256-sample partitions and the remainder uses 2048-sample
partitions. The tail path is delayed by 256 samples to align its output with
the early path. This preserves the full response and requested latency.

The policy activates only when the IR exceeds twice the larger block size.
That larger size is eight times the requested partition, capped at 8192; if
it cannot be larger than the requested size, uniform processing is retained.
No IR samples are omitted, resampled, or approximated. All work remains on the
calling audio thread, without allocations, locks, or worker-thread scheduling.

Three alternating matched runs per configuration used the same Intel Core
Ultra 9 185H laptop, CPU 1, GCC 16.2.1, optimization flags, and fully warmed
history as the earlier comparisons. CPU values below are medians of the three
average measurements and percentages of one core. Two channels means two
independent mono engines. The benchmark's `partition` column is the requested
early-block size and latency; the new tail partition is 2048 samples here.

| IR, 48kHz | Channels | Callback | Uniform PFFFT CPU | Two-stage CPU |
| --- | ---: | ---: | ---: | ---: |
| 1 second | 1 | 256 | 0.28% | 0.076% |
| 4 seconds | 1 | 64 | 1.22% | 0.177% |
| 4 seconds | 1 | 256 | 1.25% | 0.182% |
| 4 seconds | 2 | 64 | 2.62% | 0.365% |
| 4 seconds | 2 | 256 | 2.63% | 0.371% |

The four-second cases use approximately seven times less average CPU. The
median p99 callback is 0.061–0.073ms mono and 0.128–0.136ms for two channels.
Across those matched runs, the largest two-channel wall time was 0.192ms.
A preliminary, separate two-channel 64-sample run had a 1.692ms outlier,
exceeding its 1.333ms deadline; it is retained in the initial raw results.

A follow-up measured thread CPU and wall time over 24,000 callbacks for each
of mono/two-channel and 64/256-sample buffers: 96,000 callbacks in total.
There were no deadline misses in that diagnostic. The largest observed
thread CPU time for two channels was 0.231ms. The largest elapsed time was
0.630ms with only 0.005ms of thread CPU during that callback, showing the
shared laptop can preempt this normal-priority benchmark. The cause of the
preliminary outlier is not proven. These local results are not a hard realtime
guarantee; loaded-game, target-platform, and native Unreal/Wwise comparison
checks remain deferred until Cradle is available.

Raw results: [summary](docs/performance/nonuniform-summary.json),
[timing diagnostic](docs/performance/nonuniform-timing.json),
[uniform 1](docs/performance/uniform-1.csv),
[uniform 2](docs/performance/uniform-2.csv),
[uniform 3](docs/performance/uniform-3.csv),
[two-stage 1](docs/performance/nonuniform-1.csv),
[two-stage 2](docs/performance/nonuniform-2.csv),
[two-stage 3](docs/performance/nonuniform-3.csv),
[preliminary uniform](docs/performance/uniform-initial.csv),
[preliminary two-stage](docs/performance/nonuniform-initial.csv).

The default, uniform PFFFT, and portable engines each pass 173 reference cases.
New cases exercise the split on both sides, the activation boundary, signed
IR taps, complete tails, history wrapping, in-place callbacks, reset, and
latency. Peak absolute error across the default tests remains 0.00000322.
C++ and C allocation checks, sanitizers (with the documented leak-check limit),
15 exported stereo renders, native Pd rendering, CMake, and Linux CLAP/VST3
build checks pass. The IR library checkout is not used by these benchmarks.

Build the uniform comparison with
`make build/heavyverb_benchmark_uniform` or configure CMake with
`-DHEAVYVERB_UNIFORM_PARTITIONS=ON`. `make timing` runs the Linux diagnostic.
The previous dependency-free FFT remains selectable separately.

## PFFFT uniform backend (historical)

This stage introduced PFFFT's real-input SIMD FFT and native convolution
layout, linked as C99 beneath the C++17 engine. The prior optimized FFT remains
a selectable, dependency-free backend. Both keep the full IR, partition size,
and wet latency unchanged; neither uses silence shortcuts or fast-math flags.

PFFFT subset: https://github.com/marton78/pffft at
`aa16fd3db58de4ba5dae8b0438440bb9da46b6fa`. Its source is unchanged in
`third_party/pffft`, with hashes and original paths recorded in manifest.json.
Generated exports configure its header for static linkage on Windows as well.

Three alternating runs per backend used the same benchmark, laptop, CPU 1,
GCC 16.2.1, `-O3 -std=c++17` for our engine and `-O3 -std=c99` for PFFFT.
Values below are medians of the three average CPU measurements. Every history
partition is warmed before timing. All channels run sequentially on one thread.

| IR | Channels | Callback | Previous optimized CPU | PFFFT CPU |
| --- | ---: | ---: | ---: | ---: |
| 1 second | 1 | 256 | 0.88% | 0.30% |
| 4 seconds | 1 | 64 | 1.95% | 1.28% |
| 4 seconds | 1 | 256 | 1.89% | 1.30% |
| 4 seconds | 2 | 64 | 3.93% | 2.64% |
| 4 seconds | 2 | 256 | 3.92% | 2.66% |

These are percentages of one core at 48kHz, with 256-sample convolution
partitions. Roughly one third of the previous four-second CPU cost is saved;
the one-second case is about three times faster.

For four-second IRs, PFFFT's median 99th-percentile callback times were
0.071–0.073ms mono and 0.148–0.157ms for two channels. The 64-sample callback
deadline is 1.333ms and the 256-sample deadline is 5.333ms. **One mono,
256-sample wall-clock observation took 7.063ms and exceeded its deadline.**
This benchmark runs at ordinary scheduling priority on a shared laptop.
The cause of that observation is not established by the original timing run;
average improvements do not demonstrate glitch-free realtime operation.
A separate 20,000-callback diagnostic did not reproduce a deadline miss. Its
longest elapsed callback was 0.556ms, with 0.065ms of thread CPU time during
that callback; the highest thread CPU time was 0.106ms. That demonstrates
scheduling effects in the follow-up, but does not establish the cause of the
original 7.063ms observation. [Diagnostic record](docs/performance/thread-timing.json);
`make timing` now runs the broader Linux check described above.

Raw data and aggregation: [summary](docs/performance/pffft-summary.json),
[previous backend run 1](docs/performance/pre-pffft-1.csv),
[run 2](docs/performance/pre-pffft-2.csv),
[run 3](docs/performance/pre-pffft-3.csv),
[PFFFT run 1](docs/performance/pffft-1.csv),
[run 2](docs/performance/pffft-2.csv),
[run 3](docs/performance/pffft-3.csv).

Verification: both backends pass all 101 reference cases, including a dense
four-second response. PFFFT's largest absolute error across those tests is
0.00000322; the exported stereo test error is below 0.000000008. C++ new and
C malloc/calloc/realloc interception find zero processing/reset allocations.
Sanitizers instrument both our engine and the vendored C implementation;
address/undefined-behavior checks pass with leak detection disabled. Native Pd,
Make and CMake builds, and Linux CLAP/VST3 builds pass. Wwise source generation
includes the required C files, SIMD headers, and notices; SDK builds and
interactive DAW/PlugData listening remain unverified.

The headless Pd check now captures into arrays and writes a WAV synchronously.
Its old background file writer could be outrun by accelerated batch rendering;
that was a test harness race. PFFFT's opaque FFT plan is excluded from the
`memory_bytes()` working-buffer estimate.

## Earlier optimization pass (historical)

The first implementation was too expensive: a four-second response used about
11–12% of one core for mono. The optimized implementation measures about 1.9%
mono and 3.8% for two independent channels, with the same 256-sample wet latency.

### What changed

- Store and accumulate only the unique half of the frequency spectrum. The
  other half is reconstructed before the inverse transform.
- Store real and imaginary coefficients in separate arrays so the compiler
  can process multiple frequency bins together.
- Use explicit real arithmetic in the inner loops instead of complex-number
  operations that hindered compiler vectorization.

No IR samples are discarded. No silence shortcut, fast-math build flag,
larger partition, background worker, or extra latency accounts for the gain.
An initial half-spectrum change alone regressed performance because the
complex data layout prevented vectorization; the separate arrays resolved it.

### Comparable measurements

Linux laptop: Intel Core Ultra 9 185H, GCC 16.2.1, `-O3 -std=c++17`.
Both versions were compiled with the same compiler and flags, then run pinned
to CPU 1. The deterministic dense noise IR decays exponentially but is never
truncated. Every IR history partition was warmed before three seconds of audio
were timed. Callbacks process all channels sequentially on one thread.

CPU percentage means processing time divided by audio duration, relative to
one core. Times include both channels where applicable. Two channels means two
independent mono convolvers, not a four-path true-stereo convolution.

| Four-second IR, 48kHz | Original CPU | Optimized CPU | Optimized p99 callback | Optimized maximum callback | Callback deadline |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mono, 64 samples | 11.18% | 1.84% | 0.103ms | 0.114ms | 1.333ms |
| Mono, 256 samples | 11.07% | 1.87% | 0.126ms | 0.156ms | 5.333ms |
| Two channels, 64 samples | 22.84% | 3.82% | 0.216ms | 0.253ms | 1.333ms |
| Two channels, 256 samples | 22.82% | 3.84% | 0.225ms | 0.235ms | 5.333ms |

Raw data: [original](docs/performance/baseline.csv),
[optimized](docs/performance/optimized.csv). The benchmark prints CSV;
`make benchmark` reruns it. To compare a saved earlier header, compile the
same benchmark with `HEAVYVERB_HEADER` set to that header's quoted path.

These are standalone engine timings, not DAW CPU-meter measurements, and
include wall-clock scheduling noise. A measured maximum is not a hard realtime
guarantee. Plugin wrapper overhead and other game/audio work are additional.
Other hardware, sample rates, and mobile/console builds remain unmeasured.

### Verification

101 direct-reference cases pass, including the complete four-second response.
Arbitrary callbacks, in-place processing, reset, and allocation checks pass.
Fifteen exported stereo graph renders and the headless native Pd render pass.
Address and undefined-behavior sanitizer checks pass with leak detection off
(the sandbox tracer prevents LeakSanitizer). CLAP and VST3 are rebuilt with
the optimized header; interactive listening and SDK-specific verification
remain separate checks.
