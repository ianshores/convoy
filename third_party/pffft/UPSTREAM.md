# PFFFT single-precision subset

Source: https://github.com/marton78/pffft
Commit: aa16fd3db58de4ba5dae8b0438440bb9da46b6fa

Selected C99 single-precision implementation, headers, and license copied
without content changes. Paths are flattened for generated plugin projects.
The source-to-vendor mapping and SHA-256 hashes are in manifest.json.

This dependency retains its own license in LICENSE.txt. Heavyverb's original
code remains MIT. Compile pffft.c and pffft_common.c with PFFFT_STATIC_DEFINE;
include this directory. The SIMD selection headers choose SSE, NEON, WASM,
Altivec, or a scalar fallback for the target compiler. No fast-math flag is
required. Upstream's build system, doubles, and unrelated DSP components are
not included.
