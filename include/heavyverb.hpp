// SPDX-License-Identifier: MIT
// Compatibility include; new code uses convoy.hpp and convoy::.
#pragma once
#include "convoy_legacy_options.hpp"
#include "convoy.hpp"
#ifndef CONVOY_LEGACY_NAMESPACE
#define CONVOY_LEGACY_NAMESPACE
namespace heavyverb = convoy;
#endif
#if defined(CONVOY_FFT_SSE) && !defined(HEAVYVERB_FFT_SSE)
#define HEAVYVERB_FFT_SSE CONVOY_FFT_SSE
#endif
#if defined(CONVOY_HAS_AVX_MIXER) && !defined(HEAVYVERB_HAS_AVX_MIXER)
#define HEAVYVERB_HAS_AVX_MIXER CONVOY_HAS_AVX_MIXER
#endif
#if defined(CONVOY_HAS_AVX_RECONSTRUCTION) && !defined(HEAVYVERB_HAS_AVX_RECONSTRUCTION)
#define HEAVYVERB_HAS_AVX_RECONSTRUCTION CONVOY_HAS_AVX_RECONSTRUCTION
#endif
