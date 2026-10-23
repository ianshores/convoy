# Third optimization report

2026-10-22. The custom **whole convolver** wins all eight repeated workloads on
Odysseus, including against an AVX-targeted PFFFT control. No IR samples are
removed in these results. Sample rate, wet latency, partition policy, and ordinary
floating-point semantics are unchanged. The isolated custom FFT remains slower;
the complete engine wins through combined transform and partition-mixing work.

## Machine and method

Intel Core Ultra 9 185H laptop, Linux, GCC 16.2.1, logical CPU 1, ordinary priority.
C++17/C99, `-O3`, no fast-math, FMA, or native-CPU build flags. Custom AVX dispatch
selects eight-float kernels at runtime on supported GNU/Clang x86 builds, with
SSE/scalar fallback. The second PFFFT control compiles its C and wrapper with
`-mavx`; its pinned float algorithm remains four-wide. PFFFT revision:
`aa16fd3db58de4ba5dae8b0438440bb9da46b6fa`.

Each candidate passed correctness checks before three initial throughput trials.
Seven shuffled confirmation trials cover every successful candidate and control.
Each trial measures 30 seconds of 48kHz audio after warming the entire history.
Early/tail partitions are 256/2048 samples, wet latency 256 samples. IRs are dense
synthetic one- or four-second decays. Percentages measure elapsed processing as a
fraction of **one core's audio-time budget**, not total-machine utilization.
Compilation, IR setup, and warmup are excluded. Raw outliers remain in the logs.

## Complete comparison

| IR seconds | Channels | Callback | Custom CPU % | PFFFT CPU % | PFFFT `-mavx` CPU % | Saving vs PFFFT | Saving vs AVX control |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 64 | 0.071245 | 0.081967 | 0.074376 | 13.08% | 4.21% |
| 1 | 1 | 256 | 0.069152 | 0.080731 | 0.073107 | 14.34% | 5.41% |
| 1 | 2 | 64 | 0.142477 | 0.163635 | 0.154833 | 12.93% | 7.98% |
| 1 | 2 | 256 | 0.141585 | 0.163165 | 0.151050 | 13.23% | 6.27% |
| 4 | 1 | 64 | 0.148213 | 0.180514 | 0.172818 | 17.89% | 14.24% |
| 4 | 1 | 256 | 0.149498 | 0.181293 | 0.171728 | 17.54% | 12.94% |
| 4 | 2 | 64 | 0.318687 | 0.386709 | 0.362511 | 17.59% | 12.09% |
| 4 | 2 | 256 | 0.316518 | 0.380064 | 0.364001 | 16.72% | 13.04% |

## Every experiment

The columns below show percentage reductions in CPU against the named parent,
using seven-trial confirmation medians and 256-sample callbacks. Negative means
slower. These are relative changes, not percentage points of a core. Small
changes and mixed results should not be read as universal wins. All code,
including rejected candidates, has an archived snapshot.

| Candidate | Parent | Change | 1s mono | 1s two | 4s mono | 4s two | Decision |
|---|---|---|---:|---:|---:|---:|---|
| 01-vector-reconstruction | 00-control | Contiguous conjugate-pair reconstruction | +11.45% | +12.06% | +6.00% | +6.56% | Keep |
| 02b-pruned-forward | 01-vector-reconstruction | Skip known-zero first-stage arithmetic | +4.77% | +1.61% | +4.97% | +3.87% | Keep; naming collision fixed before timing |
| 03-contiguous-history | 02b-pruned-forward | Contiguous history traversal | -7.28% | -6.18% | -5.76% | -4.65% | Reject |
| 04-four-partition-mix | 03-contiguous-history | Four products with split spectra | -0.94% | +2.30% | -2.96% | +1.94% | Reject combined branch |
| 05-fixed-schedule | 02b-pruned-forward | Fixed schedules for common sizes | +5.73% | +6.69% | +1.50% | +3.23% | Keep for fallback |
| 06-packed-spectra | 05-fixed-schedule | Four-bin packed spectra | -7.40% | -3.52% | -5.44% | -3.93% | Foundation; slower alone |
| 07-four-packed-mix | 06-packed-spectra | Four products with packed spectra | +7.62% | +7.13% | +12.11% | +9.90% | Keep |
| 08-eight-packed-mix | 07-four-packed-mix | Eight products per accumulator visit | -4.58% | -6.41% | -12.53% | -8.89% | Reject |
| 09-block-boundary | 07-four-packed-mix | Keep block routine out of line | +0.55% | -0.79% | -3.47% | -2.12% | Reject |
| 10-eight-bin-layout | 07-four-packed-mix | Eight-bin packed spectra | -0.41% | -1.59% | -1.95% | -0.18% | Foundation; mixed alone |
| 11-runtime-avx-mix | 10-eight-bin-layout | Runtime AVX partition mixing | +12.84% | +11.57% | +6.38% | +3.90% | Keep |
| 13-avx-reconstruction | 11-runtime-avx-mix | AVX real-spectrum reconstruction | +0.33% | -1.60% | +0.95% | +1.97% | Keep combined; small/mixed alone |
| 14-avx-butterflies | 13-avx-reconstruction | AVX large radix-4 stages | +4.62% | +6.22% | +0.84% | -1.00% | Keep combined |

