// SPDX-License-Identifier: MIT
#pragma once
#include "Heavy_HeavyVerbDemo.h"
#include "Heavy_HeavyVerbDemo.hpp"
#include "wav_loader.hpp"
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
namespace heavyverb {
// The worker owns construction and destruction. The audio thread owns the
// active/fading graphs and transfers obsolete graphs through a one-slot queue.
class GraphRuntime {
    struct Prepared {
        std::unique_ptr<HeavyContextInterface> graph;
        uint64_t generation;
        float seconds;
        bool builtin;
        float cutoff = std::numeric_limits<float>::quiet_NaN();
        Prepared(const IRData *ir, double rate, uint64_t gen)
            : generation(gen), seconds(ir ? ir->channels[0].size() / rate : 24000. / rate),
              builtin(!ir) {
            ScopedIR scope(ir);
            graph.reset(hv_HeavyVerbDemo_new_with_options(rate, 10, 2, 2));
            if (!graph)
                throw std::runtime_error("Cannot construct audio graph");
            // Heavy lazily allocates its first control-message node. Warm it on
            // this construction thread, before the graph reaches audio.
            float silence[256]{}, left[256], right[256];
            float *inputs[] = {silence, silence};
            float *outputs[] = {left, right};
            parameter(0.f);
            graph->process(inputs, outputs, 256);
        }
        void parameter(float db) {
            if (db != cutoff) {
                if (graph->sendFloatToReceiver(
                        Heavy_HeavyVerbDemo::Parameter::In::IR_TAIL_CUTOFF_DB, db))
                    cutoff = db;
            }
        }
    };
    const double rate_;
    const std::size_t fade_samples_;
    Prepared *active_;
    Prepared *fading_ = nullptr;
    std::size_t fade_position_ = 0, warm_remaining_ = 0;
    std::atomic<Prepared *> pending_{nullptr}, retired_{nullptr};
    // High bits identify the request; low two bits hold its status. One CAS
    // prevents an older audio handoff from marking a newer request ready.
    std::atomic<uint64_t> request_state_{0};
    std::atomic<float> duration_{.5f};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false, requested_ = false;
    std::string request_path_, saved_path_;
    std::thread worker_;
    void work() {
        for (;;) {
            delete retired_.exchange(nullptr);
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(20),
                           [&] { return stop_ || requested_; });
            if (stop_)
                break;
            if (!requested_)
                continue;
            std::string path = request_path_;
            const auto gen = request_state_.load() >> 2;
            requested_ = false;
            lock.unlock();
            try {
                IRData ir;
                if (!path.empty())
                    ir = load_wav(path, rate_);
                auto candidate =
                    std::make_unique<Prepared>(path.empty() ? nullptr : &ir, rate_, gen);
                lock.lock();
                if ((request_state_.load() >> 2) == gen) {
                    saved_path_ = path;
                    delete pending_.exchange(candidate.release());
                }
            } catch (const std::exception &) {
                if (!lock.owns_lock())
                    lock.lock();
                uint64_t expected = (gen << 2) | 1;
                request_state_.compare_exchange_strong(expected, (gen << 2) | 3);
            }
        }
    }

  public:
    explicit GraphRuntime(double rate)
        : rate_(rate), fade_samples_(std::max<std::size_t>(1, std::size_t(rate * .05))),
          active_(new Prepared(nullptr, rate, 0)) {
        duration_.store(active_->seconds);
        try {
            worker_ = std::thread([this] { work(); });
        } catch (...) {
            delete active_;
            throw;
        }
        static_assert(std::atomic<Prepared *>::is_always_lock_free &&
                          std::atomic<float>::is_always_lock_free &&
                          std::atomic<uint64_t>::is_always_lock_free,
                      "Audio handoff requires lock-free atomics");
    }
    ~GraphRuntime() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_one();
        worker_.join();
        delete pending_.exchange(nullptr);
        delete retired_.exchange(nullptr);
        delete fading_;
        delete active_;
    }
    // Host/UI state path, not an audio parameter; call outside process().
    void request(const std::string &path) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_path_ = path;
            requested_ = true;
            request_state_.store((((request_state_.load() >> 2) + 1) << 2) | 1);
        }
        wake_.notify_one();
    }
    std::string saved_path() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return saved_path_;
    }
    int status() const noexcept { return int(request_state_.load() & 3); }
    float seconds() const noexcept { return duration_.load(); }
    void process(const float **inputs, float **outputs, std::size_t count, float cutoff) noexcept {
        if (!fading_ && !retired_.load()) {
            if (auto *next = pending_.exchange(nullptr)) {
                if (next->generation != (request_state_.load() >> 2))
                    retired_.store(next);
                else {
                    fading_ = active_;
                    active_ = next;
                    fade_position_ = 0;
                    warm_remaining_ = 256;
                    duration_.store(active_->seconds);
                    // Status describes the graph accepted by the audio thread.
                    uint64_t expected = (active_->generation << 2) | 1;
                    request_state_.compare_exchange_strong(
                        expected, (active_->generation << 2) | (active_->builtin ? 0 : 2));
                }
            }
        }
        active_->parameter(cutoff);
        if (fading_)
            fading_->parameter(cutoff);
        float source[2][256], fresh[2][256], old[2][256];
        float *in[] = {source[0], source[1]}, *a[] = {fresh[0], fresh[1]}, *b[] = {old[0], old[1]};
        for (std::size_t pos = 0; pos < count; pos += 256) {
            const auto n = std::min<std::size_t>(256, count - pos);
            for (int ch = 0; ch < 2; ++ch)
                std::copy_n(inputs[ch] + pos, n, source[ch]);
            active_->graph->process(in, a, int(n));
            if (fading_)
                fading_->graph->process(in, b, int(n));
            for (std::size_t i = 0; i < n; ++i) {
                const float mix = !fading_ ? 1.f
                                  : warm_remaining_
                                      ? (--warm_remaining_, 0.f)
                                      : std::min(1.f, float(++fade_position_) / fade_samples_);
                for (int ch = 0; ch < 2; ++ch)
                    outputs[ch][pos + i] =
                        fading_ ? old[ch][i] * (1.f - mix) + fresh[ch][i] * mix : fresh[ch][i];
            }
            if (fading_ && fade_position_ >= fade_samples_) {
                retired_.store(fading_);
                fading_ = nullptr;
            }
        }
    }
};
} // namespace heavyverb
