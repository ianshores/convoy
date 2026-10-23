#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Render the actual Pd external headlessly; compare wet output with dry input."""
import argparse
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("pd", help="Path to the Pure Data executable")
args = parser.parse_args()
build = ROOT / "build/pd-test"
build.mkdir(parents=True, exist_ok=True)
output = build / "render.wav"
output.unlink(missing_ok=True)
# Keep the output message relative so directories with spaces are supported.
# Capture into arrays and write synchronously after both arrays have filled.
# writesf~ uses a background thread, which an accelerated batch run can outrun.
patch = r"""#N canvas 0 0 600 500 10;
#X obj 30 30 loadbang;
#X msg 30 60 \; pd dsp 1;
#X obj 170 60 delay 10;
#X obj 170 200 tabwrite~ dry;
#X obj 300 200 tabwrite~ wet;
#X obj 300 60 delay 20;
#X obj 300 100 tabplay~ source;
#X obj 300 140 heavyverb~ ir 64;
#X obj 30 250 delay 100;
#X msg 30 280 write -wave -bytes 4 render.wav dry wet;
#X obj 30 310 soundfiler;
#X obj 30 350 delay 150;
#X msg 30 380 \; pd quit;
#X obj 450 250 array define dry 4096;
#X obj 450 280 array define wet 4096;
#N canvas 0 0 450 300 (subpatch) 0;
#X array source 1 float 3;
#A 0 0.5;
#X restore 450 100 graph;
#N canvas 0 0 450 300 (subpatch) 0;
#X array ir 1597 float 3;
#A 0 1 0.5 0 -0.25 0.125 0 0 0.0625;
#A 511 0.25 -0.125 0.0625;
#A 1023 -0.125 0.0625;
#A 1596 0.03125;
#X restore 450 30 graph;
#X connect 0 0 1 0;
#X connect 0 0 2 0;
#X connect 0 0 5 0;
#X connect 0 0 8 0;
#X connect 0 0 11 0;
#X connect 2 0 3 0;
#X connect 2 0 4 0;
#X connect 5 0 6 0;
#X connect 6 0 7 0;
#X connect 6 0 3 0;
#X connect 7 0 4 0;
#X connect 8 0 9 0;
#X connect 9 0 10 0;
#X connect 11 0 12 0;
"""
(build / "test.pd").write_text(patch)
result = subprocess.run([args.pd, "-nogui", "-batch", "-noaudio", "-r", "48000", "-path", str(ROOT / "build"), str(build / "test.pd")], cwd=build, capture_output=True, text=True, timeout=20)
assert result.returncode == 0, result.stderr
assert "error" not in result.stderr.lower() and "couldn't create" not in result.stderr, result.stderr
data = output.read_bytes()
assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
pos = 12
while data[pos:pos+4] != b"data":
    size = struct.unpack_from("<I", data, pos+4)[0]
    pos += 8 + size + (size & 1)
size = struct.unpack_from("<I", data, pos+4)[0]
samples = struct.unpack_from("<" + "f" * (size // 4), data, pos+8)
dry, wet = samples[::2], samples[1::2]
first = max(range(len(dry)), key=lambda i: abs(dry[i]))
expected = [0.] * len(wet)
# Include coefficients on both sides of the 512-sample early/tail split.
taps = {0:1, 1:.5, 3:-.25, 4:.125, 7:.0625, 511:.25, 512:-.125,
        513:.0625, 1023:-.125, 1024:.0625, 1596:.03125}
for j, h in taps.items(): expected[first + 64 + j] = dry[first] * h
error = max(abs(a-b) for a,b in zip(wet,expected))
assert abs(dry[first]) > 0.1 and error < 1e-6, error
print(f"PASS: native Pd external rendered matching early and late response; error {error:.3g}")
