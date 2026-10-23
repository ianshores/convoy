// SPDX-License-Identifier: MIT
#include "HvLightPipe.h"
#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
#include <stdexcept>
#include <vector>
static void check(bool ok) {
    if (!ok)
        throw std::runtime_error("LightPipe alignment/FIFO failure");
}
int main() {
    HvLightPipe pipe;
    hLp_init(&pipe, 1024);
    std::deque<std::vector<char>> queue;
    std::mt19937 random(125);
    auto read = [&] {
        check(hLp_hasData(&pipe) != 0 && !queue.empty());
        hv_uint32_t size = 0;
        auto *data = hLp_getReadBuffer(&pipe, &size);
        check(reinterpret_cast<uintptr_t>(data) % 8 == 0 && size == queue.front().size());
        check(std::equal(queue.front().begin(), queue.front().end(), data));
        hLp_consume(&pipe);
        queue.pop_front();
    };
    for (int i = 0; i < 100000; ++i) {
        const unsigned size = 1 + random() % 80;
        if (auto *data = hLp_getWriteBuffer(&pipe, size)) {
            check(reinterpret_cast<uintptr_t>(data) % 8 == 0);
            std::vector<char> value(size);
            for (auto &c : value)
                c = char(random());
            std::copy(value.begin(), value.end(), data);
            hLp_produce(&pipe, size);
            queue.push_back(value);
        } else {
            check(!queue.empty());
            read();
        }
        if (!queue.empty() && random() % 3 == 0)
            read();
    }
    while (!queue.empty())
        read();
    check(hLp_hasData(&pipe) == 0);
    hLp_reset(&pipe);
    check(hLp_hasData(&pipe) == 0);
    hLp_free(&pipe);
    std::puts(
        "PASS: aligned LightPipe payloads, variable sizes, full/wrap/FIFO/reset (100,000 writes)");
}
