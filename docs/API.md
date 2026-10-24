# Convoy engine contract

Include `convoy.hpp` and use `convoy::Convolver` with C++17 or newer.

| Operation | Contract |
|---|---|
| `Convolver(ir, length, partition = 256)` | Build off the audio thread. Copies/compiles the IR; the caller may release the source afterwards. Rejects empty, nonfinite or oversized IRs and unsupported partition sizes. |
| `process(input, output, count)` | Mono float buffers; arbitrary callback lengths; exact in-place use supported. No allocation, locks or I/O. Serialize calls per instance. |
| `latency()` | Wet-path delay in samples, equal to the requested partition size. |
| `reset()` | Clear audio history without reallocating. Serialize with processing. |
| `set_tail_cutoff_db(db)` | Audio-thread control, serialized with process/reset. Zero restores the full response; negative finite values set an absolute IR coefficient threshold. |
| `retained_samples()` | Current retained IR span including the early stage. |
| `memory_bytes()` | Estimated owned object/buffer storage; excludes allocator bookkeeping. |

The partition size must be a power of two from 16 through 8192. IR length is
limited to 16,777,216 samples. IR coefficients are used at their supplied gain
and rate. File decoding, rate conversion, gain staging and device sample rates
are host responsibilities.

A Convolver is stateful. Process each instance on one audio thread and destroy
it away from the audio callback. Use separate instances for independent streams.
Preparing another instance does not transfer audio history; a host can prime
and crossfade instances when changing IRs.

`CONVOY_DISABLE_AVX` disables optional runtime AVX paths. `CONVOY_FFT_SCALAR`
forces scalar FFT code. `CONVOY_UNIFORM_PARTITIONS` disables the larger tail
stage. Compile every translation unit using the same configuration.

CMake installs a relocatable `ConvoyConfig.cmake` and the `convoy::convoy`
interface target. The public module metadata in `module.json` describes its
entry point without depending on any deployment system.
