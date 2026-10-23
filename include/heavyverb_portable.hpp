// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ian Shores
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <memory>
#include <vector>

namespace heavyverb {
// Uniform partitioned overlap-add convolution. Configure on a non-audio thread;
// process/reset never allocate. One instance per channel; B samples of latency.
class Convolver {
    using Complex = std::complex<float>;
    std::size_t b_, n_, bins_, partitions_, cursor_ = 0, position_ = 0;
    std::vector<std::size_t> reverse_;
    std::vector<Complex> roots_, work_, sum_;
    std::vector<float> filter_r_, filter_i_, history_r_, history_i_, sum_r_, sum_i_;
    std::vector<float> input_, output_, overlap_;

    // Separate real/imaginary arrays let the compiler vectorize across FFT bins.
    static void accumulate(float* __restrict sr, float* __restrict si,
                           const float* __restrict ar, const float* __restrict ai,
                           const float* __restrict br, const float* __restrict bi,
                           std::size_t count) noexcept {
        for (std::size_t k = 0; k < count; ++k) {
            sr[k] += ar[k] * br[k] - ai[k] * bi[k];
            si[k] += ar[k] * bi[k] + ai[k] * br[k];
        }
    }
    void fft(std::vector<Complex>& a, bool inverse) noexcept {
        for (std::size_t i = 0; i < n_; ++i)
            if (i < reverse_[i]) std::swap(a[i], a[reverse_[i]]);
        for (std::size_t len = 2; len <= n_; len *= 2) {
            const auto half = len / 2, step = n_ / len;
            for (std::size_t base = 0; base < n_; base += len)
                for (std::size_t j = 0; j < half; ++j) {
                    const auto w = inverse ? std::conj(roots_[j * step]) : roots_[j * step];
                    const auto u = a[base + j], z = a[base + j + half];
                    const Complex v(z.real() * w.real() - z.imag() * w.imag(),
                                    z.real() * w.imag() + z.imag() * w.real());
                    a[base + j] = u + v;
                    a[base + j + half] = u - v;
                }
        }
        if (inverse) for (auto& v : a) v /= static_cast<float>(n_);
    }
    void block() noexcept {
        std::fill(work_.begin(), work_.end(), Complex{});
        for (std::size_t i = 0; i < b_; ++i) work_[i] = input_[i];
        fft(work_, false);
        for (std::size_t k = 0; k < bins_; ++k) {
            history_r_[cursor_ * bins_ + k] = work_[k].real();
            history_i_[cursor_ * bins_ + k] = work_[k].imag();
        }
        std::fill(sum_r_.begin(), sum_r_.end(), 0.f);
        std::fill(sum_i_.begin(), sum_i_.end(), 0.f);
        for (std::size_t p = 0; p < partitions_; ++p) {
            const auto h = p * bins_, x = ((cursor_ + partitions_ - p) % partitions_) * bins_;
            accumulate(sum_r_.data(), sum_i_.data(), filter_r_.data() + h, filter_i_.data() + h,
                       history_r_.data() + x, history_i_.data() + x, bins_);
        }
        for (std::size_t k = 0; k < bins_; ++k) sum_[k] = Complex(sum_r_[k], sum_i_[k]);
        // Real input has conjugate-symmetric spectra: accumulate only one half.
        for (std::size_t k = 1; k < b_; ++k) sum_[n_ - k] = std::conj(sum_[k]);
        fft(sum_, true);
        for (std::size_t i = 0; i < b_; ++i) {
            output_[i] = sum_[i].real() + overlap_[i];
            overlap_[i] = sum_[i + b_].real();
        }
        cursor_ = (cursor_ + 1) % partitions_;
    }
public:
    Convolver(const float* ir, std::size_t length, std::size_t partition = 256)
        : b_(partition), n_(partition * 2), bins_(partition + 1), partitions_(0) {
        if (partition < 16 || partition > 8192 || (partition & (partition - 1)))
            throw std::invalid_argument("partition must be a power of two from 16 to 8192");
        if (!ir || !length || length > 16777216)
            throw std::invalid_argument("IR must contain 1 to 16777216 samples");
        for (std::size_t i = 0; i < length; ++i)
            if (!std::isfinite(ir[i])) throw std::invalid_argument("IR contains non-finite samples");
        partitions_ = (length + b_ - 1) / b_;
        reverse_.resize(n_); roots_.resize(n_ / 2);
        filter_r_.resize(partitions_ * bins_); filter_i_.resize(partitions_ * bins_);
        history_r_.resize(partitions_ * bins_); history_i_.resize(partitions_ * bins_);
        sum_r_.resize(bins_); sum_i_.resize(bins_);
        work_.resize(n_); sum_.resize(n_);
        input_.resize(b_); output_.resize(b_); overlap_.resize(b_);
        for (std::size_t i = 0; i < n_; ++i) {
            auto v = i; std::size_t rev = 0;
            for (auto bit = n_; bit > 1; bit >>= 1) { rev = (rev << 1) | (v & 1); v >>= 1; }
            reverse_[i] = rev;
        }
        for (std::size_t i = 0; i < n_ / 2; ++i) {
            const auto angle = -6.2831853071795864769 * static_cast<double>(i) / n_;
            roots_[i] = Complex(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        }
        for (std::size_t p = 0; p < partitions_; ++p) {
            std::fill(work_.begin(), work_.end(), Complex{});
            for (std::size_t i = 0; i < b_ && p * b_ + i < length; ++i) work_[i] = ir[p * b_ + i];
            fft(work_, false);
            for (std::size_t k = 0; k < bins_; ++k) {
                filter_r_[p * bins_ + k] = work_[k].real();
                filter_i_[p * bins_ + k] = work_[k].imag();
            }
        }
    }
    std::size_t latency() const noexcept { return b_; }
    std::size_t memory_bytes() const noexcept {
        return sizeof(*this) + reverse_.capacity() * sizeof(std::size_t)
            + (roots_.capacity() + work_.capacity() + sum_.capacity()) * sizeof(Complex)
            + (input_.capacity() + output_.capacity() + overlap_.capacity()
               + filter_r_.capacity() + filter_i_.capacity() + history_r_.capacity()
               + history_i_.capacity() + sum_r_.capacity() + sum_i_.capacity()) * sizeof(float);
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
    void process(const float* input, float* output, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            const float sample = input[i];
            output[i] = output_[position_];
            input_[position_] = sample;
            if (++position_ == b_) { block(); position_ = 0; }
        }
    }
};
} // namespace heavyverb
