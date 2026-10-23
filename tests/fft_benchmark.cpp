// SPDX-License-Identifier: MIT
#include "heavyverb_portable.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
namespace heavyverb {
namespace detail {
struct ConvolverProbe {
    static void mac(float *sr, float *si, const float *ar, const float *ai, const float *br,
                    const float *bi, std::size_t n) {
        PartitionedConvolver::accumulate(sr, si, ar, ai, br, bi, n);
    }
};
struct FFTProbe {
    static void shuffle_pair(RealFFT &p) {
#if defined(HEAVYVERB_FFT_STEP) && HEAVYVERB_FFT_STEP == 5
        p.reorder(false);
        p.reorder(true);
#elif defined(HEAVYVERB_FFT_STEP) && HEAVYVERB_FFT_STEP < 5
        for (int repeat = 0; repeat < 2; ++repeat)
            for (std::size_t k = 0; k < p.m_; ++k)
                if (k < p.reverse_[k]) {
#if HEAVYVERB_FFT_STEP >= 2
                    std::swap(p.r_[k], p.r_[p.reverse_[k]]);
                    std::swap(p.i_[k], p.i_[p.reverse_[k]]);
#else
                    std::swap(p.work_[k], p.work_[p.reverse_[k]]);
#endif
                }
#else
        (void)p;
#endif
    }
};
} // namespace detail
} // namespace heavyverb
using Clock = std::chrono::steady_clock;
volatile double consume = 0;
template <class F> double measure(F f, int iterations = 3000) {
    for (int i = 0; i < 100; ++i)
        consume += f(i);
    const auto start = Clock::now();
    double checksum = 0;
    for (int i = 0; i < iterations; ++i)
        checksum += f(i);
    double us =
        std::chrono::duration<double, std::micro>(Clock::now() - start).count() / iterations;
    consume += checksum;
    return us;
}
int main() {
    std::puts("fft_size,forward_us,inverse_us,copy_zero_us,overlap_us,mac_partitions,mac_us,"
              "shuffle_pair_us,plan_bytes");
    for (std::size_t n : {512u, 4096u}) {
        heavyverb::detail::RealFFT plan(n);
        std::vector<float> time(n), real(n / 2 + 1), imag(n / 2 + 1), out(n), overlap(n / 2);
        std::mt19937 g(42);
        std::uniform_real_distribution<float> d(-.1f, .1f);
        for (auto &x : time)
            x = d(g);
        const double f = measure([&](int i) {
            plan.forward(time.data(), real.data(), imag.data());
            return real[i % (n / 2 + 1)];
        });
        const double b = measure([&](int i) {
            plan.inverse(real.data(), imag.data(), out.data());
            return out[i % n];
        });
        const double c = measure([&](int i) {
            std::copy(time.begin(), time.begin() + n / 2, out.begin());
            std::fill(out.begin() + n / 2, out.end(), 0.f);
            return out[i % n];
        });
        const double o = measure([&](int j) {
            for (std::size_t i = 0; i < n / 2; ++i) {
                out[i] = time[i] + overlap[i];
                overlap[i] = time[i + n / 2];
            }
            return out[j % (n / 2)];
        });
        const std::size_t bins = n / 2 + 1, parts = n == 512 ? 8 : 93;
        std::vector<float> hr(bins * parts), hi(hr.size()), xr(hr.size()), xi(hr.size()), sr(bins),
            si(bins);
        for (auto *v : {&hr, &hi, &xr, &xi})
            for (auto &x : *v)
                x = d(g);
        const double mac = measure(
            [&](int j) {
                std::fill(sr.begin(), sr.end(), 0.f);
                std::fill(si.begin(), si.end(), 0.f);
                for (std::size_t p = 0; p < parts; ++p)
                    heavyverb::detail::ConvolverProbe::mac(
                        sr.data(), si.data(), hr.data() + p * bins, hi.data() + p * bins,
                        xr.data() + p * bins, xi.data() + p * bins, bins);
                return sr[j % bins];
            },
            1000);
        double shuffle = 0;
#if defined(HEAVYVERB_FFT_STEP) && HEAVYVERB_FFT_STEP < 6
        shuffle = measure([&](int i) {
            heavyverb::detail::FFTProbe::shuffle_pair(plan);
            return time[i % n];
        });
#endif
        std::printf("%zu,%.6f,%.6f,%.6f,%.6f,%zu,%.6f,%.6f,%zu\n", n, f, b, c, o, parts, mac,
                    shuffle, plan.memory_bytes());
    }
    if (!std::isfinite(consume))
        return 1;
}
