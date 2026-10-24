// SPDX-License-Identifier: MIT
// Linux diagnostic: distinguish processing CPU time from elapsed callback time.
#include "convoy.hpp"
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
#include <time.h>
static double cpu_us() {
    timespec t;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0)
        throw std::runtime_error("thread CPU clock unavailable");
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}
int main() {
    std::puts("[");
    bool first = true;
    for (int channels : {1, 2}) for (int callback : {64, 256}) {
        std::mt19937 rng(99);
        std::uniform_real_distribution<float> noise(-.1f, .1f);
        std::vector<float> ir(192000), input(callback), output(callback);
        for (auto& v : ir) v = noise(rng);
        for (auto& v : input) v = noise(rng);
        std::vector<std::unique_ptr<convoy::Convolver>> engines;
        for (int ch = 0; ch < channels; ++ch)
            engines.emplace_back(new convoy::Convolver(ir.data(), ir.size()));
        for (int i = 0; i < 192000; i += callback)
            for (auto& engine : engines) engine->process(input.data(), output.data(), callback);
        double max_wall = 0, cpu_at_max_wall = 0, max_cpu = 0;
        int misses = 0;
        const double deadline = callback / 48000.0 * 1e6;
        for (int i = 0; i < 24000; ++i) {
            const auto start = std::chrono::steady_clock::now();
            const double before = cpu_us();
            for (auto& engine : engines) engine->process(input.data(), output.data(), callback);
            const double cpu = cpu_us() - before;
            const double wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            if (wall > max_wall) { max_wall = wall; cpu_at_max_wall = cpu; }
            max_cpu = std::max(cpu, max_cpu);
            if (wall > deadline) ++misses;
        }
        if (!first) std::puts(",");
        first = false;
        std::printf("{\"callbacks\":24000,\"channels\":%d,\"callback_samples\":%d,"
            "\"deadline_us\":%.3f,\"max_wall_us\":%.3f,\"thread_cpu_us_at_max_wall\":%.3f,"
            "\"max_thread_cpu_us\":%.3f,\"wall_deadline_misses\":%d}",
            channels, callback, deadline, max_wall, cpu_at_max_wall, max_cpu, misses);
    }
    std::puts("\n]");
}
