# FFT optimization experiment log

See the [short report](../../../FFT_OPTIMIZATION_REPORT.md) for the result.
All measurements here use **Odysseus: Intel Core Ultra 9 185H**, Linux,
GCC 16.2.1, logical CPU 1, ordinary priority. C++17 `-O3`, PFFFT C99 `-O3`.
No fast-math or native-CPU flags. FMA experiments alone explicitly add
`-mfma -mavx -mprefer-vector-width=128 -ffp-contract=off`; the control uses the
identical flags, with only the explicit FMA kernel disabled.

## Method and series

Each throughput CSV covers one- and four-second dense synthetic IRs at 48kHz,
one/two independent mono engines, 64/256-sample callbacks, 256-sample early
partitions, default tail 2048, wet latency 256. IR and input have fixed random
seeds. Full history is warmed; no silence shortcut. Timing covers public
`Convolver::process`, including FFT, accumulation, overlap, and scheduling.
Output is consumed. Setup, compilation, and IR loading are excluded.

CPU percentages mean sum of elapsed processing intervals divided by audio
duration, relative to one core. They include scheduling interruptions; they
are not a hardware-counter or whole-machine utilization measurement.

- `00-...` through `16-...`: initial three-second-audio, three-trial experiments,
  recorded immediately after each implemented feature. The baseline survived
  the restart. Exact header versions are preserved in the snapshot archive.
- `confirm-...`: rebuilt reference/variants, 30 seconds of measured audio per
  case, three trials in reproducibly shuffled order. These confirm or reject
  the early apparent gains. [Execution schedule](confirmation-schedule.json).
- `17-...` through `21-production`: combination tests, further reconstruction
  experiments, then cleanup to a focused production implementation. Three
  30-second-audio trials each.
- `final-original`, `final-custom`, `final-pffft`: final five-trial alternating
  30-second-audio comparison after cleanup. Identical scheduler, source driver,
  compiler options, and latency. [Execution schedule](final-schedule.json).

Do not attribute the difference between a confirmation row and a final-series
row to a single edit: they are separate sessions on a shared laptop. Tiny
percentage changes here are often within variation. No outliers were removed.

## Main incremental staircase

The following uses **four-second IR, mono, 256-sample callbacks**, medians of
three long confirmation trials. “Less CPU” is relative to the preceding row.

| Change | Mono CPU | Less CPU |
| --- | ---: | ---: |
| Original full complex FFT | 1.620% | — |
| Dedicated real-input FFT | 0.896% | +44.7% |
| Separate real/imaginary planes | 0.372% | +58.4% |
| Sequential coefficient tables | 0.321% | +13.6% |
| Explicit SIMD, radix-2 stages | 0.329% | -2.2% |
| Radix-4 stages with SIMD | 0.263% | +20.0% |
| Shared internal bin order | 0.256% | +2.7% |

Explicit radix-2 SIMD did not improve the whole engine on its own. The final
radix-4 SIMD path is useful: the matched scalar fallback costs
0.287% versus 0.260% with SIMD,
with identical pre-scaling enabled in both of those diagnostic variants.
The production path keeps normalization in the inverse instead.

## Additional recommendations and decisions

Every row below uses the same CPU and four-second mono workload. The percentage
is relative to that experiment's control, not the preceding table row. Positive
means less CPU. Exact controls are specified in the text and raw compiler flags.

