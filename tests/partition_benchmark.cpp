// SPDX-License-Identifier: MIT
// Complete uniform building-block cost, including the actual accumulation path.
#include "heavyverb.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
volatile double consume = 0;
int main() {
    std::puts("fft_size,partitions,complete_block_us");
    for (std::size_t n : {512u, 4096u}) {
        const std::size_t b = n / 2, parts = n == 512 ? 8 : 93;
        std::vector<float> ir(b * parts), input(b), output(b);
        std::mt19937 g(42);
        std::uniform_real_distribution<float> d(-.1f, .1f);
        for (auto &x : ir)
            x = d(g);
        for (auto &x : input)
            x = d(g);
        heavyverb::detail::PartitionedConvolver p(ir.data(), ir.size(), b);
        for (std::size_t i = 0; i < parts + 100; ++i)
            p.process(input.data(), output.data(), b);
        const int iterations = 3000;
        auto start = std::chrono::steady_clock::now();
        double checksum = 0;
        for (int i = 0; i < iterations; ++i) {
            p.process(input.data(), output.data(), b);
            checksum += output[i % b];
        }
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                .count() /
            iterations;
        consume += checksum;
        std::printf("%zu,%zu,%.6f\n", n, parts, us);
    }
    return !std::isfinite(consume);
}
