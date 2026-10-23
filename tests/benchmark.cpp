// SPDX-License-Identifier: MIT
#ifndef HEAVYVERB_HEADER
#define HEAVYVERB_HEADER "heavyverb.hpp"
#endif
#include HEAVYVERB_HEADER
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <memory>
#include <vector>
using Clock = std::chrono::steady_clock;
int main() {
    std::cout << "ir_seconds,channels,callback,sample_rate,partition,cpu_percent,p99_us,max_us,deadline_us,checksum\n";
    for (int seconds : {1, 4}) for (int channels : {1, 2}) for (int callback : {64, 256}) {
        std::mt19937 rng(99); std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
        std::vector<float> ir(seconds * 48000), input(callback), output(callback);
        for (std::size_t i = 0; i < ir.size(); ++i) ir[i] = noise(rng) * std::exp(-6.f * i / ir.size());
        for (auto& v : input) v = noise(rng);
        std::vector<std::unique_ptr<heavyverb::Convolver>> engines;
        for (int ch = 0; ch < channels; ++ch)
            engines.emplace_back(new heavyverb::Convolver(ir.data(), ir.size(), 256));
        // Fill every history partition before timing; there is no silence shortcut.
        for (int i = 0; i < seconds * 48000; i += callback)
            for (auto& engine : engines) engine->process(input.data(), output.data(), callback);
        const char* requested=std::getenv("HEAVYVERB_BENCH_SECONDS");
        const int duration=requested?std::max(1,std::atoi(requested)):3;
        const int repeats = duration * 48000 / callback;
        std::vector<double> times; times.reserve(repeats);
        double total_us = 0, checksum = 0;
        for (int i = 0; i < repeats; ++i) {
            const auto start = Clock::now();
            for (auto& engine : engines) engine->process(input.data(), output.data(), callback);
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            total_us += us; times.push_back(us);
            checksum += std::abs(output[i % callback]);
        }
        std::sort(times.begin(), times.end());
        const double audio_us = double(repeats) * callback / 48000 * 1e6;
        std::cout << std::setprecision(9) << seconds << ',' << channels << ',' << callback << ",48000,256,"
                  << 100 * total_us / audio_us << ',' << times[times.size() * 99 / 100] << ','
                  << times.back() << ',' << callback / 48000.0 * 1e6 << ',' << checksum << '\n';
    }
}
