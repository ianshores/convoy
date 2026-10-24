#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test and record one FFT experiment without timing compilation or IR setup.

Use a new label for each experiment. Results contain the complete flags,
source hashes, correctness output, component timings, and unfiltered CSVs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label")
    parser.add_argument("--output-root", default="docs/performance/fft-optimization")
    backend = parser.add_mutually_exclusive_group()
    backend.add_argument("--baseline", action="store_true")
    backend.add_argument("--pffft", action="store_true")
    parser.add_argument("--production", action="store_true", help="Use production headers rather than preserved experiment variants")
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=3)
    parser.add_argument("--cxx-flag", action="append", default=[])
    parser.add_argument("--c-flag", action="append", default=[])
    args = parser.parse_args()
    if args.pffft and not (ROOT / "third_party/pffft").exists():
        parser.error("PFFFT comparison requires verification commit 69f7962; current builds use the custom FFT")
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.label):
        parser.error("label must contain only letters, digits, underscores or hyphens")
    if args.runs < 0 or args.seconds < 1:
        parser.error("runs must be nonnegative and seconds must be positive")
    output = ROOT / args.output_root / args.label
    if output.exists():
        parser.error("that label already exists; use a new label to preserve the log")
    output.mkdir(parents=True)
    build = ROOT / "build/fft-optimization" / args.label
    include = build / "include"
    sources = build / "tests"
    sources.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "tests").glob("*.cpp"):
        shutil.copy2(source, sources / source.name)
    shutil.copytree(ROOT / "include", include, dirs_exist_ok=True)
    # Keep historical experiment identifiers while checking the same current sources.
    # This isolated build copy lets preserved pre-rename reference engines remain usable.
    for source in sources.glob('*.cpp'):
        source.write_text(source.read_text().replace('convoy', 'heavyverb').replace('CONVOY', 'HEAVYVERB'))
    for stem in ('convoy', 'convoy_fft', 'convoy_portable'):
        canonical = ROOT / 'include' / (stem + '.hpp')
        (include / (stem.replace('convoy', 'heavyverb') + '.hpp')).write_text(
            canonical.read_text().replace('convoy', 'heavyverb').replace('CONVOY', 'HEAVYVERB'))
    if args.baseline:
        shutil.copy2(ROOT / "tests/reference/original_portable.hpp", include / "heavyverb_portable.hpp")
        shutil.copy2(ROOT / "tests/reference/original.hpp", include / "heavyverb.hpp")
    elif not args.pffft and not args.production:
        for source,target in (("fft_experiments.hpp","heavyverb_fft.hpp"),("portable_experiments.hpp","heavyverb_portable.hpp"),("wrapper_experiments.hpp","heavyverb.hpp")):
            shutil.copy2(ROOT / "tests/reference" / source, include / target)
    if not args.baseline and not args.pffft and not args.production and not any(d.startswith("HEAVYVERB_FFT_STEP=") for d in args.define):
        step = re.search(r"#define HEAVYVERB_FFT_STEP (\d+)", (include / "heavyverb_fft.hpp").read_text())[1]
        args.define.append("HEAVYVERB_FFT_STEP=" + step)
    flags = ["-O3", "-std=c++17", "-Wall", "-Wextra", *args.cxx_flag]
    if not args.pffft:
        flags.append("-DHEAVYVERB_PORTABLE_FFT")
    flags.extend("-D" + value for value in args.define)
    flags.append("-I" + str(include))
    link = []
    if args.pffft:
        run(["make", "build/pffft.o", "build/pffft_common.o"], cwd=ROOT)
        flags.extend(["-I" + str(ROOT / "third_party/pffft"), "-DPFFFT_STATIC_DEFINE"])
        link = [str(ROOT / "build/pffft.o"), str(ROOT / "build/pffft_common.o")]
        if args.c_flag:
            link = []
            for name in ("pffft", "pffft_common"):
                obj = build / (name + ".o")
                run(["cc", "-O3", "-std=c99", "-fPIC", "-DPFFFT_STATIC_DEFINE",
                     *args.c_flag, "-I" + str(ROOT / "third_party/pffft"), "-c",
                     str(ROOT / "third_party/pffft" / (name + ".c")), "-o", str(obj)])
                link.append(str(obj))

    metadata = {
        "label": args.label,
        "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "cpu": next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")),
        "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
        "flags": flags,
        "link_objects": link,
        "extra_c_flags": args.c_flag,
        "link_object_sha256": {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in link},
        "platform": platform.platform(),
        "logical_cpu": 1,
        "sample_rate": 48000,
        "head_partition": 256,
        "tail_partition": min(8192, 256 * next((int(d.split("=", 1)[1]) for d in args.define if d.startswith("HEAVYVERB_TAIL_FACTOR=")), 8)),
        "latency_samples": 256,
        "baseline": args.baseline,
        "pffft": args.pffft,
        "definitions": args.define,
        "headers": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(include.glob("*.hpp"))},
        "sources": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources.glob("*.cpp")},
        "measured_audio_seconds": args.seconds,
        "runs": args.runs,
        "method": "Seeded dense IR/input, IR-length warmup, elapsed processing / audio duration as percent of one core; ordinary priority; no silence shortcut. Runs are sequential, never concurrent.",
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")

    def compile_source(name, source, extra=()):
        run(["c++", *flags, *extra, str(sources / source), *link, "-o", str(build / name)])

    def capture(name):
        result = subprocess.run(["taskset", "-c", "1", str(build / name)], capture_output=True, text=True)
        (output / (name + ".txt")).write_text(result.stdout + result.stderr)
        result.check_returncode()
        print(result.stdout, flush=True)

    compile_source("correctness", "core.cpp")
    capture("correctness")
    compile_source("benchmark", "benchmark.cpp")
    if args.baseline:
        # Expose the unchanged private FFT only in this diagnostic copy.
        source = (include / "heavyverb_portable.hpp").read_text()
        (include / "profile_original.hpp").write_text(source.replace("class PartitionedConvolver {", "class PartitionedConvolver {\npublic:", 1))
        compile_source("components", "original_fft_benchmark.cpp")
        capture("components")
    elif not args.pffft:
        compile_source("fft-correctness", "fft.cpp")
        capture("fft-correctness")
        if "void forward_half(" in (include / "heavyverb_fft.hpp").read_text():
            compile_source("fft-half-correctness", "fft.cpp", ["-DHEAVYVERB_TEST_HALF_FORWARD"])
            capture("fft-half-correctness")
        if "void forward_half_packed(" in (include / "heavyverb_fft.hpp").read_text():
            packed_flags = ["-DHEAVYVERB_TEST_PACKED"]
            if re.search(r"spectrum\s*\+\s*8", (include / "heavyverb_fft.hpp").read_text()):
                packed_flags.append("-DHEAVYVERB_TEST_PACKED8")
            compile_source("fft-packed-correctness", "fft.cpp", packed_flags)
            capture("fft-packed-correctness")
        compile_source("components", "fft_benchmark.cpp")
        capture("components")
    elif args.pffft:
        compile_source("components", "pffft_components.cpp")
        capture("components")
    environment = dict(os.environ, HEAVYVERB_BENCH_SECONDS=str(args.seconds))
    for trial in range(1, args.runs + 1):
        result = run(["taskset", "-c", "1", str(build / "benchmark")], env=environment, capture_output=True)
        (output / f"run-{trial}.csv").write_text(result.stdout)
        print(args.label, "trial", trial, flush=True)


if __name__ == "__main__":
    main()
