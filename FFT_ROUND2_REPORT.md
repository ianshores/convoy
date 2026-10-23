# Heavyverb: second FFT optimization report

2026-10-22 · Intel Core Ultra 9 185H · Odysseus

## Result

All five recommendations were implemented, tested individually, and retained.
The custom engine uses **21.4% less CPU for mono and 19.8% less for two channels**
than at the start of this round with a four-second IR. Its cost is **7.2% / 7.4%
above PFFFT**, respectively. The starting gap was 36.4% / 33.9% in the same
confirmation session. PFFFT is the default backend; the dependency-free custom
backend is available through `HEAVYVERB_PORTABLE_FFT` and plugin builder
`--fft custom`.

The README and preceding optimization work were pushed first in commit
`4ebe2e9`. I then inspected both implementations, wrote the
[recommendations](FFT_ROUND2_RECOMMENDATIONS.md), implemented each change in
sequence, and saved benchmarks after every step. A separate shuffled comparison
rechecked every intermediate version alongside PFFFT. Final integration tests
and custom CLAP/VST3 builds completed headlessly on Odysseus.

## What each optimization contributed

CPU columns are percentages of **one core's audio-time budget**, not total
machine CPU use. This table uses the five-trial confirmation medians for a
four-second IR with 256-sample callbacks. Savings compare each row with the
preceding custom version, so they compound rather than add. These are measured
incremental effects in this implementation, not independent universal speedups.

| Implementation / change | Mono CPU | Two-channel CPU | Saving at this step, mono / two | Mono cost above PFFFT |
| --- | ---: | ---: | ---: | ---: |
| Starting custom version | 0.2441% | 0.4957% | — | 36.4% |
| Combine inverse scaling with the output copy | 0.2393% | 0.4881% | 1.9% / 1.5% | 33.7% |
| Process small FFT stages in SIMD batches | 0.2268% | 0.4593% | 5.2% / 5.9% | 26.7% |
| Calculate related frequency bins together | 0.2130% | 0.4481% | 6.1% / 2.4% | 19.0% |
| Pad spectral rows to a vector-sized stride | 0.2093% | 0.4226% | 1.8% / 5.7% | 16.9% |
| Mix two IR partitions per accumulator visit | 0.1919% | 0.3975% | 8.3% / 5.9% | 7.2% |
| PFFFT reference | 0.1790% | 0.3701% | — | — |

1. **Combine scaling with the output copy.** The inverse transform must scale
   its result and write interleaved audio. Doing both in one pass removes a
   separate read/write pass through the scratch buffers. This was the smallest
   gain, but confirmation showed lower medians in all eight workloads.
2. **Batch the small FFT stages.** The existing SIMD loop could not cover the
   last one- or two-bin groups. The new path processes independent groups
   together, and skips multiplication by unity in the one-bin case. A shared
   butterfly kernel keeps the scalar and SIMD arithmetic consistent.
3. **Calculate related bins together.** Two bins in real-input reconstruction
   share input values and intermediate arithmetic. Computing the pair together
   removes duplicated work in both forward and inverse transforms. DC, Nyquist,
   and the self-paired midpoint have explicit handling.
4. **Pad spectral rows.** Each partition has three extra zero entries in each
   plane, making its stride a multiple of four floats. This avoids a changing
   alignment across partitions and removes scalar leftovers from the mixing
   loop. Loads remain safe without relying on a stronger allocator alignment
   guarantee. The four-second mono instance uses 4,896 extra bytes, about 0.15%
   of its roughly 3.22 MB owned storage.
5. **Mix two partitions per visit.** Read a running frequency sum once, add two
   successive partition products, then write it back. The additions keep their
   original order. This reduces accumulator traffic without switching to the
   previously unsuccessful bin-major traversal. Odd partition counts use a
   single-partition remainder path.

All five produced lower confirmation medians in all eight standard workloads.
Small percentage differences remain sensitive to the compiler and machine.
There were no discarded algorithm candidates in this round; the earlier round's
negative experiments remain documented in its own report.

## Why PFFFT is still faster

The remaining difference is concentrated in transforms, particularly the early
small transforms that run frequently. PFFFT's native real-FFT layout keeps its
real pre/postprocessing and radix operations vector-oriented. Our custom engine
uses an N/2 complex FFT plus indexed real-spectrum reconstruction. Pairing that
reconstruction reduces its work, but it still uses scalar, permuted accesses.
Our odd-log2 complex sizes also retain a separate final radix-2 pass.

