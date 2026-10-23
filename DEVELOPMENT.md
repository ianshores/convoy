# First prototype debrief

**Working:** a convolution engine, a native Pd object, and a custom Heavy
export path. Linux CLAP and VST3 binaries build. The original code uses MIT.

## Steps taken

1. Cloned the empty public repository and added MIT licensing.
2. Built an FFT-based engine that handles the response in smaller blocks.
3. Wrapped it as a Pd object that reads a named array when DSP starts.
4. Added a Heavy adapter so the object can sit between other objects in an
   exported graph, sharing the exact same convolution engine.
5. Added a click demo and a stereo effect graph, then built Linux plugins.
6. Compared both the Pd object and exported graph with independent reference
   audio, without opening or disturbing REAPER or PlugData.

## Issues found and fixes

| Issue | Fix |
| --- | --- |
| Heavy does not recognize a new Pd external automatically. | Registered a distinct compiler primitive and supplied its code generator. |
| Heavy may initialize the IR table after the convolution object. | Embedded the saved coefficients into the generated object's initialization. |
| Audio hosts use different callback sizes. | Buffered samples internally; tested sizes 1, 7, 127, 256, and 1023. |
| Heavy's generated initialization expects a memory-size expression. | Used an initialization expression compatible with that contract. |
| Heavy's stock CPU estimate cannot model this FFT engine. | Excluded it from the static estimate and added a real benchmark. |
| `vline~` is unsupported by this Heavy version. | Used a supported `line~` envelope in the click demo. |
| CMake is absent from the local machine. | Added and used a simple Makefile and direct compiler checks. |
| Leak detection cannot run under the sandbox tracer. | Address and undefined-behavior checks passed with leak detection disabled; leak detection remains unverified. |
| Pd writes four-byte WAV samples as floats. | Made the headless audio check read that format correctly. |

## Results and limits

101 engine reference cases passed, including a dense four-second IR, including complete tails and allocation
checks. Fifteen exported stereo graph renders passed; maximum error was below
0.00000001. The native Pd render matched within 0.000001. Address and undefined-behavior sanitizer checks passed (leak detection
was disabled because the sandbox tracer prevents it). The original four-second IR benchmark averaged about 12% of one CPU core per
mono instance. Follow-up optimization reduced this to about 1.9% mono / 3.8%
for two independent channels at 48kHz, without changing latency or truncating
the response. Details and raw measurements are in PERFORMANCE.md.

This prototype adds one partition of wet latency. It supports fixed saved
IRs, not live IR changes or resampling. PlugData preview requires standalone
mode. Wwise source generation is checked; Wwise SDK builds, Unreal Submix,
MetaSound, and interactive DAW listening tests remain future verification.

## PFFFT follow-up

Replaced the default FFT with a pinned PFFFT C library, keeping the C++ engine
and previous backend available. Four-second CPU fell from about 1.9% mono /
3.9% for two channels to 1.3% / 2.7%. One-second mono is about 0.3%. The full
response and latency are unchanged. Source hashes and dependency license are
retained and exported. Core, C-allocation, exported audio, native Pd, sanitizer,
Make, CMake, and Linux plugin build checks pass.

One ordinary-priority timing observation exceeded its callback deadline; this
remains part of realtime verification. Fixed a headless test race by recording
into arrays before writing the WAV synchronously. Full measurements are in
PERFORMANCE.md. No new UI sessions were opened.

## Long-tail optimization

Added two-stage partitioning: small blocks handle early reflections and larger
blocks handle later response samples, aligned to preserve the same wet latency.
The four-second benchmark now uses about 0.18% mono / 0.37% for two channels,
versus about 1.3% / 2.7% with uniform PFFFT. Nothing is removed from the IR.

Expanded checks to 173 cases, including exact split boundaries, history wrapping,
complete tails, reset, and in-place processing. Allocation checks, exported and
native audio renders, sanitizers, CMake configurations, and Linux plugin builds
pass. A 96,000-callback diagnostic had no deadline misses; earlier wall-time
outliers remain recorded. Unreal/Wwise performance and gameplay checks still
need Cradle. Cradle was not contacted. The downloaded IR collection remains
for auditioning until Ian approves a selection; it was not used for these tests.

## Custom FFT comparison

Moved two-stage scheduling above both FFT backends and tested four matched
configurations on the Intel Core Ultra 9 185H laptop. With a dense four-second
48kHz IR, 256-sample callbacks, and 256/2048-sample two-stage partitions, custom
FFT uses about 1.62% mono / 3.19% for two channels; PFFFT uses 0.18% / 0.37% of
one core. The custom variant's penalty is about ninefold. One-second custom
FFT is slower with this two-stage policy than with uniform blocks.

PFFFT remains the default. The custom two-stage path is now a tested baseline
for future FFT optimization. All 173 reference cases pass on both backends;
the custom path also passes allocation/sanitizer checks and a headless Pd
render, with no PFFFT symbols in its built Pd module. Default exported audio
and Linux plugin builds pass. Callback outliers and the custom diagnostic's
one deadline miss are retained in PERFORMANCE.md and the raw data. No Cradle
work or IR-library adoption was performed.


## Custom FFT optimization guide follow-up — 2026-10-22

The previous custom radix-2 comparison above is now historical. The optimized
real-input, four-float SSE, radix-4 DIF/DIT implementation passes 183 convolution
references and 60 transform cases per execution path. See
[the final report](FFT_OPTIMIZATION_REPORT.md) for the measured staircase,
rejected ideas, current CPU comparison, numerical checks, and remaining limits.
