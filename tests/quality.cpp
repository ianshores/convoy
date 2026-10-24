// SPDX-License-Identifier: MIT
// Full-tail null test against an independent double-precision linear convolution.
#include "convoy.hpp"
#include <complex>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>
using C = std::complex<double>;
static void reference_fft(std::vector<C> &x, bool inverse) {
    const auto n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n / 2;
        for (; j & bit; bit /= 2)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len *= 2) {
        std::vector<C> roots(len / 2);
        for (std::size_t k = 0; k < len / 2; ++k) {
            const double angle = (inverse ? 1. : -1.) * 6.2831853071795864769 * k / len;
            roots[k] = C(std::cos(angle), std::sin(angle));
        }
        for (std::size_t base = 0; base < n; base += len)
            for (std::size_t k = 0; k < len / 2; ++k) {
                const C a = x[base + k], b = x[base + k + len / 2] * roots[k];
                x[base + k] = a + b;
                x[base + k + len / 2] = a - b;
            }
    }
    if (inverse)
        for (auto &v : x)
            v /= double(n);
}
int main() {
    try {
        std::puts("sample_rate,ir_seconds,signal,max_error,rms_error,snr_db,reference_peak");
        for (int rate : {44100, 48000, 96000}) {
            std::mt19937 rng(1021);
            std::uniform_real_distribution<float> noise(-1.f, 1.f);
            std::vector<float> ir(4 * rate);
            double energy = 0;
            for (std::size_t i = 0; i < ir.size(); ++i) {
                ir[i] = float(noise(rng) * std::exp(-6. * i / ir.size()));
                energy += double(ir[i]) * ir[i];
            }
            for (auto &v : ir)
                v = float(v / std::sqrt(energy));
            constexpr std::size_t input_count = 8192, latency = 256;
            const auto total = input_count + ir.size() - 1 + latency;
            std::size_t size = 1;
            while (size < total)
                size *= 2;
            std::vector<C> filter(size);
            for (std::size_t i = 0; i < ir.size(); ++i)
                filter[i] = double(ir[i]);
            reference_fft(filter, false);
            for (int kind = 0; kind < 4; ++kind) {
                const char *names[] = {"impulse", "wideband", "multitone_bursts", "quiet_wideband"};
                std::vector<float> input(total, 0), output(total), scratch(1023);
                for (std::size_t i = 0; i < input_count; ++i) {
                    if (kind == 0)
                        input[i] = i == 0 ? .5f : 0.f;
                    if (kind == 1 || kind == 3)
                        input[i] = noise(rng) * (kind == 3 ? .000025f : .25f);
                    if (kind == 2 && (i / 1024) % 2 == 0) {
                        for (double hz : {73., 997., 7800.})
                            input[i] +=
                                float(.08 * std::sin(6.2831853071795864769 * hz * i / rate));
                    }
                }
                std::vector<C> ref(size);
                for (std::size_t i = 0; i < input_count; ++i)
                    ref[i] = double(input[i]);
                reference_fft(ref, false);
                for (std::size_t i = 0; i < size; ++i)
                    ref[i] *= filter[i];
                reference_fft(ref, true);
                convoy::Convolver convolver(ir.data(), ir.size(), latency);
                const std::size_t blocks[] = {7, 64, 127, 256, 1023};
                for (std::size_t i = 0, step = 0; i < total; ++step) {
                    const auto count = std::min(blocks[step % 5], total - i);
                    if (step % 2) {
                        std::copy_n(input.data() + i, count, scratch.data());
                        convolver.process(scratch.data(), scratch.data(), count);
                        std::copy_n(scratch.data(), count, output.data() + i);
                    } else
                        convolver.process(input.data() + i, output.data() + i, count);
                    i += count;
                }
                double worst = 0, squared = 0, power = 0, peak = 0;
                for (std::size_t i = 0; i < total; ++i) {
                    const double expected = i < latency ? 0. : ref[i - latency].real();
                    const double error = output[i] - expected;
                    worst = std::max(worst, std::abs(error));
                    squared += error * error;
                    power += expected * expected;
                    peak = std::max(peak, std::abs(expected));
                }
                const double snr = 10 * std::log10(power / squared),
                             rms = std::sqrt(squared / total);
                std::printf("%d,4,%s,%.12g,%.12g,%.6f,%.9g\n", rate, names[kind], worst, rms, snr,
                            peak);
                if (!std::isfinite(snr) || snr < 120 || worst > 1e-5)
                    throw std::runtime_error("Full-tail accuracy requirement failed");
            }
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
