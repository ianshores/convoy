// SPDX-License-Identifier: MIT
#pragma once
#include "heavyverb_ir_override.hpp"
#include <memory>
#include <samplerate.h>
#include <sndfile.h>
#include <stdexcept>
#include <string>
namespace heavyverb {
inline IRData load_wav(const std::string &path, double rate) {
    SF_INFO info{};
    std::unique_ptr<SNDFILE, decltype(&sf_close)> file(sf_open(path.c_str(), SFM_READ, &info),
                                                       sf_close);
    if (!file)
        throw std::runtime_error("Cannot open WAV file");
    const int type = info.format & SF_FORMAT_TYPEMASK;
    if (type != SF_FORMAT_WAV && type != SF_FORMAT_WAVEX && type != SF_FORMAT_RF64)
        throw std::runtime_error("Choose a WAV file");
    if (info.channels != 1 && info.channels != 2)
        throw std::runtime_error("Use mono or stereo WAV");
    if (info.samplerate < 8000 || info.samplerate > 384000 || !std::isfinite(rate) || rate < 8000 ||
        rate > 384000)
        throw std::runtime_error("Unsupported sample rate");
    if (info.frames < 1 || info.frames > 30LL * info.samplerate)
        throw std::runtime_error("IR must contain audio and be at most 30 seconds");
    std::vector<float> samples(std::size_t(info.frames) * info.channels);
    if (sf_readf_float(file.get(), samples.data(), info.frames) != info.frames)
        throw std::runtime_error("Incomplete WAV data");
    for (float v : samples)
        if (!std::isfinite(v))
            throw std::runtime_error("WAV contains invalid samples");
    if (rate != info.samplerate) {
        const double ratio = rate / info.samplerate;
        std::vector<float> converted((std::size_t(std::ceil(info.frames * ratio)) + 256) *
                                     info.channels);
        SRC_DATA data{};
        data.data_in = samples.data();
        data.data_out = converted.data();
        data.input_frames = info.frames;
        data.output_frames = converted.size() / info.channels;
        data.src_ratio = ratio;
        data.end_of_input = 1;
        if (src_simple(&data, SRC_SINC_BEST_QUALITY, info.channels) || data.output_frames_gen < 1)
            throw std::runtime_error("Could not resample WAV");
        converted.resize(std::size_t(data.output_frames_gen) * info.channels);
        // Preserve the IR's integral/DC gain when the tap density changes.
        for (auto &v : converted)
            v = float(v / ratio);
        samples.swap(converted);
    }
    IRData result;
    result.channels.resize(info.channels);
    for (int ch = 0; ch < info.channels; ++ch) {
        auto &dst = result.channels[ch];
        dst.resize(samples.size() / info.channels);
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] = samples[i * info.channels + ch];
            if (!std::isfinite(dst[i]))
                throw std::runtime_error("Invalid resampled audio");
        }
    }
    return result;
}
} // namespace heavyverb
