// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include "heavyverb_fft.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace heavyverb {
namespace detail {
// Uniform partitioned overlap-add convolution. Configure on a non-audio thread;
// process/reset never allocate. One instance per channel; B samples of latency.
class PartitionedConvolver {
    friend struct ConvolverProbe;
    // Spectrum has B+1 useful bins; zero padding keeps strides a SIMD multiple.
    std::size_t b_, n_, bins_, partitions_, cursor_ = 0, position_ = 0;
    RealFFT fft_;
    std::vector<float> work_, filter_r_, filter_i_, history_r_, history_i_, sum_r_, sum_i_;
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
    // Keep each sum in a register for two successive partitions. Addition
    // order matches the single-partition loop (no reassociation/fast-math).
    static void accumulate_pair(float *__restrict sr, float *__restrict si,
                                const float *__restrict ar, const float *__restrict ai,
                                const float *__restrict br, const float *__restrict bi,
                                const float *__restrict cr, const float *__restrict ci,
                                const float *__restrict dr, const float *__restrict di,
                                std::size_t count) noexcept {
        for (std::size_t k = 0; k < count; ++k) {
            const float r = sr[k] + (ar[k] * br[k] - ai[k] * bi[k]);
            const float i = si[k] + (ar[k] * bi[k] + ai[k] * br[k]);
            sr[k] = r + (cr[k] * dr[k] - ci[k] * di[k]);
            si[k] = i + (cr[k] * di[k] + ci[k] * dr[k]);
        }
    }
    void block() noexcept {
        std::copy(input_.begin(), input_.end(), work_.begin());
        std::fill(work_.begin() + b_, work_.end(), 0.f);
        fft_.forward(work_.data(), history_r_.data() + cursor_ * bins_,
                     history_i_.data() + cursor_ * bins_);
        std::fill(sum_r_.begin(), sum_r_.end(), 0.f);
        std::fill(sum_i_.begin(), sum_i_.end(), 0.f);
        std::size_t p = 0;
        for (; p + 1 < partitions_; p += 2) {
            const auto h = p * bins_, next = h + bins_;
            const auto x = ((cursor_ + partitions_ - p) % partitions_) * bins_;
            const auto y = ((cursor_ + partitions_ - p - 1) % partitions_) * bins_;
            accumulate_pair(sum_r_.data(), sum_i_.data(), filter_r_.data() + h,
                            filter_i_.data() + h, history_r_.data() + x, history_i_.data() + x,
                            filter_r_.data() + next, filter_i_.data() + next, history_r_.data() + y,
                            history_i_.data() + y, bins_);
        }
        if (p < partitions_) {
            const auto h = p * bins_, x = ((cursor_ + partitions_ - p) % partitions_) * bins_;
            accumulate(sum_r_.data(), sum_i_.data(), filter_r_.data() + h, filter_i_.data() + h,
                       history_r_.data() + x, history_i_.data() + x, bins_);
        }
        fft_.inverse(sum_r_.data(), sum_i_.data(), work_.data());
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
        : b_(checked_partition(partition)), n_(b_ * 2), bins_(b_ + 4), partitions_(0), fft_(n_) {
        checked_length(ir, length);
        partitions_ = (length + b_ - 1) / b_;
        filter_r_.resize(partitions_ * bins_);
        filter_i_.resize(partitions_ * bins_);
        history_r_.resize(partitions_ * bins_);
        history_i_.resize(partitions_ * bins_);
        sum_r_.resize(bins_);
        sum_i_.resize(bins_);
        work_.resize(n_);
        input_.resize(b_);
        output_.resize(b_);
        overlap_.resize(b_);
        for (std::size_t p = 0; p < partitions_; ++p) {
            std::fill(work_.begin(), work_.end(), 0.f);
            for (std::size_t i = 0; i < b_ && p * b_ + i < length; ++i)
                work_[i] = ir[p * b_ + i];
            fft_.forward(work_.data(), filter_r_.data() + p * bins_, filter_i_.data() + p * bins_);
        }
    }
    std::size_t latency() const noexcept { return b_; }
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + fft_.memory_bytes() - sizeof(RealFFT) +
               (work_.capacity() + input_.capacity() + output_.capacity() + overlap_.capacity() +
                filter_r_.capacity() + filter_i_.capacity() + history_r_.capacity() +
                history_i_.capacity() + sum_r_.capacity() + sum_i_.capacity()) *
                   sizeof(float);
    }
    void reset() noexcept {
        cursor_ = position_ = 0;
        std::fill(history_r_.begin(), history_r_.end(), 0.f);
        std::fill(history_i_.begin(), history_i_.end(), 0.f);
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
} // namespace heavyverb
