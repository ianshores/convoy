# heavyverb

An MIT-licensed partitioned convolution engine, a `heavyverb~` Pd external,
and an experimental Heavy compiler adapter. The same engine runs inside Pd
and inside exported graphs. The default FFT backend is a pinned, vendored
single-precision PFFFT C library. This is a working first prototype.

The FFT splits long impulse responses into smaller blocks. Processing uses no
allocations, locks, or file access. Each object is mono; use two for independent
stereo channels. This is not a true-stereo four-path convolver.

## Try it in standalone PlugData

Build the external on Linux with a C++17 compiler and the Pure Data headers:

```sh
make pd PD_INCLUDE_DIR=/path/to/pure-data/src
```

Open [`examples/click-demo.pd`](examples/click-demo.pd) in **standalone**
PlugData. Enable DSP and Play clicks. This demo sends short noise bursts through
our `heavyverb~` convolution reverb and mixes the dry signal back in. Listen
for the reflections and decaying reverb tail. The example finds the external
in `../build` and includes a saved synthetic 48kHz reverb impulse response.
PlugData's DAW-hosted version does not support loading custom externals.

Use `[heavyverb~ array-name 256]` in your own graph. The second argument is the
partition size, a power of two from 16 through 8192, default 256. The wet signal
has exactly that many samples of latency (5.33ms for 256 at 48kHz). Disable and
re-enable DSP to load edits to an array; live IR replacement is not implemented.
IR samples are used as-is, without automatic resampling or normalization.
The preview external currently targets float32 Pd on Linux/macOS; the Linux
Makefile target is Linux-specific. The optional CMake build handles macOS.