| Experiment | Mono CPU | Less CPU vs control | Decision |
| --- | ---: | ---: | --- |
| IR pre-scaling | [0.260%](confirm-07-prescale/metadata.json) | -1.8% | No reliable gain; inverse normalization retained. |
| Initialize accumulator from first product | [0.265%](confirm-08-first-product/metadata.json) | -1.9% | Minor short-IR gain, no dependable long-IR win; rejected. |
| Explicit four-float spectral MAC | [0.266%](confirm-09-explicit-mac/metadata.json) | -2.2% | Compiler already vectorizes it; rejected. |
| FMA, identical target flags | [0.244%](confirm-11-fma/metadata.json) | +8.3% | Retained as an opt-in kernel; not a portable default. |
| Process wrapper in chunks | [0.254%](confirm-12-chunk/metadata.json) | +2.3% | Small/inconsistent gain, extra scratch and code; rejected. |
| 4096-sample tail partitions | [0.230%](confirm-13-tail16/metadata.json) | +9.4% | Long-IR average improves; short IR and callback peaks worsen; rejected as default. |
| 1024-sample tail partitions | [0.390%](confirm-14-tail4/metadata.json) | -53.5% | Lower peaks but much higher average cost; rejected as default. |
| Bin-major accumulation traversal | [0.878%](confirm-15-bin-major/metadata.json) | -237.6% | Strided access loses locality/vectorization; rejected. |
| Remove unused experimental plan storage | [0.267%](confirm-16-compact-plan/metadata.json) | -2.5% | Retained for measured plan-memory savings, no claimed CPU gain. |
| Disable explicit SIMD in radix-4 | [0.287%](confirm-17-scalar/metadata.json) | -10.4% | Portable fallback verified; SIMD retained on supported x86. |
| Uniform instead of two-stage scheduling | [1.398%](confirm-18-custom-uniform/metadata.json) | -437.4% | Much more average CPU; existing two-stage policy retained. |
| Coefficients/reconstruction in physical slot order | [0.247%](18-contiguous-reconstruction/metadata.json) | -1.2% | No whole-engine win; rejected. |
| SIMD real-spectrum reconstruction at all sizes | [0.238%](19-simd-reconstruction/metadata.json) | +3.8% | Tail FFT improves, small FFT worsens; inconsistent whole-engine win; rejected. |
| SIMD reconstruction only at N>=1024 | [0.244%](20-selective-reconstruction/metadata.json) | +0.0% | Improves tail-transform microbenchmark, not reliably complete reverb; rejected. |

Controls: pre-scaling compares with unordered (06); first-product and explicit
MAC compare with pre-scaling (07); FMA compares with target-matched control
(10); batching compares with pre-scaling; tail factors compare with batching
(12); traversal/compact/scalar/uniform compare with pre-scaling. Reconstruction
experiments compare with the retained combination (17), except all-size SIMD
(19), which compares with contiguous reconstruction (18).

Tail factor 16 improves the four-second mean but worsens one-second mono from
0.143% to 0.157%,
and two-engine/64-callback p99 rises from
170.4 to
250.8 microseconds. Keeping
the existing policy preserves that balance without extra algorithmic latency.

The compact-plan experiment changes 4096-point plan storage from 131,544 to
66,016 bytes. Production cleanup removes the remaining variant scaffolding,
leaving 65,848 bytes (512-point plan: 8,440 bytes). This is not the complete
convolver/IR/history footprint and is not an extra measured CPU speedup.

## Component timings

Each `components.txt` records forward/inverse transforms, a separate
permutation-pair diagnostic, copy/zero and overlap loops, and the actual
contiguous spectral-MAC primitive at realistic partition working-set sizes.
The original FFT is exposed only in a diagnostic header copy; its arithmetic
is unchanged. Original timing includes packing/extracting full complex data.
The real transform timing includes real packing/reconstruction. These are
isolated warm microbenchmarks, not additive accounting of complete CPU time.
There is one component timing batch per variant, so small differences are not
statistically established. Whole-engine confirmation decides retention.

For example, [original](final-original/components.txt) versus
[production](final-custom/components.txt) at FFT size 4096:
forward 215.30 → 8.57 microseconds; inverse 130.81 → 9.62 microseconds.
The standalone shuffle pair disappears; some indexed reads remain in real
reconstruction. Spectral accumulation stays around 54 microseconds in this
isolated 93-partition workload. That remaining work limits whole-engine gains.
GCC's [vectorization report](runtime/vectorization.txt) confirms vectorized
spectral accumulation; intrinsics are not automatically an improvement.

## Existing safeguards verified, rather than counted as new speedups

Plans, twiddles, scratch, IR spectra, and history are created during setup.
IR partitions are transformed once, histories use circular indices, and the
callback/reset allocate no memory, take no locks, and do no logging or I/O.
Mutable scratch is per instance. IRs are immutable during processing: there is
no live-IR replacement/handoff to optimize in the current API. Shared immutable
plans were not added because setup reuse already exists and the bottleneck is
processing. DIF/DIT was selected over the alternative autosort design; both
were suggestions in the guide, not simultaneous requirements.

## Validation and remaining timing limits

See [validation summary](runtime/validation.txt), raw [custom callback timing](runtime/custom-timing.json),
[custom load stress](runtime/custom-load.csv), and [PFFFT load stress](runtime/pffft-load.csv).
The standard timing diagnostic uses 24,000 callbacks for each mono/two-engine
and 64/256-sample combination (96,000 total), four-second dense IR, 48kHz,
256/2048 partitions. It records both elapsed and thread CPU time; there are no
misses in this particular run. The mono/256 run still contains a 3.46ms thread
CPU outlier, under its 5.33ms deadline; cause is not established.