Both tested backends use four-float SSE; PFFFT's advantage here does not come
from AVX, FMA, truncating the IR, changing the partition schedule, or accepting
more latency. Its compact spectrum packs DC and Nyquist into existing lanes,
while custom keeps them in ordinary complex-bin slots. PFFFT uses aligned
vectors with groups of four real and four imaginary values adjacent in memory.
Our separate padded planes provide a different memory access pattern.
These are source-supported structural differences; no hardware-counter study
was performed to assign exact cache-miss costs to each one.

Repeated component medians, same CPU/compiler/settings, five shuffled trials:

| Component | Starting custom | Final custom | PFFFT |
| --- | ---: | ---: | ---: |
| 512-point forward + inverse | 1.942 µs | 1.133 µs | 0.552 µs |
| 4096-point forward + inverse | 18.495 µs | 11.916 µs | 5.460 µs |
| Complete early block, 8 partitions | 2.681 µs | 1.886 µs | 1.342 µs |
| Complete late block, 93 partitions | 78.150 µs | 65.359 µs | 62.297 µs |

Forward-plus-inverse figures sum the separate medians. PFFFT's inverse is
unnormalized because its convolution multiply supplies the scaling. Complete
block measurements include the actual normalization, mixing, copying, and
stream processing paths for both engines. They are standalone uniform building
blocks, separate from the complete two-stage throughput benchmark.

The isolated FFT pair is still about twice as expensive in custom, but the
complete long block is only about 5% more expensive. Reducing mixing overhead
helps close the whole-engine gap. The saved legacy `mac_us` diagnostic measures
only a single-partition accumulation helper with unpadded rows; it does **not**
measure the final paired path. Complete block and full-engine timings are used
to assess that optimization.

A further major FFT improvement likely requires vector-native real-spectrum
reconstruction or a redesigned real FFT/data layout, rather than another small
multiply-loop edit. A fused radix-8 terminal stage is another narrower candidate
for the odd-log2 sizes. Neither has been implemented or credited with a gain
in this report.

## All measured standard workloads

Same Intel Core Ultra 9 185H, 48kHz, 256-sample head partitions, 2048-sample tail
partitions, and 256-sample wet latency. CPU values are confirmation medians.

| IR | Channels | Callback | Starting custom | Final custom | PFFFT | Custom saving | Final cost above PFFFT |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1s | 1 | 64 | 0.1384% | 0.1029% | 0.0792% | 25.7% | 30.0% |
| 1s | 1 | 256 | 0.1390% | 0.1034% | 0.0780% | 25.6% | 32.6% |
| 1s | 2 | 64 | 0.2828% | 0.2098% | 0.1585% | 25.8% | 32.3% |
| 1s | 2 | 256 | 0.2836% | 0.2128% | 0.1585% | 25.0% | 34.3% |
| 4s | 1 | 64 | 0.2418% | 0.1977% | 0.1770% | 18.2% | 11.7% |
| 4s | 1 | 256 | 0.2441% | 0.1919% | 0.1790% | 21.4% | 7.2% |
| 4s | 2 | 64 | 0.5028% | 0.3968% | 0.3742% | 21.1% | 6.0% |
| 4s | 2 | 256 | 0.4957% | 0.3975% | 0.3701% | 19.8% | 7.4% |

For the headline four-second/256 case, final custom ranges were 0.1889–0.1932%
mono and 0.3887–0.4089% for two channels. PFFFT ranges were 0.1741–0.1813% and
0.3635–0.3772%. A one-second response still has a 33–34% relative gap because
frequent FFT overhead represents more of its total cost.

## Accuracy, integration, and issues found

- All 183 direct-convolution cases passed for both backends. Final custom
  maximum absolute error was **3.10e-6**, versus **3.22e-6** for PFFFT. The scalar
  custom build also passed all 183 cases.
- All 60 independent FFT cases passed in SSE and scalar builds, covering sizes
  32–16384, every bin against an independent double-precision reference, selected
  direct DFT checks, tones, random input, DC, Nyquist, and decay. Maximum
  roundtrip error was **4.77e-7**; worst roundtrip RMS error was **1.29e-7**.
  FMA opt-in correctness passed separately; headline timings do not enable FMA.
