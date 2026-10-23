#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install the stereo demo's file-loading host around its generated Heavy graph."""
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
def configure(output):
    source=output/'plugin/source'
    for name in ['HeavyDPF_HeavyVerbDemo.cpp','HeavyDPF_HeavyVerbDemo.hpp']:
        (source/name).unlink()
    for path in (ROOT/'plugin').iterdir():
        if path.suffix in {'.cpp','.hpp'}:shutil.copy2(path,source/path.name)
    info=source/'DistrhoPluginInfo.h';text=info.read_text()
    text=text.replace('#define DISTRHO_PLUGIN_HAS_UI               0','#define DISTRHO_PLUGIN_HAS_UI               1')
    # Override generated defaults after their definitions, before framework use.
    text+='\n#undef DISTRHO_PLUGIN_HAS_UI\n#define DISTRHO_PLUGIN_HAS_UI 1\n#undef DISTRHO_PLUGIN_WANT_STATE\n#define DISTRHO_PLUGIN_WANT_STATE 1\n#undef DISTRHO_PLUGIN_WANT_FULL_STATE\n#define DISTRHO_PLUGIN_WANT_FULL_STATE 1\n#define DISTRHO_UI_FILE_BROWSER 1\n#define DISTRHO_UI_USE_NANOVG 1\n'
    info.write_text(text)
    make=source/'Makefile';text=make.read_text().replace('FILES_DSP = $(wildcard *.cpp)','FILES_DSP = $(filter-out HeavyVerbUI.cpp,$(wildcard *.cpp))\nFILES_UI = HeavyVerbUI.cpp\nUSE_FILE_BROWSER = true')
    text+='\nBUILD_CXX_FLAGS += $(shell pkg-config --cflags sndfile samplerate)\nLINK_FLAGS += $(shell pkg-config --libs sndfile samplerate)\n'
    make.write_text(text)