Other raw throughput trials include elapsed intervals beyond a callback's
deadline, including a 1.54ms custom mono/64 interval in the final series.
Neither the standard diagnostic nor average CPU constitutes a hard realtime
guarantee. Ordinary-priority synthetic tests do not model game-thread load,
actual audio scheduling, or an Unreal/Wwise integration.

The separate synthetic load test uses 44.1/48/96kHz, 1/2/8 independent engines,
64/256/1024 partitions, several callback sizes, and four-second IRs. The first
six cases meet their deadlines here on both backends. Deliberately severe cases
fail on both: two engines with 7-sample/48kHz callbacks (custom 15 misses out of
20,571; PFFFT 17), and eight engines at 96kHz, 64-sample callback/early partition
(custom and PFFFT 562 misses out of 4,500). Large simultaneous working sets also
scale worse than a simple channel-count multiplication. Those configurations
need a different latency/scheduling/work-distribution budget; FFT tuning alone
does not solve them. No such architecture change is included in this work.
Reported owned memory excludes PFFFT's opaque plan; do not compare that column
as an exact total-backend memory comparison.

## Subnormal experiment

[Custom diagnostic](runtime/custom-quiet.csv) and [PFFFT diagnostic](runtime/pffft-quiet.csv)
explicitly compare flush-to-zero/denormals-are-zero off/on. The diagnostic
restores the calling thread's floating-point mode; the engine never changes it.
It times five seconds of audio after warmup. Normal signal CPU is about 0.24%
either way. Input scaled near 1e-37 costs 9.15% with normal subnormal handling,
versus 0.25% with flushing; its tiny output becomes zero. An artificial IR tail
spanning roughly 780dB costs 0.66% versus 0.25%. These are extreme numerical
stress signals, not normal audible reverb or an approved library IR.
Changing this policy globally would change tiny-value behavior. No such
numerical change is enabled by default. Use the host's existing policy or a
scoped host callback policy if appropriate; do not silently change process-wide
floating-point state.

## Reproduction and source audit

From the repository root:

```sh
python tools/run_fft_matrix.py --prepare
python tools/compare_final_fft.py
python tools/check_fft_runtime.py
python tools/summarize_fft_experiments.py
make test
```

Each label is immutable in `fft_experiment.py`; choose a fresh label to record
a new experiment. The matrix/final helpers reuse completed build/log directories
when rerun, so a fresh comparison should use a clean worktree/output location.
Every label contains CPU/compiler/flags, header hashes, correctness output, and
unfiltered CSV trials. `summary.json` retains median, min/max averages, p99 and
worst elapsed interval for every case. The initial metadata's hardcoded tail
size was corrected for factor experiments using the actual recorded flags.

[Exact header snapshots](source-snapshots.tar.gz) preserve every tested variant,
including rejected versions and the original full complex implementation.
`tests/reference` contains replayable experimental implementations; production
`include` files retain the selected algorithm only. `guide-source.json` identifies
the user-supplied guide by filename/hash; it is not redistributed. No PFFFT
implementation code was copied into the custom FFT.

Production builds use `HEAVYVERB_PORTABLE_FFT` (or CMake's option) to select the
custom backend; SSE is chosen at compile time on supported x86, otherwise the
scalar fallback. `HEAVYVERB_FFT_SCALAR` forces that fallback. Optional
`HEAVYVERB_FFT_FMA` requires an explicitly compatible target/compiler (the test
flags above); there is no automatic FMA runtime dispatch. SSE loads are safely
unaligned. The experimental step controls are confined to the replay fixtures,
not the production header.

The custom CLAP/VST3 build can be reproduced with:

```sh
.venv/bin/python tools/build_plugins.py examples/effect.pd build/effect-custom --dpf /path/to/DPF --fft custom
.venv/bin/python tests/export.py --portable
```

DPF's release defaults enable fast-math. The build helper now adds later
`-fno-fast-math` overrides for both C and C++ so plugin builds keep finite-value
checks and match the conservative numerical policy used by the engine tests.
Both formats were rebuilt after this fix. They are stripped binaries; backend
verification uses the unstripped Heavy input object, source-object list, and
[recorded compile command](runtime/plugin-compile-command.txt), rather than
assuming a stripped symbol table proves absence of a dependency.
Address/undefined-behavior sanitizers passed with `ASAN_OPTIONS=detect_leaks=0`;
no leak-detection result is claimed.

No UI was opened and Cradle was not contacted. No third-party IR was adopted;
all benchmark/test responses are synthetic. Unreal/Wwise SDK builds and game
benchmarks remain unmeasured here.
