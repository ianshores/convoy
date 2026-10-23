// SPDX-License-Identifier: MIT
// Synthetic instance/format stress: not an in-game performance claim.
#include "heavyverb.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <time.h>
static double thread_us() {
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t))
        throw std::runtime_error("thread clock failed");
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
int main() {
    struct Case {
        int rate, callback, partition, instances;
    };
    const Case cases[] = {{48000, 256, 256, 1}, {48000, 64, 256, 2}, {48000, 256, 256, 8},
                          {44100, 127, 256, 2}, {96000, 64, 256, 2}, {96000, 256, 1024, 2},
                          {48000, 7, 64, 2},    {96000, 64, 64, 8}};
    std::puts("sample_rate,callback,partition,instances,ir_seconds,cpu_percent,p99_us,max_wall_us,"
              "max_thread_us,deadline_us,deadline_misses,callbacks,owned_bytes,checksum");
    for (const auto &c : cases) {
        std::mt19937 g(171);
        std::uniform_real_distribution<float> random(-.1f, .1f);
        std::vector<float> ir(c.rate * 4), input(c.callback), output(c.callback);
        for (std::size_t k = 0; k < ir.size(); ++k)
            ir[k] = random(g) * std::exp(-6.f * k / ir.size());
        for (auto &x : input)
            x = random(g);
        std::vector<std::unique_ptr<heavyverb::Convolver>> engines;
        for (int k = 0; k < c.instances; ++k)
            engines.emplace_back(new heavyverb::Convolver(ir.data(), ir.size(), c.partition));
        for (int k = 0; k < c.rate * 4; k += c.callback)
            for (auto &e : engines)
                e->process(input.data(), output.data(), c.callback);
        const int blocks = 3 * c.rate / c.callback;
        std::vector<double> times;
        times.reserve(blocks);
        const double deadline = 1e6 * c.callback / c.rate;
        double total = 0, max_cpu = 0, sum = 0;
        int misses = 0;
        for (int k = 0; k < blocks; ++k) {
            const auto before = thread_us();
            const auto start = std::chrono::steady_clock::now();
            for (auto &e : engines)
                e->process(input.data(), output.data(), c.callback);
            const auto wall =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                    .count();
            max_cpu = std::max(max_cpu, thread_us() - before);
            times.push_back(wall);
            total += wall;
            misses += wall > deadline;
            sum += std::abs(output[k % c.callback]);
        }
        std::sort(times.begin(), times.end());
        std::size_t bytes = 0;
        for (auto &e : engines)
            bytes += e->memory_bytes();
        std::printf("%d,%d,%d,%d,4,%.9f,%.6f,%.6f,%.6f,%.6f,%d,%d,%zu,%.9g\n", c.rate, c.callback,
                    c.partition, c.instances, 100 * total / (blocks * deadline),
                    times[times.size() * 99 / 100], times.back(), max_cpu, deadline, misses, blocks,
                    bytes, sum);
    }
}
