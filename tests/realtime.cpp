// SPDX-License-Identifier: MIT
// GNU linker wrappers check allocations in our direct C calls. tests/core.cpp separately intercepts C++ operator new.
#include "convoy.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <new>
static bool armed = false;
static std::size_t calls = 0;
// Keep the allocation hooks opaque so GCC does not mistake this intentional
// malloc/free implementation of global new/delete for mismatched allocation.
#if defined(__GNUC__)
#define HV_TEST_NOINLINE __attribute__((noinline))
#else
#define HV_TEST_NOINLINE
#endif
HV_TEST_NOINLINE void* operator new(std::size_t n) {
    if (armed) ++calls;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
HV_TEST_NOINLINE void operator delete(void* p) noexcept { std::free(p); }
HV_TEST_NOINLINE void operator delete(void* p, std::size_t) noexcept { std::free(p); }
extern "C" {
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
void* __wrap_malloc(std::size_t n) { if (armed) ++calls; return __real_malloc(n); }
void* __wrap_calloc(std::size_t a, std::size_t b) { if (armed) ++calls; return __real_calloc(a, b); }
void* __wrap_realloc(void* p, std::size_t n) { if (armed) ++calls; return __real_realloc(p, n); }
}
int main() {
    std::vector<float> ir(4 * 48000, 0.001f), input(127, 0.1f), output(127);
    convoy::Convolver engine(ir.data(), ir.size());
    // Check that the linker interception is active before trusting it.
    armed = true;
    void* (*volatile allocator)(std::size_t) = std::malloc;
    void* allocation = allocator(128);
    armed = false;
    std::free(allocation);
    if (calls != 1) { std::fputs("allocation interception failed\n", stderr); return 1; }
    calls = 0; armed = true;
    for (int i = 0; i < 5 * 48000; i += 127) {
        engine.set_tail_cutoff_db((i / 127) % 17 == 0 ? 0.f : -1.f);
        engine.process(input.data(), output.data(), 127);
    }
    engine.reset();
    armed = false;
    if (calls) { std::fprintf(stderr, "audio processing made %zu C allocation calls\n", calls); return 1; }
    std::puts("PASS: C malloc/calloc/realloc interception found zero audio processing/reset allocations");
}
