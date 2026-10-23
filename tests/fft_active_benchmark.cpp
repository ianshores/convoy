// SPDX-License-Identifier: MIT
// Time the transform path used by each convolver, with half-zero input.
#include "heavyverb.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
volatile double consume = 0;
template <class F> double measure(F f) {
    for (int i = 0; i < 100; ++i)
        consume += f(i);
    auto start = std::chrono::steady_clock::now();
    double sum = 0;
    for (int i = 0; i < 3000; ++i)
        sum += f(i);
    double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                    .count() /
                3000;
    consume += sum;
    return us;
}
int main() {
    std::puts("fft_size,forward_us,inverse_us,custom_runtime_avx");
    for (std::size_t n : {512u, 4096u}) {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> d(-.1f, .1f);
#ifdef HEAVYVERB_PORTABLE_FFT
        heavyverb::detail::RealFFT plan(n);
        std::vector<float> time(n, 0.f), out(n), spectrum(n + 16), real(n / 2 + 1), imag(n / 2 + 1);
        for (std::size_t k = 0; k < n / 2; ++k)
            time[k] = d(rng);
#ifdef HEAVYVERB_PROFILE_PACKED
        const double f = measure([&](int i) {
            plan.forward_half_packed(time.data(), spectrum.data());
            return spectrum[i % (n + 16)];
        });
        const double b = measure([&](int i) {
            plan.inverse_packed(spectrum.data(), out.data());
            return out[i % n];
        });
#else
        const double f = measure([&](int i) {
            plan.forward(time.data(), real.data(), imag.data());
            return real[i % (n / 2 + 1)];
        });
        const double b = measure([&](int i) {
            plan.inverse(real.data(), imag.data(), out.data());
            return out[i % n];
        });
#endif
#else
        using namespace heavyverb::detail;
        std::unique_ptr<PFFFT_Setup, SetupFree> plan(pffft_new_setup(n, PFFFT_REAL));
        Buffer time(n), out(n), spectrum(n), scratch(n);
        for (std::size_t k = 0; k < n / 2; ++k)
            time[k] = d(rng);
        const double f = measure([&](int i) {
            pffft_transform(plan.get(), time.data(), spectrum.data(), scratch.data(),
                            PFFFT_FORWARD);
            return spectrum[i % n];
        });
        const double b = measure([&](int i) {
            pffft_transform(plan.get(), spectrum.data(), out.data(), scratch.data(),
                            PFFFT_BACKWARD);
            return out[i % n];
        });
#endif
        int avx = 0;
#ifdef HEAVYVERB_HAS_AVX_MIXER
        heavyverb::detail::PartitionedConvolver convolver(time.data(), n / 2, n / 2);
        avx = convolver.using_avx();
#endif
        std::printf("%zu,%.6f,%.6f,%d\n", n, f, b, avx);
    }
    return !std::isfinite(consume);
}
