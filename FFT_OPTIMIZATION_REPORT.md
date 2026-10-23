# Heavyverb FFT optimization report

2026-10-22 · Odysseus · Intel Core Ultra 9 185H · Linux · GCC 16.2.1

**The custom engine is now 6.5× faster, using 84.7% less CPU.**
It keeps the complete IR, the same 256-sample wet latency, and the same two-stage
partition policy. PFFFT remains the default; the custom backend is ready for review.

## Final matched comparison

Four-second synthetic IR, 48kHz, 256-sample callbacks, 256/2048-sample partitions.
Median of five alternating trials, each processing 30 seconds of audio after
warming the full history. `-O3 -std=c++17`, no fast-math, FMA, or native-CPU
flags; thread pinned to logical CPU 1. Percentages are elapsed processing cost
relative to **one core's audio-time budget**, not whole-machine CPU usage.

| Backend | Mono | Two independent channels |
| --- | ---: | ---: |
| Original custom | 1.587% | 3.191% |
| Optimized custom | **0.244%** | **0.500%** |
| PFFFT | 0.182% | 0.371% |

The custom backend now costs about **34% more than PFFFT** here:
+0.062 percentage points of one core for mono.

## What each main change did

These are the separate three-trial confirmation series, on the same CPU and
workload above. “Less CPU” compares with the immediately preceding row; a
negative result means more CPU. Gains interact and cannot be multiplied as
independent estimates.

| Change added | Mono CPU | Less CPU |
| --- | ---: | ---: |
| Original full complex FFT | 1.620% | — |
| Dedicated real-input FFT | 0.896% | +44.7% |
| Separate real/imaginary planes | 0.372% | +58.4% |
| Sequential coefficient tables | 0.321% | +13.6% |
| Explicit SIMD, radix-2 stages | 0.329% | -2.2% |
| Radix-4 stages with SIMD | 0.263% | +20.0% |
| Shared internal bin order | 0.256% | +2.7% |

In simple terms: process only the information real audio needs, arrange it so
the CPU can work efficiently, combine FFT stages, and avoid reshuffling data.
The final cleanup also removes unused experimental tables, roughly halving
their FFT-plan footprint. That is a plan-memory saving, not half the IR memory.

## Issues, fixes, and ideas left out

- **Small apparent gains changed between runs.** I added longer, alternating
  confirmation trials. Pre-scaling the IR, replacing the initial clear with a
  product, explicit spectral-multiply intrinsics, callback batching, and extra
  reconstruction tables did not earn a place in the final default path.
- **Bigger tail blocks traded smoother timing for lower averages.** The balanced
  partition settings stay unchanged. Bin-major traversal was over three times
  as expensive and was rejected. FMA helped in its separate experiment, but
  remains opt-in because it requires a compatible target CPU.
- **Extremely tiny values can be expensive.** Flushing them to zero helps, but
  changes those tiny outputs. The engine preserves the host's floating-point
  policy; the diagnostic and numerical effect are logged.

## Validation and practical limits

183 convolution references pass on both backends; 60 FFT cases pass in both
SSE and scalar builds. The custom maximum FFT roundtrip error is 4.77e-7.
Allocation checks, address/undefined-behavior sanitizers, native Pd rendering,
and 15 stereo Heavy export cases pass. Custom **CLAP and VST3 builds succeed
without linking PFFFT**. DPF’s default fast-math setting was disabled to match
the tested numerical policy. No audible-response approximation was introduced.

The standard 96,000-callback diagnostic recorded no deadline misses. Separate
throughput runs still contain timing outliers, and deliberately demanding
stress cases miss deadlines on both backends: seven-sample callbacks, and
eight 96kHz instances with very small partitions. These are engine benchmarks;
game/console measurements and deadline guarantees remain outside this result.

[Detailed experiment decisions, raw logs, source snapshots, and reproduction](docs/performance/fft-optimization/README.md).
