// SPDX-License-Identifier: MIT
#include <convoy.hpp>
#include <array>
#include <iostream>
int main() {
    const float ir[] = {1.f, .5f, .25f};
    convoy::Convolver filter(ir, 3, 256);
    std::array<float, 512> input{}, output{};
    input[0] = 1.f;
    filter.process(input.data(), output.data(), input.size());
    for (std::size_t i = filter.latency(); i < filter.latency() + 3; ++i)
        std::cout << output[i] << '\n';
}