- ASan/UBSan passed for core convolution and FFT tests. Leak detection was
  disabled for this run; no leak-check claim is made. C and C++ allocation
  interception found no allocations during processing/reset.
- Fifteen custom Heavy stereo renders passed across 44.1/48/96kHz and five
  callback sizes; maximum error **7.73e-9**. Four CMake checks passed. The real
  native Pd external rendered correctly, with maximum error **2.24e-8**.
- Custom CLAP and VST3 rebuilt successfully. Their unstripped engine object
  contains `RealFFT` and no PFFFT symbols; no PFFFT objects were built. Generated
  hvcc code emits existing fall-through warnings; the build succeeds. The DPF
  build uses the existing `-fno-fast-math` override. No interactive host audition
  was performed, and Unreal/Wwise SDK builds remain outside this validation.
- **Benchmark noise:** the first padded-stride run was ambiguous for two
  channels, and an isolated component sample was unusually slow. Five shuffled
  confirmation trials supported the change across all eight workloads. The raw
  samples were retained, not deleted or substituted.
- **Diagnostic coverage:** the original microbenchmark did not exercise the
  new paired mixing path. I added a complete uniform-block benchmark to measure
  the real implementation, and explicitly distinguished it from the old helper
  timing. This prevented a misleading mixing-speed conclusion.

These changes preserve the complete IR, sample rate, partition schedule,
latency, and convolution operation. Arithmetic grouping changes normal floating
point rounding; the reference tests quantify that difference. No signal
approximation, resampling, noise gating, or IR truncation was introduced.

## Callback timing and stress limits

For the four-second/256 standard case, median p99 callback cost fell from
84.65 to 66.75 µs mono and 171.39 to 140.28 µs for two channels. PFFFT measured
66.23 and 137.32 µs. This captures useful reduction in ordinary callback work,
not a guarantee about the slowest possible callback.

The final custom confirmation runs contain 64-sample callback outliers up to
**3.20 ms**, exceeding their **1.33 ms** deadline. PFFFT's corresponding two-channel
case includes a **3.97 ms** outlier. We cannot attribute those isolated wall-time
spikes to a particular cause from these measurements. No samples were removed.
A separate 96,000-callback diagnostic for each backend found zero deadline
misses, but custom's mono/256 run included a 1.48 ms wall/thread-CPU event.

The single-pass load stress check found no misses for final custom in its
first seven cases. PFFFT missed 15 of 20,571 callbacks in the unusually small
7-sample/48kHz/two-engine case. In the heaviest case—eight engines at 96kHz,
64-sample partitions/callbacks, four-second IRs—**both failed the deadline**:
custom 562/4,500, PFFFT 563/4,500. Average cost was about 40% versus 48% of one
core, but bursts still exceeded 0.667 ms. These stress observations are single
passes, not repeated claims of a general custom advantage.

Very tiny near-subnormal input still causes expensive floating-point work:
custom measured 8.80% with normal denormal handling versus 0.195% with FTZ/DAZ
in a scoped test; PFFFT measured 19.36% versus 0.186%. That mode flushes tiny
values to zero. The engine does not change the host's floating-point policy.
The full quiet-signal results, including normal input and an extreme IR tail,
are in the log. Real project, console, mobile, and Cradle measurements are still
needed before setting a production performance budget.

## Reproducibility

GCC 16.2.1, Linux 7.2.5, Intel Core Ultra 9 185H, pinned logical CPU 1,
`-O3 -std=c++17`; PFFFT C files use C99. No fast-math, native-CPU, or FMA flags.
Dense seeded synthetic IR/input, full-IR-history warmup, sequential execution
at ordinary process priority. Each candidate first received three 30-second-
audio trials; five shuffled 30-second trials per version supplied the reported
confirmation medians. Timing excludes compilation, IR setup, and warmup.

[Raw logs and reproduction instructions](docs/performance/fft-round2/README.md)
include every trial, metadata, component measurements, validation output,
runtime diagnostics, and exact source snapshots. PFFFT source is the repository's
pinned `aa16fd3db58de4ba5dae8b0438440bb9da46b6fa` revision. Production headers
match the final measured snapshot after formatting, verified by an archive
hash audit. All new custom implementation code is MIT-licensed; no upstream
implementation code was copied into it.
