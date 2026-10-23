// SPDX-License-Identifier: MIT
#include "heavyverb.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include <vector>
static std::atomic<std::size_t> allocations{0};
// Keep the allocation hooks opaque so GCC does not mistake this intentional
// malloc/free implementation of global new/delete for mismatched allocation.
#if defined(__GNUC__)
#define HV_TEST_NOINLINE __attribute__((noinline))
#else
#define HV_TEST_NOINLINE
#endif
HV_TEST_NOINLINE void* operator new(std::size_t n) {
    ++allocations;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
HV_TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
HV_TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static void require(bool v, const char* msg) { if (!v) throw std::runtime_error(msg); }
int main() {
    try {
        std::mt19937 rng(171); std::uniform_real_distribution<float> random(-0.2f, 0.2f);
        double worst = 0;
        for (auto b : {16u, 64u, 256u, 1024u})
        for (auto length : {1u, 17u, 255u, 257u, 1597u})
        for (auto callback : {1u, 7u, 127u, 256u, 1023u}) {
            std::vector<float> ir(length), input(1701 + length + b, 0), expected(input.size(), 0);
            for (auto& v : ir) v = random(rng);
            for (std::size_t i = 0; i < 1701; ++i) input[i] = random(rng);
            for (std::size_t i = 0; i < 1701; ++i)
                for (std::size_t j = 0; j < length; ++j) expected[i + j + b] += input[i] * ir[j];
            heavyverb::Convolver c(ir.data(), ir.size(), b);
            auto output = input;
            const auto before = allocations.load();
            for (std::size_t i = 0; i < output.size(); i += callback)
                c.process(output.data() + i, output.data() + i, std::min<std::size_t>(callback, output.size() - i));
            c.reset();
            require(allocations.load() == before, "audio callback/reset allocated");
            for (std::size_t i = 0; i < output.size(); ++i) {
                const auto error = std::abs(double(output[i]) - expected[i]);
                worst = std::max(worst, error);
                require(error < 0.0001, "output differs from direct reference");
            }
            std::fill(input.begin(), input.end(), 0.f);
            c.process(input.data(), input.data(), input.size());
            for (auto v : input) require(v == 0, "reset leaked tail");
        }
        // Exercise the optimized half-spectrum path across a full four-second IR.
        // A short dense source keeps the independent direct reference inexpensive.
        {
            constexpr std::size_t length = 4 * 48000, source_length = 73, b = 256;
            std::vector<float> ir(length), input(source_length + length + b, 0.f),
                               output(input.size()), expected(input.size(), 0.f);
            for (auto& v : ir) v = random(rng) * 0.05f;
            for (std::size_t i = 0; i < source_length; ++i) input[i] = random(rng);
            for (std::size_t i = 0; i < source_length; ++i)
                for (std::size_t j = 0; j < length; ++j) expected[i + j + b] += input[i] * ir[j];
            heavyverb::Convolver c(ir.data(), ir.size(), b);
            const auto before = allocations.load();
            for (std::size_t i = 0; i < input.size(); i += 127)
                c.process(input.data() + i, output.data() + i, std::min<std::size_t>(127, input.size() - i));
            require(allocations.load() == before, "long IR audio callback allocated");
            for (std::size_t i = 0; i < output.size(); ++i) {
                const auto error = std::abs(double(output[i]) - expected[i]);
                worst = std::max(worst, error);
                require(error < 0.0001, "four-second IR differs from direct reference");
            }
        }
        // Exact routing around early/tail split, activation threshold, and ring
        // wrap. Sparse signed taps keep this independent reference inexpensive.
        for (auto b : {16u, 64u, 256u, 1024u}) {
            const std::size_t split = std::min<std::size_t>(8192, b * 8);
            for (auto length : {split - 1, split, split + 1, split * 2, split * 2 + 1, split * 5 + 13})
            for (auto callback : {7u, 127u, 511u}) {
                std::vector<float> ir(length, 0.f);
                const std::size_t indices[] = {0, split - 1, split, split + 1, length - 1};
                for (std::size_t j = 0; j < 5; ++j)
                    if (indices[j] < length) ir[indices[j]] += (j & 1 ? -0.2f : 0.3f);
                const std::size_t source_length = split * 8 + 73;
                std::vector<float> input(source_length + length + b, 0.f), expected(input.size(), 0.f);
                for (std::size_t i = 0; i < source_length; ++i) input[i] = random(rng);
                for (std::size_t j = 0; j < length; ++j)
                    if (ir[j] != 0)
                        for (std::size_t i = 0; i < source_length; ++i) expected[i + j + b] += input[i] * ir[j];
                heavyverb::Convolver c(ir.data(), length, b);
                require(c.latency() == b, "partitioning changed latency");
                auto output = input;
                const auto before = allocations.load();
                for (std::size_t i = 0; i < input.size(); i += callback)
                    c.process(output.data() + i, output.data() + i, std::min<std::size_t>(callback, output.size() - i));
                c.reset();
                require(allocations.load() == before, "split processing/reset allocated");
                for (std::size_t i = 0; i < output.size(); ++i) {
                    const auto error = std::abs(double(output[i]) - expected[i]);
                    worst = std::max(worst, error);
                    require(error < 0.0001, "partition split or history wrap is misaligned");
                }
                std::fill(output.begin(), output.end(), 0.f);
                c.process(output.data(), output.data(), output.size());
                for (auto v : output) require(v == 0, "reset left a tail-stage echo");
            }
        }
        float impulse = 1;
        for (auto b : {0u, 15u, 33u, 16384u}) {
            bool rejected = false;
            try { heavyverb::Convolver c(&impulse, 1, b); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "invalid partition accepted");
        }
        bool rejected = false;
        try { heavyverb::Convolver c(nullptr, 0); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "empty IR accepted");
        std::cout << "PASS: 173 reference cases including split boundaries and a four-second IR, arbitrary callbacks, in-place, reset, no audio allocations; max error " << worst << '\n';
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
