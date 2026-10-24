# Convoy

**Fast, lightweight convolution for the audio pipeline.**

Convoy is an MIT-licensed C++17 convolution engine for real-time DSP. Use it for
reverb, cabinet and microphone responses, spatial filtering, or any finite
impulse response. The same engine can underpin sound-design tools, offline
editing, plugins, and interactive audio systems.

The engine is header-only and uses the FFT from the
[shared DSP library](https://github.com/ianshores/dsp). Its processing and reset
operations allocate no memory, acquire no locks, and access no files. Runtime
AVX acceleration is available on supported GNU/Clang x86 builds, with SSE and
scalar fallbacks. Host integrations are developed and tested separately.

## Use the engine

Clone with `git clone --recurse-submodules`, add `include/` and `vendor/dsp/include/` to your include
path, and compile as C++17:

```cpp
#include <convoy.hpp>

float impulse[] = {1.0f, 0.5f, 0.25f};
// Construct off the audio thread; keep the instance between callbacks.
convoy::Convolver filter(impulse, 3, 256);

// In each audio callback, with caller-owned float input/output buffers:
filter.process(input, output, frame_count);
```

Each instance processes one channel. Use two instances for independent stereo
or build an explicit multichannel routing matrix in your host. Input and output
may be the same buffer; partially overlapping buffers are unsupported. Callback
lengths may vary. The partition size sets the wet latency: 256 samples is
5.33ms at 48kHz. The host decides how to report or compensate that latency.

With CMake:

```cmake
add_subdirectory(path/to/convoy) # from a recursive clone
target_link_libraries(MyAudioTarget PRIVATE convoy::convoy)
```

For an installed package, use `find_package(Convoy CONFIG REQUIRED)` and the
same `convoy::convoy` target. Set `BUILD_TESTING=OFF` when embedding the project
if you do not want its test targets. [API and integration contract](docs/API.md).

## Repository boundaries

This repository owns the convolution algorithm, C++ API, correctness tests,
benchmarks, and performance records. The shared DSP library owns the FFT and
future reusable primitives; its revision is pinned as a submodule here.

[Foundry](https://github.com/ianshores/foundry) owns deployment: Pd/PlugData
adapters, optional Heavy compilation, plugin editors, CLAP/VST3 packaging,
Wwise wrappers, Unreal Submix and MetaSound integration, and host validation.
Foundry consumes a pinned Convoy revision as an external module. Small reusable
DSP building blocks live together in the shared library.

Convoy can be embedded directly in C++ without Foundry or Heavy. To audition it
in a DAW or PlugData, follow [Foundry's Convoy adapter guide](https://github.com/ianshores/foundry/tree/main/adapters/convoy).
Target readiness and SDK requirements are recorded there.

## Design and quality

Long responses use two partition sizes: small blocks for the start of the IR,
then blocks eight times larger, capped at 8192 samples. At the default setting,
the first 2048 samples use 256-sample blocks and the remainder use 2048-sample
blocks. The shared FFT uses radix-4 stages, packed spectra, half-input pruning, and
batched accumulation.

All IR coefficients are retained by default. The optional
`set_tail_cutoff_db()` control skips trailing whole partitions below an absolute
coefficient threshold and fades their contribution over eight block updates.
Set it to `0` for the full response. A coefficient threshold such as -96dB does
not guarantee a -96dBFS output difference; contributions can add together.
See the [cutoff report](CUTOFF_REPORT.md).

The engine does not decode files, resample or normalize IRs. Supply a finite,
nonempty float IR at the intended processing rate. Replacing an IR means
constructing another instance; ownership, crossfading and lifetime management
belong to the host.

## Build and verify

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

`make test` also exercises forced SSE/scalar FFT paths and allocation checks.
`make quality` checks residual error against double-precision convolution.
`make benchmark` measures the current engine. No SDK, Python package, or audio
host is required for the core checks.

CMake options: `CONVOY_DISABLE_AVX=ON` forces the fallback;
`CONVOY_UNIFORM_PARTITIONS=ON` selects uniform partitioning for comparison.
`convoy.hpp` and `convoy::Convolver` are the current API. Compatibility headers
retain the `heavyverb.hpp` include and `heavyverb::Convolver` spelling for
existing consumers. Historical benchmark sources and records keep their
original identifiers so the measurements remain traceable.

## Measured performance

**Machine:** Intel Core Ultra 9 185H, Linux, GCC 16.2.1, logical CPU 1.
48kHz, dense four-second synthetic IR, 256-sample callbacks and wet latency,
256/2048 partitions, seven shuffled 30-second-audio trials, `-O3`, no fast-math
or FMA. Median processing cost as a percentage of one core's audio budget:

| Engine | Mono | Two independent channels |
|---|---:|---:|
| Convoy, runtime AVX | **0.1495%** | **0.3165%** |
| PFFFT comparison, generic | 0.1813% | 0.3801% |
| PFFFT comparison, compiled with `-mavx` | 0.1717% | 0.3640% |

These are whole-convolver measurements for this CPU and workload. The recorded
custom implementation leads the AVX-targeted comparison by 4–14% across eight
workloads. Full-tail comparisons at 44.1/48/96kHz yield 131.94–134.97dB residual
SNR. Average CPU cost does not establish worst-case callback timing on another
machine. The rename and repository split do not change the DSP algorithm.

[Performance overview](PERFORMANCE.md) · [Optimization report](FFT_ROUND3_REPORT.md)
· [Raw records](docs/performance/fft-round3/README.md)

## License

Convoy's original engine and FFT are [MIT licensed](LICENSE).
Deployment tools and host dependencies retain their own licenses in Foundry.
