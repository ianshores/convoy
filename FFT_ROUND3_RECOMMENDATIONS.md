# Third-round FFT investigation and plan

2026-10-22. Baseline commit `7074a52`. Objective: repeat measured optimization
rounds until the custom convolver beats the pinned PFFFT implementation on the
established eight workloads, without approximating the response or increasing
latency. A win means repeated whole-engine timings, with numerical reference
checks passing, not merely an isolated kernel win.

## Current bottleneck and next experiments

The second round left the 512-point FFT pair about twice as expensive as PFFFT;
short IRs remain roughly one-third more expensive overall. The reconstruction
loops walk natural bins, look up permuted addresses, and process pairs scalarly.
The complex FFT's digit-reversed order has useful structure: conjugate pairs
can be grouped into contiguous runs with the second run reversed. Exploiting
this structure should remove indexed scalar traffic without an extra reorder.

1. Construct reconstruction runs in physical bin order, store twiddles in that
   order, and process pairs with four-float SIMD using reversed contiguous loads.
   Keep an independently tested scalar path and handle self-paired/end bins.
2. Specialize the zero-padded forward transform used by overlap-add convolution.
   Only the first half of the time block contains input, so some first-stage
   arithmetic can be simplified exactly. Preserve the general FFT API for tests.
3. If needed, investigate a fused terminal radix-8 stage, larger accumulation
   batches, and block-based streaming to reduce fixed overhead. Shared-wrapper
   changes must be applied to both backends in the comparison.

After every candidate: immutable header snapshot, 183 direct convolution cases,
60 FFT cases, component diagnostics, and three 30-second-audio throughput runs.
Record rejections and correctness issues as well as wins. Confirm candidates
against the starting version and PFFFT in shuffled repeated trials. Retain full
IRs, 48kHz sample rate, 256/2048 partitions, 256-sample wet latency, ordinary
floating-point semantics, and the existing host denormal policy. No fast-math,
new CPU instruction requirement, silence shortcut, or quality approximation.

Benchmarks run locally on Intel Core Ultra 9 185H, GCC 16.2.1, Linux, logical
CPU 1. PFFFT stays unmodified unless a shared scheduling change applies equally.
Cradle remains untouched. End validation includes scalar correctness, allocation
checks, ASan/UBSan, exported Heavy graph and native Pd renders, and plugin builds.

## Follow-up after the first two candidates

Vector reconstruction wins initially for long IRs but leaves short IRs behind.
The pruned forward path passes both the normal FFT tests and 60 additional
half-input cases; a template naming collision was fixed before any timing.
Next isolate circular-history traversal from accumulation batching: first use
contiguous history runs with the existing two-product addition order, then try
four products per accumulator visit. This removes repeated integer modulo and
keeps input pointers simple enough for vectorization. Measure each separately.

The contiguous-history and four-product segment variants both regress initially;
restore the original paired accumulation. Next test compile-time radix schedules
for the common 512- and 4096-point transforms. Their lengths and twiddle offsets
are already fixed for the instance; specializing the schedule may remove loop
and plan-lookup overhead while retaining the general path for other sizes.

## Further iteration: vector-packed spectra

Static schedules provide only a small initial gain. The existing paired MAC
still uses eight input pointers plus two sum pointers. Test adjacent groups of
four real and four imaginary values, keeping the custom bin permutation. The
FFT must read/write this format directly; adding conversion passes would hide
its benefit. Pair accumulation then needs four input pointers and one sum
pointer. Keep ordinary complex products and separate DC/Nyquist slots (no
PFFFT-special packed endpoint math). Add independent packed-path FFT tests.
This is a custom implementation of a data-layout technique; no PFFFT code is
copied. Compare complete engine cost, memory, and accuracy before retaining it.

The four-partition packed mixer improves both IR lengths, bringing short IRs
close to PFFFT while long IRs lead initially. Try eight products per accumulator
visit next. Keep product addition order and test register-pressure effects;
if large tails regress, test restricting the eight-way path to the eight-partition
head. No wider instruction set or arithmetic approximation is needed for this.

Eight-way packed mixing regresses initially; restore four-way mixing. Compiler
inspection shows the smaller packed block routine was inlined into streaming
callers. Test keeping this infrequent, expensive block routine out of the hot
per-sample loop. This changes code placement/call overhead only, not math.

Keeping the block routine out of line does not help initially. Restore the
four-way packed mixer. The short-IR gap is small but still present. Next test
an eight-bin packed layout, followed by an optional AVX float mixer selected
once at construction on supported GCC/Clang x86 builds. Preserve SSE/scalar
fallbacks and generic compiler flags: AVX is a runtime optimization, not a new
minimum CPU requirement. Test layout alone first so its effect is separate.
PFFFT's pinned float implementation uses four-wide SIMD; disclose the selected
custom path and also measure a PFFFT AVX-targeted build as a comparison control.
Do not enable FMA or fast-math for this experiment.

The runtime AVX mixer leads the default PFFFT build in all eight initial cases.
Before declaring success, measure the AVX-targeted PFFFT control and try applying
the same eight-float width to the reverse-contiguous reconstruction runs. Keep
FFT butterflies and fallback code intact. Confirm all variants in shuffled
trials, then test the full-tail error floor and final host/export builds.

The AVX-targeted PFFFT control is faster than its generic build on short IRs,
and remains close to or ahead of the custom candidate there. Do not claim a
broad win yet. Extend optional AVX to the large radix-4 butterflies next, using
eight independent bins per vector with identical arithmetic order. Retain SSE
for small terminal stages and the pruned input stage. All CPU dispatch remains
outside ISA-specific execution and the fallback remains available.