## Export a graph through Heavy

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements-export.txt
.venv/bin/python tools/export.py examples/effect.pd build/effect --generators c dpf wwise
```

`effect.pd` is a stereo wet-only convolution reverb using the same saved
synthetic impulse response.
`export-test.pd` is a small reference graph with gain stages around the object.
The adapter registers a **new** Heavy primitive; it does not replace Heavy's
existing `conv~` or modify your installed compiler. It currently requires
hvcc 0.17.2 and uses private compiler APIs, so other versions are rejected.
Use this driver instead of PlugData's built-in Export command.

Only saved, finite, nonempty Pd array data is supported. Export freezes a copy
of that data; runtime writes to the original table do not change the IR.
`readsf~`, `soundfiler`, or loadbang-populated arrays cannot supply the exported
IR. The compiler shim under `pd/export` is for export only; do not add it to
PlugData's preview search path. Heavy runs in scalar mode, while convolution
uses its own FFT. Heavy's static CPU estimate excludes this operator.

To build Linux CLAP and VST3, supply a DPF checkout including its submodules
at revision `4238e1c7f0351bbe488d79f0899c540543ac7583`:

```sh
.venv/bin/python tools/build_plugins.py examples/effect.pd build/effect --dpf /path/to/DPF
```

The plugins are written to `build/effect/bin`. The wet latency is currently
not reported to the DAW for automatic compensation. Generated Wwise source
is also produced, but this repository has not yet built or tested that source
against a Wwise SDK. Unreal Submix and MetaSound integration remains to be
connected to the existing wrapper pipeline and tested. Those integrations
are not ready for review yet.

## Checks

```sh
make test benchmark
make check-export PYTHON=.venv/bin/python
make check-pd PD=/path/to/pd
```

The engine is checked against direct convolution in 173 cases, including a
dense four-second IR. Tests include
arbitrary callback sizes, full tails, exact in-place processing, reset, and
no allocations during processing. The export test builds and renders a dense
1597-tap stereo graph at three sample rates and five callback sizes. The Pd
test renders the real external headlessly and compares its output.

**Benchmark machine:** Intel Core Ultra 9 185H laptop running Linux, with one
benchmark thread pinned to logical CPU 1. Built with GCC 16.2.1, `-O3`, C++17
for the engine and C99 for PFFFT, without fast-math flags.

The figures below use a dense generated IR at 48kHz, 256-sample early
partitions, 2048-sample tail partitions, and 64- or 256-sample host callbacks.
History is fully warmed before timing; results are the median average CPU
usage from three runs. Percentages are relative to **one CPU core**.

| IR duration | Mono | Two independent channels |
| --- | ---: | ---: |
| 1 second | ~0.08% | ~0.16% |
| 4 seconds | ~0.18% | ~0.37% |

On that same machine and matched four-second workload, the prior uniform PFFFT
implementation uses about 1.25% mono / 2.63% for two channels. Two-stage
partitioning reduces average CPU by roughly seven times while preserving the
full IR and requested wet latency.

These are standalone engine measurements. See [PERFORMANCE.md](PERFORMANCE.md)
for the raw results, callback timing checks, and retained timing outliers.
Average CPU figures are not hard realtime guarantees. In-game, mobile, and
console performance remain unmeasured.

**Custom FFT comparison on the same Intel Core Ultra 9 185H:** both FFT
backends now share the exact same two-stage partitioning code. With a dense
four-second IR at 48kHz and 256-sample callbacks, the custom radix-2 FFT uses
about **1.62% of one core mono / 3.19% for two channels**, versus PFFFT's
**0.18% / 0.37%**. The current custom implementation is approximately nine
times more expensive in this configuration. PFFFT remains the default while
future custom FFT improvements can be measured against this baseline. See
[the four-way comparison](PERFORMANCE.md#custom-fft-with-identical-partitioning-latest).

Long IRs use small blocks for early reflections and eight-times-larger blocks
for the tail, capped at 8192 samples. The tail is time-aligned with the early
path; no response samples are discarded. For the default 256-sample setting,
the first 2048 IR samples use small blocks and the rest use 2048-sample blocks.
The policy activates for IRs longer than 4096 samples at this setting. Uniform
PFFFT processing remains available through `HEAVYVERB_UNIFORM_PARTITIONS`, or
CMake's `-DHEAVYVERB_UNIFORM_PARTITIONS=ON`; use
`make build/heavyverb_benchmark_uniform` to build its comparison benchmark.

Both FFT backends have 173 direct-reference cases. A Linux linker-interception
test also checks for C allocations inside PFFFT during processing/reset.
The dependency-free custom FFT, including two-stage partitioning, is available by defining
`HEAVYVERB_PORTABLE_FFT`, or configuring CMake with
`-DHEAVYVERB_PORTABLE_FFT=ON`. Make builds the default and portable core tests;
`make build/heavyverb_benchmark_portable` builds its two-stage comparison.
`make build/heavyverb_benchmark_portable_uniform` builds the custom/uniform
comparison. `HEAVYVERB_UNIFORM_PARTITIONS` applies equally to both FFT backends.

CMake is also supplied for core tests and optional external builds:
`cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release` then
`cmake --build build-cmake` and `ctest --test-dir build-cmake`.

## License

Original heavyverb code is under [MIT](LICENSE). The vendored PFFFT subset
retains its [upstream license](third_party/pffft/LICENSE.txt), provenance, and
source hashes. Export copies that license along with the C sources and SIMD
headers. hvcc, its generated runtime, DPF, Pd headers, and SDK wrappers retain
their own notices and terms. Keep those notices when distributing an
exported plugin. See [DEVELOPMENT.md](DEVELOPMENT.md) for the first-run debrief.

## IR collection candidate

[Conner's Impulse Response Library](https://github.com/itsmusician/IR-Library)
explicitly applies MIT to its IR audio, including contributor files. Its README
lists 738 IRs; the current WAV files occupy about 1.20GB, covering rooms, outdoors, plates, springs, hardware,
and other sounds. The [license notice](https://github.com/itsmusician/IR-Library/blob/main/License.md)
must accompany redistributed files. None of that collection is bundled here
yet; the current demos still use our generated response. The local collection
is for auditioning; Ian must approve the sound before any selected responses
are used in the project. Verified 2026-10-22.
