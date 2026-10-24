// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include "convoy_portable.hpp"
#include <vector>
namespace convoy {
// Two-stage partitioning keeps the requested wet latency for early reflections,
// while later IR samples use larger blocks. Configure outside the audio thread.
class Convolver {
    std::size_t b_, delay_position_ = 0;
    std::unique_ptr<detail::PartitionedConvolver> head_, tail_;
    std::vector<float> tail_delay_;
    static std::size_t tail_size(std::size_t b, std::size_t length) noexcept {
#ifdef CONVOY_UNIFORM_PARTITIONS
        (void)b; (void)length;
        return 0;
#else
        const auto larger = std::min<std::size_t>(8192, b * 8);
        return larger > b && length > larger * 2 ? larger : 0;
#endif
    }
public:
    Convolver(const float* ir, std::size_t length, std::size_t partition = 256)
        : b_(detail::PartitionedConvolver::checked_partition(partition)),
          tail_delay_(tail_size(b_, length) ? b_ : 0, 0.f) {
        detail::PartitionedConvolver::checked_length(ir, length);
        const auto split = tail_size(b_, length);
        head_.reset(new detail::PartitionedConvolver(ir, split ? split : length, b_));
        if (split) tail_.reset(new detail::PartitionedConvolver(ir + split, length - split, split));
    }
    // Call on the audio thread, serialized with process/reset. The long-IR
    // early stage stays intact. Only trailing whole partitions are omitted.
    void set_tail_cutoff_db(float db) noexcept {
        (tail_ ? tail_.get() : head_.get())->set_tail_cutoff_db(db);
    }
    std::size_t retained_samples() const noexcept {
        return tail_ ? std::min<std::size_t>(8192, b_ * 8) + tail_->retained_samples()
                     : head_->retained_samples();
    }
    std::size_t latency() const noexcept { return b_; }
    // Owned state and buffer bytes.
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + head_->memory_bytes() + tail_delay_.capacity() * sizeof(float)
            + (tail_ ? tail_->memory_bytes() : 0);
    }
    void reset() noexcept {
        head_->reset();
        if (tail_) tail_->reset();
        std::fill(tail_delay_.begin(), tail_delay_.end(), 0.f); delay_position_ = 0;
    }
    // Exact in-place operation is supported; partial overlap is not.
    void process(const float* input, float* output, std::size_t count) noexcept {
        if (!tail_) { head_->process(input, output, count); return; }
        for (std::size_t i = 0; i < count; ++i) {
            const float sample = input[i];
            float early, late;
            head_->process(&sample, &early, 1);
            tail_->process(&sample, &late, 1);
            // The tail's own block latency accounts for its IR offset (split).
            // Delay it by b_ more samples to align with the early path's latency.
            output[i] = early + tail_delay_[delay_position_];
            tail_delay_[delay_position_] = late;
            if (++delay_position_ == b_) delay_position_ = 0;
        }
    }
};
} // namespace convoy
