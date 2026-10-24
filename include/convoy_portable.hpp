// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include "convoy_fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(CONVOY_FFT_SSE) && !defined(CONVOY_DISABLE_AVX) &&                               \
    (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#define CONVOY_HAS_AVX_MIXER 1
#endif

namespace convoy {
namespace detail {
// Uniform partitioned overlap-add convolution. Configure on a non-audio thread;
// process/reset never allocate. One instance per channel; B samples of latency.
class PartitionedConvolver {
    friend struct ConvolverProbe;
    // Spectrum has B+1 useful bins; zero padding keeps strides a SIMD multiple.
    std::size_t b_, n_, bins_, partitions_, cursor_ = 0, position_ = 0;
    std::size_t active_partitions_ = 0, target_partitions_ = 0;
    bool transitioning_ = false;
    float cutoff_db_ = 0.f;
    std::vector<float> suffix_peak_, gains_;
    RealFFT fft_;
#ifdef CONVOY_HAS_AVX_MIXER
    const bool avx_ = __builtin_cpu_supports("avx") != 0;
#endif
    std::vector<float> work_, filters_, history_, sum_;
    std::vector<float> input_, output_, overlap_;

    // Separate real/imaginary arrays let the compiler vectorize across FFT bins.
    static void accumulate(float *__restrict sr, float *__restrict si, const float *__restrict ar,
                           const float *__restrict ai, const float *__restrict br,
                           const float *__restrict bi, std::size_t count) noexcept {
        for (std::size_t k = 0; k < count; ++k) {
            sr[k] += ar[k] * br[k] - ai[k] * bi[k];
            si[k] += ar[k] * bi[k] + ai[k] * br[k];
        }
    }
#ifdef CONVOY_HAS_AVX_MIXER
    __attribute__((target("avx"), always_inline)) static inline void
    avx_add(__m256 &sr, __m256 &si, const float *h, const float *x, std::size_t k) noexcept {
        const auto ar = _mm256_loadu_ps(h + k), ai = _mm256_loadu_ps(h + k + 8);
        const auto br = _mm256_loadu_ps(x + k), bi = _mm256_loadu_ps(x + k + 8);
        sr = _mm256_add_ps(sr, _mm256_sub_ps(_mm256_mul_ps(ar, br), _mm256_mul_ps(ai, bi)));
        si = _mm256_add_ps(si, _mm256_add_ps(_mm256_mul_ps(ar, bi), _mm256_mul_ps(ai, br)));
    }
    template <std::size_t Batch>
    __attribute__((target("avx"))) static void
    accumulate_avx(float *__restrict sum, const float *h, const float *x, const float *x2,
                   const float *x3, const float *x4, std::size_t count) noexcept {
        for (std::size_t k = 0; k < count; k += 16) {
            auto sr = _mm256_loadu_ps(sum + k), si = _mm256_loadu_ps(sum + k + 8);
            avx_add(sr, si, h, x, k);
            if constexpr (Batch >= 2)
                avx_add(sr, si, h + count, x2, k);
            if constexpr (Batch == 4) {
                avx_add(sr, si, h + 2 * count, x3, k);
                avx_add(sr, si, h + 3 * count, x4, k);
            }
            _mm256_storeu_ps(sum + k, sr);
            _mm256_storeu_ps(sum + k + 8, si);
        }
    }
#endif
    template <std::size_t Batch>
    void mix(float *sum, const float *h, const float *x, const float *x2, const float *x3,
             const float *x4, std::size_t count) noexcept {
#ifdef CONVOY_HAS_AVX_MIXER
        if (avx_) {
            accumulate_avx<Batch>(sum, h, x, x2, x3, x4, count);
            return;
        }
#endif
        accumulate_packed<Batch>(sum, h, x, x2, x3, x4, count);
    }
    template <std::size_t Batch>
    static void accumulate_packed(float *__restrict sum, const float *h, const float *x,
                                  const float *x2, const float *x3, const float *x4,
                                  std::size_t count) noexcept {
        for (std::size_t group = 0; group < count; group += 16)
            for (std::size_t lane = 0; lane < 8; lane += 4) {
                const auto k = group + lane;
#ifdef CONVOY_FFT_SSE
                auto sr = _mm_loadu_ps(sum + k), si = _mm_loadu_ps(sum + k + 8);
                auto add = [&](const float *a, const float *b) {
                    const auto ar = _mm_loadu_ps(a + k), ai = _mm_loadu_ps(a + k + 8);
                    const auto br = _mm_loadu_ps(b + k), bi = _mm_loadu_ps(b + k + 8);
                    sr = _mm_add_ps(sr, _mm_sub_ps(_mm_mul_ps(ar, br), _mm_mul_ps(ai, bi)));
                    si = _mm_add_ps(si, _mm_add_ps(_mm_mul_ps(ar, bi), _mm_mul_ps(ai, br)));
                };
                add(h, x);
                if constexpr (Batch >= 2)
                    add(h + count, x2);
                if constexpr (Batch == 4) {
                    add(h + 2 * count, x3);
                    add(h + 3 * count, x4);
                }
                _mm_storeu_ps(sum + k, sr);
                _mm_storeu_ps(sum + k + 8, si);
#else
                for (std::size_t j = 0; j < 4; ++j) {
                    float sr = sum[k + j], si = sum[k + 8 + j];
                    auto add = [&](const float *a, const float *b) {
                        sr += a[k + j] * b[k + j] - a[k + 8 + j] * b[k + 8 + j];
                        si += a[k + j] * b[k + 8 + j] + a[k + 8 + j] * b[k + j];
                    };
                    add(h, x);
                    if constexpr (Batch >= 2)
                        add(h + count, x2);
                    if constexpr (Batch == 4) {
                        add(h + 2 * count, x3);
                        add(h + 3 * count, x4);
                    }
                    sum[k + j] = sr;
                    sum[k + 8 + j] = si;
                }
#endif
            }
    }
    void mix_active(std::size_t stride) noexcept {
        std::size_t p = 0;
        for (; p + 3 < active_partitions_; p += 4) {
            const auto x = ((cursor_ + partitions_ - p) % partitions_) * stride;
            const auto y = ((cursor_ + partitions_ - p - 1) % partitions_) * stride;
            const auto z = ((cursor_ + partitions_ - p - 2) % partitions_) * stride;
            const auto w = ((cursor_ + partitions_ - p - 3) % partitions_) * stride;
            mix<4>(sum_.data(), filters_.data() + p * stride, history_.data() + x,
                   history_.data() + y, history_.data() + z, history_.data() + w, stride);
        }
        for (; p + 1 < active_partitions_; p += 2) {
            const auto x = ((cursor_ + partitions_ - p) % partitions_) * stride;
            const auto y = ((cursor_ + partitions_ - p - 1) % partitions_) * stride;
            mix<2>(sum_.data(), filters_.data() + p * stride, history_.data() + x,
                   history_.data() + y, nullptr, nullptr, stride);
        }
        if (p < active_partitions_) {
            const auto x = ((cursor_ + partitions_ - p) % partitions_) * stride;
            mix<1>(sum_.data(), filters_.data() + p * stride, history_.data() + x, nullptr, nullptr,
                   nullptr, stride);
        }
    }
    // Only used while the cutoff moves. Stable settings use the unchanged
    // batched mixer. Gain changes span eight block updates; overlap-add carries
    // both sides of each weighted input block across the boundary.
    void mix_transition(std::size_t stride) noexcept {
        bool moving = false;
        for (std::size_t p = 0; p < active_partitions_; ++p) {
            const float target = p < target_partitions_ ? 1.f : 0.f;
            float &gain = gains_[p];
            gain = gain < target ? std::min(target, gain + .125f) : std::max(target, gain - .125f);
            moving |= gain != target;
            if (gain == 0.f)
                continue;
            const auto x = ((cursor_ + partitions_ - p) % partitions_) * stride;
            const float *h = filters_.data() + p * stride, *v = history_.data() + x;
            for (std::size_t k = 0; k < stride; k += 16)
                for (std::size_t j = 0; j < 8; ++j) {
                    sum_[k + j] += gain * (h[k + j] * v[k + j] - h[k + 8 + j] * v[k + 8 + j]);
                    sum_[k + 8 + j] += gain * (h[k + j] * v[k + 8 + j] + h[k + 8 + j] * v[k + j]);
                }
        }
        transitioning_ = moving;
        if (!moving)
            active_partitions_ = target_partitions_;
    }
    void block() noexcept {
        const auto stride = 2 * bins_;
        fft_.forward_half_packed(input_.data(), history_.data() + cursor_ * stride);
        std::fill(sum_.begin(), sum_.end(), 0.f);
        if (transitioning_)
            mix_transition(stride);
        else
            mix_active(stride);
        fft_.inverse_packed(sum_.data(), work_.data());
        for (std::size_t i = 0; i < b_; ++i) {
            output_[i] = work_[i] + overlap_[i];
            overlap_[i] = work_[i + b_];
        }
        cursor_ = (cursor_ + 1) % partitions_;
    }

  public:
    static std::size_t checked_partition(std::size_t b) {
        if (b < 16 || b > 8192 || (b & (b - 1)))
            throw std::invalid_argument("partition must be a power of two from 16 to 8192");
        return b;
    }
    static std::size_t checked_length(const float *ir, std::size_t length) {
        if (!ir || !length || length > 16777216)
            throw std::invalid_argument("IR must contain 1 to 16777216 samples");
        for (std::size_t i = 0; i < length; ++i)
            if (!std::isfinite(ir[i]))
                throw std::invalid_argument("IR contains non-finite samples");
        return length;
    }
    PartitionedConvolver(const float *ir, std::size_t length, std::size_t partition = 256)
        : b_(checked_partition(partition)), n_(b_ * 2), bins_(b_ + 8), partitions_(0), fft_(n_) {
        checked_length(ir, length);
        partitions_ = (length + b_ - 1) / b_;
        active_partitions_ = target_partitions_ = partitions_;
        gains_.assign(partitions_, 1.f);
        suffix_peak_.assign(partitions_, 0.f);
        for (std::size_t i = length; i-- > 0;) {
            const auto p = i / b_;
            suffix_peak_[p] = std::max(suffix_peak_[p], std::abs(ir[i]));
        }
        for (std::size_t p = partitions_ - 1; p > 0; --p)
            suffix_peak_[p - 1] = std::max(suffix_peak_[p - 1], suffix_peak_[p]);
        filters_.resize(partitions_ * bins_ * 2);
        history_.resize(partitions_ * bins_ * 2);
        sum_.resize(bins_ * 2);
        work_.resize(n_);
        input_.resize(b_);
        output_.resize(b_);
        overlap_.resize(b_);
        for (std::size_t p = 0; p < partitions_; ++p) {
            std::fill(work_.begin(), work_.end(), 0.f);
            for (std::size_t i = 0; i < b_ && p * b_ + i < length; ++i)
                work_[i] = ir[p * b_ + i];
            fft_.forward_half_packed(work_.data(), filters_.data() + p * bins_ * 2);
        }
    }
    // Audio-thread control only, like process/reset; no allocation or IR rebuild.
    // 0 (and invalid values) disables trimming. Negative dB is absolute IR gain,
    // not an output-error or audibility bound. Retain at least one partition.
    void set_tail_cutoff_db(float db) noexcept {
        if (!std::isfinite(db) || db >= 0.f)
            db = 0.f;
        else
            db = std::max(-144.f, db);
        if (db == cutoff_db_)
            return;
        cutoff_db_ = db;
        const float threshold = db == 0.f ? -1.f : std::pow(10.f, db / 20.f);
        const auto end = std::lower_bound(suffix_peak_.begin(), suffix_peak_.end(), threshold,
                                          [](float peak, float floor) { return peak >= floor; });
        const auto target = std::max<std::size_t>(1, end - suffix_peak_.begin());
        if (target == target_partitions_)
            return;
        target_partitions_ = target;
        active_partitions_ = std::max(active_partitions_, target);
        transitioning_ = true;
    }
    // End of the retained partition range; includes final zero padding.
    std::size_t retained_samples() const noexcept { return target_partitions_ * b_; }
    bool using_avx() const noexcept {
#ifdef CONVOY_HAS_AVX_MIXER
        return avx_;
#else
        return false;
#endif
    }
    std::size_t latency() const noexcept { return b_; }
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + fft_.memory_bytes() - sizeof(RealFFT) +
               (work_.capacity() + input_.capacity() + output_.capacity() + overlap_.capacity() +
                filters_.capacity() + history_.capacity() + sum_.capacity() +
                suffix_peak_.capacity() + gains_.capacity()) *
                   sizeof(float);
    }
    void reset() noexcept {
        cursor_ = position_ = 0;
        active_partitions_ = target_partitions_;
        transitioning_ = false;
        for (std::size_t p = 0; p < partitions_; ++p)
            gains_[p] = p < target_partitions_ ? 1.f : 0.f;
        std::fill(history_.begin(), history_.end(), 0.f);
        std::fill(input_.begin(), input_.end(), 0.f);
        std::fill(output_.begin(), output_.end(), 0.f);
        std::fill(overlap_.begin(), overlap_.end(), 0.f);
    }
    // Exact in-place operation is supported. Partial input/output overlap is not.
    void process(const float *input, float *output, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            const float sample = input[i];
            output[i] = output_[position_];
            input_[position_] = sample;
            if (++position_ == b_) {
                block();
                position_ = 0;
            }
        }
    }
};
} // namespace detail
} // namespace convoy
