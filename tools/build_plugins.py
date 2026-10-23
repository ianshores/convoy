#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Export a Pd effect and build Linux CLAP/VST3 with a caller-supplied DPF checkout."""
import argparse
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
DPF_REVISION = "4238e1c7f0351bbe488d79f0899c540543ac7583"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("patch", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--dpf", type=Path, required=True)
parser.add_argument("--fft", choices=("custom",), default="custom", help="Custom FFT (sole backend; retained for command compatibility)")
args = parser.parse_args()
dpf = args.dpf.resolve()
revision = subprocess.check_output(["git", "-C", str(dpf), "rev-parse", "HEAD"], text=True).strip()
if revision != DPF_REVISION:
    parser.error(f"tested DPF revision is {DPF_REVISION}; got {revision}")
output = args.output.resolve()
subprocess.run([sys.executable, str(ROOT / "tools/export.py"), str(args.patch), str(output), "--generators", "c", "dpf", "wwise"], check=True)
link = output / "dpf"
if link.exists() or link.is_symlink():
    if link.resolve() != dpf: parser.error("output/dpf already points to a different checkout")
else: link.symlink_to(dpf, target_is_directory=True)
makefile = output / "plugin/source/Makefile"
# DPF enables fast-math by default. Match the conservative numerical policy
# used by the engine tests, including finite-value validation.
with makefile.open("a") as stream:
    stream.write("\nBUILD_C_FLAGS += -fno-fast-math\nBUILD_CXX_FLAGS += -fno-fast-math\n")
# A backend switch in an existing output must rebuild all objects.
subprocess.run(["make", "-C", str(output / "plugin/source"), "clean"], check=True)
subprocess.run(["make", "-C", str(output / "plugin/source"), "clap", "vst3", "-j2"], check=True)
print(f"Built plugins in {output / 'bin'}")
