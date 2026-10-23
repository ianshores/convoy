#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run sequential callback, synthetic load, and quiet-signal diagnostics."""
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/performance/fft-optimization/runtime"
OUTPUT.mkdir(exist_ok=True)
BUILD = ROOT / "build/fft-optimization/runtime"
BUILD.mkdir(exist_ok=True)
metadata = {
    "cpu": next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")),
    "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
    "platform": platform.platform(),
    "logical_cpu": 1,
    "priority": "ordinary",
    "headers": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in (ROOT / "include").glob("*.hpp")},
    "executions": [],
}
for backend in ("custom", "pffft"):
    flags = ["-O3", "-std=c++17", "-I" + str(ROOT / "include")]
    link = []
    if backend == "custom":
        flags.append("-DHEAVYVERB_PORTABLE_FFT")
    else:
        flags.extend(["-I" + str(ROOT / "third_party/pffft"), "-DPFFFT_STATIC_DEFINE"])
        link = [str(ROOT / "build/pffft.o"), str(ROOT / "build/pffft_common.o")]
    for diagnostic in ("timing", "load", "quiet"):
        executable = BUILD / (backend + "-" + diagnostic)
        command = ["c++", *flags, str(ROOT / "tests" / (diagnostic + ".cpp")), *link, "-o", str(executable)]
        subprocess.run(command, check=True)
        result = subprocess.run(["taskset", "-c", "1", str(executable)], capture_output=True, text=True, check=True)
        extension = "json" if diagnostic == "timing" else "csv"
        (OUTPUT / (backend + "-" + diagnostic + "." + extension)).write_text(result.stdout)
        metadata["executions"].append({"backend": backend, "diagnostic": diagnostic, "compile_command": command})
        print(backend, diagnostic, "complete", flush=True)
(OUTPUT / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
