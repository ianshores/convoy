// SPDX-License-Identifier: MIT
// Matched sizes, seed, iterations, and partition counts to fft_benchmark.cpp.
#include "heavyverb_pffft.hpp"
#include <chrono>
#include <cstdio>
#include <random>
using Clock = std::chrono::steady_clock;
volatile double consume = 0;
template <class F> double measure(F f, int count = 3000) {
    for (int i = 0; i < 100; ++i)
        consume += f(i);
    auto start = Clock::now();
    double sum = 0;
    for (int i = 0; i < count; ++i)
        sum += f(i);
    double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / count;
    consume += sum;
    return us;
}
int main() {
    using namespace heavyverb::detail;
    std::puts("fft_size,forward_us,inverse_us,mac_partitions,mac_us");
    for (int n : {512, 4096}) {
        std::unique_ptr<PFFFT_Setup, SetupFree> plan(pffft_new_setup(n, PFFFT_REAL));
        Buffer time(n), freq(n), out(n), scratch(n);
        std::mt19937 g(42);
        std::uniform_real_distribution<float> d(-.1f, .1f);
        for (int k = 0; k < n; ++k)
            time[k] = d(g);
        double f = measure([&](int i) {
            pffft_transform(plan.get(), time.data(), freq.data(), scratch.data(), PFFFT_FORWARD);
            return freq[i % n];
        });
        // PFFFT inverse is unnormalized; convolver normalizes within MAC.
        double b = measure([&](int i) {
            pffft_transform(plan.get(), freq.data(), out.data(), scratch.data(), PFFFT_BACKWARD);
            return out[i % n];
        });
        int parts = n == 512 ? 8 : 93;
        Buffer h(n * parts), x(n * parts), sum(n);
        for (int k = 0; k < n * parts; ++k) {
            h[k] = d(g);
            x[k] = d(g);
        }
        double mac = measure(
            [&](int i) {
                sum.clear();
                for (int p = 0; p < parts; ++p)
                    pffft_zconvolve_accumulate(plan.get(), h.data() + p * n, x.data() + p * n,
                                               sum.data(), 1.f / n);
                return sum[i % n];
            },
            1000);
        std::printf("%d,%.6f,%.6f,%d,%.6f\n", n, f, b, parts, mac);
    }
    return !std::isfinite(consume);
}
