// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
// Compatibility layer for the FFT now owned by the shared DSP library.
#pragma once
#include "convoy_legacy_options.hpp"
#if defined(CONVOY_FFT_SCALAR) && !defined(DSP_FFT_SCALAR)
#define DSP_FFT_SCALAR CONVOY_FFT_SCALAR
#endif
#if defined(CONVOY_FFT_FMA) && !defined(DSP_FFT_FMA)
#define DSP_FFT_FMA CONVOY_FFT_FMA
#endif
#if defined(CONVOY_DISABLE_AVX) && !defined(DSP_DISABLE_AVX)
#define DSP_DISABLE_AVX CONVOY_DISABLE_AVX
#endif
#include <dsp/fft.hpp>
#if defined(DSP_FFT_SSE) && !defined(CONVOY_FFT_SSE)
#define CONVOY_FFT_SSE DSP_FFT_SSE
#endif
#if defined(DSP_HAS_AVX_RECONSTRUCTION) && !defined(CONVOY_HAS_AVX_RECONSTRUCTION)
#define CONVOY_HAS_AVX_RECONSTRUCTION DSP_HAS_AVX_RECONSTRUCTION
#endif
namespace convoy {
namespace detail {
using RealFFT = ::dsp::RealFFT;
} // namespace detail
} // namespace convoy
