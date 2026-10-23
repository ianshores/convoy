// SPDX-License-Identifier: MIT
#include "DistrhoPlugin.hpp"
#include "graph_runtime.hpp"
#include <cstring>
START_NAMESPACE_DISTRHO
class HeavyVerbPlugin : public Plugin {
    std::unique_ptr<heavyverb::GraphRuntime> runtime_;
    std::atomic<float> cutoff_{0.f};

  public:
    HeavyVerbPlugin() : Plugin(3, 0, 1), runtime_(new heavyverb::GraphRuntime(getSampleRate())) {}

  protected:
    const char *getLabel() const override { return "HeavyVerbDemo"; }
    const char *getMaker() const override { return "Ian Shores"; }
    const char *getHomePage() const override { return "https://github.com/ianshores/heavyverb"; }
    // Original engine/host code is MIT; preserve generated runtime's own terms.
    const char *getLicense() const override {
        return "MIT (heavyverb); see bundled component notices";
    }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }
    int64_t getUniqueId() const override { return int64_t(0xA90CB1BD); }
    void initParameter(uint32_t index, Parameter &p) override {
        if (index == 0) {
            p.name = "IR Tail Cutoff dB";
            p.symbol = "ir_tail_cutoff_db";
            p.unit = "dB";
            p.hints = kParameterIsAutomatable;
            p.ranges.min = -144;
            p.ranges.max = 0;
            p.ranges.def = 0;
        } else if (index == 1) {
            p.name = "IR Load Status";
            p.symbol = "ir_load_status";
            p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.min = 0;
            p.ranges.max = 3;
            p.ranges.def = 0;
        } else {
            p.name = "IR Duration";
            p.symbol = "ir_duration";
            p.unit = "s";
            p.hints = kParameterIsOutput;
            p.ranges.min = 0;
            p.ranges.max = 30;
            p.ranges.def = .5;
        }
    }
    float getParameterValue(uint32_t index) const override {
        return index == 0   ? cutoff_.load()
               : index == 1 ? float(runtime_->status())
                            : runtime_->seconds();
    }
    void setParameterValue(uint32_t index, float value) override {
        if (index == 0)
            cutoff_.store(std::isfinite(value) ? std::max(-144.f, std::min(0.f, value)) : 0.f);
    }
    void initState(uint32_t, State &state) override {
        state.key = "ir_file";
        state.label = "Impulse response WAV";
        state.defaultValue = "";
        state.hints = kStateIsFilenamePath;
    }
    String getState(const char *key) const override {
        return std::strcmp(key, "ir_file") == 0 ? String(runtime_->saved_path().c_str()) : String();
    }
    void setState(const char *key, const char *value) override {
        if (std::strcmp(key, "ir_file") == 0)
            runtime_->request(value);
    }
    void sampleRateChanged(double rate) override {
        const auto path = runtime_->saved_path();
        runtime_.reset(new heavyverb::GraphRuntime(rate));
        if (!path.empty())
            runtime_->request(path);
    }
    void run(const float **inputs, float **outputs, uint32_t frames) override {
        runtime_->process(inputs, outputs, frames, cutoff_.load());
    }
};
Plugin *createPlugin() { return new HeavyVerbPlugin; }
END_NAMESPACE_DISTRHO
