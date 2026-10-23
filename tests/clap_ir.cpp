// SPDX-License-Identifier: MIT
#include <algorithm>
#include <array>
#include <chrono>
#include <clap/entry.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/timer-support.h>
#include <clap/host.h>
#include <clap/plugin-factory.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sndfile.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
static void check(bool ok, const char *msg) {
    if (!ok)
        throw std::runtime_error(msg);
}
static bool reg(const clap_host_t *, uint32_t, clap_id *id) {
    *id = 1;
    return true;
}
static bool unreg(const clap_host_t *, clap_id) { return true; }
static const clap_host_timer_support_t timers{reg, unreg};
static void resizeHints(const clap_host_t *) {}
static bool resize(const clap_host_t *, uint32_t, uint32_t) { return true; }
static bool show(const clap_host_t *) { return true; }
static void closed(const clap_host_t *, bool) {}
static const clap_host_gui_t hostGui{resizeHints, resize, show, show, closed};
static const void *extension(const clap_host_t *, const char *id) {
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &timers;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0)
        return &hostGui;
    return nullptr;
}
static void request(const clap_host_t *) {}
static uint32_t count(const clap_input_events_t *) { return 0; }
static const clap_event_header_t *get(const clap_input_events_t *, uint32_t) { return nullptr; }
static bool push(const clap_output_events_t *, const clap_event_header_t *) { return true; }
struct Stream {
    std::vector<char> data;
    std::size_t pos = 0;
};
static int64_t read(const clap_istream_t *s, void *out, uint64_t n) {
    auto &v = *static_cast<Stream *>(s->ctx);
    n = std::min<std::size_t>(n, v.data.size() - v.pos);
    std::memcpy(out, v.data.data() + v.pos, n);
    v.pos += n;
    return n;
}
static int64_t write(const clap_ostream_t *s, const void *in, uint64_t n) {
    auto &v = *static_cast<Stream *>(s->ctx);
    auto *p = static_cast<const char *>(in);
    v.data.insert(v.data.end(), p, p + n);
    return n;
}
static Stream file_state(const std::string &path) {
    Stream s;
    for (const auto &str :
         std::vector<std::string>{"__dpf_state_begin__", "ir_file", path, "__dpf_state_end__"}) {
        s.data.insert(s.data.end(), str.begin(), str.end());
        s.data.push_back(0);
    }
    s.data.push_back(char(0xfe));
    s.data.push_back(0);
    return s;
}
static void wav(const std::string &path, int channels, int format,
                const std::vector<float> &samples, int rate = 48000) {
    SF_INFO info{};
    info.channels = channels;
    info.samplerate = rate;
    info.format = SF_FORMAT_WAV | format;
    auto *f = sf_open(path.c_str(), SFM_WRITE, &info);
    check(f, "write fixture");
    check(sf_writef_float(f, samples.data(), samples.size() / channels) ==
              sf_count_t(samples.size() / channels),
          "fixture samples");
    sf_close(f);
}
int main(int argc, char **argv) {
    try {
        check(argc >= 3, "CLAP path and fixture directory required");
        const std::string dir = argv[2];
        std::vector<float> stereo(1024 * 2);
        stereo[0] = .5;
        stereo[37 * 2 + 1] = -.25;
        wav(dir + "/stereo.wav", 2, SF_FORMAT_PCM_24, stereo);
        std::vector<float> mono(1024);
        mono[0] = 1;
        mono[100] = .25;
        wav(dir + "/mono.wav", 1, SF_FORMAT_FLOAT, mono);
        wav(dir + "/resample.wav", 1, SF_FORMAT_PCM_16, mono, 44100);
        std::ofstream(dir + "/bad.wav") << "invalid file";
        void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        check(lib, dlerror());
        const auto *entry = static_cast<const clap_plugin_entry_t *>(dlsym(lib, "clap_entry"));
        check(entry && entry->init(argv[1]), "entry");
        const auto *factory =
            static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        check(factory, "factory");
        const auto *desc = factory->get_plugin_descriptor(factory, 0);
        check(desc && std::strcmp(desc->vendor, "Ian Shores") == 0, "maker metadata");
        clap_host_t host{CLAP_VERSION,
                         nullptr,
                         "heavyverb IR test",
                         "heavyverb",
                         "https://github.com/ianshores/heavyverb",
                         "1",
                         extension,
                         request,
                         request,
                         request};
        const clap_plugin_t *plugin = nullptr;
        const clap_plugin_params_t *params = nullptr;
        const clap_plugin_state_t *state = nullptr;
        auto create = [&] {
            plugin = factory->create_plugin(factory, &host, desc->id);
            check(plugin && plugin->init(plugin), "init");
            params = static_cast<const clap_plugin_params_t *>(
                plugin->get_extension(plugin, CLAP_EXT_PARAMS));
            state = static_cast<const clap_plugin_state_t *>(
                plugin->get_extension(plugin, CLAP_EXT_STATE));
            check(params && state, "extensions");
            check(plugin->activate(plugin, 48000, 1, 256) && plugin->start_processing(plugin),
                  "activate");
        };
        create();
        auto destroy = [&] {
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
        };
        clap_input_events_t incoming{nullptr, count, get};
        clap_output_events_t outgoing{nullptr, push};
        float in0[256]{}, in1[256]{}, out0[256]{}, out1[256]{};
        float *ins[] = {in0, in1}, *outs[] = {out0, out1};
        clap_audio_buffer_t input{}, output{};
        input.data32 = ins;
        input.channel_count = 2;
        output.data32 = outs;
        output.channel_count = 2;
        clap_process_t process{};
        process.frames_count = 256;
        process.audio_inputs = &input;
        process.audio_outputs = &output;
        process.audio_inputs_count = process.audio_outputs_count = 1;
        process.in_events = &incoming;
        process.out_events = &outgoing;
        auto block = [&] {
            check(plugin->process(plugin, &process) != CLAP_PROCESS_ERROR, "process");
            process.steady_time += 256;
            for (int i = 0; i < 256; ++i)
                check(std::isfinite(out0[i]) && std::isfinite(out1[i]), "finite output");
        };
        auto load = [&](Stream s) {
            clap_istream_t stream{&s, read};
            check(state->load(plugin, &stream), "state load");
        };
        auto wait = [&](int target) {
            for (int i = 0; i < 5000; ++i) {
                block();
                double status;
                check(params->get_value(plugin, 1, &status), "status parameter");
                if (status == target) {
                    for (int j = 0; j < 180; ++j)
                        block();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("IR loading timed out");
        };
        auto impulse = [&] {
            std::array<std::vector<float>, 2> result;
            for (int b = 0; b < 160; ++b) {
                in0[0] = in1[0] = b == 0 ? .5f : 0.f;
                block();
                result[0].insert(result[0].end(), out0, out0 + 256);
                result[1].insert(result[1].end(), out1, out1 + 256);
            }
            return result;
        };
        auto stereo_check = [&] {
            auto actual = impulse();
            for (int ch = 0; ch < 2; ++ch)
                for (std::size_t i = 0; i < actual[ch].size(); ++i) {
                    double expect = ch == 0 && i == 256 ? .125 : ch == 1 && i == 293 ? -.0625 : 0.;
                    check(std::abs(actual[ch][i] - expect) < 1e-6,
                          "stereo channel assignment or samples");
                }
        };
        load(file_state(dir + "/stereo.wav"));
        wait(2);
        stereo_check();
        Stream saved;
        clap_ostream_t save{&saved, write};
        check(state->save(plugin, &save), "state save");
        load(file_state(dir + "/mono.wav"));
        wait(2);
        auto a = impulse();
        check(std::abs(a[0][256] - .25) < 1e-6 && std::abs(a[0][356] - .0625) < 1e-6 &&
                  a[0] == a[1],
              "mono broadcast");
        load(file_state(dir + "/resample.wav"));
        wait(2);
        double duration;
        params->get_value(plugin, 2, &duration);
        check(std::abs(duration - 1024. / 44100) < .0001, "sample-rate conversion duration");
        a = impulse();
        check(a[0] == a[1], "resampled mono broadcast");
        // Restore into a newly created instance, just as a project reload does.
        destroy();
        create();
        load(saved);
        wait(2);
        stereo_check();
        load(file_state(dir + "/bad.wav"));
        wait(3);
        stereo_check();
        Stream afterError;
        clap_ostream_t errorSave{&afterError, write};
        check(state->save(plugin, &errorSave), "state after error");
        check(afterError.data == saved.data, "failed file must preserve saved good path");
        // A host sample-rate change reconstructs the graph and reloads its saved IR.
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        check(plugin->activate(plugin, 96000, 1, 256) && plugin->start_processing(plugin),
              "96k activate");
        wait(2);
        params->get_value(plugin, 2, &duration);
        check(std::abs(duration - 1024. / 48000) < .0001, "rate change preserves IR duration");
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        check(plugin->activate(plugin, 48000, 1, 256) && plugin->start_processing(plugin),
              "48k reactivate");
        wait(2);
        stereo_check();
        // Latest request wins under rapid repeated file selection.
        for (int i = 0; i < 20; ++i)
            load(file_state(dir + (i % 2 ? "/stereo.wav" : "/mono.wav")));
        wait(2);
        stereo_check();
        load(file_state(""));
        wait(0);
        a = impulse();
        check(std::abs(a[0][256] - .0625) < 1e-6, "return to built-in IR");
        if (argc > 3) {
            const auto *gui =
                static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
            check(gui && gui->create(plugin, CLAP_WINDOW_API_X11, true) && gui->show(plugin),
                  "GUI create/show");
            const auto *timer = static_cast<const clap_plugin_timer_support_t *>(
                plugin->get_extension(plugin, CLAP_EXT_TIMER_SUPPORT));
            check(timer, "GUI timer");
            for (int i = 0; i < (std::getenv("HEAVYVERB_UI_TEST_TICKS")
                                     ? std::atoi(std::getenv("HEAVYVERB_UI_TEST_TICKS"))
                                     : 1500);
                 ++i) {
                timer->on_timer(plugin, 1);
                plugin->on_main_thread(plugin);
                block();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            gui->hide(plugin);
            gui->destroy(plugin);
            if (const char *expected = std::getenv("HEAVYVERB_UI_EXPECT_PATH")) {
                wait(2);
                Stream selected;
                clap_ostream_t selectedSave{&selected, write};
                check(state->save(plugin, &selectedSave), "GUI state save");
                check(std::search(selected.data.begin(), selected.data.end(), expected,
                                  expected + std::strlen(expected)) != selected.data.end(),
                      "GUI-selected path persisted");
                auto rendered = impulse();
                check(std::abs(rendered[0][256] - .25) < 1e-6 &&
                          std::abs(rendered[0][356] - .0625) < 1e-6 && rendered[0] == rendered[1],
                      "GUI-selected mono WAV rendered");
                std::puts("PASS: file-picker selection persisted and rendered the chosen WAV");
            }
        }
        destroy();
        entry->deinit();
        dlclose(lib);
        std::puts("PASS: maker, mono/stereo WAV, resampling, fresh-instance state restore, "
                  "bad-file fallback, latest-request handoff, built-in restore");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
