// SPDX-License-Identifier: MIT
#pragma once
#include "heavyverb.hpp"
namespace heavyverb {
// Used only while a host constructs a graph on a non-audio thread. Ordinary
// exports keep the Pd array. Channel 0/1 select independent mono convolution.
struct IRData {
    std::vector<std::vector<float>> channels;
};
inline thread_local const IRData *construction_ir = nullptr;
class ScopedIR {
    const IRData *previous_;

  public:
    explicit ScopedIR(const IRData *ir) : previous_(construction_ir) { construction_ir = ir; }
    ScopedIR(const ScopedIR &) = delete;
    ScopedIR &operator=(const ScopedIR &) = delete;
    ~ScopedIR() { construction_ir = previous_; }
};
inline Convolver *make_convolver(const float *saved, std::size_t length, std::size_t partition,
                                 std::size_t channel) {
    if (construction_ir && !construction_ir->channels.empty()) {
        const auto &data =
            construction_ir->channels[std::min(channel, construction_ir->channels.size() - 1)];
        return new Convolver(data.data(), data.size(), partition);
    }
    return new Convolver(saved, length, partition);
}
} // namespace heavyverb
