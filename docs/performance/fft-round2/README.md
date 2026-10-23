# Second-round FFT experiment archive

Read the [full report](../../../FFT_ROUND2_REPORT.md) and the
[recommendations written before implementation](../../../FFT_ROUND2_RECOMMENDATIONS.md).

## Conditions

Intel Core Ultra 9 185H on Odysseus, Linux 7.2.5, GCC 16.2.1, CPU affinity 1,
ordinary priority. C++17/C99 with `-O3`; no fast-math, native-CPU, or FMA flags.
The main eight workloads cover 1/4-second dense seeded IRs, 1/2 engines,
64/256-sample callbacks, 48kHz, B=256, tail=2048, wet latency=256. CPU is elapsed
processing/audio duration as percent of one core. Setup/compilation/warmup are
excluded. This is not an OS utilization percentage or a realtime guarantee.

## Files

Each `r2-*` directory contains metadata with source hashes and exact flags,
reference-test output, one initial component diagnostic, three initial
`run-*.csv` trials, and five `confirmation-*.csv` trials. Every throughput trial
represents 30 seconds of audio per case after full-history warmup. The
confirmation schedule interleaves all versions in deterministic shuffled order.
Initial and confirmation results are distinct series; the report uses confirmation.

| Directory | Cumulative state |
| --- | --- |
| `r2-00-control` | Custom at commit `4ebe2e9` |
| `r2-00-pffft` | PFFFT at the same revision, unchanged algorithm |
| `r2-01-fused-scaling` | Control + fused inverse scale/output copy |
| `r2-02-terminal-simd` | Previous + SIMD terminal radix stages |
| `r2-03-paired-reconstruction` | Previous + paired real-spectrum reconstruction |
| `r2-04-padded-stride` | Previous + padded spectral strides |
| `r2-05-paired-mac` | Previous + paired partition accumulation; selected final |

- `summary.json`: CPU median/min/max, median p99, worst callback, and deadline
  for every case and series. No outlier removal.
- `components-1.txt` through `components-5.txt` in control/final/PFFFT folders:
  repeated transform/helper diagnostics. `components-summary.json` holds medians.
- `partition-components-*.txt`: complete uniform building-block measurements
  using `tests/partition_benchmark.cpp`, including the actual selected MAC path.
  The old `mac_us` field in `components-*.txt` times a single-partition helper
  with unpadded bins; it must not be treated as a paired-MAC measurement.
- `source-snapshots.tar.gz`: exact headers for each experimental binary.
  `snapshot-audit.txt` verifies all hashes and that current production equals
  the final measured snapshot after formatting. `final-source.json` records
  production hashes. Pure formatting did not change any algorithm or default.
- `validation*.txt`: Make, scalar, opt-in FMA correctness, ASan/UBSan, Heavy
  export, CMake, native Pd, and custom CLAP/VST3 build output. Sanitizer leak
  detection was disabled. FMA was not used for performance comparisons.
- `runtime/`: single-pass callback-thread timing, load stress, and scoped
  quiet-signal FTZ/DAZ diagnostics for both backends, with source metadata.
- `plugins.json`: rebuilt custom binary hashes and engine-symbol verification.
  These binaries were not auditioned interactively during this round.

## Reproduce or extend

For a **new candidate** in the current production code:

```sh
python3 tools/fft_experiment.py r2-new-name --production \
  --output-root docs/performance/fft-round2 --runs 3 --seconds 30
```

Labels must be fresh; the runner rejects an existing label. `--production` is
important: without it, the generic runner uses the preserved first-round
experimental headers. PFFFT uses `--pffft` instead. Candidate tests run before
throughput. The runner retains raw CSVs and a private build header snapshot.

For **historical reproduction**, extract the archived headers into the ignored
build tree and compile a selected version using the current benchmark harness:

```sh
mkdir -p build/fft-optimization
# Archive paths are r2-*/include/*.hpp; this does not edit production include/.
tar -xzf docs/performance/fft-round2/source-snapshots.tar.gz -C build/fft-optimization
c++ -O3 -std=c++17 -Wall -Wextra -DHEAVYVERB_PORTABLE_FFT \
  -Ibuild/fft-optimization/r2-05-paired-mac/include tests/benchmark.cpp \
  -o build/fft-optimization/r2-05-paired-mac/benchmark
HEAVYVERB_BENCH_SECONDS=30 taskset -c 1 \
  build/fft-optimization/r2-05-paired-mac/benchmark
```

Substitute any custom snapshot label. PFFFT requires its C objects, without the
custom definition:

```sh
make build/pffft.o build/pffft_common.o
c++ -O3 -std=c++17 -Wall -Wextra -DPFFFT_STATIC_DEFINE \
  -Ibuild/fft-optimization/r2-00-pffft/include -Ithird_party/pffft \
  tests/benchmark.cpp build/pffft.o build/pffft_common.o \
  -o build/fft-optimization/r2-00-pffft/benchmark
```

After reconstructing all desired benchmark binaries, run
`python3 tools/confirm_fft_round2.py --prefix rerun --labels LABEL...` to preserve
another shuffled five-trial series. The default confirmation series is resumed,
never overwritten. `summarize_fft_round2.py` summarizes the original `run` and
`confirmation` prefixes only; reruns should be compared as separate sessions.
`profile_fft_round2.py` generates repeated component and full-block diagnostics
from the saved binaries. Do not run other CPU-intensive tests concurrently with
benchmarks. Compiler/version/CPU changes require a new matched baseline.

## Interpretation limits

The standard throughput series includes callback deadline outliers. The separate
96,000-callback diagnostic had no misses for either backend, but this does not
cancel out those other observations. Eight engines at 96kHz with 64-sample
partitions/callbacks exceeded the deadline for both backends. Near-subnormal
signals remain expensive unless the host applies a flush-to-zero policy.
The engine does not change that policy. See the report for counts and values.

No Cradle, Unreal, Wwise, console, or mobile measurements were performed.
The synthetic IRs do not incorporate the unauditioned IR library.
