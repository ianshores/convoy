// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
// Define HEAVYVERB_PORTABLE_FFT to use the dependency-free reference backend.
#ifdef HEAVYVERB_PORTABLE_FFT
#include "heavyverb_portable.hpp"
#else
#include "heavyverb_pffft.hpp"
#endif
