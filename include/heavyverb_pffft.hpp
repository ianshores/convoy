// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#ifndef PFFFT_STATIC_DEFINE
#define PFFFT_STATIC_DEFINE
#endif
#include "pffft.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace heavyverb {
namespace detail {
struct AlignedFree { void operator()(float* p) const noexcept { pffft_aligned_free(p); } };
struct SetupFree { void operator()(PFFFT_Setup* p) const noexcept { pffft_destroy_setup(p); } };
class Buffer {
    std::size_t count_;
    std::unique_ptr<float, AlignedFree> data_;
public:
    explicit Buffer(std::size_t count) : count_(count),
        data_(static_cast<float*>(pffft_aligned_malloc(count * sizeof(float)))) {
        if (!data_) throw std::bad_alloc();
        clear();
    }
    float* data() noexcept { return data_.get(); }
    const float* data() const noexcept { return data_.get(); }
    float& operator[](std::size_t i) noexcept { return data()[i]; }
    std::size_t bytes() const noexcept { return count_ * sizeof(float); }
    void clear() noexcept { std::fill_n(data(), count_, 0.f); }
};
// Uniform overlap-add building block, used for both early and late responses.
class PartitionedConvolver {
    std::size_t b_, n_, partitions_, cursor_ = 0, position_ = 0;
    std::unique_ptr<PFFFT_Setup, detail::SetupFree> setup_;
    detail::Buffer filters_, history_, input_, output_, overlap_, scratch_, sum_;
public:
    static std::size_t checked_partition(std::size_t b) {
        if (b < 16 || b > 8192 || (b & (b - 1)))
            throw std::invalid_argument("partition must be a power of two from 16 to 8192");
        return b;
    }
    static std::size_t checked_length(const float* ir, std::size_t length) {
        if (!ir || !length || length > 16777216)
            throw std::invalid_argument("IR must contain 1 to 16777216 samples");
        for (std::size_t i = 0; i < length; ++i)
            if (!std::isfinite(ir[i])) throw std::invalid_argument("IR contains non-finite samples");
        return length;
    }
private:
    void block() noexcept {
        pffft_transform(setup_.get(), input_.data(), history_.data() + cursor_ * n_,
                        scratch_.data(), PFFFT_FORWARD);
        sum_.clear();
        for (std::size_t p = 0; p < partitions_; ++p) {
            const auto x = (cursor_ + partitions_ - p) % partitions_;
            pffft_zconvolve_accumulate(setup_.get(), filters_.data() + p * n_,
                history_.data() + x * n_, sum_.data(), 1.f / static_cast<float>(n_));
        }
        pffft_transform(setup_.get(), sum_.data(), output_.data(), scratch_.data(), PFFFT_BACKWARD);
        for (std::size_t i = 0; i < b_; ++i) {
            output_[i] += overlap_[i];
            overlap_[i] = output_[i + b_];
        }
        cursor_ = (cursor_ + 1) % partitions_;
    }
public:
    PartitionedConvolver(const float* ir, std::size_t length, std::size_t partition = 256)
        : b_(checked_partition(partition)), n_(b_ * 2),
          partitions_((checked_length(ir, length) + b_ - 1) / b_),
          setup_(pffft_new_setup(static_cast<int>(n_), PFFFT_REAL)),
          filters_(partitions_ * n_), history_(partitions_ * n_),
          input_(n_), output_(n_), overlap_(b_), scratch_(n_), sum_(n_) {
        if (!setup_) throw std::bad_alloc();
        for (std::size_t p = 0; p < partitions_; ++p) {
            input_.clear();
            const auto count = std::min(b_, length - p * b_);
            std::copy_n(ir + p * b_, count, input_.data());
            pffft_transform(setup_.get(), input_.data(), filters_.data() + p * n_,
                            scratch_.data(), PFFFT_FORWARD);
        }
        input_.clear();
    }
    std::size_t latency() const noexcept { return b_; }
    // Owned buffer bytes; PFFFT's opaque setup allocation is not included.
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + filters_.bytes() + history_.bytes() + input_.bytes()
            + output_.bytes() + overlap_.bytes() + scratch_.bytes() + sum_.bytes();
    }
    void reset() noexcept {
        cursor_ = position_ = 0;
        history_.clear(); input_.clear(); output_.clear(); overlap_.clear();
    }
    void process(const float* input, float* output, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            const float sample = input[i];
            output[i] = output_[position_];
            input_[position_] = sample;
            if (++position_ == b_) { block(); position_ = 0; }
        }
    }
};
} // namespace detail

} // namespace heavyverb
