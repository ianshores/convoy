// SPDX-License-Identifier: MIT
// Diagnostic only: never change the host's floating-point mode in the engine.
#include "convoy.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
struct RestoreMode {
    unsigned value = _mm_getcsr();
    ~RestoreMode() { _mm_setcsr(value); }
};
int main() {
    RestoreMode restore;
    std::puts("case,ftz_daz,cpu_percent,checksum");
    for (int kind = 0; kind < 3; ++kind)
        for (int flush = 0; flush < 2; ++flush) {
            _mm_setcsr((restore.value & ~0x8040u) | (flush ? 0x8040u : 0));
            std::mt19937 gen(171);
            std::uniform_real_distribution<float> noise(-.1f, .1f);
            std::vector<float> ir(192000), input(256), output(256);
            for (std::size_t k = 0; k < ir.size(); ++k)
                ir[k] = noise(gen) * std::exp(-(kind == 2 ? 90.f : 6.f) * k / ir.size());
            for (auto &x : input)
                x = noise(gen) * (kind == 1 ? 1.e-37f : 1.f);
            convoy::Convolver c(ir.data(), ir.size());
            for (int k = 0; k < 192000; k += 256)
                c.process(input.data(), output.data(), 256);
            const int blocks = 5 * 48000 / 256;
            double sum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int k = 0; k < blocks; ++k) {
                c.process(input.data(), output.data(), 256);
                sum += std::abs(output[k % 256]);
            }
            const double sec =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("%s,%d,%.9f,%.9g\n",
                        kind == 0   ? "normal"
                        : kind == 1 ? "near-subnormal-input"
                                    : "extreme-780dB-ir-tail",
                        flush, 100 * sec / (blocks * 256. / 48000), sum);
        }
}
#else
int main() { std::puts("SKIP: floating-point-mode diagnostic requires SSE2"); }
#endif