Candidate `r3-02-pruned-forward` did not compile because a local name collided
with a template argument. Renaming it fixed the issue; no timing is attributed
to the failed build. Candidate 12 is a PFFFT control, not a custom optimization.
Candidate 15 forces SSE on the final implementation: four-second mono uses
0.16096% and two channels 0.33055%; one-second mono uses 0.08589%, still behind
PFFFT's 0.08073%. The all-workload win is specific to the runtime AVX path here.

## Why the complete engine wins

Five shuffled component trials, same CPU/compiler policy, times in microseconds:

| Work | Round-two custom | Generic PFFFT | AVX-targeted PFFFT | Final custom |
|---|---:|---:|---:|---:|
| 512-point forward | .5533 | .2739 | .2554 | .3328 |
| 512-point inverse | .6005 | .2846 | .2680 | .3798 |
| 4096-point forward | 6.2087 | 2.8814 | 2.7050 | 3.4383 |
| 4096-point inverse | 5.9977 | 2.7088 | 2.5375 | 3.7355 |
| Complete 8-partition, 512-point block | 1.8978 | 1.3444 | 1.1927 | 1.2360 |
| Complete 93-partition, 4096-point block | 65.8876 | 63.2121 | 59.8655 | 50.1533 |

Custom forward uses the production half-input packed path. PFFFT inverse is
unnormalized; its MAC supplies normalization. Complete block timings include
normalization for both. The packed layout reduces pointer traffic; four-product
batches reuse accumulators; AVX handles eight bins at once. These compensate for
the remaining isolated-transform gap. No PFFFT code was copied into the custom
MIT implementation. Legacy component logs' split-plane MAC diagnostic does not
represent the final packed mixer; use the complete block measurements above.

## Quality and integration checks

- 183 direct convolution cases for PFFFT and custom AVX/SSE/scalar paths.
  Custom maximum absolute error: 3.10e-6; PFFFT: 3.22e-6.
- General, half-input, and packed FFT paths: 180 cases per AVX/SSE/scalar mode,
  540 total. Maximum round-trip error below 4.8e-7.
- Independent double-precision full-tail reference at 44.1/48/96kHz with impulse,
  noise, multitone bursts, and quiet noise. Custom residual SNR 131.94–134.97dB;
  maximum residual peak 8.31e-8. PFFFT SNR 131.83–135.63dB. Both retain float32
  accuracy. This is numerical validation, not a listening-panel claim.
- Audio/reset allocation interception, ASan/UBSan, and all six CMake tests pass.
- Fifteen exported stereo graph renders pass (worst error 7.73e-9), native
  headless Pd render passes (2.24e-8), and custom CLAP/VST3 builds succeed.
  Generated hvcc fallthrough warnings are unchanged. DPF fast-math is disabled.

## Timing limits and remaining work

Separate 96,000-callback timing runs have no deadline misses. A heavy stress case
(eight engines, 96kHz, 64-sample partitions/callbacks, four-second IR) misses
562/4500 deadlines with each engine: lower average CPU is not a realtime guarantee.
Custom's average in that stress run is 32.63% versus PFFFT's 49.06%, on the CPU
specified above. Extreme subnormal signals still require host denormal policy;
this round does not change FTZ/DAZ. Standard four-second mono owned storage is
3,234,264 bytes custom versus 3,164,536 PFFFT, excluding PFFFT's opaque plan.

No Cradle, Unreal, Wwise, console, or mobile measurements were made. GNU/Clang
x86 AVX support is runtime optional; MSVC and other architectures use fallback
code. Default selection and dependency removal are a separate follow-up change.

[Recommendations](FFT_ROUND3_RECOMMENDATIONS.md) ·
[Raw logs and reproduction](docs/performance/fft-round3/README.md)
