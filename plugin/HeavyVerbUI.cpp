// SPDX-License-Identifier: MIT
#include "DistrhoUI.hpp"
#include <cmath>
#include <cstdio>
#include <string>
START_NAMESPACE_DISTRHO
class HeavyVerbUI : public UI {
    std::string path_;
    float cutoff_ = 0, seconds_ = .5f;
    int status_ = 0;
    bool dragging_ = false;
    void label(float x, float y, const char *text, float size = 16) {
        fontSize(size);
        fillColor(225, 229, 235);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        this->text(x, y, text, nullptr);
    }
    void button(float x, float w, const char *text) {
        beginPath();
        roundedRect(x, 94, w, 38, 6);
        fillColor(49, 65, 83);
        fill();
        label(x + 14, 104, text);
    }
    void setCutoff(float x) {
        cutoff_ = std::round(std::max(0.f, std::min(1.f, (x - 28) / 484.f)) * 144.f) - 144.f;
        setParameterValue(0, cutoff_);
        repaint();
    }

  public:
    HeavyVerbUI() : UI(560, 290) {
        loadSharedResources();
        setGeometryConstraints(560, 290, false);
    }

  protected:
    void parameterChanged(uint32_t index, float value) override {
        if (index == 0)
            cutoff_ = value;
        else if (index == 1)
            status_ = int(value);
        else
            seconds_ = value;
        repaint();
    }
    void stateChanged(const char *key, const char *value) override {
        if (std::string(key) == "ir_file")
            path_ = value;
        repaint();
    }
    void uiFileBrowserSelected(const char *filename) override {
        if (filename && *filename) {
            path_ = filename;
            status_ = 1;
            setState("ir_file", filename);
            repaint();
        }
    }
    void onNanoDisplay() override {
        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fillColor(22, 27, 34);
        fill();
        label(28, 22, "Heavyverb", 27);
        label(405, 30, "Ian Shores", 15);
        std::string name =
            path_.empty() ? "Built-in demo IR" : path_.substr(path_.find_last_of("/\\") + 1);
        if (name.size() > 60)
            name = name.substr(0, 57) + "...";
        label(28, 65, name.c_str());
        button(28, 184, "Load WAV...");
        button(226, 174, "Use demo IR");
        const char *state = status_ == 1   ? "Loading..."
                            : status_ == 3 ? "Could not load WAV. Previous IR remains active."
                            : status_ == 0 ? "Built-in demo response"
                                           : "WAV ready";
        label(28, 146, state, 14);
        char value[96];
        std::snprintf(value, sizeof(value), "Tail cutoff: %s",
                      cutoff_ == 0 ? "Off — full response" : "");
        if (cutoff_ != 0)
            std::snprintf(value, sizeof(value), "Tail cutoff: %.0f dB", cutoff_);
        label(28, 181, value, 16);
        beginPath();
        roundedRect(28, 218, 484, 6, 3);
        fillColor(63, 74, 88);
        fill();
        beginPath();
        circle(28 + (cutoff_ + 144) / 144 * 484, 221, 8);
        fillColor(124, 191, 210);
        fill();
        std::snprintf(value, sizeof(value),
                      "%.2f s  |  Wet only  |  Mono or stereo WAV, up to 30 s", seconds_);
        label(28, 252, value, 13);
    }
    bool onMouse(const MouseEvent &e) override {
        if (e.button != 1)
            return false;
        if (!e.press && dragging_) {
            dragging_ = false;
            editParameter(0, false);
            return true;
        }
        if (!e.press)
            return false;
        if (e.pos.getY() >= 94 && e.pos.getY() < 132) {
            if (e.pos.getX() >= 28 && e.pos.getX() < 212) {
                openFileBrowser();
                return true;
            }
            if (e.pos.getX() >= 226 && e.pos.getX() < 400) {
                setState("ir_file", "");
                path_.clear();
                status_ = 1;
                repaint();
                return true;
            }
        }
        if (e.pos.getY() >= 207 && e.pos.getY() < 236) {
            dragging_ = true;
            editParameter(0, true);
            setCutoff(e.pos.getX());
            return true;
        }
        return false;
    }
    bool onMotion(const MotionEvent &e) override {
        if (!dragging_)
            return false;
        setCutoff(e.pos.getX());
        return true;
    }
};
UI *createUI() { return new HeavyVerbUI; }
END_NAMESPACE_DISTRHO
