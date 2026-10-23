// SPDX-License-Identifier: MIT
#include "wav_loader.hpp"
#include <cstdio>
#include <limits>
static void require(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static void write(const std::string &p, int format, int channels, int rate,
                  const std::vector<float> &v) {
    SF_INFO info{};
    info.format = format;
    info.channels = channels;
    info.samplerate = rate;
    auto *f = sf_open(p.c_str(), SFM_WRITE, &info);
    require(f, "fixture open");
    require(sf_writef_float(f, v.data(), v.size() / channels) == sf_count_t(v.size() / channels),
            "fixture write");
    sf_close(f);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "fixture directory required");
        const std::string p = std::string(argv[1]) + "/loader-fixture.wav";
        std::vector<float> samples(2048);
        samples[128] = .5f;
        samples[129] = -.25f;
        for (int format : {SF_FORMAT_PCM_16, SF_FORMAT_PCM_24, SF_FORMAT_PCM_32, SF_FORMAT_FLOAT,
                           SF_FORMAT_DOUBLE}) {
            write(p, SF_FORMAT_WAV | format, 2, 48000, samples);
            auto ir = heavyverb::load_wav(p, 48000);
            require(ir.channels.size() == 2 && ir.channels[0].size() == 1024, "shape");
            require(ir.channels[0][64] == .5f && ir.channels[1][64] == -.25f,
                    "exact channel/PCM decoding");
        }
        std::vector<float> impulse(4096);
        impulse[1024] = 1;
        for (int rate : {44100, 96000}) {
            write(p, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, rate, impulse);
            auto ir = heavyverb::load_wav(p, 48000);
            double sum = 0;
            for (float x : ir.channels[0])
                sum += x;
            require(std::abs(sum - 1) < .001, "resampling must preserve impulse integral");
            require(std::abs(double(ir.channels[0].size()) / 48000 - 4096. / rate) < .0001,
                    "resampling duration");
        }
        auto reject = [&] {
            try {
                heavyverb::load_wav(p, 48000);
            } catch (const std::exception &) {
                return;
            }
            throw std::runtime_error("invalid file accepted");
        };
        write(p, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 3, 48000, std::vector<float>(3));
        reject();
        write(p, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, 48000,
              {std::numeric_limits<float>::quiet_NaN()});
        reject();
        write(p, SF_FORMAT_WAV | SF_FORMAT_FLOAT, 1, 8000, std::vector<float>(240001));
        reject();
        write(p, SF_FORMAT_AIFF | SF_FORMAT_PCM_16, 1, 48000, {1});
        reject();
        std::puts("PASS: PCM16/24/32, float32/64, stereo mapping, sinc rate conversion/integral, "
                  "invalid format/channel/nonfinite/length rejection");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
