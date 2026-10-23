#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and render an exported stereo graph against an independent reference."""
import argparse
import ctypes as C
import json
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--portable", action="store_true", help="Use a separate custom test output directory")
args = parser.parse_args()
BUILD = ROOT / ("build/export-test-custom" if args.portable else "build/export-test")
BUILD.mkdir(parents=True, exist_ok=True)
rng = random.Random(9901)
ir = [rng.uniform(-0.05, 0.05) for _ in range(1597)]
# Keep the example's gains/routing but replace its tiny IR with a dense long IR.
patch = (ROOT / "examples/export-test.pd").read_text()
patch = patch.replace("array ir 8", f"array ir {len(ir)}")
patch = patch.replace("#A 0 1 0.5 0 -0.25 0.125 0 0 0.0625;", "#A 0 " + " ".join(map(str, ir)) + ";")
patch = patch.replace("#X connect 0 0 1 0;", "#X obj 420 180 r IR_Tail_Cutoff_dB @hv_param -144 0 0;\n#X connect 10 0 2 1;\n#X connect 10 0 5 1;\n#X connect 0 0 1 0;")
(BUILD / "test.pd").write_text(patch)
subprocess.run([sys.executable, str(ROOT / "tools/export.py"), str(BUILD / "test.pd"), str(BUILD), "--generators", "c", "dpf", "wwise"], check=True)
src = BUILD / "c"
libpath = BUILD / "libtest.so"
objects = []
for source in src.glob("*.c"):
    obj = BUILD / (source.stem + ".o")
    subprocess.run(["cc", "-std=c99", "-O2", "-fPIC", "-DHV_SIMD_NONE=1", "-c", str(source), "-o", str(obj)], check=True)
    objects.append(str(obj))
subprocess.run(["c++", "-std=c++17", "-O2", "-shared", "-fPIC", "-DHV_SIMD_NONE=1", "-DPFFFT_STATIC_DEFINE",
                *(["-DHEAVYVERB_PORTABLE_FFT"] if args.portable else []),
                *map(str, src.glob("*.cpp")), *objects, "-o", str(libpath)], check=True)
lib = C.CDLL(str(libpath))
fp = C.POINTER(C.c_float)
lib.hv_HeavyVerbDemo_new.argtypes = [C.c_double]
lib.hv_HeavyVerbDemo_new.restype = C.c_void_p
lib.hv_HeavyVerbDemo_free.argtypes = [C.c_void_p]
lib.hv_process.argtypes = [C.c_void_p, C.POINTER(fp), C.POINTER(fp), C.c_int]
lib.hv_process.restype = C.c_int
signal = [rng.uniform(-0.2, 0.2) for _ in range(701)]
expected = [0.] * (len(signal) + len(ir) + 64)
for i, v in enumerate(signal):
    for j, h in enumerate(ir):
        expected[i + j + 64] += v * h * 0.125
source = signal + [0.] * (len(expected) - len(signal))
results = []
for rate in (44100, 48000, 96000):
    for block in (1, 7, 127, 256, 1023):
        ctx = lib.hv_HeavyVerbDemo_new(rate)
        assert ctx
        rendered = [[], []]
        try:
            for start in range(0, len(source), block):
                samples = source[start:start + block]
                n = len(samples)
                Array = C.c_float * n
                ins = [Array(*samples), Array(*samples)]
                outs = [Array(), Array()]
                assert lib.hv_process(ctx, (fp * 2)(*ins), (fp * 2)(*outs), n) == n
                for channel in range(2): rendered[channel].extend(outs[channel])
        finally:
            lib.hv_HeavyVerbDemo_free(ctx)
        error = max(abs(rendered[ch][i] - v * (1 if ch == 0 else -1))
                    for ch in range(2) for i, v in enumerate(expected))
        assert error < 1e-5, (rate, block, error)
        results.append({"sample_rate": rate, "callback": block, "max_error": error})
# Exercise the actual exported parameter receiver and full-history restoration.
lib.hv_sendFloatToReceiver.argtypes = [C.c_void_p, C.c_uint32, C.c_float]
lib.hv_sendFloatToReceiver.restype = C.c_bool
lib.hv_stringToHash.argtypes = [C.c_char_p]
lib.hv_stringToHash.restype = C.c_uint32
parameter = lib.hv_stringToHash(b"IR_Tail_Cutoff_dB")
cutoff_results = []
for rate in (44100, 48000, 96000):
    ctx = lib.hv_HeavyVerbDemo_new(rate)
    def render(values):
        result = [[], []]
        for start in range(0, len(values), 127):
            samples = values[start:start+127]; n = len(samples); Array = C.c_float*n
            ins = [Array(*samples), Array(*samples)]; outs = [Array(),Array()]
            assert lib.hv_process(ctx,(fp*2)(*ins),(fp*2)(*outs),n)==n
            for ch in range(2): result[ch].extend(outs[ch])
        return result
    try:
        for db, retained in ((-1.,1024),(0.,len(ir)),(-96.,len(ir))):
            assert lib.hv_sendFloatToReceiver(ctx,parameter,db)
            render([0.]*8192)  # settle gains and flush existing history
            actual = render(source)
            expected_cut = [0.]*len(source)
            for i,v in enumerate(signal):
                for j,h in enumerate(ir[:retained]): expected_cut[i+j+64] += v*h*.125
            error = max(abs(actual[ch][i]-v*(1 if ch==0 else -1)) for ch in range(2) for i,v in enumerate(expected_cut))
            assert error<1e-5,(rate,db,error)
            cutoff_results.append({"sample_rate":rate,"cutoff_db":db,"max_error":error})
    finally: lib.hv_HeavyVerbDemo_free(ctx)
(BUILD / "cutoff-results.json").write_text(json.dumps(cutoff_results,indent=2)+"\n")
print("PASS: nine exported cutoff/restore stereo renders via actual parameter receiver")
# Test errors are caught during export rather than crashing generated audio.
for name, bad in (("missing", patch.replace("heavyverb~ ir", "heavyverb~ missing")),
                  ("partition", patch.replace("heavyverb~ ir 64", "heavyverb~ ir 63"))):
    path = BUILD / (name + ".pd")
    path.write_text(bad)
    result = subprocess.run([sys.executable, str(ROOT / "tools/export.py"), str(path), str(BUILD / name)], capture_output=True, text=True)
    assert result.returncode != 0, name
    assert "heavyverb~" in result.stderr, result.stderr
(BUILD / "audio-results.json").write_text(json.dumps(results, indent=2) + "\n")
print(f"PASS (custom FFT): 15 stereo exported graph renders, dense 1597-tap IR; max error {max(r['max_error'] for r in results):.3g}; invalid IR/partition rejected")
