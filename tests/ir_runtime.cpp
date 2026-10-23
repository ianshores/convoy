// SPDX-License-Identifier: MIT
#include "graph_runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static thread_local bool audio = false;
static thread_local unsigned allocations = 0;
__attribute__((noinline)) void *operator new(std::size_t n) {
    if (audio)
        ++allocations;
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
__attribute__((noinline)) void operator delete(void *p) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete(void *p, std::size_t) noexcept { std::free(p); }
extern "C" {
void *__real_malloc(std::size_t);
void *__real_calloc(std::size_t, std::size_t);
void *__real_realloc(void *, std::size_t);
void *__wrap_malloc(std::size_t n) {
    if (audio)
        ++allocations;
    return __real_malloc(n);
}
void *__wrap_calloc(std::size_t n, std::size_t s) {
    if (audio)
        ++allocations;
    return __real_calloc(n, s);
}
void *__wrap_realloc(void *p, std::size_t n) {
    if (audio)
        ++allocations;
    return __real_realloc(p, n);
}
}
static void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "fixture directory required");
        std::string dir = argv[1];
        for (int sign : {1, -1}) {
            SF_INFO info{};
            info.channels = 1;
            info.samplerate = 48000;
            info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
            auto *f = sf_open((dir + (sign == 1 ? "/positive.wav" : "/negative.wav")).c_str(),
                              SFM_WRITE, &info);
            float value = float(sign);
            require(f, "fixture");
            sf_writef_float(f, &value, 1);
            sf_close(f);
        }
        heavyverb::GraphRuntime engine(48000);
        float in0[1023], in1[1023], out0[1023], out1[1023];
        std::fill_n(in0, 1023, .1f);
        std::fill_n(in1, 1023, .1f);
        const float *in[] = {in0, in1};
        float *out[] = {out0, out1};
        auto process = [&](int n) {
            audio = true;
            engine.process(in, out, n, 0);
            audio = false;
            require(allocations == 0, "audio thread allocated");
            for (int i = 0; i < n; ++i)
                require(std::isfinite(out0[i]) && out0[i] == out1[i], "finite stereo");
        };
        auto wait = [&] {
            for (int i = 0; i < 5000; ++i) {
                process(127);
                if (engine.status() == 2) {
                    for (int j = 0; j < 100; ++j)
                        process(127);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("timeout");
        };
        engine.request(dir + "/positive.wav");
        wait();
        require(std::abs(out0[126] - .05f) < 1e-6, "positive convolution");
        engine.request(dir + "/negative.wav");
        float previous = .05f;
        double largest = 0;
        for (int i = 0; i < 500; ++i) {
            process(127);
            for (int j = 0; j < 127; ++j) {
                largest = std::max(largest, double(std::abs(out0[j] - previous)));
                previous = out0[j];
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::printf("crossfade final=%.9g, max step=%.9g\n", previous, largest);
        require(std::abs(previous + .05f) < 1e-6 && largest < .0001, "crossfade continuity");
        // Requests/retirement with arbitrary and in-place callbacks.
        for (int step = 0; step < 100; ++step) {
            engine.request(dir + (step % 2 ? "/positive.wav" : "/negative.wav"));
            for (int n : {1, 7, 64, 127, 256, 1023})
                process(n);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (int n : {1, 7, 64, 127, 256, 1023}) {
            const float *alias[] = {in0, in1};
            float *dest[] = {in0, in1};
            audio = true;
            engine.process(alias, dest, n, 0);
            audio = false;
            require(allocations == 0, "in-place callback allocated");
        }
        std::printf("PASS: worker handoff, variable/in-place callbacks, zero audio C/C++ "
                    "allocations; crossfade maximum sample step %.9g\n",
                    largest);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
