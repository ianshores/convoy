# Custom FFT: second-round investigation and recommendations

Written before implementation, 2026-10-22. Baseline: commit `4ebe2e9`.

## What explains the remaining gap

Both backends use the same two-stage partition schedule and four-float SSE on
this machine. PFFFT is not winning through wider AVX instructions, reduced IR
length, lower fidelity, or additional latency.

Local inspection of the pinned PFFFT source shows vectorized real-transform
pre/postprocessing (`FUNC_REAL_FINALIZE_4X4`, `FUNC_REAL_PREPROCESS_4X4`),
specialized radix kernels, aligned native unordered spectra, and normalization
inside its convolution multiply. Its native layout alternates four real and
four imaginary values and packs DC/Nyquist into existing slots.

Our custom transform uses an N/2 complex FFT, then reconstructs the real
spectrum through indexed scalar accesses. Small terminal radix stages execute
scalar butterflies, including general complex multiplication by unity. The
inverse makes a separate normalization pass. Spectral planes have an odd
B+1 stride; each successive partition starts at a different vector alignment.

Initial component measurements on **Intel Core Ultra 9 185H**, Linux,
GCC 16.2.1, `-O3`, CPU 1, no fast-math/FMA/native flags:

| Operation | Custom | PFFFT |
| --- | ---: | ---: |
| 512-point forward + inverse | 1.723 µs | 0.555 µs |
| 4096-point forward + inverse | 17.528 µs | 5.428 µs |
| 93-partition spectral accumulation | 55.937 µs | 54.098 µs |

These are initial diagnostic samples, not repeated medians. PFFFT inverse is
unnormalized; its multiply includes normalization. Both measured multiply
loops clear the accumulator. Different layouts contain the same useful bins.
The full-engine benchmarks include all work and determine retention.
The evidence points primarily to transform structure, rather than a dramatic
multiply-loop advantage. Data layout is a plausible contributor, not a
hardware-counter-proven cache diagnosis.

## Experiments, in order

1. **Fuse inverse scaling with output packing.** Remove one scratch read/write
   pass, keeping normalized transform semantics. Small, low-risk opportunity.
2. **Vectorize terminal radix stages across independent groups.** Handle q=1
   and q=2 explicitly; omit unity multiplications. This targets work that the
   existing across-bin vector loop cannot cover.
3. **Compute conjugate pairs together in real reconstruction.** Each pair
   shares input values, sums/differences, and coefficients. Compute both bins
   once rather than doing the same pair twice. Check endpoints separately.
4. **Pad spectral-plane strides to a vector multiple.** Keep unique bins and
   public FFT layout unchanged; zero padding, avoid misaligned partition
   starts and scalar multiply tails. This increases memory slightly.
5. **Pair partition accumulation.** Load a spectral sum once for two products,
   retaining the original per-partition addition order. Test whether less sum
   traffic outweighs extra simultaneous input streams. Avoid the previously
   unsuccessful bin-major traversal.

Retain only gains supported by repeated whole-engine trials across one/four
second IRs, one/two engines, and 64/256 callbacks. Each experiment gets an
immutable source snapshot, correctness output, component timings, and three
30-second-audio trials before proceeding. Repeat baseline, finalists, and
PFFFT in shuffled order for final confirmation. Negative results stay in the
report. If a candidate is rejected, the next starts from the retained state.

## Controls and acceptance

48kHz, dense seeded synthetic IRs, B=256, tail=2048, wet latency=256. Full-history
warmup; ordinary process priority; sequential CPU-1-pinned timing. CPU means
elapsed processing time divided by represented audio time, as a percentage
of one core. Compilation and IR preparation are excluded. Final comparison
uses five shuffled 30-second trials. Preserve p99/max and all outliers.

Run all 183 direct-convolution and 60 independent FFT cases after each change.
Check the final scalar fallback, processing allocations, sanitizers, generated
Heavy graph, and native Pd external. No host denormal policy changes, fast-math,
IR truncation, new CPU feature requirement, or partition-policy changes.

PFFFT's deeper vector-native real-transform architecture could require a new
algorithm/layout, beyond these incremental experiments. Do not claim the
entire remaining difference is explained by any one source-code observation.
No upstream implementation code is copied into the MIT custom engine.
